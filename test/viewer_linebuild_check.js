#!/usr/bin/env node
// Viewer line-build regression check (FRONTEND layer).
// ============================================================================
// Companion to test/viewer_protocol_check.py. That check guards the engine↔protocol
// contract: it feeds a reference's chosen plan INDICES into the binary and verifies the
// engine emits well-formed decisions and reaches a clean terminal. It CANNOT see the
// browser: the reference records `chosen: <index>`, and the engine enumerates that index
// fine, so a GUI that is physically unable to BUILD that line from clicks still passes.
//
// This check closes that gap by driving the REAL GUI line-building code (tools/play/
// linebuild.js, the same module index.html loads) headlessly. For every main-phase
// decision the user actually played in a saved reference, it reconstructs the chosen
// plan's land + hand casts by calling LineBuild.queueCard() exactly as a double-click /
// drag would, then asserts the built line reproduces that plan. The staged-cast (Soulfire
// dig / Light Up the Stage) regression -- where queueCard silently refused to queue a card
// with no non-staged hand copy -- surfaces here as a short/mismatched reconstruction.
//
// Scope: land drop + plain HAND casts (kind land/nonpermanent/permanent), which is where
// the queue cap lives. A chosen plan that casts something NOT in hand (Retrace from the
// graveyard, an Aether Vial deploy) is queued through a different GUI path (retraceCard /
// toggleVialMode), not queueCard, so those plans are skipped as out-of-scope for v1 --
// reported in the summary so the coverage is honest.
//
// Usage:  node test/viewer_linebuild_check.js         # all references, exit 1 on any FAIL
'use strict';
const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..');
const LB = require(path.join(ROOT, 'tools', 'play', 'linebuild.js'));
const REFROOT = path.join(ROOT, 'references');

// Collect references/<deck>/claude_s*_gi*.json, one level deep only (mirrors
// viewer_protocol_check.py): the deeper references/suboptimal|optimal/... sets are
// knowingly-beatable targets, not the verified benchmark, so they're excluded.
function collectRefs() {
  const out = [];
  if (!fs.existsSync(REFROOT)) return out;
  for (const deck of fs.readdirSync(REFROOT)) {
    if (deck === 'suboptimal' || deck === 'optimal') continue;
    const dir = path.join(REFROOT, deck);
    if (!fs.statSync(dir).isDirectory()) continue;
    for (const f of fs.readdirSync(dir)) {
      if (/^claude_s.*_gi.*\.json$/.test(f)) out.push(path.join(dir, f));
    }
  }
  return out.sort();
}

function multiset(arr) { return arr.slice().sort(); }
function eqMultiset(a, b) { return JSON.stringify(multiset(a)) === JSON.stringify(multiset(b)); }

// Is `name` castable in this decision ONLY as a staged (exiled) card -- i.e. present in hand
// but with no non-staged copy? These are the cases the queue-cap bug broke.
function isStagedOnly(decision, name) {
  const hand = decision.me.hand || [];
  const copies = hand.filter(c => c.name === name);
  return copies.length > 0 && copies.every(c => c.is_staged);
}

function checkReference(p) {
  const ref = JSON.parse(fs.readFileSync(p, 'utf8'));
  const res = { checked: 0, staged: 0, skipped: 0, fails: [] };
  for (const de of ref.decisions || []) {
    const d = de.decision || {};
    if (d.type !== 'main_phase') continue;
    const ch = de.chosen;
    const plans = d.plans || [];
    if (typeof ch !== 'number' || ch < 0 || ch >= plans.length) continue;  // pass (-1) / drift: nothing to build
    const plan = plans[ch];
    const casts = plan.casts || [];

    // A cast not backed by a hand card (Retrace from yard / Vial deploy) uses a different GUI
    // path than queueCard -> out of scope for the line-build check. Skip the whole plan.
    const hand = d.me.hand || [];
    if (casts.some(nm => !hand.some(c => c.name === nm))) { res.skipped++; continue; }

    // Reconstruct the line the way the GUI would: queue the land, then each cast (kind read from
    // the hand card, exactly as index.html reads data-kind off the thumb).
    let built = [];
    if (plan.land) built = LB.queueCard(d, built, plan.land, 'land');
    for (const nm of casts) {
      const hc = hand.find(c => c.name === nm);
      built = LB.queueCard(d, built, nm, hc.kind);
    }

    res.checked++;
    const anyStaged = casts.some(nm => isStagedOnly(d, nm));
    if (anyStaged) res.staged++;

    const gotLand = LB.planLand(built);
    const wantLand = plan.land || '';
    const gotCasts = built.filter(x => x.kind !== 'land').map(x => x.name);
    if ((gotLand ? gotLand.name : '') !== wantLand || !eqMultiset(gotCasts, casts)) {
      res.fails.push({
        turn: d.turn, wantLand, wantCasts: casts,
        gotLand: gotLand ? gotLand.name : '', gotCasts, anyStaged,
      });
    }
  }
  return res;
}

// ---- Multi-dimension choose-picker walk (viewer issue #13) -----------------------------------
// A committed line with TWO independent sub-decisions (Crop Rotation + Sylvan Scrying, each
// searching for a land) must produce TWO pickers, each offering every land. It was reported as
// producing one; replaying the real game showed the engine emits 144 variants carrying both
// dimensions, so any loss would be here in the client walk -- exactly the blind spot the engine-side
// checks cannot see. Driven on a synthetic payload shaped like that real one (2 tutor dimensions x
// 12 choices) so the check stays binary-free and sub-second.
function checkDimensionWalk() {
  const LANDS = ['Windswept Heath','Breeding Pool','Forbidden Orchard','Tree of Tales','City of Brass',
                 'Azorius Chancery','Forest','Temple Garden','Reflecting Pool','Misty Rainforest',
                 'Overgrown Tomb','Stomping Ground'];
  const variants = [];
  let pi = 0;
  for (const b of LANDS) for (const a of LANDS) {
    variants.push({ plan_index: pi++, label: `Crop Rotation -> ${a}; Sylvan Scrying -> ${b}`, cards: [a, b],
                    subs: [{ key: 'Crop Rotation \u2192', choice: a, card: a, kind: 'tutor' },
                           { key: 'Sylvan Scrying \u2192', choice: b, card: b, kind: 'tutor' }] });
  }
  const fails = [];
  if (LB.dimensionsRemaining(variants) !== 2)
    fails.push(`dimensionsRemaining = ${LB.dimensionsRemaining(variants)}, expected 2 (both tutors must be asked)`);
  // Walk it the way the dialog does: ask, pick, filter, ask again.
  let remaining = variants, asked = [];
  for (let guard = 0; guard < 8; guard++) {
    const nd = LB.nextDimension(remaining);
    if (!nd) break;
    asked.push({ key: nd.dim.key, n: nd.choices.length });
    remaining = LB.filterByChoice(remaining, nd.dim.key, nd.choices[2].choice);   // pick the 3rd
  }
  if (asked.length !== 2) fails.push(`walk asked ${asked.length} picker(s), expected 2: ${JSON.stringify(asked)}`);
  asked.forEach(a => { if (a.n !== 12) fails.push(`picker ${a.key} offered ${a.n} choices, expected 12`); });
  if (remaining.length !== 1) fails.push(`after both picks ${remaining.length} variants remain, expected exactly 1`);
  return fails;
}

function main() {
  const refs = collectRefs();
  if (!refs.length) { console.log('no reference games found under references/'); return 0; }
  let checked = 0, staged = 0, skipped = 0, fail = 0;
  for (const p of refs) {
    const rel = path.relative(REFROOT, p);
    const r = checkReference(p);
    checked += r.checked; staged += r.staged; skipped += r.skipped;
    for (const f of r.fails) {
      fail++;
      console.log(`  FAIL  ${rel}  turn=${f.turn}: built [land=${f.gotLand || '-'}; casts=${JSON.stringify(f.gotCasts)}] ` +
                  `!= chosen [land=${f.wantLand || '-'}; casts=${JSON.stringify(f.wantCasts)}]` +
                  `${f.anyStaged ? '  (staged-only cast — the queue-cap regression)' : ''}`);
    }
  }
  console.log(`\nViewer line-build: ${checked} main-phase lines reconstructed ` +
              `(${staged} involving staged-only casts), ${skipped} skipped (non-hand cast: retrace/vial), ` +
              `${fail} FAIL  (${refs.length} refs)`);
  if (fail) {
    console.log('  FAIL = the GUI cannot rebuild a line the user actually played -> a viewer regression ' +
                '(a card that queueCard/encodeLine drops). Fix the shared logic in tools/play/linebuild.js.');
  }
  const dimFails = checkDimensionWalk();
  dimFails.forEach(m => console.log(`  FAIL  choose-picker walk: ${m}`));
  console.log(`Viewer choose-picker: 2-tutor line asks ${dimFails.length ? 'WRONG' : '2 pickers x 12 choices'} ` +
              `(${dimFails.length} FAIL)`);
  return (fail + dimFails.length) ? 1 : 0;
}

process.exit(main());
