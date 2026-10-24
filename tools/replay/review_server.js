// Reference REVIEW server -- serves tools/replay/review.html over the games scripts/ref_review.py laid
// out under logs/ref_review/, and records the user's verdict on each one.
//
//   ./review.sh                      # or: node tools/replay/review_server.js   -> http://localhost:8090
//
// WHY A SERVER AT ALL. The replay viewer (index.html) is file:// and read-only. A review produces USER
// WORK -- a verdict per game -- and that has to land in the repo, so something has to write it.
//
// VERDICTS ARE TREATED LIKE REFERENCES (they are hand-made user judgements that cannot be regenerated).
// They live in the TRACKED file references/review/verdicts.json, and every save goes through the play
// viewer's own safety layer (tools/play/refsafe.js): an out-of-repo backup copy, a commit to the
// references-autosave branch with git plumbing (working tree, index and branch untouched), and a push.
// The same root gate applies -- the server refuses to start from a temp dir or a linked worktree,
// where a verdict file would die with the directory (CLAUDE.md, "THE PLAY SERVER MUST NEVER RUN FROM A
// TEMPORARY FOLDER").
//
// Node built-ins only. Binds 127.0.0.1 (REVIEW_HOST=0.0.0.0 for container port-forwarding).

const fs = require('fs');
const http = require('http');
const path = require('path');
const { spawn } = require('child_process');
const refsafe = require('../play/refsafe');

const ROOT = path.resolve(__dirname, '..', '..');
const REPLAY_DIR = __dirname;
const VERDICTS_REL = path.join('references', 'review', 'verdicts.json');
// The live paths. test/review_viewer_check.js points them at a fixture (and stubs `protect`) through
// makeServer's options, so a check never writes the user's verdict file or pushes the autosave branch.
const DEFAULTS = { reviewDir: path.join(ROOT, 'logs', 'ref_review'), verdicts: path.join(ROOT, VERDICTS_REL),
                   protect: (rels, message) => refsafe.protect(ROOT, rels, { message }),
                   promote: runPromote };
// The label verdicts. The fourth outcome, "promoted", is an ACTION (/api/promote: the search's line becomes
// the reference) and is recorded by scripts/ref_promote.py itself, never by a label click.
const KINDS = new Set(['clairvoyance', 'bug', 'replay_manually']);

// scripts/ref_promote.py --apply, ASYNC (a promotion replays the game several times -- seconds to a
// minute -- and a spawnSync would freeze every other request). Resolves to its one JSON result line.
function runPromote({ key, source, note }) {
  return new Promise((resolve) => {
    const py = process.platform === 'win32' ? 'python' : 'python3';
    const p = spawn(py, [path.join(ROOT, 'scripts', 'ref_promote.py'), key, '--source', source,
                         '--apply', '--json', '--note', note || ''], { cwd: ROOT });
    let out = '', err = '';
    p.stdout.on('data', d => { out += d; });
    p.stderr.on('data', d => { err += d; });
    p.on('close', (code) => {
      const last = out.trim().split(/\r?\n/).pop() || '';
      try { resolve(JSON.parse(last)); }
      catch (e) { resolve({ ok: false, error: `ref_promote.py exited ${code}: ${(err || out).slice(-600)}` }); }
    });
    p.on('error', (e) => resolve({ ok: false, error: String(e) }));
  });
}
const PORT = +(process.env.PORT || 8090);
const HOST = process.env.REVIEW_HOST || '127.0.0.1';

function readJson(p, dflt) {
  try { return JSON.parse(fs.readFileSync(p, 'utf8')); } catch (e) { return dflt; }
}

// Same rule as scripts/ref_review.py verdict_state: a verdict judged one search LINE; when the engine
// plays a different line now, the verdict is shown as stale instead of silently carried over.
function verdictState(entry, v) {
  if (!v) return 'unreviewed';
  return v.search_digest === entry.search_digest ? 'current' : 'stale';
}

// The verdict record -- the SAME shape scripts/ref_review.py `stamp` writes.
function stamp(e, verdict, note) {
  return { verdict, note: note || '', human_win: e.human_win, search_win: e.search_win,
           search_digest: e.search_digest, lost_turns: (e.lost_turns || []).map(x => x.turn),
           hint: e.hint, at: new Date().toISOString().replace(/\.\d+Z$/, '+00:00') };
}

function send(res, code, body, type) {
  res.writeHead(code, { 'Content-Type': type || 'application/json; charset=utf-8', 'Cache-Control': 'no-store' });
  res.end(typeof body === 'string' || Buffer.isBuffer(body) ? body : JSON.stringify(body));
}

// A file strictly inside `base`, or null (no '..' escapes, no absolute paths).
function inside(base, rel) {
  const p = path.resolve(base, '.' + path.sep + rel);
  return (p === base || p.startsWith(base + path.sep)) ? p : null;
}

const TYPES = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8',
                '.json': 'application/json; charset=utf-8', '.md': 'text/markdown; charset=utf-8' };

function serveFile(res, p) {
  if (!p || !fs.existsSync(p) || !fs.statSync(p).isFile()) return send(res, 404, { error: 'not found' });
  send(res, 200, fs.readFileSync(p), TYPES[path.extname(p)] || 'application/octet-stream');
}

function readBody(req) {
  return new Promise((resolve, reject) => {
    let b = '';
    req.on('data', c => { b += c; if (b.length > 1e6) { reject(new Error('body too large')); req.destroy(); } });
    req.on('end', () => resolve(b));
    req.on('error', reject);
  });
}

// Stable key order so the tracked file diffs cleanly (matches json.dump(sort_keys=True) in Python).
function sortKeys(o) {
  if (Array.isArray(o)) return o.map(sortKeys);
  if (o && typeof o === 'object') {
    const r = {};
    for (const k of Object.keys(o).sort()) r[k] = sortKeys(o[k]);
    return r;
  }
  return o;
}

function makeServer(opts) {
  const o = Object.assign({}, DEFAULTS, opts || {});
  const loadVerdicts = () => readJson(o.verdicts, { schema: 1, verdicts: {} });
  const loadIndex = () => readJson(path.join(o.reviewDir, 'index.json'), null);
  let writing = Promise.resolve();      // serialise verdict writes (read-modify-write of one file)

  async function saveVerdict(body) {
    const idx = loadIndex();
    if (!idx) return [409, { error: 'no logs/ref_review/index.json -- run scripts/ref_review.py build' }];
    const e = idx.games.find(g => g.key === body.key);
    if (!e) return [404, { error: `'${body.key}' is not in the review index` }];
    if (body.verdict !== null && !KINDS.has(body.verdict)) return [400, { error: 'bad verdict' }];
    const v = loadVerdicts();
    v.verdicts = v.verdicts || {};
    if (body.verdict === null) delete v.verdicts[body.key];
    else v.verdicts[body.key] = stamp(e, body.verdict, body.note);
    fs.mkdirSync(path.dirname(o.verdicts), { recursive: true });
    const tmp = o.verdicts + '.tmp';
    fs.writeFileSync(tmp, JSON.stringify(sortKeys(v), null, 1) + '\n');
    fs.renameSync(tmp, o.verdicts);
    const backup = await o.protect([VERDICTS_REL],
      `review verdict: ${body.key} -> ${body.verdict === null ? 'cleared' : body.verdict}`);
    return [200, { ok: true, verdict: v.verdicts[body.key] || null, state: verdictState(e, v.verdicts[body.key]),
                   backup }];
  }

  // Promote: the new reference, the human's preserved line and the verdict file are all user work, so
  // every file the promotion wrote goes through protect() (backup + autosave commit + push).
  async function promote(body) {
    const idx = loadIndex();
    if (!idx || !idx.games.find(g => g.key === body.key)) return [404, { error: `'${body.key}' is not in the review index` }];
    if (!/^(search\.json|handoff_T\d+\.json)$/.test(String(body.source))) return [400, { error: 'bad source' }];
    const r = await o.promote({ key: body.key, source: body.source, note: body.note });
    if (!r.ok) return [200, r];
    r.backup = await o.protect(r.files, `promote search line: ${body.key} (${body.source}, T${r.win_turn})`);
    const v = loadVerdicts().verdicts || {};
    const e = loadIndex().games.find(g => g.key === body.key);
    r.verdict = v[body.key] || null;
    r.state = verdictState(e, r.verdict);
    return [200, r];
  }

  return http.createServer(async (req, res) => {
    const url = new URL(req.url, 'http://x');
    try {
      if (req.method === 'GET' && (url.pathname === '/' || url.pathname === '/review.html'))
        return serveFile(res, path.join(REPLAY_DIR, 'review.html'));
      if (req.method === 'GET' && (url.pathname === '/viewer' || url.pathname === '/index.html'))
        return serveFile(res, path.join(REPLAY_DIR, 'index.html'));
      if (req.method === 'GET' && url.pathname === '/api/review') {
        const idx = loadIndex();
        if (!idx) return send(res, 200, { meta: null, games: [] });
        const vs = loadVerdicts().verdicts || {};
        for (const g of idx.games) { g.verdict = vs[g.key] || null; g.verdict_state = verdictState(g, vs[g.key]); }
        return send(res, 200, idx);
      }
      if (req.method === 'GET' && url.pathname.startsWith('/review/'))
        return serveFile(res, inside(o.reviewDir, decodeURIComponent(url.pathname.slice('/review/'.length))));
      if (req.method === 'POST' && url.pathname === '/api/verdict') {
        const body = JSON.parse(await readBody(req) || '{}');
        const job = writing.then(() => saveVerdict(body));
        writing = job.catch(() => {});
        const [code, out] = await job;
        return send(res, code, out);
      }
      if (req.method === 'POST' && url.pathname === '/api/promote') {
        const body = JSON.parse(await readBody(req) || '{}');
        const job = writing.then(() => promote(body));
        writing = job.catch(() => {});
        const [code, out] = await job;
        return send(res, code, out);
      }
      send(res, 404, { error: 'not found' });
    } catch (err) {
      send(res, 500, { error: String(err && err.stack || err) });
    }
  });
}

if (require.main === module) {
  refsafe.refuseUnsafeRoot(ROOT);
  const server = makeServer();
  server.listen(PORT, HOST, () => {
    console.log(`MagicDeckTester reference review: http://localhost:${PORT}  (bound ${HOST}:${PORT})`);
    const games = path.join(DEFAULTS.reviewDir, 'index.json');
    console.log(`  games:    ${DEFAULTS.reviewDir}${fs.existsSync(games) ? '' : '  (EMPTY -- run python3 scripts/ref_review.py build)'}`);
    console.log(`  verdicts: ${DEFAULTS.verdicts} (backed up to ${refsafe.backupRoot(ROOT)} + branch ${refsafe.AUTOSAVE_BRANCH})`);
  });
}

module.exports = { makeServer, stamp, verdictState, inside };
