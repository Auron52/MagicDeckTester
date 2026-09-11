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
  //   cast=       Krenko's tap, a loyalty ability, Call of the Wild            (orderNames match)
  //   sacout=     Skirk Prospector / Siege-Gang / Pashalik                     (LineSpec::sac_outlets)
  //   equip=      attach an Equipment to a creature, "<name>[#src][@host]"     (LineSpec::equips)
  //   attachall=  Balan's "attach all Equipment"                               (LineSpec::attach_all)
  //   sfput=      Stoneforge Mystic's put-from-hand (names the EQUIPMENT)      (LineSpec::sf_puts)
  //   jittemode=  Umezawa's Jitte counter-spend (names the MODE INT)           (LineSpec::jitte_modes)
  //   gyexile=    Deathrite Shaman's graveyard exile (names the MODE INT)      (LineSpec::gy_exiles)
  //   gyreturn=   Haven's sac-to-rebuy (names the RETURNED CARD, not the land)  (LineSpec::gy_returns)
  //   pod=        Birthing Pod's activation (names the FETCH; "(no fetch)" = sac only;
  //               the victim is picked via the choose-variant dialog)          (LineSpec::pods)
  //   ooze=       Scavenging Ooze's exile (names the EXILED graveyard card)    (LineSpec::ooze_exiles)
  //   channel=    Twinshot Sniper's from-HAND channel ability (names the card) (LineSpec::channels)
  //   suspend=    Lotus Bloom's from-HAND Suspend (names the card)             (LineSpec::suspends)
  //   blink=      Emiel / Eldrazi Displacer, "<outlet>[@<target m_number>]"    (LineSpec::blinks)
  // The verbs whose value is a MODE INT rather than a card name. Kept as a set so encodeLine has one
  // rule instead of a growing `v === 'jittemode' || v === 'gyexile' || ...` chain.
  const MODE_VERBS = { jittemode: true, gyexile: true };
  function lineVerb(p) {
    // A CHANNEL entry is the one non-'activate' kind with its own verb: its source is a card in HAND
    // (so it is not a board activation) but it is not a cast either -- "{1}{R}, Discard this card"
    // plays the same card a different way, and `cast=` cannot say which.
    if (p.kind === 'channel') return 'channel';
    // ... and SUSPEND is the second: exiling the card from hand with time counters is an
    // alternative to casting it (CR 702.61a), and `cast=` cannot say which of the two was meant.
    if (p.kind === 'suspend') return 'suspend';
    if (p.kind !== 'activate') return 'cast';
    return p.verb || (p.sacout ? 'sacout' : 'cast');
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

  function segmentParts(plan) {
    const seen = {}, main = [], extras = [];
    // Everything from the first deferred entry onward is held back and partitioned separately, so
    // a deferred entry carries the whole block behind it (a macro iteration), not just itself.
    //
    // The scan starts at 1, NOT 0, and that is load-bearing rather than tidy: a deferred entry at
    // position 0 is already the head of the segment being built, so nothing precedes it to split
    // from. Scanning from 0 cut there, produced an EMPTY first segment, and dropFirstSegment --
    // which peels by removing exactly the entries segmentParts()[0] names -- then removed nothing
    // and the commit chain spun forever on the second macro iteration.
    let cut = -1;
    for (let i = 1; i < plan.length; ++i) { if (isDeferred(plan[i])) { cut = i; break; } }
    const head = cut < 0 ? plan : plan.slice(0, cut);
    const tail = cut < 0 ? []   : plan.slice(cut);
    for (const p of head) {
      const k = (p.kind === 'activate' && p.repeatable) ? (p.src || p.name) : null;
      if (k && seen[k]) { extras.push([p]); continue; }
      if (k) { seen[k] = true; }
      main.push(p);
    }
    const lands = planLands(main);
    const parts = lands.length <= 1
      ? [main]
      : lands.slice(0, -1).map(l => [l])
             .concat([main.filter(p => p.kind !== 'land' || p === lands[lands.length - 1])]);
    // The tail recurses AS-IS -- no copy, no flag clearing. Its own first entry is the one that
    // caused the cut, and the scan above already ignores position 0, so the recursion makes
    // progress on its own. Not copying is what keeps every returned entry IDENTICAL (by object
    // identity) to the one in `plan`, which is the contract dropFirstSegment's filter relies on.
    const rest = tail.length ? segmentParts(tail) : [];
    return parts.concat(extras).concat(rest);
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
                       retrace: true, channel: true, suspend: true };
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

  // ---- MANUAL TAP/PAY (docs/design/viewer-manual-tap-pay.md) ---------------------------------
  // A 'pretap' entry is a board mana source the human tapped BY HAND for a specific face, so the
  // engine's payment spends that unit instead of allocating one itself. It is not a play: it never
  // enters the cast multiset, it takes no plan action, and a line with none of them encodes exactly
  // as it always did. `num` is the PERMANENT's m_number (which copy), `color` one of W U B R G C.
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

  function encodeLine(plan) {
    const parts = []; const l = planLand(plan); if (l) parts.push('land=' + l.name);
    // Taps first: they are what the rest of the line is paid from.
    for (const p of plan) if (isPreTap(p)) parts.push(preTapToken(p));
    for (const p of plan) if (p.kind !== 'land' && p.kind !== 'le' && p.kind !== 'vial' && p.kind !== 'retrace' && !isPreTap(p) && lineVerb(p) === 'cast') parts.push('cast=' + p.name);
    for (const p of plan) if (p.kind === 'vial') parts.push('vial=' + p.name);
    for (const p of plan) if (p.kind === 'retrace') parts.push('retrace=' + p.name);
    // Board activations that are neither a hand cast nor a pass need their own verb -- a line made
    // up ONLY of them used to encode as 'pass' (CheckLine stage 0). MODE_VERBS name the MODE INT,
    // every other verb names a card.
    for (const p of plan) {
      if (isPreTap(p)) continue;               // already emitted above, and it has no verb
      const v = lineVerb(p);
      if (v === 'cast') continue;
      parts.push(v + '=' + (MODE_VERBS[v] ? String(p.mode) : p.name)
                 + (v === 'equip' ? equipIds(p) : v === 'blink' ? blinkIds(p) : ''));
    }
    const n = leCount(plan); if (n > 0) parts.push('landsedge=' + n);
    return parts.length ? parts.join(';') : 'pass';
  }

  // Queue (or, for a land, toggle) one card into the plan. Returns the resulting plan array (the
  // land branch reassigns it, so callers must use the return value). The GUI's own main-phase /
  // busy guard stays in index.html's wrapper; this is the pure state transition.
  function queueCard(decision, plan, name, kind) {
    if (kind === 'land') {
      const lands = planLands(plan);
      const same = lands.filter(p => p.name === name);
      // With a SPARE land drop (a Scale the Heights bonus) and a spare copy in hand, another land is
      // an ADDITION, not a correction -- replacing the queued one silently ate the extra drop (viewer
      // issue #7). At the ordinary one drop the old rules stand: the same land toggles off, a
      // different one means "actually, this land instead". Removing one of two queued lands is the
      // plan chip's ✕ / drag-back, as for any other queued entry.
      if (lands.length < landDropsLeft(decision) && same.length < castableCount(decision, name)) {
        plan.push({ name, kind });
      } else if (same.length) {
        const cur = same[same.length - 1];
        plan = plan.filter(p => p !== cur);                                    // toggle that land off
      } else { plan = plan.filter(p => p.kind !== 'land'); plan.push({ name, kind }); }
      // A queued land drop consumes a land that can no longer feed Land's Edge -> trim excess.
      while (leCount(plan) > leMax(decision, plan)) { const i = plan.map(p => p.kind).lastIndexOf('le'); if (i < 0) break; plan.splice(i, 1); }
    } else {
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
  // `sacrifice` (Natural Order's additional cost) is asked BEFORE `tutor`: the victim gates the fetch
  // — a sacrificed Worldspine Wurm shuffles itself back into the library and only then is a legal
  // target — so asking the target first would offer a card whose availability isn't decided yet.
  // `activations` (Call of the Wild / a Minotaur pump's repeat count) sits where the old generic `x`
  // sub did; it is that action's own count, coupled to nothing.
  // `equip` (which creature an Equipment attaches to) and `jitte` (which counter mode) sit with the
  // other per-action target picks.
  // `bestow` sits just ahead of `enchant`: the mode decides whether an enchant target is asked at
  // all (the creature mode has none), so asking the host first would offer a dimension that the
  // other mode does not have -- the same gating reason `sacrifice` is asked before `tutor`.
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
  const SUBKIND_PRI = { face: -1, fetch: 0, free: 0.5, sacrifice: 0.75, tutor: 1, loyalty: 1.3,
                        bestow: 1.4, enchant: 1.5, equip: 1.5, jitte: 1.6, x: 2, activations: 2,
                        modal: 2.5, soulfire: 3, crackle: 4, splice: 5, replicate: 6 };
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
           stampPlanNums, isPreTap, preTapToken,
           // segmentParts is exported (not just encodeSegments) because applyAccepted needs the
           // committing segment's ENTRIES, not its encoded string, to scope the full-order pin.
           segmentParts, isDeferred, repeatBlock, loopBlock, segmentLoopable,
           untapNeed, untapNeedToken,
           fusedInvestigateEntries, removeFusedAt, CLUE_TOKEN,
           nextDimension, filterByChoice, dimensionsRemaining, choiceOf, subOf };
});
