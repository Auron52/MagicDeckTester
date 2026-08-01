#pragma once

// Concrete DecisionProvider implementations + per-deck selection.
//
// GenericProvider is the DEFAULT and the single source of truth for today's behavior:
// its hooks reproduce the param-driven heuristics verbatim (in Stage 0 by forwarding to
// the shared free functions in SpellEffects.h; later stages move the bodies here). Every
// existing deck routes through Generic until the archetype subclasses land, so the
// refactor stays byte-identical. Archetype subclasses (AntiLifegain / TreasureHunt /
// Vial) will subclass GenericProvider and override ONLY the hooks they customize,
// delegating the rest.

#include "DecisionProvider.h"

struct Decklist;

// Standing unpruned-vs-pruned A/B switch (MTG_UNPRUNED). When set, every
// search-narrowing heuristic returns its MAXIMALLY-PERMISSIVE value so the general
// search explores the full branch space the heuristics would otherwise prune --
// the audit tool for "are our heuristics costing us lines?". Declared here so the
// shared tutor/fetch candidate functions in SpellEffects.h can honour it too.
// Default off => byte-identical. Defined in DecisionProviders.cpp.
bool DecisionUnpruned();

// GRANULAR un-pruning: the global MTG_UNPRUNED opens EVERY branch-narrowing gate at once,
// which explodes the search (the full 100-game d5 audit does not finish in hours because a
// few gates -- tutor/fetch full enumeration, dig, search-ordering -- each multiply the tree).
// To isolate WHICH heuristic costs a line, MTG_UNPRUNE=<comma/space list of gate names> opens
// ONLY the named gates. Each DecisionUnpruned() callsite names the gate it belongs to below.
//
// Invariants that keep this byte-identical to before:
//   * MTG_UNPRUNED set  -> DecisionUnpruned(any gate) is true (global opens all) == today.
//   * neither env set   -> always false == today.
// Only the NEW MTG_UNPRUNE=<subset> mode depends on a callsite's gate label; the two knobs
// above ignore it. Gate names (case-insensitive) accepted in MTG_UNPRUNE: altpayload, tutor,
// fetch, dig, xspell, ponder, groupcap, comboline, searchorder, redirect, drawengine.
// "all" == every gate.
enum class UnprunedGate
{
    AltPayload,   // alt-cost payload cast enumeration + its auto-fire suppression (Invigorate/Reverent)
    Tutor,        // tutor candidate set: every legal target instead of the narrowed pick
    Fetch,        // fetchland candidate set + TurnSolver's fetch-target search cap
    Dig,          // consider a dig whenever a dig source exists (Treasure Hunt)
    XSpell,       // X-spell: full 1..max_affordable range instead of the single max-X pick
    Ponder,       // cast_reorder / scry-keep: the searched 2-way keep/bottom branch
    GroupCap,     // plan enumeration group cap (breadth policy) disabled
    ComboLine,    // ritual finisher combo-line cut disabled (search finds it unaided)
    SearchOrder,  // cast/resolve ordering search enabled
    Redirect,     // pump-then-Swords redirect heuristic disabled (search owns the target)
    DrawEngine,   // draw-engine (flood) cast gate ungated -- always a cast choice
    SacColor,     // Lotus Bloom SacForMana / Apex colour: open ALL five colours instead of the
                  // deck's spell-cost colours (the narrowed default candidate set)
    AccelPrefix,  // Dragonstorm acceleration-prefix collapse disabled: enumerate the full 2^K ritual-
                  // accelerant powerset instead of only the K+1 cheapest-first prefixes (DragonstormProvider)
    PayoffPrune,  // Dragonstorm payoff-prune disabled: keep the ritual-accelerant subsets that cast no
                  // payoff (Dragon/Dragonstorm/Apex) instead of dropping them
                  // (DragonstormProvider::PrunesAcceleratorWithoutPayoff)
    SpliceCollapse, // Dragonstorm Desperate Ritual splice-count collapse disabled: enumerate the full
                  // k=0..N-1 splice fan-out per copy instead of only the bare + max-chain families
                  // (DragonstormProvider::UseSpliceCollapse)
    _Count
};

// True if `g` should be un-pruned for this run: the global MTG_UNPRUNED opens all gates,
// else MTG_UNPRUNE=<list> opens the named subset. Same human-play suppression as the no-arg
// form. Default (neither env) => false.
bool DecisionUnpruned(UnprunedGate g);

// A/B gate for the learned mid-game evaluator (MTG_EVAL_MODEL). Default OFF: even when a deck ships
// an eval sidecar, the learned plan ranking is used only when this is set, so every existing ground
// truth stays byte-identical until an eval model is deliberately enabled. Reads the env once (mirrors
// DecisionUnpruned). See docs/design/learned-d0-policy.md.
bool UseLearnedEval();

// A/B gate for the learned leaf VALUE model (MTG_VALUE_MODEL). Default OFF: even when a deck ships a
// value sidecar, the search's horizon rollout is replaced by the learned estimate only when this is
// set, so existing ground truth stays byte-identical until deliberately enabled. Reads env once.
bool UseValueModel();

// Gate probe: run a deck once with the probe ON, then read QueriedGatesMask() to learn which gates
// have a live decision point for that deck. A gate NOT in the mask has no reachable callsite (no
// matching cards / rituals / dig source), so opening it provably changes nothing -- skip sweeping it.
// The mask accumulates across all threads/games since SetGateProbe(true). GateName maps enum->name.
void        SetGateProbe(bool on);
uint32_t    QueriedGatesMask();
const char* GateName(UnprunedGate g);

class GenericProvider : public DecisionProvider
{
public:
    std::vector<std::string> TutorCandidates(const GameState&, int, const CardParams&) const override;
    std::vector<std::string> FetchCandidates(const GameState&, int, const CardParams&) const override;
    bool        CanAutoFireAltPayload(const GameState&, int, const CardDefinition&) const override;
    bool        HasAnyDigSource (const GameState&) const override;
    bool        ShouldConsiderDig(const GameState&) const override;
    std::string SelectDigSource (const GameState&, const ManaPool&, bool&) const override;
    int         LandsEdgeFireCount(const GameState&, int) const override;
    bool        WantVialCharge(const GameState&, const Permanent&) const override;
    bool        ScryKeepOnTop(const GameState&, const Card&) const override;
    bool        CastEnablerFirst(const GameState&, const std::string&) const override;
    bool        DiscardLandsFirst(const GameState&) const override;
    bool        ShouldEmitRiskyAltPayload(const GameState&, int, const CardDefinition&) const override;
    bool        ShouldStageSpectacleDraw(const GameState&, int, const CardDefinition&) const override;
    bool        ShouldCastDrawEngine(const GameState&, int, const CardDefinition&) const override;
    std::string PostDrawKeepLandName(const GameState&, int) const override;
    bool        HasExtraLethalModel() const override;
    int         ExtraLethalDamage(const GameState&, const std::vector<const CardDefinition*>&) const override;
    bool        ArchetypeCardValue(const GameState&, const CardDefinition&, int, int&) const override;
    bool        ShouldAttackWith(const GameState&, const Permanent&) const override;
    int         CastOrderRank(const GameState&, const CardDefinition&) const override;
    std::vector<int> XCandidates(const GameState&, const CardDefinition&, int) const override;
    int         ManaSourceRank(const GameState&, const CardDefinition&) const override;
    double      NcLandDropTempoBonus(const GameState&, int) const override;
};

// Anti-Lifegain combo (Tainted Remedy / Plague Drone / Aria / Reverent Silence): the
// deck whose damage flows through opponent-lifegain flipped to loss. Overrides the
// tutor/fetch/alt-payload/enabler-ordering hooks; inherits Generic for the rest.
class AntiLifegainProvider : public GenericProvider
{
public:
    std::vector<std::string> TutorCandidates(const GameState&, int, const CardParams&) const override;
    std::vector<std::string> FetchCandidates(const GameState&, int, const CardParams&) const override;
    bool CanAutoFireAltPayload(const GameState&, int, const CardDefinition&) const override;
    bool CastEnablerFirst(const GameState&, const std::string&) const override;
    bool ShouldEmitRiskyAltPayload(const GameState&, int, const CardDefinition&) const override;
    int  CastOrderRank(const GameState&, const CardDefinition&) const override;
    // Seeking the Grove drip is useful exactly when a lifegain->loss enabler (Tainted Remedy / Plague
    // Drone) is active -- it reverses the opponent's "gain 1" into 1 damage. Drives the two drip rules
    // (colours-not-{C}, and the end-of-main sweep); default false in the base provider.
    bool OpponentLifegainUseful(const GameState&, int) const override;
    // Exalted-aware attack declaration: a 0-power, no-attack-trigger dork must NOT swing alongside a
    // real attacker (it deals nothing and breaks the lone-attacker Exalted bonus from Ignoble
    // Hierarch); it attacks only as the sole eligible creature (to switch Exalted on / carry an
    // Invigorate pump). Required for mana-source reservation to be lossless here (a HELD dork that
    // then pointlessly attacked would forfeit Exalted).
    bool ShouldAttackWith(const GameState&, const Permanent&) const override;
    // Mana-hungry combo (dorks + on-curve enabler deploy) with NO land-as-resource mechanic -> the NC
    // search should ALWAYS develop mana, not just while establishing the base. Aggressive (ungated).
    double NcLandDropTempoBonus(const GameState&, int) const override;
    // Both tutors here fetch from a pool of exactly TWO distinct names: the deck's only enchantments
    // are Tainted Remedy and Aria of Flame, and it runs no artifacts at all -- so Idyllic Tutor
    // (enchantment) and Enlightened Tutor (artifact or enchantment) see the same two-card list. 2 is
    // therefore EXHAUSTIVE, not a prune: the train sweep's play digests are byte-identical from
    // width 2 all the way to 12, because there was never a third candidate to enumerate. The old
    // global 6 was pure enumeration cost here for a provably empty gain.
    int TutorSearchWidth() const override { return 2; }
};

// Treasure Hunt + Land's Edge: dig-when-stuck, Land's Edge fire count, deck-aware
// scry/surveil keep, and land-first discard. Inherits Generic for the rest.
class TreasureHuntProvider : public GenericProvider
{
public:
    bool        HasAnyDigSource (const GameState&) const override;
    bool        ShouldConsiderDig(const GameState&) const override;
    std::string SelectDigSource (const GameState&, const ManaPool&, bool&) const override;
    int         LandsEdgeFireCount(const GameState&, int) const override;
    bool        DiscardLandsFirst(const GameState&) const override;
    bool        ScryKeepOnTop(const GameState&, const Card&) const override;
    bool        ShouldCastDrawEngine(const GameState&, int, const CardDefinition&) const override;
    std::string PostDrawKeepLandName(const GameState&, int) const override;
    bool        HoldDeferredDropForLethal(const GameState&, int) const override;
    bool        HoldDeferredDropForFurtherDig(const GameState&, int) const override;
    bool        HasExtraLethalModel() const override;
    int         ExtraLethalDamage(const GameState&, const std::vector<const CardDefinition*>&) const override;
    bool        ArchetypeCardValue(const GameState&, const CardDefinition&, int, int&) const override;
    KeepGuard   KeepFloor(const std::vector<Card>&, int, bool) const override;
    // Cleanup discard: THE deck that makes this decision. Per 400 d0 games it discards 336 times;
    // every other deck in the suite is under 40 and five are at zero. The base rule sheds "the first
    // non-staged land in hand order", which for this deck is a choice among 4-22 lands spanning 13
    // distinct names -- a Reliquary Tower, three cyclers, a sac-to-draw, two depletion/storage
    // lands, two ETB-scry lands and four duals. Those are different cards, not copies of "a land".
    std::vector<int> CleanupDiscardCandidates(
        const GameState&, const std::vector<std::string>*) const override;
    // The only deck that opts into branching the ROLLOUT's cleanup shed. MTG_TH_DISCARD_WIDTH
    // overrides for the sweep; see the definition for the measured value.
    int CleanupDiscardSearchWidth() const override;
};

// Aether Vial decks (Slivers, Knights): the hand-aware vial charge policy.
class VialProvider : public GenericProvider
{
public:
    bool WantVialCharge(const GameState&, const Permanent&) const override;
};

// Goblins (Lackey / Matron / Muxus aggro). The deck rode GenericProvider until there was something
// MEASURED to put here -- an earlier attempt at a Goblins Lackey-put priority table was worth
// exactly 0.0000 and was correctly not shipped. This is the first hook that pays: Goblin Matron
// tutors for "a Goblin card" out of ~16 distinct Goblin names, and unlike every other tutor in the
// suite its targets are not close substitutes (Muxus and Mogg War Marshal are not the same fetch),
// so the search keeps finding value well past the provider's top few.
//
// Measured, MONOTONE with a knee at 8 -- the opposite shape to Hinata's dilution:
//   TRAIN (1001/2002/3003)   w2 -0.0294  w4 -0.1627  w6 -0.1846  w8 -0.2100  w12 -0.2108
//   HELD-OUT (4004..7007)    w12 -0.2740 (the best arm measured there too)
// 8 and 12 are statistically indistinguishable on train (0.0008 apart over 15 cases); 12 is taken
// because both seed sets rank it no worse and the axis is ADDITIVE -- the whole 3-deck regression
// makespan moves 38s -> 46s across the entire width range, not per-width multiplicatively.
class GoblinsProvider : public GenericProvider
{
public:
    int TutorSearchWidth() const override { return 12; }
};

// Mono-red Burn (Searing Blaze's landfall damage is the deck's signature): once it has enough
// lands in play (its curve tops at mana value 2), it BANKS further land drops so a future
// topdecked Searing Blaze has a land to play for its landfall (3-to-face instead of 1). Inherits
// Generic for everything else; the only override is the equal-value land-drop tiebreak.
class BurnProvider : public GenericProvider
{
public:
    bool PreferHoldLandDrop(const GameState&, int) const override;
};

// Hinata, Dawn-Crowned (UR Crackle / cost-reduction combo). Its spells slash their cost by
// Hinata's "{1} less per target", which the deck maximises by targeting extra/own/opponent
// permanents -- so its goldfish opponent must present real targets. Layer 2 grows this provider
// with the board-aware multi-target discount and the Reality-Spasm -> Crackle mana ritual.
class HinataProvider : public GenericProvider
{
public:
    bool OpponentPlaysLands() const override { return true; }
    // Gamble (and any unrestricted tutor) is narrowed combo-aware: while no Hinata is in play
    // or hand, fetch Hinata (a no-Hinata hand is a dead hand); once she is online, let the
    // search pick among the missing payoffs/rituals (the generic full set). Honours MTG_UNPRUNED.
    std::vector<std::string> TutorCandidates(const GameState&, int, const CardParams&) const override;
    // Combo-aware scry/dig: a no-Hinata hand is a dead hand (the payoffs are uncastable at full
    // price), so while no Hinata is in play or hand the dig HUNTS her -- keep Hinata, keep only
    // the lands/ramp/cantrips that cast or continue finding her, and bottom the dead payoffs.
    bool ScryKeepOnTop(const GameState&, const Card&) const override;
    // Ponder reorder keep-vs-shuffle (KeepReorderTop): Hinata is in a class of her own -- while she is not
    // in play or hand the combo (and even an affordable Soulfire) is out of reach, so a top set is
    // only worth keeping if it advances toward her: it contains Hinata, OR a dig/tutor toward her
    // plus at least one other useful card. Otherwise shuffle and dig fresh. Once she is online, keep
    // iff any card is wanted (the generic rank-threshold behaviour).
    bool KeepReorderTop(const GameState&, const std::vector<Card>&) const override;
    // Situational "what do I need THIS turn" ranking (SituationalCardRank): drives EI / Ponder / Preordain
    // card selection deterministically. ScryKeepOnTop above is re-expressed as a threshold on this
    // rank, so the keep/bottom gate and the ordering share one source of truth.
    int  SituationalCardRank(const GameState&, const Card&) const override;
    // CastOrderRank: NOT overridden. "Cast a mana RITUAL (Reality Spasm / Irencrag Feat) before the
    // payoff so its float funds the same-turn Crackle" -- Hinata (creature, 10) -> ritual (15) ->
    // Crackle (20) -- is now the GENERIC card-parameter tiering, shared with every ritual deck.
    // Archetype gates relocated out of the solver (audit B1/B2): the untap-ritual cast variant and
    // Soulfire's own-target branch only earn their keep with Hinata's discount online.
    bool ShouldEmitUntapRitual(const GameState&) const override;
    bool BranchSoulfireOwnTargets(const GameState&) const override;
    // Hold mana dorks back from combat: a 0-power, no-trigger creature (Ornithopter of Paradise)
    // deals nothing when it swings and only taps itself -- forfeiting the mana that the (now
    // second-main) Crackle wants. In a goldfish there are no blockers and Hinata has no Exalted, so
    // such a dork has ZERO reason to attack. Keep it untapped to fund the post-combat payoff.
    bool ShouldAttackWith(const GameState&, const Permanent&) const override;
    // Hold a LONE Crackle with Power as a combo piece (XCandidates): casting a single
    // non-lethal Crackle for chip damage throws away the Reality-Spasm -> big-Crackle lethal the
    // shallow search cannot see past its horizon (the combo pays off several turns later). Default:
    // HOLD the lone Crackle unless casting it wins THIS turn (5X + optimistic attack >= opp life) or
    // a second copy is already in hand. Off-switch MTG_NO_HINATA_HOLD_CRACKLE. Deterministic on the
    // GameState, so lockstep across the search enumeration, the rollout, and the executor.
    std::vector<int> XCandidates(const GameState&, const CardDefinition&, int) const override;
    // Magma Opus scaled-cast (face-damage) variants (ScaledCastVariants): the deck-specific cost model. More
    // face damage = fewer distinct spread/tap targets = less Hinata discount = more mana. ADOPTED
    // default-ON (2026-07-21); MTG_LEGACY_MAGMA -> {} -> byte-identical over-count path. See the .cpp.
    std::vector<ScaledCastVariant>
    ScaledCastVariants(const GameState&, const CardDefinition&) const override;
    // TutorSearchWidth: deliberately NOT overridden -- this deck keeps the base 6. Worth recording
    // because the train seeds said otherwise and the holdout overruled them. Gamble is unrestricted
    // ("search your library for a card") but TutorCandidates above already narrows it hard (Hinata
    // alone while she is missing; SituationalCardRank order once she is online), so the hypothesis
    // was that extra targets are ones the provider already judged worse and only dilute a deck whose
    // turns are the most budget-starved in the suite. Train agreed -- w2 -0.0568 vs w6 -0.0284.
    // The direct held-out check did NOT: w2 vs w6 came out +0.0009, a wash, and w2 is not cheaper
    // either (overnight makespan 68s vs 69s). A train-only delta of that size is what a selection
    // artifact looks like, so there is nothing to adopt. (Both seed sets DO agree Hinata dislikes a
    // WIDE width -- w12 is its worst arm on each -- so if the base ever moves, it must not move up.)
};

// Dragonstorm (mono-red ritual-storm combo): a {8}{R} Storm sorcery puts min(storm+1, Dragons
// left) Dragons onto the battlefield, each firing its ETB (Scourge ping / Lathliss token) and, with
// a haste-Dragon out, swinging the same turn. The one override is the tutor-to-battlefield put ORDER
// + SELECTION heuristic (Lathliss-first / Scourge-second, haste-Dragon reserved for the alpha
// strike); the engine keeps the put + reshuffle mechanism and MTG_UNPRUNED(tutor) reverts to the
// full library-order enumeration. Inherits Generic for everything else.
class DragonstormProvider : public GenericProvider
{
public:
    std::vector<std::string>
    TutorToBattlefieldPutOrder(const GameState&, int, const CardParams&, int) const override;

    // CastOrderRank: NOT overridden. "Cast mana rituals (15) / the Ruby Medallion reducer (16) / an
    // Irencrag-style cast-restrictor (18) before the rank-20 payoff, so their float is online to pay
    // it" is now the GENERIC card-parameter tiering -- it was written out identically here and in
    // HinataProvider, and any future ritual deck now inherits it from its card data.

    // Opt in to the acceleration-prefix collapse (UseAccelPrefixCollapse): the go-off hand's ritual
    // accelerants are enumerated cheapest-first prefix-only rather than powerset. HEURISTIC,
    // MTG_UNPRUNED(AccelPrefix)- openable. See docs/design/dragonstorm-search-pruning.md (Step 2).
    bool UseAccelPrefixCollapse() const override { return true; }

    // Opt in to the Desperate Ritual splice-count collapse (UseSpliceCollapse): emit only the bare +
    // max-chain splice families per copy and keep prefix selections, taming the splice-count powerset that
    // dominates the go-off rollout after the accel/Lotus prefix collapses. HEURISTIC,
    // MTG_UNPRUNED(SpliceCollapse)-openable. See docs/design/dragonstorm-search-pruning.md.
    bool UseSpliceCollapse() const override { return true; }

    // Opt in to the cast-ORDERING search (WantsCastOrderingSearch): let the search FIND the best cast order
    // for a combo turn instead of the fixed CastOrderRank bucket. The fixed order is needed for
    // search/executor lockstep + human play, but it reorders ~12 combos into worse lines AND leaves broad
    // value on the table; searching the order recovers it (regression d3 5.56->4.95, d5 5.36->4.82). See
    // docs/design/dragonstorm-cast-order-search.md.
    bool WantsCastOrderingSearch() const override { return true; }

    // Payoff-prune (PrunesAcceleratorWithoutPayoff): allow plans that cast a Dragon / Dragonstorm / Apex;
    // prune the other one-turn ritual-accelerant lines (their float has no same-turn sink here). Default ON
    // for Dragonstorm; the callsites gate it with !DecisionUnpruned(UnprunedGate::PayoffPrune), so the
    // standing MTG_UNPRUNED / MTG_UNPRUNE=payoffprune audit reverts to the full (unpruned) branch set.
    // Measured (train+held-out): ~-0.055 turns, -42% rollout calls. See
    // docs/design/dragonstorm-payoff-prune.md.
    bool PrunesAcceleratorWithoutPayoff() const override { return true; }
    // CastCheapestFirstWithinTier: NOT overridden -- cheapest-first among same-tier accelerants is now
    // the ROOT default (DecisionProvider::CastCheapestFirstWithinTier), shared with every ritual deck.

    // Float-colour collapse (Hook: ImpulseFloatColorRedOnly / RestrictSacColorsToHasteAndRed). Apex of
    // Power floats RED only; Lotus Bloom floats RED unless a HASTE Dragon (Karrthus {4}{B}{R}{G} /
    // Kolaghan {4}{B}{R}) is castable this turn (in hand or Apex-staged). Collapses the per-colour
    // Lotus/Apex cast fan-out (the top branch-stats driver, ~590k plans/node) to ~1 on most turns.
    // HEURISTIC -- validated on avg-win-turn, not byte-identical. See docs/design + the branch-stats capture.
    bool ImpulseFloatColorRedOnly()       const override { return true; }
    bool RestrictSacColorsToHasteAndRed() const override { return true; }

    // Go-off lethal model (HasExtraLethalModel / ExtraLethalDamage). The Dragonstorm storm go-off (rituals ->
    // Dragonstorm -> N Dragons -> Scourge ETB pings) IS this turn's lethal, but Dragonstorm the spell has
    // direct_damage 0 and its dragons resolve later, so the engine's generic win-check (attack + direct)
    // never sees it. Without this the greedy/rollout `wins` check misses every go-off, so each leaf casts
    // Dragonstorm late by board-dev and the search can't tell a T3 kill from a T5 durdle (flat win-turn
    // signal). This projects the storm-put Dragons' ETB burst so `wins` fires for real go-offs; execution
    // stays the arbiter (an over-projection only STEERS the pick, never fabricates a win). See
    // docs/design/dragonstorm-goff-lethal-recognition.md.
    bool HasExtraLethalModel() const override;   // default ON; off-switch MTG_NO_DRAGONSTORM_GOFF
    int  ExtraLethalDamage(const GameState&,
                           const std::vector<const CardDefinition*>&) const override;
};

// Goblins (aggro tribal: Krenko / Skirk / Siege-Gang / Warchief / Lackey / Aether Vial). The one
// override defers the creature-sac VALUE outlets (and haste-gates Skirk's sac-for-mana) out of the
// pre-combat cast-subset enumeration -- the wide-board branch explosion that dominates the deep
// rollout. Inherits Generic for everything else. Off-switch MTG_NO_GOBLIN_SAC_2ND (default ON).
class GoblinsProvider : public GenericProvider
{
public:
    bool DeferSacOutletPreCombat(const GameState&, const Permanent&, bool) const override;
    // Opt in to the board-lethal search short-circuit (win-turn-invariant; see UseLethalShortCircuit). Kept
    // Goblins-only so the other suite decks' play digests stay byte-identical -- Goblins re-accepts its GT
    // for the sac-deferral heuristic anyway, so absorbing this cut's play-digest change costs nothing extra.
    bool UseLethalShortCircuit() const override { return true; }
    // (A per-turn enumeration breadth cap was evaluated here and REJECTED: even a gentle top-6 cap skewed
    // the rollout win-turn (+0.0025 at cap6, +0.085 at cap4), which would corrupt the depth-matrix's
    // heuristic-arm reference. Goblin card value is too deep for a static SituationalCardRank. See
    // analysis-goblins.md. Goblins therefore inherit GenericProvider's EnumGroupCap (12; never fires here).)
    // Mogg War Marshal echo keep-exception: pay to keep the body (instead of the default decline) when the
    // live attacker is needed -- it is lethal THIS turn (the death token would be summoning-sick) or there
    // is no other castable spell ("no gas"), so the banked mana buys nothing. Off-switch MTG_NO_GOBLIN_ECHO.
    bool PayEchoToKeep(const GameState&, const Permanent&) const override;
    // (A Goblin Matron tutor-target exclusion list was evaluated + REJECTED. The +0.0025 rollout regression
    // was NOT a clairvoyance artifact -- per-game diff (2026-07-31) showed the two slower games were the
    // "lone Lackey" exclusion misfiring: with a bomb IN HAND (Muxus/Krenko/Warchief), fetching Lackey to
    // cheat it into play via combat is the genuinely fast line and needs no future knowledge. Lackey's value
    // depends on what's in hand to cheat -- exactly the depth a static list lacks -- so Goblins inherit
    // GenericProvider's full-candidate TutorCandidates. See analysis-goblins.md.)
};

// Process-lifetime default provider (stateless, shared across threads). Used as the
// nullptr fallback so any raw-GameState path stays valid.
const DecisionProvider& DefaultProvider();

// Pick the provider for a deck by archetype detection. Stage 0: always Generic.
const DecisionProvider& SelectDecisionProvider(const Decklist& deck);

// Resolve the provider for a state: its attached provider, or the default fallback for
// any path that built a raw GameState. Cheap (a pointer test on the common path); the
// DefaultProvider() call only happens when m_provider is null.
inline const DecisionProvider& ResolveProvider(const GameState& s)
{
    return s.m_provider ? *s.m_provider : DefaultProvider();
}
