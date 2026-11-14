# Boars analyze-deck: handoff to Codex (2026-11-14, ~12:55 UTC)

Handed over from Claude session e3822f9b at the USER's request. Read this file first, then
`docs/design/boars-analysis-checkpoint.md` (the full running log) and the repo `CLAUDE.md`, which holds every rule. The
Codex notes there cover waiters and timers.

## Where things are

* **Main worktree:** `/workspaces/MagicDeckTester/.claude/worktrees/boars`, branch `boars-analysis`. All commits are LOCAL;
  NOTHING has been pushed this session. The shared branch is `phase-1-2-deck-analyzer`, pushed at most once per 4 h, by the
  parent session only. A push needs, in order:
  1. `scripts/check_references_safe.py`;
  2. a rebase, never a merge;
  3. a rebuild + smoke + `test/check_gt_logs.py`;
  4. a background `gh run watch <id> --interval 60`.
* **Integrated on `boars-analysis` (oldest first):**

  | Commit | Change |
  |---|---|
  | `74d8e579d` | viewer fetch-line fix |
  | `43afd29fd` | one fetch-target list for both routes |
  | `4a59c17f6` | land-drop fetch width = top-1 of the shared order (`DecisionProvider::OnPlayFetchWidth`) |
  | `963b0858f` | USER Boars cast order ON |
  | `ba5d932b0` | Flinthoof site-9 haste |
  | `94f435fee` | Cyclone keeps its own width (0) |
  | `c8004bd60` | payment fixes |
  | `080665c27` | human-play menu/display |
  | `151d9b9b6` | sweep record |

  The commits after that are docs only, the last being this handoff.
* **Side branches (worktrees under `.claude/worktrees/`):**
  * `boars-rank`: fetch-ranking fix, NOT integrated yet. Commits `fb58227ef`, `34483bf24`, `23e2e67db` (the current rule).
    Lever `MTG_FETCH_RANK_PIPS` (heurarm slot), default OFF until its A/B passes. Brief and handoff:
    `.claude/worktrees/boars-rank/logs/boars-research/rank-{brief,handoff}.md`. The handoff holds every USER fetch ruling
    VERBATIM.
  * `boars-d0haste`: `53281d211` WIP. A greedy self-haste rider for the depth-0 runner (`MTG_GREEDY_SELF_HASTE`): the d0
    runner casts Flinthoof and keeps {R} for its haste (from d0 gi92, seed 95093). Not integrated.
  * `boars-haste` and `boars-pay` are already integrated. They live NESTED at
    `.claude/worktrees/boars/.claude/worktrees/boars-{haste,pay}` (a path mistake). Stray brief dirs are at
    `/workspaces/MagicDeckTester/.claude/worktrees/boars-{haste,pay}/logs/`. Remove them only with
    `scripts/safe_worktree_remove.sh`, after `check_references_safe.py`.
* **Scratch:** everything is on the build volume, `/workspaces/MagicDeckTester/build/scratch/boars/`.

## Running right now

| What | PID | Output / marker | Owner |
|---|---|---|---|
| Fetch-ranking A/B: ONE pooled batch, 24 threads, both arms per job via `"flags"` | `mtg.pips` 567419 (driver `run_ab.sh` 567416) | `rank2/ab.err` (heartbeat), `rank2/{off,on}.wins`, `rank2/cmp.txt`, `rank2/DONE` | Claude rank agent |
| Root-cause of the 9 persisting Boars slower games (read-only) | Claude subagent | `rootcause/rc-boars.md` (not written yet; work in `rootcause/rc-boars/`) | Claude subagent |
| d0haste agent: its pooled A/B finished 12:48Z (`d0haste/ab/pooled.out`); the agent is analysing | -- | `d0haste/ab/` | Claude subagent |

* **Cells of the rank A/B:**
  * Boars, Boars 2HG, Creature Giving, CG 2HG and Minotaur, each at play setting and at d3 b10;
  * 300 games per cell, with `shuffle_salt_search` 1 decoupled rows;
  * 8 control decks × 20 games, which must be byte-identical.
* **The Claude subagents and the Claude 2-hourly timer (`e5606a1d`) are children of the Claude session.** They stop if it
  closes. A background shell started by one of them may die with it too, so check `kill -0 567419` before assuming the
  A/B is still going.
* **Codex:** arm your own PID waiter (`kill -0 <pid>`, never `pgrep -f`) and your own 2-hourly status notification.
  12:30-20:00 UTC is the USER's overnight window, so the CLAUDE.md timer rule applies.
* If the rank A/B died, relaunch it in this order:
  1. `bash build/scratch/boars/rank2/run_ab.sh`, after checking that `rank2/mtg.pips` is a build of `boars-rank` HEAD
     `23e2e67db`;
  2. watch the heartbeat in the first 10 minutes;
  3. at DONE, compare the arms per cell: delta, t, faster/slower;
  4. root-cause every slower game at 4x budget.
* **The ship bar:** default ON only if every deck's aggregate is <= 0. USER: "+1 is not neutral. 0 is neutral."

## Chain3 result (HEAD 151d9b9b6, MTG_PGO=0) -- NOT ACCEPTED YET

* **Smoke (16 cases changed):**
  * Boars d3 4.86 -> 4.78, d5 4.92 -> 4.87, 2HG 5.20 -> 5.12.
  * Minotaur and Five Colour are better. Cyclone is back at GT.
  * Two searched games are slower, both CG d5:
    * gi105 is churn: it recovers at 16x.
    * gi113 (seed 1114) is REAL. It's the ranking tie: the T2 board is Tomb + Birds, library order picks Breeding Pool,
      and there is no W left for Enlightened Tutor. The boars-rank fix covers it (T5 -> T4 with the lever on).
* **Regression (27 configs changed):** searched slower 10, faster 263.
  * Boars: d3 5.08 -> 4.82, d5 5.04 -> 4.77 / 5.03 -> 4.80, 2HG 5.37 -> 5.08, d0 5.79 -> 5.50.
  * CG, Minotaur and Five Colour are better.
* **Overnight `--deck=boars,boars2hg` (15 changed):** searched slower 8, faster 473.
  * d5 ~5.10 -> ~4.82, 2HG ~5.35 -> ~5.07.
* **Classification:** `chain3/classify_{smoke,regression,overnight}.txt`.
  * Churn: CG regression gi330 and Boars overnight gi79.
  * 16 PERSIST at 16x: 9 Boars, 6 CG, 1 Five Colour.
* **The old and new binaries and the new wins are frozen in `rootcause/`:** `{smoke,regression,overnight}-{old,new}`,
  `*-wins-new` and `gt_logs-old`. `--accept` overwrites `logs/snapshots/<mode>-baseline`, so explain old-vs-new from
  these copies after the accept.
* **rc-cg verdict (DONE; `rootcause/rc-cg.md`):** all 6 CG/5C games are VARIANCE-clairvoyant. The evidence: under
  `blind_salt` the old binary loses its faster win every time, and in 5C it reproduces the new game exactly. Cast order and
  payment are not involved. It also found three things to follow up:
  1. **43afd29fd is not byte-identical for Five Colour at play settings:** 7 of 50 digests differ, and 2 win turns move
     each way. The agent's unverified explanation:
     * for the ported providers (AL/Burn, 5C, Morph), the top-1 cut now happens inside the pick list, so the land drop
       becomes the UNNAMED `add_for_land(ln, "")` plan;
     * `PerformFetch` then ranks at resolution, with the fetch already in the graveyard (Deathrite live) and the pool and
       life changed.

     If that is confirmed, a single provider pick should be emitted as a NAMED plan. Verify it.
  2. **`shuffle_salt_search` had no effect on these games;** `blind_salt` did. So the rank A/B's "decoupled" rows may not
     detect this kind of clairvoyance. Consider a `blind_salt` ensemble for fetch judgements.
  3. **The old regression baseline (bca43941d) predates `26c08b53e` (crack fix) and `124950596` (USER: fetch hold
     opt-in).** CG gi401 comes from 124950596.
* **rc-boars:** still running. Its games:
  * regression: boars2hg d5 s2002 gi36 and gi66; boars d5 s3003 gi117;
  * overnight: boars2hg d5 s4004 gi10; boars2hg d5 s7007 gi13; boars d5 s4004 gi270; boars d5 s5005 gi277;
    boars d5 s6006 gi4 and gi41; boars d5 s7007 gi13;
  * plus the d0 examples gi257 and gi290.

  Its brief, `logs/boars-research/rootcause-brief.md` in the boars worktree, gives the method.

## Next steps, in order

1. Get rc-boars's verdicts: read `rootcause/rc-boars.md`, or redo the 9 games with the brief's method.
2. If nothing REAL needs a fix first, accept from the boars worktree with `MTG_PGO=0`:
   * `env -C <boars wt> bash test/regression.sh --smoke --accept`
   * `env -C <boars wt> bash test/regression.sh --accept`
   * `env -C <boars wt> bash test/regression.sh --overnight --accept --deck=boars,boars2hg`

   This deck-scoped overnight accept is a one-off for the analysis. USER: the overnight GT otherwise belongs to the
   periodic run. Then run `python3 test/check_gt_logs.py` and commit GT.
3. When the rank A/B is DONE:
   * if every deck's aggregate is <= 0, cherry-pick `boars-rank` onto `boars-analysis` and flip
     `MTG_FETCH_RANK_PIPS` default ON;
   * otherwise report it and leave it OFF.
4. Integrate `boars-d0haste` if its A/B is good; read its agent report or `d0haste/ab/`.
5. Rebuild (`flock /workspaces/MagicDeckTester/build/BUILD.lock ./build.sh`), re-run smoke + regression, classify, accept.
6. Remaining analyze-deck Stage 5:
   * `verify_deck.py`;
   * 5a nonconv/fd-diverge, 5b multi-depth, 5c2 leaf tiebreak, 5e-5h (5h: the Raggadragga `target` viewer HARD_MISS);
   * the discard bucket policy.

   Then the Stage 6 report with the 6a disclosure. The 5d Claude-play sweep is DONE: 16 games, seeds 97001-97016, logged
   in `docs/design/analysis-Boars.md`. 97005 is fixed by boars-rank once that is integrated.
7. Re-measure the 3x-Hinata cost gate on an idle box.
8. Push the shared branch per the 4 h rule. Then clean up the worktrees.

## USER rulings this session (VERBATIM)

* Cast order:
  * "If you want a ruling on the cast order I recommend you come up with a recommended order and I review it."
  * "Can you show me a full cast order instead?"
  * "That order seems reasonable to me."

  The order is in `docs/design/boars-cast-order-2026-11-14.md`: land, Herald's Horn, Lightning Greaves, Herald of Ilharg,
  Birds, Ignoble Hierarch, Flinthoof, Zhur-Taa, Sage, Raggadragga, Ilharg, End-Raze, Decimator.
* Fetch ranking: "We should obviously use the same heuristic that just grabs Stomping Ground."
* Fetch hold: "Human play doesn't need to keep the hold, at least by default."
* Fetch width:
  * "Okay, I get it. For a colour complex deck like creature giving you might have a point that we need to allow 2-3
    options in some cases. Though it might still be preferable to figure out a rule that indeed narrows it sufficiently. I
    would try that first and only fall back on the other if all else fails and even then try to make it only 2 options."
  * "Boars and such do not fall into the same category. The search choices are obvious."
* Shocks and basics:
  * "If there are 2 colours you literally can just always get the shock and then basics after that. This makes fixing
    extremely easy."
  * "Yeah, with 2 colours it should never matter. Even if you pay every fetch + shock cost you will still have remaining
    life."
  * "(of course, this i assuming we don't have cards caring about your life total, but we generally do not, because these
    cards are bad with those anyway)"
  * "Anti-lifegain doesn't care about your own life." / "But, it has 4 colours, so it needs a complex ranking anyway."
  * "Though to be fair, getting a shock over a basic is usually what we want to do anyway. The only exception is if we have
    no shocks in the colour our line needs."
* Standing: clairvoyant wins are not regressions (USER 2026-10-13). A heuristic's order must be a deliberate ranking,
  never library order.

## Open USER questions (repeat them in the closing report)

* **Fetch width:** keep top-1. The ruling above says fix the ranking first; a width-2 fallback is a PROPOSAL only, and
  only for colour-complex decks. Report whether boars-rank closes 97005 and CG smoke gi113, and whether any game still
  needs width 2.
* **Five Colour:** 43afd29fd moves Five Colour play (rc-cg item 1). Was that intended? Should a single provider pick be a
  named plan?
* **Vampires:** the Stromkirk Condemned site-9 gap (the same class as Flinthoof). Fix it with an A/B?
* **Carried over from the checkpoint:**
  * the cost gate on units;
  * Hinata perf (idle-box HEAD vs `9229b25ca`);
  * the Boars d5 unbudgeted tail (~16% abandon);
  * D1-D8;
  * beyond-horizon defaults;
  * Zhur-Taa 2HG hold-back / post-combat ping;
  * the Herald "no decision" rows;
  * the Ulamog shared target rank;
  * d5 b0 backstop rows identical to d3 b0;
  * the stale CG overnight GT;
  * Raggadragga `deck_has_mana_untapper` (left as is).

## Subagent handoffs (all three Claude subagents were told to stop and hand off, ~13:00 UTC)
* rank: `.claude/worktrees/boars-rank/logs/boars-research/rank-handoff.md` (A/B batch left RUNNING, PID 567419).
* d0haste: `.claude/worktrees/boars-d0haste/logs/boars-research/d0haste-handoff.md`.
* rc-boars: `build/scratch/boars/rootcause/rc-boars.md` (done rows + TODO rows) and
  `.claude/worktrees/boars/logs/boars-research/rootcause-rc-boars-handoff.md`.
If a file is missing, that agent stopped before writing it; redo its step from its brief.
* 13:0xZ rank agent STOPPED: A/B already DONE 12:54Z (exit 0; nothing running). Lever stays OFF: CG +4 (+1 on the
  matched-shuffle rows) fails "every deck <= 0"; Boars -18, Boars 2HG -6, CG 2HG 0, Minotaur -2, Mill 0; controls
  byte-identical; 20 of 30 slower games persist at 4x, and CG gi138 + CG 2HG gi258 (the ones that decide the bar) are not
  root-caused yet. Details: rank-handoff.md "HANDOFF #2".
* d0haste agent STOPPED: `9d08b49b5` on boars-d0haste, MTG_GREEDY_SELF_HASTE default ON (d0 runner only). 400-game
  held-out A/B: Boars d0 -24, Boars 2HG d0 -20, every searched cell <= 0, six other decks identical; 95093 gi92 T5 -> T4.
  Units 3035 / scenarios 517 pass, but only BEFORE the final comment-only amend: rebuild + re-run before integrating.
  One slower 2HG d3 game (gi399) hinges on the top-1 fetch width (reported, prune unchanged). Files:
  `.claude/worktrees/boars-d0haste/logs/boars-research/d0haste-handoff.md`,
  `.claude/worktrees/boars-d0haste/docs/design/boars-greedy-self-haste.md`.
* rc-boars agent STOPPED, all 12 games DONE (`build/scratch/boars/rootcause/rc-boars.md`; handoff
  `logs/boars-research/rootcause-rc-boars-handoff.md`). Searched: REAL-ranking-tie 4 (2HG reg gi66, 2HG ovn s7007 gi13,
  ovn s4004 gi270, s5005 gi277 -- all recover with boars-rank MTG_FETCH_RANK_PIPS=1; extra reasons the ranking misses:
  Stomping Ground is a Mountain = Flinthoof +1/+1 [gi270 exact lethal], a mana creature in hand counted as colour cover,
  Ilharg's RR unseen); REAL-castorder 1 (2HG reg gi36: Greaves + hasted Birds over Birds + Horn on T3; Horn line found at
  64x/unlimited, so a budget re-pricing, not unfindable; order-off finds it at 1x); VARIANCE-clairvoyant 5 (reg s3003
  gi117, 2HG ovn s4004 gi10, ovn s6006 gi4/gi41, ovn s7007 gi13). d0 gi257/gi290 = REAL-other: the crack fix frees mana,
  and the d0 runner taps creatures that should attack (pumped Birds, exalted Hierarch). Minor misplay: ovn s6006 gi4
  casts a second Ilharg into the legend rule. MTG_UNPRUNE=fetch also restores the hold (not a clean revert of the width).
  => every searched slower game now has a verdict: the accept (next step 2) is unblocked. Report gi36 (cast order) and the
  ranking-tie games to the USER with it.

## Codex fetch-ranking follow-up (2026-11-14; pending USER review)

The accepted Boars/search changes are on `boars-analysis` through `5028fd747`. The fetch-ranking lever remains OFF and
unmerged. Its separate worktree `boars-rank` now has local commit `142303454` (no push): count each mana producer's
`produces_amount` when deciding which spells' colour pips the fetch ranking should consider. The previous count treated
Azorius Chancery as one mana and excluded Massacre Wurm {3}{B}{B}{B} from Creature Giving 2HG d3 gi165 on T5. The
top-1 fetch then took Breeding Pool instead of Overgrown Tomb, leaving only two black sources. The corrected binary
takes Tomb and wins T5 (old ranking T7 in blind salt 1; T6 in the original A/B salt1 cell). The new unit fixture
checks this exact target. The rule still approximates the candidate line using castable spells in hand; the search does
not pass its selected spell sequence to the fetch provider.

Validation of `142303454`: 3,042/3,042 unit tests; 516/516 scenarios. One pooled 6,000-game A/B at 12 workers,
`build/scratch/boars/rank2/capacity_ab/`, compared the corrected binary with the frozen previous ranking across
Boars, Boars 2HG, Creature Giving, Creature Giving 2HG and Minotaur, both play/depth-3 settings and both search salts.
Exactly ONE game changed digest or win turn: CG 2HG d3 salt1 gi165, T6 -> T5. No game got slower. Relative to the
ranking-OFF arm, deck aggregates are Boars -18, Boars 2HG -6, CG +4, CG 2HG -1, Minotaur -2. The CG +4 is entirely
the play-profile gi125 and gi138 in both salts: those four original T4 -> T5 losses tie under five known-respecting
blind-search salts, and their old T4 lines depend on post-fetch draw order. The candidate is NOT default-on yet.

Other loss audit: Boars d3 gi84, Boars 2HG d3 gi42, and Boars 2HG play gi22 retain losses with a fixed realised
shuffle, but each first differs at Forest versus Stomping Ground and then draws different cards. With 20 fresh real
shuffle salts plus blind search, their paired turn deltas sum +1, 0, and 0 respectively; no consistent downside to
the shock. CG 2HG d3 gi205 similarly totals +1 over 20 real salts (2 faster, 3 slower, 15 ties). CG 2HG play gi258's
original T4 -> T5 loss ties under all five blind-search salts; with 20 real shuffle salts, two outcomes still favour
the old ranking. In those two, the new red fetch makes T2 War-Riders castable and the search chooses it over two Birds;
the two-Birds line remains legal after the red fetch, but the search does not take it at 16x budget. This is a separate
search-line judgment, not a missing colour in the fetch target. See `rank2/rc/cg2hg_real_focus/` and the final report.

Follow-up on the USER's "should be a recoverable line?": yes, the T2 Stomping Ground land plan with **two Birds** is
actually enumerated (new `boars-rank` unit fixture), and a controlled T3 replay with that board and the observed next
three draws wins T5. But the ranked fetch's real salt-10 game remains T6 at **depth 8, unlimited budget** under blind
search. When the search sees the realised shuffle instead, it wins T5 even with the ranked Stomping Ground, by casting
War-Riders on T2 and Defense of the Heart on T4. In eight paired depth-8/unlimited blind-search salts on that same
realised shuffle, ranking OFF wins T5 in 6/8, ranking ON in 4/8; the two ON losses are salts 1 and 6. Both choose the
T2 War-Riders line, then Sylvan Scrying over the castable Defense on T4. ON's four T5 salts do cast Defense on T4.
This is a genuine one-sample blind-search line-choice variance, not a missing legal plan or finite-budget cutoff. It
does not justify calling the two-Birds line "unfindable"; nor does the exact-shuffle T5 erase the paired blind losses.
The standalone diagnostic manifests and traces are in `build/scratch/boars/rank2/recovery_g258/`.

Review request: consider enabling `MTG_FETCH_RANK_PIPS` by default with the capacity correction; the only failing raw
per-deck aggregate is the four clairvoyant CG play results, which the USER said to ignore. Keep the top-1 width and
do not add a Creature Giving width-2 fallback without separate evidence and USER review. Five Colour's shared-list
change remains a separate question: the single provider pick becomes an unnamed land plan and is reranked after the
fetch moves to the graveyard; its observed earlier win relied on retaining a known library top, but the choice-timing
change itself is real. No fetch-ranking work is running at this checkpoint.

### Isolated Five Colour plan-naming candidate

`boars-fetchname` is a separate local worktree/branch at `8bb5ceba6` (not merged or pushed). For a fetch whose provider
returns one target, TurnSolver now stamps that target on the land-drop plan instead of leaving it empty for a second
pick after the fetch enters the graveyard. The same correction covers a Courser-style land played from the library top.
A unit test checks that Five Colour's one provider target survives plan enumeration. Full checks: 3,036/3,036 unit
tests and 517/517 scenarios. In 50 paired Five Colour games (play profile, seed 3163..3212, game indices 160..209),
only gi184 T5 -> T4 and gi204 T4 -> T5 change win turn, for zero aggregate change; both arms tie under all five
known-respecting blind salts on each game. A seven-deck depth-3 control (20 games each) has no win-turn changes:
Boars, CG, Morph, Anti-Lifegain, Modern RW Burn, Cyclone digest-identical; Five Colour has one same-turn play change.
Artifacts: `build/scratch/boars/fetchname/`. Recommend reviewing this as a separate plan-timing correctness fix, not
as a claimed speed or win-turn gain. No jobs remain running.
