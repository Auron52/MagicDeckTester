#!/usr/bin/env node
// Deck-maturity (alpha / beta / stable) check for the play viewer.
// =====================================================================================
// The picker grades every deck so a user choosing one can see how far its numbers can be trusted:
//
//   (alpha)  a piece of the apparatus is MISSING -- <10 optimal references, no value leaf, or no
//            completed mulligan profile. The numbers may simply be wrong.
//   (beta)   complete but unproven -- fewer than 30 references, or the shipped search does not
//            match the human win turn on the ones it has.
//   (none)   30+ references AND green on every one of them.
//
// See tierFrom / deckMaturity in tools/play/server.js.
//
// WHY THIS NEEDS A TEST AT ALL. Every failure mode here is silent and reads as good news: a rule
// that stops firing does not error, it just quietly promotes an unfinished deck to the top tier.
// The four specific ways it can rot:
//
//   * the artifact NAME drifts. The mulligan check probes the same two extensions
//     MulliganProfileIO.h:967 loads, in the same order, and the value leaf is the same
//     `<stem>.value.json` AttachValueSidecar resolves. If the engine's names move and these do not,
//     every deck silently reads "complete".
//   * SUBOPTIMAL references start counting. They live one level deeper (references/suboptimal/<Deck>/)
//     and are excluded structurally, not by a filter -- but "count the .json files under the deck"
//     is the obvious refactor, and it would inflate three decks toward the threshold with games the
//     user explicitly flagged as NOT the standard.
//   * references played on an ARCHIVED list start counting (deck_registry.REFERENCE_DECK). That one
//     already happened: Mirrorwing Dragon read "ready" on 24 games played against a deck it is no
//     longer, which is the worst version, since the top tier is exactly what references earn.
//   * the BENCH gate goes soft. A stale, missing or hand-mismatched bench must not read as green --
//     absence of evidence is not evidence, and this is the only criterion that can move a deck DOWN
//     after it was fine, so a soft version of it makes the whole scheme monotone and inert.
//
// Fast, static, no binary: the policy is a pure function and the rest is directory reads. jsdom, if
// installed, additionally proves the label actually renders.
// Run:  node test/viewer_deck_beta_check.js
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
// Every combination of every criterion, at and around both thresholds. Written out rather than
// generated so the EXPECTED column is a statement of policy someone can read and disagree with.
console.log('--- policy (tierFrom) ---');
const A = srv.MIN_OPTIMAL_REFS;      // below this -> alpha
const S = srv.STABLE_REFS;           // at or above this (and green) -> stable
ok(A === 10, 'the alpha floor is 10 optimal references', 'MIN_OPTIMAL_REFS=' + A);
ok(S === 30, 'the stable bar is 30 reference games', 'STABLE_REFS=' + S);
ok(A < S, 'the alpha floor is below the stable bar');

const GREEN = { state: 'green', n: 40 };
// A stale bench carries the LAST verdict forward (user, 2026-08-29), so there are two of them and
// they must land on opposite sides: stale-was-green keeps the top tier, stale-was-short does not.
const STALE_GREEN = { state: 'stale', at: 'abc123', n: 40, wasGreen: true };
const STALE_SHORT = { state: 'stale', at: 'abc123', n: 40, wasGreen: false, short: 3 };
const BENCHES = [GREEN, { state: 'short', short: 1, n: 40 }, STALE_GREEN, STALE_SHORT,
                 { state: 'unbenched' }];
// Does this bench count as proof of green play? The policy, restated independently of tierFrom.
const benchProves = b => b.state === 'green' || (b.state === 'stale' && b.wasGreen);

for (const refs of [0, A - 1, A, A + 1, S - 1, S, S + 1]) {
  for (const hasValueLeaf of [false, true]) {
    for (const hasKeepModel of [false, true]) {
      for (const bench of BENCHES) {
        const r = srv.tierFrom({ hasProfile: true, refs, hasValueLeaf, hasKeepModel, bench });
        // The policy, restated independently of the implementation.
        const missing = refs < A || !hasValueLeaf || !hasKeepModel;
        const want = missing ? 'alpha'
                   : (refs >= S && benchProves(bench)) ? 'stable' : 'beta';
        ok(r.tier === want,
           `tier(refs=${refs}, value=${hasValueLeaf}, keep=${hasKeepModel}, bench=${bench.state}) === ${want}`,
           'got ' + r.tier);
        // A labelled deck must always be able to say why -- the badge renders the reasons, so an
        // empty list would show "beta — " with nothing after it.
        ok((r.tierReasons.length > 0) === (r.tier === 'alpha' || r.tier === 'beta'),
           `${want}: reasons present iff labelled`, JSON.stringify(r.tierReasons));
        // ALPHA MUST NOT LEAK BETA'S REASONS: an apparatus-missing deck is not also "28/30", which
        // would read as nearly-done when the truth is that a piece is absent.
        if (r.tier === 'alpha') {
          ok(!r.tierReasons.some(x => x.includes('/' + S + ' reference')),
             'alpha does not also report the 30-reference shortfall', JSON.stringify(r.tierReasons));
        }
      }
    }
  }
}

// THE GREEN GATE, isolated: identical deck, only the bench differs. This is the criterion the whole
// three-tier scheme turns on, and the only one that can move a deck DOWN after it was fine.
for (const bench of BENCHES) {
  const r = srv.tierFrom({ hasProfile: true, refs: S + 5, hasValueLeaf: true, hasKeepModel: true, bench });
  ok(r.tier === (benchProves(bench) ? 'stable' : 'beta'),
     `a fully-equipped ${S + 5}-reference deck is ${benchProves(bench) ? 'stable' : 'beta'} when the bench is ${bench.state}`
       + (bench.state === 'stale' ? ` (wasGreen=${!!bench.wasGreen})` : ''),
     'got ' + r.tier);
  if (!benchProves(bench)) {
    ok(r.tierReasons.length === 1, 'the ONLY thing holding it back is the bench', JSON.stringify(r.tierReasons));
  }
}
// STALENESS ALONE IS NOT A DEMOTION (user, 2026-08-29: "they should be marked as beta only if they
// were not stable before"). Every src commit stales every deck at once, so a stale-means-beta rule
// re-labelled the entire picker on every commit -- signalling nothing about any individual deck.
// The carried-forward verdict is the last REAL measurement, so this is not "assume green": a deck
// that was short stays beta, and a re-bench that finds a genuine shortfall still demotes it.
ok(srv.tierFrom({ hasProfile: true, refs: 99, hasValueLeaf: true, hasKeepModel: true,
                  bench: { state: 'stale', at: 'deadbee', n: 99, wasGreen: true } }).tier === 'stable',
   'a stale bench whose last verdict was GREEN keeps the deck stable');
ok(srv.tierFrom({ hasProfile: true, refs: 99, hasValueLeaf: true, hasKeepModel: true,
                  bench: { state: 'stale', at: 'deadbee', n: 99, wasGreen: false, short: 2 } }).tier === 'beta',
   'a stale bench whose last verdict was SHORT still holds the deck at beta');
// The demotion path still works end-to-end: stale-and-green today, genuinely short after the
// re-bench lands => beta. This is the property the old stale-is-never-green rule existed to protect.
ok(srv.tierFrom({ hasProfile: true, refs: 99, hasValueLeaf: true, hasKeepModel: true,
                  bench: { state: 'short', short: 4, n: 99 } }).tier === 'beta',
   'a completed re-bench that finds a shortfall still demotes a formerly-stable deck');
// Neither is a bench that never ran -- absence of evidence must not read as evidence.
ok(srv.tierFrom({ hasProfile: true, refs: 99, hasValueLeaf: true, hasKeepModel: true,
                  bench: { state: 'unbenched' } }).tier === 'beta',
   'an un-benched deck cannot promote to stable');
// A missing `bench` argument entirely must fail CLOSED, not throw and not pass.
ok(srv.tierFrom({ hasProfile: true, refs: 99, hasValueLeaf: true, hasKeepModel: true }).tier === 'beta',
   'a missing bench argument fails closed to beta');

// A deck with NO profile is unplayable, not alpha: the picker already renders "(no profile)" and
// disables it, which is the strictly stronger statement. Stacking a tier on it would say less.
for (const refs of [0, S + 1]) {
  const r = srv.tierFrom({ hasProfile: false, refs, hasValueLeaf: false, hasKeepModel: false, bench: GREEN });
  ok(r.tier === 'unplayable', 'a profile-less deck is "unplayable", not a tier');
  ok(r.tierReasons.length === 0, 'a profile-less deck carries no tier reasons');
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
  ok(d.tier === 'alpha', `${d.name}: is ALPHA on someone else's games, not a graded deck`, d.tier);
  ok(d.tierReasons.some(r => r.includes('archived list')),
     `${d.name}: the badge explains WHY the count is 0`, JSON.stringify(d.tierReasons));
}

// ---- 4) the live tree agrees with the policy ----------------------------------------
// listDecks() is what /api/decks serves. Recompute beta from the fields it reports and require the
// two to agree, so a wiring mistake in listDecks (stale field, wrong argument order) is caught even
// though the policy itself is right.
console.log('--- listDecks() wiring ---');
const decks = srv.listDecks();
ok(decks.length > 0, 'listDecks() found decks');
for (const d of decks) {
  for (const f of ['refs', 'hasValueLeaf', 'hasKeepModel', 'tier', 'tierReasons', 'bench']) {
    ok(f in d, `${d.name}: /api/decks reports "${f}"`);
  }
  const want = srv.tierFrom(d);
  ok(d.tier === want.tier, `${d.name}: reported tier matches the policy applied to its own fields`,
     `tier=${d.tier} refs=${d.refs} value=${d.hasValueLeaf} keep=${d.hasKeepModel} bench=${d.bench.state}`);
  // A deck whose references belong to an archived list must not read the OWNER's bench: lending
  // that green to the list which replaced it is the borrowed-evidence bug in a second form.
  if (d.refsOnArchivedList) {
    ok(d.bench.state === 'unbenched', `${d.name}: does not borrow the archived list's bench`, d.bench.state);
  }
  // Every saved game in the folder is accounted for as EITHER counted or archived-list -- stronger
  // than "refs == folder size", which stopped being true once ownership mattered, and it catches a
  // game silently vanishing from both columns.
  // Scoped to the ENTRY'S OWN folder. A deck may now offer several lists -- the one that ships plus
  // any archived list under decks/<Deck>/<Version>/ -- and each keeps its references in its own
  // references/<Deck>[/<Version>]/ folder. Counting the deck NAME here compared a variant's refs
  // against the shipping list's folder and failed for every variant.
  ok(d.refs + d.refsOnArchivedList === srv.countOptimalRefs(d.name, d.version),
     `${d.label || d.name}: every saved game is either counted or attributed to an archived list`,
     `refs=${d.refs} archived=${d.refsOnArchivedList} folder=${srv.countOptimalRefs(d.name, d.version)}`);
  ok(!(d.refs > 0 && d.refsOnArchivedList > 0), `${d.label || d.name}: a folder belongs to ONE list, not both`);
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
    { deck: 'Stable.cod', name: 'Stable', hasProfile: true,  tier: 'stable',     refs: 31, refGoal: null, tierReasons: [] },
    { deck: 'Beta.cod',   name: 'Beta',   hasProfile: true,  tier: 'beta',       refs: 24, refGoal: 30,   tierReasons: ['24/30 reference games'] },
    { deck: 'Alpha.cod',  name: 'Alpha',  hasProfile: true,  tier: 'alpha',      refs: 0,  refGoal: 10,   tierReasons: ['0/10 optimal reference games', 'no value-leaf model', 'no completed mulligan profile'] },
    { deck: 'NoProf.cod', name: 'NoProf', hasProfile: false, tier: 'unplayable', refs: 0,  refGoal: 10,   tierReasons: [] },
    // The two shapes the "always show progress" rule adds (USER 2026-08-27): a deck whose ref
    // count is NOT among its tier reasons still shows the fraction while the next requirement is
    // unmet, and a deck PAST the last count requirement shows the bare tier word.
    { deck: 'Alpha2.cod', name: 'Alpha2', hasProfile: true,  tier: 'alpha',      refs: 13, refGoal: 30,   tierReasons: ['no value-leaf model'] },
    { deck: 'Beta2.cod',  name: 'Beta2',  hasProfile: true,  tier: 'beta',       refs: 31, refGoal: null, tierReasons: ['the search is slower than the human on 1 of 31 references'] },
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
    ok(label('Stable') === 'Stable',              'a stable deck gets no suffix', label('Stable'));
    ok(label('Beta')   === 'Beta (beta)',         'an unproven deck is "(beta)"', label('Beta'));
    ok(label('Alpha')  === 'Alpha (alpha)',       'an apparatus-missing deck is "(alpha)"', label('Alpha'));
    ok(label('NoProf') === 'NoProf (no profile)', 'a profile-less deck stays "(no profile)"', label('NoProf'));
    const noProfOpt = [...sel.options].find(o => o.value === 'NoProf.cod');
    ok(noProfOpt && noProfOpt.disabled, 'a profile-less deck is still disabled');

    // The badge follows the SELECTED deck. It is a COMPACT CHIP -- visible text is the tier word
    // only, with every reason in the hover tooltip (USER 2026-08-27: the inlined reasons string
    // pushed the reference-saved badge out of the header, and that confirmation "is 1000 times
    // more important"; also "the 'reference bench is stale...' is long and unnecessary").
    const badge = win.document.getElementById('betanote');
    ok(!!badge, 'the top bar has a #betanote badge');
    const refBadge = win.document.getElementById('refnote');
    ok(refBadge && badge.compareDocumentPosition(refBadge) & 2 /* PRECEDING */,
       'the reference-saved badge comes BEFORE the maturity chip, so it can never be pushed by it');
    sel.value = 'Alpha.cod'; win.showBetaNote();
    ok(badge.style.display !== 'none', 'the badge is shown for an alpha deck');
    ok(badge.classList.contains('alpha'), 'the alpha badge is styled differently from beta');
    ok(badge.textContent === 'alpha 0/10',
       'the chip is the tier word plus the reference fraction (USER: the count to the next level '
       + 'was the only useful part of the old inlined string)', badge.textContent);
    for (const r of FIXTURE[2].tierReasons) {
      ok(badge.title.includes(r), 'the tooltip names the reason: ' + r, badge.title);
    }
    sel.value = 'Beta.cod'; win.showBetaNote();
    ok(badge.style.display !== 'none', 'the badge is shown for a beta deck');
    ok(!badge.classList.contains('alpha'), 'the beta badge drops the alpha styling on re-select',
       badge.className);
    ok(badge.textContent === 'beta 24/30', 'the beta chip carries its reference fraction',
       badge.textContent);
    ok(badge.title.includes('24/30 reference games'), 'the beta tooltip names its reason',
       badge.title);
    sel.value = 'Alpha2.cod'; win.showBetaNote();
    ok(badge.textContent === 'alpha 13/30',
       'the fraction shows even when the count is NOT a tier reason (met 10, chasing 30)',
       badge.textContent);
    sel.value = 'Beta2.cod'; win.showBetaNote();
    ok(badge.textContent === 'beta',
       'past the last count requirement the chip is the bare tier word', badge.textContent);
    sel.value = 'Stable.cod'; win.showBetaNote();
    ok(badge.style.display === 'none', 'the badge is hidden for a stable deck', badge.textContent);
    sel.value = 'NoProf.cod'; win.showBetaNote();
    ok(badge.style.display === 'none', 'the badge is hidden for a profile-less deck');
    finish();
  })().catch(e => { ok(false, 'UI rendering threw', String(e && e.message || e)); finish(); });
}

function finish() {
console.log('');
for (const t of ['stable', 'beta', 'alpha']) {
  const names = decks.filter(d => d.tier === t).map(d => d.name);
  // padEnd, not '%-7s' -- Node's console.log has no width specifier and prints it literally.
  console.log('  ' + (t + ' ').padEnd(8) + '(' + names.length + '): ' + (names.join(', ') || '(none)'));
}
console.log('');
if (fails) { console.log('viewer deck-beta check: %d FAILURE(S)', fails); process.exit(1); }
console.log('viewer deck-beta check: PASS');
process.exit(0);      // the jsdom window keeps the loop alive otherwise
}
