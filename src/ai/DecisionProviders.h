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
    SacLandHold,  // burn's sac-land burn hold disabled: keep the plans that cast Shard Volley before the
                  // winning turn (BurnProvider::HoldsSacLandBurnUntilLethal)
    TrickTarget,  // Mirrorwing/Zada solo-target trick target set opened to EVERY legal creature
                  // (battlefield + hand) instead of MirrorwingProvider::TrickTargetCandidates'
                  // narrowed set (magnets + best attacker + haste/copy extras)
    TreasureTrickCast, // Gold Rush cast gate disabled: enumerate the magnetless "bank a Treasure
                  // nothing wants" cast (MirrorwingProvider::TrickCastSensible; USER doctrine
                  // 2026-08-12 -- magnetless GR is 2 mana for 1 Treasure, a ramp/screw-mitigation
                  // play toward the magnet, never a this-turn mana play)
    EquipHost,    // Equip host candidate set opened: every legal (equipment, host) pair instead of
                  // the width-capped benefit ranking (KittyEquipment; also forced by HumanPlayActive
                  // so the viewer surfaces every legal host -- the pre-existing narrowing gap found
                  // during that deck's onboarding)
    JitteMode,    // Umezawa's Jitte non-combat modes (-1/-1 / gain 2) re-enumerated in autonomous
                  // search (USER doctrine 2026-08-14: "always use it for +2/+2 in goldfishing" --
                  // the modes are pruned by default; human play always keeps them)
    UACast,       // Unexpectedly Absent hand-cast re-enumerated in autonomous search (USER doctrine
                  // 2026-08-14: "just not cast for now" -- pruned by default; human play keeps it)
    MainPhase,    // main-phase classification filter disabled: enumerate every cast in the
                  // pre-combat main instead of deferring Main2-classified casts to the post-combat
                  // main (USER design 2026-08-14, docs/design/main-phase-classification.md;
                  // human play always keeps the full pre-combat set)
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
    const char* Name() const override { return "Generic"; }
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
    const char* Name() const override { return "AntiLifegain"; }
    std::vector<std::string> TutorCandidates(const GameState&, int, const CardParams&) const override;
    std::vector<std::string> FetchCandidates(const GameState&, int, const CardParams&) const override;
    bool CanAutoFireAltPayload(const GameState&, int, const CardDefinition&) const override;
    // CastEnablerFirst / CastOrderRank: no overrides -- enabler-first is the generic
    // param-derived ENABLES tier now (lifegain_to_loss -> rank 0, card-dependency-map).
    bool ShouldEmitRiskyAltPayload(const GameState&, int, const CardDefinition&) const override;
    // Tainted Remedy and Plague Drone are the SAME role (opponent lifegain -> loss) and you only
    // need one at a time, so holding one makes the other a redundant required piece rather than a
    // protected "last copy" (user 2026-08-07).
    const std::vector<std::string>* InterchangeableRequiredGroup(const std::string&) const override;
    // Cleanup discard: USER-AUTHORED bucket policy (2026-08-07) -- keep 1 enabler + enough mana,
    // maximize payoffs. State-dependent (board mana, enabler status, opponent creatures), so it
    // lives here rather than in a profile discard_order. MTG_AL_BUCKET_DISCARD=0 -> generic base.
    std::vector<int> CleanupDiscardCandidates(
        const GameState&, const std::vector<std::string>*) const override;
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
    const char* Name() const override { return "TreasureHunt"; }
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
    // The FULL keep-set ranking (every hand index, preference order) behind the hook above. The
    // hook returns only the TOP pick -- the ranking IS the decision, so the searched pass has
    // nothing to fan over (user design 2026-08-06) -- but the multi-card consumers (Land's Edge
    // pitch, Throes retrace cost) need the whole ordering and call this directly.
    std::vector<int> CleanupDiscardFullRanking(
        const GameState&, const std::vector<std::string>*) const;
    // The only deck that opts into branching the ROLLOUT's cleanup shed. MTG_TH_DISCARD_WIDTH
    // overrides for the sweep; see the definition for the measured value.
    int CleanupDiscardSearchWidth() const override;
    // Throes of Chaos' retrace cost is "discard a land" -- the SAME question the cleanup shed and
    // the Land's Edge pitch ask, so it gets the same answer rather than a third opinion. The base
    // rule was first-land-in-hand-order, which this deck has now measured twice as the wrong way
    // to choose among 13 distinct lands.
    std::vector<int> RetraceDiscardCandidates(
        const GameState&, int, const std::vector<int>&) const override;
};

// Aether Vial decks (Slivers, Knights): the hand-aware vial charge policy.
class VialProvider : public GenericProvider
{
public:
    const char* Name() const override { return "Vial"; }
    bool WantVialCharge(const GameState&, const Permanent&) const override;
};


// Mono-red Burn (Searing Blaze's landfall damage is the deck's signature): once it has enough
// lands in play (its curve tops at mana value 2), it BANKS further land drops so a future
// topdecked Searing Blaze has a land to play for its landfall (3-to-face instead of 1). Inherits
// Generic for everything else; the only override is the equal-value land-drop tiebreak.
class BurnProvider : public GenericProvider
{
public:
    const char* Name() const override { return "Burn"; }
    bool PreferHoldLandDrop(const GameState&, int) const override;
    // Shard Volley is the deck's only sacrifice-a-land spell, and every land it could sacrifice is a
    // Mountain (no utility land whose loss is free) -- so holding it until it wins the game or unlocks
    // Spectacle is a pure gain in available mana. See HoldSacLandBurn in TurnSolver.cpp.
    bool HoldsSacLandBurnUntilLethal() const override { return true; }
};

// Hinata, Dawn-Crowned (UR Crackle / cost-reduction combo). Its spells slash their cost by
// Hinata's "{1} less per target", which the deck maximises by targeting extra/own/opponent
// permanents -- so its goldfish opponent must present real targets. Layer 2 grows this provider
// with the board-aware multi-target discount and the Reality-Spasm -> Crackle mana ritual.
class HinataProvider : public GenericProvider
{
public:
    const char* Name() const override { return "Hinata"; }
    bool OpponentPlaysLands() const override { return true; }
    // Cleanup discard: the deck's USER-AUTHORED KEEP PRIORITY (2026-08-07), assigned as ranks
    // and shed in reverse. Keep hardest -> shed first: Hinata #1, Reality Spasm #1, Crackle #1,
    // Sol Ring (never shed), Spasm #2 or Irencrag, mana up to 5 with colours (2U/1R/1W,
    // counting the board), Soulfire with Hinata, cantrips while pieces are missing (2 without
    // Hinata, 1 otherwise), Soulfire without Hinata, Magma Opus, extra mana or cantrips, extra
    // Crackle, anything else, inert spells (Distorting Wake / Icy Blast / Memory Lapse /
    // Remand -- always shed if available). Cantrip and Soulfire ranks move with the board, so
    // this is a rank assignment, not a static order. Derived from searched trial tables via the
    // discard-analysis stage; the priority list itself is the user's.
    // MTG_HINATA_DISCARD_ORDER=0 -> generic base (A/B hatch);
    // MTG_HINATA_CANTRIP_FIRST=1 -> the 1-mana-cantrip-above-mana arm (keeps a 2-source floor).
    std::vector<int> CleanupDiscardCandidates(
        const GameState&, const std::vector<std::string>*) const override;
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
    // Main-phase doctrine (USER, 2026-08-14, verbatim): "everything in Hinata is second main.
    // The reason is because there is literally no attacking creature beyond Hinata herself and
    // she never gets haste or is pumped. So, by my rule everything should be cast second main
    // including all draw." NOT special-cased here (USER: "I would rather not have to specify a
    // special rule for decks like Hinata"): the collapse is DERIVED --
    // GoldFishRunner::DeckFeedsCombat finds no attack-feeding card in this deck, and with
    // GameState::deck_feeds_combat false the base classifier sends draws, card-flow riders and
    // every doubt-class custom to Main2 by itself (verified digest-identical to the explicit
    // total override it replaced). Corollary, also per the USER: "if we are considering main 1
    // for Hinata, there is a bug" -- a game measuring worse under the filter is an ENGINE
    // defect in the post-combat path (see main-phase-classification.md round 2).
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
    const char* Name() const override { return "Dragonstorm"; }
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

    // Cleanup discard: NO spare-copy rule for this deck, ever. Recorded because it was MEASURED,
    // not assumed: the 2026-08-06 adoption gate ran dragonstorm with a shed-duplicates-first band
    // and got 11/12 overnight cells worse (+0.063 net, 0 better) -- a storm deck consumes ritual
    // copies CUMULATIVELY (storm count + float), so a "spare" Rite of Flame is next turn's mana
    // and storm. The discard-analysis labels agree (band 85.8% optimal vs the base MV rule's 99%
    // over 401 decisions). See docs/design/per-deck-discard-analysis-phase.md.

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
    const char* Name() const override { return "Goblins"; }
    bool DeferSacOutletPreCombat(const GameState&, const Permanent&, bool) const override;
    // Goblin Matron tutors for "a Goblin card" out of ~16 distinct Goblin names, and unlike every
    // other tutor in the suite its targets are not close substitutes (Muxus and Mogg War Marshal are
    // not the same fetch), so the search keeps finding value well past the provider's top few.
    //
    // Measured, MONOTONE with a knee at 8 -- the opposite shape to Hinata's dilution:
    //   TRAIN (1001/2002/3003)   w2 -0.0294  w4 -0.1627  w6 -0.1846  w8 -0.2100  w12 -0.2108
    //   HELD-OUT (4004..7007)    w12 -0.2740 (the best arm measured there too)
    // 8 and 12 are statistically indistinguishable on train (0.0008 apart over 15 cases); 12 is taken
    // because both seed sets rank it no worse and the axis is ADDITIVE -- the whole 3-deck regression
    // makespan moves 38s -> 46s across the entire width range, not per-width multiplicatively.
    // FOLDED IN a value x deploy-discount RANKING (TutorCandidates below): the smooth width-sensitivity
    // documented above was largely an UNRANKED coverage gap, not pure target diversity. Measured (d5 b20
    // s2002): ranked w4 4.322 ~= unranked w12 4.316; ranked w2 4.324 beats unranked w4 4.329. So width
    // drops 12 -> 4 to search only the ranked THREATS (Muxus/Siege-Gang/lords/Krenko float to the top,
    // chaff sinks) -- near-full quality (train ~+0.006) at a narrower, cheaper axis. See TutorCandidates.
    //
    // WIDENED 4 -> 6 (2026-08-04) once the lord-amplification term landed. The two are entangled and
    // neither is adoptable alone: amplification is a clear ranking IMPROVEMENT (held-out d0, which takes
    // cands[0] and so reads ordering directly, -114.0 turn-units) that at W=4 nonetheless cost +11.0 on
    // held-out SEARCHED -- promoting a lord into a four-slot window pushes out a card the search still
    // wanted. That is a window-MEMBERSHIP effect, not an ordering error, and the measurement separates
    // them cleanly: the same arm at W=6 is -4.0 searched, while its d0 gain is identical at both widths
    // (width is irrelevant when you take the top card). Width is cheap here -- the axis is ADDITIVE, not
    // multiplicative -- so 6 buys back the displaced candidate. Held-out searched for the adopted bundle
    // (v2 ranking + amplification + W=6) is -5.0 with train agreeing on every tier.
    // ... and 6 -> 9 under MTG_TUTOR_AXIS_RESOLVE (defined in the .cpp; needs EngineFlags.h). The
    // resolution-state ranking is HONEST about mana where the legacy pre-land state was doubly
    // pessimistic -- and that pessimism was an accidental diversity mechanism: it buried the bombs,
    // which kept cheap ENABLERS (Skirk / Warchief / Lackey) inside the 6-wide window. At the honest
    // state those enablers rank 7-9 (their credit is ~10% under the mid-value bodies), and in five
    // of the six resolve-mode held-out regressions the baseline's winning fetch sat at EXACTLY
    // resolution rank 8-9 (gi768/gi727 Skirk 8, gi714/gi200 Warchief 8, gi352 Lackey 9). Same
    // window-membership-vs-ordering separation as the 4 -> 6 widening above, diagnosed the same way
    // (MTG_TUTOR_CHOSEN_RANK against the resolution list). Measured: resolve-mode goblins held-out
    // searched +13 tu at W=6 (0 better / 13 worse, every one recovering at 4x budget) -> +3 at W=9,
    // d0 untouched (width is irrelevant when you take the top card).
    int TutorSearchWidth() const override;
    std::vector<std::string> TutorCandidates(const GameState&, int, const CardParams&) const override;
    // WITHDRAWN, and left here only as the hook's one implementation: "turns 1-2, play a Mountain if
    // one is in hand, without branching over the alternatives". Reachable ONLY under
    // MTG_FORCED_EARLY_LAND=1 (default off) -- see TurnSolver's s_forced_early_land.
    //
    // The argument still looks sound (on turns 1-2 both singleton utility lands are dominated by a
    // Mountain: Cavern of Souls is colored_creature_only so it cannot pay Lightning Bolt, and Three
    // Tree City's scaled mode is unreachable before turn 3), and the hook correctly returns "" when
    // the hand holds no Mountain, so it never removes an option a Mountain does not dominate.
    //
    // What did NOT hold was the measurement. The claimed "-3.72% rollout calls at identical win
    // turn" came from seed bases 9001-9006, which OVERLAP: the per-game seed is base+gi, so 6 bases
    // x 1000 games share 999 of every 1000 games. On disjoint bases the prune is +1.87% rollout
    // calls -- WORSE, not better, because the branch units it saves on turns 1-2 move to turn 3,
    // where the cast-subset multiplier is larger. See
    // docs/design/searched-design-audit-blind-spots.md ("Method trap: overlapping seed bases").
    std::string ForcedEarlyLandName(const GameState& s, int controller) const override;
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

// Creature Giving (gift-the-opponent drain: Suture Priest / Massacre Wurm / Defense of the
// Heart / Forbidden Orchard). One override, USER-DIRECTED (2026-08-06, "0 exceptions -- these
// live in the provider"): the deck's LAND tutors (Sylvan Scrying to-hand, Crop Rotation
// to-battlefield) always fetch Forbidden Orchard while a copy remains in the library -- the
// Orchard's per-turn Spirit gift is the drain engine's fuel and the Defense of the Heart
// trigger enabler, so it strictly dominates the fixing any other land offers. A single
// candidate means the tutor emits ONE cast variant (no search axis); MTG_UNPRUNED remains
// the standing full-list A/B lever. With no Orchard left the full Generic list returns
// (search picks). Non-land tutors (Enlightened Tutor) are untouched. Inherits Generic for
// everything else, including the root SacTutorPutList burst scorer.
class CreatureGivingProvider : public GenericProvider
{
public:
    const char* Name() const override { return "CreatureGiving"; }
    std::vector<std::string> TutorCandidates(const GameState&, int, const CardParams&) const override;
    std::vector<int> CleanupDiscardCandidates(
        const GameState&, const std::vector<std::string>*) const override;
};

// FiveColour (5-colour domain/goodstuff): eleven fetchlands feeding six shocks, two triomes and
// three basics, with two dynamic domain sources (Faeburrow Elder / Bloom Tender) and Deathrite's
// any-colour tap. Its ONLY archetype-specific decision is which land a fetch pulls, and the
// generic "return every legal target" default is wrong for it in both directions: it branches the
// search over up to eight targets on a deck that is already the most expensive in the suite, and
// it applies no colour reasoning at all.
//
// Before this provider existed the deck rode AntiLifegainProvider by accident -- `fetch_land_types`
// alone sets the `anti` signature in SelectDecisionProvider, and FiveColour carries no other
// archetype marker -- so its fetches were ranked by tiebreaks tuned to a 4-colour anti-lifegain
// shell (which needs black exactly once and white twice). Third deck to hit that trap after
// Goblins and Creature Giving. See docs/design/fivecolour-search-cost.md section 6.
//
// The fetch policy is USER-DIRECTED (2026-08-07): "prioritize getting the colours to cast early
// acceleration or other spells while aiming to have all 5 colours spread out over different
// sources; once we have this, aim to be able to generate 2 of each colour." Encoded as a strict
// lexicographic key in FetchCandidates -- coverage first (weighted by what the hand actually wants
// to cast, accelerants first), then redundancy toward two sources per colour.
class FiveColourProvider : public GenericProvider
{
public:
    const char* Name() const override { return "FiveColour"; }
    std::vector<std::string> FetchCandidates(const GameState&, int, const CardParams&) const override;
    void ModalSplitCandidates(const GameState&, const CardDefinition&,
                              std::vector<int>&) const override;
    // Hold a live UTILITY mana dork (Deathrite Shaman / Bloom Tender / Birds) out of combat when
    // its tap is worth more than its chip damage -- see the .cpp note. Vigilant sources (Faeburrow
    // Elder) still always attack: attacking never costs them their tap.
    bool ShouldAttackWith(const GameState& s, const Permanent& attacker) const override;
    // Main-phase doctrine (USER design 2026-08-14): the Tier-3 customs the base template rules
    // keep pre-combat by doubt but that provably cannot feed this turn's attack -- Unite the
    // Coalition (the USER's named example: vigilant Faeburrow attacks, THEN Unite spends its
    // mana), Nicol Bolas (face-damage walker; loyalty is a separate un-filtered action and fires
    // once per turn from either main), Mana Cannons (cast-trigger damage, timing-neutral), Oko
    // (a cast-turn Oko can never produce an attacker: +2 makes a sick-irrelevant Food, +1 needs
    // a Food that does not exist yet). NOT active until ClassifiesMainPhases()/MTG_PHASE_CLASSIFY.
    std::optional<MainPhase> MainPhaseOverride(const GameState& s,
                                               const CardDefinition& def) const override;
};

// Mirrorwing/Zada spell-copy swarm: overrides ONLY the trick-target narrowing (a 5f perf prune --
// the per-target variant group was the measured top branching driver on a swarm board). Every
// other decision resolves through GenericProvider exactly as before.
class MirrorwingProvider : public GenericProvider
{
public:
    const char* Name() const override { return "Mirrorwing"; }
    void TrickTargetCandidates(const GameState&, const CardDefinition&,
                               std::vector<int>&) const override;
    // Gold Rush cast gate (USER doctrine 2026-08-12): magnetless GR is 2 mana for 1 Treasure --
    // never a this-turn mana play. Sensible only as (a) magnet fan-out, (b) a pump that could
    // matter for THIS turn's lethal, (c) ramp / mana-screw mitigation toward mana-constrained
    // gas in hand ("drop a Zada or Mirrorwing a turn or more earlier", incl. colour-fixing), or
    // (d) redundancy (>=2 GR in hand + a board: "no point holding back").
    // MTG_UNPRUNE=treasuretrickcast opens it for audit.
    bool TrickCastSensible(const GameState&, int, const CardDefinition&) const override;
    // Enumeration breadth: a post-fan-out hand holds 10-12 castable groups x ~5 trick-target
    // options each -- the full product is computationally infeasible (a single node measured a
    // 4 GiB digit store / billions of positions; see analysis-mirrorwing-dragon.md). The generic
    // cap of 12 groups never binds there; 8 keeps the worst node ~=400k positions. Same gated
    // breadth-policy shape as the base hook (MTG_UNPRUNED / MTG_NO_GROUP_CAP opens it).
    int EnumGroupCap() const override { return 8; }
    // Legend-rule keep with the user's Twinflame exception (Stage 6 directive, analysis ledger):
    // keep the hasty exile-at-end COPY over a summoning-sick original iff attacking with the copy
    // wins THIS turn and attacking without it does not. Decided by simulating combat both ways.
    int LegendKeepIndex(const GameState&, int,
                        const std::vector<int>&) const override;
    // Cleanup discard: the USER-AUTHORED keep policy (Stage 6 review, refined 2026-08-13) -- one
    // magnet enabler, >=4 weighted bodies (Instigator counts 2), mana for the kept enabler with a
    // >=2 red + >=2 green floor (untapped drop when next turn is the cast turn), then spells:
    // Gold Rush (EVERY copy -- GR outranks any other pump) / ONE Twinflame / Fists kept,
    // Anger > Expedite > Scale, different pumps over
    // copies. Board coverage nets each bucket. The FULL decision, heuristic, never searched.
    // MTG_MW_BUCKET_DISCARD=0 -> generic base ranking (A/B lever).
    std::vector<int> CleanupDiscardCandidates(
        const GameState&, const std::vector<std::string>*) const override;
    // Zada and Mirrorwing fill ONE role (the copy magnet): redundancy is counted across the pair
    // so the last-copy veto cannot protect both (the antilife enabler-group lesson).
    const std::vector<std::string>* InterchangeableRequiredGroup(const std::string&) const override;
    // Twinflame strive counts: only K=0 and the max affordable K (user, Stage-6 round 3: strive
    // is cast "for lethal on the highest power creature(s)" -- intermediate counts are
    // mana-coupling corners the lethal line never wants). Gated with the tricktarget family.
    bool StriveCountMaxOnly(const GameState&, const CardDefinition&) const override;
    // Go-off order (user round 3): magnets (5) before Twinflame (8) before the pump tricks --
    // the fan-out target must exist before the token doubler, the doubler before the pumps.
    // Consumed by the opaque apply path's enabler sort in both worlds.
    bool CastEnablerFirst(const GameState&, const std::string&) const override;
    int  CastOrderRank(const GameState&, const CardDefinition&) const override;
};

// Equipment aggro (KittyEquipment). Detection keys on the equipment-deck gated params
// (attack_dig_attach_count, equip_combat_damage_charges, tap_put_from_hand_cost, ...) which no
// other deck carries; it must WIN OVER anti in the routing order because Stoneforge Mystic's
// tutor_to_hand sets the anti-lifegain signature on its own (the exact Goblin-Matron misroute
// class -- without this the deck ran under AntiLifegainProvider, whose discard/tutor heuristics
// hunt lifegain_to_loss enablers this deck does not play).
class EquipmentProvider : public GenericProvider
{
public:
    const char* Name() const override { return "Equipment"; }
    // Haste-equip host width 2 (base default 1, the measured FiveColour trade-off): the gi=39
    // T5 kill needs "equip Greaves -> the Balan cast in this same subset", and the width-1
    // ranking's top host hides it. 100-game d3 A/B: width2 == MTG_EQUIP_ALL_HOSTS == unpruned
    // (5.02, sole diff gi=39 T6->T5), zero nonconv. MTG_EQUIP_HOST_WIDTH still overrides.
    int EquipHostWidth() const override { return 2; }
    // USER equip-consolidation doctrine (2026-08-14) -- see the base hook's comment for the full
    // policy (single consolidation host; ds-vs-Kemba searched; move rule; Greaves exemptions).
    bool ConsolidatesEquips() const override { return true; }
    // Board-lethal search short-circuit (the Goblins-proven wide-board cut): when attack-all
    // damage already kills this turn, skip the cast-subset odometer and just attack. Win-turn-
    // invariant. This deck's late boards are exactly the pathological shape -- Kemba cats +
    // creatures + 5-8 equipment whose metalcraft {0} equips the mana bound cannot prune, so a
    // rollout-leaf Solve walks ~1M subsets and a d5 game hit 40+ min on one seed (300003 gi=2)
    // once the rollout learned Puresteel draws (enter-cascade fix). Off-switch MTG_NO_LETHAL_CUT.
    bool UseLethalShortCircuit() const override { return true; }
};

// Process-lifetime default provider (stateless, shared across threads). Used as the
// nullptr fallback so any raw-GameState path stays valid.
const DecisionProvider& DefaultProvider();

// Pick the provider for a deck. Honors MTG_PROVIDER_DECK (pin to the provider detected for THAT
// decklist -- the screening driver's "an arm is a declared modification of the base deck, so its
// identity is the base's" route); unset, it is DetectDecisionProvider(deck). Every routing site
// goes through THIS one, so a pin cannot be missed by one path and honored by another.
const DecisionProvider& SelectDecisionProvider(const Decklist& deck);

// Raw archetype detection by card params, ignoring any pin. For REPORTING only (BatchRunner's
// [play] line prints detected-vs-pinned so a signature crossing is surfaced, not silently routed
// around); never use this to attach a provider to a state.
const DecisionProvider& DetectDecisionProvider(const Decklist& deck);

// Resolve the provider for a state: its attached provider, or the default fallback for
// any path that built a raw GameState. Cheap (a pointer test on the common path); the
// DefaultProvider() call only happens when m_provider is null.
inline const DecisionProvider& ResolveProvider(const GameState& s)
{
    return s.m_provider ? *s.m_provider : DefaultProvider();
}
