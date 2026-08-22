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
    out.push({ deck: deckFile, name, hasProfile });
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
module.exports = { runStep, runValidate, listDecks, resolveDeck, buildArgs, BIN };
