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

// ---- Natural Order: the SACRIFICE victim is asked, and asked FIRST ---------------------------
// "As an additional cost to cast this spell, sacrifice a green creature" is a real, irreversible
// choice, but it rode Action::soulfire_own_targets with no sub -- so the per-victim plans shared a
// signature, the dedup kept one, and the viewer silently ate the engine's pick (user-reported
// 2026-08-23: "No choice was given for what to sacrifice to Natural Order"). It must also be asked
// BEFORE the tutor target, because the victim GATES the fetch: a sacrificed Worldspine Wurm
// shuffles itself back in and only then is a legal target. Synthetic + binary-free, like the walk
// above; the engine half is guarded by the reference sweep and the scenario fixtures.
function checkSacrificeDimension() {
  const VICTIMS = ['Arbor Elf', 'Elvish Archdruid'];
  const TARGETS = ['Worldspine Wurm', 'Craterhoof Behemoth', 'Terastodon'];
  const variants = [];
  let pi = 0;
  for (const v of VICTIMS) for (const t of TARGETS) {
    variants.push({ plan_index: pi++, label: `Natural Order sacrifices ${v}; Natural Order → ${t}`,
                    subs: [{ key: 'Natural Order sacrifices', choice: v, card: v, kind: 'sacrifice' },
                           { key: 'Natural Order →', choice: t, card: t, kind: 'tutor' }] });
  }
  const fails = [];
  if (LB.dimensionsRemaining(variants) !== 2)
    fails.push(`dimensionsRemaining = ${LB.dimensionsRemaining(variants)}, expected 2 (victim AND target)`);
  const first = LB.nextDimension(variants);
  if (!first || first.dim.kind !== 'sacrifice')
    fails.push(`first picker is ${first ? first.dim.kind : 'none'}, expected the sacrifice victim ` +
               `(it gates which creatures the fetch can find)`);
  if (first && first.choices.length !== VICTIMS.length)
    fails.push(`victim picker offered ${first.choices.length} choices, expected ${VICTIMS.length}`);
  let remaining = first ? LB.filterByChoice(variants, first.dim.key, first.choices[0].choice) : variants;
  const second = LB.nextDimension(remaining);
  if (!second || second.dim.kind !== 'tutor')
    fails.push(`second picker is ${second ? second.dim.kind : 'none'}, expected the tutor target`);
  if (second && second.choices.length !== TARGETS.length)
    fails.push(`target picker offered ${second.choices.length} choices, expected ${TARGETS.length}`);
  return fails;
}

// ---- Board activations encode with the RIGHT LineSpec verb ------------------------------------
// An activation of a permanent already in play is committed by a verb the engine chose (see the
// `verb` field on the plan action). Writing the wrong one makes CheckLine reject a legal line: an
// ActivatePump used to encode as `sacout=`, a verb only ever matched against SacForMana /
// SacCreatureOutlet actions, so every Minotaur pump read as a reject; and equipping needs `equip=`
// because `cast=Bonesplitter` cannot be told apart from CASTING the copy in hand.
function checkActivationVerbs() {
  const fails = [];
  const enc = (entry) => LB.encodeLine([entry]);
  const cases = [
    [{ name: 'Krenko, Mob Boss', src: 'Krenko, Mob Boss', kind: 'activate' }, 'cast=Krenko, Mob Boss'],
    [{ name: 'Skirk Prospector', src: 'Skirk Prospector', kind: 'activate', verb: 'sacout', sacout: true }, 'sacout=Skirk Prospector'],
    [{ name: 'Skirk Prospector', src: 'Skirk Prospector', kind: 'activate', sacout: true }, 'sacout=Skirk Prospector'],  // legacy entry, no verb
    [{ name: 'Bonesplitter', src: 'Bonesplitter', kind: 'activate', verb: 'equip' }, 'equip=Bonesplitter'],
    [{ name: 'Balan, Wandering Knight', src: 'Balan, Wandering Knight', kind: 'activate', verb: 'attachall' }, 'attachall=Balan, Wandering Knight'],
    [{ name: 'Colossus Hammer', src: 'Stoneforge Mystic', kind: 'activate', verb: 'sfput' }, 'sfput=Colossus Hammer'],
    [{ name: "Umezawa's Jitte", src: "Umezawa's Jitte", kind: 'activate', verb: 'jittemode', mode: 1 }, 'jittemode=1'],
  ];
  cases.forEach(([entry, want]) => {
    const got = enc(entry);
    if (got !== want) fails.push(`${entry.src} (${entry.verb || 'default'}) encoded "${got}", expected "${want}"`);
  });
  // A line made only of board activations must NOT collapse to 'pass' (CheckLine stage 0).
  const only = LB.encodeLine([{ name: 'Bonesplitter', src: 'Bonesplitter', kind: 'activate', verb: 'equip' }]);
  if (only === 'pass') fails.push('an activation-only line encoded as "pass"');
  return fails;
}

// The BONUS LAND DROP path (viewer issue #7). A Scale the Heights / Explore grant makes
// me.land_drops_left > 1, and the engine's Plan still carries ONE land -- so a two-land turn is
// committed as consecutive SEGMENTS. Both halves of that live here (queueCard must ADD rather than
// replace; encodeSegments must split lands-first), and neither is visible to the reference sweep,
// since every saved reference predates the field. Synthetic + binary-free, like the dimension walk.
function checkBonusLandDrop() {
  const hand = [{ name: 'Forest', kind: 'land' }, { name: 'Forest', kind: 'land' },
                { name: 'Mountain', kind: 'land' }, { name: 'Gold Rush', kind: 'nonpermanent' }];
  const two = { me: { hand, land_drops_left: 2 } };
  const one = { me: { hand, land_drops_left: 1 } };
  const fails = [];
  const eq = (a, b) => JSON.stringify(a) === JSON.stringify(b);

  // Two drops: two DIFFERENT lands + a cast -> land, then land + cast.
  let p = [];
  p = LB.queueCard(two, p, 'Forest', 'land');
  p = LB.queueCard(two, p, 'Mountain', 'land');
  p = LB.queueCard(two, p, 'Gold Rush', 'nonpermanent');
  if (!eq(LB.encodeSegments(p), ['land=Forest', 'land=Mountain;cast=Gold Rush']))
    fails.push(`two drops -> ${JSON.stringify(LB.encodeSegments(p))}`);

  // Two drops, two copies of the SAME land: both queue (the copy cap is the hand, not the name).
  let q = [];
  q = LB.queueCard(two, q, 'Forest', 'land');
  q = LB.queueCard(two, q, 'Forest', 'land');
  if (!eq(LB.encodeSegments(q), ['land=Forest', 'land=Forest']))
    fails.push(`two drops, two Forests -> ${JSON.stringify(LB.encodeSegments(q))}`);

  // One drop: a second land is still a CORRECTION (replace), and the same land toggles OFF.
  let r = [];
  r = LB.queueCard(one, r, 'Forest', 'land');
  r = LB.queueCard(one, r, 'Mountain', 'land');
  if (!eq(LB.encodeSegments(r), ['land=Mountain'])) fails.push(`one drop replace -> ${JSON.stringify(r)}`);
  let t = [];
  t = LB.queueCard(one, t, 'Forest', 'land');
  t = LB.queueCard(one, t, 'Forest', 'land');
  if (t.length) fails.push(`one drop, same land twice should toggle off -> ${JSON.stringify(t)}`);

  // No land_drops_left field (every saved reference): the one-drop rules must still apply.
  const legacy = { me: { hand } };
  let u = [];
  u = LB.queueCard(legacy, u, 'Forest', 'land');
  u = LB.queueCard(legacy, u, 'Mountain', 'land');
  if (!eq(LB.encodeSegments(u), ['land=Mountain'])) fails.push(`legacy payload -> ${JSON.stringify(u)}`);
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
  const landFails = checkBonusLandDrop();
  landFails.forEach(m => console.log(`  FAIL  bonus land drop: ${m}`));
  console.log(`Viewer bonus land drop: ${landFails.length ? 'WRONG' : 'two drops commit as two segments'} ` +
              `(${landFails.length} FAIL)`);
  const sacFails = checkSacrificeDimension();
  sacFails.forEach(m => console.log(`  FAIL  sacrifice dimension: ${m}`));
  console.log(`Viewer sacrifice picker: ${sacFails.length ? 'WRONG' : 'victim asked first, then the fetch target'} ` +
              `(${sacFails.length} FAIL)`);
  const verbFails = checkActivationVerbs();
  verbFails.forEach(m => console.log(`  FAIL  activation verb: ${m}`));
  console.log(`Viewer activation verbs: ${verbFails.length ? 'WRONG' : 'cast/sacout/equip/attachall/sfput/jittemode all encode'} ` +
              `(${verbFails.length} FAIL)`);
  return (fail + dimFails.length + landFails.length + sacFails.length + verbFails.length) ? 1 : 0;
}

process.exit(main());
