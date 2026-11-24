# g993 handoff (Maze's End 2HG s26200993 gi993, the one unrecovered slower game of MTG_RANK_OBJECT_COLORS)

Branch wip/maze-applydrop, worktree /workspaces/MagicDeckTester/.claude/worktrees/maze-applydrop.
Scratch: /workspaces/MagicDeckTester/build/scratch/claude_maze_applydrop/g993x/ (all repro outputs below).

## Repro (single game, seconds)
```
cd <worktree>; MTG_TAPDBG=1 MTG_START_LIFE=30 MTG_OPPONENT_HEADS=2 MTG_RANK_OBJECT_COLORS=<0|1> \
  ./build/Release/mtg "decks/Maze's End/Maze's End.cod" --profile "decks/Maze's End/Maze's End.profile.json" \
  --games 1 --seed 26200993 --game-index 993 --depth 8 --budget-ms 0 --ignore-play-profile --threads 1
```
Reproduces the batch exactly: rank off -> win T4, rank on -> T5. `MTG_TAPDBG=1` prints every REAL payment
(`[paydbg] cost=... taps: ...`) and every tap (`[tapdbg] ... col=` 0W 1U 2B 3R 4G 5C). With `MTG_NEEDS_TAP_ORDER=1`
it also prints each candidate's rank (`[nddbg]`) -- a cheap way to see the rank per source.
Outputs: g993x/td_base.err, td_rank.err (tapdbg), x_needs_rank.err (nddbg ranks), or_*.err (mana oracle, T4).

## Mechanism (Q1/Q2) -- per-source evidence
T4 board: Selesnya GG, Manor Gate#41 (G, chosen U), Maze's End #43/#44, Cliffgate#16 (R, chosen G), Azorius GG,
Guild Summit, Manabond; hand Exploration, Gond Gate, Planar Nexus. Manabond#40 and Sylvan Scrying are DRAWN mid-turn
by Guild Summit off the Gate entries.
BASE (T4 win) payments: Exploration {G}=Selesnya; Maze's End#43 {3}=Manor#41 + Azorius + Planar Nexus (#44 held by the
plan's act hold -- the plan carries two activations); Manabond {G}=Cliffgate {G}; Scrying {1}{G}=Gond {G} + Baldur's {C}
(-> Talon Gates). Main-1 second activation fails ({3} ok=0). END STEP: the two Manabond triggers dump Manor#42 /
Maze's End#43 / Talon (Gates enter untapped under Gond, Guild Summit draws, second dump puts Citadel Gate...), then
Maze's End#44's {3} = Manor#42 + Talon + Citadel -> 10th distinct Gate -> WIN T4.
RANK: Cliffgate now ranks as the R/G dual (20) = Manor (20) = Azorius (20); ties go by battlefield order (Manor idx1,
Cliffgate idx5, Azorius idx7), so the {3} takes Nexus + Manor + Cliffgate (nddbg shows the three at rank 20), leaving
Azorius (W/U): Manabond's {G} and Scrying's {G} then have only Gond and a Baldur's Gate FED by the held Maze's End#44
(oracle plans 51-56 under rank: "act ME + act ME" ends with ME#44 tapped, float {G}{G}{G}, 2nd activation DROPPED).
The draws are identical (same action order -> same shuffles), so only the PAYMENT differs. Payment is the standing
exemption (not searched), so at d8 b0 the base line simply no longer wins under rank; the search then picks a T5 line.
Base was "lucky": the old rainbow misrank of Cliffgate (50) spent Azorius first.

## Verdict (Q3)
PAYMENT-ORDER DEFECT, not clairvoyance: same information, same draws, the rank's dual-tie spends the deck's {G} over a
W/U source whose W no spell in the list can use. (The base T4 win itself does lean on Guild Summit draws the clairvoyant
search sees, but that is common to both arms.) NC check: `MTG_NC_SEARCH=1` runs of both arms were started at ~09:45 UTC
(PIDs 2006004 base / 2006005 rank, outputs g993x/nc_{base,rank}.{out,err}, markers nc_*.DONE); at d8 b0 NC is very
slow (no T1 payment printed after 10+ min). They are MY experiments: kill them if still running and not needed
(`kill 2006004 2006005`); they run the OLD binary inode, unaffected by the rebuild. No "owning prune" applies here (the
change is a rank, not a prune).

## Fix (Q4) -- implemented and BUILT (65f77011e); does NOT yet recover g993
MTG_MAZE_RANK_SPENDABLE (heurarm MAZE_RANK_SPENDABLE, DEFAULT ON, live only with RANK_OBJECT_COLORS on, so
RANK_OBJECT_COLORS=0 is still the committed tree's play):
* GameState::deck_spell_pip_colors (uint8, deck constant, in the 3-byte hole after eot_attack_draw_n; sizeof 1736
  unchanged -- verified hole with gdb ptype /o on a -g probe TU; Dominance.h note added). Stamped in
  GoldFishRunner::StampDeckTraits beside deck_creature_pip_colors (every nonland card's coloured pips incl. hybrid;
  Garth -> 0x1F). Default 0x1F for unstamped states (unit fixtures -> historical ranks).
* MazesEndProvider::ManaSourceRank: for a plain coloured land (not filter / reflector / scaled, 1 mana per tap) whose
  base rank is an exact ladder rung (10..50 step 10), re-rank by the number of its real colours (object colours via
  g_rank_object for etb_choose_color) that the list's spells carry: 0 -> 5, n -> 10n. This list is G/U/R (Gates Ablaze
  R), so Azorius = 10, Orzhov/Rakdos/Boros = 5, Selesnya = 10, Cliffgate(R+G) = 20, Manor(G+U) = 20, Gateway Plaza 30.
  On g993 the {3} becomes Nexus + Azorius + Manor = base's payment.
* Unit tests appended to test/unit/test_mazes_end_rank_object.cpp (3 cases: default pays Azorius and keeps both {G};
  OFF strands Scrying; rank rewrite scope incl. RANK_OBJECT_COLORS=0 and MAZE_RANK_SPENDABLE=0 -> 20).
* A/B generator: logs/maze-applydrop/mk_ab_spend.py (arms base / rank (spend off) / spend; fresh seeds 25400000 /
  26400000 / 27400000; Maze's End only -- nothing else can move). NOT RUN (coordinator runs big batches).

## Results after the build (build/Release = 65f77011e, build_spend.log clean)
* `mtg-test -tc="*Maze*"`: 121/121 cases, 5061 assertions pass (incl. the 3 new list-colour cases).
* g993 d8 b0: rank+spend (default) **T5** (still unrecovered); rank, spend off: T5; rank off: T4 (base unchanged).
* Under the default the chosen line is STILL Exploration-first: {G}=Selesnya, crack {3}=Manor + Maze's End#44 + AZORIUS
  (the spend rank works: Azorius now pays before Cliffgate), Manabond {G}=Cliffgate, Scrying {1}{G}=Baldur's + Gond,
  then {1},{1} from Planar Nexus (Gateway Plaza etc.). So Maze's End#44 is spent in the one-activation plan and the
  search still does not choose base's Planar-Nexus-first two-activation line.

## Running at handoff
* NC runs PIDs 2006004 / 2006005 (my own experiments, old binary; see Verdict) -- kill if not wanted.

## Remaining, in order
1. Find why the default still misses T4: run the oracle (MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4
   MTG_MANA_ORACLE_ONLY_TURN=4, see g993x/or_*.err) under the default and read the engine-ended board of base's plan
   {land=Planar Nexus + cast:Exploration + act:Maze's End + act:Maze's End}: does it now end with ME#44 untapped,
   Manabond + Scrying cast (as base's plan 51 did)? If yes, the miss is elsewhere (draw order differs -> different
   Scrying target / end-step draws; compare with base's hand [Manor Gate, Maze's End, Talon]); if no, read that plan's
   [paydbg] under the oracle. Maybe also the mana-payment unit set (`-tc="*mana*,*pay*"`) -- not yet run.
2. Decide whether MTG_MAZE_RANK_SPENDABLE stays (it is sound order-only and fixes the dual-tie stranding in its unit
   test) or is reverted if it does not buy g993; either way report to the coordinator.
3. Hand the coordinator: mk_ab_spend.py manifest for the held-out A/B, plus a d8 b0 replay of the 25 previously
   recovered slower games under the new default (the spend lever moves Maze's End play beyond the rank fix).

## NC runs: ended WITHOUT a result
Both `MTG_NC_SEARCH=1` d8 b0 runs (PIDs 2006004/2006005) exited during turn 1 with no `avg` line (stderr ends after the
first T1 payment; nc_*.DONE written, exit status lost). Most likely killed or crashed when the rebuild replaced
build/Release/mtg under them (or memory). The NC clairvoyance check is therefore UN-RUN; to redo it, run from the
frozen copy /workspaces/MagicDeckTester/build/scratch/claude_maze_applydrop/abbin/mtg, not build/Release.
