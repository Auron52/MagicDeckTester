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
  // 'le' entries are Land's Edge discards, not casts -- exclude them from the cast count.
  function plannedCount(plan, name) {
    return plan.filter(p => p.name === name && p.kind !== 'land' && p.kind !== 'le').length;
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
  //   equip=      attach an Equipment ALREADY IN PLAY to a creature            (LineSpec::equips)
  //   attachall=  Balan's "attach all Equipment"                               (LineSpec::attach_all)
  //   sfput=      Stoneforge Mystic's put-from-hand (names the EQUIPMENT)      (LineSpec::sf_puts)
  //   jittemode=  Umezawa's Jitte counter-spend (names the MODE INT)           (LineSpec::jitte_modes)
  function lineVerb(p) {
    if (p.kind !== 'activate') return 'cast';
    return p.verb || (p.sacout ? 'sacout' : 'cast');
  }

  // A multi-land plan (a Scale the Heights bonus drop) as the SEGMENTS the engine can accept: one
  // land-only segment per extra drop, then the final land WITH every cast. Lands go first so the
  // casts are paid off the full set of lands the human meant to play -- committing a cast segment
  // before its second land is what made "play two lands, then cast" unreachable. A single-land plan
  // returns exactly one segment == encodeLine(plan), so nothing else changes.
  function encodeSegments(plan) {
    const lands = planLands(plan);
    if (lands.length <= 1) { return [encodeLine(plan)]; }
    const rest = plan.filter(p => p.kind !== 'land' || p === lands[lands.length - 1]);
    return lands.slice(0, -1).map(l => 'land=' + l.name).concat([encodeLine(rest)]);
  }

  function encodeLine(plan) {
    const parts = []; const l = planLand(plan); if (l) parts.push('land=' + l.name);
    for (const p of plan) if (p.kind !== 'land' && p.kind !== 'le' && p.kind !== 'vial' && p.kind !== 'retrace' && lineVerb(p) === 'cast') parts.push('cast=' + p.name);
    for (const p of plan) if (p.kind === 'vial') parts.push('vial=' + p.name);
    for (const p of plan) if (p.kind === 'retrace') parts.push('retrace=' + p.name);
    // Board activations that are neither a hand cast nor a pass need their own verb -- a line made
    // up ONLY of them used to encode as 'pass' (CheckLine stage 0). jittemode= names the MODE INT,
    // every other verb names a card.
    for (const p of plan) {
      const v = lineVerb(p);
      if (v === 'cast') continue;
      parts.push(v + '=' + (v === 'jittemode' ? String(p.mode) : p.name));
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
  const SUBKIND_PRI = { face: -1, fetch: 0, free: 0.5, sacrifice: 0.75, tutor: 1, enchant: 1.5,
                        equip: 1.5, jitte: 1.6, x: 2, activations: 2, modal: 2.5, soulfire: 3,
                        crackle: 4, splice: 5 };
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
        if (!seen.some(z => z.choice === c)) { const s = subOf(x, k.key); seen.push({ choice: c, card: s ? s.card : '' }); }
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
           leCount, leMax, encodeLine, encodeSegments, queueCard, isSacOut, lineVerb,
           nextDimension, filterByChoice, dimensionsRemaining, choiceOf, subOf };
});
