#!/usr/bin/env node
// Deck-maturity ("(beta)") check for the play viewer.
// =====================================================================================
// The viewer marks a deck "(beta)" when its APPARATUS is incomplete -- fewer than 10 optimal
// reference games, no value leaf, or no completed mulligan profile -- so a user picking a deck can
// see that its numbers are not yet fully reliable. See deckMaturity/betaFrom in tools/play/server.js.
//
// WHY THIS NEEDS A TEST AT ALL. Every failure mode here is silent and reads as good news: a rule
// that stops firing does not error, it just quietly promotes an unfinished deck to "ready". The two
// specific ways it can rot:
//
//   * the artifact NAME drifts. The mulligan check probes the same two extensions
//     MulliganProfileIO.h:967 loads, in the same order, and the value leaf is the same
//     `<stem>.value.json` AttachValueSidecar resolves. If the engine's names move and these do not,
//     every deck silently reads "complete".
//   * SUBOPTIMAL references start counting. They live one level deeper (references/suboptimal/<Deck>/)
//     and are excluded structurally, not by a filter -- but "count the .json files under the deck"
//     is the obvious refactor, and it would inflate three decks toward the threshold with games the
//     user explicitly flagged as NOT the standard.
//
// Fast, static, no binary and no jsdom: the policy is a pure function and the rest is directory
// reads. Run:  node test/viewer_deck_beta_check.js
'use strict';
const fs = require('fs');
const path = require('path');
const ROOT = path.resolve(__dirname, '..');
const srv = require(path.join(ROOT, 'tools', 'play', 'server.js'));

let fails = 0;
function ok(cond, what, detail) {
  if (cond) return;
  fails++;
  console.log('  FAIL: ' + what + (detail ? '  [' + detail + ']' : ''));
}

// ---- 1) the policy, exhaustively ----------------------------------------------------
// Every combination of the three criteria, at and around the threshold. Written out rather than
// generated so the EXPECTED column is a statement of policy someone can read and disagree with.
console.log('--- policy (betaFrom) ---');
const T = srv.MIN_OPTIMAL_REFS;
ok(T === 10, 'threshold is 10 optimal references', 'MIN_OPTIMAL_REFS=' + T);

for (const refs of [0, T - 1, T, T + 1]) {
  for (const hasValueLeaf of [false, true]) {
    for (const hasKeepModel of [false, true]) {
      const r = srv.betaFrom({ hasProfile: true, refs, hasValueLeaf, hasKeepModel });
      const want = refs < T || !hasValueLeaf || !hasKeepModel;
      ok(r.beta === want,
         `beta(refs=${refs}, value=${hasValueLeaf}, keep=${hasKeepModel}) === ${want}`,
         'got ' + r.beta);
      // A reason per failing criterion -- the badge shows them, so an empty list on a beta deck
      // would render "beta — " with nothing after it.
      const wantN = (refs < T ? 1 : 0) + (hasValueLeaf ? 0 : 1) + (hasKeepModel ? 0 : 1);
      ok(r.betaReasons.length === wantN, `reason count for (${refs},${hasValueLeaf},${hasKeepModel})`,
         JSON.stringify(r.betaReasons));
      ok(r.beta === (r.betaReasons.length > 0), 'beta iff there is at least one reason');
    }
  }
}

// A deck with NO profile is unplayable, not beta: the picker already renders "(no profile)" and
// disables it, which is the strictly stronger statement. Stacking "(beta)" would say less.
for (const refs of [0, T + 1]) {
  const r = srv.betaFrom({ hasProfile: false, refs, hasValueLeaf: false, hasKeepModel: false });
  ok(r.beta === false, 'a profile-less deck is NOT beta (it is already "(no profile)")');
  ok(r.betaReasons.length === 0, 'a profile-less deck carries no beta reasons');
}

// ---- 2) the artifact names still match the engine's ---------------------------------
// Pinned against the engine source rather than restated, so a rename in one has to be a rename in
// both. MulliganProfileIO.h probes .gz FIRST; order is part of the contract being mirrored.
console.log('--- artifact names vs the engine ---');
const mio = fs.readFileSync(path.join(ROOT, 'src', 'ai', 'MulliganProfileIO.h'), 'utf8');
for (const ext of srv.KEEPMODEL_EXTS) {
  ok(mio.includes('"' + ext + '"'), 'MulliganProfileIO.h still loads ' + ext);
}
ok(srv.KEEPMODEL_EXTS[0].endsWith('.gz'), 'the .gz form is probed first, as the engine does');
ok(mio.includes('".value.json"'), 'MulliganProfileIO.h still resolves <stem>.value.json');

// ---- 3) suboptimal references are excluded ------------------------------------------
// references/suboptimal/<Deck>/ sits one level deeper than references/<Deck>/, so reading the deck's
// own folder never sees them. Asserted against whichever decks actually have one, so this keeps
// working as the suboptimal set changes (references/suboptimal/README.md).
console.log('--- suboptimal references do not count ---');
const SUB = path.join(ROOT, 'references', 'suboptimal');
let checkedSub = 0;
for (const deck of (fs.existsSync(SUB) ? fs.readdirSync(SUB) : [])) {
  const subdir = path.join(SUB, deck);
  if (!fs.statSync(subdir).isDirectory()) continue;
  const nSub = fs.readdirSync(subdir).filter(f => /^claude_s\d+_gi\d+\.json$/.test(f)).length;
  if (!nSub) continue;
  const own = path.join(ROOT, 'references', deck);
  const nOwn = fs.existsSync(own)
    ? fs.readdirSync(own).filter(f => /^claude_s\d+_gi\d+\.json$/.test(f)).length : 0;
  ok(srv.countOptimalRefs(deck) === nOwn,
     `${deck}: counts only its own ${nOwn} reference(s), not the ${nSub} suboptimal one(s)`,
     'got ' + srv.countOptimalRefs(deck));
  checkedSub++;
}
ok(checkedSub > 0, 'at least one deck has a suboptimal reference to check exclusion against');

// A folder that does not exist reads as zero, not as a throw -- a brand-new deck has no
// references/<Deck>/ at all, and that is the single most common beta case.
ok(srv.countOptimalRefs('NoSuchDeckAnywhere') === 0, 'a missing references folder counts 0');

// ---- 3b) references played on an ARCHIVED list do not count -------------------------
// scripts/deck_registry.py's REFERENCE_DECK maps a reference folder to the deck key its games were
// actually played on. server.js PARSES that dict (it is Python, the viewer is dependency-free Node),
// so the parse is cross-checked against Python's own value here -- a silently-empty parse would
// restore the bug it exists to fix, and would do it in the direction that reads as "ready".
console.log('--- references vs the list they were played on ---');
const owners = srv.referenceOwners();
const py = require('child_process').spawnSync('python3',
  ['-c', 'import sys; sys.path.insert(0,"scripts"); import deck_registry as r; import json;'
       + ' print(json.dumps(r.REFERENCE_DECK))'], { cwd: ROOT, encoding: 'utf8' });
if (py.status === 0) {
  const want = JSON.parse(py.stdout.trim());
  ok(JSON.stringify(owners) === JSON.stringify(want),
     'the JS parse of REFERENCE_DECK matches Python exactly',
     JSON.stringify(owners) + ' vs ' + JSON.stringify(want));
  // Cheap but load-bearing: an empty parse silently disables the whole rule.
  ok(Object.keys(owners).length === Object.keys(want).length, 'the parse is not silently empty');
  // pySlug must agree with deck_registry.slug for the keys to line up at all.
  const py2 = require('child_process').spawnSync('python3',
    ['-c', 'import sys; sys.path.insert(0,"scripts"); import deck_registry as r; import json;'
         + ' print(json.dumps([r.slug(s) for s in ["Mirrorwing Dragon","Anti-Lifegain","Creature Giving","burn","slivers_vial"]]))'],
    { cwd: ROOT, encoding: 'utf8' });
  if (py2.status === 0) {
    const wantSlugs = JSON.parse(py2.stdout.trim());
    const gotSlugs = ['Mirrorwing Dragon', 'Anti-Lifegain', 'Creature Giving', 'burn', 'slivers_vial'].map(srv.pySlug);
    ok(JSON.stringify(gotSlugs) === JSON.stringify(wantSlugs),
       'pySlug agrees with deck_registry.slug', JSON.stringify(gotSlugs) + ' vs ' + JSON.stringify(wantSlugs));
  }
} else {
  console.log('  (python3 unavailable -- REFERENCE_DECK cross-check skipped)');
}

// Every deck the map re-points must report 0 of its own, and say why. Driven off the map rather
// than naming Mirrorwing, so this keeps working as decklists are archived and replaced.
for (const [refKey, ownerKey] of Object.entries(owners)) {
  if (refKey === ownerKey) continue;
  const d = srv.listDecks().find(x => srv.pySlug(x.name) === refKey);
  if (!d) continue;                                          // the folder was renamed away
  ok(d.refs === 0, `${d.name}: counts 0 -- its references belong to ${ownerKey}`, 'refs=' + d.refs);
  ok(d.refsOnArchivedList > 0,
     `${d.name}: reports how many saved games are on the archived list`, String(d.refsOnArchivedList));
  ok(d.beta, `${d.name}: is beta, not ready, on someone else's games`);
  ok(d.betaReasons.some(r => r.includes('archived list')),
     `${d.name}: the badge explains WHY the count is 0`, JSON.stringify(d.betaReasons));
}

// ---- 4) the live tree agrees with the policy ----------------------------------------
// listDecks() is what /api/decks serves. Recompute beta from the fields it reports and require the
// two to agree, so a wiring mistake in listDecks (stale field, wrong argument order) is caught even
// though the policy itself is right.
console.log('--- listDecks() wiring ---');
const decks = srv.listDecks();
ok(decks.length > 0, 'listDecks() found decks');
for (const d of decks) {
  for (const f of ['refs', 'hasValueLeaf', 'hasKeepModel', 'beta', 'betaReasons']) {
    ok(f in d, `${d.name}: /api/decks reports "${f}"`);
  }
  const want = srv.betaFrom(d);
  ok(d.beta === want.beta, `${d.name}: reported beta matches the policy applied to its own fields`,
     `beta=${d.beta} refs=${d.refs} value=${d.hasValueLeaf} keep=${d.hasKeepModel}`);
  // Every saved game in the folder is accounted for as EITHER counted or archived-list -- stronger
  // than "refs == folder size", which stopped being true once ownership mattered, and it catches a
  // game silently vanishing from both columns.
  ok(d.refs + d.refsOnArchivedList === srv.countOptimalRefs(d.name),
     `${d.name}: every saved game is either counted or attributed to an archived list`,
     `refs=${d.refs} archived=${d.refsOnArchivedList} folder=${srv.countOptimalRefs(d.name)}`);
  ok(!(d.refs > 0 && d.refsOnArchivedList > 0), `${d.name}: a folder belongs to ONE list, not both`);
}

// ---- 5) the UI actually SHOWS it ----------------------------------------------------
// The server can be perfectly right and the picker still say nothing -- which is the whole feature
// failing, silently and in the direction that looks fine. So drive index.html's REAL loadDecks()
// against synthetic decks and read the rendered option text and badge back out. jsdom-optional: the
// policy checks above are the gate, this is the extra mile when the dep is available.
let JSDOM = null;
try { ({ JSDOM } = require('jsdom')); } catch (e) { /* optional */ }
if (!JSDOM) {
  console.log('--- UI rendering: SKIPPED (jsdom not installed) ---');
  finish();
} else {
  console.log('--- UI rendering (index.html, jsdom) ---');
  const PLAY = path.join(ROOT, 'tools', 'play');
  const FIXTURE = [
    { deck: 'Ready.cod',  name: 'Ready',  hasProfile: true,  refs: 12, hasValueLeaf: true,  hasKeepModel: true,  beta: false, betaReasons: [] },
    { deck: 'Few.cod',    name: 'Few',    hasProfile: true,  refs: 3,  hasValueLeaf: true,  hasKeepModel: true,  beta: true,  betaReasons: ['3/10 optimal reference games'] },
    { deck: 'Three.cod',  name: 'Three',  hasProfile: true,  refs: 0,  hasValueLeaf: false, hasKeepModel: false, beta: true,  betaReasons: ['0/10 optimal reference games', 'no value-leaf model', 'no completed mulligan profile'] },
    { deck: 'NoProf.cod', name: 'NoProf', hasProfile: false, refs: 0,  hasValueLeaf: false, hasKeepModel: false, beta: false, betaReasons: [] },
  ];
  let html = fs.readFileSync(path.join(PLAY, 'index.html'), 'utf8');
  const lb = fs.readFileSync(path.join(PLAY, 'linebuild.js'), 'utf8');
  html = html.replace('<script src="/linebuild.js"></script>', '<script>\n' + lb + '\n</script>');
  const dom = new JSDOM(html, {
    runScripts: 'dangerously', url: 'http://localhost/',
    beforeParse(window) {
      window.fetch = (u) => Promise.resolve({ ok: true, json: () => Promise.resolve(
        String(u).startsWith('/api/decks') ? { decks: FIXTURE, binExists: true } : {}) });
    },
  });
  const win = dom.window;
  const tick = () => new Promise(r => win.setTimeout(r, 0));
  (async () => {
    await win.loadDecks();
    for (let i = 0; i < 50; i++) await tick();
    const sel = win.document.getElementById('deck');
    const label = n => { const o = [...sel.options].find(o => o.value === n + '.cod'); return o ? o.textContent : '(missing)'; };
    ok(label('Ready')  === 'Ready',              'a ready deck gets no suffix', label('Ready'));
    ok(label('Few')    === 'Few (beta)',         'a deck short on references is "(beta)"', label('Few'));
    ok(label('Three')  === 'Three (beta)',       'a deck failing all three is "(beta)" once', label('Three'));
    ok(label('NoProf') === 'NoProf (no profile)', 'a profile-less deck stays "(no profile)"', label('NoProf'));
    const noProfOpt = [...sel.options].find(o => o.value === 'NoProf.cod');
    ok(noProfOpt && noProfOpt.disabled, 'a profile-less deck is still disabled');

    // The badge follows the SELECTED deck and names every reason -- the option suffix says "beta",
    // the badge says why.
    const badge = win.document.getElementById('betanote');
    ok(!!badge, 'the top bar has a #betanote badge');
    sel.value = 'Three.cod'; win.showBetaNote();
    ok(badge.style.display !== 'none', 'the badge is shown for a beta deck');
    for (const r of FIXTURE[2].betaReasons) {
      ok(badge.textContent.includes(r), 'the badge names the reason: ' + r, badge.textContent);
    }
    sel.value = 'Ready.cod'; win.showBetaNote();
    ok(badge.style.display === 'none', 'the badge is hidden for a ready deck', badge.textContent);
    sel.value = 'NoProf.cod'; win.showBetaNote();
    ok(badge.style.display === 'none', 'the badge is hidden for a profile-less deck');
    finish();
  })().catch(e => { ok(false, 'UI rendering threw', String(e && e.message || e)); finish(); });
}

function finish() {
const beta = decks.filter(d => d.beta).map(d => d.name);
const ready = decks.filter(d => d.hasProfile && !d.beta).map(d => d.name);
console.log('');
console.log('  beta  (%d): %s', beta.length, beta.join(', ') || '(none)');
console.log('  ready (%d): %s', ready.length, ready.join(', ') || '(none)');
console.log('');
if (fails) { console.log('viewer deck-beta check: %d FAILURE(S)', fails); process.exit(1); }
console.log('viewer deck-beta check: PASS');
process.exit(0);      // the jsdom window keeps the loop alive otherwise
}
