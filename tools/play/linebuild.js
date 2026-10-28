// Pure line-building logic for the play GUI — the ONE source of truth shared by the browser
// (index.html loads it via <script src="/linebuild.js">) and the frontend regression check
// (test/viewer_linebuild_check.js require()s it). No DOM, no globals: every function takes the
// decision object and/or the current plan array explicitly, so the exact code the GUI ships can
// be driven headlessly against saved reference games.
//
// Why this exists: the engine↔protocol contract check (test/viewer_protocol_check.py) replays a
// reference's chosen plan INDICES straight into the engine and can't see whether the GUI can
// actually BUILD that line from clicks. A staged (exiled-but-playable) cast regression lived
// entirely here — queueCard silently refused to queue a card the engine happily enumerated — so
// the buildability of every recorded line is now guarded by exercising these functions directly.
//
// `plan` is the array of queued entries {name, kind}; kind ∈ land | nonpermanent | permanent |
// vial | retrace | le. `decision` is the main_phase decision JSON (needs .me.hand, .lands_edge,
// .opponent for Land's Edge math).
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.LineBuild = factory();
})(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  function planLand(plan) { return plan.find(p => p.kind === 'land') || null; }
  // Every queued land, in queue order. A committed SEGMENT carries at most one (the engine's
  // Plan::land_to_play is a single land), so a multi-land plan commits as consecutive segments --
  // see encodeSegments.
  function planLands(plan) { return plan.filter(p => p.kind === 'land'); }
  // Land drops still available at this decision (engine truth: me.land_drops_left). Older decision
  // payloads predate the field -> assume the ordinary one.
  function landDropsLeft(decision) {
    const n = decision && decision.me && decision.me.land_drops_left;
    return (typeof n === 'number') ? n : 1;
  }

  // Counts of REAL hand copies by name -- excludes staged (exiled-but-playable) cards, which are
  // shown and counted separately (else e.g. 1 hand Mountain + 2 staged Mountains mis-pip as x3).
  function handCounts(decision) {
    const m = {}; (decision.me.hand || []).forEach(c => { if (!c.is_staged) m[c.name] = (m[c.name] || 0) + 1; }); return m;
  }
  // Counts of STAGED copies by name@expiry -- so duplicate staged cards collapse to one x N thumb.
  function stagedCounts(decision) {
    const m = {}; (decision.me.hand || []).forEach(c => { if (c.is_staged) { const k = c.name + '@' + c.staged_until; m[k] = (m[k] || 0) + 1; } }); return m;
  }
  // Total castable copies of a name = REAL hand copies PLUS staged copies. This is the queue cap:
  // handCounts() alone excludes staged cards, so a staged-ONLY spell (Soulfire dig, a Light Up card
  // with no in-hand duplicate) had cap 0 and could never be queued -- double-click/drag silently
  // did nothing. Burn's 4-of redundancy masked it (a staged copy usually had a real duplicate).
  function castableCount(decision, name) {
    return (decision.me.hand || []).filter(c => c.name === name).length;
  }
  // 'le' entries are Land's Edge discards and 'pretap' entries are board mana taps, not casts --
  // exclude both from the cast count (a pre-tapped Forest must not eat a Forest's cast cap).
  function plannedCount(plan, name) {
    return plan.filter(p => p.name === name && p.kind !== 'land' && p.kind !== 'le'
                                            && p.kind !== 'pretap').length;
  }
  function leCount(plan) { return plan.filter(p => p.kind === 'le').length; }

  // How many lands may still be discarded to Land's Edge: lands in hand minus the one taken by a
  // queued land drop, minus those already queued, capped at lethal (mirrors the engine's enum cap).
  function leMax(decision, plan) {
    const le = decision && decision.lands_edge; if (!le) return 0;
    const avail = (le.lands_in_hand || 0) - planLands(plan).length;
    const lethal = le.rate > 0 ? Math.ceil((decision.opponent.life || 0) / le.rate) : 0;
    return Math.max(0, Math.min(avail, lethal));
  }

  // A queued SAC-OUTLET activation (Skirk Prospector / Siege-Gang / Pashalik). Tagged with a flag
  // rather than given its own `kind` so every existing renderer that special-cases kind==='activate'
  // (the planbar chip, the ⟳ badge, the "puts no card onto the battlefield" checks) keeps working
  // untouched; only the encoding differs. One entry == one creature sacrificed.
  function isSacOut(p) { return p.kind === 'activate' && !!p.sacout; }

  // The LineSpec verb a queued entry writes. Only an 'activate' entry can carry one other than
  // 'cast': the engine tags each activation with the verb CheckLine matches it by (see the
  // `verb` field in main.cpp's plan-action JSON). `sacout` predates the field, so an entry that
  // only carries the old flag still resolves to sacout=.
  //   act=        Krenko's tap, a loyalty ability, Call of the Wild            (orderNames match,
  //               and the plan must ACTIVATE it -- LineSpec::board_acts; a hand cast is cast=)
  //   sacout=     Skirk Prospector / Siege-Gang / Pashalik                     (LineSpec::sac_outlets)
  //   equip=      attach an Equipment to a creature, "<name>[#src][@host]"     (LineSpec::equips)
  //   attachall=  Balan's "attach all Equipment"                               (LineSpec::attach_all)
  //   sfput=      Stoneforge Mystic's put-from-hand (names the EQUIPMENT)      (LineSpec::sf_puts)
  //   auraswap=   Arcanum Wings' main-phase Aura swap (names the Aura IN)      (LineSpec::aura_swaps)
  //   jittemode=  Umezawa's Jitte counter-spend (names the MODE INT)           (LineSpec::jitte_modes)
  //   gyexile=    Deathrite Shaman's graveyard exile (names the MODE INT)      (LineSpec::gy_exiles)
  //   gyreturn=   Haven's sac-to-rebuy (names the RETURNED CARD, not the land)  (LineSpec::gy_returns)
  //   pod=        Birthing Pod's activation (names the FETCH; "(no fetch)" = sac only;
  //               the victim is picked via the choose-variant dialog)          (LineSpec::pods)
  //   ooze=       Scavenging Ooze's exile (names the EXILED graveyard card)    (LineSpec::ooze_exiles)
  //   channel=    Twinshot Sniper's from-HAND channel ability (names the card) (LineSpec::channels)
  //   suspend=    Lotus Bloom's from-HAND Suspend (names the card)             (LineSpec::suspends)
  //   eternalize= Timeless Witness's from-GRAVEYARD Eternalize (names the card) (LineSpec::eternalizes)
  //   adventure=  Brightcap Badger's adventure half (names the PARENT card)     (LineSpec::adventures)
  //   kicked=     Saproling Migration cast WITH its kicker paid (names the card)  (LineSpec::kickeds)
  //   blink=      Emiel / Eldrazi Displacer, "<outlet>[@<target m_number>]"    (LineSpec::blinks)
  //   taplife=    Wellwisher's "{T}: gain 1 life for each Elf" (names the SOURCE) (LineSpec::tap_lifes)
  //   landsearch= Evolving Wilds / Myriad Landscape / Khalni Heart Expedition crack (names the SOURCE;
  //               the lands are picked at resolution)                      (LineSpec::land_searches)
  //   untapact=   Crab Umbra / Galvanic Alchemist's grant / Staff of Domination / Seedcradle Witch,
  //               "<source>[#<source m_number>][@<target m_number>]"       (LineSpec::untap_acts)
  //   tapdraw=    Staff of Domination's "{5},{T}: draw" (names the SOURCE)  (LineSpec::tap_draws)
  //   untaploop=  the closed-form untap LOOP on an untapper (same "#src@target" shape as untapact=;
  //               runs until the float funds the kill -- chunk C2b)        (LineSpec::untap_acts, loop)
  // The verbs whose value is a MODE INT rather than a card name. Kept as a set so encodeLine has one
  // rule instead of a growing `v === 'jittemode' || v === 'gyexile' || ...` chain.
  const MODE_VERBS = { jittemode: true, gyexile: true };
  // ...and the verbs whose value is a BOARD M_NUMBER rather than a card name or a mode int.
  //   equipallfree=  "attach every FREE loose Equipment to this creature", ONE engine action
  //                  covering N attaches                                  (LineSpec::equip_all_free)
  // It names the HOST's number because two Kor Duelists in play are genuinely different hosts --
  // the same reason `equip=` carries `@<host>` rather than trusting a by-name sub-decision.
  const NUM_VERBS = { equipallfree: true };
  function lineVerb(p) {
    // A CHANNEL entry is the one non-'activate' kind with its own verb: its source is a card in HAND
    // (so it is not a board activation) but it is not a cast either -- "{1}{R}, Discard this card"
    // plays the same card a different way, and `cast=` cannot say which.
    if (p.kind === 'channel') return 'channel';
    // ... and a CYCLE (cycle a card from HAND to draw, or into Unpredictable Cyclone's walk): the same
    // card played a different way, so `cast=` cannot say it. USER 2026-10-28: "Cycling should be a plan
    // item like any other." (LineSpec::cycles; a plan's cycle is matched only by this verb.)
    if (p.kind === 'cycle') return 'cycle';
    // ... and SUSPEND is the second: exiling the card from hand with time counters is an
    // alternative to casting it (CR 702.61a), and `cast=` cannot say which of the two was meant.
    if (p.kind === 'suspend') return 'suspend';
    // ... and ETERNALIZE (Timeless Witness) is activated from the GRAVEYARD -- neither a cast nor a
    // board activation, so it carries its own verb (names the graveyard card).
    if (p.kind === 'eternalize') return 'eternalize';
    if (p.kind !== 'activate') return 'cast';
    // A plain board activation (Frost Augur's look, Krenko's tap) writes `act=`, not `cast=`: the
    // engine keeps it in the same ordered multiset but requires the matched plan to ACTIVATE the
    // source, so a same-named copy in HAND cannot match instead. Snow seed 9 T3 (USER 2026-10-06):
    // `cast=Frost Augur` matched both, and the human got a "Frost Augur X?" dialog for nothing.
    const v = p.verb || (p.sacout ? 'sacout' : 'cast');
    // The closed-form untap LOOP (AccomplishedAlchemist chunk C2b) is an untapact= option flagged
    // untapLoop: it writes its own verb so a single activation and the loop never match each other.
    if (v === 'untapact' && p.untapLoop) return 'untaploop';
    return v === 'cast' ? 'act' : v;
  }

  // A multi-land plan (a Scale the Heights bonus drop) as the SEGMENTS the engine can accept: one
  // land-only segment per extra drop, then the final land WITH every cast. Lands go first so the
  // casts are paid off the full set of lands the human meant to play -- committing a cast segment
  // before its second land is what made "play two lands, then cast" unreachable. A single-land plan
  // returns exactly one segment == encodeLine(plan), so nothing else changes.
  // The plan split into the consecutive SEGMENTS the engine can accept, as plan-entry arrays.
  // Two independent reasons a line needs more than one:
  //   * two land drops (above), and
  //   * REPEATED activations of one repeatable ability. An engine Plan holds at most ONE activation
  //     of a given source, so "blink ten times" cannot be one plan -- but the main phase re-prompts
  //     after each commit, so ten consecutive one-activation segments IS the ten-activation turn.
  //     USER 2026-09-07: "if I click the ability 10 times I should get 10 activations (as part of
  //     the plan) rather than needing to commit line between each." The extra clicks split off here
  //     and advanceTo auto-commits them, so the human clicks ten times and commits ONCE.
  // Only repeats of the SAME source split: two DIFFERENT activations are what the engine's powerset
  // already enumerates together, so they stay in one segment exactly as they do today. A plan with
  // no repeat is partitioned identically to before -> every existing line and saved reference is
  // byte-identical.
  // ---- LINE MACROS (docs/design/viewer-line-macros.md) ---------------------------------------
  // A DEFERRED entry starts a new segment, and every entry after it goes in that segment (or a
  // later one). This is the ONE new primitive the two macro features need, and both need exactly
  // it -- for the same underlying reason:
  //
  //   * "Investigate & crack" -- the Clue does not EXIST while the Investigate is being committed,
  //     so the crack cannot be in the same line. It has to be validated against the decision the
  //     Investigate produces.
  //   * "repeat xN" -- an engine Plan holds at most ONE activation of a given source, and the
  //     `repeatable` split above only covers sources the engine tags repeatable (no {T}, no
  //     sacrifice: ActivateBlink and the Drain/ExileTop perm abilities -- src/main.cpp's
  //     `repeatable` emit). A {T} outlet like Mariposa's "{5},{T}: draw" is NOT tagged, because it
  //     genuinely cannot be activated twice -- until a blink untaps it. So the second iteration of
  //     a [draw, blink] block has to be its own segment, and nothing in the existing split says so.
  //
  // `defer` is therefore a statement about the QUEUE, not about the card, which is why it is a flag
  // on the entry rather than another engine-published predicate. No entry the viewer built before
  // this feature carries one, so every recorded line partitions exactly as it always did.
  function isDeferred(p) { return !!p.defer; }

  // IN QUEUE ORDER, ALWAYS (USER 2026-10-22: "The viewer should use exactly the order I give."). A new
  // segment starts exactly where an entry cannot join the one being built -- a DEFERRED entry, a repeat
  // of a repeatable source the segment already activates, a second land drop -- and every entry stays
  // where the human put it. This used to lift every extra land drop into a land-only segment AHEAD of
  // the line ("cast, land, bonus land" played both lands first) and to push every repeat of a source
  // behind the WHOLE segment ("blink, blink, cast X" ran X before the second blink).
  // A deferred entry at position 0 is the head of its own segment, so it starts nothing new (the old
  // recursion's "scan starts at 1"). Every returned entry is IDENTICAL (by object identity) to the one
  // in `plan` -- dropFirstSegment peels by identity. An empty plan is one empty segment ('pass').
  function segmentParts(plan) {
    const parts = [];
    let cur = [], seen = {}, hasLand = false;
    const flush = () => { if (cur.length) { parts.push(cur); } cur = []; seen = {}; hasLand = false; };
    (plan || []).forEach((p, i) => {
      const k = (p.kind === 'activate' && p.repeatable) ? (p.src || p.name) : null;
      // A CYCLE ends its segment: the engine's plan cycles AFTER its casts (the cycle seed's position), and
      // a plan holds one cycle, so "cast A, cycle B, cast C" commits as [A, cycle B] then [C] -- the queue
      // order, exactly. On a deck whose cycles are standalone plans (`alone`, stamped at queue time from
      // the decision's own plans) the cycle is a segment by itself.
      const cut = (i > 0 && (isDeferred(p) || (p.kind === 'cycle' && p.alone))) || (k && seen[k])
               || (p.kind === 'land' && hasLand) || (i > 0 && cur.length && cur[cur.length - 1].kind === 'cycle');
      if (cut) { flush(); }
      if (k) { seen[k] = true; }
      if (p.kind === 'land') { hasLand = true; }
      cur.push(p);
    });
    flush();
    return parts.length ? parts : [[]];
  }

  // Repeat plan entries [from, to) `n` times in total (n=1 is a no-op), returning the new plan.
  // Each repetition after the first begins with a DEFERRED copy, so the repetitions commit as
  // consecutive segments -- exactly the sequence the human would have produced by clicking the
  // block out N times and committing between each, which is what keeps a macro'd game replayable
  // from its saved reference with no new protocol.
  //
  // Entries are COPIED (never shared), because `dropFirstSegment` peels by object IDENTITY: two
  // repetitions sharing one object would both vanish when the first segment committed.
  //
  // A REPETITION KEEPS THE BLOCK'S OWN INTERNAL BOUNDARIES. Only entry 0 of each repetition has its
  // `defer` FORCED on (it is the entry that starts the repetition's first line); every other entry
  // keeps whatever flag it already carried, because a boundary INSIDE the block is part of the
  // block's shape, not noise to normalise away.
  //
  // This used to write `{ defer: false }` on every non-first entry, and that was the EDF loop bug
  // (2026-09-11). The block the user repeats is the Kitchen loop, which is queued as a FUSED
  // "Investigate & crack" plus a blink -- and the crack is DEFERRED by construction, because the
  // Clue does not exist while the Investigate is being committed. Clearing that flag on the copies
  // collapsed every iteration after the first into ONE line:
  //
  //      want   cast=Kitchen                | cast=Clue Token;blink=Eldrazi Displacer@42*1
  //      got    cast=Kitchen;cast=Clue Token;blink=Eldrazi Displacer@42*1        <- illegal
  //
  // which is exactly the one-line form test/scenarios/edf_fused_clue_needs_two_lines.json pins as
  // ILLEGAL ("'Clue Token' is not in hand"). So iteration 1 committed, iteration 2 was rejected, the
  // chain stopped, and the game picked up a reject it can never be saved as a clean reference with.
  function repeatBlock(plan, from, to, n) {
    const block = plan.slice(from, to);
    if (!block.length || !(n > 1)) { return plan; }
    const out = plan.slice(0, to);
    for (let r = 1; r < n; ++r) {
      block.forEach((p, j) => {
        out.push(j === 0 ? Object.assign({}, p, { defer: true }) : Object.assign({}, p));
      });
    }
    return out.concat(plan.slice(to));
  }

  // ---- LOOP: repeat the last K COMMITTED segments (docs/design/viewer-line-macros.md) ----------
  // `repeatBlock` above repeats what is still QUEUED, so it has to be set up before the first
  // iteration is committed. The loop the EDF player actually grinds is discovered by playing it:
  // activate Kitchen, blink a Drake to untap, crack the Clue -- and only THEN "do that again, twenty
  // times". By that point the queue is empty and the block exists only as committed history.
  //
  // So this takes the SEGMENTS that were committed (each an array of the plan entries that made up
  // one line) and rebuilds them as a queue block. The head of every segment is deferred, which is
  // what makes the block commit back as the same K lines rather than one fused line -- the same
  // primitive, applied to a boundary that is already known rather than one being invented.
  //
  // A segment's entries are COPIED, and the copy drops `num` for the HAND kinds: that field is the
  // m_number of the specific hand copy the entry was stamped with (stampPlanNums), which belongs to
  // the frame it was queued on. A board id -- a `pretap`'s permanent, an activation's source, an
  // equip/enchant host -- is the human's declared choice and is kept verbatim, so a looped `tap=`
  // token names the same land it named the first time.
  const HAND_KINDS = { land: true, nonpermanent: true, permanent: true, vial: true,
                       retrace: true, channel: true, suspend: true, cycle: true };
  function loopBlock(segments, n) {
    const flat = [];
    (segments || []).forEach(seg => (seg || []).forEach((p, j) => {
      const c = Object.assign({}, p);
      if (HAND_KINDS[c.kind]) { delete c.num; }
      if (j === 0) { c.defer = true; }
      flat.push(c);
    }));
    if (!flat.length || !(n > 0)) { return []; }
    return n > 1 ? repeatBlock(flat, 0, flat.length, n) : flat;
  }

  // Is a committed segment one the loop may repeat? A LAND DROP and a LAND'S EDGE discard are
  // once-per-turn resources, so a second iteration is a line the engine simply rejects -- the
  // control is not offered rather than offered and then refused, exactly as for `repeatBlock`.
  //
  // A `pretap` is NOT refused here, and that is the one place this differs from the queue-time
  // Repeat. There the block has never been played, so a tap naming a specific untapped copy is a
  // guess about a board that does not exist yet. Here the segment ALREADY committed once, and the
  // loop being repeated is usually the very thing that untaps that land again -- so the honest
  // answer is to replay the human's declared tap and let the per-iteration validation say no if the
  // board really has moved, rather than to withhold the control from every hand-paid loop.
  function segmentLoopable(seg) {
    return !!(seg && seg.length) && !seg.some(p => p.kind === 'land' || p.kind === 'le');
  }

  // The `need=<COLOURS>` token for a committing segment: the non-generic pips every entry STILL
  // QUEUED behind it wants, deduped to a letter set. "" when the continuation is empty or wants
  // nothing specific, in which case the caller emits no token at all and the engine's untap pick is
  // byte-identical to before.
  //
  // `pipsOf` is supplied by the caller because the two entry classes read their pips from different
  // published fields: an ACTIVATION from its enumerated action's `cost_pips`, a hand CAST from the
  // hand card's `cost` display string. Keeping the lookup out here is what makes this testable.
  function untapNeed(remaining, pipsOf) {
    const seen = {};
    (remaining || []).forEach(p => {
      const s = pipsOf(p) || '';
      for (const ch of s) { if ('WUBRGC'.indexOf(ch) >= 0) { seen[ch] = true; } }
    });
    // A FIXED alphabet order, so the same continuation always produces the same token -- a saved
    // reference replays the string byte-for-byte, and two orderings of one queue cannot disagree.
    return 'WUBRGC'.split('').filter(c => seen[c]).join('');
  }
  function untapNeedToken(remaining, pipsOf) {
    const s = untapNeed(remaining, pipsOf);
    return s ? 'need=' + s : '';
  }

  // ---- FUSED "investigate & crack" ------------------------------------------------------------
  // The engine's CLUE-FUSION doctrine (fuse create+spend, payability-gated) is a SEARCH shortcut and
  // is explicitly excluded from human play -- "the viewer keeps per-action blinking and its explicit
  // FINISH plan" (DecisionProviders.cpp's EdfAutoGoOffAfterCasts note). This is the human-queue
  // analogue of the same idea: one gesture, two queued entries, no engine change at all.
  //
  // It has to be TWO entries rather than one fused line token because the Clue is not on the
  // battlefield while the Investigate is being committed -- there is no `cast=Clue Token` for the
  // engine to match yet. The crack is therefore DEFERRED: it validates against the decision the
  // Investigate produces, where the Clue is real, individually addressable and (having no {T} in its
  // sac cost) crackable the same turn. Both halves encode exactly as a hand-clicked pair would.
  //
  // `srcName` is the Investigate source (Conservatory / Kitchen); `verb`/`mode` come from the
  // enumerated action as for any other activation, so a future investigate source with its own verb
  // needs no change here.
  var CLUE_TOKEN = 'Clue Token';
  function fusedInvestigateEntries(srcName, opt) {
    const o = opt || {};
    return [
      { name: srcName, src: srcName, kind: 'activate', verb: o.verb || 'cast',
        mode: o.mode != null ? o.mode : null, fuse: 'clue' },
      // The crack. `defer` puts it in the NEXT segment; `fused` marks it as the tail of a fused
      // gesture so the plan bar can render the pair as one chip and remove them together.
      { name: CLUE_TOKEN, src: CLUE_TOKEN, kind: 'activate', verb: 'cast',
        defer: true, fused: 'clue' }
    ];
  }
  // Removing either half of a fused pair removes both: half a fused gesture is an Investigate whose
  // Clue is never cracked (or a crack with nothing to crack), and neither is what the human asked
  // for. Returns the new plan.
  function removeFusedAt(plan, i) {
    const p = plan[i];
    if (!p) { return plan; }
    if (p.fuse === 'clue') {
      const j = plan.findIndex((q, k) => k > i && q.fused === 'clue');
      return plan.filter((q, k) => k !== i && k !== j);
    }
    if (p.fused === 'clue') {
      let j = -1;
      for (let k = i - 1; k >= 0; --k) { if (plan[k].fuse === 'clue') { j = k; break; } }
      return plan.filter((q, k) => k !== i && k !== j);
    }
    return plan.filter((q, k) => k !== i);
  }
  function encodeSegments(plan) { return segmentParts(plan).map(encodeLine); }
  // The plan entries left after the FIRST segment commits -- what stays queued while the chain runs.
  // Idempotent by construction: re-partitioning the remainder peels exactly one more segment, so the
  // caller can just call it once per accepted segment.
  function dropFirstSegment(plan) {
    const first = segmentParts(plan)[0] || [];
    return plan.filter(p => first.indexOf(p) < 0);
  }

  // The "#<equipment m_number>@<host m_number>" suffix an `equip=` token carries. Both halves are
  // OPTIONAL and each is omitted when the viewer does not know it:
  //   * `srcNum` — which copy is being attached. Absent when the Equipment is not on the battlefield
  //     yet, i.e. a cast-and-equip queued by dragging the card straight from HAND: the permanent it
  //     becomes has no m_number to name until the cast resolves. The engine reads 0 as "any".
  //   * `target` — the host the human dropped it on. Always present for a drag.
  // Stamping them is what makes two same-named creatures (or two copies of one Equipment) separable:
  // the engine's `equip` sub-decision keys on the host NAME, so without these the variants share a
  // signature and all but one are silently dropped -- the second Kor Duelist could not be equipped
  // at all (user-reported, KittyEquipment seed 6). No MTG card name contains '#' or '@'.
  function equipIds(p) {
    return (p.srcNum ? '#' + p.srcNum : '') + (p.target ? '@' + p.target : '');
  }

  // The "@<blinked creature m_number>" suffix a `blink=` token carries. Same reason equipIds exists:
  // the outlet's NAME is not the decision -- blinking a Peregrine Drake (refill five lands) and
  // blinking anything else both read `blink=Emiel the Blessed`, so without the target the engine
  // matches whichever variant sorted first and the human's pick is silently discarded. Omitted (read
  // as "any") when the viewer somehow has no target, which keeps a legacy line parsing.
  // ...and a "*<count>" tail for the FINISH plan (the go-off: run the loop N times and cash the
  // sinks). No MTG card name contains '*'.
  //
  // THE COUNT IS EMITTED FOR *EVERY* BLINK, INCLUDING 1 -- it used to be written only for count > 1,
  // and that omission is what produced the phantom "pick one of 2" dialog on every single Emiel /
  // Displacer activation (USER, seed 11 T6: "spammed by dialogs that shouldn't exist ...
  // Emiel the Blessed X? / X=1 / X=6"). The chain: under human play the enumerator deliberately
  // offers TWO blink actions -- the single activation and one sized FINISH count (TurnSolver.cpp's
  // fold_fanout / MTG_PLAY_FINISH_PLAN) -- while CheckLine's BlinkAssign treats a count of 0 as a
  // WILDCARD. A countless `blink=Emiel the Blessed@42` therefore matched BOTH plans, CheckLine
  // returned `choose`, and the viewer rendered the pair as an X question. Emiel has no {X} in its
  // cost at all (`blink_cost: "{3}"`), so that dialog asked about an internal batching artifact --
  // exactly what the play-viewer decision principle says must never interrupt the player.
  //
  // Saying "*1" makes an ordinary click UNAMBIGUOUS, so it accepts straight through with no dialog,
  // and the bulk affordance stays exactly where the user wants it: the explicit COMBO OFF button,
  // which encodes its own "*N". Nothing is lost by being explicit -- multi-activation is expressed
  // by clicking K times (segmentParts splits those into K one-blink segments, each now writing
  // "*1"), not by this token. Engine-side back-compat is untouched: BlinkAssign's 0 == wildcard
  // still stands, so every saved reference's countless `blink=` string keeps matching what it
  // always matched. This file is loaded only by the play viewer, so no engine or search path can
  // reach it -- budgeted search play is byte-identical by construction.
  function blinkIds(p) {
    return (p.blinkTarget ? '@' + p.blinkTarget : '')
         + (p.blinkCount > 0 ? '*' + p.blinkCount : '');
  }

  // The "#<source m_number>@<target m_number>" suffix an `untapact=` token carries -- equipIds' shape.
  // The SOURCE number matters here where it does not for a blink: two Staffs of Domination, or two
  // Crab Umbras on two creatures, share a name, and a tapped Staff costs {1} more to reuse than an
  // untapped one. The TARGET is the decision (which creature to untap). Either omitted = any.
  function untapIds(p) {
    return (p.untapSrc ? '#' + p.untapSrc : '') + (p.untapTarget ? '@' + p.untapTarget : '');
  }

  // ---- MANUAL TAP/PAY (docs/design/viewer-manual-tap-pay.md) ---------------------------------
  // A 'pretap' entry is a board mana source the human tapped BY HAND for a specific face, so the
  // engine's payment spends that unit instead of allocating one itself. It is not a play: it never
  // enters the cast multiset, it takes no plan action, and a line with none of them encodes exactly
  // as it always did. `num` is the PERMANENT's m_number (which copy), `color` one of W U B R G C --
  // or, for a mana-CONVERSION source's fed output, several of them ("UR" for Izzet Signet, "UU" /
  // "UR" / "RR" for Cascade Bluffs; the engine publishes them as `tap_fed`), written verbatim the
  // same way: `tap=Izzet Signet#20:UR`. A one-letter face encodes exactly as it always did.
  //
  // The token text is produced HERE and nowhere else, because it is written into two different
  // arguments -- the `--validate-line` spec and the `--cast-order` full-order list -- and the
  // engine parses both with one reader (ParseHumanPreTapToken). Two producers is how the two
  // arguments would come to disagree about the same tap.
  //
  // `auraColors` is the human's colour for each ANY-COLOUR land Aura on the host (Fertile Ground,
  // Trace of Abundance -- "adds an additional one mana of any color"), appended as '+<C>' suffixes
  // in the engine's AnyColorLandAuras order: `tap=Brushland#7:W+U`. ABSENT WHEN EMPTY, which is what
  // keeps every line ever recorded encoding byte-for-byte as it did -- the engine reads a token with
  // no '+' exactly as before (the bonus credits as `wild`). The '+' is only ever written INSIDE the
  // colour field, past the final ':', so a card name containing one is unaffected.
  function isPreTap(p) { return p.kind === 'pretap'; }
  function preTapToken(p) {
    const ac = (p.auraColors || []).filter(c => c);
    return 'tap=' + p.name + '#' + (p.num || 0) + ':' + (p.color || '')
         + (ac.length ? '+' + ac.join('+') : '');
  }

  // LAND-DROP POSITION (engine Plan::land_slot, 2026-10-15). Does this decision offer ANY line that
  // plays its land AFTER a cast -- after all of them (`land_after_casts`) or right after a landfall
  // cast in the middle (`land_slot`)? Only a deck holding a landfall card ever does, and only on a
  // turn where a cast can put a landfall permanent onto the battlefield; everywhere else this is
  // false and a queued land encodes as `land=` exactly as it always did.
  function offersLandAfter(decision) {
    return !!(decision && Array.isArray(decision.plans)
              && decision.plans.some(pl => pl && (pl.land_after_casts || pl.land_slot)));
  }
  // An entry that encodes into the engine's LineSpec::casts (a `cast=` or `act=` token) -- what a
  // `landat=<k>` position counts. Mirrors encodeLine's cast filter exactly.
  function isSpecCast(p) {
    return p && p.kind !== 'land' && p.kind !== 'le' && p.kind !== 'vial' && p.kind !== 'retrace'
        && !isPreTap(p) && (lineVerb(p) === 'cast' || lineVerb(p) === 'act');
  }
  // The queued land's POSITION, from where it sits in the queue: '' = first (before every cast),
  // 'all' = after every cast, or the number of casts before it (`landat=<k>`). Only a land queued
  // AFTER a cast on a decision that offers positioned lines (`after`, stamped by queueCard) is
  // positioned at all; every other land is land-first, exactly as before.
  // A KAROO (bounce land) whose position the decision offers (engine Plan::land_slot <= -2, published
  // as `karoo_before`, 2026-10-22): the engine plays it AFTER the casts by default, and any earlier
  // point is a real line ("Selesnya Sanctuary, then Rest for the Weary for 8"). Its queue position IS
  // its timing: queued with casts still to come it encodes `landat=<k>` (k = the casts before it, 0 =
  // before every cast); queued after every cast it is the plain `land=` (the after-the-casts default).
  function karooPositionsOffered(decision, name) {
    return !!(decision && Array.isArray(decision.plans)
              && decision.plans.some(pl => pl && pl.land === name && pl.karoo_before));
  }
  // EVERY LAND's queue position is its timing (USER 2026-10-22: "The viewer should use exactly the order I
  // give. Same for everything else in the viewer."). It used to be read only on a decision that offered a
  // positioned line (`after`, stamped by queueCard on landfall turns) -- everywhere else "cast Sol Ring,
  // then play the Forest" encoded `land=` and the engine played the Forest first. The engine now matches
  // a legal position the menu does not enumerate against the land-first plan (TurnSolver::CheckLine) and
  // realises the queued position through the "**" side channel (sequenceTokens), so the line says where
  // the land goes on every deck. A land queued FIRST still encodes `land=`, exactly as it always did.
  function landPosition(plan, l) {
    if (l && l.karoo) {
      const i = plan.indexOf(l);
      const before = plan.slice(0, i).filter(isSpecCast).length;
      const later  = plan.slice(i + 1).filter(isSpecCast).length;
      return later === 0 ? '' : String(before);
    }
    if (!l) return '';
    const i = plan.indexOf(l);
    const before = plan.slice(0, i).filter(isSpecCast).length;
    const later  = plan.slice(i + 1).filter(isSpecCast).length;
    if (before === 0) return '';
    return later === 0 ? 'all' : String(before);
  }
  // A queued entry that is a cast/activation the land could come AFTER (not a land, pretap, LE discard).
  function isCastLike(p) { return p && p.kind !== 'land' && p.kind !== 'le' && p.kind !== 'pretap'; }

  // ---- THE HUMAN'S WHOLE SEQUENCE (the "**" --cast-order list, 2026-10-22) ---------------------
  // USER: "The viewer should use exactly the order I give. Same for everything else in the viewer." The
  // "*" list carried the queued casts and board activations, and only when the matched plan's actions
  // were exactly the queued names; the land, a Land's Edge discard, a sac-land cast, a retrace, Krenko's
  // tap, a declared sac-for-mana, a suspend, a convoke tap and a land-search crack all ran in canonical
  // passes of their own, and a plan holding one implicit action lost the activation order entirely.
  // This is the committing SEGMENT as the engine's TurnSolver::ApplyHumanSequencePin reads it: one token
  // per queued entry, IN QUEUE ORDER, `<verb>=<name>` with the LineSpec verb the entry writes (so the
  // engine can tell an `act=` of the Augur in play from a `cast=` of the copy in hand), `land=` /
  // `landtop=` for the drop and `le=` for one Land's Edge discard, and the manual taps' own `tap=` tokens
  // in place. The name is the plan ACTION's card -- the line value, except for the verbs whose value
  // names a TARGET (gyreturn / pod / ooze name the returned / fetched / exiled card; the action is the
  // source's).
  // ...and WHICH play the entry is, in every detail the viewer knows, so the engine never has to pick
  // between two same-named candidates (review 2026-10-22): `#<n>` the permanent the activation was
  // clicked on / the Equipment (srcNum), `@<n>` the equip host / the blinked creature / the
  // equip-all host, `*<n>` a blink's count, `+kicked` / `+adventure` a cast's mode. The engine
  // (TurnSolver::ApplyHumanSequencePin) matches on all of it, and REFUSES a step that still could mean
  // two different plays rather than guess. No MTG card name contains '#', '@' or '*'.
  // A MANA sac outlet's click (`sacMana`: Skirk Prospector, a Utopia Mycon's Saproling sac -- the engine's
  // `sac_mana`) is a PAYMENT, not a step (USER 2026-10-22): it writes no token (null, filtered out by
  // sequenceTokens) and counts as no step; its sacrifices pay for the casts that need them. The line
  // string keeps its `sacout=` (CheckLine matches the plan with the burst by it).
  function sequenceToken(p) {
    if (p && p.sacMana) return null;
    // A CYCLE is a step like any other (`cycle=<name>`); it always ENDS its segment (segmentParts), which is
    // where the engine runs a plan's cycle -- after the line's casts.
    if (p && p.kind === 'cycle') return 'cycle=' + p.name;
    if (isPreTap(p)) return preTapToken(p);
    if (p.kind === 'land') return (p.fromTop ? 'landtop=' : 'land=') + p.name;
    if (p.kind === 'le') return 'le=' + p.name;
    if (p.kind === 'vial') return 'vial=' + p.name;
    if (p.kind === 'retrace') return 'retrace=' + p.name;
    const v = lineVerb(p);
    const card = (v === 'gyreturn' || v === 'pod' || v === 'ooze') ? (p.src || p.name) : p.name;
    // An activated untapper (untapact= / untaploop=) carries its source and target as untapSrc /
    // untapTarget -- the same `#src@target` its line token writes (untapIds).
    const target = p.target || p.blinkTarget || p.untapTarget || (v === 'equipallfree' ? p.hostNum : 0);
    const srcNum = p.untapSrc || p.srcNum || 0;
    return v + '=' + card
         + (srcNum ? '#' + srcNum : '')
         + (target ? '@' + target : '')
         + (v === 'blink' && p.blinkCount > 0 ? '*' + p.blinkCount : '')
         + (p.kicked ? '+kicked' : '')
         + (p.adventure ? '+adventure' : '');
  }
  function sequenceTokens(seg) { return (seg || []).map(sequenceToken).filter(t => t != null); }

  // ONE-AT-A-TIME PICKS IN THE ORDER THEY WERE CLICKED (cleanup discard, London bottoming; USER
  // 2026-10-22: "The viewer should use exactly the order I give."). The engine asks one decision per
  // card and reads a HAND INDEX, against a hand the previous pick already shrank (the card is erased in
  // place, so the rest keep their relative order). `picks` are ORIGINAL hand indices in click order;
  // the result re-bases each onto the hand as it stands at its own decision: an earlier pick at a LOWER
  // index shifts it down by one. (The viewer used to sort the picks into descending index order, which
  // needs no re-basing but applies them in an order nobody chose.)
  function shrinkingIndices(picks) {
    const out = [];
    (picks || []).forEach((p, k) => {
      let shift = 0;
      for (let j = 0; j < k; ++j) { if (picks[j] < p) { ++shift; } }
      out.push(p - shift);
    });
    return out;
  }
  // How many STEPS a segment holds (every entry but a manual tap, which is a payment, not a step). The
  // viewer pins a segment of two or more: a single step has no order to keep.
  function sequenceSteps(seg) { return (seg || []).filter(p => !isPreTap(p) && !p.sacMana).length; }
  // Does a committing segment carry a "**" pin? Two or more steps (an order to keep), or a manual tap
  // (a payment with a position). index.html adds the continuation's `need=` on top.
  function sequencePinned(seg) { return sequenceSteps(seg) >= 2 || (seg || []).some(isPreTap); }
  // The line a segment is VALIDATED with: encodeLine's line plus, when the segment is pinned, its "**"
  // sequence as `seq=<step>|<step>|...` -- so the engine previews the commit in exactly the order it will
  // apply (src/main.cpp ValidateHumanSequence) and rejects a step that cannot be done where it was
  // queued, with its reason, instead of the commit dropping it. Tokens never contain ';' or '|'.
  // `need` (optional): the continuation's `need=<COLOURS>` token the commit's pin carries -- passed here
  // too, so the sequence the validation previews is the pin the commit sends, token for token.
  function validationLine(seg, need) {
    const line = encodeLine(seg || []);
    if (!(sequencePinned(seg) || need)) { return line; }
    const toks = sequenceTokens(seg);
    return line + ';seq=' + (need ? toks.concat([need]) : toks).join('|');
  }

  // THE LIBRARY TOP AS A LAND SOURCE (engine Plan::land_from_top, Courser of Kruphix, 2026-10-15).
  // The engine publishes me.library_top ONLY while a revealer is ours; `playable_land` says the top may
  // be played as the land drop (a land under a play-from-top permanent). The cap on queueing it is 1 --
  // it is one card -- and it is NEVER counted among the hand copies (castableCount reads the hand).
  function topLandOffered(decision, name) {
    const t = decision && decision.me && decision.me.library_top;
    return !!(t && t.playable_land && (name == null || t.name === name));
  }

  function encodeLine(plan) {
    // A land queued AFTER every cast encodes as `landafter=` -- "cast Rampaging Baloths, then play the
    // Forest" -- on every deck now (landPosition: the queue position is the timing, 2026-10-22).
    // A land queued from the library TOP (`fromTop`, Courser) encodes as `landtop=` -- `land=` keeps
    // meaning a HAND copy, so every saved reference replays unchanged -- and its timing rides the
    // `landat=all` modifier (`landafter=` names a hand copy only).
    // A land queued BETWEEN two casts (`after`, with a cast still to come) encodes its position as
    // `landat=<k>` -- k = the casts before it, in click order (CheckLine maps k onto the engine's
    // Plan::land_slot, which counts landfall casts) -- "cast Khalni, play the Forest, cast Rampant
    // Growth with its mana". After every cast it stays `landafter=` (hand) / `landat=all` (top).
    const parts = []; const l = planLand(plan); const pos = landPosition(plan, l);
    if (l && l.fromTop) { parts.push('landtop=' + l.name); if (pos) parts.push('landat=' + pos); }
    else if (l && pos === 'all') parts.push('landafter=' + l.name);
    else if (l) { parts.push('land=' + l.name); if (pos) parts.push('landat=' + pos); }
    // Taps first: they are what the rest of the line is paid from.
    for (const p of plan) if (isPreTap(p)) parts.push(preTapToken(p));
    for (const p of plan) if (p.kind !== 'land' && p.kind !== 'le' && p.kind !== 'vial' && p.kind !== 'retrace' && !isPreTap(p) && (lineVerb(p) === 'cast' || lineVerb(p) === 'act')) parts.push(lineVerb(p) + '=' + p.name);
    for (const p of plan) if (p.kind === 'vial') parts.push('vial=' + p.name);
    for (const p of plan) if (p.kind === 'retrace') parts.push('retrace=' + p.name);
    // ADVENTURE (CR 715) rides ALONGSIDE its own `cast=`, not instead of it -- the adventure half is
    // genuinely a cast of that hand card, so the entry keeps its ordinary kind and the cast loop
    // above has already emitted `cast=<parent>`. This token only says WHICH MODE, which is why it is
    // a flag on a normal cast entry rather than a kind of its own like channel/suspend (those are
    // not casts at all). The engine compares the adventure set unconditionally, so omitting the
    // token means "cast the creature".
    for (const p of plan) if (p.adventure) parts.push('adventure=' + p.name);
    // KICKER (CR 702.33) rides alongside its own `cast=` for exactly the adventure reason above:
    // kicking IS a cast of that hand card, just a dearer one, so the cast loop has already emitted
    // `cast=<name>` and this token only says WHICH MODE. Compared unconditionally by the engine, so
    // omitting it means "do not kick".
    for (const p of plan) if (p.kicked) parts.push('kicked=' + p.name);
    // Board activations that are neither a hand cast nor a pass need their own verb -- a line made
    // up ONLY of them used to encode as 'pass' (CheckLine stage 0). MODE_VERBS name the MODE INT,
    // every other verb names a card.
    for (const p of plan) {
      if (isPreTap(p)) continue;               // already emitted above, and it has no verb
      const v = lineVerb(p);
      if (v === 'cast' || v === 'act') continue;   // emitted in clicked order with the casts
      parts.push(v + '=' + (MODE_VERBS[v] ? String(p.mode)
                            : NUM_VERBS[v] ? String(p.hostNum || 0)
                            : p.name)
                 + (v === 'equip' ? equipIds(p) : v === 'blink' ? blinkIds(p)
                    : (v === 'untapact' || v === 'untaploop') ? untapIds(p) : ''));
    }
    const n = leCount(plan); if (n > 0) parts.push('landsedge=' + n);
    return parts.length ? parts.join(';') : 'pass';
  }

  // KICKER (CR 702.33 -- Saproling Migration): the pure state transition behind index.html's
  // "⊕ kicked" badge. Same split queueCard uses below -- the GUI keeps the guards and the hint text,
  // the plan edit lives HERE so a headless check can drive it (test/viewer_linebuild_check.js).
  // That split is the point: the kicker shipped unusable on 2026-10-16 because its only missing
  // piece was a GUI branch, and nothing in this repo's gates drives index.html.
  //
  // Kicking is NOT a second way to play the card, the way channel / suspend / adventure are: it is
  // the SAME spell at a dearer cost (an ADDITIONAL cost, CR 601.2b -- it adds to the total, it does
  // not replace it). So the badge toggles the kicker onto a cast rather than appending one:
  //   * nothing of this name queued -> append a kicked cast, so one click works from an empty plan
  //   * an UNKICKED queued cast     -> promote the LAST one IN PLACE ("kick the one I just queued")
  //   * every queued cast kicked    -> WRAP to none
  // Appending ONLY when nothing of this name is queued is what makes the gesture safe. The two modes
  // share a hand slot, so with one copy in hand `cast=X;cast=X;kicked=X` matches no enumerated plan
  // and the commit dead-ends with nothing for the human to act on; this cannot build that line.
  //
  // The last branch WRAPS rather than demoting one, which is not a detail: demoting the last kicked
  // copy would leave the NEXT click re-promoting that very entry, so with two copies queued the plan
  // oscillated between 2 and 1 kicked and an all-kicked line could never get back to unkicked. The
  // count is therefore the gesture -- 0 -> 1 -> 2 -> 0 over n queued casts -- which is this GUI's
  // standing idiom for a repeated click on one affordance, and reads off the badge's "xN".
  // Returns { plan, action }, action being 'kicked' | 'unkicked' | 'queued' for the GUI's hint.
  function toggleKicker(plan, name, kind) {
    const idxs = plan.map((p, i) => ({ p, i }))
                     .filter(x => x.p.name === name && isSpecCast(x.p) && lineVerb(x.p) === 'cast')
                     .map(x => x.i);
    if (!idxs.length) { plan.push({ name, kind, kicked: true }); return { plan, action: 'queued' }; }
    const un = idxs.filter(i => !plan[i].kicked);
    if (un.length) { plan[un[un.length - 1]].kicked = true; return { plan, action: 'kicked' }; }
    idxs.forEach(i => { delete plan[i].kicked; });
    return { plan, action: 'unkicked' };
  }

  // Queue (or, for a land, toggle) one card into the plan. Returns the resulting plan array (the
  // land branch reassigns it, so callers must use the return value). The GUI's own main-phase /
  // busy guard stays in index.html's wrapper; this is the pure state transition.
  // `fromTop` (Courser of Kruphix): the land clicked is the revealed library TOP, not a hand copy. A
  // top Forest and a hand Forest are DIFFERENT entries (the engine branches them: different library,
  // different hand), so the toggle below matches on (name, source), and the top's cap is the single
  // card itself (topLandOffered), never the hand count.
  function queueCard(decision, plan, name, kind, fromTop) {
    if (kind === 'land') {
      const lands = planLands(plan);
      const top = !!fromTop;
      const same = lands.filter(p => p.name === name && !!p.fromTop === top);
      const cap = top ? (topLandOffered(decision, name) ? 1 : 0) : castableCount(decision, name);
      // A top that is not a playable land right now (no play-from-top permanent, a spell on top, or
      // a stale click after the top changed) queues NOTHING -- not even as a "this land instead".
      if (top && cap === 0) return plan;
      // With a SPARE land drop (a Scale the Heights bonus) and a spare copy in hand, another land is
      // an ADDITION, not a correction -- replacing the queued one silently ate the extra drop (viewer
      // issue #7). At the ordinary one drop the old rules stand: the same land toggles off, a
      // different one means "actually, this land instead". Removing one of two queued lands is the
      // plan chip's ✕ / drag-back, as for any other queued entry.
      // The land's TIMING follows the click order: queued after a cast, on a decision that offers
      // land-after lines (offersLandAfter), it is played AFTER the casts (`landafter=`). A deck that
      // never offers one pushes exactly the {name, kind} entry it always did.
      const landEntry = () => {
        const e = (offersLandAfter(decision) && plan.some(isCastLike))
          ? { name, kind, after: true } : { name, kind };
        if (top) e.fromTop = true;   // absent for a hand land: the entry is exactly what it always was
        if (karooPositionsOffered(decision, name)) e.karoo = true;   // its queue position is its timing
        return e;
      };
      if (lands.length < landDropsLeft(decision) && same.length < cap) {
        plan.push(landEntry());
      } else if (same.length) {
        const cur = same[same.length - 1];
        plan = plan.filter(p => p !== cur);                                    // toggle that land off
      } else { plan = plan.filter(p => p.kind !== 'land'); plan.push(landEntry()); }
      // A queued land drop consumes a land that can no longer feed Land's Edge -> trim excess.
      while (leCount(plan) > leMax(decision, plan)) { const i = plan.map(p => p.kind).lastIndexOf('le'); if (i < 0) break; plan.splice(i, 1); }
    } else {
      // A SUSPEND-ONLY card (Lotus Bloom: no mana cost; the engine stamps `suspend_only` on the hand
      // card) cannot be cast, only suspended (USER 2026-10-22), so every play of its thumb -- click,
      // double-click, drag -- queues the SUSPEND, exactly as its badge does. It used to queue a plain
      // `cast=`, which the "**" sequence could not match to the card's only action.
      const hc = ((decision && decision.me && decision.me.hand) || []).find(c => c.name === name);
      if (hc && hc.suspend_only) {
        const n = plan.filter(p => p.kind === 'suspend' && p.name === name).length;
        if (n < castableCount(decision, name)) plan.push({ name, kind: 'suspend' });
        return plan;
      }
      if (plannedCount(plan, name) < castableCount(decision, name)) plan.push({ name, kind });
    }
    return plan;
  }

  // Give every queued entry the m_number of the hand copy it will become, so a card queued THIS
  // turn is itself a drop target for a same-turn attachment: drag Rancor onto the Bogle you are
  // casting, or Wild Growth onto the land you are PLAYING. Mutates `plan` in place.
  //
  // Two things here are the fix rather than tidying, and both are why it lives in this file:
  //   * it walks the WHOLE plan, not just the entry queueCard appended. Queueing a land can REPLACE
  //     a previously queued one, which leaves the plan the same LENGTH -- and the caller's old
  //     "only if the plan grew" guard therefore never stamped that land.
  //   * it includes LANDS. They were excluded outright, so a land drop could never host an Aura
  //     queued in the same line: the only way to enchant a land you were playing was to commit the
  //     line and enchant it in the NEXT one (USER, 2026-09-04: "Technically I can commit line and
  //     then add it, but it would be easier if I could do it in one step").
  // The engine always offered the line -- `land=Aether Hub;cast=Wild Growth` enumerates with
  // enchant_target = the land's own m_number, which is exactly the number its hand copy carries --
  // so this was a missing affordance in the GUI, never a missing plan.
  function stampPlanNums(decision, plan) {
    const hand = (((decision || {}).me || {}).hand) || [];
    const used = new Set(plan.filter(p => p.num != null).map(p => p.num));
    plan.forEach(p => {
      // 'pretap' names a permanent ALREADY on the battlefield, so its `num` is a board id, not a
      // hand card's -- it is never stamped and never consumes one (it also always arrives stamped).
      if (p.num != null || p.kind === 'le' || p.kind === 'pretap') return;
      // A library-TOP land (Courser) is the revealed top card, never a hand copy: its number is the
      // engine's me.library_top.number, so a land Aura can still be dropped on it in the same line.
      if (p.fromTop) {
        const t = (((decision || {}).me || {}).library_top) || null;
        if (t && t.name === p.name && !used.has(t.number)) { p.num = t.number; used.add(t.number); }
        return;
      }
      const hc = hand.find(c => c.name === p.name && !used.has(c.num));
      if (hc) { p.num = hc.num; used.add(hc.num); }
    });
    return plan;
  }

  // ---- Choose-variant dimension walk (the multi-sub-decision picker) -------------------------
  // Lives here, not inline in index.html, for the same reason queueCard does: it decides how many
  // dialogs a committed line produces, and that was un-testable while it sat in the page. Viewer
  // issue #13 was reported as "Crop Rotation + Sylvan Scrying only let me choose ONE land" -- the
  // engine offers both dimensions (verified: 144 variants, 12 lands each), so any regression here
  // silently eats a human decision while every engine-side check stays green.
  // `free` (Maelstrom Archangel pay-vs-bank) is asked FIRST among the per-spell dimensions: it
  // decides how much mana the rest of the line has, so every later sub-decision reads in that
  // context. `modal` (Unite the Coalition's mode split) sits where the old generic `x` sub used to.
  // `sacrifice` (Natural Order's additional cost) is NO LONGER EMITTED as a sub (2026-09-17) — the
  // victim is a RESOLUTION-time board click now, like a loyalty ability's target below. It used to
  // sit at 0.75, ahead of `tutor`, on the theory that the victim gates the fetch ("a sacrificed
  // Worldspine Wurm shuffles itself back into the library and only then is a legal target"). That
  // coupling was never real: CollectActions builds the tutor candidate list ONCE and crosses it with
  // the victim list, so every victim sees the same fetch options. Asking at resolution instead is
  // what lets you sacrifice a creature THE SAME LINE CAST — the queue-time set was a snapshot of the
  // pre-plan board. The key stays in the table below only so a stale saved line does not reorder.
  // `activations` (Call of the Wild / a Minotaur pump's repeat count) sits where the old generic `x`
  // sub did; it is that action's own count, coupled to nothing.
  // `equip` (which creature an Equipment attaches to) and `jitte` (which counter mode) sit with the
  // other per-action target picks.
  // `bestow` sits just ahead of `enchant`: the mode decides whether an enchant target is asked at
  // all (the creature mode has none), so asking the host first would offer a dimension that the
  // other mode does not have (the gating reason `sacrifice` USED to be asked before `tutor`).
  // `replicate` (how many extra token copies a Sliver spell pays for) is asked LAST among the
  // per-spell dimensions for the same reason `free` is asked first: it is the one that consumes
  // whatever mana the rest of the line left, so every earlier pick reads in a fixed context and the
  // count is chosen against what is actually spare.
  // `loyalty` (WHICH loyalty ability of a planeswalker) sits just ahead of the attachment picks for
  // the same gating reason `bestow` does: it decides what the rest of the line even means, and its
  // TARGET is not asked here at all (a loyalty ability that targets is board-clicked at resolution).
  // It used to be unlisted, i.e. priority 9 -- behind `enchant` at 1.5 -- and Oko's +1 target rode
  // the `enchant` key, so the dialog asked "Oko, Thief of Crowns ->? / leave it unattached /
  // Faeburrow Elder / Deathrite Shaman": an attach question standing in for the ability choice
  // (2026-09-02 item 2). The engine no longer emits that sub, and the ability is asked first.
  // `devour` (how many creatures a Mycoloth-class cast eats as it enters) sits with the other
  // per-spell counts, just ahead of `replicate`: it consumes BODIES rather than mana, so unlike
  // replicate it does not want to be read against what the rest of the line left spare -- and it
  // decides how many victims the resolution-time board click will then ask for.
  // `color` (which colour a sac-for-mana outlet floats) is asked LAST of all, behind every count.
  // It is the only dimension that is purely a MEANS: the colour matters solely because some cast in
  // the line consumes it, so every other dimension -- what is cast, what X is, how many bodies are
  // eaten -- determines what the colour needs to be, and asking it first would be asking the human
  // to fix a variable whose constraint has not been stated yet. Asked last, it is usually already
  // collapsed to a single choice by the picks above and never surfaces at all, which is the point:
  // at candidate-B Fungus seed 12 T4 this axis alone was 76% of the emitted plan menu (151 of 200
  // slots, one play appearing 18 times) purely because it had no dimension to live in.
  // `pumptarget` (WHICH creature Kessig Wolf Run's "+X/+0" lands on) is a per-action target pick like
  // `equip`, so it is asked before that pump's `x`.
  const SUBKIND_PRI = { face: -1, fetch: 0, free: 0.5, sacrifice: 0.75, tutor: 1, loyalty: 1.3,
                        bestow: 1.4, enchant: 1.5, equip: 1.5, pumptarget: 1.5, jitte: 1.6, x: 2, activations: 2,
                        modal: 2.5, soulfire: 3, crackle: 4, splice: 5, devour: 5.5, replicate: 6,
                        color: 7, pitch: 9.5 };   // pitch: Fury's evoke-exile card, after its `evoke` mode (9)
  function subKindPri(k) { return SUBKIND_PRI[k] === undefined ? 9 : SUBKIND_PRI[k]; }
  function subOf(v, key) { return (v.subs || []).filter(s => s.key === key)[0]; }
  function choiceOf(v, key) { const s = subOf(v, key); return s ? s.choice : '—'; }

  // The next dimension to ask about, given the variants still in play: the highest-priority one
  // that still has MORE THAN ONE distinct choice. null => nothing left to disambiguate.
  function nextDimension(remaining) {
    const keys = [];
    remaining.forEach(x => (x.subs || []).forEach(s => {
      if (!keys.some(k => k.key === s.key)) keys.push({ key: s.key, kind: s.kind });
    }));
    keys.sort((a, b) => subKindPri(a.kind) - subKindPri(b.kind));
    for (const k of keys) {
      const seen = [];
      remaining.forEach(x => {
        const c = choiceOf(x, k.key);
        // `num` (the m_number the choice names, 0 when it names no board object) rides along so the
        // caller can auto-resolve a dragged attach target by IDENTITY -- two same-named creatures
        // produce two choices whose display strings differ only by the engine's " #k" suffix.
        if (!seen.some(z => z.choice === c)) { const s = subOf(x, k.key); seen.push({ choice: c, card: s ? s.card : '', num: s ? (s.num || 0) : 0 }); }
        // `suggested` (enchant only): the engine's Aura host ranking picks this host for at least one
        // remaining variant's plan class (MarkSuggestedAuraHosts). A MARK, never an order: the
        // choices keep their first-seen order, which reference replays depend on.
        const s2 = subOf(x, k.key);
        if (s2 && s2.suggested) { const z = seen.find(z => z.choice === c); if (z) z.suggested = true; }
      });
      if (seen.length > 1) return { dim: k, choices: seen };
    }
    return null;
  }
  function filterByChoice(remaining, key, choice) { return remaining.filter(x => choiceOf(x, key) === choice); }

  // How many dimensions REMAIN to be asked (including the current one) -- drives the "step N of M"
  // counter, which is what makes two identical-looking 12-land grids tellable apart.
  function dimensionsRemaining(remaining) {
    let n = 0, cur = remaining;
    while (n < 16) {
      const d = nextDimension(cur);
      if (!d) break;
      n++;
      cur = filterByChoice(cur, d.dim.key, d.choices[0].choice);
    }
    return n;
  }

  return { planLand, planLands, landDropsLeft, handCounts, stagedCounts, castableCount, plannedCount,
           leCount, leMax, encodeLine, encodeSegments, dropFirstSegment, queueCard, isSacOut, lineVerb,
           toggleKicker,
           offersLandAfter, topLandOffered, landPosition, karooPositionsOffered,
           sequenceToken, sequenceTokens, sequenceSteps, sequencePinned, validationLine, shrinkingIndices,
           stampPlanNums, isPreTap, preTapToken,
           // segmentParts is exported (not just encodeSegments) because applyAccepted needs the
           // committing segment's ENTRIES, not its encoded string, to scope the full-order pin.
           segmentParts, isDeferred, repeatBlock, loopBlock, segmentLoopable,
           untapNeed, untapNeedToken,
           fusedInvestigateEntries, removeFusedAt, CLUE_TOKEN,
           nextDimension, filterByChoice, dimensionsRemaining, choiceOf, subOf };
});
