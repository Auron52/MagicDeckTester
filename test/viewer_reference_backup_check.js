#!/usr/bin/env node
// REFERENCE SAFETY check (tools/play/refsafe.js + the server.js wiring).
//
// On 2026-10-05 a week of the user's hand-played reference games was lost: a viewer had been
// started from a scratch worktree and saved into it, and the container was rebuilt. This pins every
// layer added in response, against SCRATCH git repos (never the real one):
//   A. protect(): out-of-repo backup copy (never overwritten; a new version gets a timestamped
//      sibling), autosave commit on refs/heads/references-autosave via plumbing -- with the working
//      tree, index, HEAD and current branch byte-for-byte untouched -- and the push to origin.
//   B. a push FAILURE is reported (pushed:false + error), never thrown, and the commit still lands;
//      a remote autosave branch that moved on (another machine) is rebuilt on LINEARLY, no merges.
//   C. the startup scan finds an untracked reference, protects it, and is quiet the second time.
//   D. the REAL /api/save-reference route over a socket: a played game is saved, backed up,
//      autosave-committed and pushed, and the response carries `backup`; then with origin broken
//      the same route still saves but reports the push failure. Needs the binary.
//   F. the scanner flags an unprotected reference in a worktree, safe_worktree_remove.sh refuses it,
//      and both pass once it is on the autosave branch.
//   E. the server REFUSES to listen from a linked git worktree (exit 2) and does start from the
//      primary checkout of the same scratch repo (control arm).
//
// Usage: node test/viewer_reference_backup_check.js     (exit 0 pass, 1 fail, 2 setup)
'use strict';
const fs = require('fs');
const os = require('os');
const path = require('path');
const http = require('http');
const { spawnSync, spawn } = require('child_process');

const ROOT = path.resolve(__dirname, '..');
let fails = 0;
const fail = (m) => { console.log('  FAIL: ' + m); fails++; };
const ok = (m) => console.log('  ok: ' + m);
const chk = (c, m) => (c ? ok(m) : fail(m));

function git(cwd, ...args) {
  const r = spawnSync('git', args, { cwd, encoding: 'utf8',
    env: Object.assign({}, process.env, { GIT_AUTHOR_NAME: 't', GIT_AUTHOR_EMAIL: 't@t',
                                          GIT_COMMITTER_NAME: 't', GIT_COMMITTER_EMAIL: 't@t' }) });
  if (r.status !== 0) throw new Error(`git ${args.join(' ')} in ${cwd}: ${r.stderr}`);
  return r.stdout.trim();
}
const tryGit = (cwd, ...a) => { try { return git(cwd, ...a); } catch (e) { return null; } };

// Scratch area. The server's temp-dir guard only applies at LISTEN, so A-D (require()d) can live in
// os.tmpdir(); E spawns a real server and must be OUTSIDE the temp dir so the only thing that can
// refuse it is the linked-worktree rule -- it uses the repo's own gitignored logs/.
const SCRATCH = fs.mkdtempSync(path.join(os.tmpdir(), 'mdt-refsafe-'));
const BACKUPS = path.join(SCRATCH, 'backups');
process.env.MDT_REFERENCE_BACKUP_DIR = BACKUPS;
const refsafe = require(path.join(ROOT, 'tools', 'play', 'refsafe.js'));

function makeRepo(name) {
  const origin = path.join(SCRATCH, name + '-origin.git');
  const repo = path.join(SCRATCH, name);
  git(SCRATCH, 'init', '-q', '--bare', origin);
  git(SCRATCH, 'init', '-q', '-b', 'main', repo);
  fs.mkdirSync(path.join(repo, 'references', 'Deck'), { recursive: true });
  fs.writeFileSync(path.join(repo, 'references', 'Deck', 'claude_s1_gi0.json'), '{"tracked":1}\n');
  fs.writeFileSync(path.join(repo, 'README'), 'x\n');
  git(repo, 'add', '-A'); git(repo, 'commit', '-q', '-m', 'init');
  git(repo, 'remote', 'add', 'origin', origin);
  git(repo, 'push', '-q', 'origin', 'main');
  return { repo, origin };
}

// Everything a careless implementation could disturb: HEAD, the current branch, the index FILE
// bytes, and porcelain status.
function snapshot(repo) {
  const idx = path.join(repo, '.git', 'index');
  return { head: git(repo, 'rev-parse', 'HEAD'), branch: git(repo, 'symbolic-ref', 'HEAD'),
           index: fs.readFileSync(idx).toString('base64'),
           status: git(repo, 'status', '--porcelain=v1', '--untracked-files=all') };
}
function sameSnap(a, b, label) {
  chk(a.head === b.head, `${label}: HEAD unchanged`);
  chk(a.branch === b.branch, `${label}: current branch unchanged (${b.branch})`);
  chk(a.index === b.index, `${label}: index file byte-identical`);
  chk(a.status === b.status, `${label}: working-tree status unchanged`);
}
const blobAt = (repo, ref, p) => tryGit(repo, 'rev-parse', `${ref}:${p}`);
const hashFile = (repo, p) => git(repo, 'hash-object', path.join(repo, p));

async function testProtect() {
  console.log('--- A. protect(): backup + plumbing autosave + push ---');
  const { repo, origin } = makeRepo('a');
  const rel = path.join('references', 'Deck', 'claude_s2_gi1.json');
  fs.writeFileSync(path.join(repo, rel), '{"game":"v1"}\n');
  const before = snapshot(repo);
  const b = await refsafe.protect(repo, [rel]);
  sameSnap(before, snapshot(repo), 'after protect');
  chk(b.ok === true && b.errors.length === 0, `status ok (errors: ${JSON.stringify(b.errors)})`);
  const bk = path.join(BACKUPS, rel);
  chk(fs.existsSync(bk) && fs.readFileSync(bk, 'utf8') === '{"game":"v1"}\n', `backup copy written at ${bk}`);
  chk(b.autosave_commit && blobAt(repo, refsafe.AUTOSAVE_REF, 'references/Deck/claude_s2_gi1.json') === hashFile(repo, rel),
      'references-autosave commit contains the file (same blob)');
  chk(b.pushed === true && blobAt(origin, 'refs/heads/references-autosave', 'references/Deck/claude_s2_gi1.json') === hashFile(repo, rel),
      'pushed: origin/references-autosave has the file');
  chk(tryGit(repo, 'rev-parse', `${refsafe.AUTOSAVE_REF}:README`) === null, 'autosave tree holds only references (no source files)');

  // second VERSION of the same reference: new timestamped backup, old backup untouched, new commit on top
  const first = b.autosave_commit;
  fs.writeFileSync(path.join(repo, rel), '{"game":"v2"}\n');
  const b2 = await refsafe.protect(repo, [rel]);
  const sibs = fs.readdirSync(path.dirname(bk)).filter(f => f.startsWith('claude_s2_gi1.'));
  chk(fs.readFileSync(bk, 'utf8') === '{"game":"v1"}\n', 'existing backup NOT overwritten by a new version');
  chk(sibs.length === 2 && b2.localPaths[0] !== bk && fs.readFileSync(b2.localPaths[0], 'utf8') === '{"game":"v2"}\n',
      `new version backed up as a timestamped sibling (${path.basename(b2.localPaths[0])})`);
  chk(git(repo, 'rev-parse', `${b2.autosave_commit}^`) === first, 'autosave history is linear (v2 parent = v1 commit)');
  // re-protecting identical bytes is a no-op (no new commit, no new backup)
  const b3 = await refsafe.protect(repo, [rel]);
  chk(b3.autosave_commit === b2.autosave_commit, 'identical re-save: no new autosave commit');
  chk(fs.readdirSync(path.dirname(bk)).filter(f => f.startsWith('claude_s2_gi1.')).length === 2, 'identical re-save: no new backup file');
  return { repo, origin };
}

async function testPushFailureAndDivergence() {
  console.log('--- B. push failure surfaces; a moved remote is rebuilt on linearly ---');
  const { repo, origin } = makeRepo('b');
  const rel = path.join('references', 'Deck', 'claude_s3_gi2.json');
  fs.writeFileSync(path.join(repo, rel), '{"game":"b"}\n');
  git(repo, 'remote', 'set-url', 'origin', path.join(SCRATCH, 'does-not-exist.git'));
  const before = snapshot(repo);
  let threw = null, b = null;
  try { b = await refsafe.protect(repo, [rel]); } catch (e) { threw = e; }
  chk(!threw, 'push failure does not throw');
  chk(b && b.pushed === false && b.ok === false && b.errors.some(e => /push/i.test(e)),
      `push failure reported: pushed=false, error="${b && (b.errors[0] || '').slice(0, 80)}"`);
  chk(b && b.autosave_commit && b.local === 'ok', 'backup + autosave commit still landed despite the push failure');
  sameSnap(before, snapshot(repo), 'after failed push');

  // Another machine's viewer pushed its own autosave in the meantime.
  git(repo, 'remote', 'set-url', 'origin', origin);
  const other = path.join(SCRATCH, 'b-other');
  git(SCRATCH, 'clone', '-q', origin, other);
  fs.mkdirSync(path.join(other, 'references', 'Other'), { recursive: true });
  fs.writeFileSync(path.join(other, 'references', 'Other', 'claude_s9_gi8.json'), '{"other":1}\n');
  const oprot = await refsafe.protect(other, [path.join('references', 'Other', 'claude_s9_gi8.json')]);
  chk(oprot.pushed === true, 'second machine pushed its autosave first');
  const b2 = await refsafe.protect(repo, [rel]);
  const tip = git(origin, 'rev-parse', 'refs/heads/references-autosave');
  chk(b2.pushed === true, `diverged push recovered (${(b2.errors || []).join(' | ') || 'no errors'})`);
  chk(blobAt(origin, tip, 'references/Other/claude_s9_gi8.json') && blobAt(origin, tip, 'references/Deck/claude_s3_gi2.json'),
      'remote tip holds BOTH machines\' references');
  const merges = git(origin, 'rev-list', '--merges', 'refs/heads/references-autosave');
  chk(merges === '', 'no merge commits on references-autosave');
}

async function testStartupScan() {
  console.log('--- C. startup scan ---');
  const server = require(path.join(ROOT, 'tools', 'play', 'server.js'));
  const { repo, origin } = makeRepo('c');
  const rel = path.join('references', 'Deck', 'claude_s4_gi3.json');
  fs.writeFileSync(path.join(repo, rel), '{"untracked":1}\n');
  fs.writeFileSync(path.join(repo, 'references', 'Deck', 'claude_s1_gi0.json'), '{"tracked":"MODIFIED"}\n');
  const before = snapshot(repo);
  const st = await server.startupReferenceScan(repo);
  sameSnap(before, snapshot(repo), 'after startup scan');
  chk(st.files.length === 2, `scan found the untracked + the modified reference (${st.files.join(', ')})`);
  chk(st.backup && st.backup.ok && st.backup.pushed === true, 'startup scan protected + pushed them');
  chk(blobAt(origin, 'refs/heads/references-autosave', 'references/Deck/claude_s1_gi0.json') ===
      hashFile(repo, path.join('references', 'Deck', 'claude_s1_gi0.json')), 'the MODIFIED tracked reference is on the autosave branch');
  const st2 = await server.startupReferenceScan(repo);
  chk(st2.files.length === 0, 'second scan: nothing left unprotected');
  return server;
}

function post(port, route, body) {
  return new Promise((resolve, reject) => {
    const data = JSON.stringify(body);
    const req = http.request({ host: '127.0.0.1', port, path: route, method: 'POST',
                               headers: { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(data) } },
      (res) => { let s = ''; res.on('data', d => { s += d; }); res.on('end', () => { try { resolve(JSON.parse(s)); } catch (e) { reject(e); } }); });
    req.on('error', reject); req.end(data);
  });
}
function get(port, route) {
  return new Promise((resolve, reject) => {
    http.get({ host: '127.0.0.1', port, path: route }, (res) => {
      let s = ''; res.on('data', d => { s += d; }); res.on('end', () => resolve(JSON.parse(s)));
    }).on('error', reject);
  });
}
const SIDE_PROMPTS = { firebreathe: 'firebreathe', jitte: 'jitte', storage_hold: 'storageHold' };
function pick(d) {
  if (d.type === 'bottom') {
    const K = d.bottom_total || 1;
    const set = Array.isArray(d.ai_set) && d.ai_set.length === K ? d.ai_set.slice() : Array.from({ length: K }, (_, i) => i);
    return set.sort((a, b) => b - a);
  }
  const ac = d.ai_choice;
  if (typeof ac === 'number') return [ac];
  if (ac && typeof ac.index === 'number') return [ac.index];
  if (typeof d.heuristic_default === 'number') return [d.heuristic_default];
  return [d.type === 'main_phase' ? -1 : 0];
}

async function testRoute(server) {
  console.log('--- D. the real /api/save-reference route ---');
  if (!fs.existsSync(server.BIN)) { console.log(`  SKIP: no engine binary at ${server.BIN} (build Release, or set MTG_BIN)`); return; }
  const { repo, origin } = makeRepo('d');
  server.setReferenceRepoForTest(repo);
  const srv = server.httpServer;
  await new Promise(r => srv.listen(0, '127.0.0.1', r));
  const port = srv.address().port;
  try {
    const decks = await get(port, '/api/decks');
    chk(decks.referenceSafety && decks.referenceSafety.backupDir === BACKUPS, '/api/decks carries referenceSafety (backupDir)');
    const p = { deck: 'burn.txt', version: null, seed: 990002, gameIndex: 0, maxTurns: 8, depth: 0,
                choices: [], firebreathe: {}, jitte: {}, storageHold: {}, castOrder: {} };
    let result = null;
    for (let n = 0; n < 400 && !result; n++) {
      const out = await post(port, '/api/step', p);
      if (out.kind === 'result') { result = out.result; break; }
      if (out.kind !== 'decision') { fail('route walk: ' + (out.error || out.kind)); return; }
      const d = out.decision;
      if (d.type in SIDE_PROMPTS) {
        p[SIDE_PROMPTS[d.type]][d.type === 'storage_hold' ? `${d.turn}:${d.land_num != null ? d.land_num : 0}` : String(d.turn)] =
          typeof d.heuristic_default === 'number' ? d.heuristic_default : 0;
        continue;
      }
      p.choices = p.choices.concat(pick(d));
    }
    if (!result) { fail('route walk: the game did not finish'); return; }
    const before = snapshot(repo);
    const r = await post(port, '/api/save-reference', p);
    const rel = path.join('references', 'burn', 'claude_s990002_gi0.json');
    chk(r.savedAs && fs.existsSync(path.join(repo, rel)), `route saved the reference into the scratch repo (${rel})`);
    chk(r.backup && r.backup.ok === true, `response.backup ok (${JSON.stringify(r.backup && r.backup.errors)})`);
    chk(r.backup && fs.existsSync(path.join(BACKUPS, rel)), 'route: backup copy written');
    chk(r.backup && r.backup.autosave_commit && blobAt(repo, r.backup.autosave_commit, toPosix(rel)) === hashFile(repo, rel),
        'route: autosave commit contains the saved reference');
    chk(r.backup && r.backup.pushed === true && blobAt(origin, 'refs/heads/references-autosave', toPosix(rel)),
        'route: pushed to origin/references-autosave');
    const after = snapshot(repo);
    chk(after.head === before.head && after.branch === before.branch && after.index === before.index,
        'route: HEAD, branch and index untouched (only the new untracked file appears)');

    git(repo, 'remote', 'set-url', 'origin', path.join(SCRATCH, 'gone.git'));
    fs.appendFileSync(path.join(repo, rel), ' ');        // make the re-save a NEW version on disk first
    const r2 = await post(port, '/api/save-reference', p);
    chk(r2.savedAs && r2.backup && r2.backup.pushed === false && r2.backup.ok === false && r2.backup.errors.length > 0,
        `route: push failure surfaces in the save response (pushed=false: ${r2.backup && (r2.backup.errors[0] || '').slice(0, 70)})`);
    const sibs = fs.readdirSync(path.join(BACKUPS, 'references', 'burn'));
    chk(sibs.length >= 2, `route: the overwritten on-disk version was backed up before the re-save (${sibs.length} files)`);
    const d2 = await get(port, '/api/decks');
    chk(d2.referenceSafety.lastSave && d2.referenceSafety.lastSave.ok === false, '/api/decks reports the failed last save');
  } finally {
    await new Promise(r => srv.close(r));
    server.setReferenceRepoForTest(ROOT);
  }
}
const toPosix = (p) => p.split(path.sep).join('/');

function testLinkedWorktreeRefusal() {
  console.log('--- E. the server refuses to LISTEN from a linked worktree ---');
  fs.mkdirSync(path.join(ROOT, 'logs'), { recursive: true });
  const base = fs.mkdtempSync(path.join(ROOT, 'logs', 'refsafe-e-'));
  try {
    const prim = path.join(base, 'primary');
    git(base, 'init', '-q', '-b', 'main', prim);
    fs.mkdirSync(path.join(prim, 'tools', 'play'), { recursive: true });
    for (const f of ['server.js', 'refsafe.js']) fs.copyFileSync(path.join(ROOT, 'tools', 'play', f), path.join(prim, 'tools', 'play', f));
    git(prim, 'add', '-A'); git(prim, 'commit', '-q', '-m', 'x');
    const linked = path.join(base, 'linked');
    git(prim, 'worktree', 'add', '-q', '-b', 'lw', linked);
    const env = Object.assign({}, process.env, { PORT: '0' });
    const r = spawnSync(process.execPath, [path.join(linked, 'tools', 'play', 'server.js')], { env, encoding: 'utf8' });
    chk(r.status === 2, `linked worktree: exit ${r.status} (want 2)`);
    chk(/LINKED git worktree/.test(r.stderr), 'linked worktree: the refusal names the cause');
    // CONTROL: the same files from the PRIMARY checkout must start (else "exit 2" proves nothing).
    return new Promise((resolve) => {
      const c = spawn(process.execPath, [path.join(prim, 'tools', 'play', 'server.js')], { env });
      let out = '', done = false;
      const finish = (started) => {
        if (done) return; done = true;
        chk(started, 'control: the same server STARTS from the primary checkout');
        try { c.kill(); } catch (e) {}
        resolve();
      };
      c.stdout.on('data', d => { out += d; if (/play GUI: http/.test(out)) finish(true); });
      c.on('exit', () => finish(false));
    });
  } finally {
    setTimeout(() => fs.rmSync(base, { recursive: true, force: true }), 500);
  }
}

function testScanner() {
  console.log('--- F. scripts/check_references_safe.py + scripts/safe_worktree_remove.sh ---');
  if (spawnSync('python3', ['--version']).status !== 0 || process.platform === 'win32') {
    console.log('  SKIP: needs python3 + bash'); return Promise.resolve();
  }
  const { repo } = makeRepo('f');
  const wt = path.join(SCRATCH, 'f-wt');
  git(repo, 'worktree', 'add', '-q', '-b', 'fw', wt);
  const rel = path.join('references', 'Deck', 'claude_s7_gi6.json');
  fs.writeFileSync(path.join(wt, rel), '{"only":"here"}\n');
  const scan = () => spawnSync('python3', [path.join(ROOT, 'scripts', 'check_references_safe.py'), wt], { encoding: 'utf8' });
  let r = scan();
  chk(r.status === 1 && r.stdout.includes('claude_s7_gi6.json'), `scanner flags an untracked reference (exit ${r.status})`);
  r = spawnSync('bash', [path.join(ROOT, 'scripts', 'safe_worktree_remove.sh'), wt, '--force'], { encoding: 'utf8' });
  chk(r.status === 1 && fs.existsSync(path.join(wt, rel)), 'safe_worktree_remove.sh REFUSES and leaves the worktree intact');
  return refsafe.protect(wt, [rel]).then(() => {
    const r2 = scan();
    chk(r2.status === 0, `after the autosave push the scanner passes (exit ${r2.status}) ${r2.stdout.split('\n').slice(-2).join(' ')}`);
    const r3 = spawnSync('bash', [path.join(ROOT, 'scripts', 'safe_worktree_remove.sh'), wt, '--force'], { encoding: 'utf8' });
    chk(r3.status === 0 && !fs.existsSync(wt), 'safe_worktree_remove.sh removes a SAFE worktree');
  });
}

(async () => {
  try {
    await testScanner();
    await testProtect();
    await testPushFailureAndDivergence();
    const server = await testStartupScan();
    await testRoute(server);
    await testLinkedWorktreeRefusal();
  } catch (e) {
    fail('exception: ' + (e.stack || e));
  }
  fs.rmSync(SCRATCH, { recursive: true, force: true });
  console.log(fails ? `reference backup check: FAIL (${fails})` : 'reference backup check: PASS');
  setTimeout(() => process.exit(fails ? 1 : 0), 600);
})();
