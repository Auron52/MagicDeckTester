#!/usr/bin/env node
// MagicDeckTester — Play GUI bridge
// =================================================================================
// A thin, dependency-free local bridge between the browser play-GUI (index.html) and
// the simulator's existing stateless --claude-play protocol. A human plays a game one
// decision at a time; each move re-invokes the binary with the accumulated --choices
// CSV, which deterministically replays the game and emits the NEXT decision (exit 70)
// or the final result (exit 0).
//
// ARCHITECTURE SEAM (important): the browser <-> engine contract is the decision-JSON
// protocol — *identical* whether the engine is reached by this subprocess bridge (today,
// no toolchain needed) or by an in-browser WebAssembly module later (decide(deck, seed,
// gi, choices) -> json). Swapping the transport does not change the UI. See README.md.
//
// Run:   node tools/play/server.js          (then open http://localhost:8080)
// Env:   PORT (default 8080), MTG_BIN (default ./build/Release/mtg)
//
// NOTE: this is a single-user LOCAL dev tool. It binds to 127.0.0.1 and shells out to a
// local binary; do not expose it to a network.

const http = require('http');
const fs = require('fs');
const path = require('path');
const { spawnSync, spawn } = require('child_process');

const ROOT = path.resolve(__dirname, '..', '..');          // repo root
const DECKS_DIR = path.join(ROOT, 'decks');
const CARDS_JSON = path.join(ROOT, 'src', 'cards', 'data', 'cards.json');
// Engine binary. Honours $MTG_BIN, else probes the multi-config layout for BOTH names:
// build/Release/mtg on Linux/macOS, build/Release/mtg.exe on Windows/MSVC. This mirrors the
// two-candidate probe every other entry point already does (test/lib/harness.sh:find_engine,
// scripts/analyze_deck.py's EXE_SUFFIX, scripts/play_invariants.py) -- this file was the one
// place that hard-coded the Unix name, so the viewer could not find the engine on Windows at
// all. Falls back to the plain name so the "MISSING" message below names a sensible path.
function resolveBin() {
  if (process.env.MTG_BIN) return process.env.MTG_BIN;
  const dir = path.join(ROOT, 'build', 'Release');
  for (const name of ['mtg', 'mtg.exe']) {
    const p = path.join(dir, name);
    if (fs.existsSync(p)) return p;
  }
  return path.join(dir, process.platform === 'win32' ? 'mtg.exe' : 'mtg');
}
const BIN = resolveBin();
const PORT = parseInt(process.env.PORT || '8080', 10);
// Bind host: defaults to loopback (single-user local tool), but PLAY_HOST=0.0.0.0 lets a
// devcontainer/WSL forward the port to the host browser (a 127.0.0.1 bind inside a container
// often can't be auto-forwarded).
const HOST = process.env.PLAY_HOST || '127.0.0.1';
const STEP_TIMEOUT_MS = 120000;   // generous: late-turn replays + opponent AI
const HINT_DEPTH = parseInt(process.env.HINT_DEPTH || '5', 10);   // deep-search depth for async AI hints

// ---- helpers ---------------------------------------------------------------------

function sendJson(res, code, obj) {
  const body = JSON.stringify(obj);
  res.writeHead(code, { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(body) });
  res.end(body);
}

function readBody(req) {
  return new Promise((resolve, reject) => {
    let data = '';
    req.on('data', (c) => { data += c; if (data.length > 1e6) req.destroy(); });
    req.on('end', () => { try { resolve(data ? JSON.parse(data) : {}); } catch (e) { reject(e); } });
    req.on('error', reject);
  });
}

function extractBlock(text, startMarker, endMarker) {
  const s = text.indexOf(startMarker);
  if (s < 0) return null;
  const e = text.indexOf(endMarker, s + startMarker.length);
  if (e < 0) return null;
  return text.slice(s + startMarker.length, e).trim();
}

// Resolve a user-supplied deck name to a safe path inside its per-deck folder plus the sibling
// profile. Per-deck folder layout: decks/<stem>/<stem>.{txt,cod} + decks/<stem>/<stem>.profile.json
// (docs/design/per-deck-folder-layout.md). The folder name equals the decklist stem.
function resolveDeck(deckName) {
  if (typeof deckName !== 'string' || !deckName) throw new Error('deck required');
  const base = path.basename(deckName);                    // strip any path components
  const stem = base.replace(/\.[^.]+$/, '');
  const dir = path.join(DECKS_DIR, stem);                  // per-deck folder
  const deckPath = path.join(dir, base);
  if (path.dirname(deckPath) !== dir) throw new Error('deck must be under decks/<name>/');
  if (!fs.existsSync(deckPath)) throw new Error('deck not found: ' + base);
  const profilePath = path.join(dir, stem + '.profile.json');
  // The exhaustive keep/bottom sidecar (mulligan TABLE). Its presence gates whether the viewer's
  // pre-game keep/bottom suggestions come from the table (see runStep) rather than the live heuristic.
  const sidecarPath = path.join(dir, stem + '.keepmodel.exhaustive.profile.json.gz');
  return { deckPath, profilePath: fs.existsSync(profilePath) ? profilePath : null, stem,
           hasSidecar: fs.existsSync(sidecarPath) };
}

function intParam(v, dflt) {
  const n = parseInt(v, 10);
  return Number.isFinite(n) ? n : dflt;
}

// Build the argv for one --claude-play invocation.
// validateLine (optional): an encoded human-assembled line ("land=X;cast=Y;...") to reconcile
// against the model at the first un-chosen main phase instead of dumping the plan menu.
// exhaustiveKeep (optional): pass --exhaustive-keep so the engine loads the deck's mulligan-table
// sidecar and emits table-based keep/bottom suggestions. Used ONLY for the pre-game keep/bottom
// steps (see runStep) — the sidecar parse is expensive (up to tens of seconds on big decks), so it
// must never be on the per-turn hot path. This is the VIEWER opting in; default --claude-play (the
// engine-test sweep/oracle) deliberately stays table-less (see main.cpp AttachExhaustiveSidecar).
function buildArgs(p, logDir, validateLine, exhaustiveKeep) {
  const { deckPath, profilePath } = resolveDeck(p.deck);
  const args = [deckPath];
  if (profilePath) args.push('--profile', profilePath);
  args.push('--cards-json', CARDS_JSON);
  args.push('--claude-play');
  if (exhaustiveKeep) args.push('--exhaustive-keep');
  args.push('--seed', String(intParam(p.seed, 1)));
  args.push('--game-index', String(intParam(p.gameIndex, 0)));
  args.push('--max-turns', String(intParam(p.maxTurns, 8)));
  // depth 0 is REQUIRED for human play: at depth > 0 the engine enables lookahead-bottoming
  // and mulligan/lookahead rollouts replay whole games through the SAME external chooser, so the
  // human would be asked to play hypothetical rollout games (the chooser must drive the real
  // game only). Keep it 0 unless you understand that interaction.
  args.push('--depth', String(intParam(p.depth, 0)));
  // No clairvoyance: human play is deliberately played WITHOUT --reveal so a human win-turn is a
  // legitimate no-foresight ground-truth bound the AI should be able to match (see README).
  if (logDir) args.push('--log-dir', logDir);
  args.push('--choices', Array.isArray(p.choices) ? p.choices.join(',') : '');
  // #4 firebreathe-amount side-channel: p.firebreathe is a { turn: count } map of the human's picks,
  // passed as "turn:count,..." (turn-keyed, NEVER a --choices slot -> existing references unaffected).
  // --firebreathe-prompt makes the engine emit a firebreathe decision (exit 70) for any combat turn not
  // yet answered, so the viewer can surface the modal. A fully-answered map simply never prompts.
  if (p.firebreathe && typeof p.firebreathe === 'object') {
    const pairs = Object.keys(p.firebreathe).map(t => `${t}:${p.firebreathe[t]}`);
    if (pairs.length) args.push('--firebreathe', pairs.join(','));
  }
  // Umezawa's Jitte counter-spend side-channel: same turn-keyed shape as firebreathe.
  if (p.jitte && typeof p.jitte === 'object') {
    const jpairs = Object.keys(p.jitte).map(t => `${t}:${p.jitte[t]}`);
    if (jpairs.length) args.push('--jitte', jpairs.join(','));
  }
  args.push('--jitte-prompt');
  args.push('--firebreathe-prompt');
  // #10 cast-order side-channel: p.castOrder is a { mainOrdinal: [name, ...] } map of the human's
  // pinned non-sac hand-cast order for that main-phase decision. Passed as "<ord>:A|B|C;..." (pipe-
  // separated names, since MTG names contain ',' but never '|'), keyed by main-phase ordinal — NEVER
  // a --choices slot, so existing references (no --cast-order) replay in canonical order unchanged.
  if (p.castOrder && typeof p.castOrder === 'object') {
    const entries = Object.keys(p.castOrder)
      .filter(k => Array.isArray(p.castOrder[k]) && p.castOrder[k].length)
      .map(k => `${k}:${p.castOrder[k].join('|')}`);
    if (entries.length) args.push('--cast-order', entries.join(';'));
  }
  // #6 storage tap-vs-charge side-channel: p.storageHold is a { "turn:num": 0|1 } map of the human's
  // per-(turn, land) hold answers (1 = hold/charge, 0 = allow tap). Passed as "turn:num:val,..." keyed by
  // (turn, land number) — NEVER a --choices slot, so existing references (no --storage-hold) replay as the
  // burst heuristic. --storage-hold-prompt makes the engine emit a storage_hold decision (exit 70) for any
  // charged storage land not yet answered, so the viewer's modal (which handles it) can surface it; a
  // fully-answered map simply never prompts.
  if (p.storageHold && typeof p.storageHold === 'object') {
    const trips = Object.keys(p.storageHold).map(k => `${k}:${p.storageHold[k] ? 1 : 0}`);
    if (trips.length) args.push('--storage-hold', trips.join(','));
  }
  args.push('--storage-hold-prompt');
  if (validateLine != null) args.push('--validate-line', validateLine);
  return args;
}

// Run the binary once for the given choices (optionally with the mulligan-table sidecar); classify.
function runStepRaw(p, logDir, exhaustiveKeep) {
  const args = buildArgs(p, logDir, null, exhaustiveKeep);
  const r = spawnSync(BIN, args, { cwd: ROOT, encoding: 'utf8', timeout: STEP_TIMEOUT_MS, maxBuffer: 32 * 1024 * 1024 });
  if (r.error) return { kind: 'error', error: String(r.error), args };
  const out = (r.stdout || '') + '\n' + (r.stderr || '');

  const decisionRaw = extractBlock(out, '<<<CLAUDE_DECISION>>>', '<<<END_DECISION>>>');
  if (decisionRaw) {
    try { return { kind: 'decision', decision: JSON.parse(decisionRaw) }; }
    catch (e) { return { kind: 'error', error: 'bad decision JSON: ' + e.message, raw: decisionRaw }; }
  }
  const resultRaw = extractBlock(out, '<<<CLAUDE_RESULT>>>', '<<<END_RESULT>>>');
  if (resultRaw) {
    try { return { kind: 'result', result: JSON.parse(resultRaw) }; }
    catch (e) { return { kind: 'error', error: 'bad result JSON: ' + e.message, raw: resultRaw }; }
  }
  return { kind: 'error', error: 'no decision/result markers in output', raw: out.slice(0, 4000), code: r.status };
}

// Run one step for the given choices. ALWAYS table-LESS (no sidecar) so /api/step NEVER blocks on the
// up-to-tens-of-seconds sidecar parse -- the browser gets the decision instantly. The deck's mulligan
// TABLE recommendation (the keep/mulligan call AND the joint bottom set) is fetched SEPARATELY and
// ASYNCHRONOUSLY via /api/keep-hint (runKeepHint), fired in parallel with the modal, so the human is
// never made to wait for a suggestion that is good-to-have but not required to make the choice.
function runStep(p, logDir) {
  return runStepRaw(p, logDir, false);
}

// Spawn the binary ASYNCHRONOUSLY (child_process.spawn, not spawnSync) so the up-to-tens-of-seconds
// sidecar parse does NOT freeze Node's single-threaded event loop -- otherwise a spawnSync here would
// block EVERY other request (including the fast /api/step for the next mulligan dialog) until it
// returned, which is exactly the "big wait between mulligan dialog 1 and 2" the exhaustive keep-hint
// caused. Resolves to { status, stdout, stderr, error } like spawnSync's return.
function spawnAsyncCollect(bin, args, opts) {
  return new Promise((resolve) => {
    let stdout = '', stderr = '', settled = false;
    const child = spawn(bin, args, { cwd: opts.cwd });
    const done = (r) => { if (settled) return; settled = true; if (timer) clearTimeout(timer); resolve(r); };
    const timer = opts.timeout ? setTimeout(() => { try { child.kill('SIGKILL'); } catch (e) {} done({ status: null, stdout, stderr, error: 'timeout' }); }, opts.timeout) : null;
    child.stdout.on('data', d => { stdout += d; });
    child.stderr.on('data', d => { stderr += d; });
    child.on('error', (err) => done({ status: null, stdout, stderr, error: String(err) }));
    child.on('close', (code) => done({ status: code, stdout, stderr }));
  });
}

// Async MULLIGAN-TABLE hint: re-run the CURRENT pre-game decision (mulligan or bottom) WITH the
// exhaustive keep sidecar (--exhaustive-keep -- the expensive parse) and return the deck's TABLE
// recommendation. The re-run replays the identical game (same seed+choices → same decision); only the
// AI-suggestion metadata differs. This drives BOTH the keep/mulligan call (ai_choice, from the table's
// KeepHand -- so the profile decides whether to mulligan, not just how to bottom) and the joint bottom
// set (ai_set). Uses ASYNC spawn so the sidecar parse never blocks the event loop (see spawnAsyncCollect
// -- a spawnSync here froze the next /api/step). The browser fires it in PARALLEL with the modal, never
// blocking the human (the AI pick is a hint). Table-less decks return hasSidecar:false with NO spawn.
// Viewer-scoped; the engine's default --claude-play stays table-less (see main.cpp AttachExhaustiveSidecar).
async function runKeepHint(p) {
  let hasSidecar = false;
  try { hasSidecar = resolveDeck(p.deck).hasSidecar; } catch (e) { /* table-less */ }
  if (!hasSidecar) return { kind: 'keep-hint', hasSidecar: false };
  const args = buildArgs(p, null, null, true);   // WITH --exhaustive-keep
  const r = await spawnAsyncCollect(BIN, args, { cwd: ROOT, timeout: STEP_TIMEOUT_MS });
  if (r.error) return { kind: 'keep-hint', hasSidecar: true, error: String(r.error) };
  const out = (r.stdout || '') + '\n' + (r.stderr || '');
  const decisionRaw = extractBlock(out, '<<<CLAUDE_DECISION>>>', '<<<END_DECISION>>>');
  if (!decisionRaw) return { kind: 'keep-hint', hasSidecar: true, error: 'no decision markers' };
  let d;
  try { d = JSON.parse(decisionRaw); } catch (e) { return { kind: 'keep-hint', hasSidecar: true, error: 'bad decision JSON: ' + e.message }; }
  return {
    kind: 'keep-hint', hasSidecar: true,
    decision_index: d.decision_index, type: d.type,
    ai_choice: (d.ai_choice != null ? d.ai_choice : null),   // mulligan: 1=keep, 0=mulligan
    ai_set: Array.isArray(d.ai_set) ? d.ai_set : null,        // bottom: the table's joint bottom set
    bottom_total: (d.bottom_total != null ? d.bottom_total : null),
  };
}

// Compute the DEEP-search AI hint for the current pending decision, run at HINT_DEPTH (default 5) on a
// SEPARATE binary invocation so the primary /api/step stays fast (depth 0, no lookahead-bottoming
// rollout). The browser fires this in PARALLEL after showing a decision and fills the "AI would do X"
// hint in when it returns -- it never blocks the human's own choice (the AI pick is never required to
// make it). Only the `bottom` decision needs it: its AI pick is depth-dependent (the clairvoyant
// win-optimal removal), whereas the mulligan keep and other decisions are already correct at the play
// depth. Returns the pending decision's ai_choice + per-card win_optimal so the browser can patch the
// modal in place. Bottoming happens before turn 1, so this invocation only pays the bottoming rollout.
//
// Uses ASYNC spawn (spawnAsyncCollect), NOT spawnSync: at HINT_DEPTH the engine runs lookahead-bottoming
// (a full clairvoyant RolloutWinTurn per candidate card, per bottom step -- up to count*hand_size rollouts,
// expensive on a combo deck like Dragonstorm). A spawnSync here froze Node's single-threaded event loop
// for that whole computation, so the user's own /api/step (submit the bottom, advance to turn 1) could not
// be serviced until the hint finished -- the "waiting for lookahead bottoming while making the decision"
// stall. Async spawn keeps the hint a true background fill-in. Mirrors runKeepHint's async fix.
async function runAiHint(p) {
  const depth = intParam(p.hintDepth, HINT_DEPTH);
  const args = buildArgs({ ...p, depth }, null, null, false);   // table-less, depth = HINT_DEPTH
  const r = await spawnAsyncCollect(BIN, args, { cwd: ROOT, timeout: STEP_TIMEOUT_MS });
  if (r.error) return { kind: 'hint-error', error: String(r.error) };
  const out = (r.stdout || '') + '\n' + (r.stderr || '');
  const decisionRaw = extractBlock(out, '<<<CLAUDE_DECISION>>>', '<<<END_DECISION>>>');
  if (!decisionRaw) return { kind: 'hint-error', error: 'no decision markers' };
  let d;
  try { d = JSON.parse(decisionRaw); } catch (e) { return { kind: 'hint-error', error: 'bad decision JSON: ' + e.message }; }
  return {
    kind: 'hint',
    decision_index: d.decision_index,
    type: d.type,
    depth,
    ai_choice: (d.ai_choice != null ? d.ai_choice : null),
    win_optimal: Array.isArray(d.hand) ? d.hand.map(c => (c.win_optimal === undefined ? null : c.win_optimal)) : null,
  };
}

// Reconcile a hand-assembled line against the model at the first un-chosen main phase.
// Returns { kind:'validation', verdict, ... } for a verdict, or — if the first un-chosen
// decision is NOT a main phase (e.g. an Aether Vial charge) — the normal decision/result so
// the caller resolves that first.
function runValidate(p, line) {
  const args = buildArgs(p, null, line);
  const r = spawnSync(BIN, args, { cwd: ROOT, encoding: 'utf8', timeout: STEP_TIMEOUT_MS, maxBuffer: 32 * 1024 * 1024 });
  if (r.error) return { kind: 'error', error: String(r.error), args };
  const out = (r.stdout || '') + '\n' + (r.stderr || '');

  const valRaw = extractBlock(out, '<<<CLAUDE_VALIDATION>>>', '<<<END_VALIDATION>>>');
  if (valRaw) {
    try { return { kind: 'validation', ...JSON.parse(valRaw) }; }
    catch (e) { return { kind: 'error', error: 'bad validation JSON: ' + e.message, raw: valRaw }; }
  }
  // No validation block: the first un-chosen decision wasn't a main phase. Fall back to the
  // normal classification (vial charge to resolve, or the game already ended).
  const decisionRaw = extractBlock(out, '<<<CLAUDE_DECISION>>>', '<<<END_DECISION>>>');
  if (decisionRaw) {
    try { return { kind: 'decision', decision: JSON.parse(decisionRaw) }; }
    catch (e) { return { kind: 'error', error: 'bad decision JSON: ' + e.message, raw: decisionRaw }; }
  }
  const resultRaw = extractBlock(out, '<<<CLAUDE_RESULT>>>', '<<<END_RESULT>>>');
  if (resultRaw) {
    try { return { kind: 'result', result: JSON.parse(resultRaw) }; }
    catch (e) { return { kind: 'error', error: 'bad result JSON: ' + e.message, raw: resultRaw }; }
  }
  return { kind: 'error', error: 'no validation/decision/result markers', raw: out.slice(0, 4000), code: r.status };
}

// Sanitise a deck stem for use in an artifact filename.
function safeStem(s) { return String(s).replace(/[^A-Za-z0-9_-]+/g, '_'); }

// ---- deck maturity: which decks are still (beta) ----------------------------------
//
// A deck is only as trustworthy as the apparatus fitted to it, and three of those pieces arrive
// LATE and INDEPENDENTLY of the decklist -- so a deck can be fully implemented, pass every gate, and
// still be measuring something we would not quote. The viewer had no way to say so: every profiled
// deck rendered identically in the picker.
//
// The three, and why each one alone is enough to withhold confidence:
//
//   * FEWER THAN 10 OPTIMAL REFERENCE GAMES. References are the only human-played ground truth in
//     the repo -- the bound the AI's win turn is judged against, and the thing that surfaces engine
//     bugs autonomous play cannot (every viewer bug-bash in docs/design/ started as a reference).
//     A deck with three of them has not been looked at.
//   * NO VALUE-LEAF (`<stem>.value.json`, the path AttachValueSidecar resolves).
//   * NO COMPLETED MULLIGAN PROFILE. "Completed" means the COMPILED table exists, not that
//     generation was started: FiveColour holds a `.raw.json.journal` and no compiled profile, which
//     is a paused run, not a model. The extension list mirrors MulliganProfileIO.h:967 exactly, in
//     the same order, so the check cannot drift from what the engine actually loads.
//
// A deck with NO PROFILE is not beta, it is unplayable -- it already renders "(no profile)" and is
// disabled, which is the strictly stronger statement. Stacking "(beta)" on it would say less.
//
// THREE TIERS, because "has an apparatus" and "the apparatus agrees with a human" are different
// questions and the second is the one that matters:
//
//   (alpha)  a piece of the apparatus is MISSING -- <10 references, no value leaf, or no completed
//            mulligan profile. Its numbers may simply be wrong.
//   (beta)   the apparatus is complete, but the deck has not yet earned the top tier: fewer than 30
//            references, or the search does not match the human on the ones it has.
//   (none)   30+ references AND the shipped search matches or beats the human win turn on every
//            single one of them.
//
// THE GREEN GATE IS THE POINT (user, 2026-08-27: "ideally beta would be exited only when the play is
// green on the references -- i.e. we at least match the win turn"). A reference count measures how
// much work was done; a shortfall measures whether the engine is actually right. Thirty games nobody
// compared against is not evidence. This is also the only criterion here that can go RED on a deck
// that was previously fine -- the other three are monotone once earned, so without it the label can
// never react to a regression.
//
// 30 IS NOT A STATISTICAL THRESHOLD, it is the working convention: every deck's references run from
// seed 1 upward, roughly one game per seed, and none goes past ~33. So "30 references" reads as "the
// first 30 seeds have been played", and Knights at 28 is not 2 short of a quota -- it has two gaps.
//
// The bench is CACHED, never run from here (user, 2026-08-27: "I don't think we should run it
// manually every time across all decks... maybe we can cache it somehow"). scripts/ref_bench.py
// writes test/ref_bench.json, stamping each deck with the src tree it was measured at; a stamp that
// no longer matches reads STALE, which is not green. `ref_bench.py --stale-only --json ...` then
// re-benches exactly the decks that need it.
const MIN_OPTIMAL_REFS = 10;
const STABLE_REFS = 30;
const REF_BENCH = path.join(ROOT, 'test', 'ref_bench.json');
const KEEPMODEL_EXTS = ['.keepmodel.exhaustive.profile.json.gz', '.keepmodel.exhaustive.profile.json'];
const REFS_DIR = path.join(ROOT, 'references');

// Saved references are `claude_s<seed>_gi<gi>.json`; anything else in the folder is not a game.
// Matching the NAME rather than counting *.json keeps a stray file (references/ has one at top
// level) from inflating a deck past the threshold.
const REF_FILE_RE = /^claude_s\d+_gi\d+\.json$/;

// SUBOPTIMAL references do not count, and are excluded structurally rather than by a filter:
// references/suboptimal/<Deck>/ sits one level deeper than references/<Deck>/, so reading the
// deck's own folder never sees them (references/suboptimal/README.md).
function countOptimalRefs(name) {
  try {
    return fs.readdirSync(path.join(REFS_DIR, safeStem(name))).filter(f => REF_FILE_RE.test(f)).length;
  } catch (e) { return 0; }                                  // no folder = no references
}

// A deck's references belong to the LIST they were played on, not to the folder name.
//
// When a deck's shipping decklist is replaced, its existing references keep resolving to the folder
// -- and so, silently, to the NEW list. A recorded human line replayed against cards that were never
// in that deck is a benchmark that means nothing and reports no error, which is why
// scripts/deck_registry.py carries REFERENCE_DECK: an explicit map from a reference folder to the
// deck key its games were actually played on. Today it has one entry, Mirrorwing Dragon, whose 24
// references were played on the Twinflame/Ancestral Anger list archived on 2026-08-22.
//
// Without this, the maturity check counted those 24 and called the deck READY on the strength of
// games played against a deck it no longer is -- the exact failure the registry exists to end, and
// the worst version of it, because "ready" is precisely the claim references are supposed to earn.
//
// PARSED, not re-stated. The dict is Python and this is Node, and the viewer is deliberately
// dependency-free (node + the binary, no python3 -- it has to run on Windows), so shelling out is
// not free. A second hand-maintained copy is exactly the failure mode the registry's own docstring
// describes, so the test cross-checks this parse against Python's real dict.
function pySlug(s) { return String(s).replace(/[^A-Za-z0-9]/g, '_').toLowerCase(); }
function referenceOwners() {
  try {
    const src = fs.readFileSync(path.join(ROOT, 'scripts', 'deck_registry.py'), 'utf8');
    const m = src.match(/^REFERENCE_DECK\s*=\s*\{([\s\S]*?)^\}/m);
    const out = {};
    if (m) for (const e of m[1].matchAll(/^\s*"([^"]+)"\s*:\s*"([^"]+)"/gm)) out[e[1]] = e[2];
    return out;
  } catch (e) { return {}; }
}
const REF_OWNERS = referenceOwners();

// The CACHED reference bench (scripts/ref_bench.py --json test/ref_bench.json). Read once per
// request rather than at startup, so re-benching a deck shows up on a browser refresh instead of
// needing the server restarted -- the file is a few KB and this is a single-user local tool.
//
// The current src tree is re-resolved (with a short memo) rather than frozen at startup: HEAD:src
// moves with every commit while the server keeps running, and a startup-frozen hash had this
// long-lived server calling a freshly-measured bench "stale" against yesterday's tree (2026-08-27).
// If git is unavailable the fingerprint is empty and staleness is simply not judged -- reporting
// every deck stale forever would destroy the signal rather than protect it, and a checkout without
// git is not a case this repo has.
let srcNowCache = { at: 0, val: '' };
function srcNow() {
  const now = Date.now();
  if (now - srcNowCache.at < 3000) return srcNowCache.val;
  let val = '';
  try {
    const r = spawnSync('git', ['rev-parse', 'HEAD:src'], { cwd: ROOT, encoding: 'utf8' });
    val = r.status === 0 ? r.stdout.trim() : '';
  } catch (e) { /* val stays '' */ }
  srcNowCache = { at: now, val };
  return val;
}

// SELF-HEALING bench (user, 2026-08-27: "It should just run something on the side to remove the
// staleness if it is stale... I don't want it to redo any work on the regular, but if it needs to
// update the cache, then it should do so in the background... Probably deck by deck so it finishes
// and writes work units relatively quickly."): when a deck's bench stamp genuinely mismatches the
// current src tree, re-bench in the background instead of asking the user to -- ONE DECK PER RUN,
// sequentially, so each deck's row merges into the cache (`--json` merges) the moment it lands and
// its badge clears on the next browser refresh while later decks are still queued. No work on the
// regular: the trigger only fires from a real stamp mismatch, at most one queue per src tree (a
// failing bench must not respawn per request), and each child also passes --stale-only so a deck
// healed in the meantime costs zero games. Output goes under logs/ per the repo rule.
const benchRefresh = { running: false, attemptedSrc: '', queue: [] };
function benchRunNext(log) {
  const key = benchRefresh.queue.shift();
  if (!key) {
    benchRefresh.running = false;
    try { fs.closeSync(log); } catch (e) {}
    return;
  }
  try {
    fs.writeSync(log, `--- re-benching ${key} (${new Date().toISOString()}) ---\n`);
    const child = spawn('python3',
      ['scripts/ref_bench.py', '--deck', key, '--stale-only',
       '--json', path.join('test', 'ref_bench.json')],
      { cwd: ROOT, stdio: ['ignore', log, log], detached: false });
    child.on('close', () => benchRunNext(log));
    child.on('error', () => benchRunNext(log));
  } catch (e) { benchRefresh.running = false; try { fs.closeSync(log); } catch (e2) {} }
}
function maybeStartBenchRefresh(src) {
  if (!src || benchRefresh.running || benchRefresh.attemptedSrc === src) return;
  let staleKeys = [];
  try {
    const cache = JSON.parse(fs.readFileSync(REF_BENCH, 'utf8'));
    staleKeys = Object.entries(cache.decks || {})
      .filter(([, e]) => e && e.src && e.src !== src)
      .map(([k]) => k);
  } catch (e) { return; }
  if (!staleKeys.length) return;
  benchRefresh.running = true;
  benchRefresh.attemptedSrc = src;
  benchRefresh.queue = staleKeys;
  try {
    fs.mkdirSync(path.join(ROOT, 'logs'), { recursive: true });
    const log = fs.openSync(path.join(ROOT, 'logs', 'ref_bench_auto.log'), 'a');
    fs.writeSync(log, `\n=== auto re-bench at src ${src}: ${staleKeys.length} stale deck(s) ===\n`);
    benchRunNext(log);
  } catch (e) { benchRefresh.running = false; }
}

function benchState(key) {
  let cache;
  try { cache = JSON.parse(fs.readFileSync(REF_BENCH, 'utf8')); }
  catch (e) { return { state: 'unbenched' }; }               // never run, or not committed here
  const e = (cache.decks || {})[key];
  if (!e) return { state: 'unbenched' };
  const now = srcNow();
  if (now && e.src && e.src !== now) {
    maybeStartBenchRefresh(now);
    return { state: 'stale', at: e.src.slice(0, 12), n: e.n, refreshing: benchRefresh.running };
  }
  // A reference whose forced opening hand did not reconstruct was never a valid comparison, so a
  // deck carrying one has not been measured -- counting it as a pass is the same failure as an
  // empty parse reading as clean.
  if (e.hand_mismatch) return { state: 'unbenched', handMismatch: e.hand_mismatch, n: e.n };
  if (e.short) return { state: 'short', short: e.short, n: e.n, games: e.shortfalls || [] };
  return { state: 'green', n: e.n, human: e.human, search: e.search };
}

// PURE, so the policy is testable without a filesystem (test/viewer_deck_beta_check.js).
// -> { tier: 'unplayable' | 'alpha' | 'beta' | 'stable', tierReasons: [...] }
function tierFrom({ hasProfile, refs, hasValueLeaf, hasKeepModel, refsOnArchivedList, bench }) {
  if (!hasProfile) return { tier: 'unplayable', tierReasons: [] };

  // ALPHA -- a piece of the apparatus is missing outright.
  const missing = [];
  if (refs < MIN_OPTIMAL_REFS) {
    missing.push(`${refs}/${MIN_OPTIMAL_REFS} optimal reference games`
      + (refsOnArchivedList ? ` (the ${refsOnArchivedList} saved games were played on an archived list)` : ''));
  }
  if (!hasValueLeaf) missing.push('no value-leaf model');
  if (!hasKeepModel) missing.push('no completed mulligan profile');
  if (missing.length) return { tier: 'alpha', tierReasons: missing };

  // BETA -- complete, but not yet proven at depth or against the human.
  const short = [];
  if (refs < STABLE_REFS) short.push(`${refs}/${STABLE_REFS} reference games`);
  const b = bench || { state: 'unbenched' };
  if (b.state === 'short') {
    short.push(`the search is slower than the human on ${b.short} of ${b.n} references`);
  } else if (b.state === 'stale') {
    short.push(b.refreshing
      ? `reference bench is stale (measured at src ${b.at}) — re-benching on the side, refresh in a minute`
      : `reference bench is stale (measured at src ${b.at}) — auto re-bench did not clear it, see logs/ref_bench_auto.log`);
  } else if (b.state !== 'green') {
    short.push(b.handMismatch
      ? `${b.handMismatch} reference(s) did not reconstruct, so the bench is not a valid comparison`
      : 'reference bench has never been run for this deck');
  }
  return short.length ? { tier: 'beta', tierReasons: short } : { tier: 'stable', tierReasons: [] };
}

function deckMaturity(dir, name, hasProfile) {
  const key = pySlug(name);
  const owner = REF_OWNERS[key] || key;
  const saved = countOptimalRefs(name);
  // Someone else's games do not count toward this deck. Reported separately so the badge can say
  // WHY a deck with a full folder reads zero -- otherwise it looks like the count is broken.
  const refsOnArchivedList = owner === key ? 0 : saved;
  const refs = owner === key ? saved : 0;
  const hasValueLeaf = fs.existsSync(path.join(dir, name + '.value.json'));
  const hasKeepModel = KEEPMODEL_EXTS.some(ext => fs.existsSync(path.join(dir, name + ext)));
  // ref_bench.py keys its results by the deck the references were PLAYED on. So a deck whose folder
  // belongs to an archived list must NOT read that entry: the bench under `owner` describes the
  // archived list's play, and lending its green to the deck that replaced it is precisely the
  // borrowed-evidence bug this whole ownership rule exists to stop. No references of its own means
  // no bench of its own.
  const bench = owner === key ? benchState(key) : { state: 'unbenched' };
  // The NEXT reference-count requirement, so the chip can always show progress ("beta 20/30")
  // until the count is met (USER 2026-08-27: "always put it unless we have more references than
  // the requirement"). Server-owned so the thresholds are never re-stated client-side. Null once
  // refs >= STABLE_REFS -- past the last count requirement there is no fraction to show.
  const refGoal = refs < MIN_OPTIMAL_REFS ? MIN_OPTIMAL_REFS
                : refs < STABLE_REFS      ? STABLE_REFS : null;
  return Object.assign({ refs, refGoal, hasValueLeaf, hasKeepModel, refsOnArchivedList, bench },
                       tierFrom({ hasProfile, refs, hasValueLeaf, hasKeepModel, refsOnArchivedList, bench }));
}

// ---- routes ----------------------------------------------------------------------

function listDecks() {
  const out = [];
  // Per-deck folder layout: one directory per deck (decks/<name>/), each holding <name>.{txt,cod}
  // plus optional <name>.profile.json and sibling models.
  for (const name of fs.readdirSync(DECKS_DIR)) {
    const dir = path.join(DECKS_DIR, name);
    if (!fs.statSync(dir).isDirectory()) continue;
    let deckFile = null;
    for (const ext of ['.cod', '.txt']) {
      if (fs.existsSync(path.join(dir, name + ext))) { deckFile = name + ext; break; }
    }
    if (!deckFile) continue;                                 // folder without a matching decklist
    const hasProfile = fs.existsSync(path.join(dir, name + '.profile.json'));
    out.push(Object.assign({ deck: deckFile, name, hasProfile },
                           deckMaturity(dir, name, hasProfile)));
  }
  // playable (profiled) decks first, then alpha
  out.sort((a, b) => (b.hasProfile - a.hasProfile) || a.name.localeCompare(b.name));
  return out;
}

const server = http.createServer(async (req, res) => {
  try {
    const url = new URL(req.url, 'http://localhost');
    if (req.method === 'GET' && (url.pathname === '/' || url.pathname === '/index.html')) {
      const html = fs.readFileSync(path.join(__dirname, 'index.html'));
      // no-store so the browser always re-fetches the GUI after an edit (otherwise a stale cached
      // index.html hides changes until a manual hard-refresh).
      res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8', 'Cache-Control': 'no-store' });
      return res.end(html);
    }
    if (req.method === 'GET' && url.pathname === '/linebuild.js') {
      // Shared line-building module (also require()d by test/viewer_linebuild_check.js). no-store so
      // a browser always re-fetches after an edit (matching index.html).
      const js = fs.readFileSync(path.join(__dirname, 'linebuild.js'));
      res.writeHead(200, { 'Content-Type': 'application/javascript; charset=utf-8', 'Cache-Control': 'no-store' });
      return res.end(js);
    }
    if (req.method === 'GET' && url.pathname === '/api/decks') {
      return sendJson(res, 200, { decks: listDecks(), binExists: fs.existsSync(BIN) });
    }
    if (req.method === 'GET' && url.pathname === '/api/reference-exists') {
      // Does a saved reference game already exist for this (deck, seed, game#)? The top bar shows a
      // note so the user can skip replaying a game they've already saved (they can still play it).
      try {
        const { stem } = resolveDeck(url.searchParams.get('deck'));
        const seed = intParam(url.searchParams.get('seed'), 1);
        const gi = intParam(url.searchParams.get('gi'), 0);
        const rel = path.join('references', safeStem(stem), `claude_s${seed}_gi${gi}.json`);
        const exists = fs.existsSync(path.join(ROOT, rel));
        const relSub = path.join('references', 'suboptimal', safeStem(stem), `claude_s${seed}_gi${gi}.json`);
        const suboptimal = fs.existsSync(path.join(ROOT, relSub));
        return sendJson(res, 200, { exists, path: exists ? rel : null,
                                    suboptimal, suboptimalPath: suboptimal ? relSub : null });
      } catch (e) {
        return sendJson(res, 200, { exists: false, path: null });
      }
    }
    if (req.method === 'POST' && url.pathname === '/api/step') {
      const p = await readBody(req);
      return sendJson(res, 200, runStep(p, null));
    }
    if (req.method === 'POST' && url.pathname === '/api/ai-hint') {
      // Async deep-search hint for the current decision (see runAiHint). Genuinely non-blocking now: the
      // hint spawns via async spawn, so its lookahead-bottoming rollout never freezes the event loop /
      // the user's next /api/step (advance to turn 1). The browser has already shown the decision.
      const p = await readBody(req);
      return sendJson(res, 200, await runAiHint(p));
    }
    if (req.method === 'POST' && url.pathname === '/api/keep-hint') {
      // Async mulligan-TABLE hint for the current pre-game decision (see runKeepHint). Non-blocking:
      // the browser has already shown the mulligan/bottom modal; this loads the exhaustive sidecar via
      // an ASYNC spawn (so the parse never freezes the event loop / the next /api/step) and returns the
      // table's keep/mulligan call + joint bottom set.
      const p = await readBody(req);
      return sendJson(res, 200, await runKeepHint(p));
    }
    if (req.method === 'POST' && url.pathname === '/api/validate') {
      // Reconcile the hand-assembled line (p.line, an encoded "land=X;cast=Y;..." string)
      // against the model at the current decision. Accept -> the GUI appends plan_index and
      // advances; reject -> the GUI shows the classified verdict and offers an artifact save.
      const p = await readBody(req);
      return sendJson(res, 200, runValidate(p, String(p.line == null ? '' : p.line)));
    }
    if (req.method === 'POST' && url.pathname === '/api/reject-artifact') {
      // Persist a rejected line ("I tried X, the model wouldn't take it") for later triage.
      // Gitignored scratch under logs/play/rejections/. The body carries the full context the
      // verdict was computed from (state snapshot + attempted line + verdict + reason).
      const p = await readBody(req);
      const dir = path.join(ROOT, 'logs', 'play', 'rejections');
      fs.mkdirSync(dir, { recursive: true });
      const seed = intParam(p.seed, 1), gi = intParam(p.gameIndex, 0), turn = intParam(p.turn, 0);
      const fn = `${safeStem(p.deck || 'deck')}_s${seed}_gi${gi}_t${turn}.json`;
      const artifact = {
        savedFrom: 'tools/play', deck: p.deck, seed, gameIndex: gi, turn,
        phase: p.phase || null,
        priorChoices: Array.isArray(p.choices) ? p.choices : [],
        attemptedLine: p.attemptedLine || null,     // {land, casts[]}
        encodedLine: p.line || null,
        verdict: p.verdict || null,                 // illegal | legal_not_enumerated | unsupported
        reason: p.reason || null,
        failedAction: p.failed_action || null,
        modelPlans: p.modelPlans || null,           // what the model WOULD play here
        state: p.state || null,                     // me/opponent snapshot at the decision
        note: 'Reproduce: --claude-play --seed <seed> --game-index <gi> --choices "' +
              (Array.isArray(p.choices) ? p.choices.join(',') : '') +
              '" --validate-line "' + (p.line || '') + '"',
      };
      const full = path.join(dir, fn);
      fs.writeFileSync(full, JSON.stringify(artifact, null, 2));
      return sendJson(res, 200, { savedAs: path.relative(ROOT, full) });
    }
    if (req.method === 'POST' && url.pathname === '/api/save-reference') {
      // Promote a CLEAN human-played game (no rejects) to the tracked references set. These are
      // no-clairvoyance ground-truth games whose win-turn a good AI should match. With
      // suboptimal:true the game goes to references/suboptimal/<deck>/ instead -- a "known-slow"
      // target (you believe the win is reachable EARLIER), kept out of the verified benchmark the
      // checker gates on (see references/suboptimal/README.md).
      const p = await readBody(req);
      const { stem } = resolveDeck(p.deck);
      const dir = p.suboptimal
        ? path.join(ROOT, 'references', 'suboptimal', safeStem(stem))
        : path.join(ROOT, 'references', safeStem(stem));
      fs.mkdirSync(dir, { recursive: true });
      const r = runStep(p, dir);
      const seed = intParam(p.seed, 1), gi = intParam(p.gameIndex, 0);
      const tracePath = path.join(dir, `claude_s${seed}_gi${gi}.json`);
      return sendJson(res, 200, { ...r, savedAs: fs.existsSync(tracePath) ? path.relative(ROOT, tracePath) : null });
    }
    if (req.method === 'POST' && url.pathname === '/api/save') {
      // Re-run the FULL accumulated choices with --log-dir so RunClaudePlay writes the
      // deterministic per-game decision trace (the "trustworthy reference" artifact).
      // It is also viewable in tools/replay/ and re-playable from (deck, seed, gi, choices).
      const p = await readBody(req);
      const logDir = path.join(ROOT, 'logs', 'play');
      fs.mkdirSync(logDir, { recursive: true });
      const r = runStep(p, logDir);
      const seed = intParam(p.seed, 1), gi = intParam(p.gameIndex, 0);
      const tracePath = path.join(logDir, `claude_s${seed}_gi${gi}.json`);
      return sendJson(res, 200, { ...r, savedAs: fs.existsSync(tracePath) ? path.relative(ROOT, tracePath) : null });
    }
    res.writeHead(404); res.end('not found');
  } catch (e) {
    sendJson(res, 500, { kind: 'error', error: String(e && e.message || e) });
  }
});

// Only bind the port when run as a script (node tools/play/server.js). When require()d — by the
// jsdom client check (test/viewer_client_check.js), which reuses runStep/runValidate/listDecks to
// serve the exact same protocol the browser talks — do NOT listen (no port bind, no console spam).
if (require.main === module) {
  server.listen(PORT, HOST, () => {
    const buildCmd = process.platform === 'win32' ? 'build.cmd' : './build.sh';
    const found = fs.existsSync(BIN);
    console.log(`MagicDeckTester play GUI: http://localhost:${PORT}  (bound ${HOST}:${PORT})`);
    console.log(`  binary: ${BIN} ${found ? '(found)' : '(MISSING)'}`);
    console.log(`  decks:  ${DECKS_DIR}`);
    if (!found) {
      // Name the platform's build command rather than a bare "build Release first" -- this is
      // the first thing a new user hits, and the answer differs per OS.
      console.log('');
      console.log(`  The engine is not built yet. Run ${buildCmd} from the repo root, then restart.`);
      console.log(`  (Or use ${process.platform === 'win32' ? 'play.cmd' : './play.sh'}, which builds it for you.)`);
    }
  });
}

// Exported for the headless jsdom client check so it drives the REAL protocol (not a reimplementation).
module.exports = { runStep, runValidate, listDecks, resolveDeck, buildArgs, BIN,
                   // deck maturity, for test/viewer_deck_beta_check.js
                   tierFrom, benchState, deckMaturity, countOptimalRefs, MIN_OPTIMAL_REFS, STABLE_REFS, KEEPMODEL_EXTS,
                   pySlug, referenceOwners };
