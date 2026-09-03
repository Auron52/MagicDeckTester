# Treasure-vs-dork tap choice, generalised — and the doctrine the measurement reversed (2026-09-02)

**Ask (USER):** support the Mirrorwing treasure-vs-dork case "in a more general manner, since for
example with Frontline Heroism or with no Mirrorwing/Zada the dork is the right tap."

**What shipped:**
1. **The Mirrorwing provider pins are now generic** (byte-identical refactor, smoke 48/48
   zero-changed): `SpendOneShotsFreely` (= `bodies_are_multipliers`) and the whole-board
   copy-magnet `ReserveCreatureHold` moved from `MirrorwingProvider` into the `DecisionProvider`
   base. Both conditions are param-keyed (`copies_solo_targeted_spells` x `solo_target_trick`), so
   any future magnet deck inherits them and every magnet-less deck is untouched by construction.
2. **`MTG_HEROISM_MAGNET_TRAIT` (ADOPTED 2026-09-02, default ON): a live copy-token enchantment
   (`frontline_copy_tokens` -- Frontline Heroism) counts toward `PlanTraits::copy_magnet_live`.**
   Its trick turns have a magnet's go-off shape: every qualifying cast mints a hasted body, the
   copy doubles a Gold Rush's mint, and the whole board wants to swing -- so the one-shot spend
   bias and the whole-board creature hold treat it as a magnet.

## The measurement reversed the intuition — three refutations and one winner

Apparatus: 20-cell Mirrorwing suite footprint (smoke+regression+overnight, 13,625 games/arm),
pooled multi-arm batches, ctl byte-matching GT per-game in every cell of every run.

| variant | idea | result |
|---|---|---|
| shot | ladder holds Treasures harder than dorks, unconditionally | **+0.0028, 18F/50S — refuted** |
| shot2 | ...only when no magnet is live | identical numbers — refuted (the firing turns were all pre-magnet anyway) |
| mag | a magnet cast in this plan counts as live | +0.0007, 1F/11S — refuted |
| hershot | Heroism-as-magnet + the gated ladder flip | +0.0010, 46F/58S — the flip still loses |
| **her** | **Heroism-as-magnet trait alone** | **−0.00125, 28F/15S — winner** |

`her`'s searched tiers are the signal: **d5 7 faster / 0 slower, d3 12 faster / 2 slower**, with
the same games (gi282, gi295, gi144, gi161, gi38, gi95) a full turn faster at BOTH depths across
disjoint seed blocks — replicated, not noise. d0 is flat (+0.0003, 8/13).

**Why the intuitive rule ("the dork is the right tap — it untaps, the Treasure is gone forever")
loses on this deck:** traced on mw_regression_d0 gi284 (4→7 under the flip). T4 casts Frontline
Heroism + double Fortifying Draught — a go-off turn with NO magnet — and holding the Treasure
taps the dorks, which forfeits the winning attack; the game then drifts. On a deck that RE-MINTS
Treasures (Gold Rush, doubled under Heroism), an attacker tapped today costs more than a Treasure
banked for tomorrow, even pre-magnet. The gi81 shape (the specific Treasure a later kill needs)
exists — 18 faster games under the flip — but loses 50-vs-18. So Heroism turns out to be
magnet-LIKE, not magnet-less: the user's example inverted, and the general rule is "count it as
one", which is exactly variant `her`.

The magnet-less half of the ask (a deck with Treasures and NO magnet/Heroism at all) has no deck
in the suite to measure on — Gold Rush is the only minter and only Mirrorwing plays it. The
generic base holds (§2b: hold the one-shot when the turn pays without it) already covers the
slack-turn case for such a deck; the COMPETING-turn order for it stays the shipped
creatures-first, which is also what Mirrorwing's own pre-magnet turns measured as correct. If a
genuinely magnet-less treasure deck ever joins the suite, re-derive from this record — do not
re-build the ladder flip blind (the TurnSolver ladder comment points here).

## Adoption

ADOPTED 2026-09-02 (user approved): the reader now defaults ON (`EnvOn("MTG_HEROISM_MAGNET_TRAIT",
true)`); all three GT tiers rebaselined at the flip — only Mirrorwing cells moved, matching the
`her` arm's measured per-game movers.

## The payment-layer follow-up (measured 2026-09-02): MTG_HEROISM_FRESH_HOLD wins — ADOPTED

The deferred question — the fresh-hold `CopyMagnetLive` gate and the Gold-Rush-positive net-mana
rule still counted only true magnets — was measured on the same 20-cell footprint (13,625
games/arm, pooled 4-arm batch, ctl byte-matched GT):

| variant | idea | result |
|---|---|---|
| **hfresh** | **live Heroism unlocks same-turn spend of fresh Treasures (`PaySacSpendableNow`)** | **−0.00257, 46F/10S — winner, staged** |
| hnet | +1 `TreasureSpellNetMana` resolution per live Heroism (Gold Rush net-0) | EXACT NULL — 0 changed games; deleted |
| hboth | both | identical to hfresh (consistent with the null) |

`hfresh` is double the magnet-trait adoption's effect: searched 18F/4S with the same games faster
at both d3 and d5 (gi156, gi9, gi80, gi281 across disjoint seed blocks), d0 28F/6S including
**3 unwon→won** (gi47, gi259, gi1272) against one marginal 7→unwon (regression d3 gi34) and
knife-edge churn (gi126 4→5 gives back a game the trait adoption sped up; gi247 4→5 at both
depths). Mechanism trace (overnight d3 s6006 gi9, seed 6015, 4→3): both lines cast T2 Heroism +
T3 double Gold Rush, but only with the lever may the Heroism-doubled fresh mints pay same-turn —
the ON line attacks T3 for exactly 20 instead of 16.

ADOPTED 2026-09-03 (user approved, with a standing policy stated: adopt all changes with no
quality or performance drawbacks): reader defaults ON, all three tiers rebaselined — exactly the
20 Mirrorwing cells moved and every one byte-matched the battery's `hfresh` arm.
