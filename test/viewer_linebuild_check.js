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

// Collect claude_s*_gi*.json RECURSIVELY under references/, excluding the knowingly-beatable
// suboptimal/ and optimal/ sets BY NAME (they are targets, not the verified benchmark, and would
// permanently drift against a gate).
//
// RECURSIVE, not one level, and the distinction has bitten twice. CLAUDE.md's archive convention
// files a superseded list's references at references/<Deck>/v<N>-<slug>/ -- a reference belongs to
// the list it was PLAYED on -- which is the SAME depth the old one-level walk used to exclude the
// suboptimal sets. So promoting a list silently dropped that list's whole reference set out of this
// gate: 78 games across 7 folders were checked by nothing until 2026-10-02 (USER: "We should check
// all of the references for all versions automatically" / "we want to leverage everything the user
// has created to ensure the engine is solid"). viewer_protocol_check.py and viewer_validate_check.js
// were fixed then; THIS walk was missed, and the Fungus v2 promotion on 2026-10-16 archived ten more
// hand-played games straight back out of it. A depth test standing in for a semantic one is the bug;
// excluding by NAME cannot go stale when someone files a reference deeper than a glob remembered.
//
// Nothing here needs the decklist: checkReference reads the reference's OWN recorded frames (hand,
// plans, chosen index), so widening the walk is purely additive.
function collectRefs() {
  const out = [];
  const walk = (dir) => {
    for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
      if (e.isDirectory()) {
        if (e.name === 'suboptimal' || e.name === 'optimal') continue;
        walk(path.join(dir, e.name));
      } else if (/^claude_s.*_gi.*\.json$/.test(e.name)) {
        out.push(path.join(dir, e.name));
      }
    }
  };
  if (!fs.existsSync(REFROOT)) return out;
  walk(REFROOT);
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
    [{ name: 'Krenko, Mob Boss', src: 'Krenko, Mob Boss', kind: 'activate' }, 'act=Krenko, Mob Boss'],
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
    // EQUIP-ALL-FREE names the HOST's m_number, not a card name -- the one verb in NUM_VERBS. Two
    // Kor Duelists in play are genuinely different hosts, so a by-name token could not say which
    // (the same reason `equip=` carries `@<host>`); and the engine action's card_name IS the host's
    // name, so a name-valued token would also be indistinguishable from a cast of a same-named card.
    [{ name: 'Kor Duelist', src: 'Kor Duelist', kind: 'activate', verb: 'equipallfree', hostNum: 19 },
     'equipallfree=19'],
    // No hostNum => 0, which the engine reads as its "any host" wildcard rather than as a
    // malformed token. That is what keeps a hand-typed line and a scenario fixture usable.
    [{ name: 'Kor Duelist', src: 'Kor Duelist', kind: 'activate', verb: 'equipallfree' },
     'equipallfree=0'],
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
    // ACTIVATED UNTAPPER (Crab Umbra / Galvanic's grant / Staff / Seedcradle Witch): the equip
    // '#source@target' shape -- two Staffs share a name, and the target is the decision.
    [{ name: 'Staff of Domination', src: 'Staff of Domination', kind: 'activate', verb: 'untapact',
       untapSrc: 7, untapTarget: 42 }, 'untapact=Staff of Domination#7@42'],
    [{ name: 'Crab Umbra', src: 'Crab Umbra', kind: 'activate', verb: 'untapact', untapTarget: 42 },
     'untapact=Crab Umbra@42'],
    // The closed-form untap LOOP (chunk C2b): the same shape under its own verb.
    [{ name: 'Crab Umbra', src: 'Crab Umbra', kind: 'activate', verb: 'untapact', untapTarget: 42,
       untapLoop: true }, 'untaploop=Crab Umbra@42'],
    [{ name: 'Staff of Domination', src: 'Staff of Domination', kind: 'activate', verb: 'tapdraw' },
     'tapdraw=Staff of Domination'],
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

// KICKER (CR 702.33 -- Saproling Migration). Two layers, and the SECOND is the one that matters:
//   * the TOKEN -- a kicked cast emits `cast=<name>` AND `kicked=<name>`, since kicking is a real
//     cast of that hand card and the extra token only says which MODE (encodeLine's own comment).
//   * the GESTURE -- LB.toggleKicker, the plan edit behind index.html's "⊕ kicked" badge.
// The gesture is here because of how the kicker shipped: engine, chooser, protocol emitter and the
// `kicked=` token were all correct and the feature was still UNUSABLE, because the GUI had no branch
// for it (USER 2026-10-16: "Seed 5 I wasn't given the option to Kicker"). Unit tests, scenarios,
// smoke 119/119, regression 166/166 and the whole reference corpus passed throughout -- none of them
// drives the GUI. So the transition was moved into linebuild.js to BE drivable, and this is the gate.
// Synthetic and binary-free: no saved reference holds a kicked cast (none could be built until now).
function checkKickerToken() {
  const fails = [];
  const K = { name: 'Saproling Migration', kind: 'nonpermanent' };
  const enc = (plan) => LB.encodeLine(plan);

  // ---- the token ----------------------------------------------------------------------------
  // Unkicked: no stray token. This is the "absent means do not kick" half of the contract -- the
  // engine compares the kicked set unconditionally, so a spurious token is a different line.
  if (enc([{ ...K }]) !== 'cast=Saproling Migration')
    fails.push(`an UNKICKED cast encoded "${enc([{ ...K }])}", expected "cast=Saproling Migration"`);
  if (enc([{ ...K, kicked: true }]) !== 'cast=Saproling Migration;kicked=Saproling Migration')
    fails.push(`a KICKED cast encoded "${enc([{ ...K, kicked: true }])}", expected both tokens`);
  // Two copies, one kicked: the kicked set is a MULTISET beside the casts, so one `kicked=` against
  // two `cast=` is exactly "cast both, kick one" -- the line a count-token design would have lost.
  const mixed = enc([{ ...K }, { ...K, kicked: true }]);
  if (mixed !== 'cast=Saproling Migration;cast=Saproling Migration;kicked=Saproling Migration')
    fails.push(`cast+kicked of two copies encoded "${mixed}"`);
  // Both kicked -> two tokens, and the land still leads the line.
  const both = enc([{ name: 'Forest', kind: 'land' }, { ...K, kicked: true }, { ...K, kicked: true }]);
  if (both !== 'land=Forest;cast=Saproling Migration;cast=Saproling Migration;'
             + 'kicked=Saproling Migration;kicked=Saproling Migration')
    fails.push(`land + two kicked casts encoded "${both}"`);

  // ---- the gesture --------------------------------------------------------------------------
  // From EMPTY one click queues a kicked cast (the badge is the only route to the mode, so it must
  // work without queueing the card first).
  let r = LB.toggleKicker([], K.name, K.kind);
  if (r.action !== 'queued' || r.plan.length !== 1 || !r.plan[0].kicked)
    fails.push(`toggle from empty gave action=${r.action}, plan=${JSON.stringify(r.plan)}`);
  // An already-queued cast is PROMOTED IN PLACE, not duplicated. The duplicate is the real hazard:
  // both modes share a hand slot, so a second entry builds `cast=X;cast=X;kicked=X`, which matches
  // no plan with one copy in hand and dead-ends the commit.
  r = LB.toggleKicker([{ ...K }], K.name, K.kind);
  if (r.action !== 'kicked' || r.plan.length !== 1 || !r.plan[0].kicked)
    fails.push(`toggle over a queued cast gave action=${r.action}, plan=${JSON.stringify(r.plan)}`);
  // Clicking again TAKES THE KICKER BACK and leaves the cast queued (the badge toggles the kicker,
  // not the cast -- the chip's ✕ removes the cast).
  r = LB.toggleKicker(r.plan, K.name, K.kind);
  if (r.action !== 'unkicked' || r.plan.length !== 1 || r.plan[0].kicked)
    fails.push(`second toggle gave action=${r.action}, plan=${JSON.stringify(r.plan)}`);
  if (enc(r.plan) !== 'cast=Saproling Migration')
    fails.push(`a taken-back kicker still encoded "${enc(r.plan)}"`);
  // The LAST unkicked copy is the one promoted, so clicking the badge twice with two copies queued
  // kicks both rather than fighting over one.
  r = LB.toggleKicker([{ ...K }, { ...K }], K.name, K.kind);
  if (!(r.plan[1] || {}).kicked || (r.plan[0] || {}).kicked)
    fails.push(`with two queued casts the promoted entry was ${JSON.stringify(r.plan)}`);
  r = LB.toggleKicker(r.plan, K.name, K.kind);
  if (!r.plan.every(p => p.kicked) || r.plan.length !== 2)
    fails.push(`two toggles over two casts gave ${JSON.stringify(r.plan)}`);
  // ...and the next click WRAPS to none rather than demoting one. Demoting would leave the FOLLOWING
  // click re-promoting that same entry, so with two copies the plan oscillated between 2 and 1 kicked
  // and an all-kicked line could never get back to unkicked (this assertion caught exactly that in
  // the first cut of toggleKicker). The count is the gesture: 0 -> 1 -> 2 -> 0.
  r = LB.toggleKicker(r.plan, K.name, K.kind);
  if (r.action !== 'unkicked' || r.plan.length !== 2 || r.plan.some(p => p.kicked))
    fails.push(`the wrap-around click gave action=${r.action}, plan=${JSON.stringify(r.plan)}`);
  // The cycle is closed, so clicking on round-trips forever without ever growing the plan: the plan
  // NEVER exceeds the casts the human queued, which is the safety property, not a convenience.
  for (let i = 0; i < 7; i++) r = LB.toggleKicker(r.plan, K.name, K.kind);
  if (r.plan.length !== 2) fails.push(`nine clicks grew the plan to ${JSON.stringify(r.plan)}`);
  // A BOARD ACTIVATION of the same name is never a kicked cast -- it must not be promoted, and the
  // badge must fall through to queueing a cast instead.
  r = LB.toggleKicker([{ name: K.name, src: K.name, kind: 'activate' }], K.name, K.kind);
  if (r.action !== 'queued' || r.plan.length !== 2 || !r.plan[1].kicked || r.plan[0].kicked)
    fails.push(`a same-named activation was treated as a cast: ${JSON.stringify(r.plan)}`);
  // ...as is a LAND of the same name (a nonsensical pairing, but isSpecCast is what rules it out and
  // this pins that it is doing the work).
  r = LB.toggleKicker([{ name: K.name, kind: 'land' }], K.name, K.kind);
  if (r.action !== 'queued' || r.plan.length !== 2)
    fails.push(`a same-named land was treated as a cast: ${JSON.stringify(r.plan)}`);
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
// MANUAL TAP/PAY token (docs/design/viewer-manual-tap-pay.md). The `+<AURA>` suffix carries the
// colour the human chose for each "one mana of any color" land Aura on the host. Pinned HERE because
// this file owns the encoder and nothing else can see the string: the engine parses whatever it is
// handed, so an encoder that stopped writing the suffix would silently restore the `wild` credit
// this feature exists to remove -- and every reference above would keep passing, since none has one.
function checkPreTapToken() {
  const fails = [];
  const tok = p => LB.encodeLine([Object.assign({ kind: 'pretap' }, p)]);
  const cases = [
    // [entry, expected] -- the no-aura form is FIRST because it is the back-compat guarantee:
    // every line recorded before this feature must still encode byte-for-byte as it did.
    [{ name: 'Conservatory', num: 7, color: 'W' }, 'tap=Conservatory#7:W'],
    [{ name: 'Conservatory', num: 7, color: 'W', auraColors: [] }, 'tap=Conservatory#7:W'],
    [{ name: 'Brushland', num: 6, color: 'W', auraColors: ['U'] }, 'tap=Brushland#6:W+U'],
    [{ name: 'Brushland', num: 6, color: 'C', auraColors: ['R', 'G'] }, 'tap=Brushland#6:C+R+G'],
    // A wildcard copy id still works with the suffix (a hand-written line / a fixture).
    [{ name: 'Aether Hub', num: 0, color: 'C', auraColors: ['R'] }, 'tap=Aether Hub#0:C+R'],
    // ARTIFACT / CREATURE sources (USER 2026-10-22): a filter's fed output is a MULTI-LETTER face,
    // written verbatim (`tap_fed`), and the display-only fields a queued entry carries (`sac`, `fed`,
    // `note`) never reach the token.
    [{ name: 'Izzet Signet', num: 20, color: 'UR' }, 'tap=Izzet Signet#20:UR'],
    [{ name: 'Cascade Bluffs', num: 5, color: 'UU', fed: true, note: 'x' }, 'tap=Cascade Bluffs#5:UU'],
    [{ name: 'Ferrous Lake', num: 6, color: 'UR', auraColors: ['G'] }, 'tap=Ferrous Lake#6:UR+G'],
    [{ name: 'Generator Servant', num: 10, color: 'C', sac: true, note: 'haste' }, 'tap=Generator Servant#10:C'],
  ];
  for (const [entry, want] of cases) {
    const got = tok(entry);
    if (got !== want) { fails.push(`${JSON.stringify(entry)} -> ${got}, expected ${want}`); }
  }
  return fails;
}

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
  const ITER = 'act=Mariposa Military Base;blink=Emiel the Blessed@10*1';

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
          ['act=Mariposa Military Base;blink=Emiel the Blessed@10*1',
           'blink=Emiel the Blessed@10*1']))
    fails.push(`undeferred plan -> ${JSON.stringify(LB.encodeSegments(plain))}`);

  // ---- fused "investigate & crack" --------------------------------------------------------------
  const fused = LB.fusedInvestigateEntries('Conservatory', { verb:'cast', mode:null });
  if (!eq(LB.encodeSegments(fused), ['act=Conservatory', 'act=Clue Token']))
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
  // iterations 2..N into ONE line -- `act=Kitchen;act=Clue Token;blink=...`, the exact one-line
  // form test/scenarios/edf_fused_clue_needs_two_lines.json pins as ILLEGAL, because the Clue does
  // not exist while the Investigate is being committed. Iteration 1 played, iteration 2 was
  // rejected, and the game picked up a reject it can never be saved as a clean reference with.
  const kitchen = () => LB.fusedInvestigateEntries('Kitchen', { verb:'cast', mode:null });
  const dis = () => ({ name:'Eldrazi Displacer', src:'Eldrazi Displacer', kind:'activate',
                       verb:'blink', repeatable:true, blinkTarget:42, blinkCount:1 });
  const loopBlk = kitchen().concat([dis()]);
  const KITCH = 'act=Kitchen', CRACK = 'act=Clue Token;blink=Eldrazi Displacer@42*1';
  const want3 = [KITCH, CRACK, KITCH, CRACK, KITCH, CRACK];
  const got3 = LB.encodeSegments(LB.repeatBlock(loopBlk, 0, loopBlk.length, 3));
  if (!eq(got3, want3)) fails.push(`repeat over a FUSED block flattened its deferral -> ${JSON.stringify(got3)}`);
  // ...and the flat (unfused) block is unchanged by the same fix: no internal boundary, one line
  // per iteration, exactly as before.
  const flatBlk = [{ name:'Kitchen', src:'Kitchen', kind:'activate', verb:'cast' }, dis()];
  if (!eq(LB.encodeSegments(LB.repeatBlock(flatBlk, 0, 2, 3)),
          ['act=Kitchen;blink=Eldrazi Displacer@42*1', 'act=Kitchen;blink=Eldrazi Displacer@42*1',
           'act=Kitchen;blink=Eldrazi Displacer@42*1']))
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
          ['tap=Kitchen#29:U;act=Kitchen', 'tap=Kitchen#29:U;act=Kitchen']))
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

// THE LIBRARY TOP AS A LAND SOURCE (Courser of Kruphix, 2026-10-15; engine Plan::land_from_top). The
// revealed top is published as me.library_top {name, number, playable_land}; clicking it queues a land
// entry with `fromTop`, which encodes as `landtop=` (never `land=`, which names a HAND copy and is what
// every saved reference carries). Pins: the encoding, the (name, SOURCE) toggle -- a top Forest and a
// hand Forest are different plays -- the cap of one, the stamped number (a land Aura can host on it),
// a non-playable top refusing, and the after-the-casts modifier on a decision that offers land-after.
function checkLibraryTopLand() {
  const hand = [{ name: 'Forest', num: 31 }, { name: 'Lava Spike', num: 40 }];
  const top  = { name: 'Forest', number: 7, kind: 'land', playable_land: true };
  const dec  = { me: { hand, land_drops_left: 1, library_top: top } };
  const fails = [];
  const eq = (a, b) => JSON.stringify(a) === JSON.stringify(b);

  let p = [];
  p = LB.queueCard(dec, p, 'Forest', 'land', true);
  p = LB.queueCard(dec, p, 'Lava Spike', 'nonpermanent');
  LB.stampPlanNums(dec, p);
  if (LB.encodeLine(p) !== 'landtop=Forest;cast=Lava Spike') fails.push(`top land encodes ${LB.encodeLine(p)}`);
  const tl = p.find(e => e.kind === 'land');
  if (!tl || tl.num !== 7) fails.push(`top land stamped num ${tl && tl.num} != 7 (library_top.number)`);

  // A hand Forest REPLACES the queued top Forest at one drop (different source = a different play),
  // and clicking the top again toggles it OFF (same name, same source).
  let q = [];
  q = LB.queueCard(dec, q, 'Forest', 'land', true);
  q = LB.queueCard(dec, q, 'Forest', 'land');
  if (LB.encodeLine(q) !== 'land=Forest') fails.push(`hand Forest after top Forest -> ${LB.encodeLine(q)}`);
  let r = [];
  r = LB.queueCard(dec, r, 'Forest', 'land', true);
  r = LB.queueCard(dec, r, 'Forest', 'land', true);
  if (r.length) fails.push(`top Forest twice should toggle off -> ${JSON.stringify(r)}`);

  // Two drops: the top Forest AND the hand Forest both queue (two different cards), as two segments.
  const two = { me: { hand, land_drops_left: 2, library_top: top } };
  let t = [];
  t = LB.queueCard(two, t, 'Forest', 'land', true);
  t = LB.queueCard(two, t, 'Forest', 'land');
  if (!eq(LB.encodeSegments(t), ['landtop=Forest', 'land=Forest']))
    fails.push(`two drops, top + hand Forest -> ${JSON.stringify(LB.encodeSegments(t))}`);

  // A top that is not a playable land (no play-from-top permanent, or a spell on top) never queues.
  const dead = { me: { hand, land_drops_left: 1, library_top: { name: 'Forest', number: 7, playable_land: false } } };
  let u = [];
  u = LB.queueCard(dead, u, 'Forest', 'land', true);
  if (u.length) fails.push(`non-playable top queued -> ${JSON.stringify(u)}`);

  // After a cast, on a decision that offers land-after lines: the top land carries the timing modifier.
  const after = { me: { hand, land_drops_left: 1, library_top: top }, plans: [{ land_after_casts: true }] };
  let v = [];
  v = LB.queueCard(after, v, 'Lava Spike', 'nonpermanent');
  v = LB.queueCard(after, v, 'Forest', 'land', true);
  if (LB.encodeLine(v) !== 'landtop=Forest;landat=all;cast=Lava Spike')
    fails.push(`top land after a cast -> ${LB.encodeLine(v)}`);
  return fails;
}

// THE LAND'S POSITION IN THE CAST SEQUENCE (engine Plan::land_slot, 2026-10-15). On a decision that
// offers positioned lines (a plan with land_after_casts or land_slot), a land queued AFTER a cast takes
// its position from the queue: after every cast -> `landafter=` (hand) / `landat=all` (top); BETWEEN two
// casts -> `landat=<k>`, k = the casts queued before it in click order. A land queued first, or on a
// decision that offers no positioned line, encodes `land=` exactly as it always did.
function checkLandPosition() {
  const hand = [{ name: 'Forest', num: 31 }, { name: 'Khalni Heart Expedition', num: 40 },
                { name: 'Rampant Growth', num: 41 }];
  const offers = { me: { hand, land_drops_left: 1 }, plans: [{ land_slot: 1 }] };
  const plain  = { me: { hand, land_drops_left: 1 }, plans: [{}] };
  const fails = [];

  // Khalni, Forest, Rampant Growth -> the Forest sits after cast #1.
  let p = [];
  p = LB.queueCard(offers, p, 'Khalni Heart Expedition', 'permanent');
  p = LB.queueCard(offers, p, 'Forest', 'land');
  p = LB.queueCard(offers, p, 'Rampant Growth', 'nonpermanent');
  if (LB.encodeLine(p) !== 'land=Forest;landat=1;cast=Khalni Heart Expedition;cast=Rampant Growth')
    fails.push(`mid position -> ${LB.encodeLine(p)}`);
  // Khalni, Rampant Growth, Forest -> after every cast: `landafter=` (the legacy verb).
  let q = [];
  q = LB.queueCard(offers, q, 'Khalni Heart Expedition', 'permanent');
  q = LB.queueCard(offers, q, 'Rampant Growth', 'nonpermanent');
  q = LB.queueCard(offers, q, 'Forest', 'land');
  if (LB.encodeLine(q) !== 'landafter=Forest;cast=Khalni Heart Expedition;cast=Rampant Growth')
    fails.push(`after all -> ${LB.encodeLine(q)}`);
  // Forest first -> land-first, no position token.
  let r = [];
  r = LB.queueCard(offers, r, 'Forest', 'land');
  r = LB.queueCard(offers, r, 'Khalni Heart Expedition', 'permanent');
  if (LB.encodeLine(r) !== 'land=Forest;cast=Khalni Heart Expedition') fails.push(`land first -> ${LB.encodeLine(r)}`);
  // No positioned line offered (every deck without a landfall card): the queue position is STILL the
  // timing (USER 2026-10-22: "The viewer should use exactly the order I give."). It used to be dropped
  // here -- `land=` and the engine played the land first; the engine now validates a legal position
  // against the land-first plan and realises it through the "**" pin.
  let t = [];
  t = LB.queueCard(plain, t, 'Khalni Heart Expedition', 'permanent');
  t = LB.queueCard(plain, t, 'Forest', 'land');
  t = LB.queueCard(plain, t, 'Rampant Growth', 'nonpermanent');
  if (LB.encodeLine(t) !== 'land=Forest;landat=1;cast=Khalni Heart Expedition;cast=Rampant Growth')
    fails.push(`no positioned line offered, land between -> ${LB.encodeLine(t)}`);
  let t2 = [];
  t2 = LB.queueCard(plain, t2, 'Rampant Growth', 'nonpermanent');
  t2 = LB.queueCard(plain, t2, 'Forest', 'land');
  if (LB.encodeLine(t2) !== 'landafter=Forest;cast=Rampant Growth')
    fails.push(`no positioned line offered, land after -> ${LB.encodeLine(t2)}`);
  let t3 = [];
  t3 = LB.queueCard(plain, t3, 'Forest', 'land');
  t3 = LB.queueCard(plain, t3, 'Rampant Growth', 'nonpermanent');
  if (LB.encodeLine(t3) !== 'land=Forest;cast=Rampant Growth')
    fails.push(`no positioned line offered, land first -> ${LB.encodeLine(t3)}`);
  // A library-TOP land in the middle carries the same modifier.
  const top = { me: { hand, land_drops_left: 1, library_top: { name: 'Forest', number: 7, playable_land: true } },
                plans: [{ land_slot: 1 }] };
  let u = [];
  u = LB.queueCard(top, u, 'Khalni Heart Expedition', 'permanent');
  u = LB.queueCard(top, u, 'Forest', 'land', true);
  u = LB.queueCard(top, u, 'Rampant Growth', 'nonpermanent');
  if (LB.encodeLine(u) !== 'landtop=Forest;landat=1;cast=Khalni Heart Expedition;cast=Rampant Growth')
    fails.push(`top land mid position -> ${LB.encodeLine(u)}`);
  return fails;
}

// THE KAROO DROP'S POSITION (engine Plan::land_slot <= -2, published as `karoo_before`, 2026-10-22). On
// a decision that offers an earlier Karoo point, a queued Karoo's QUEUE POSITION is its timing: with casts
// still to come it encodes `landat=<k>` (k = the casts before it, 0 = before every cast); after every cast
// it is the plain `land=` (the engine's after-the-casts default). A decision with no `karoo_before` plan
// (every pre-2026-10-22 recording, every non-Karoo land) encodes exactly as before.
function checkKarooPosition() {
  const hand = [{ name: 'Selesnya Sanctuary', num: 40 }, { name: 'Rest for the Weary', num: 50 },
                { name: 'Llanowar Elves', num: 51 }];
  const offers = { me: { hand, land_drops_left: 1 },
                   plans: [{ land: 'Selesnya Sanctuary' }, { land: 'Selesnya Sanctuary', karoo_before: 1 }] };
  const plain  = { me: { hand, land_drops_left: 1 }, plans: [{ land: 'Selesnya Sanctuary' }] };
  const fails = [];
  // Sanctuary first, then Rest -> before cast #1: landat=0.
  let a = [];
  a = LB.queueCard(offers, a, 'Selesnya Sanctuary', 'land');
  a = LB.queueCard(offers, a, 'Rest for the Weary', 'nonpermanent');
  if (LB.encodeLine(a) !== 'land=Selesnya Sanctuary;landat=0;cast=Rest for the Weary')
    fails.push(`karoo first -> ${LB.encodeLine(a)}`);
  // Elves, Sanctuary, Rest -> between the casts: landat=1.
  let b = [];
  b = LB.queueCard(offers, b, 'Llanowar Elves', 'permanent');
  b = LB.queueCard(offers, b, 'Selesnya Sanctuary', 'land');
  b = LB.queueCard(offers, b, 'Rest for the Weary', 'nonpermanent');
  if (LB.encodeLine(b) !== 'land=Selesnya Sanctuary;landat=1;cast=Llanowar Elves;cast=Rest for the Weary')
    fails.push(`karoo between -> ${LB.encodeLine(b)}`);
  // Rest, then Sanctuary -> after every cast: the plain land= (the engine's default point).
  let c = [];
  c = LB.queueCard(offers, c, 'Rest for the Weary', 'nonpermanent');
  c = LB.queueCard(offers, c, 'Selesnya Sanctuary', 'land');
  if (LB.encodeLine(c) !== 'land=Selesnya Sanctuary;cast=Rest for the Weary')
    fails.push(`karoo last -> ${LB.encodeLine(c)}`);
  // No earlier Karoo point offered: click order never matters, exactly as before.
  let d = [];
  d = LB.queueCard(plain, d, 'Selesnya Sanctuary', 'land');
  d = LB.queueCard(plain, d, 'Rest for the Weary', 'nonpermanent');
  if (LB.encodeLine(d) !== 'land=Selesnya Sanctuary;cast=Rest for the Weary')
    fails.push(`no karoo position offered -> ${LB.encodeLine(d)}`);
  return fails;
}

// THE HUMAN'S WHOLE SEQUENCE (the "**" --cast-order list, 2026-10-22; USER: "The viewer should use
// exactly the order I give. Same for everything else in the viewer."). sequenceTokens is the committing
// segment as the engine's TurnSolver::ApplyHumanSequencePin reads it: one `<verb>=<name>` per entry IN
// QUEUE ORDER, the land and each Land's Edge discard as steps, the manual taps in place, and the ACTION's
// card for the verbs whose line value names a target (pod / ooze / gyreturn name the source's action).
function checkSequenceTokens() {
  const fails = [];
  const eq = (a, b) => JSON.stringify(a) === JSON.stringify(b);
  const seg = [
    { name: 'Krenko, Mob Boss', src: 'Krenko, Mob Boss', kind: 'activate', verb: 'cast' },
    { name: 'Forest', kind: 'land' },
    { kind: 'pretap', name: 'Forest', num: 7, color: 'G' },
    { name: 'Shard Volley', kind: 'nonpermanent' },
    { name: 'Mountain', kind: 'le' },
    { name: 'Goblin Lackey', kind: 'vial' },
    { name: 'Throes of Chaos', kind: 'retrace' },
    { name: 'Skirk Prospector', src: 'Skirk Prospector', kind: 'activate', verb: 'sacout', sacout: true },
    { name: 'Lotus Bloom', kind: 'suspend' },
    { name: 'Craterhoof Behemoth', src: 'Birthing Pod', kind: 'activate', verb: 'pod' },
    { name: 'Emiel the Blessed', src: 'Emiel the Blessed', kind: 'activate', verb: 'blink', blinkTarget: 42, blinkCount: 1 },
    { name: 'Forest', kind: 'land', fromTop: true },
  ];
  const want = ['act=Krenko, Mob Boss', 'land=Forest', 'tap=Forest#7:G', 'cast=Shard Volley', 'le=Mountain',
                'vial=Goblin Lackey', 'retrace=Throes of Chaos', 'sacout=Skirk Prospector', 'suspend=Lotus Bloom',
                'pod=Birthing Pod', 'blink=Emiel the Blessed@42*1', 'landtop=Forest'];
  const got = LB.sequenceTokens(seg);
  if (!eq(got, want)) fails.push(`sequenceTokens -> ${JSON.stringify(got)}`);
  if (LB.sequenceSteps(seg) !== seg.length - 1) fails.push(`sequenceSteps -> ${LB.sequenceSteps(seg)} (a tap is not a step)`);
  if (LB.sequenceSteps([]) !== 0) fails.push('sequenceSteps([]) != 0');
  // WHICH play (review 2026-10-22): the clicked permanent, the equip host, the kicker / adventure mode
  // ride on the step, so two same-named candidates are never taken in plan-vector order.
  const ids = [
    { name: 'Krenko, Mob Boss', src: 'Krenko, Mob Boss', kind: 'activate', verb: 'cast', srcNum: 17 },
    { name: 'Bonesplitter', src: 'Bonesplitter', kind: 'activate', verb: 'equip', srcNum: 5, target: 9 },
    { name: 'Saproling Migration', kind: 'nonpermanent', kicked: true },
    { name: 'Brightcap Badger', kind: 'permanent', adventure: true },
    { name: 'Kor Duelist', kind: 'activate', verb: 'equipallfree', hostNum: 12 },
  ];
  const wantIds = ['act=Krenko, Mob Boss#17', 'equip=Bonesplitter#5@9', 'cast=Saproling Migration+kicked',
                   'cast=Brightcap Badger+adventure', 'equipallfree=Kor Duelist@12'];
  if (!eq(LB.sequenceTokens(ids), wantIds)) fails.push(`identity tokens -> ${JSON.stringify(LB.sequenceTokens(ids))}`);
  // The VALIDATED line carries the sequence (`seq=`) exactly when the commit will pin it.
  const two = [{ name: 'Sol Ring', kind: 'permanent' }, { name: 'Forest', kind: 'land' }];
  if (LB.validationLine(two) !== 'landafter=Forest;cast=Sol Ring;seq=cast=Sol Ring|land=Forest')
    fails.push(`validationLine(two steps) -> ${LB.validationLine(two)}`);
  const one = [{ name: 'Sol Ring', kind: 'permanent' }];
  if (LB.validationLine(one) !== 'cast=Sol Ring') fails.push(`validationLine(one step) -> ${LB.validationLine(one)}`);
  const tapped = [{ kind: 'pretap', name: 'Forest', num: 3, color: 'G' }, { name: 'Llanowar Elves', kind: 'permanent' }];
  if (LB.validationLine(tapped) !== 'tap=Forest#3:G;cast=Llanowar Elves;seq=tap=Forest#3:G|cast=Llanowar Elves')
    fails.push(`validationLine(tap + one step) -> ${LB.validationLine(tapped)}`);
  if (LB.validationLine([]) !== 'pass') fails.push(`validationLine([]) -> ${LB.validationLine([])}`);
  // A MANA sac outlet's clicks (`sacMana`, the engine's `sac_mana`: Skirk Prospector) are payment, not
  // steps (USER 2026-10-22): no token, no step; the line keeps its `sacout=` for CheckLine.
  const skirk = { name: 'Skirk Prospector', src: 'Skirk Prospector', kind: 'activate', verb: 'sacout', sacout: true, sacMana: true };
  const sk = [skirk, Object.assign({}, skirk), { name: 'Krenko, Mob Boss', kind: 'permanent' }];
  if (!eq(LB.sequenceTokens(sk), ['cast=Krenko, Mob Boss'])) fails.push(`mana sac tokens -> ${JSON.stringify(LB.sequenceTokens(sk))}`);
  if (LB.sequenceSteps(sk) !== 1) fails.push(`mana sac steps -> ${LB.sequenceSteps(sk)}`);
  if (LB.validationLine(sk) !== 'cast=Krenko, Mob Boss;sacout=Skirk Prospector;sacout=Skirk Prospector')
    fails.push(`mana sac validationLine -> ${LB.validationLine(sk)}`);
  // A NON-mana outlet's clicks (Carrion Feeder) stay steps.
  const feeder = { name: 'Carrion Feeder', src: 'Carrion Feeder', kind: 'activate', verb: 'sacout', sacout: true };
  if (!eq(LB.sequenceTokens([feeder, feeder]), ['sacout=Carrion Feeder', 'sacout=Carrion Feeder']))
    fails.push('non-mana sac clicks must stay steps');
  // A SUSPEND-ONLY card's thumb (Lotus Bloom, `suspend_only` on the hand card) queues the SUSPEND, never a
  // cast -- it cannot be cast (USER 2026-10-22).
  const dLotus = { me: { hand: [{ name: 'Lotus Bloom', num: 7, kind: 'permanent', suspend_only: true },
                                { name: 'Lightning Bolt', num: 8, kind: 'nonpermanent' }] } };
  let q = [];
  q = LB.queueCard(dLotus, q, 'Lightning Bolt', 'nonpermanent');
  q = LB.queueCard(dLotus, q, 'Lotus Bloom', 'permanent');
  if (!eq(LB.sequenceTokens(q), ['cast=Lightning Bolt', 'suspend=Lotus Bloom']))
    fails.push(`suspend-only thumb -> ${JSON.stringify(LB.sequenceTokens(q))}`);
  if (LB.encodeLine(q) !== 'cast=Lightning Bolt;suspend=Lotus Bloom') fails.push(`suspend-only line -> ${LB.encodeLine(q)}`);
  q = LB.queueCard(dLotus, q, 'Lotus Bloom', 'permanent');   // one copy: a second click queues nothing more
  if (q.filter(p => p.name === 'Lotus Bloom').length !== 1) fails.push('suspend-only thumb over-queued');
  // An activated untapper's step names its source and target (untapSrc / untapTarget), as its line token does.
  const staff = { name: 'Staff of Domination', src: 'Staff of Domination', kind: 'activate', verb: 'untapact',
                  untapSrc: 4, untapTarget: 9 };
  const staffLoop = Object.assign({}, staff, { untapLoop: true });
  if (!eq(LB.sequenceTokens([staff, staffLoop]), ['untapact=Staff of Domination#4@9', 'untaploop=Staff of Domination#4@9']))
    fails.push(`untapper tokens -> ${JSON.stringify(LB.sequenceTokens([staff, staffLoop]))}`);
  // ...and the continuation's `need=` the commit pin carries rides the previewed sequence too.
  if (LB.validationLine(one, 'need=G') !== 'cast=Sol Ring;seq=cast=Sol Ring|need=G')
    fails.push(`validationLine(one step + need) -> ${LB.validationLine(one, 'need=G')}`);
  return fails;
}

// SEGMENTS KEEP QUEUE ORDER (review 2026-10-22). A new segment starts where an entry cannot join the
// one being built -- a second land drop, a repeat of a repeatable source, a deferred entry -- and every
// entry stays where the human queued it. Before: extra land drops were lifted AHEAD of the line and a
// repeated activation was pushed behind the whole segment.
function checkSegmentOrder() {
  const fails = [];
  const names = parts => parts.map(seg => seg.map(p => p.name));
  const eq = (a, b) => JSON.stringify(a) === JSON.stringify(b);
  const A = { name: 'A', kind: 'nonpermanent' }, B = { name: 'B', kind: 'nonpermanent' };
  const L1 = { name: 'Forest', kind: 'land' }, L2 = { name: 'Island', kind: 'land' };
  let got = names(LB.segmentParts([A, L1, L2, B]));
  if (!eq(got, [['A', 'Forest'], ['Island', 'B']])) fails.push(`cast, land, bonus land, cast -> ${JSON.stringify(got)}`);
  got = names(LB.segmentParts([L1, L2, A]));
  if (!eq(got, [['Forest'], ['Island', 'A']])) fails.push(`land, land, cast -> ${JSON.stringify(got)}`);
  const blink = () => ({ name: 'Emiel', src: 'Emiel', kind: 'activate', verb: 'blink', repeatable: true });
  const b1 = blink(), b2 = blink();
  got = names(LB.segmentParts([b1, b2, A]));
  if (!eq(got, [['Emiel'], ['Emiel', 'A']])) fails.push(`blink, blink, cast -> ${JSON.stringify(got)}`);
  got = names(LB.segmentParts([A, b1, B, b2]));
  if (!eq(got, [['A', 'Emiel', 'B'], ['Emiel']])) fails.push(`cast, blink, cast, blink -> ${JSON.stringify(got)}`);
  if (!eq(LB.segmentParts([]), [[]])) fails.push('an empty plan is one empty segment');
  // The concatenated segments ARE the queue, entry for entry and in order.
  const q = [A, L1, b1, B, L2, b2];
  const flat = [].concat(...LB.segmentParts(q));
  if (flat.length !== q.length || flat.some((p, i) => p !== q[i])) fails.push('segments do not concatenate back to the queue');
  // A queued CYCLE (USER 2026-10-28: "Cycling should be a plan item like any other") ENDS its segment --
  // the engine runs a plan's cycle after its casts -- and on a deck whose cycles are standalone plans
  // (`alone`) it is a segment of its own. Its line token is cycle=, its "**" step cycle=.
  const C = { name: 'Curator of Mysteries', kind: 'cycle', alone: false };
  const Cs = { name: 'Lonely Sandbar', kind: 'cycle', alone: true };
  got = names(LB.segmentParts([A, B, C]));
  if (!eq(got, [['A', 'B', 'Curator of Mysteries']])) fails.push(`cast, cast, cycle -> ${JSON.stringify(got)}`);
  got = names(LB.segmentParts([A, C, B]));
  if (!eq(got, [['A', 'Curator of Mysteries'], ['B']])) fails.push(`cast, cycle, cast -> ${JSON.stringify(got)}`);
  got = names(LB.segmentParts([C, C]));
  if (!eq(got, [['Curator of Mysteries'], ['Curator of Mysteries']])) fails.push(`cycle, cycle -> ${JSON.stringify(got)}`);
  got = names(LB.segmentParts([A, Cs, B]));
  if (!eq(got, [['A'], ['Lonely Sandbar'], ['B']])) fails.push(`cast, standalone cycle, cast -> ${JSON.stringify(got)}`);
  if (LB.encodeLine([A, B, C]) !== 'cast=A;cast=B;cycle=Curator of Mysteries')
    fails.push(`encodeLine(cast, cast, cycle) -> ${LB.encodeLine([A, B, C])}`);
  if (JSON.stringify(LB.sequenceTokens([A, B, C])) !== JSON.stringify(['cast=A', 'cast=B', 'cycle=Curator of Mysteries']))
    fails.push(`sequenceTokens(cast, cast, cycle) -> ${JSON.stringify(LB.sequenceTokens([A, B, C]))}`);
  return fails;
}

// CLICK-ORDER PICKS (cleanup discard, London bottoming; USER 2026-10-22). The engine reads one hand
// index per decision against a hand the previous pick shrank, so the picks go in CLICK order, each
// re-based: an earlier pick at a lower index shifts it down by one. (They used to be sorted into
// descending index order -- always valid, never the order the human chose.)
function checkShrinkingIndices() {
  const fails = [];
  const eq = (a, b) => JSON.stringify(a) === JSON.stringify(b);
  const cases = [
    [[], []],
    [[3], [3]],
    [[1, 4], [1, 3]],          // the 2nd card (index 1) first: index 4 is index 3 once it is gone
    [[4, 1], [4, 1]],          // the higher one first: nothing below it moved
    [[5, 0, 3], [5, 0, 2]],
    [[2, 6, 0, 4], [2, 5, 0, 2]],
  ];
  for (const [picks, want] of cases) {
    const got = LB.shrinkingIndices(picks);
    if (!eq(got, want)) fails.push(`shrinkingIndices(${JSON.stringify(picks)}) -> ${JSON.stringify(got)}, want ${JSON.stringify(want)}`);
  }
  // Replaying the re-based indices against a shrinking hand removes exactly the clicked cards, in order.
  const hand = ['a', 'b', 'c', 'd', 'e', 'f', 'g'];
  const picks = [2, 6, 0, 4];
  const live = hand.slice(), removed = [];
  for (const i of LB.shrinkingIndices(picks)) { removed.push(live[i]); live.splice(i, 1); }
  if (!eq(removed, picks.map(i => hand[i]))) fails.push(`replay removed ${JSON.stringify(removed)}`);
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
  const tapFails = checkPreTapToken();
  tapFails.forEach(m => console.log(`  FAIL  pre-tap token: ${m}`));
  console.log(`Viewer pre-tap token: ${tapFails.length ? 'WRONG' : "the source's face (a filter's multi-letter output verbatim), plus a +<C> per any-colour Aura; absent when none"} ` +
              `(${tapFails.length} FAIL)`);
  const topFails = checkLibraryTopLand();
  topFails.forEach(m => console.log(`  FAIL  library-top land: ${m}`));
  console.log(`Viewer library-top land: ${topFails.length ? 'WRONG' : 'the revealed top queues as landtop=, separate from a hand copy'} ` +
              `(${topFails.length} FAIL)`);
  const posFails = checkLandPosition();
  posFails.forEach(m => console.log(`  FAIL  land position: ${m}`));
  console.log(`Viewer land position: ${posFails.length ? 'WRONG' : 'a land between two casts encodes landat=<k>, after all landafter='} ` +
              `(${posFails.length} FAIL)`);
  const seqFails = checkSequenceTokens();
  seqFails.forEach(m => console.log(`  FAIL  sequence tokens: ${m}`));
  console.log(`Viewer whole-sequence pin: ${seqFails.length ? 'WRONG' : 'every queued entry is one <verb>=<name> step in queue order'} ` +
              `(${seqFails.length} FAIL)`);
  const segFails = checkSegmentOrder();
  segFails.forEach(m => console.log(`  FAIL  segment order: ${m}`));
  console.log(`Viewer segment order: ${segFails.length ? 'WRONG' : 'segments cut where needed, every entry in queue order'} ` +
              `(${segFails.length} FAIL)`);
  const shrinkFails = checkShrinkingIndices();
  shrinkFails.forEach(m => console.log(`  FAIL  click-order picks: ${m}`));
  console.log(`Viewer click-order picks: ${shrinkFails.length ? 'WRONG' : 'discard / bottom picks go in click order, re-based'} ` +
              `(${shrinkFails.length} FAIL)`);
  const karooFails = checkKarooPosition();
  karooFails.forEach(m => console.log(`  FAIL  karoo position: ${m}`));
  console.log(`Viewer karoo position: ${karooFails.length ? 'WRONG' : 'a Karoo before a cast encodes landat=<k> (0 = first), after every cast land='} ` +
              `(${karooFails.length} FAIL)`);
  const kickFails = checkKickerToken();
  kickFails.forEach(m => console.log(`  FAIL  kicker: ${m}`));
  console.log(`Viewer kicker: ${kickFails.length ? 'WRONG' : 'cast=+kicked= per kicked copy; the badge toggles in place and never over-queues'} ` +
              `(${kickFails.length} FAIL)`);
  return (fail + dimFails.length + landFails.length + sacFails.length + verbFails.length
          + mixFails.length + hostFails.length + stackFails.length + macroFails.length
          + tapFails.length + topFails.length + posFails.length + kickFails.length
          + karooFails.length + seqFails.length + shrinkFails.length + segFails.length) ? 1 : 0;
}

process.exit(main());
