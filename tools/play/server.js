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
// `version` (optional) selects an ARCHIVED list: decks/<stem>/<version>/<stem>.<ext>, the layout
// deck_registry.discover() already treats as a real addressable deck. Empty/absent = the list that
// currently ships, byte-identical to the old single-argument behaviour.
//
// The traversal guard is kept intact rather than widened: `version` must be a SINGLE path segment
// matching a strict allowlist (no dot-dot, no separator), and the resolved directory is re-checked
// to be an immediate child of the deck folder. path.basename() still strips components off the
// deck name itself, exactly as before.
const VERSION_RE = /^[A-Za-z0-9][A-Za-z0-9._-]*$/;
function resolveDeck(deckName, version) {
  if (typeof deckName !== 'string' || !deckName) throw new Error('deck required');
  const base = path.basename(deckName);                    // strip any path components
  const stem = base.replace(/\.[^.]+$/, '');
  const deckDir = path.join(DECKS_DIR, stem);              // per-deck folder
  let dir = deckDir;
  if (version) {
    if (!VERSION_RE.test(version) || version === '.' || version === '..') {
      throw new Error('bad version: ' + version);
    }
    dir = path.join(deckDir, version);
    if (path.dirname(dir) !== deckDir) throw new Error('version must be one folder under the deck');
  }
  const deckPath = path.join(dir, base);
  if (path.dirname(deckPath) !== dir) throw new Error('deck must be under decks/<name>/');
  if (!fs.existsSync(deckPath)) throw new Error('deck not found: ' + base);
  const profilePath = path.join(dir, stem + '.profile.json');
  // The exhaustive keep/bottom sidecar (mulligan TABLE). Its presence gates whether the viewer's
  // pre-game keep/bottom suggestions come from the table (see runStep) rather than the live heuristic.
  const sidecarPath = path.join(dir, stem + '.keepmodel.exhaustive.profile.json.gz');
  return { deckPath, profilePath: fs.existsSync(profilePath) ? profilePath : null, stem,
           version: version || null, hasSidecar: fs.existsSync(sidecarPath) };
}

function intParam(v, dflt) {
  const n = parseInt(v, 10);
  return Number.isFinite(n) ? n : dflt;
}

// Build the argv for one --claude-play invocation.
// #10 cast-order side-channel, as argv. `map` is a { mainOrdinal: [entry, ...] } map of the human's
// declared order for that main-phase decision. Passed as "<ord>:A|B|C;..." (pipe-separated, since
// MTG names contain ',' but never '|'), keyed by main-phase ordinal — NEVER a --choices slot, so
// existing references (no --cast-order) replay in canonical order unchanged.
//
// An entry is a card NAME, the leading "*" full-order marker, or a MANUAL TAP/PAY token
// `tap=<name>#<num>:<COLOUR>` (docs/design/viewer-manual-tap-pay.md) — all three are opaque strings
// here and are told apart by the engine (AIEngine::ReorderPlanCasts). ONE builder, because the
// reject artifact's reproduce command has to name the identical argument the live run used;
// re-deriving it there is how the two would come to disagree.
function castOrderArg(map) {
  if (!map || typeof map !== 'object') return [];
  const entries = Object.keys(map)
    .filter(k => Array.isArray(map[k]) && map[k].length)
    .map(k => `${k}:${map[k].join('|')}`);
  return entries.length ? ['--cast-order', entries.join(';')] : [];
}

// validateLine (optional): an encoded human-assembled line ("land=X;cast=Y;...") to reconcile
// against the model at the first un-chosen main phase instead of dumping the plan menu.
// exhaustiveKeep (optional): pass --exhaustive-keep so the engine loads the deck's mulligan-table
// sidecar and emits table-based keep/bottom suggestions. Used ONLY for the pre-game keep/bottom
// steps (see runStep) — the sidecar parse is expensive (up to tens of seconds on big decks), so it
// must never be on the per-turn hot path. This is the VIEWER opting in; default --claude-play (the
// engine-test sweep/oracle) deliberately stays table-less (see main.cpp AttachExhaustiveSidecar).
function buildArgs(p, logDir, validateLine, exhaustiveKeep) {
  const { deckPath, profilePath } = resolveDeck(p.deck, p.version);
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
  // Umezawa's Jitte counter-spend side-channel: same turn-keyed shape as firebreathe. Kept for
  // REPLAY of games that recorded answers; the live PROMPT is gone (USER 2026-08-27: "It should be
  // handled by allowing activations to be specifically triggered rather than having a dialog on
  // attack") -- pumps are explicit main-phase JitteModeAbility plans, which last until end of turn
  // and so cover combat; an unanswered combat turn BANKS the counters (engine human-play default).
  if (p.jitte && typeof p.jitte === 'object') {
    const jpairs = Object.keys(p.jitte).map(t => `${t}:${p.jitte[t]}`);
    if (jpairs.length) args.push('--jitte', jpairs.join(','));
  }
  args.push('--firebreathe-prompt');
  args.push(...castOrderArg(p.castOrder));
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
  // sessionBin, not BIN: every spawn belonging to a live game must use the image that game STARTED
  // on (see "SESSION-PINNED ENGINE BINARY" below) -- a rebuild mid-session must not change a
  // half-played game's replay semantics.
  const r = spawnSync(sessionBin(p), args, { cwd: ROOT, encoding: 'utf8', timeout: STEP_TIMEOUT_MS, maxBuffer: 32 * 1024 * 1024 });
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

// ---- INTERACTIVE PREFIX-CACHE: one persistent --interactive child per game ----
//
// The stateless protocol re-simulates the whole --choices prefix on every step, so step N pays for
// re-enumerating all N-1 earlier plan fans -- on a long manual combo turn (EDF seed 8, USER
// 2026-09-09: "it becomes progressively slower for some reason") each click costs more than the
// last. With --interactive the engine blocks on stdin after emitting a decision instead of exiting
// 70, and continues the SAME in-process game when the next picks arrive: each step costs only its
// own frame. The child runs the identical chooser code path, so decisions are byte-identical to a
// stateless replay of the same prefix (parity-checked by test/interactive_parity_check.py).
//
// The stateless respawn remains the source of truth and the fallback for EVERYTHING unusual:
// rewind/undo (choices no longer extend the child's stream), changed side-channel args (they are
// argv-baked, so the session key covers them), a validate/save step (those keep their own spawns),
// a side-channel PROMPT frame (firebreathe/jitte/storage-hold still exit 70 by design), child
// death, malformed output, or a step timeout. Opt out with PLAY_INTERACTIVE=0.
const INTERACTIVE = process.env.PLAY_INTERACTIVE !== '0';
let isession = null;   // single-user tool: at most ONE live child { key, child, sent, buf, dead, busy }

function killIsession() {
  if (isession) { try { isession.child.kill('SIGKILL'); } catch (e) {} isession = null; }
}

// Scan `sess.buf` from `start` for one complete decision block; null until it arrives.
function scanDecision(sess, start) {
  const end = sess.buf.indexOf('<<<END_DECISION>>>', start);
  if (end < 0) return null;
  const begin = sess.buf.indexOf('<<<CLAUDE_DECISION>>>', start);
  if (begin < 0 || begin > end) return { kind: 'error', error: 'interactive: unpaired decision markers' };
  const raw = sess.buf.slice(begin + '<<<CLAUDE_DECISION>>>'.length, end).trim();
  try { return { kind: 'decision', decision: JSON.parse(raw) }; }
  catch (e) { return { kind: 'error', error: 'interactive: bad decision JSON: ' + e.message }; }
}

// Wait on the session until a decision block lands after `start`, or the child exits (game over ->
// parse the result), or the step timeout. Resolves null on anything that should trigger the
// stateless fallback.
function awaitIsession(sess, start) {
  return new Promise((resolve) => {
    let timer = null;
    const settle = (v) => { if (timer) clearTimeout(timer); sess.wake = null; sess.busy = false; resolve(v); };
    const check = () => {
      const dec = scanDecision(sess, start);
      if (dec) return settle(dec.kind === 'decision' ? dec : null);
      if (sess.dead) {
        const resultRaw = extractBlock(sess.buf, '<<<CLAUDE_RESULT>>>', '<<<END_RESULT>>>');
        if (resultRaw) { try { return settle({ kind: 'result', result: JSON.parse(resultRaw) }); } catch (e) {} }
        return settle(null);
      }
    };
    timer = setTimeout(() => settle(null), STEP_TIMEOUT_MS);
    sess.wake = check;
    check();
  });
}

// Same wait, for the VALIDATION block a `@validate-line` directive produces. A validation does not
// advance the game, so the child is still parked on the same frame afterwards and the session stays
// reusable -- which is the whole point.
function awaitIvalidation(sess, start) {
  return new Promise((resolve) => {
    let timer = null;
    const settle = (v) => { if (timer) clearTimeout(timer); sess.wake = null; sess.busy = false; resolve(v); };
    const check = () => {
      const end = sess.buf.indexOf('<<<END_VALIDATION>>>', start);
      if (end >= 0) {
        const begin = sess.buf.indexOf('<<<CLAUDE_VALIDATION>>>', start);
        if (begin < 0 || begin > end) return settle(null);
        const raw = sess.buf.slice(begin + '<<<CLAUDE_VALIDATION>>>'.length, end).trim();
        try { return settle({ kind: 'validation', ...JSON.parse(raw) }); }
        catch (e) { return settle(null); }
      }
      if (sess.dead) return settle(null);
    };
    timer = setTimeout(() => settle(null), STEP_TIMEOUT_MS);
    sess.wake = check;
    check();
  });
}

// Split buildArgs' argv into (everything-but-choices, choices[]) -- plus the --cast-order spec,
// pulled OUT of the session key.
//
// WHY --cast-order IS NOT PART OF THE KEY. The key is "every argv except --choices", which is the
// right rule for an argv-baked channel: change one and the live child is executing a different
// invocation, so it must be respawned. But --cast-order is not like the others in the one way that
// matters here: the viewer ADDS a pin on the ordinary commit path (index.html records the applied
// order under the just-committed decision's main_ordinal), so on a deck where the human sequences
// their own lines the argv changes on nearly every click. Measured on EldraziDisplacerFlicker
// claude_s12_gi11 (2026-09-11): 31 of 60 decisions carry a pin, so 31 of 61 clicks respawned and
// re-simulated the whole prefix -- 32 engine spawns for 61 clicks, 100-190 ms per pinning click
// against 5-20 ms for a cached one. The persistent child existed and was being thrown away.
//
// The pin is delivered to the LIVE child instead, on stdin, immediately before the picks it
// belongs to (`@cast-order`; ClaudePlayHarness::AwaitNext in src/main.cpp carries the timing
// argument). The other keyed channels (--firebreathe / --jitte / --storage-hold) stay IN the key
// deliberately: their answers only ever arrive in response to a PROMPT frame, and those frames
// exit 70 unconditionally by design, so the child is already gone and a respawn is what has to
// happen anyway.
const CAST_ORDER_FLAG = '--cast-order';
function argsAndChoices(p, logDir) {
  const args = buildArgs(p, logDir, null, false);
  const i = args.indexOf('--choices');
  const rest = args.slice(0, i).concat(args.slice(i + 2));
  const ci = rest.indexOf(CAST_ORDER_FLAG);
  const castOrder = ci >= 0 ? rest[ci + 1] : '';
  const stable = ci >= 0 ? rest.slice(0, ci).concat(rest.slice(ci + 2)) : rest;
  return { rest, stable, castOrder,
           choices: args[i + 1] ? args[i + 1].split(',') : [] };
}

// "<ord>:A|B;<ord>:X" -> Map(ord -> "A|B"). Mirrors ParseCastOrderSpec in main.cpp closely enough
// for the ONE question asked of it below: are these two specs the same statement about the same
// ordinals? Entry ORDER is irrelevant (the engine keys a map), so comparing the raw strings would
// respawn on a re-serialisation that says exactly the same thing.
function parseCastOrder(spec) {
  const out = new Map();
  for (const entry of String(spec || '').split(';')) {
    const c = entry.indexOf(':');
    if (c < 0) continue;
    const ord = parseInt(entry.slice(0, c), 10);
    if (!Number.isFinite(ord)) continue;
    out.set(ord, entry.slice(c + 1));
  }
  return out;
}

// May the live child be brought up to date with `next` by MERGING, or must it be respawned?
//
// Only an ADDITIVE change is deliverable. The engine merges a directive into its map, so a pin
// the child already holds cannot be withdrawn, and a pin that CHANGED is a different statement
// about a decision whose apply has already run -- delivering either would make the child's game
// diverge from the one the browser is showing, which is the whole class of bug the save audit
// exists to catch. An undo (index.html deletes S.castOrder[st.co]) and a rewind both land here as
// non-additive / non-extending and respawn, which is correct and cheap: those are rare.
function castOrderExtends(prev, next) {
  const a = parseCastOrder(prev), b = parseCastOrder(next);
  for (const [ord, v] of a) { if (b.get(ord) !== v) return false; }
  return true;
}

async function runStepCached(p, logDir) {
  if (!INTERACTIVE || logDir) return runStep(p, logDir);
  const { rest, stable, castOrder, choices } = argsAndChoices(p, logDir);
  const key = JSON.stringify(stable);
  const s = isession;
  // RE-STEP AT THE SAME PREFIX: hand back the frame the child is already parked on, with no
  // engine work at all. This is the REJECT/RETRY path -- a rejected line re-steps into the very
  // decision it was rejected at -- and it used to fall through to the respawn below, because the
  // fast path requires the stream to GROW. A whole-game re-simulation to be told what we were
  // just told, at exactly the moment the human is already fighting the viewer.
  if (s && !s.dead && !s.busy && s.key === key && s.castOrder === castOrder &&
      s.pendingDecision && choices.length === s.sent.length &&
      s.sent.every((c, i) => c === choices[i])) {
    return { kind: 'decision', decision: s.pendingDecision };
  }
  if (s && !s.dead && !s.busy && s.key === key &&
      choices.length > s.sent.length && s.sent.every((c, i) => c === choices[i]) &&
      castOrderExtends(s.castOrder, castOrder)) {
    // Fast path: this step extends the live child's stream -- feed it only the delta.
    s.busy = true;
    const start = s.buf.length;
    try {
      // The pin BEFORE the picks, always: the engine reads ordinal N's order immediately after
      // consuming N's pick, so the directive has to be in the map by then.
      if (castOrder !== s.castOrder) { s.child.stdin.write('@cast-order ' + castOrder + '\n'); }
      s.child.stdin.write(choices.slice(s.sent.length).join(',') + '\n');
    }
    catch (e) { s.busy = false; killIsession(); return runStep(p, logDir); }
    const r = await awaitIsession(s, start);
    if (r) {
      s.sent = choices; s.castOrder = castOrder;
      s.pendingType = r.kind === 'decision' ? r.decision.type : null;
      s.pendingDecision = r.kind === 'decision' ? r.decision : null;
      if (r.kind === 'result') killIsession();
      return r;
    }
    killIsession();
    return runStep(p, logDir);   // stateless fallback decides what this step really is
  }
  // (Re)spawn: new game, rewind, changed side-channel args, or a busy/dead child. `rest` (not
  // `stable`) -- the spawn's argv is unchanged, cast-order pin and all.
  killIsession();
  const child = spawn(sessionBin(p), rest.concat(['--choices', choices.join(','), '--interactive']),
                      { cwd: ROOT });
  const ns = { key, child, sent: choices, castOrder, pendingType: null, pendingDecision: null,
               buf: '', dead: false, busy: true, wake: null };
  child.stdout.on('data', (d) => { ns.buf += d; if (ns.wake) ns.wake(); });
  child.stderr.on('data', () => {});   // [play] chatter; the stateless fallback surfaces real errors
  child.on('error', () => { ns.dead = true; if (ns.wake) ns.wake(); });
  child.on('close', () => { ns.dead = true; if (ns.wake) ns.wake(); });
  isession = ns;
  const r = await awaitIsession(ns, 0);
  if (r) {
    ns.pendingType = r.kind === 'decision' ? r.decision.type : null;
    ns.pendingDecision = r.kind === 'decision' ? r.decision : null;
    if (r.kind === 'result') killIsession();
    return r;
  }
  killIsession();
  return runStep(p, logDir);
}

// Reconcile a line against the LIVE child instead of a fresh full-prefix spawn.
//
// THE OTHER FULL-PREFIX SPAWN A CLICK PAID FOR. The queue workflow commits through `Commit Line`,
// which POSTs /api/validate and only then /api/step -- so with the step cached (above) the
// validation became the whole cost of a commit: one stateless re-simulation of the entire prefix,
// measured at 0.36-0.42 s CPU on EldraziDisplacerFlicker claude_s9_gi8's turn-4 go-off.
//
// The frame a stateless `--validate-line` spawn would reconcile against IS the frame the live child
// is parked on: both are "the first un-chosen main-phase decision" for the same choice prefix. So
// the directive asks the same question of the same state, and WriteValidation (one writer, both
// routes) answers it with the same bytes. A validation consumes no pick, so the child is still
// parked on that frame afterwards and the next step is still a cached one.
//
// STRICTLY CONSERVATIVE about when it may be used, because a validation that answered about the
// wrong frame would be a verdict on a line the human is not looking at:
//   * the prefix must be EXACTLY the child's (a validation does not extend the stream);
//   * the cast-order map must be EXACTLY the child's (the line's own taps ride --validate-line;
//     a pin is only recorded AFTER a verdict is accepted, so this is the normal state);
//   * the pending frame must be a main_phase -- the 28 other emission sites cannot answer a
//     validation, and the stateless path's behaviour there is to fall through and re-emit the
//     pending decision, which the caller already has.
// Anything else, and anything malformed, falls back to `runValidate` -- unchanged.
async function runValidateCached(p, line) {
  // An EMPTY line is not a validation at all on the stateless path (`validate_line.empty()` falls
  // through to re-emitting the decision), and a line carrying a newline would desync the stdin
  // protocol. Neither can be served here; both are already handled correctly by the spawn.
  if (!INTERACTIVE || !line || /[\r\n]/.test(line)) return runValidate(p, line);
  const { stable, castOrder, choices } = argsAndChoices(p, null);
  const key = JSON.stringify(stable);
  const s = isession;
  if (s && !s.dead && !s.busy && s.key === key && s.pendingType === 'main_phase' &&
      s.castOrder === castOrder && choices.length === s.sent.length &&
      s.sent.every((c, i) => c === choices[i])) {
    s.busy = true;
    const start = s.buf.length;
    try { s.child.stdin.write('@validate-line ' + line + '\n'); }
    catch (e) { s.busy = false; killIsession(); return runValidate(p, line); }
    const r = await awaitIvalidation(s, start);
    if (r) return r;
    killIsession();
  }
  return runValidate(p, line);
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
  try { hasSidecar = resolveDeck(p.deck, p.version).hasSidecar; } catch (e) { /* table-less */ }
  if (!hasSidecar) return { kind: 'keep-hint', hasSidecar: false };
  const args = buildArgs(p, null, null, true);   // WITH --exhaustive-keep
  const r = await spawnAsyncCollect(sessionBin(p), args, { cwd: ROOT, timeout: STEP_TIMEOUT_MS });
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
  const r = await spawnAsyncCollect(sessionBin(p), args, { cwd: ROOT, timeout: STEP_TIMEOUT_MS });
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
  const r = spawnSync(sessionBin(p), args, { cwd: ROOT, encoding: 'utf8', timeout: STEP_TIMEOUT_MS, maxBuffer: 32 * 1024 * 1024 });
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

// The KEYED side channels (never --choices slots) rendered as a shell fragment, for the reproduce
// line a rejection artifact carries. DERIVED from buildArgs rather than re-encoded, so the printed
// repro cannot drift from the invocation the engine actually receives -- an artifact whose repro
// omits, say, --cast-order describes a different game from the one that was rejected.
const SIDE_CHANNEL_FLAGS = ['--cast-order', '--firebreathe', '--jitte', '--storage-hold'];
function sideChannelArgs(p) {
  let args;
  try { args = buildArgs(p, null, null, false); } catch (e) { return ''; }
  let out = '';
  for (let i = 0; i + 1 < args.length; i++) {
    if (SIDE_CHANNEL_FLAGS.indexOf(args[i]) >= 0) { out += ` ${args[i]} "${args[i + 1]}"`; }
  }
  return out;
}

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
//   * NO VALUE-LEAF (`<stem>.value.json`, the path AttachValueSidecar resolves) -- UNLESS the deck
//     holds a `<stem>.value.DISABLED.json`: that is a model the adoption A/B measured and REJECTED
//     for play (the value-leaf skill's rejected-model shipping form), so leaf-less play is the
//     decided configuration, not a gap. Surfaced as a by-design note, never as alpha.
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
// no longer matches reads STALE. `ref_bench.py --stale-only --json ...` then re-benches exactly the
// decks that need it.
//
// STALE CARRIES THE LAST VERDICT FORWARD; IT IS NOT ITSELF A DEMOTION (user, 2026-08-29:
// "everything is marked as beta for a period. That is not a good idea. They should be marked as
// beta only if they were not stable before"). Every src commit stales EVERY deck at once, so
// treating stale as not-green re-labelled the whole picker on every commit -- a badge that fires on
// all decks simultaneously says nothing about any of them. A stale deck now keeps the tier its last
// real bench earned: previously green stays green, previously short stays beta. See tierFrom.
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
// `version` counts an ARCHIVED list's own references, which live one level deeper --
// references/<Deck>/<Version>/ mirroring decks/<Deck>/<Version>/ (see ref_bench.ref_dirs).
//
// This is why the shipping list's count is right without a filter: a variant folder is a
// DIRECTORY, and REF_FILE_RE only matches the claude_s<seed>_gi<gi>.json file name, so reading the
// deck's own folder never sees another list's games -- structurally, exactly as suboptimal/ is
// excluded. Before the split those games sat loose in the same folder and every one of them
// counted toward whichever list was being played.
function countOptimalRefs(name, version) {
  const dir = version ? path.join(REFS_DIR, safeStem(name), version)
                      : path.join(REFS_DIR, safeStem(name));
  try {
    return fs.readdirSync(dir).filter(f => REF_FILE_RE.test(f)).length;
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
    // Carry the PREVIOUS verdict through the stale window (see "STALE DOES NOT DEMOTE" above).
    // The cache entry still holds what the deck last measured; throwing it away is what made a
    // single src commit re-label every deck at once.
    const wasGreen = !e.short && !e.hand_mismatch;
    return { state: 'stale', at: e.src.slice(0, 12), n: e.n, refreshing: benchRefresh.running,
             wasGreen, short: e.short, handMismatch: e.hand_mismatch };
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
function tierFrom({ hasProfile, refs, hasValueLeaf, valueLeafDisabled, hasKeepModel, refsOnArchivedList, bench }) {
  if (!hasProfile) return { tier: 'unplayable', tierReasons: [] };

  // ALPHA -- a piece of the apparatus is missing outright.
  const missing = [];
  if (refs < MIN_OPTIMAL_REFS) {
    missing.push(`${refs}/${MIN_OPTIMAL_REFS} optimal reference games`
      + (refsOnArchivedList ? ` (the ${refsOnArchivedList} saved games were played on an archived list)` : ''));
  }
  // A `<stem>.value.DISABLED.json` is a DECIDED absence, not a missing piece: the value-leaf skill
  // ships a model there when the phase-E adoption A/B measured it as no-benefit for this deck
  // (fluctuator 2026-09-06: quality delta -0.00012 at 1.06x cost). Running without a live sidecar
  // is that deck's measured-best configuration, so it satisfies the apparatus check; the client
  // shows the by-design state in the maturity tooltip instead of an alpha demotion.
  if (!hasValueLeaf && !valueLeafDisabled) missing.push('no value-leaf model');
  if (!hasKeepModel) missing.push('no completed mulligan profile');
  if (missing.length) return { tier: 'alpha', tierReasons: missing };

  // BETA -- complete, but not yet proven at depth or against the human.
  const short = [];
  if (refs < STABLE_REFS) short.push(`${refs}/${STABLE_REFS} reference games`);
  const b = bench || { state: 'unbenched' };
  if (b.state === 'short') {
    short.push(`the search is slower than the human on ${b.short} of ${b.n} references`);
  } else if (b.state === 'stale') {
    // STALE DOES NOT DEMOTE A DECK THAT WAS ALREADY GREEN (user, 2026-08-29: "everything is marked
    // as beta for a period. That is not a good idea. They should be marked as beta only if they
    // were not stable before").
    //
    // Staleness is not a measurement -- it says the cached bench was taken at a different src tree,
    // which EVERY src commit causes for EVERY deck simultaneously. Treating that as a beta reason
    // made the label track "did anyone commit recently", not "is this deck proven", and a badge
    // that lights up on all decks at once carries no information.
    //
    // What the old rule was protecting is kept: this is still the only criterion that can move a
    // deck DOWN after it was fine, because the carried-forward verdict is the LAST REAL
    // MEASUREMENT, not an assumption of green. A deck whose last bench was short stays beta while
    // stale, and when the auto re-bench lands a genuine shortfall the deck demotes then. The only
    // thing that changed is the interim: a previously-green deck is no longer accused of being
    // unproven simply because the clock moved.
    //
    // Residual, stated rather than hidden: during the stale window a REGRESSION introduced by the
    // new src is not yet visible. That is a re-bench latency (minutes, auto-started above), and it
    // is the cost the user chose over labelling every deck beta after every commit.
    if (!b.wasGreen) {
      short.push(b.handMismatch
        ? `${b.handMismatch} reference(s) did not reconstruct as of the last bench (src ${b.at}), so the bench is not a valid comparison`
        : `the search was slower than the human on ${b.short} of ${b.n} references as of the last bench (src ${b.at})`);
    }
  } else if (b.state !== 'green') {
    short.push(b.handMismatch
      ? `${b.handMismatch} reference(s) did not reconstruct, so the bench is not a valid comparison`
      : 'reference bench has never been run for this deck');
  }
  return short.length ? { tier: 'beta', tierReasons: short } : { tier: 'stable', tierReasons: [] };
}

// `version` describes an ARCHIVED list. Its key is the COMPOUND key deck_registry.discover()
// assigns a variant folder (`<deck>_<variant>`), so it reads its OWN bench row and counts its OWN
// references -- never the shipping list's, and never the reverse.
function deckMaturity(dir, name, hasProfile, version) {
  const key = version ? pySlug(name) + '_' + pySlug(version) : pySlug(name);
  const owner = REF_OWNERS[key] || key;
  const saved = countOptimalRefs(name, version);
  // Someone else's games do not count toward this deck. Reported separately so the badge can say
  // WHY a deck with a full folder reads zero -- otherwise it looks like the count is broken.
  const refsOnArchivedList = owner === key ? 0 : saved;
  const refs = owner === key ? saved : 0;
  const hasValueLeaf = fs.existsSync(path.join(dir, name + '.value.json'));
  // Rejected-for-play model shipped inert by the value-leaf skill -- see tierFrom for why this
  // counts as a decided apparatus rather than a missing one.
  const valueLeafDisabled = !hasValueLeaf
    && fs.existsSync(path.join(dir, name + '.value.DISABLED.json'));
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
  return Object.assign({ refs, refGoal, hasValueLeaf, valueLeafDisabled, hasKeepModel, refsOnArchivedList, bench },
                       tierFrom({ hasProfile, refs, hasValueLeaf, valueLeafDisabled, hasKeepModel, refsOnArchivedList, bench }));
}

// ---- SESSION-PINNED ENGINE BINARY + LIVE-FRAME LEDGER ----------------------------
//
// WHAT WENT WRONG (user, 2026-09-10 11:21, EldraziDisplacerFlicker seed 9 gi 8): a long go-off
// session was saved and the written log "just skipped to turn 7" -- 36 decisions, win_turn 7, while
// the human was still mid-turn-4. Nothing was wrong with the choice stream. The 49 picks recorded in
// that session's 11:09 rejection artifact replay PERFECTLY on the session's own engine image: they
// land on decision 49, turn 4, exactly the board the human was looking at.
//
// What changed was the ENGINE UNDERNEATH. /api/save re-runs the FULL accumulated choices in a FRESH
// process, and that process was spawned from build/Release/mtg -- which had been rebuilt at 11:19,
// two minutes earlier, by another agent working in the same checkout. The live --interactive child
// was still executing the pre-rebuild image (a running process keeps its inode), so the two engines
// enumerated different plan fans for the same board:
//
//     decision 27:  live/session image 3528 plans   |  save-time image 1206 plans   -> pick 3526 is
//     decision 28:  live/session image 14112 plans  |  save-time image  423 plans      OUT OF RANGE
//
// A plan index is only meaningful against the fan it was picked from. Once index 3526 fell off the
// end of a 1206-plan menu the replay took some other line, the turn ended early, and every later
// index landed on a different plan -- the game "veered" to an auto-win at turn 7 and the remaining
// picks were simply never consumed. The log was written with no error whatsoever.
//
// TWO FIXES, because either alone leaves a hole:
//
//  1. PIN THE BINARY PER GAME. The first request of a game copies the engine to a session-scoped
//     path under logs/play/.session/ and EVERY later spawn for that game -- interactive child,
//     stateless fallback, /api/validate, the hints, /api/save, /api/save-reference -- runs the COPY.
//     A rebuild mid-session can no longer change a half-played game's replay semantics. A new game
//     (different deck/seed/game-index) re-pins, so new games pick the new binary up immediately.
//
//  2. AUDIT THE SAVE. Pinning cannot cover a server restarted mid-game, an engine nondeterminism, or
//     a carrier that stops threading through. So the save is VERIFIED before it is published: the
//     server already sees every live decision frame (it serves them), so it keeps a per-decision
//     state fingerprint and compares the replayed trace against it -- plus a self-contained
//     out-of-range check that needs no ledger at all. A save that fails is REFUSED, not written.
//
// Both detectors fire on the real incident: the out-of-range check at decision 27, the fingerprint
// at decision 28 (phase pre_main vs post_main, float {G:26} vs none). The out-of-range check was
// run over all 344 committed references and flags none of them.

const SESSION_DIR = path.join(ROOT, 'logs', 'play', '.session');
const STAGING_DIR = path.join(ROOT, 'logs', 'play', '.staging');
const DIVERGED_DIR = path.join(ROOT, 'logs', 'play', 'diverged');

// Pinning is for a LIVE viewer session, where a rebuild can land mid-game. It is OFF when server.js
// is require()d by a headless check: those drive hundreds of distinct (deck, seed) games one-shot,
// so a 6 MB copy per game would be pure overhead against a rebuild that cannot happen. PLAY_PIN_BIN
// forces it either way (=1 on, =0 off) -- test/viewer_save_parity_check.js sets =1 to exercise it.
const PIN_BIN = process.env.PLAY_PIN_BIN === '1'
             || (process.env.PLAY_PIN_BIN !== '0' && require.main === module);

// At most ONE live game (single-user tool), mirroring `isession`. Holds the pinned binary and the
// fingerprints of the decision frames this game has actually been shown.
let gsession = null;   // { key, bin, pinned, frames: Map<decision_index, fingerprint> }

// The identity of a GAME, not of an invocation: `depth` is deliberately absent so the deep-search
// AI hint (which re-runs the same board at HINT_DEPTH) stays inside the session it is hinting for.
function gameKey(p) {
  return [String(p.deck || ''), String(p.version || ''),
          intParam(p.seed, 1), intParam(p.gameIndex, 0), intParam(p.maxTurns, 8)].join('|');
}

// Copy the engine aside. Best-effort by design: a failure here falls back to the shared binary
// rather than taking the viewer down -- the save audit is the backstop that keeps a drifted replay
// from being published either way.
function pinEngineBinary() {
  try {
    fs.mkdirSync(SESSION_DIR, { recursive: true });
    // Sweep pins a previous run leaked (a hard kill skips the exit handler). A day is far longer
    // than any session, so this can never race a live sibling server.
    const DAY = 24 * 60 * 60 * 1000;
    for (const f of fs.readdirSync(SESSION_DIR)) {
      if (!f.startsWith('mtg-')) continue;
      const fp = path.join(SESSION_DIR, f);
      try { if (Date.now() - fs.statSync(fp).mtimeMs > DAY) fs.unlinkSync(fp); } catch (e) {}
    }
    const dst = path.join(SESSION_DIR, `mtg-${process.pid}-${Date.now()}${path.extname(BIN)}`);
    fs.copyFileSync(BIN, dst);
    try { fs.chmodSync(dst, 0o755); } catch (e) {}
    return dst;
  } catch (e) { return null; }
}

function dropPin(sess) {
  if (!sess || !sess.pinned) return;
  // Windows holds an executing image open; an EBUSY here just leaves the file for the sweep above.
  try { fs.unlinkSync(sess.bin); } catch (e) {}
}

let pinWarned = false;
function sessionFor(p) {
  const key = gameKey(p);
  if (gsession && gsession.key === key) return gsession;
  if (gsession) { killIsession(); dropPin(gsession); }
  const pin = PIN_BIN ? pinEngineBinary() : null;
  // SAY SO when the pin could not be taken. Falling back to the shared binary is the right call --
  // refusing to serve a game because a copy failed would be worse -- but an unpinned game is
  // exactly the game that can be corrupted by a rebuild, and silence about it is how the original
  // bug got to be silent. Once per process; the save audit is still the backstop.
  if (PIN_BIN && !pin && !pinWarned) {
    pinWarned = true;
    console.log('  WARNING: could not pin the engine binary for this session (see logs/play/.session/).');
    console.log('           A rebuild during play could change this game\'s replay; saves are still audited.');
  }
  gsession = { key, bin: pin || BIN, pinned: !!pin, frames: new Map() };
  return gsession;
}

// The binary every spawn belonging to `p`'s game must use.
function sessionBin(p) { return sessionFor(p).bin; }

// A cheap, replay-stable fingerprint of the STATE a decision was taken from.
//
// Deliberately NOT the plan menu: `plans` is display-capped (kMaxEmittedPlans) and the --log-dir
// writer additionally re-emits the chosen plan when it sits beyond the cap, so the trace's plan
// array legitimately differs from the live frame's by one entry. Board state has no such licence --
// if the replay reached a different board, these differ, and if it reached the same board they are
// identical. Fields absent on a given decision type simply compare absent-to-absent.
function frameFingerprint(d) {
  if (!d || typeof d !== 'object') return null;
  const me = d.me || {}, opp = d.opponent || {};
  const len = (v) => (Array.isArray(v) ? v.length : -1);
  return [d.type, d.turn, d.phase, me.life, opp.life, me.library_size,
          len(me.hand), len(me.battlefield), len(me.graveyard),
          JSON.stringify(me.floating_mana === undefined ? null : me.floating_mana),
          len(opp.battlefield)].join('|');
}

// Record the frame the human was actually shown. Frames arrive in non-decreasing decision_index
// order along the live branch, so recording index i INVALIDATES everything after it -- which is
// exactly what an undo/rewind does, and is why a rewound branch cannot leave stale entries behind.
function recordFrame(p, d) {
  if (!d || typeof d.decision_index !== 'number') return;
  const s = sessionFor(p);
  for (const k of Array.from(s.frames.keys())) { if (k > d.decision_index) s.frames.delete(k); }
  s.frames.set(d.decision_index, frameFingerprint(d));
}

// PURE, so it is testable without a filesystem or a server (test/viewer_save_parity_check.js).
// `frames` is a Map<decision_index, fingerprint> (may be empty -- then only the self-contained
// out-of-range check runs, and everything else is reported as unverified rather than as passing).
function auditTrace(traceObj, frames) {
  const problems = [];
  const decs = (traceObj && traceObj.decisions) || [];
  let checked = 0, unverified = 0, lastIdx = -1;
  for (const e of decs) {
    const d = (e && e.decision) || {};
    const i = typeof d.decision_index === 'number' ? d.decision_index : -1;
    if (i > lastIdx) lastIdx = i;
    // (1) OUT-OF-RANGE PICK. The writer guarantees a chosen plan is emitted even past the display
    // cap, so in a faithful trace `chosen` is never above the highest emitted plan index. Above it
    // means the pick indexed a fan this replay never produced -- the exact 2026-09-10 signature.
    if (d.type === 'main_phase' && typeof e.chosen === 'number' && e.chosen >= 0) {
      let mx = -1;
      for (const pl of (d.plans || [])) {
        if (pl && typeof pl.index === 'number' && pl.index > mx) mx = pl.index;
      }
      if (e.chosen > mx) {
        problems.push(`decision ${i} (turn ${d.turn}): pick ${e.chosen} is outside the replay's `
                    + `plan menu (highest enumerated index ${mx}) -- the replay saw a different game`);
      }
    }
    // (2) LIVE-FRAME MISMATCH.
    const want = frames && typeof frames.get === 'function' ? frames.get(i) : undefined;
    if (want === undefined) { unverified++; continue; }
    checked++;
    const got = frameFingerprint(d);
    if (want !== got) {
      problems.push(`decision ${i} (turn ${d.turn}): the replay's board is not the one you played `
                  + `from  [live ${want}] != [replay ${got}]`);
    }
  }
  // (3) TRUNCATION. The user's corrupted log ended after 36 decisions on a session that had played
  // far more: the replay's game simply finished early. A live frame past the trace's last decision
  // is that, stated directly.
  let maxLive = -1;
  if (frames && typeof frames.forEach === 'function') { frames.forEach((_, k) => { if (k > maxLive) maxLive = k; }); }
  if (maxLive > lastIdx) {
    problems.push(`the replay ended after decision ${lastIdx}, but this session reached decision `
                + `${maxLive} -- the saved log would be TRUNCATED`);
  }
  return { ok: problems.length === 0, checked, unverified, decisions: decs.length, problems };
}

function rmrf(p) { try { fs.rmSync(p, { recursive: true, force: true }); } catch (e) {} }

function moveFile(src, dst) {
  fs.mkdirSync(path.dirname(dst), { recursive: true });
  try { fs.renameSync(src, dst); }
  catch (e) { fs.copyFileSync(src, dst); try { fs.unlinkSync(src); } catch (e2) {} }   // EXDEV
}

// Replay the full choice stream with --log-dir, AUDIT the trace, and only then publish it.
//
// The replay writes into a STAGING directory, never straight into its destination. That is what
// makes "refuse" possible at all -- and it is load-bearing for /api/save-reference, whose
// destination is references/, where a corrupted overwrite would destroy user ground truth that the
// repo's own rules say may never be discarded.
function saveTrace(p, destDir) {
  const seed = intParam(p.seed, 1), gi = intParam(p.gameIndex, 0);
  const name = `claude_s${seed}_gi${gi}.json`;
  const stage = path.join(STAGING_DIR, `${process.pid}-${Date.now()}`);
  fs.mkdirSync(stage, { recursive: true });
  const r = runStep(p, stage);
  const staged = path.join(stage, name);
  // No trace: the game did not end on this choice stream (the engine emitted the next decision
  // instead). Unchanged behaviour -- savedAs null, and the caller reports whatever `r` says.
  if (!fs.existsSync(staged)) { rmrf(stage); return { ...r, savedAs: null }; }

  let audit;
  try { audit = auditTrace(JSON.parse(fs.readFileSync(staged, 'utf8')), sessionFor(p).frames); }
  catch (e) { audit = { ok: false, checked: 0, unverified: 0, decisions: 0,
                        problems: ['the replayed trace could not be read back: ' + e.message] }; }

  if (!audit.ok) {
    const keptAt = path.join(DIVERGED_DIR, `${Date.now()}_${name}`);
    moveFile(staged, keptAt);
    rmrf(stage);
    const rel = path.relative(ROOT, keptAt);
    return {
      kind: 'error', savedAs: null, diverged: audit.problems,
      divergedAs: rel,
      error: 'SAVE REFUSED -- the replay diverged from the game you played, so the log would be '
           + 'wrong. Most likely the engine was rebuilt mid-session (a new binary enumerates a '
           + 'different plan fan, so your recorded plan indices no longer mean the same lines). '
           + 'Nothing was written to ' + path.relative(ROOT, path.join(destDir, name)) + '; the '
           + 'diverged replay is kept at ' + rel + ' for triage. '
           + audit.problems.slice(0, 3).join(' | ')
           + (audit.problems.length > 3 ? ` | (+${audit.problems.length - 3} more)` : ''),
    };
  }

  const dst = path.join(destDir, name);
  moveFile(staged, dst);
  rmrf(stage);
  return { ...r, savedAs: path.relative(ROOT, dst),
           verified: audit.checked, unverified: audit.unverified };
}

// Session end = process end (single-user tool, one live game). Keyed on PIN_BIN rather than on
// "am I the server" so a require()d harness that opted INTO pinning still cleans up after itself.
if (PIN_BIN) {
  const bye = () => { killIsession(); if (gsession) dropPin(gsession); };
  process.on('exit', bye);
  for (const sig of ['SIGINT', 'SIGTERM']) {
    process.on(sig, () => { bye(); process.exit(0); });
  }
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
    out.push(Object.assign({ id: deckFile, deck: deckFile, name, version: null, label: name, hasProfile },
                           deckMaturity(dir, name, hasProfile, null)));
    // ARCHIVED / VARIANT lists: decks/<Name>/<Variant>/<Name>.cod, kept beside the shipping list
    // with the artifacts fitted to it. Offered as their own selectable entries so an older list can
    // still be played and replayed -- its references are inputs to the system and still catch bugs,
    // which they cannot do if the list itself is unreachable from the UI. Files inside a variant
    // are named after the PARENT deck (the engine resolves sidecars directory-relative off the
    // profile), so the lookup is the parent's stem inside the subfolder.
    for (const sub of fs.readdirSync(dir)) {
      const subdir = path.join(dir, sub);
      if (!fs.statSync(subdir).isDirectory()) continue;
      let vFile = null;
      for (const ext of ['.cod', '.txt']) {
        if (fs.existsSync(path.join(subdir, name + ext))) { vFile = name + ext; break; }
      }
      if (!vFile) continue;
      const vProfile = fs.existsSync(path.join(subdir, name + '.profile.json'));
      out.push(Object.assign({ id: vFile + '@' + sub, deck: vFile, name, version: sub,
                              label: name + '  \u2014  ' + sub, hasProfile: vProfile },
                             deckMaturity(subdir, name, vProfile, sub)));
    }
  }
  // shipping lists first, then archived variants, then alpha by label
  out.sort((a, b) => (b.hasProfile - a.hasProfile) || (!!a.version - !!b.version)
                     || String(a.label).localeCompare(String(b.label)));
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
        const { stem, version } = resolveDeck(url.searchParams.get('deck'), url.searchParams.get('version'));
        const seed = intParam(url.searchParams.get('seed'), 1);
        const gi = intParam(url.searchParams.get('gi'), 0);
        // Scoped to the LIST being played. Un-scoped, this reported "reference saved" for a seed
        // whose game was played on a DIFFERENT list -- telling the user to skip a game they have
        // never actually played on the deck in front of them.
        const vseg = version ? [version] : [];
        const rel = path.join('references', safeStem(stem), ...vseg, `claude_s${seed}_gi${gi}.json`);
        const exists = fs.existsSync(path.join(ROOT, rel));
        const relSub = path.join('references', 'suboptimal', safeStem(stem), ...vseg, `claude_s${seed}_gi${gi}.json`);
        const suboptimal = fs.existsSync(path.join(ROOT, relSub));
        return sendJson(res, 200, { exists, path: exists ? rel : null,
                                    suboptimal, suboptimalPath: suboptimal ? relSub : null });
      } catch (e) {
        return sendJson(res, 200, { exists: false, path: null });
      }
    }
    if (req.method === 'POST' && url.pathname === '/api/step') {
      const p = await readBody(req);
      const out = await runStepCached(p, null);
      // Ledger the frame the human is about to answer -- this is the ONLY record of what the live
      // session actually saw, and it is what /api/save is audited against (see auditTrace).
      if (out && out.kind === 'decision') recordFrame(p, out.decision);
      return sendJson(res, 200, out);
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
      // Served by the LIVE interactive child when it is parked on exactly this frame (see
      // runValidateCached); otherwise the unchanged full-prefix spawn.
      const out = await runValidateCached(p, String(p.line == null ? '' : p.line));
      // A validation carries the same pending decision frame; a rejected line then re-steps into it,
      // so ledgering here keeps the audit complete across a reject/retry.
      // Both shapes carry the pending frame under `decision` (a verdict nests it; a non-main-phase
      // fallthrough IS one), so one line covers the verdict, the fallthrough and a reject/retry.
      if (out && out.decision) recordFrame(p, out.decision);
      return sendJson(res, 200, out);
    }
    if (req.method === 'POST' && url.pathname === '/api/reject-artifact') {
      // Persist a rejected line ("I tried X, the model wouldn't take it") for later triage.
      // Gitignored scratch under logs/play/rejections/. The body carries the full context the
      // verdict was computed from (state snapshot + attempted line + verdict + reason).
      const p = await readBody(req);
      const dir = path.join(ROOT, 'logs', 'play', 'rejections');
      fs.mkdirSync(dir, { recursive: true });
      const seed = intParam(p.seed, 1), gi = intParam(p.gameIndex, 0), turn = intParam(p.turn, 0);
      const vtag = p.version ? '_' + safeStem(p.version) : '';
      const fn = `${safeStem(p.deck || 'deck')}${vtag}_s${seed}_gi${gi}_t${turn}.json`;
      const artifact = {
        savedFrom: 'tools/play', deck: p.deck, version: p.version || null, seed, gameIndex: gi, turn,
        phase: p.phase || null,
        priorChoices: Array.isArray(p.choices) ? p.choices : [],
        attemptedLine: p.attemptedLine || null,     // {land, casts[]}
        encodedLine: p.line || null,
        verdict: p.verdict || null,                 // illegal | legal_not_enumerated | unsupported
        reason: p.reason || null,
        failedAction: p.failed_action || null,
        modelPlans: p.modelPlans || null,           // what the model WOULD play here
        state: p.state || null,                     // me/opponent snapshot at the decision
        // The main-ordinal-keyed pins of every EARLIER decision, carrying their queued action order
        // AND their MANUAL TAP/PAY `tap=` entries (docs/design/viewer-manual-tap-pay.md). Without
        // it the reproduce command replays those decisions in the engine's own order and with the
        // engine's own mana allocation, so it reaches a different board and the recorded verdict is
        // simply not reachable -- priorChoices pins WHICH plan, never how it was ordered or paid.
        // This line's own taps are already inside `encodedLine`.
        castOrder: (p.castOrder && Object.keys(p.castOrder).length) ? p.castOrder : null,
        // The KEYED side channels the session was carrying. They are not part of --choices, so an
        // artifact holding only priorChoices records a repro that is NOT the game the user played
        // (a cast-order pin in particular changes which line is applied). Recorded verbatim, and
        // spelled out in `note` below, so the reproduce line is the whole invocation
        // (sideChannelArgs derives from buildArgs, so it carries castOrderArg's output too).
        sideChannels: {
          castOrder: p.castOrder || {}, firebreathe: p.firebreathe || {},
          jitte: p.jitte || {}, storageHold: p.storageHold || {},
        },
        note: 'Reproduce: --claude-play --seed <seed> --game-index <gi> --choices "' +
              (Array.isArray(p.choices) ? p.choices.join(',') : '') +
              '"' + sideChannelArgs(p) +
              ' --validate-line "' + (p.line || '') + '"',
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
      const { stem, version } = resolveDeck(p.deck, p.version);
      // A reference belongs to the LIST it was played on, so an archived list's games are saved
      // one level deeper -- references/<Deck>/<Version>/ mirroring decks/<Deck>/<Version>/. This is
      // the root-cause fix: previously every game landed in references/<Deck>/ whatever list was
      // being played, so replacing a deck's shipping list silently mixed two lists' games in one
      // folder, and each was then benched against whichever list the folder was bound to.
      const dir = p.suboptimal
        ? path.join(ROOT, 'references', 'suboptimal', safeStem(stem), ...(version ? [version] : []))
        : path.join(ROOT, 'references', safeStem(stem), ...(version ? [version] : []));
      fs.mkdirSync(dir, { recursive: true });
      // Staged + AUDITED (saveTrace): a reference is user ground truth the repo's rules say may
      // never be discarded, so a replay that diverged from the played game must not be able to land
      // on top of one. A refusal returns kind:'error' with savedAs null -- the GUI already renders
      // that as "save failed: <error>", and nothing is written.
      return sendJson(res, 200, saveTrace(p, dir));
    }
    if (req.method === 'POST' && url.pathname === '/api/save') {
      // Re-run the FULL accumulated choices with --log-dir so RunClaudePlay writes the
      // deterministic per-game decision trace (the "trustworthy reference" artifact).
      // It is also viewable in tools/replay/ and re-playable from (deck, seed, gi, choices).
      //
      // AUDITED (saveTrace). This is the route that wrote the 2026-09-10 corrupted log: the replay
      // ran on a binary rebuilt two minutes earlier, veered at decision 27, and the wrong game was
      // published under the right name with no error at all. It is now staged, checked against the
      // frames this session was actually shown, and refused if they disagree.
      const p = await readBody(req);
      const logDir = path.join(ROOT, 'logs', 'play');
      fs.mkdirSync(logDir, { recursive: true });
      return sendJson(res, 200, saveTrace(p, logDir));
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
                   // save-path integrity, for test/viewer_save_parity_check.js. `httpServer` is the
                   // unbound http.Server, so that check can listen(0) and drive the REAL routes --
                   // the route wiring (does /api/step ledger? does /api/save audit?) is not
                   // reachable by calling the helpers directly, and that wiring is the whole fix.
                   runStepCached, runValidateCached, saveTrace, auditTrace, frameFingerprint, recordFrame,
                   sessionBin, sessionFor, gameKey, PIN_BIN, SESSION_DIR, httpServer: server,
                   // deck maturity, for test/viewer_deck_beta_check.js
                   tierFrom, benchState, deckMaturity, countOptimalRefs, MIN_OPTIMAL_REFS, STABLE_REFS, KEEPMODEL_EXTS,
                   pySlug, referenceOwners };
