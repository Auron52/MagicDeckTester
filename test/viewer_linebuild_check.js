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
    // `plan.casts` names a card once per plan ENTRY, and an ACTIVATION of a permanent already on
    // the battlefield appears there under the same name as a hand cast of another copy. Minotaur
    // s11/gi10 T5 is exactly that shape: one Burning-Fist Minotaur in hand, one already in play --
    // cast the first and pump the second, and `casts` reads ["Burning-Fist Minotaur",
    // "Burning-Fist Minotaur"] for a line with ONE hand cast.
    //
    // Replaying both through queueCard asked the GUI to queue two hand casts off a single hand
    // card, which it CORRECTLY refuses (castableCount counts copies in hand) -- so this check
    // reported "the GUI cannot rebuild a line the user actually played" about a line the GUI
    // builds fine, and the human had in fact played it. The activation reaches the plan by a board
    // click, not a hand-thumb double-click: a different GUI path, and out of scope here exactly
    // like the Retrace/Vial casts skipped below.
    //
    // `plan.actions` is the field that distinguishes them (`activate: true`), so SUBTRACT the
    // activations from `casts` rather than rebuilding the list out of `actions`. Rebuilding was
    // tried first and is wrong: a cycling / sac-draw land (Fiery Islet, Lonely Sandbar, Horizon
    // Canopy) is an action WITHOUT `activate` that `casts` deliberately omits, so deriving the list
    // from `actions` turned one false failure into five. `casts` stays the source of truth; the
    // only correction is dropping the entries that are activations of a permanent already in play.
    const actCount = {};
    for (const a of (Array.isArray(plan.actions) ? plan.actions : [])) {
      if (a && a.activate) { actCount[a.card] = (actCount[a.card] || 0) + 1; }
    }
    const casts = (plan.casts || []).filter(nm => {
      if (actCount[nm]) { actCount[nm]--; return false; }                 // this entry is the activation
      return true;
    });

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

// ---- A MIXED variant set is still walked, never dumped flat ----------------------------------
// Some variants of a line carry a dimension and others simply do not: "cast Bonesplitter" versus
// "cast Bonesplitter AND equip the copy in play" is the shape that got reported (KittyEquipment
// seed 7). index.html used to bail to a flat "Choose how to resolve" grid whenever ANY variant
// lacked `subs` -- a row of repeated card art labelled by whole line summaries in overflowing
// badges, which the user asked to be killed outright (2026-09-01). The walk handles the shape fine:
// a variant with no value in a dimension reports '—', which is just another choice. This pins that,
// binary-free, so the fallback cannot creep back in by way of a walk that can't cope.
function checkMixedSubVariantWalk() {
  const variants = [
    { plan_index: 0, label: 'cast: Bonesplitter', cards: [], subs: [] },                       // no equip at all
    { plan_index: 1, label: 'cast: equip Bonesplitter — Bonesplitter → Balan', cards: ['Balan'],
      subs: [{ key: 'Bonesplitter equips to', choice: 'Balan', card: 'Balan', kind: 'equip', num: 5 }] },
    { plan_index: 2, label: 'cast: equip Bonesplitter — Bonesplitter → Puresteel Paladin', cards: ['Puresteel Paladin'],
      subs: [{ key: 'Bonesplitter equips to', choice: 'Puresteel Paladin', card: 'Puresteel Paladin', kind: 'equip', num: 9 }] },
  ];
  const fails = [];
  const nd = LB.nextDimension(variants);
  if (!nd) { fails.push('no dimension found in a MIXED sub/no-sub set (the flat picker case)'); return fails; }
  if (nd.choices.length !== 3)
    fails.push(`mixed set offered ${nd.choices.length} choices, expected 3 (two hosts + the no-equip '—')`);
  if (!nd.choices.some(c => c.choice === '—'))
    fails.push("the variant WITHOUT the dimension is not offered as '—' (it would be unreachable)");
  // The m_number rides through the walk, which is what lets the dialog auto-resolve a DRAG by
  // identity instead of by a display name two creatures can share.
  const balan = nd.choices.find(c => c.choice === 'Balan');
  if (!balan || balan.num !== 5) fails.push(`the choice's num did not survive the walk (${balan && balan.num})`);
  // Every choice must actually narrow to something.
  nd.choices.forEach(c => {
    const left = LB.filterByChoice(variants, nd.dim.key, c.choice);
    if (left.length !== 1) fails.push(`choice "${c.choice}" left ${left.length} variants, expected 1`);
  });
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
    // The HOST (and the copy being moved) ride on the token as m_numbers. Leaving the host to the
    // `equip` sub-decision -- whose choice string is the host's NAME -- meant two same-named hosts
    // shared a dedup signature and all but one variant was silently dropped, so the second Kor
    // Duelist could not be equipped at all (2026-09-01). Each id is independently optional: a
    // from-HAND cast-and-equip knows the host but the source is only 0 until the cast resolves.
    [{ name: 'Bonesplitter', src: 'Bonesplitter', kind: 'activate', verb: 'equip', srcNum: 7, target: 19 },
     'equip=Bonesplitter#7@19'],
    [{ name: 'Bonesplitter', src: 'Bonesplitter', kind: 'activate', verb: 'equip', target: 19 },
     'equip=Bonesplitter@19'],
    [{ name: 'Bonesplitter', src: 'Bonesplitter', kind: 'activate', verb: 'equip', srcNum: 7 },
     'equip=Bonesplitter#7'],
    [{ name: 'Balan, Wandering Knight', src: 'Balan, Wandering Knight', kind: 'activate', verb: 'attachall' }, 'attachall=Balan, Wandering Knight'],
    [{ name: 'Colossus Hammer', src: 'Stoneforge Mystic', kind: 'activate', verb: 'sfput' }, 'sfput=Colossus Hammer'],
    [{ name: "Umezawa's Jitte", src: "Umezawa's Jitte", kind: 'activate', verb: 'jittemode', mode: 1 }, 'jittemode=1'],
    // BLINK: the target is part of the decision, and so is the COUNT. Human play offers one blink
    // plus (when the go-off recognizer sees a live loop) a single FINISH plan at the go-off count;
    // both are the same verb on the same target, so a token without the count cannot tell CheckLine
    // which of the two the human picked -- and CheckLine would hand back the other one's index.
    // An EXPLICIT count is emitted even at 1 (commit 58ef6fc7, the phantom-X-dialog fix: a
    // *1-less token used to be re-asked); a MISSING count still encodes bare and the engine
    // reads it as the 0 wildcard, which is how every pre-count saved reference keeps matching --
    // the full 307-ref protocol sweep is green under this encoding. (This expectation asserted
    // the pre-58ef6fc7 format for months; updated 2026-09-10.)
    [{ name: 'Emiel the Blessed', src: 'Emiel the Blessed', kind: 'activate', verb: 'blink', blinkTarget: 42 },
     'blink=Emiel the Blessed@42'],
    [{ name: 'Emiel the Blessed', src: 'Emiel the Blessed', kind: 'activate', verb: 'blink', blinkTarget: 42, blinkCount: 1 },
     'blink=Emiel the Blessed@42*1'],
    [{ name: 'Emiel the Blessed', src: 'Emiel the Blessed', kind: 'activate', verb: 'blink', blinkTarget: 42, blinkCount: 9 },
     'blink=Emiel the Blessed@42*9'],
    // No target (the legacy any-target wildcard) still takes a count tail.
    [{ name: 'Eldrazi Displacer', src: 'Eldrazi Displacer', kind: 'activate', verb: 'blink', blinkCount: 12 },
     'blink=Eldrazi Displacer*12'],
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
// A land you are PLAYING this turn must be a legal host for an Aura queued in the SAME line --
// stampPlanNums is what gives the queued land the m_number the GUI uses as its drop-target key, and
// it is the engine's `enchant_target` for that aura (the hand copy and the permanent share a number).
// Without it the only way to enchant a land you were playing was to commit the line and enchant it
// in the next one (USER, 2026-09-04). Also pins the replacement path, which is where the old
// "only when the plan GREW" stamping silently did nothing: swapping the queued land keeps the plan
// the same length.
function checkQueuedLandIsAuraHost() {
  const hand = [{ name: 'Aether Hub', num: 3 }, { name: 'Conservatory', num: 13 },
                { name: 'Wild Growth', num: 54 }, { name: 'Wild Growth', num: 53 }];
  const dec = { me: { hand, land_drops_left: 1 } };
  const fails = [];

  let p = [];
  p = LB.queueCard(dec, p, 'Aether Hub', 'land');
  p = LB.queueCard(dec, p, 'Wild Growth', 'permanent');
  LB.stampPlanNums(dec, p);
  const land = p.find(e => e.kind === 'land');
  if (!land || land.num !== 3) fails.push(`queued land num ${land && land.num} != 3 (its hand copy)`);
  if (LB.encodeLine(p) !== 'land=Aether Hub;cast=Wild Growth')
    fails.push(`encoded ${LB.encodeLine(p)}`);

  // REPLACING the queued land (one drop, a different land) keeps the plan length the same -- the
  // path the old stamping missed entirely, leaving the land that actually gets played unnumbered.
  let q = [];
  q = LB.queueCard(dec, q, 'Aether Hub', 'land');
  LB.stampPlanNums(dec, q);
  q = LB.queueCard(dec, q, 'Conservatory', 'land');
  LB.stampPlanNums(dec, q);
  const land2 = q.find(e => e.kind === 'land');
  if (!land2 || land2.name !== 'Conservatory' || land2.num !== 13)
    fails.push(`replaced land ${land2 && land2.name}#${land2 && land2.num} != Conservatory#13`);

  // Two copies of one Aura claim DIFFERENT hand numbers, so two queued auras are separable targets.
  let r = [];
  r = LB.queueCard(dec, r, 'Wild Growth', 'permanent');
  r = LB.queueCard(dec, r, 'Wild Growth', 'permanent');
  LB.stampPlanNums(dec, r);
  if (r[0].num === r[1].num) fails.push(`two Wild Growths share num ${r[0].num}`);
  return fails;
}

// STACKED REPEATABLE ACTIVATIONS (2026-09-07). Clicking a no-{T}, no-sac ability K times queues K
// activations in ONE declared line; because an engine Plan holds at most one activation of a given
// source, the line has to commit as K consecutive segments. Pins both halves of that contract --
// the split, and that dropFirstSegment peels exactly one segment per accepted commit (the loop
// advanceTo runs). Also pins the NON-regression: an unrepeatable line partitions as it always did.
function checkStackedActivations() {
  const fails = [];
  const eq = (a, b) => JSON.stringify(a) === JSON.stringify(b);
  const blink = () => ({ name:'Emiel the Blessed', src:'Emiel the Blessed', kind:'activate',
                         verb:'blink', repeatable:true, blinkTarget:42, blinkCount:1 });

  // Three clicks on one outlet -> three segments, one activation each.
  const three = [blink(), blink(), blink()];
  const want = ['blink=Emiel the Blessed@42*1', 'blink=Emiel the Blessed@42*1', 'blink=Emiel the Blessed@42*1'];
  if (!eq(LB.encodeSegments(three), want))
    fails.push(`3 blinks -> ${JSON.stringify(LB.encodeSegments(three))}`);

  // The first segment carries the land + casts; only the EXTRA activations split off.
  const mixed = [{ name:'Yavimaya Coast', kind:'land' },
                 { name:'Peregrine Drake', kind:'nonpermanent' }, blink(), blink()];
  if (!eq(LB.encodeSegments(mixed),
          ['land=Yavimaya Coast;cast=Peregrine Drake;blink=Emiel the Blessed@42*1',
           'blink=Emiel the Blessed@42*1']))
    fails.push(`land+cast+2 blinks -> ${JSON.stringify(LB.encodeSegments(mixed))}`);

  // dropFirstSegment peels ONE segment per call and converges to empty.
  let rest = LB.dropFirstSegment(three);
  if (rest.length !== 2) fails.push(`dropFirstSegment left ${rest.length}, want 2`);
  rest = LB.dropFirstSegment(rest);
  if (rest.length !== 1) fails.push(`dropFirstSegment(2) left ${rest.length}, want 1`);
  rest = LB.dropFirstSegment(rest);
  if (rest.length !== 0) fails.push(`dropFirstSegment(1) left ${rest.length}, want 0`);

  // A NON-repeatable activation never splits: two different sources stay in one segment, which is
  // what the engine's powerset already enumerates together.
  const two = [{ name:'A', src:'A', kind:'activate', verb:'cast' },
               { name:'B', src:'B', kind:'activate', verb:'cast' }];
  if (LB.encodeSegments(two).length !== 1)
    fails.push(`two non-repeatable -> ${JSON.stringify(LB.encodeSegments(two))}`);
  return fails;
}

// LINE MACROS (docs/design/viewer-line-macros.md). The macro and the fused clue are deliberately
// pure QUEUE edits -- they expand into ordinary entries and commit through the existing segment
// chain -- so this file, which drives the real browser queue logic headlessly, is the layer that
// can see them at all. The engine half (the `need=` untap promotion, and that each segment really
// lands) is test/viewer_line_macros_check.py.
function checkLineMacros() {
  const fails = [];
  const eq = (a, b) => JSON.stringify(a) === JSON.stringify(b);
  // A {T} draw outlet (NOT engine-repeatable: it taps, so it genuinely cannot go twice -- until a
  // blink untaps it) and a repeatable blink. This pairing is the whole point: the existing
  // `repeatable` split cannot express it, which is why `defer` had to exist.
  const draw  = () => ({ name:'Mariposa Military Base', src:'Mariposa Military Base',
                         kind:'activate', verb:'cast' });
  const blink = () => ({ name:'Emiel the Blessed', src:'Emiel the Blessed', kind:'activate',
                         verb:'blink', repeatable:true, blinkTarget:10, blinkCount:1 });
  const ITER = 'cast=Mariposa Military Base;blink=Emiel the Blessed@10*1';

  // ---- repeatBlock: N iterations, each its own committed line ----------------------------------
  const x3 = LB.repeatBlock([draw(), blink()], 0, 2, 3);
  if (x3.length !== 6) fails.push(`repeatBlock x3 made ${x3.length} entries, want 6`);
  if (!eq(LB.encodeSegments(x3), [ITER, ITER, ITER]))
    fails.push(`x3 -> ${JSON.stringify(LB.encodeSegments(x3))}`);
  // Only the FIRST entry of each repetition is deferred: marking them all would split one iteration
  // into one segment per action, which is a different (and wrong) line.
  if (!eq(x3.map(p => !!p.defer), [false, false, true, false, true, false]))
    fails.push(`x3 defer flags -> ${JSON.stringify(x3.map(p => !!p.defer))}`);

  // n<=1 is a no-op, and an empty block cannot be repeated.
  if (LB.repeatBlock([draw(), blink()], 0, 2, 1).length !== 2) fails.push('x1 was not a no-op');
  if (LB.repeatBlock([draw()], 0, 0, 5).length !== 1) fails.push('empty block was repeated');

  // Entries are COPIED, not shared: dropFirstSegment peels by object IDENTITY, so a shared object
  // would make two iterations vanish together. Peel all three and check it converges.
  let rest = x3, peeled = 0;
  while (rest.length && peeled < 10) { rest = LB.dropFirstSegment(rest); peeled++; }
  if (peeled !== 3) fails.push(`x3 peeled in ${peeled} segments, want 3`);

  // ---- the deferred entry carries its BLOCK, not just itself -----------------------------------
  // Three entries where the middle one is deferred -> [a], [b, c]. If `defer` only split off its own
  // entry, the third would land back in the first segment and the iteration would be torn apart.
  const trio = [draw(), Object.assign(blink(), { defer:true }), draw()];
  if (LB.encodeSegments(trio).length !== 2)
    fails.push(`deferred-middle trio -> ${JSON.stringify(LB.encodeSegments(trio))}`);

  // ---- a plan with NO deferred entry partitions exactly as it always did ------------------------
  // The byte-identity claim for every recorded line, asserted directly rather than inferred.
  const plain = [draw(), blink(), blink()];
  if (!eq(LB.encodeSegments(plain),
          ['cast=Mariposa Military Base;blink=Emiel the Blessed@10*1',
           'blink=Emiel the Blessed@10*1']))
    fails.push(`undeferred plan -> ${JSON.stringify(LB.encodeSegments(plain))}`);

  // ---- fused "investigate & crack" --------------------------------------------------------------
  const fused = LB.fusedInvestigateEntries('Conservatory', { verb:'cast', mode:null });
  if (!eq(LB.encodeSegments(fused), ['cast=Conservatory', 'cast=Clue Token']))
    fails.push(`fused clue -> ${JSON.stringify(LB.encodeSegments(fused))}`);
  // Removing EITHER half removes both -- half a fused gesture is a different play.
  if (LB.removeFusedAt(fused, 0).length !== 0) fails.push('removing the investigate left the crack');
  if (LB.removeFusedAt(fused, 1).length !== 0) fails.push('removing the crack left the investigate');
  // ...but an ordinary entry beside a fused pair is untouched by either removal.
  const withOther = [draw()].concat(fused);
  if (LB.removeFusedAt(withOther, 1).length !== 1)
    fails.push('removing a fused half also removed an unrelated entry');
  if (LB.removeFusedAt(withOther, 0).length !== 2)
    fails.push('removing an ordinary entry disturbed the fused pair');

  // ---- REPEAT MUST NOT FLATTEN A BLOCK'S OWN SEGMENT BOUNDARY (the EDF loop bug, 2026-09-11) ----
  // USER, mid-game: "it would be nice if loops like kitchen activate -> untap were possible to
  // repeat ... Currently that bugs out when I try it."
  //
  // The block the user repeats is the Kitchen loop, which with clue fusion ON (the default) is
  // queued as a FUSED investigate + its DEFERRED crack + a blink. repeatBlock used to stamp
  // `defer:false` on every non-first entry of each copy, which erased that deferral and collapsed
  // iterations 2..N into ONE line -- `cast=Kitchen;cast=Clue Token;blink=...`, the exact one-line
  // form test/scenarios/edf_fused_clue_needs_two_lines.json pins as ILLEGAL, because the Clue does
  // not exist while the Investigate is being committed. Iteration 1 played, iteration 2 was
  // rejected, and the game picked up a reject it can never be saved as a clean reference with.
  const kitchen = () => LB.fusedInvestigateEntries('Kitchen', { verb:'cast', mode:null });
  const dis = () => ({ name:'Eldrazi Displacer', src:'Eldrazi Displacer', kind:'activate',
                       verb:'blink', repeatable:true, blinkTarget:42, blinkCount:1 });
  const loopBlk = kitchen().concat([dis()]);
  const KITCH = 'cast=Kitchen', CRACK = 'cast=Clue Token;blink=Eldrazi Displacer@42*1';
  const want3 = [KITCH, CRACK, KITCH, CRACK, KITCH, CRACK];
  const got3 = LB.encodeSegments(LB.repeatBlock(loopBlk, 0, loopBlk.length, 3));
  if (!eq(got3, want3)) fails.push(`repeat over a FUSED block flattened its deferral -> ${JSON.stringify(got3)}`);
  // ...and the flat (unfused) block is unchanged by the same fix: no internal boundary, one line
  // per iteration, exactly as before.
  const flatBlk = [{ name:'Kitchen', src:'Kitchen', kind:'activate', verb:'cast' }, dis()];
  if (!eq(LB.encodeSegments(LB.repeatBlock(flatBlk, 0, 2, 3)),
          ['cast=Kitchen;blink=Eldrazi Displacer@42*1', 'cast=Kitchen;blink=Eldrazi Displacer@42*1',
           'cast=Kitchen;blink=Eldrazi Displacer@42*1']))
    fails.push('the defer fix disturbed an unfused repeat');

  // ---- LOOP: repeat the last K COMMITTED segments ------------------------------------------------
  // Same expansion, read off committed history instead of the queue -- which is the only way to ask
  // for the loop AFTER playing it once, when the queue is empty.
  const committed = [[kitchen()[0]], [kitchen()[1], dis()]];   // the two lines one iteration commits as
  if (!eq(LB.encodeSegments(LB.loopBlock(committed, 3)), want3))
    fails.push(`loopBlock x3 -> ${JSON.stringify(LB.encodeSegments(LB.loopBlock(committed, 3)))}`);
  if (!eq(LB.encodeSegments(LB.loopBlock(committed, 1)), [KITCH, CRACK]))
    fails.push('loopBlock x1 is not one plain iteration');
  if (LB.loopBlock([], 5).length) fails.push('an empty committed tail produced a loop');
  if (LB.loopBlock(committed, 0).length) fails.push('n=0 produced a loop');
  // Every segment head is deferred, INCLUDING the block's first: that is what makes the expansion
  // commit as k*n separate lines rather than fusing with whatever the queue already holds.
  const lb3 = LB.loopBlock(committed, 3);
  if (!eq(lb3.map(p => !!p.defer), [true, true, false, true, true, false, true, true, false]))
    fails.push(`loopBlock defer flags -> ${JSON.stringify(lb3.map(p => !!p.defer))}`);
  // Entries are COPIES, not the committed records themselves -- expanding a loop must not mutate
  // the history it was read from (dropFirstSegment peels by identity and would eat the record).
  if (lb3.some(p => committed[0].indexOf(p) >= 0 || committed[1].indexOf(p) >= 0))
    fails.push('loopBlock returned the committed records themselves, not copies');
  if (committed[0][0].defer === true) fails.push('loopBlock mutated the committed record it read');
  // ...and it peels cleanly: k*n segments, each individually committable.
  let lrest = lb3, lpeeled = 0;
  while (lrest.length && lpeeled < 20) { lrest = LB.dropFirstSegment(lrest); lpeeled++; }
  if (lpeeled !== 6) fails.push(`loopBlock x3 peeled in ${lpeeled} segments, want 6`);
  // A MANUAL TAP rides through verbatim: the loop repeats a line that already committed once, and
  // the loop itself is usually what untaps that land again. (The queue-time Repeat refuses a
  // pre-tap because there the block has never been played -- see segmentLoopable's note.)
  const tapSeg = [{ kind:'pretap', name:'Kitchen', num:29, color:'U' },
                  { name:'Kitchen', src:'Kitchen', kind:'activate', verb:'cast' }];
  if (!eq(LB.encodeSegments(LB.loopBlock([tapSeg], 2)),
          ['tap=Kitchen#29:U;cast=Kitchen', 'tap=Kitchen#29:U;cast=Kitchen']))
    fails.push(`a looped pre-tap did not survive -> ${JSON.stringify(LB.encodeSegments(LB.loopBlock([tapSeg], 2)))}`);
  // ...and a pre-tap keeps its BOARD m_number, while a hand cast loses its stamped hand m_number
  // (that one belongs to the frame it was queued on; stampPlanNums re-stamps it on the new frame).
  const handSeg = [{ name:'Training Grounds', kind:'permanent', num:17 }];
  if (LB.loopBlock([handSeg], 2).some(p => p.num != null))
    fails.push('a looped hand cast kept its stale hand m_number');
  if (LB.loopBlock([tapSeg], 2)[0].num !== 29) fails.push('a looped pre-tap lost its board m_number');
  // A once-per-turn resource is refused rather than offered and then rejected.
  if (LB.segmentLoopable([{ kind:'land', name:'Forest' }])) fails.push('a land drop is loopable');
  if (LB.segmentLoopable([{ kind:'le', name:'Mountain' }])) fails.push("a Land's Edge discard is loopable");
  if (LB.segmentLoopable([])) fails.push('an empty segment is loopable');
  if (!LB.segmentLoopable(tapSeg)) fails.push('a hand-paid segment is not loopable');

  // ---- the need= token --------------------------------------------------------------------------
  const pips = p => ({ 'Eldrazi Displacer':'C', 'Cloud of Faeries':'U', 'Emiel the Blessed':'' }[p.name] || '');
  if (LB.untapNeedToken([{ name:'Eldrazi Displacer' }, { name:'Cloud of Faeries' }], pips) !== 'need=UC')
    fails.push(`need token -> ${LB.untapNeedToken([{ name:'Eldrazi Displacer' }, { name:'Cloud of Faeries' }], pips)}`);
  // A FIXED alphabet order, so the same continuation always produces the same string -- a saved
  // reference has to replay it byte-for-byte.
  if (LB.untapNeedToken([{ name:'Cloud of Faeries' }, { name:'Eldrazi Displacer' }], pips) !== 'need=UC')
    fails.push('need token is order-dependent');
  // No pips (Emiel's blink is a bare {3}) -> no token -> the untap pick is byte-identical to before.
  if (LB.untapNeedToken([{ name:'Emiel the Blessed' }], pips) !== '')
    fails.push('a pip-less continuation still emitted a need= token');
  if (LB.untapNeedToken([], pips) !== '') fails.push('an empty continuation emitted a need= token');
  // Duplicates collapse: a demand is a SET, not a count.
  if (LB.untapNeedToken([{ name:'Eldrazi Displacer' }, { name:'Eldrazi Displacer' }], pips) !== 'need=C')
    fails.push('repeated demand did not collapse to a set');
  return fails;
}

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
  const hostFails = checkQueuedLandIsAuraHost();
  hostFails.forEach(m => console.log(`  FAIL  queued-land aura host: ${m}`));
  console.log(`Viewer queued-land aura host: ${hostFails.length ? 'WRONG' : 'a land being played is a legal same-line Aura host'} ` +
              `(${hostFails.length} FAIL)`);
  const mixFails = checkMixedSubVariantWalk();
  mixFails.forEach(m => console.log(`  FAIL  mixed-sub walk: ${m}`));
  console.log(`Viewer mixed-sub walk: ${mixFails.length ? 'WRONG' : "a variant lacking the dimension is offered as '—', not dumped flat"} ` +
              `(${mixFails.length} FAIL)`);
  const stackFails = checkStackedActivations();
  stackFails.forEach(m => console.log(`  FAIL  stacked activation: ${m}`));
  console.log(`Viewer stacked activations: ${stackFails.length ? 'WRONG' : 'K clicks commit as K segments, one per accepted commit'} ` +
              `(${stackFails.length} FAIL)`);
  const macroFails = checkLineMacros();
  macroFails.forEach(m => console.log(`  FAIL  line macro: ${m}`));
  console.log(`Viewer line macros: ${macroFails.length ? 'WRONG' : 'repeat xN and investigate&crack expand into ordinary segments'} ` +
              `(${macroFails.length} FAIL)`);
  return (fail + dimFails.length + landFails.length + sacFails.length + verbFails.length
          + mixFails.length + hostFails.length + stackFails.length + macroFails.length) ? 1 : 0;
}

process.exit(main());
