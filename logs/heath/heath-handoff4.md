# heath handoff 4 (2026-11-24) -- for a fresh agent with NO context

Read CLAUDE.md first (box lock `/workspaces/MagicDeckTester/build/scratch/claude_box.lock` for ONE big process; small
jobs via `scripts/box_lane.sh --slots 2`; never `timeout`; never push phase-1-2-deck-analyzer -- the parent lands it;
references commit-only). Full measurements: `docs/design/heath-produces-2026-11-22.md` (Rounds 2-4 section).

## Tree / branch
`.claude/worktrees/heath-derived` = branch `wip/heath-derived`, REBASED onto origin/phase-1-2-deck-analyzer (138f60c83),
rebase only (one HeuristicArm.h conflict, both slot lists unioned). Commits on top of origin:
* deck-derived hint `HandFixingProduces` + `MTG_FETCH_HINT_TAPPED` report-only arm (8dad54530 after the rebase), tests,
  round-2 static hints and Orchard-before-fetch (earlier commits on the branch).
* 500bcdfc8 perf: the hint's library scan uses load-time bytes `CardDefinition::fh_*` (CardDatabase.h/.cpp,
  SpellEffects.h FetchReachColourMask fast path). Byte-identical: 32/32 job digests equal vs the unfixed binary.
* 0df9be388 docs (rounds 2-4).
Build of 500bcdfc8: `build/wt/heath-derived/Release/mtg` (= `build/scratch/heath/mtg.rb`). Units 4126/4126.
Scratch: `/workspaces/MagicDeckTester/build/scratch/heath/{r4,r5}`. Pre-rebase binaries: mtg.base (42d819bc0, run in
`.claude/worktrees/heath-base`), mtg.derived (c7a354a63, run in `.claude/worktrees/heath-produces`, same cards.json),
mtg.fix (738bb3766 = derived + perf fix, worktree `.claude/worktrees/heath-fixonly`; scratch, removable later via
scripts/safe_worktree_remove.sh).

## USER rulings (verbatim)
* "I don't know what this is for, but W/U is understating the power of fetchlands. They can get any shock in the deck, so
  they can theoritically produce any colour those can get."
* "Yes, for 1 we should count shocks. I'm less certain about triomes because of the come-into-play-tapped, but usually
  shocks cover the colours we need. 2. Orchard first makes sense for the decks you mentioned." (Orchard-before-fetch
  USER-APPROVED, default ON.)
* "I'm more concerned about areas that help searched."
* NEW BAR (2026-11-24): "I think I would be willing to accept something that helps a lot of decks, but doesn't help a few
  deck and then look for ways to mitigate the cost on the other decks without blocking the adoption. Naturally, if the
  only reason it is worse on those other decks are the specific seeds we can just move on. d0 is not very important at
  all as I said previously. The primary thing that matters is play settings, though we should also check whether any
  worse case is a result of clairvoyant play. We can ignore those." / "(until we can show it is clairvoyance, variance or
  fix it)" / "If play settings are super marginally better, but other settings are much worse it might be worth holding
  back as well, but usually play settings being better is enough."

## r4 A/B (pre-rebase; base 42d819bc0 vs derived c7a354a63). Play = each deck's play setting, seed 3000000; d0 100k seed 7000000
play net (+ = slower): Antilife 0 (8000), Antilife2HG +2 (4000), Boars -1 (6000), CG -2 (6000), Five Colour +4 (3000),
Mill 0, Minotaur 0, Burn +11 (10000; 5 fast / 16 slow), Morph +4 (10000), Sea +2 (4000), Spirits +2 (4000), Stuffy -1
(2000), Cyclone 0, Worldfire +6 (2000; 34/36, 2 lost / 1 gained).
d0 net: Antilife -2106, 2HG -2363, Boars -870, CG -611, FC -3649, Mill +3, Minotaur -31, Burn +33 (z~0.4), Morph -939,
Sea -1397, Spirits -611, Stuffy -959, Cyclone +4, Worldfire +23 (z~0.7).
Tapped arm (MTG_FETCH_HINT_TAPPED) on Five Colour: d0 net 0, play 0 changed -> untapped-only costs nothing.
CPU: the r4 d0 CPU gaps (WF +21%, Stuffy +12%) were run-time drift (the arms ran 3 h apart). Concurrent same-lane
base vs fix: WF d0 0.984, Stuffy 0.985, Mill 0.997. Searched units within +-0.8% except Five Colour play +2.6%.

## Slower searched games (94) -- classification
* 55/56 non-Worldfire recover at d8 b0. Boars gi5351: d8 b0 base T5 / derived T6 (derived fetches T1). MTG_NC_SEARCH=1
  derived T5 (base NC T5) -> CLAIRVOYANCE (lockstep search values a T1 fetch through a library order it won't get).
* Worldfire 38: 19 recover at d8 b0. The other 19 are intractable at d8 b0 (6-13 h per game). Escalated to the play depth
  at b2000 with a 60M-unit cap (r4/wf_b2000.out): 14 recover (46, 156, 1007, 1023, 1625, 1732, 1874, 613, 645, 751, 844,
  868, 906, 921). gi1410 equals base at b2000 (base T8 at b2000 too). gi205 ABANDONED at the cap.
  103, 1363 and 1684 are slower at b2000 on the same setting (base 5 / derived 6). UNRESOLVED: derived's NC run was killed
  at 16 h, and base NC never ran.
* Rebased tiers' 15 searched slower games: ALL recover at d8 b0 (r5/d8_tiers.out).

## Rebased tiers (r5/tiers.log; outputs in this tree's test/logs/{smoke,regression,combined})
smoke 44 failed / regression 69 failed, all fetch decks. Searched: smoke 5 slow / 4 fast, regression 10 slow / 6 fast.
Reference protocol clean. NOT ACCEPTED YET.

## Remaining, in order
1. Decide ship vs hold under the NEW USER BAR. It is a correctness fix of the Heath / Foothills / Mesa / Strand hand model
   (USER ruling 1). At play settings Burn +11, Worldfire +6, FC +4, Morph +4 and 2HG/Sea/Spirits +2 are near-zero-signal
   counts with every recoverable game recovering, so it most likely ships default ON. State that in the report.
2. Optionally settle Worldfire 103 / 1363 / 1684 / 205: NC derived vs NC base at play depth b2000, ONE game per lane job
   (2 slots) with abandon_units set, so a stuck game cannot jam the lane. Note: Worldfire NC b2000 ran >16 h on these
   games, so cap it (e.g. abandon_units 60000000 applies to NC too; the killed run had it -- check that it actually binds
   under NC). If still unresolved, report them as a USER item with the per-game list (the coordinator asked for that).
3. Accept GT from the existing run, without re-running (HEAD src is unchanged since the tiers ran; verify
   `git diff 500bcdfc8 HEAD -- src` is empty): `bash test/regression.sh --both --accept-with-regressions="<note: heath
   deck-derived fetch hint; r4 A/B numbers; 15 tier slower all recover d8 b0; Boars 5351 clairvoyance; WF open games>"`,
   then `python3 test/check_gt_logs.py`, `python3 scripts/check_references_safe.py`, commit, push wip/heath-derived only.
   If origin has moved by then, the parent rebases and re-runs the tiers at landing.
4. Owed / say to the USER: ~60 direct `params.produces` readers still see the static hint (mostly per-deck provider
   heuristics); `LandAbilityClasses.h` still lists the deleted `hand_fixing_produces` key (harmless).

## Running jobs
None of mine. (r4/d8_rest and r4/wf_b2000_4 were killed by the coordinator; their DONE markers will not appear.)
