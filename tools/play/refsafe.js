// Reference-game SAFETY layer for the play viewer (tools/play/server.js).
// =================================================================================
// WHY THIS EXISTS. The viewer writes the user's hand-played reference games into
// <root>/references/. On 2026-09-28 a viewer was started from a /tmp scratch worktree; on 2026-10-05
// the container was recreated and a WEEK of references -- user work that cannot be regenerated --
// was gone. The user's directive: hard rules so this cannot happen "under any situation".
//
// Defence in depth, every layer independent of the others:
//   1. refuseUnsafeRoot()   -- the server will not LISTEN from a temp dir or a LINKED git worktree.
//   2. backupFile()         -- every saved reference is copied OUTSIDE the repo, never overwritten.
//   3. autosaveCommit()     -- every saved reference is committed to refs/heads/references-autosave
//                              with git PLUMBING (a private index file): the working tree, the real
//                              index, HEAD and the current branch are never touched.
//   4. pushAutosave()       -- that branch is pushed to origin asynchronously.
//   5. scanUnprotected()    -- at startup, any untracked/modified reference not yet on the autosave
//                              branch is backed up + committed before anything else happens.
// A failure in any step NEVER fails the save (the file is already written); it is reported in the
// response so the GUI can show a red banner.
//
// Windows parity: node built-ins + the git CLI only; paths via path.join; home via os.homedir().

const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawnSync, spawn } = require('child_process');

const AUTOSAVE_BRANCH = 'references-autosave';
const AUTOSAVE_REF = 'refs/heads/' + AUTOSAVE_BRANCH;

function git(root, args, opts) {
  opts = opts || {};
  const r = spawnSync('git', args, {
    cwd: root, encoding: 'utf8', input: opts.input,
    env: Object.assign({}, process.env, { GIT_TERMINAL_PROMPT: '0' }, opts.env || {}),
    maxBuffer: 64 * 1024 * 1024, windowsHide: true,
  });
  if (r.error) return { ok: false, out: '', err: String(r.error.message || r.error), code: -1 };
  // `raw` keeps leading whitespace: porcelain status lines START with a space (" M path"), and
  // trimming it shifted every modified entry's path by one character.
  const out = opts.raw ? (r.stdout || '') : (r.stdout || '').trim();
  return { ok: r.status === 0, out, err: (r.stderr || '').trim(), code: r.status };
}

function realpath(p) { try { return fs.realpathSync(p); } catch (e) { return path.resolve(p); } }
const norm = (p) => (process.platform === 'win32' ? p.toLowerCase() : p);
function isInside(parent, child) {
  const rel = path.relative(norm(parent), norm(child));
  return rel === '' || (!rel.startsWith('..') && !path.isAbsolute(rel));
}

// ---------------------------------------------------------------------------------
// 1. Startup guard
// ---------------------------------------------------------------------------------
// Returns null when `root` is a safe place to serve from, else a list of reasons. Two refusals:
//   * a TEMPORARY directory (the 2026-09-28 incident: /tmp is wiped with the container);
//   * a LINKED git worktree. Scratch worktrees are created and deleted freely by agents (and on this
//     devcontainer they live on the ephemeral overlay root), so references saved into one are one
//     `git worktree remove` / one container rebuild away from being lost. The viewer must run from
//     the PRIMARY checkout, where references are seen by every agent's `git status`.
function unsafeRootReasons(root) {
  const reasons = [];
  const real = realpath(root);
  const temps = new Set(['/tmp', '/var/tmp', '/dev/shm']);
  for (const t of [os.tmpdir(), process.env.TMPDIR, process.env.TEMP, process.env.TMP]) {
    if (!t) continue;
    temps.add(path.resolve(t));
    temps.add(realpath(t));
  }
  for (const t of temps) {
    if (isInside(t, real)) {
      reasons.push(`the play server root ${real} is inside the temporary directory ${t}`);
      break;
    }
  }
  const lw = linkedWorktreeOf(root);
  if (lw) reasons.push(`the play server root ${real} is a LINKED git worktree (${lw}); `
                     + 'the viewer must run from the PRIMARY checkout');
  return reasons.length ? reasons : null;
}

// Non-null (a description) when `root` is a linked worktree. Two independent signals so a missing
// git binary cannot open the hole: `.git` being a FILE (gitdir: pointer) is how every linked
// worktree is laid out, and git-dir != git-common-dir is git's own definition.
function linkedWorktreeOf(root) {
  const dotgit = path.join(root, '.git');
  try {
    if (fs.statSync(dotgit).isFile()) {
      const txt = fs.readFileSync(dotgit, 'utf8').trim();
      return `.git is a file: ${txt}`;
    }
  } catch (e) { /* no .git at all: not a git checkout (e.g. a zip download) -> not a worktree */ }
  const g = git(root, ['rev-parse', '--path-format=absolute', '--git-dir', '--git-common-dir']);
  if (g.ok) {
    const [gd, cd] = g.out.split(/\r?\n/);
    if (gd && cd && norm(realpath(gd)) !== norm(realpath(cd))) return `git-dir ${gd} != git-common-dir ${cd}`;
  }
  return null;
}

// Hard exit (code 2), no bypass. Called by server.js ONLY when it is about to LISTEN, so the test
// harnesses that require() it from an agent's worktree keep working.
function refuseUnsafeRoot(root) {
  const reasons = unsafeRootReasons(root);
  if (!reasons) return;
  for (const r of reasons) console.error('REFUSING TO START: ' + r + '.');
  console.error('Reference games are saved under <root>/references and would be LOST when it is wiped.');
  console.error('Start the viewer ONLY from the primary checkout (e.g. /workspaces/MagicDeckTester after');
  console.error('`git -C <primary> pull --rebase`). See CLAUDE.md "REFERENCE GAMES" -- there is no bypass.');
  process.exit(2);
}

// ---------------------------------------------------------------------------------
// 2. Out-of-repo backup
// ---------------------------------------------------------------------------------
// The repo NAME is the primary checkout's folder (git-common-dir's parent), so every worktree of
// one repo shares one backup tree.
function repoName(root) {
  const g = git(root, ['rev-parse', '--path-format=absolute', '--git-common-dir']);
  if (g.ok && g.out) {
    const cd = g.out.split(/\r?\n/)[0];
    const base = path.basename(cd) === '.git' ? path.basename(path.dirname(cd)) : path.basename(cd).replace(/\.git$/, '');
    if (base) return base;
  }
  return path.basename(realpath(root));
}

function backupRoot(root) {
  if (process.env.MDT_REFERENCE_BACKUP_DIR) return path.resolve(process.env.MDT_REFERENCE_BACKUP_DIR);
  // DEVCONTAINER DEFAULT: ~/.claude is the one persistent volume under $HOME (the rest of $HOME is
  // the overlay root a rebuild wipes). When it is a separate mount, back up there by default so a
  // plain `node tools/play/server.js` restart is safe without remembering the env var (USER
  // 2026-10-06 hit the "will NOT survive a container rebuild" warning after a restart that omitted
  // it). Same folder the devcontainer guidance names, so existing backups stay in one tree.
  const claudeDir = path.join(os.homedir(), '.claude');
  try {
    if (process.platform === 'linux' && fs.existsSync(claudeDir)
        && fs.statSync(claudeDir).dev !== fs.statSync('/').dev) {
      return path.join(claudeDir, 'mdt-reference-backups');
    }
  } catch (e) {}
  return path.join(os.homedir(), '.mdt-reference-backups', repoName(root));
}

// A warning (string) when the backup dir is likely to die with the container, else null. In a
// devcontainer $HOME is usually on the overlay root, which a rebuild wipes -- exactly the failure
// the backup exists to survive. Heuristic, Linux only, never fatal.
function backupDirWarning(dir) {
  if (process.platform !== 'linux') return null;
  const inContainer = fs.existsSync('/.dockerenv') || fs.existsSync('/run/.containerenv')
    || fs.existsSync('/workspaces');
  if (!inContainer) return null;
  try {
    let p = dir;
    while (!fs.existsSync(p)) p = path.dirname(p);
    if (fs.statSync(p).dev === fs.statSync('/').dev) {
      return `the backup directory ${dir} is on the container's root filesystem and will NOT survive `
           + 'a container rebuild; set MDT_REFERENCE_BACKUP_DIR to a persistent mount';
    }
  } catch (e) {}
  return null;
}

function sameBytes(a, b) {
  try { return fs.readFileSync(a).equals(fs.readFileSync(b)); } catch (e) { return false; }
}

// Copy <root>/<rel> to <backupRoot>/<rel>. Never overwrites: an identical copy already present is a
// no-op, a DIFFERENT one gets a timestamped sibling (<name>.<UTC stamp>[-n].json).
function backupFile(root, rel) {
  const src = path.join(root, rel);
  const broot = backupRoot(root);
  let dst = path.join(broot, rel);
  try {
    fs.mkdirSync(path.dirname(dst), { recursive: true });
    if (fs.existsSync(dst)) {
      if (sameBytes(src, dst)) return { ok: true, path: dst, already: true };
      const dir = path.dirname(dst), ext = path.extname(dst), stem = path.basename(dst, ext);
      // Any earlier timestamped copy with these exact bytes also counts as "already backed up".
      for (const f of fs.readdirSync(dir)) {
        if (f.startsWith(stem + '.') && f.endsWith(ext) && sameBytes(src, path.join(dir, f))) {
          return { ok: true, path: path.join(dir, f), already: true };
        }
      }
      const stamp = new Date().toISOString().replace(/[:.]/g, '-');
      let n = 0;
      do { dst = path.join(dir, `${stem}.${stamp}${n ? '-' + n : ''}${ext}`); n++; } while (fs.existsSync(dst));
    }
    fs.copyFileSync(src, dst, fs.constants.COPYFILE_EXCL);
    return { ok: true, path: dst, already: false };
  } catch (e) {
    return { ok: false, path: dst, error: 'backup copy failed: ' + (e.message || e) };
  }
}

// ---------------------------------------------------------------------------------
// 3. Autosave commit (plumbing only)
// ---------------------------------------------------------------------------------
const toGitPath = (rel) => rel.split(path.sep).join('/');

function identityEnv(root) {
  const env = {};
  const name = git(root, ['config', 'user.name']), email = git(root, ['config', 'user.email']);
  if (!process.env.GIT_AUTHOR_NAME && !(name.ok && name.out)) env.GIT_AUTHOR_NAME = 'mdt-play-viewer';
  if (!process.env.GIT_AUTHOR_EMAIL && !(email.ok && email.out)) env.GIT_AUTHOR_EMAIL = 'mdt-play-viewer@localhost';
  if (!process.env.GIT_COMMITTER_NAME && !(name.ok && name.out)) env.GIT_COMMITTER_NAME = 'mdt-play-viewer';
  if (!process.env.GIT_COMMITTER_EMAIL && !(email.ok && email.out)) env.GIT_COMMITTER_EMAIL = 'mdt-play-viewer@localhost';
  return env;
}

function revParse(root, spec) {
  const g = git(root, ['rev-parse', '--verify', '-q', spec]);
  return g.ok && g.out ? g.out : null;
}

// Build a commit on top of `parent` (or a root commit) whose tree is `baseTree` (a tree-ish, may be
// null) with each of `files` ({gitPath, blob}) laid over it. Uses a PRIVATE index file, so the
// user's index is never read or written. Returns {sha, tree, changed}.
function commitOver(root, parent, baseTree, files, message) {
  const idxPath = git(root, ['rev-parse', '--path-format=absolute', '--git-path',
                             `mdt-refsafe-index-${process.pid}-${Date.now()}`]);
  if (!idxPath.ok) throw new Error('git rev-parse --git-path failed: ' + idxPath.err);
  const env = { GIT_INDEX_FILE: idxPath.out };
  try {
    let g = baseTree ? git(root, ['read-tree', baseTree], { env }) : git(root, ['read-tree', '--empty'], { env });
    if (!g.ok) throw new Error('read-tree failed: ' + g.err);
    for (const f of files) {
      g = git(root, ['update-index', '--add', '--cacheinfo', `100644,${f.blob},${f.gitPath}`], { env });
      if (!g.ok) throw new Error('update-index failed: ' + g.err);
    }
    g = git(root, ['write-tree'], { env });
    if (!g.ok) throw new Error('write-tree failed: ' + g.err);
    const tree = g.out;
    const parentTree = parent ? revParse(root, parent + '^{tree}') : null;
    if (parent && tree === parentTree) return { sha: parent, tree, changed: false };
    const args = ['commit-tree', tree];
    if (parent) args.push('-p', parent);
    g = git(root, args, { env: identityEnv(root), input: message + '\n' });
    if (!g.ok) throw new Error('commit-tree failed: ' + g.err);
    return { sha: g.out, tree, changed: true };
  } finally {
    try { fs.unlinkSync(idxPath.out); } catch (e) {}
  }
}

// Commit every rel path (relative to root, must exist) to refs/heads/references-autosave. The
// working tree, the index, HEAD and the current branch are untouched. Returns
// {ok, sha, changed, error}.
function autosaveCommit(root, rels, message) {
  try {
    const files = [];
    for (const rel of rels) {
      const g = git(root, ['hash-object', '-w', '--', path.join(root, rel)]);
      if (!g.ok) throw new Error('hash-object failed for ' + rel + ': ' + g.err);
      files.push({ gitPath: toGitPath(rel), blob: g.out });
    }
    const parent = revParse(root, AUTOSAVE_REF);
    const msg = message || `autosave: ${files.length} reference game(s) saved by the play viewer\n\n`
      + files.map(f => '  ' + f.gitPath).join('\n')
      + `\n\nhost=${os.hostname()} root=${realpath(root)} at=${new Date().toISOString()}`;
    const c = commitOver(root, parent, parent, files, msg);
    if (c.changed) {
      const args = ['update-ref', '-m', 'play viewer reference autosave', AUTOSAVE_REF, c.sha];
      if (parent) args.push(parent); else args.push('0'.repeat(40));   // compare-and-swap
      const u = git(root, args);
      if (!u.ok) throw new Error('update-ref failed: ' + u.err);
    }
    return { ok: true, sha: c.sha, changed: c.changed };
  } catch (e) {
    return { ok: false, sha: null, changed: false, error: String(e.message || e) };
  }
}

// ---------------------------------------------------------------------------------
// 4. Push (async; never blocks the event loop)
// ---------------------------------------------------------------------------------
function gitAsync(root, args) {
  return new Promise((resolve) => {
    let out = '', err = '';
    let p;
    try {
      p = spawn('git', args, { cwd: root, windowsHide: true,
                               env: Object.assign({}, process.env, { GIT_TERMINAL_PROMPT: '0' }) });
    } catch (e) { return resolve({ ok: false, out: '', err: String(e.message || e), code: -1 }); }
    p.stdout.on('data', d => { out += d; });
    p.stderr.on('data', d => { err += d; });
    p.on('error', e => resolve({ ok: false, out, err: String(e.message || e), code: -1 }));
    p.on('close', code => resolve({ ok: code === 0, out: out.trim(), err: err.trim(), code }));
  });
}

// Push the autosave branch. If origin's copy has commits we lack (another machine's viewer also
// autosaves), rebuild ours LINEARLY on top of theirs -- their tree with our files laid over it --
// and push again. Never a merge commit, never a force push.
async function pushAutosave(root, remote) {
  remote = remote || 'origin';
  const has = git(root, ['remote', 'get-url', remote]);
  if (!has.ok) return { pushed: false, error: `no git remote '${remote}' to push the autosave branch to` };
  let p = await gitAsync(root, ['push', '--quiet', remote, `${AUTOSAVE_REF}:${AUTOSAVE_REF}`]);
  if (p.ok) return { pushed: true };
  const firstErr = p.err;
  if (!/rejected|non-fast-forward|fetch first/i.test(p.err)) return { pushed: false, error: 'git push failed: ' + firstErr };
  const f = await gitAsync(root, ['fetch', '--quiet', remote,
                                  `+${AUTOSAVE_REF}:refs/remotes/${remote}/${AUTOSAVE_BRANCH}`]);
  if (!f.ok) return { pushed: false, error: 'git push rejected and fetch failed: ' + f.err };
  try {
    const theirs = revParse(root, `refs/remotes/${remote}/${AUTOSAVE_BRANCH}`);
    const ours = revParse(root, AUTOSAVE_REF);
    const ls = git(root, ['ls-tree', '-r', '-z', ours]);
    if (!ls.ok) throw new Error('ls-tree failed: ' + ls.err);
    const files = ls.out.split('\0').filter(Boolean).map(line => {
      const [meta, gitPath] = line.split('\t');
      return { gitPath, blob: meta.split(' ')[2] };
    });
    const c = commitOver(root, theirs, theirs, files,
      `autosave: rebuild local autosave on top of ${remote}/${AUTOSAVE_BRANCH}\n\n(previous local tip ${ours})`);
    const u = git(root, ['update-ref', AUTOSAVE_REF, c.sha, ours]);
    if (!u.ok) throw new Error('update-ref failed: ' + u.err);
  } catch (e) {
    return { pushed: false, error: 'git push rejected; rebuilding on the remote tip failed: ' + (e.message || e) };
  }
  p = await gitAsync(root, ['push', '--quiet', remote, `${AUTOSAVE_REF}:${AUTOSAVE_REF}`]);
  return p.ok ? { pushed: true, rebuiltOnRemote: true }
              : { pushed: false, error: 'git push failed after rebuilding on the remote tip: ' + p.err };
}

// ---------------------------------------------------------------------------------
// The one entry point a save route calls: backup + autosave + push for `rels`.
// Returns the `backup` status object that goes into the save response. Never throws.
// ---------------------------------------------------------------------------------
async function protect(root, rels, opts) {
  opts = opts || {};
  const status = { files: rels.map(toGitPath), local: 'ok', localPaths: [], autosave_commit: null,
                   pushed: false, errors: [] };
  for (const rel of rels) {
    const b = backupFile(root, rel);
    if (b.ok) status.localPaths.push(b.path);
    else { status.local = 'failed'; status.errors.push(b.error); }
  }
  const w = backupDirWarning(backupRoot(root));
  if (w) status.warnings = [w];
  const c = autosaveCommit(root, rels, opts.message);
  if (c.ok) status.autosave_commit = c.sha;
  else status.errors.push('autosave commit failed: ' + c.error);
  if (c.ok && opts.push !== false) {
    const p = await pushAutosave(root, opts.remote);
    status.pushed = !!p.pushed;
    if (!p.pushed) status.errors.push(p.error);
  } else if (opts.push === false) {
    status.pushed = null;          // not attempted (caller opted out)
  }
  status.ok = status.errors.length === 0;
  if (!status.ok) {
    console.error('\n!!! REFERENCE BACKUP PROBLEM for ' + status.files.join(', '));
    for (const e of status.errors) console.error('!!!   ' + e);
    console.error('!!! The file IS saved in the repo, but commit + push it by hand NOW.\n');
  }
  return status;
}

// ---------------------------------------------------------------------------------
// 5. Startup scan
// ---------------------------------------------------------------------------------
// Reference files under <root>/references that are untracked or modified vs HEAD (deleted ones are
// not "at risk") and whose current bytes are NOT already on the local autosave branch.
function scanUnprotected(root) {
  const st = git(root, ['status', '--porcelain=v1', '-z', '--untracked-files=all', '--', 'references'], { raw: true });
  if (!st.ok) return { ok: false, files: [], error: 'git status failed: ' + st.err };
  const entries = st.out.split('\0').filter(Boolean);
  const changed = [];
  for (let i = 0; i < entries.length; i++) {
    const e = entries[i];
    const xy = e.slice(0, 2), p = e.slice(3);
    if (xy[0] === 'R' || xy[0] === 'C') i++;             // rename/copy: next entry is the source
    if (xy.includes('D')) continue;
    const abs = path.join(root, p);
    try { if (!fs.statSync(abs).isFile()) continue; } catch (err) { continue; }
    changed.push(p);
  }
  const onAutosave = new Set();
  if (revParse(root, AUTOSAVE_REF)) {
    const ls = git(root, ['ls-tree', '-r', '-z', AUTOSAVE_REF]);
    if (ls.ok) for (const line of ls.out.split('\0').filter(Boolean)) onAutosave.add(line.split('\t')[0].split(' ')[2]);
  }
  const files = [];
  for (const p of changed) {
    const h = git(root, ['hash-object', '--', path.join(root, p)]);
    if (!(h.ok && onAutosave.has(h.out))) files.push(p.split('/').join(path.sep));
  }
  return { ok: true, files };
}

module.exports = { AUTOSAVE_BRANCH, AUTOSAVE_REF, unsafeRootReasons, linkedWorktreeOf, refuseUnsafeRoot,
                   backupRoot, backupFile, backupDirWarning, autosaveCommit, pushAutosave, protect,
                   scanUnprotected, repoName };
