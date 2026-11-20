# Overnight accept 2026-11-20: attribution of the changed games

Periodic overnight tier, run 2026-11-20 18:31-20:00 UTC in the primary checkout (src = `c86a86333`'s tree,
identical to the run snapshot `72160eb09`; Release, `MTG_PGO=0`). Old side = the committed
`test/gt_logs/*overnight*.wins`, last fully accepted at `1443eeba0` (2026-11-14; src tree `47fcee084`).
670 of 735 configs changed. Searched slower 1148 / faster 3529; d0 slower 3986 / faster 15567.
`classify_turn_later.sh` (4x/16x): 406 churn, 742 PERSISTS.

**Method.** The OLD binary was built from `1443eeba0` in a scratch worktree and always run from that
worktree (old `cards.json`, old deck artifacts), so the old arm is the real old configuration. Every old-arm
case-setting replay reproduced its committed GT turn (`repro=ok` on every game checked). Then:

1. **Both arms at `--depth 8 --budget-ms 0`** (`ignore_play_profile`) on 178 PERSISTS games: every PERSISTS
   game outside Midrange Burn and Spirits (158), plus a seeded random sample of 10 Midrange Burn and 10
   Spirits.
2. **Case-setting traces of both arms** for the same games: same kept hand? same draws? first differing
   decision.
3. **Single-lever flips** on the NEW binary at case settings, and at d8 b0 where it matters. A flip that
   reproduces the old GT turn names the lever.
4. **Artifact swaps**: the new binary on a copy of the deck folder holding the OLD artifacts, run on whole
   overnight rows.

Scratch (build volume): `build/scratch/claude_ovn1120/agent/` (`d8b0.classified`, `old/`, `new/`,
`trold/`, `trnew/`, `art/`, `flip_*`, `legacy/`, `cyc_pf_off/`, `fc_legacy/`, `bisect/`). Scripts:
`logs/overnight-accept/` in the agent worktree.

## Per worse deck

### Gods (gods_srch +0.32 over 2 rows, gods2hg_srch +0.26): the USER's strict-dredge rule, not a regression

- **Mechanism:** `MTG_GODS_RETRIEVAL_DREDGE` (default ON), introduced by `c39c1da1a` and adopted by
  `6cb9a7c85` (2026-11-15; smoke and regression accepted with the note "coupled searched cells slower").
  The rule dredges toward Mystic Retrieval instead of taking the three-Gods natural draw. Under the
  coupled (clairvoyant) search it is slower. Blind (`MTG_NC_SEARCH`) it is **faster**: one-headed -0.1305
  turns over 2000 pairs, two-headed -0.069 over 1000 (`docs/design/gods-retrieval-dredge-codex-2026-11-15.md`).
- **Proof:** with the new binary and `MTG_GODS_RETRIEVAL_DREDGE=0`, all 27 Gods/Gods2HG PERSISTS games
  return to the old GT turn at case settings (27/27). At d8 b0, 25 return to the old turn and the other 3
  get faster than old. Kept hands are identical; the arms first differ at a loot/dredge decision (e.g.
  s4004 gi1: old casts Faithless Looting T1, new does not, then T2 skips its land drop).
- At d8 b0 the old arm is still faster in 24 of 27 games. This is the coupled cost the USER accepted with the
  rule, not a search defect. d0 Gods is unchanged (-0.014).

### Hinata (hinata_srch +0.23 over 7 rows, hinata2hg_srch +0.16): the restored d5 play floor

- **Mechanism:** `da17c0bbf` "Restore Hinata depth-5 play floor" (USER: depth 5 is the minimum, never
  lower). It changed `Hinata.value.json` `value_play.target_depth` from 4 to 5 and dropped the emulated
  ladder. The 2026-11-14 overnight GT was taken at the invalid depth-4 block.
- **Proof (artifact swap):** the NEW binary with the OLD `Hinata.value.json` reproduces the old GT almost
  game for game, and is slightly faster:

  | row | old GT | new run | new bin + old value.json | same as old |
  |---|---|---|---|---|
  | hinata d5 s4004 | 5.7435 | 5.8282 | 5.7318 | 420/425 |
  | hinata d5 s5005 | 5.5529 | 5.5765 | 5.5435 | 419/425 |
  | hinata d5 s6006 | 5.7294 | 5.8000 | 5.6871 | 417/425 |
  | hinata d5 s7007 | 5.7953 | 5.8565 | 5.7741 | 416/425 |
  | hinata2hg d5 s4004..s7007 | 5.59/5.33/5.79/5.56 | 5.68/5.36/5.93/5.58 | identical to old | 100/100 each |

  So the engine changes since 1443 are net FASTER for Hinata (-0.009 to -0.042 per game). The whole
  slowdown is the depth-5 setting at the 30 ms budget: +0.06 to +0.11 turns per game one-headed, +0.02 to
  +0.14 on 2HG, relative to depth 4 at the same budget.
- **d8 b0 (21 + 4 PERSISTS):** 14 recover, 8 reach the same turn on both arms (the old GT turn was budget
  luck), 2 are unrecoverable (s5005 gi384 T4->T7, s6006 gi156 T4->T5; both arms at d8 b0: old T4, new T5),
  and 1 is still pending. Both of the 2 first differ at a T1/T3 cantrip/scry choice (gi384: T1 Gamble vs Sol
  Ring). They are unattributed: the opponent-lands lever (`legacy_passive_opponent_lands`) and
  `MTG_UNSEEN_KEEP_SCARCE=0` do not restore them.
- **USER-owned:** the depth-5 floor is the USER's ruling and stays. The measurement for the USER: at b30, d5
  costs Hinata about 0.08 turns per game against d4 on the overnight seeds. A larger budget at d5 is the
  settings lever, if one is wanted.

### Unpredictable Cyclone (cyclone_srch +0.074 over 6 worse rows): budget churn

- **Mechanism:** the Ponder-first adoption (`7b95d828b`, `MTG_CYCLONE_PONDER_FIRST_T1` + `_DEMOTE`, USER
  "whichever gets the best results"; held-out 10,000 pairs net -16), plus the general engine and
  opponent-model churn.
- **d8 b0:** **51 of 51 finished PERSISTS games recover** to at most the old turn. 2 d8 b0 replays are still
  running (s6006 gi234, s7007 gi104; old arm T5/T4). This matches the adoption's own record: 279/280
  recovered at d8 b0 and the last at d8 b1000+.
- **Row level, Ponder-first off:** searched rows go 3303 games -8 turn-games against the new run (Ponder-first
  +0.0024/game here, within noise of the held-out -16/10,000), and are still +10 against the old GT. The
  residual is churn (above). d0 Cyclone is much faster: -1125 turn-games over 8000.
- Not a regression.

### Five Colour d0 (fivecolour_d0 +0.015 over 4 rows; searched rows faster): a T1 fetchland-hold change in d0

- About 100 games per 2000-game row change (95% byte-identical). Net +42 turn-games over 8000 (+0.005 per
  game). Searched Five Colour rows are faster (-20 turn-games, 0 slower at d5).
- **Mechanism (traced):** kept hand and draws are identical. On T1, d0 now plays the dual or basic and holds
  the fetchland, where the old binary played the fetch. s4004 gi192 shows it worst: T1 Mountain with Deathrite
  Shaman in hand (uncastable), where the old binary fetched and cast Deathrite T1; T5 -> T7. s4004 gi67 is
  similar (Breeding Pool T1 instead of Windswept Heath; T5 -> T7).
- **Not the cause:** the opponent-lands model (`legacy_passive_opponent_lands` leaves the rows at +0.007 to
  +0.025 vs old), `MTG_HELD_FETCH_CRACK_FIRST=0`, `MTG_GREEDY_SELF_HASTE=0`, `MTG_GREEDY_TUTOR_TARGET_TIE=0`,
  `MTG_UNSEEN_KEEP_SCARCE=0`.
- **Commit:** BISECT_RESULT
- The same T1 "dual instead of fetch" shape is the first difference in several unattributed searched
  d8 b0-unrecoverable games: Antilife s6006 gi117 (Temple Garden instead of Windswept Heath), Minotaur
  s6006 gi937 (Secluded Courtyard instead of Bloodstained Mire), and Creature Giving (a different fetch
  *target*: Overgrown Tomb instead of Temple Garden, so a different shuffle). See below.

## Midrange Burn and Spirits (faster on aggregate; sample of 10 + 10)

- **Midrange Burn (and Midrange Burn 2HG): the USER's opponent-lands model** (`c9022b11d` "Every deck's
  passive opponent plays a real pool deck's lands", USER 2026-11-17; 2HG `afc931bb4`). The old opponent was
  three placeholder "Opponent Nonbasic Land"s, so **Price of Progress** always hit for 6. Now it hits for
  2x the real pool deck's nonbasics (s4004 gi53 2HG: City of Brass + Forest). With
  `legacy_passive_opponent_lands: true`, **25 of 25** Midrange Burn PERSISTS games (10 one-headed, all 15
  2HG) return to the old turn at case settings. `MTG_MIDRANGE_FIREBLAST_ORDER=0` restores none. This is a
  modelling change to the opponent, i.e. a different game, not a decision regression. The deck is net faster
  (d3 -0.014 to -0.055, d5 -0.034 to -0.062, 2HG -0.21 to -0.61).
- **Spirits: the keep-table adoption** (`a05ac4395`, 2026-11-15). All 10 sampled games have DIFFERENT kept
  hands (KEPT-DIFF), so they are different physical games. Artifact swap, d5 rows: the new binary on the OLD
  folder (no keep table, no sidecar) reproduces old GT 416-420/425 per row (-0.012 to -0.019 vs old). The
  keep table alone gives -0.10 per game, matching the new run to within 0.01. The `sampled_gate=3` sidecar
  (`fc2f60a9b`) is about -0.005 per game.

## PERSISTS classification at d8 b0 (both arms)

Of 178 games re-run (158 = every non-MB/Spirits PERSISTS, plus 20 sampled):

| class | games | decks |
|---|---|---|
| recovers (new d8 b0 <= old GT turn) | 71 | Cyclone 51, Hinata 14, Gods 3, Alchemist 2, MB2HG 1 |
| same turn on both arms at d8 b0 (old GT was budget luck) | 8 | Hinata 6, Hinata2HG 2 |
| new better at d8 b0 | 1 | Mill |
| differs, explained by a USER-adopted lever/artifact | 61 | Gods 24 (dredge rule), Midrange Burn 25 (opponent lands), Spirits 10 (keep table), PD 1 (below) ... |
| differs, unattributed line change | 30 | Creature Giving 20, Galadriel 5, Hinata 2, Antilife/Antilife2HG 2, Minotaur 1 |
| pending (d8 b0 replay still running) | ~7 | Cyclone 2, Gods 2, Hinata 1, Mill 1, Snow 1 |
| new deck, no old arm | 1 | Red Elemental |

COUNTS_NOTE

## Anything that looks like a real regression

1. **Prevent Damage s6006 gi47, T8 -> unwon (also unwon at d8 b0): `MTG_PD_LATER_HAND_HOLD`**
   (`636910330`, 2026-11-15, mana-clean "Hold known Earthquake red pip through armed payment"). Same kept hand
   and the same draws. On T4 the new line plays **no land** with two Battlefield Forges and an Ancient Tomb in
   hand, and on T5 again (it casts Spellshock instead). Flipping `MTG_PD_LATER_HAND_HOLD=0` alone restores T8;
   `_MULTI_QUAKE_HOLD` and `_SPELLSHOCK_HOLD` do not. A skipped land drop with lands in hand looks like a
   defect in the hold's interaction with land play, not a trade-off. One game (the PD rows are otherwise
   clean); hand to the mana-chain owner.
2. **The T1 fetch-hold shape (Five Colour d0; searched Antilife, Minotaur; fetch target in Creature
   Giving).** The d0 cases are clear losses (holding the fetch leaves a T1 spell uncastable). The searched
   cases are unrecoverable at d8 b0 but net-neutral to faster at row level (Creature Giving -0.006 to -0.011
   per game). Commit: BISECT_RESULT.
3. **Hinata gi384 / gi156 and Galadriel x5:** d8 b0-unrecoverable line changes at a cantrip/scry or
   scry-land choice. Unattributed. Both decks are net faster or neutral at row level.

## Proposed `--accept-with-regressions` note

ACCEPT_NOTE
