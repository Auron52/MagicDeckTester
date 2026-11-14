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
