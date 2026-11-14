# Handoff to the mana agent: overnight-tier findings traced to mana changes (2026-11-14)

**From:** the Spirits analysis session, while rebaselining the periodic overnight tier on 75ebee43a (origin 246e34da6 +
one Spirits ledger commit). **For:** whoever owns the mana-clean queue (`docs/design/mana-clean-pass-checkpoint.md`,
items A / C+I / D). USER 2026-11-14: *"If any unrecoverable items or overall regressions end up being caused by mana
changes we may end up wanted to create a handoff doc for the mana agent and then rebaseline. It would probably be best
to pass any items directly related to the mana system there."* Also USER, on the slower games: *"Draws Diverge is not an
acceptable explanation, since at d8 b0 we should still be able to find the line."*

STATUS: COMPLETE (2026-11-14 ~08:30 UTC). Items 1, 2, 4 and 5 are yours; item 3 is a payment-ordering heuristic
(item I(2)) for you to judge. Everything not mana-related stays with the Spirits session (see "Not yours").

## How the items were found
- Overnight tier (PGO, src 817876230ddf) vs the committed overnight GT (last periodic accept fec16d7d2, 2026-11-12):
  178 configs changed; searched slower 297, faster 1225. Classifier (4x / 16x the case budget): 19 churn, 278 PERSISTS.
- Searched rows that got WORSE outside the decks that changed deliberately (Martyr keep table, Morph INT work):
  niv d3 s4004 / d5 s5005 / d5 s6006, bruna d5 s7007, dragons d5 s4004, edf d5 s5005, cyclone d3 s5005,
  alchemist d5 s7007 (cyclone + alchemist: every slower game recovers at 4x/16x).
- **Attribution by lever flip** (new binary, one default-ON lever =0 at a time, 26 generic levers added since
  fec16d7d2; probe `build/scratch/spirits/leverprobe/`). Only these three levers restore any of the three like-for-like
  (identical kept hand + draws) slower games:

| game | old -> new | lever =0 that restores the old turn |
|---|---|---|
| dragons d5 s4004 gi363 (seed 4367) | T5 -> T6 | `MTG_PREPAY_WILD_TRUE` (item A) |
| niv d5 s7007 gi380 (seed 7387) | T5 -> T6 | `MTG_PREPAY_WILD_TRUE` or `MTG_PREPAY_WILD_LITERAL` (item A) |
| bruna d5 s7007 gi432 (seed 7439) | T4 -> T5 | `MTG_PAY_HAND_GENERIC` (item I(2)) |

Repro (one game, d5 b20 via the profile's value_play): manifest job
`{"deck": ..., "profile": ..., "games": 1, "seed": <seed>, "game_index": <gi>, "budget_ms": 20}`, binary at HEAD;
base = fec16d7d2 built in `.claude/worktrees/ab-base-fec16d7d2`. `MTG_TAPDBG=1` prints `[paydbg]` real payments.

## Item 1 (the clearest): item A's prepay refuses a LEGAL whole-turn bill -- a completeness loss
Dragons gi363, turn 5. Board: 3 Mountains (after the land drop), Gruul Turf ({R}{G} bundle), Sol Ring ({C}{C}), one
Urza's Incubator naming Dragon; hand: a second Urza's Incubator, 2x Lathliss, Dragon Queen. The bill is legal and exact:
Incubator {3} = G + CC, then each Lathliss {4}{R}{R} - 4 = {R}{R}, so RRRR from the three Mountains + Turf's R.
- **Base (fec16d7d2):** the whole-turn prepay funds all three spells (`[paydbg] cost={3} ok=1 taps:` empty, then
  `{R}{R} ok=1` twice) -> two Lathliss, win T5.
- **New:** the prepay does not fund the turn; the per-spell payer pays the Incubator's {3} with **Mountain + Sol Ring**
  (an {R} spent on generic), Lathliss #1 with Gruul Turf + Mountain (the {G} wasted), Lathliss #2 `{R}{R} ok=0` -> T6.
- With `MTG_PREPAY_WILD_TRUE=0` the new binary wins T5 again.
- This contradicts round 2's "KNOWN GAPS (permissive, never lossy)": here the fix makes a payable line unpayable.
  Suspect: the bundle (Karoo {R}{G}) unit's colour mask under the true-colour booking (round 2 fixed the dual's mask;
  a bundle's two units may still be booked as one source's colour).

## Item 2: Niv gi380 -- same lever family, NOT yet traced
Turn 4: the new arm casts Tandem Lookout before Preordain; turn 5 it no longer finds the Niv-Mizzet kill (old T5,
new T6). Restored by either PREPAY_WILD lever. Trace with `MTG_TAPDBG=1` as for item 1.

## Item 3: Bruna gi432 -- item I(2)'s hand-generic ordering changes the land sequence
T1 old Remote Farm / new Razorverge Thicket; old wins T4 (Sol Ring + Glittering Wish + Bruna), new T5. Restored by
`MTG_PAY_HAND_GENERIC=0`. A payment ORDERING heuristic changing a land-drop choice from turn 1 is worth a look:
the land choice is searched, so the payer's tie-break is leaking into the search's evaluation.

## Results (were PENDING)
**d8 b0 recovery** (`build/scratch/spirits/d8b0/`): 278 overnight PERSISTS games + 2 Spirits regression games, each
replayed at `--depth 8 --budget-ms 0` (ignore_play_profile) on HEAD: **277 unrecoverable**. Single-lever attribution
at the shipped setting (`build/scratch/spirits/attr/`, 9,339 one-game jobs): mana-system ones below; the rest are
Martyr/Morph keep-table adoptions and the Spirits fetch-hold drop (not yours).

| deck / game (seed, gi) | old -> new | d8 b0 | restored by =0 | old line LEGAL? (`MTG_PREPAY_WILD_TRUE=0 MTG_PREPAY_WILD_AUDIT=2`) |
|---|---|---|---|---|
| dragons overnight d5 s4004 (4367, 363) | 5 -> 6 | 6 | PREPAY_WILD_TRUE | old taps ILLEGAL (`[pwt] T5 ... gen=[R,G]`: Turf's G paid an R pip), but a legal assignment EXISTS (G -> the Incubator's generic) -- **lossy** |
| niv overnight d5 s7007 (7387, 380) | 5 -> 6 | 6 | WILD_TRUE or WILD_LITERAL | ILLEGAL (`T5 ... gen=[U,U]`) -- correctness cost |
| niv overnight d5 s6006 (6528, 522) | 6 -> 7 | 7 | WILD_TRUE | not audited |
| niv overnight d5 s7007 (7251, 244) | 3 -> 4 | 4 | WILD_TRUE | not audited |
| edf overnight d5 s5005 (5554, 549) | 4 -> 5 | 5 | WILD_TRUE or WILD_LITERAL | not audited |
| bruna overnight d5 s7007 (7439, 432) | 4 -> 5 | 5 | **PAY_HAND_GENERIC** | n/a (ordering) |

**Held-out A/B** (`build/scratch/spirits/heldout_ab/paired.txt`; seeds 670000+, 2000 paired games per row; base
fec16d7d2 vs HEAD, byte-identical deck files): alchemist d5 -0.0005, bruna d5 0, cyclone d3 +0.0005 / d5 -0.0005,
dragons d5 0, edf d5 +0.0005 -- all neutral. **Niv is worse in general: d3 +0.0030 (1 better / 6 worse, t 1.90), d5
+0.0030 (1 / 7, t 2.12).** Attribution of the 13 worse Niv games (`niv_worse.json`): 11 restored by
`MTG_PREPAY_WILD_TRUE=0`, gi815 (d3+d5) by `MTG_PAY_HAND_GENERIC=0`. At d8 b0 five recover (gi599, gi641, gi1210 x2,
gi815 x2); four do not (gi1138, gi1283, gi949, gi1000). Legality audit of the old line:

| held-out game (seed 670000+gi) | old line | verdict |
|---|---|---|
| gi1283 (d3, d5), gi949 (d3, d5), gi1000 (d5) | ILLEGAL (`[pwt] ... ILLEGAL`, viol=1) | correctness cost, fine |
| **gi1138 (d3, d5)** | **LEGAL** (viol=0, real 0) | **lossy: the legal T4 line is unfindable at d8 b0** |

## Item 4: gi1138 (Niv, held-out seed 671138) -- a legal line lost (like item 1)
Old arm legal by the audit; `MTG_PREPAY_WILD_TRUE=0` or `_LITERAL=0` restores T4; HEAD gives T5 at d5 b20 AND at
d8 b0. Repro: Niv deck, `{"games":1,"seed":671138,"game_index":1138,"budget_ms":20}`.

## Item 5: the general Niv cost (+0.003 t at d3 and d5)
Mostly the CR 106.1b correctness cost already noted in round 2 ("niv +3 net"), but items 1 and 4 show part of it is a
completeness hole: when the true-colour booking refuses an ILLEGAL assignment it does not then find the LEGAL
re-assignment of the same units (e.g. a bundle's G moved onto generic). Please re-check the round-2 claim "KNOWN GAPS
(permissive, never lossy)" against items 1 and 4.

## Mana-chain follow-up (Codex, 2026-11-14; WIP until gates finish)
- **Item 1: repaired in `wip/item-W-defer`.** The prepay now prices the second Incubator's same-line discount;
  the literal generic credit permits reassignment. Dragons gi363 wins T5 at shipped settings and d8 b0.
- **Item 2: correctness cost retained.** Niv gi380/522/244 remain T6/T7/T4; their old faster payments used
  wrong-colour mana (CR 106.1b). No false claim of a legal recovery is made for them.
- **Item 3: residual open.** The Karoo generic credit is implemented, with an ordinary-payment-first retry to
  avoid consuming Bruna's blue on Glittering Wish. Bruna gi432 is again T4 when `MTG_PAY_HAND_GENERIC=0`, but
  remains T5 with that shipped lever on, including at d8 b0. Wish targets are not in its hand-demand count.
- **Item 4: residual open from the earlier literal fix.** Niv gi1138 and EDF gi549 still win a turn later at
  d8 b0, with or without the new defer lever. They need their own legal-line attribution.
- The round-2 "permissive, never lossy" claim was corrected in `mana-clean-pass-checkpoint.md`.
- Validation in the Codex worktree: units 3093/3093, PGO built; smoke searched slower 0 and regression
  searched slower 0, with four same-score regression line changes inspected. Strict reference replay had
  no play drift or enum gap. The held-out main-deck A/B (46,400 paired games) has per-deck net <= 0:
  Dragons -2 turns, Niv 0 (one faster, one slower), all other decks 0. Niv's slower d5 gi2565 is
  undergoing the required d8 b0 recovery check; 2HG A/B and GT promotion remain pending.

## Not yours (kept by the Spirits session)
- Spirits 8 games: all restored by `MTG_SPIRITS_NO_FETCH_HOLD=0` (the USER's fetch-hold drop) -- a Spirits provider
  prune; being handled there (horizon-only variant under A/B).
- Martyr 168 / Morph 91 games: restored by the deck's pre-keep-table artifacts (different kept hands).
- Morph 3 games (d3/d5 s4004 gi64, d5 s7007 gi29): keep table + `MTG_MORPH_FETCH_RULE` (or SAMETURN_HARD_CAST /
  DOUBLER_AFTER_EXALTED for gi29) -- for the Morph owner.
