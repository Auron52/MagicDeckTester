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
- **d8 b0 (21 + 4 PERSISTS):** 15 recover, 8 reach the same turn on both arms (the old GT turn was budget
  luck), and 2 are unrecoverable (s5005 gi384 T4->T7, s6006 gi156 T4->T5; both arms at d8 b0: old T4, new T5). Both of the 2 first differ at a T1/T3 cantrip/scry choice (gi384: T1 Gamble vs Sol
  Ring). They are unattributed: the opponent-lands lever (`legacy_passive_opponent_lands`) and
  `MTG_UNSEEN_KEEP_SCARCE=0` do not restore them.
- **USER-owned:** the depth-5 floor is the USER's ruling and stays. The measurement for the USER: at b30, d5
  costs Hinata about 0.08 turns per game against d4 on the overnight seeds. A larger budget at d5 is the
  settings lever, if one is wanted.

### Unpredictable Cyclone (cyclone_srch +0.074 over 6 worse rows): budget churn

- **Mechanism:** the Ponder-first adoption (`7b95d828b`, `MTG_CYCLONE_PONDER_FIRST_T1` + `_DEMOTE`, USER
  "whichever gets the best results"; held-out 10,000 pairs net -16), plus the general engine and
  opponent-model churn.
- **d8 b0:** **all 53 PERSISTS games recover** to at most the old turn (the last two, s6006 gi234 and s7007
  gi104, finished after 2+ h each). This matches the adoption's own record: 279/280
  recovered at d8 b0 and the last at d8 b1000+.
- **Row level, Ponder-first off:** searched rows go 3303 games -8 turn-games against the new run (Ponder-first
  +0.0024/game here, within noise of the held-out -16/10,000), and are still +10 against the old GT. The
  residual is churn (above). d0 Cyclone is much faster: -1125 turn-games over 8000.
- Not a regression.

### Five Colour d0 (fivecolour_d0 +0.015 over 4 rows; searched rows faster): Windswept Heath card data

- About 100 games per 2000-game row change (95% byte-identical). Net +42 turn-games over 8000 (+0.005 per
  game). Searched Five Colour rows are faster (-20 turn-games, 0 slower at d5).
- **Mechanism (traced):** kept hand and draws are identical. On T1, d0 now plays the dual or basic and holds
  Windswept Heath, where the old binary played the Heath and fetched. s4004 gi192 shows it worst: T1 Mountain
  with Deathrite Shaman in hand (uncastable), where the old binary fetched Overgrown Tomb and cast Deathrite
  T1; T5 -> T7. s4004 gi67 is similar (Breeding Pool T1 instead of Windswept Heath; T5 -> T7).
- **Commit: `9344fa594`** "Integrate Boars deck, viewer decisions, and verified engine changes"
  (2026-11-15), specifically its **`cards.json` rewrite of Windswept Heath** into a real battlefield
  fetchland (Boars I1). The record changed from `produces: [W,B,R,G]`, template `basic_land` (a legacy
  immediate fetch) to `produces: []` + `hand_fixing_produces: [W,B,R,G]` + the activation fields.
  `hand_fixing_produces` is read in exactly one place (Creature Giving's cleanup-discard cover,
  `DecisionProviders.cpp` ~23177). Every other land-drop and colour-fixing reader still reads `produces`, so a
  Heath in hand now counts as a land that fixes NO colour, and the dual/basic is preferred over it. Scalding Tarn
  and Misty Rainforest got the same battlefield-fetch conversion in the window but KEPT their W/U/B/R/G
  `produces` hand hint (their oracle note says why), so the Heath record is the odd one out.
- **Bisect:** index 64 `86db981f1` good, 70 `ba307d48a` bad. Indices 65..69 (`9344fa594`, `3ff69eae8`,
  `f8021ae0f`, `b71a0cb2e`, `9547ea31b`) do not compile on their own: the Boars integration landed across them,
  and `ba307d48a` restores the held-fetch functions they call. So the bisect resolves to that batch. The batch
  touches `cards.json` only in `9344fa594`, and in the deck's cards it changes only Windswept Heath.
- **Proof (card-data swap):** the NEW binary with `c86a86333`'s `cards.json`, with Windswept Heath alone
  reverted to its `86db981f1` record, reproduces the OLD turn on **all 6 traced d0 games** (gi18 6, gi35 5,
  gi67 5, gi192 5, gi524 5, gi553 5; new 7/6/7/7/7/7). The same swap at case settings restores **Antilife
  s6006 gi117 and Antilife2HG s6006 gi117 (2 of 2)** and **10 of 20 Creature Giving** games (both decks run
  Heath). It does not restore Minotaur s6006 gi937 (that deck has no Heath; its first difference is a
  Bloodstained Mire, whose record did not change).
- **Not the cause** (ruled out earlier): the opponent-lands model, `MTG_HELD_FETCH_CRACK_FIRST=0`,
  `MTG_GREEDY_SELF_HASTE=0`, `MTG_GREEDY_TUTOR_TARGET_TIE=0`, `MTG_UNSEEN_KEEP_SCARCE=0`.
- **Classification: a real regression, a card-data defect** (not churn, not a USER setting, not a trade-off).
  The battlefield-fetch model is the correct one and should stay. The colour hint it dropped is what costs the
  games. **Fix candidate (NOT applied):** give Windswept Heath back `produces: [W,B,R,G]` as a hand-only hint,
  exactly as Scalding Tarn / Misty Rainforest keep theirs, and drop `hand_fixing_produces`. Alternatively, have
  the fixing readers fall back to `hand_fixing_produces` for a fetchland in hand. Either needs a held-out A/B on
  Five Colour, Anti-Lifegain, Creature Giving, Boars and Sea Creatures (every Heath deck), then GT accepts.
  Wooded Foothills has had `produces: []` since before 1443, so the same question applies to it.
- Scratch: `bisect/log`, `bisect/mtg_idx{64,70}`, `heath/` (d0), `heathflip/` (case), and
  `cards_c86_heathlegacy.json`.


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

## PERSISTS classification at d8 b0 (both arms, final)

Of 178 games re-run (158 = every non-MB/Spirits PERSISTS, plus 20 sampled). The 6 `fivecolour` rows in
`d8b0.idx` are d0 trace rows with no d8 b0 job; they are covered by the bisect above.

| class | games | decks |
|---|---|---|
| recovers (new d8 b0 <= old GT turn) | 76 | Cyclone 53, Hinata 13, Hinata2HG 2, Gods 4, Alchemist 2, MB2HG 1, Snow 1 |
| same turn on both arms at d8 b0 (old GT was budget luck) | 9 | Hinata 6, Hinata2HG 2, Gods2HG 1 |
| new better at d8 b0 | 2 | Mill (see caveat) |
| still slower, explained | 59 | Gods 24 (strict dredge), Midrange Burn 24 (opponent real lands), Spirits 10 (keep table), PD 1 (gi47, MTG_PD_LATER_HAND_HOLD) |
| still slower, unattributed at the time of the d8 b0 run | 31 | Creature Giving 20, Galadriel 5, Hinata 2, Antilife/Antilife2HG 2, Minotaur 1, Alchemist 1 |
| new deck, no old arm | 1 | Red Elemental |

**After the Heath swap**, 12 of the 31 are attributed to the Windswept Heath card-data defect (`9344fa594`):
Antilife s6006 gi117, Antilife2HG s6006 gi117, and Creature Giving d3 s4004 gi53, d5 s4004 gi53 / gi1335 /
gi1403, s5005 gi334 / gi624 / gi1136, s6006 gi135 / gi489, s7007 gi489.

**19 remain unattributed**, game by game (`<deck> <row> <gi>: old GT -> new case turn, d8 b0 new turn; old arm
at d8 b0 = old GT turn`). They are owed follow-ups, not blockers for this accept:
- Creature Giving d5 (10): s4004 gi362 (5->6, d8b0 6), s4004 gi392 (5->6, 6), s5005 gi318 (4->5, 5), s5005
  gi419 (4->5, 5), s6006 gi592 (5->6, 6), s6006 gi941 (4->5, 5), s6006 gi1242 (4->5, 5), s7007 gi480 (4->5, 5),
  s7007 gi551 (5->6, 6), s7007 gi1030 (4->5, 5). Same deck as the Heath games; the first difference recorded
  earlier is a fetch TARGET (Overgrown Tomb instead of Temple Garden), not a Heath hold. Row level is net faster
  (-0.006 to -0.011 per game).
- Galadriel d5 (5): s4004 gi1235 (5->6), s5005 gi234 (5->6), s5005 gi586 (5->6), s5005 gi1048 (4->5), s6006
  gi47 (4->5); d8 b0 new = the new case turn on all 5. Scry-land / cantrip line changes; deck net neutral to
  faster.
- Hinata d5 (2): s5005 gi384 (4->7, d8b0 5), s6006 gi156 (4->5, 5). T1/T3 cantrip/scry choice; the deck is net
  faster once the USER's depth-5 floor is separated out.
- Minotaur d5 s6006 gi937 (5->6, d8b0 6): T1 Secluded Courtyard instead of Bloodstained Mire. Not Heath (the
  deck has none, and the Mire record did not change). Most likely the Boars-batch fetch-pick changes in
  `9344fa594`..`ba307d48a` (provider-ranked battlefield picks), unproven.
- Alchemist d5 s6006 gi318 (5->6, d8b0 6): same kept hand and draws. New holds Wild Growth from T1 to T2 (no
  mana difference) and on T5 casts Utopia Sprawl + one Stroke of Genius where old cast Weather the Storm, Crab
  Umbra, two Strokes and Arbor Elf for the T5 mill kill. The Alchemist d5 rows are net faster (4.9867 -> 4.9800,
  5.0150 -> 5.0100, 4.9767 -> 4.9717).

**Mill caveat (for the accept note):** in Mill s5005 gi3 and gi4 the OLD binary built at `1443eeba0` loses
both games, at case settings and at d8 b0, so those committed GT wins (T5 / T5) do not replay from
`1443eeba0` itself. The new binary wins them (T6 / T7 at case settings, T6 at d8 b0). They are counted as "new
better" because the old arm cannot win them; their old GT entries are stale baseline, not something the new
engine lost.


## Anything that looks like a real regression

1. **Windswept Heath card data (`9344fa594`): Five Colour d0, Antilife s6006 gi117 (x2), Creature Giving x10.**
   The Heath record lost its `produces` hand-colour hint, so fixing heuristics treat a Heath in hand as
   colourless (details and fix candidate in the Five Colour section). The d0 cases are clear losses (holding
   the fetch leaves a T1 spell uncastable). The searched cases are unrecoverable at d8 b0, but net neutral to
   faster at row level. Not fixed here; owed to whoever owns Boars / card data, with an A/B on the five Heath
   decks.
2. **Prevent Damage s6006 gi47, T8 -> unwon (also unwon at d8 b0): `MTG_PD_LATER_HAND_HOLD`**
   (`636910330`, 2026-11-15, mana-clean "Hold known Earthquake red pip through armed payment"). Same kept hand
   and the same draws. On T4 the new line plays **no land** with two Battlefield Forges and an Ancient Tomb in
   hand, and on T5 again (it casts Spellshock instead). Flipping `MTG_PD_LATER_HAND_HOLD=0` alone restores T8;
   `_MULTI_QUAKE_HOLD` and `_SPELLSHOCK_HOLD` do not. A skipped land drop with lands in hand looks like a
   defect in the hold's interaction with land play, not a trade-off. One game (the PD rows are otherwise
   clean); hand to the mana-chain owner.
3. **19 unattributed d8 b0-unrecoverable games** (listed above: Creature Giving 10, Galadriel 5, Hinata 2,
   Minotaur 1, Alchemist 1). All in decks that are net faster or neutral at row level. Owed follow-ups.


## Proposed `--accept-with-regressions` note

The PARENT runs the accept in the primary checkout. Note text (one paragraph per line is fine):

```
Periodic overnight 2026-11-20 (since 1443eeba0, 320 src commits). Every searched PERSISTS game outside MB/Spirits (158) plus 10+10 sampled was replayed on both arms at d8 b0 (178): 76 recover, 9 same turn on both arms (old GT was budget luck), 2 new better (Mill), 59 slower but explained, 31 slower and unattributed at replay time (12 since attributed below), 1 new deck (Red Elemental).
Explained: Gods 24 = USER strict dredge MTG_GODS_RETRIEVAL_DREDGE (c39c1da1a/6cb9a7c85; blind -0.13; lever=0 restores 27/27). Hinata = USER d5 floor restore da17c0bbf (new binary + old value.json = old GT; d5 costs ~+0.08/game at b30). Cyclone: all 53 recover at d8 b0 (Ponder-first 7b95d828b + churn). Midrange Burn 24 = USER opponent-lands model c9022b11d (legacy lands restores 25/25). Spirits 10 = keep table a05ac4395.
REAL, owed fixes: (1) Windswept Heath card data in 9344fa594 (Boars I1) dropped its produces W/B/R/G hand hint (hand_fixing_produces is read only by Creature Giving's discard), so Heath counts as colourless in hand: Five Colour d0 +0.005/game (T1 holds Heath; reverting the Heath record restores 6/6), Antilife s6006 gi117 x2 and 10 of 20 Creature Giving games restored by the same swap. Fix candidate: restore produces as the hand hint like Scalding Tarn/Misty Rainforest, A/B on the five Heath decks. (2) PD s6006 gi47 T8 -> unwon from MTG_PD_LATER_HAND_HOLD (636910330) skipping T4/T5 land drops; =0 restores.
Unattributed, d8 b0-stable, net-faster/neutral decks (owed follow-ups): Creature Giving 10, Galadriel 5, Hinata 2 (s5005 gi384, s6006 gi156), Minotaur 1 (s6006 gi937), Alchemist 1 (s6006 gi318).
Mill caveat: s5005 gi3/gi4 old GT wins do not replay from 1443eeba0 (old binary loses both at case settings and d8 b0); new wins them T6/T7.
See docs/design/overnight-accept-2026-11-20.md (branch wip/overnight-accept-1120).
```
