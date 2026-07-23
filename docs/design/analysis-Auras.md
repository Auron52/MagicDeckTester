# Auras deck — analysis ledger

Selesnya (GW) Bogles / hexproof-auras aggro. Game plan: land a hexproof 1-drop
(Slippery Bogle / Gladecover Scout) or Kor Spiritdancer, pile Auras on ONE creature,
attack for a fast clock. Value engines: Kor Spiritdancer (draw-on-aura + per-aura buff)
and Light-Paws (cast aura → tutor+attach a cheaper aura).

Deck file: `decks/Auras/Auras.cod`. Branch: `phase-1-2-deck-analyzer`.

## >>> STATUS: COMMITTED + Stage-6 REPORTED (d00e045 auras, 8d4bdef analyzer) — 2026-07-22 <<<
Auras analysis COMPLETE + committed + Stage-6 delivered. Card-scores-only profile shipped
(`decks/Auras/Auras.profile.json`). Analyzer simplified: analyze now generates ONLY card scores
(mulligan optimisation / land-grid DELETED per user).

**Stage-6 headline (representative, 200 games/seed 700001, budget 20ms):** avg turn-to-win
**d0(greedy)=4.740 · d3=4.465 · d5=4.465** (THE metric, unwon=max+1; lower=better). d3 and d5 share
digest `dca0f3a86f64c939` → search CONVERGES by depth 3 (deeper lookahead buys nothing on this
low-branching linear aggro deck); search over greedy = −0.275t. Monotone non-increasing, plausible
Bogles T4–5 clock. nonconv=0, fd-diverge≈0 (1/100 residual = accepted Light-Paws shuffle variance).

**Stage-5d claude-play sweep DONE (2026-07-23, 50 games, seeds 900001..900050, Sonnet players):**
50/50 played, 50/50 Claude wins, 0 agent errors, ~9.6 min, **802,669 output tokens** (≈16k/game — the
cost the 100-game run blew; 50 is affordable). **1 verified engine bug found + FIXED:** the human-play
`plan_signature` dedup (`TurnSolver::EnumeratePlans` ~L5885) keyed on tutor_target/chosen_x/… but NOT
`enchant_target`, so plans differing only in an aura's target collapsed to the first-enumerated creature
— a dead-decision bug hiding legal (sometimes better) aura targets from human/claude-play (the real
Solve() search was unaffected, so no GT/win-turn impact). Fix = add `enchant_target` to the human-play
sub-signature (human-play-gated → autonomous byte-identical: Stage-6 d5 digest `dca0f3a86f64c939`
unchanged). Verified at gi=0 T4: Ancestral Mask now offers all 3 creatures, Daybreak Coronet 2 (Gladecover
Scout correctly excluded — no aura). 3 faster-than-search WEAK-signal games (gi=14 claude T8 vs search
unwon-in-10; gi=21/47 claude T7 vs search T8) = the d5/200ms search budget missing a fast line on
slow-draw games, NOT a bug — noted for a future search-quality look. NOT pushed (left for user).

## >>> (historical resume notes) <<<
**DONE + verified:** aura-attach mechanic + all 22 cards (build clean, coverage clean, costs/P-T/keywords
match Scryfall); fd-diverge systematic bug FIXED (rollout `m_number` stamp, TurnSolver.cpp `cast_number`
×3) — residual 1/100 = accepted Light-Paws shuffle draw-variance; nonconv=0; provider routing FIXED
(`th`-narrowing, Auras→generic, avg 4.7→4.3); viewer enchant_target wired (Bucket A) + auditor/DECISIONS.md
updated (5h STATIC passes). All engine changes UNCOMMITTED (user hasn't asked to commit) but survived a
Windows-update kill; binary `build/Release/mtg` fresh + matches source.

**IN FLIGHT:** Stage-4 profile regen — `python scripts/analyze_deck.py decks/Auras/Auras.cod --no-rebuild`
(log `/tmp/auras_profile.log`; writes `decks/Auras/Auras.profile.json`). If killed again, just rerun it.

**REMAINING (do after profile exists):**
1. **5b multi-depth sanity** (no profile needed): `MTG_DUMP_WINS=1 ./build/Release/mtg decks/Auras/Auras.cod
   --games 40 --seed 6000 --depth {0,3,5} --budget-ms {200,300,500} --threads 1 2>&1 | grep '^\[win\]'` —
   confirm wins non-increasing w/ depth, turns plausible (~T4-5), read any outlier.
2. **5h full viewer sweep** (needs profile): `python scripts/audit_viewer_decisions.py decks/Auras/Auras.cod
   decks/Auras/Auras.profile.json 6000 20` — confirm no HARD MISS; enchant_target variants surface.
3. **5d claude-play backstop** — SCOPE DOWN from 100 (cost): ~4-6 games via the claude-play skill
   (`.claude/skills/claude-play.md`), disclose the scoping; look for legality/misplay flags only.
4. **Stage 6 report + avg-turn-to-win**: run a representative multi-seed batch at suite settings for the
   avg (THE metric, unwon=max+1), OR the regression suite. Present Stage-6 report using the 6a draft below.
Full worked state is in the sections below; the 6a disclosure table is already drafted.

## User design decisions (2026-07-22)
- **Aura modeling = goldfish-collapsed + lifelink.** Model each aura's **power bonus +
  scaling** faithfully. **Model LIFELINK** as a real tracked grant (user: life-total decks
  are coming). Leave the other keywords **inert** (disclosed 6a): trample, flying, first
  strike, vigilance, reach, hexproof, protection, umbra/totem armor. No double strike or
  haste in this deck (confirmed) so nothing else moves the clock. Toughness is currently
  inert (passive opp) but stored anyway (free, faithful).
- **Approach = full build now** (mechanic + all 22 cards + verify to convergence).

## The engine gap
`Permanent::attached_to` is a **dead stub** — nothing reads/writes it. There is NO
aura-attach mechanic. This is the Tier-3 build. The combat clock, lords, counters, tokens,
lifegain infra all already exist; auras just need to feed `EffectivePower()` + lifegain.

## Card catalog (authoritative Scryfall, fetched 2026-07-22)

### Creatures (bodies + engines)
| Card | Cost | P/T | Notes / modeling |
|---|---|---|---|
| Slippery Bogle | {G/U} | 1/1 | Hexproof (inert). vanilla_creature. Hybrid — deck pays G. |
| Gladecover Scout | {G} | 1/1 | Hexproof (inert). vanilla_creature. |
| Kor Spiritdancer | {1}{W} | 0/2 | **+2/+2 per Aura attached to it** (dynamic self-buff) + **cast Aura → may draw** (trigger). |
| Light-Paws, Emperor's Voice | {1}{W} | 2/2 | Legendary. **Aura you control enters (if cast) → tutor an Aura MV ≤ that aura, diff name than each Aura you control, put onto battlefield attached to Light-Paws, shuffle.** |

### Auras (all "Enchant creature", attach → grant P/T + kw)
| Card | Cost | Bonus | Keywords | Special |
|---|---|---|---|---|
| Ethereal Armor | {W} | +1/+1 **per enchantment you control** | first strike (inert) | SCALING |
| Rancor | {G} | +2/+0 | trample (inert) | returns to hand on death (**inert** — no removal) |
| Daybreak Coronet | {W}{W} | +3/+3 | first strike, vigilance (inert), **LIFELINK** | **enchant creature WITH another Aura** (restriction) |
| Armadillo Cloak | {1}{G}{W} | +2/+2 | trample (inert), **LIFELINK** ("deals damage → gain that much life") |
| Hyena Umbra | {W} | +1/+1 | first strike, umbra armor (inert) | |
| Spirit Mantle | {1}{W} | +1/+1 | protection from creatures (inert) | |
| Spider Umbra | {G} | +1/+1 | reach, umbra armor (inert) | |
| Ancestral Mask | {2}{G} | +2/+2 **per OTHER enchantment on battlefield** | — | SCALING (goldfish: opp has none → = other ench you control) |
| Alpha Authority | {1}{G} | **+0/+0** | hexproof, can't-be-blocked-by->1 (inert) | **zero power**; only matters via count/Kor/lifelink-none |
| Gryff's Boon | {W} | +1/+0 | flying (inert) | {3}{W} graveyard-return (**inert** — never in yard) |
| Audacity | {G} | +2/+0 | trample (inert) | dies → draw (**inert** — never dies) |
| All That Glitters | {1}{W} | +1/+1 **per artifact and/or enchantment you control** | — | SCALING |
| Spirit Link | {W} | **+0/+0** | — | **LIFELINK** ("deals damage → gain"). zero power. |
| Lion Umbra | {G}{G} | +3/+3 | vigilance, reach, umbra armor (inert) | **enchant MODIFIED creature** (restriction — needs existing aura/equip/counter) |

### Lands
| Card | Modeling |
|---|---|
| Horizon Canopy | {T},pay 1 life: G/W + {1},{T},Sac: draw. → per-tap life + `sacrifice_draw_cost`. |
| Razorverge Thicket | enters tapped unless ≤2 other lands; {T}: G/W. → **fastland** conditional-tapped (new param). |
| Brushland | {T}: C free; {T}: G/W deal 1 to you. → painland (free-C option). |
| Branchloft Pathway // Boulderloft | MDFC: front {T}:G, back {T}:W. → **[PARTIAL: modeled as GW dual; real card commits to one color at play. Overstates fixing marginally; deck is mono-pip-heavy]** — surface to user in 6a. |
| Forest / Plains | already covered. |

## Inert-keyword collapse (disclose 6a, user-approved 2026-07-22)
trample, flying, first strike, vigilance, reach, hexproof, protection, umbra/totem armor,
toughness — all provably inert vs the single passive opponent (never blocks/removes/deals
damage). **lifelink is MODELED** (Daybreak Coronet, Armadillo Cloak, Spirit Link + none
else). Rancor/Audacity/Gryff's-Boon graveyard clauses inert (no removal → auras never
leave the battlefield). Lion Umbra & Daybreak Coronet targeting restrictions ARE modeled.

## New decision surface (core invariant + 2c-ter)
WHICH creature an Aura enchants is a real search + viewer decision (like tutor_target).
Must be a plan variant (`enchant_target`) AND a new viewer decision type. Light-Paws'
tutor target is another decision.

## Engine changes made (Stage 2)
- **Permanent**: `int aura_attached_to` (stable m_number, copy-safe; the raw `attached_to*` stays dead).
- **CardParams**: `is_aura, aura_power_bonus, aura_tough_bonus, aura_grants_lifelink, aura_scale_kind
  {enchantments|other_enchantments|artifacts_enchantments}, aura_scale_power/tough, aura_enchant_requires
  {another_aura|modified}, aura_self_buff_power/tough (Kor), draw_on_aura_cast (Kor), aura_cast_tutor_attach
  (Light-Paws)`; plus `fastland_max_other_lands` (Razorverge).
- **SpellEffects.h**: `AuraBonusFor, CreatureHasLifelink, CountAuraScaleUnits, CreatureHasAura,
  CreatureIsModified, LegalEnchantTargets, ResolveEnchantTarget, PerformLightPawsAttach`; Kor draw in
  `FireOnCastTriggers`; fastland branch in `LandWouldEnterTapped`.
- **Combat**: `AuraBonusFor(p).first` added at PendingAttackDamage (projection), SimulateCombat (rollout),
  GameEngine::CombatPhase (executor); lifelink applied in the latter two.
- **enchant_target** search dimension: Action + StackEntry field; CollectActions emits one variant per
  legal creature (`LegalEnchantTargets`); threaded through apply_one / cast_by_name / CastSpellFromHand;
  aura attach + Light-Paws at both resolution sites (EffectHandler default case + apply_one else-if).
- **Parser**: hybrid `{A/B}` -> first colour (Slippery Bogle {G/U}->G). Inert keyword tags: Hexproof,
  Enchant, Umbra armor.
- **Viewer (2c-ter)**: enchant_target is a main_phase plan VARIANT (Bucket A); SummarizePlan labels it
  ("Rancor -> Kor Spiritdancer"); per-action JSON emits enchant_target + name. No new chooser needed.

## Stage 5 status
- Coverage clean; costs+P/T+keywords verified vs Scryfall (all match). Smoke: wins T4-6, avg 4.7 (d3/b300).
- **nonconv = 0** (40 games).
- **fd-diverge RESOLVED** (was ~35%, now 0/40 @ seed 6000; nonconv still 0). ROOT CAUSE: the rollout's
  plan-application (`TurnSolver::apply_one`) created each cast permanent with `perm.card = def.card` and
  never stamped the per-copy `m_number` (the definition's is 0), so EVERY rollout permanent had
  `m_number == 0`. `ResolveEnchantTarget` then returned 0 for every aura's `aura_attached_to`, and
  `AuraBonusFor` (`a.aura_attached_to == creature.m_number`) matched EVERY aura (att 0) against EVERY
  creature (m_number 0) -> every aura counted on every attacker + inflated Kor self-buff/Ancestral
  scaling. The executor (`EffectHandler::EnterBattlefield`) already preserves the real per-copy number
  (`perm.card.m_number = entry.source.m_number`), so it attached correctly -> the two paths disagreed.
  FIX: stamp `perm.card.m_number = cast_number` (the cast copy's id, captured before `zone.erase`) in
  both rollout entry blocks (creature + non-creature-permanent), mirroring the executor. Byte-identical
  for non-aura decks (Knights smoke identical before/after; `BuildSimKey` folds `m_name_hash`, never
  `m_number`). MTG_AURAS_NO_ENGINES temp gates removed.

## 5h viewer audit (static self-guard PASSES)
`audit_viewer_decisions.py --no-sweep` exit 0. New params registered: `is_aura` -> MAINPHASE_PARAMS
(enchant target = main_phase plan variant); `aura_cast_tutor_attach` -> DEFERRED_PARAMS (Light-Paws
fetch target, heuristic-picked, disclosed 6a like cascade); the rest -> INERT_PARAMS (effect/stat/trigger
detail). Oracle cross-check advisories, both triaged: Light-Paws "search" = the deferred fetch; Gryff's
Boon "target creature" = its {3}{W} graveyard-return (bracket-noted inert — never reaches graveyard vs
passive opp). Full sweep (verify variants actually surface) pending post-fix + profile.

## Provider routing BUG (found; fix after fd-diverge)
`SelectDecisionProvider` routes the Auras deck to **g_treasure** (Treasure-Hunt provider), NOT
GenericProvider, because Horizon Canopy's `sacrifice_draw_cost` matches the broad `th` signature
(`sacrifice_draw_cost.has_value() || cycling || etb_scry/surveil || discard_land_damage ||
DrawUntilNonland`). A generic aggro auras deck should ride GenericProvider. Most TH hooks are gated on
TH cards (Land's Edge / Treasure Hunt / Reliquary Tower — none here) so likely mostly inert, but must be
verified. FIX options: narrow the `th` signature to the real TH engine (DrawUntilNonland/Land's Edge) —
verify existing TH decks still route to g_treasure (they carry Treasure Hunt/Land's Edge) — OR add an
aura-archetype route to Generic ahead of `th`. **A/B Auras win-turns g_treasure vs g_generic; adopt the
routing fix only if byte-neutral-or-better, disclosed 6a.** (This is NOT the fd-diverge cause — the
provider is used identically in rollout + executor.)
CONFIRMED SAFE FIX (deck scan): only `treasure_hunt` + `Auras` match `th` at all; treasure_hunt carries
DrawUntilNonland + Land's Edge (discard_land_damage), Auras matches ONLY via sacrifice_draw_cost. So
narrow `th` to `discard_land_damage > 0 || tmpl==DrawUntilNonland` (drop cycling/scry/surveil/sac-draw as
independent triggers) -> treasure_hunt stays g_treasure (byte-identical GT), Auras falls to g_generic, no
other deck touched. Verify treasure_hunt regression byte-identical after.

## fd-diverge FINAL (systematic bug fixed; 1/100 residual = accepted draw-variance)
Final binary (m_number fix + provider fix): fd-diverge = **0/40 (seed6000), 1/30 (seed7000), 0/30
(seed8000) = 1/100 (1%)**; nonconv = 0. The single residual (seed 7022) is **Light-Paws fetch-shuffle
draw variance**, NOT an over-count: persists at b2000 (so not budget/re-solve-depth); the committed line
casts 2 auras on T4 (Hyena Umbra + Spirit Link) but the realized game drew Spirit MANTLE instead of Spirit
Link because Light-Paws' "then shuffle" reordered the library — the predicted T4 win is physically
achievable, just realized a turn later on a divergent post-shuffle draw. This matches the documented
"every turn-later game has a divergent post-fetch draw — fetch-shuffle DRAW VARIANCE, not a bug" precedent
(antilife exalted / [[fd-diverge-and-clairvoyance-isolation-2026-07-11]]). ACCEPTED + disclosed 6a.
Provider fix net-positive: Auras avg 4.7 -> 4.3 (d3/b300, generic > mis-routed g_treasure). treasure_hunt
unchanged (still g_treasure; th-detection value identical by construction — regression suite confirms).

## Stage 6a — encoded heuristics & assumptions disclosure (draft)

**1. Global engine assumptions in force (shape the numbers):**
- Single PASSIVE opponent — never blocks, casts, removes, deals damage, or gains/prevents life. This is
  what makes the aura keyword-collapse valid.
- Clairvoyant search over a known, deterministically-shuffled library; results deterministic + thread-invariant.
- **First-main only** — `DeckUsesSecondMain` does NOT fire for this deck (no spectacle, no combat-mana). All
  auras are cast pre-combat; correct (combat creates no new resources for a goldfish).
- Depth/budget: suite settings (regression harness). Deterministic lookahead-bottoming when active.

**2. Card-modeling simplifications (bracket notes, all user-approved 2026-07-22):**
- **Inert keyword collapse** — trample, flying, first strike, vigilance, reach, hexproof, protection,
  umbra/totem armor, and TOUGHNESS are not modeled (provably inert vs the passive opponent: nothing blocks,
  targets, or damages our creatures). **Lifelink IS modeled** (Daybreak Coronet, Armadillo Cloak, Spirit Link).
- **Graveyard clauses inert** — Rancor (return to hand), Audacity (dies→draw), Gryff's Boon ({3}{W}
  graveyard-return): the aura never leaves the battlefield (no removal), so these never fire.
- **Slippery Bogle {G/U}** modeled as {G} (deck runs no blue; hybrid parser → first colour).
- **Brushland** modeled as a G/W painland; free {C} mode not modeled (our life loss is inert for the
  opponent-life clock; only matters to a future life-total deck).
- **Branchloft Pathway** modeled as a G/W dual (real card is an MDFC committing to one colour at play) —
  marginal fixing overstatement, near-inert in this mono-pip-heavy deck. **← flag to user.**

**3. Deck/archetype DecisionProvider heuristics:**
- Routes to **GenericProvider** (after the `th`-signature fix) → **overrides nothing** = pure search within
  the global assumptions. No deck-specific narrowing heuristics. The enchant-target choice is a full search
  decision (plan variants), NOT a provider heuristic. (Routing fix disclosed: Horizon Canopy's
  sacrifice_draw_cost previously mis-routed the deck to the Treasure-Hunt provider.)

**4. Play-viewer auto-resolved decisions (2c-ter / 5h):**
- **Light-Paws fetch target** — which Aura it tutors is heuristic-picked (highest power contribution), NOT
  surfaced. Disclosed known gap (like cascade target); could become a search decision. `DEFERRED_PARAMS`.
- **Kor "may draw"** — auto-resolved as always-draw (strictly good in goldfish); no meaningful choice.
- Enchant-target FALLBACK (`ResolveEnchantTarget`) only fires if a target were ever unset — it never is
  (CollectActions always emits a concrete target); the real choice IS surfaced as labeled plan variants.

## Progress
- [x] Stage 1 coverage. [x] Stage 2 engine + 22 cards. [x] Stage 3 coverage clean. [x] 2d-bis audits.
- [x] Stage 4 profile — card-scores-only (24 scores, NO_GATE), regenerated post fd-diverge fix.
- [x] Stage 5 verify: nonconv=0; fd-diverge fixed (≈0, 1/100 accepted variance); multi-depth monotone
      (d0=4.740, d3=d5=4.465 converged); 5h viewer surface wired (enchant_target variants).
- [x] Stage 5d claude-play sweep — 50 games, 1 bug found+fixed (enchant_target dedup), see below.
- [x] Stage 6 report — delivered to user 2026-07-22 (headline avg above; 6a disclosure below).

## Claude-play sweep
- commit: `<HEAD after the enchant_target dedup fix>`
- seeds: 900001 (50 games, seeds 900001..900050); Sonnet players; 802,669 output tokens
- flags: 0 unresolved
  - [RESOLVED] enchant_target human-play dedup collapse — `TurnSolver::EnumeratePlans` `plan_signature`
    did not key on `enchant_target`, so multi-target aura plans collapsed to the first-enumerated
    creature in the human/claude-play menu (autonomous search unaffected, GT byte-identical). Fixed by
    adding `enchant_target` to the human-play sub-signature. Verified at gi=0 T4 (all legal targets now
    surface; Daybreak Coronet restriction still respected).
