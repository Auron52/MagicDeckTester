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
const { spawnSync } = require('child_process');

const ROOT = path.resolve(__dirname, '..', '..');          // repo root
const DECKS_DIR = path.join(ROOT, 'decks');
const CARDS_JSON = path.join(ROOT, 'src', 'cards', 'data', 'cards.json');
const BIN = process.env.MTG_BIN || path.join(ROOT, 'build', 'Release', 'mtg');
const PORT = parseInt(process.env.PORT || '8080', 10);
const STEP_TIMEOUT_MS = 120000;   // generous: late-turn replays + opponent AI

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

// Resolve a user-supplied deck name to a safe path inside decks/ plus its sibling profile.
function resolveDeck(deckName) {
  if (typeof deckName !== 'string' || !deckName) throw new Error('deck required');
  const base = path.basename(deckName);                    // strip any path components
  const deckPath = path.join(DECKS_DIR, base);
  if (path.dirname(deckPath) !== DECKS_DIR) throw new Error('deck must be under decks/');
  if (!fs.existsSync(deckPath)) throw new Error('deck not found: ' + base);
  const stem = base.replace(/\.[^.]+$/, '');
  const profilePath = path.join(DECKS_DIR, stem + '.profile.json');
  return { deckPath, profilePath: fs.existsSync(profilePath) ? profilePath : null, stem };
}

function intParam(v, dflt) {
  const n = parseInt(v, 10);
  return Number.isFinite(n) ? n : dflt;
}

// Build the argv for one --claude-play invocation.
// validateLine (optional): an encoded human-assembled line ("land=X;cast=Y;...") to reconcile
// against the model at the first un-chosen main phase instead of dumping the plan menu.
function buildArgs(p, logDir, validateLine) {
  const { deckPath, profilePath } = resolveDeck(p.deck);
  const args = [deckPath];
  if (profilePath) args.push('--profile', profilePath);
  args.push('--cards-json', CARDS_JSON);
  args.push('--claude-play');
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
  if (validateLine != null) args.push('--validate-line', validateLine);
  return args;
}

// Run the binary once for the given choices; classify the output.
function runStep(p, logDir) {
  const args = buildArgs(p, logDir, null);
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
  for (const f of fs.readdirSync(DECKS_DIR)) {
    if (!/\.(txt|cod)$/i.test(f)) continue;
    if (/\.profile\./i.test(f) || /\.constraints\./i.test(f)) continue;
    const stem = f.replace(/\.[^.]+$/, '');
    const hasProfile = fs.existsSync(path.join(DECKS_DIR, stem + '.profile.json'));
    out.push({ deck: f, name: stem, hasProfile });
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
        return sendJson(res, 200, { exists, path: exists ? rel : null });
      } catch (e) {
        return sendJson(res, 200, { exists: false, path: null });
      }
    }
    if (req.method === 'POST' && url.pathname === '/api/step') {
      const p = await readBody(req);
      return sendJson(res, 200, runStep(p, null));
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
      // Promote a CLEAN human-played game (no rejects) to the permanent, tracked references set.
      // These are no-clairvoyance ground-truth games whose win-turn a good AI should match.
      const p = await readBody(req);
      const { stem } = resolveDeck(p.deck);
      const dir = path.join(ROOT, 'references', safeStem(stem));
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

server.listen(PORT, '127.0.0.1', () => {
  console.log(`MagicDeckTester play GUI: http://localhost:${PORT}`);
  console.log(`  binary: ${BIN} ${fs.existsSync(BIN) ? '(found)' : '(MISSING — build Release first)'}`);
  console.log(`  decks:  ${DECKS_DIR}`);
});
