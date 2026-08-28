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
    TapReserve,   // the mana-CREATURE hold opened as a searched branch instead of a default: the
                  // enumeration emits a second variant of each plan that spends its dorks rather
                  // than sparing them, so the search scores hold-vs-tap per line instead of taking
                  // the static "spare the body" rule on faith (USER doctrine 2026-08-17: "general
                  // rules themselves lose to situational awareness"; the oracle-first audit
                  // docs/design/mana-source-reservation.md has prescribed since 2026-07)
    MainPhase,    // main-phase classification filter disabled: enumerate every cast in the
                  // pre-combat main instead of deferring Main2-classified casts to the post-combat
                  // main (USER design 2026-08-14, docs/design/main-phase-classification.md;
                  // human play always keeps the full pre-combat set)
    TeraK,        // Terastodon ETB destroy-K narrowing opened: enumerate the full K = 0..cap fan
                  // instead of the USER's lethality-window K-set (2026-08-20: K=0 only when a real
                  // threat can drop, the K that kills next turn, the K that kills the turn after,
                  // K=max when a team-pump haste finisher (Craterhoof) is also in hand; human play
                  // always keeps the full fan)
    Replicate,    // REPLICATE COUNT opened as a searched plan dimension: enumerate one cast variant
                  // per k (extra token copies, priced into the cast's cost) instead of leaving the
                  // count to the greedy-max sink at resolution. Pruned by default because the sink
                  // is already budget-correct (SinkCostWithLineHold) and the fan multiplies every
                  // Sliver in hand; human play always keeps the full fan, so the person -- not a
                  // greedy at resolution time -- decides how many copies the turn's mana buys
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
    bool        ShouldEmitRiskyAltPayload(const GameState&, int, const CardDefinition&, bool) const override;
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
    bool ArchetypeCardValue(const GameState&, const CardDefinition&, int, int&) const override;
    std::vector<std::string> TutorCandidates(const GameState&, int, const CardParams&) const override;
    std::vector<std::string> FetchCandidates(const GameState&, int, const CardParams&) const override;
    bool CanAutoFireAltPayload(const GameState&, int, const CardDefinition&) const override;
    // CastEnablerFirst: no override -- enabler-first is the generic param-derived ENABLES tier
    // now (lifegain_to_loss -> rank 0, card-dependency-map).
    // CastOrderRank / MainPhaseOverride: the USER's 2026-08-18 review of this deck's order --
    // Swords after Invigorate, Skyshroud Cutter down with the m2 group, Reverent Silence Main2
    // and last. Both gated on MTG_AL_ORDER (default off); see the definitions.
    int  CastOrderRank(const GameState&, const CardDefinition&) const override;
    std::optional<MainPhase> MainPhaseOverride(const GameState&, const CardDefinition&) const override;
    // The REAL m1/m2 split (USER ruling 2026-08-21: "There should be a real split between the
    // two, not a re-evaluation") -- opt this deck into the pre-combat Main2 filter, and scope the
    // searched interior second main to the split being live, exactly the FiveColour package.
    // Both gated default-OFF pending measurement + the user's classification review.
    bool ClassifiesMainPhases() const override;
    bool SearchedSecondMainInSearch() const override;
    // ...and the ROLLOUT site separately -- the leaf estimator's playout policy is a DIFFERENT
    // lever from the branch-site decision. See the .cpp note.
    bool SearchesRolloutSecondMain() const override;
    bool PhaseFilterRootTurnOnly() const override;
    // The FiveColour condemnation doctrine, AL arm (USER 2026-08-21: one condemnation across a
    // turn's phases and breakpoints; AL first). MTG_AL_CONDEMN / MTG_AL_BP_CONDEMN, both
    // default OFF pending measurement, both scoped to the split (see the .cpp notes -- the
    // breakpoint half is PREDICTED unsafe: Idyllic Tutor is a value-changing acquisition).
    bool CondemnsPassedMainPhase() const override;
    bool CondemnsConsideredAtBreakpoint() const override;
    bool ShouldEmitRiskyAltPayload(const GameState&, int, const CardDefinition&, bool) const override;
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
    // The SHED ORDER is the untruncated ranking: the top-1 narrowing above bounds the executor's
    // searched fan, and this deck's cleanups shed many cards, not one.
    std::vector<int> CleanupDiscardShedOrder(
        const GameState&, const std::vector<std::string>*) const override;
    // NOT prefix-stable: the ranking bands SPARE copies first, so shedding the duplicate Land's Edge
    // makes the survivor the only outlet and moves it to the back. Verified against the per-shed
    // loop (MTG_DISCARD_SHED_VERIFY) -- batching this rule sheds both outlets.
    bool CleanupDiscardShedStable() const override { return false; }
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
    // No overrides left: the hand-aware charge policy this class existed for is now the ROOT
    // default (GenericProvider::WantVialCharge, adopted 2026-08-18) -- an archetype opt-in was the
    // very thing that let Goblins/Minotaur silently lose their Vial. The class stays so routing and
    // the batch [play] line still name the archetype.
    const char* Name() const override { return "Vial"; }
    // MTG_KNIGHTS_ORDER (USER-reviewed Knights cast order, 2026-08-19): the cast-trigger
    // watcher (Worthy Knight) before the tribe, the gated ETB digger (Acclaimed Contender)
    // early when its board condition already holds and late otherwise. PARAM-derived tests
    // only -- no sliver carries cast_trigger_* or etb_dig_* params, so the shared provider
    // stays byte-identical for slivers_vial (verified against cards.json 2026-08-19).
    int         CastOrderRank(const GameState&, const CardDefinition&) const override;
    const char* CastOrderTierName(int rank) const override;
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
    // NOTE (2026-08-23): this deck used to OPT OUT of the horizon-honest no-win tie-break
    // (GradesNoWinLeaf) on the strength of s5005 gi227 -- "truncates a 13-spell turn-6 chain, turn-8
    // win -> LOSS, survives 20x budget". Re-tested against both adoption gates, that game supports
    // neither claim: at this deck's PLAY setting (d5/20) both arms win T7, so it does not regress at
    // the configuration we ship, and at d3 it recovers fully at 100x budget (LOSS, LOSS, T6, T6, T6
    // as budget goes 10 -> 100 -> 1,000 -> 100,000) -- recoverable, not a valuation error. "20x" was
    // simply not unlimited. Measured overall at play settings the tie-break is ~inert here and mildly
    // positive: -10 turns over 100,000 paired games, 0.029% binding. The opt-out was removed.
    // See docs/design/horizon-honest-leaf.md.

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

    // EOT dominance: the two axes this deck inverts relative to the generic table (USER,
    // 2026-08-14 -- the archetype declares what is deck-dependent; see DecisionProvider.h).
    //
    // The opponent's board is this deck's RESOURCE, not a threat: the drain watchers (Suture
    // Priest / the Wardens) bill per enemy body entering and dying, and Defense of the Heart's
    // intervening-if wants the opponent at three or more creatures. Vs the passive goldfish
    // opponent (never attacks, never blocks) an extra enemy creature has no downside at all, so
    // MORE dominates.
    DomDir DominanceOpponentBoard() const override { return DomDir::MoreDominates; }

    // Varchild's War-Riders' cumulative-upkeep age counters are generically a COST -- one more
    // upkeep payment per counter -- which is why the default table fails them closed. Here the
    // payment IS the payoff: PerformUpkeepCumulativeGifts gifts `age_counters` Survivors to the
    // opponent every upkeep and nothing is ever sacrificed for it, so a higher age is strictly
    // more fuel every turn from now on.
    DomDir DominanceAxisDirection(DomAxis a) const override
    {
        if (a == DomAxis::AgeCounters) { return DomDir::MoreDominates; }
        return GenericProvider::DominanceAxisDirection(a);
    }

    // The USER-reviewed cast order (2026-08-19, recorded verbatim in cast-order-rankings.md),
    // gated on MTG_CG_ORDER (default off pending measurement): Crop Rotation right after the
    // land (mana-neutral; land first for more sacrifice options), Sylvan Scrying early (its
    // before-land ideal is the shared land-two-position open item), watchers before the givers
    // (Suture Priest bills each gifted body entering), Hunted Phantasm after them, Enlightened
    // Tutor after every same-turn shuffle (its to-top placement dies to one), Massacre Wurm
    // LAST ("ensure that enemy creatures are created first" -- each fresh token dies for 2).
    int CastOrderRank(const GameState&, const CardDefinition&) const override;
    const char* CastOrderTierName(int rank) const override;
    // "Sylvan Scrying is the one card that can go before the Land drop" (USER, 2026-08-19):
    // defer the d0 drop when a hand-land tutor is payable from the mana already on board, so
    // the fetched Orchard can be the drop. Gated with MTG_CG_ORDER.
    bool LandDropAfterHandLandTutor(const GameState&, int) const override;
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
    // Fetch search breadth: the base-class DEFAULT (top entry only) is exactly this deck's
    // reviewed policy -- no override needed. Evidence that top-1 is right here: the wide fan's
    // coupled-metric edge (+0.034 held-out 8/8 keys) was proven 100% CLAIRVOYANCE by the
    // MTG_SHUFFLE_SALT_SEARCH decouple ensemble (salts 1-4, 2,400 games: uncap 5.0558 WORST in
    // every salt vs top-1 5.0317) -- the extra targets only "won" by pre-seeing the deterministic
    // post-fetch shuffle. See docs/design/fivecolour-gen-leaf-cost-wallclock.md RESOLUTION.
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
    // MTG_5C_ORDER (USER-reviewed Five Colour cast order, 2026-08-19 prototype): Cannons before
    // every multicolored cast, Cornucopia/Greaves/Unite next, mana creatures EARLY iff Greaves
    // is live (battlefield or hand -- one copy in deck, so hand ~= plan) with the scaling dorks
    // (Bloom/Faeburrow) additionally gated on all five colors already on the field, LATE
    // otherwise (the late slot self-corrects: the 5-color bodies resolve before it). Bodies:
    // CHEAPEST FIRST (rank 10 + mv -- USER: cheap colorful bodies widen the domain the scaling
    // dorks read); the middle order is otherwise not load-bearing and adds no search cost.
    int         CastOrderRank(const GameState&, const CardDefinition&) const override;
    const char* CastOrderTierName(int rank) const override;
    // The scaling dork's two USER-ruled positions in preference order (ideal
    // completer-adjacent slot, then the front) -- the funding ladder walks between them on
    // payability, exactly the Gold Rush machinery. Non-empty only in the Greaves-live
    // below-5-colors situation.
    std::vector<int> CastOrderFallbackRanks(const GameState&, const CardDefinition&) const override;
    // MTG_5C_PHASE: per-deck opt-in to the pre-combat Main2 filter (this deck actually plays a
    // second main), activating the override above with the USER's 2026-08-19 phase rules.
    bool        ClassifiesMainPhases() const override;
    // MTG_5C_SSM: search this deck's INTERIOR second mains (the phase spec above made them carry
    // real decisions -- the global-arm split is recorded at the base hook). Measurement lever,
    // default OFF pending the per-deck A/B.
    bool        SearchedSecondMainInSearch() const override;
    // MTG_5C_CONDEMN: the order-condemnation post-combat filter (base hook note) -- main 2
    // continues with main 1's condemnation list instead of re-litigating the whole hand.
    // Measurement lever, default OFF pending the per-deck A/B.
    bool        CondemnsPassedMainPhase() const override;
    // Nicol Bolas +3 is "destroy target NONCREATURE permanent" -- it REQUIRES a target, and with a
    // land-less opponent the only legal ones are ours. Destroying our own land is a real cost, so
    // the search correctly declined the ability; but -9 is unreachable from loyalty 5 without two
    // +3s, which left an EIGHT-mana walker ({4}{U}{B}{B}{R}) inert BY CONSTRUCTION. Opting in gives the passive opponent
    // lands (inert props -- destroying one changes nothing else), so the faithful play is available
    // and +3 becomes the free ramp it is meant to be. Same reason HinataProvider opts in.
    bool OpponentPlaysLands() const override { return true; }
    // MTG_5C_BUCKET_DISCARD: USER-reviewed bucket cleanup-shed policy (2026-08-21) -- quota-first
    // keep set (colour coverage / land drops / acceleration / threat floor / Cannons / Greaves),
    // single-index return. See the .cpp comment for the full design + evidence.
    std::vector<int> CleanupDiscardCandidates(
        const GameState& s, const std::vector<std::string>* required_pieces) const override;
    // The bucket rule's whole shed ORDER; the hook above is that order narrowed to one candidate
    // for the searched fan.
    std::vector<int> CleanupDiscardShedOrder(
        const GameState& s, const std::vector<std::string>* required_pieces) const override;
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
    // COPY-MAGNET reserve override (USER 2026-08-25, mana-order-and-reserve-overhaul.md): when the
    // plan casts a pump/trick and a magnet is live, EVERY untapped creature is a copy target and
    // an attacker, so the base "hold only the pump target" narrowing is exactly wrong here -- keep
    // holding the whole board of creatures and pay off everything else. With no pump in the plan
    // (or no magnet on board) the base rule applies unchanged.
    std::uint64_t ReserveCreatureHold(const GameState& s, const PlanTraits& t,
                                      std::uint64_t crea_mask) const override
    { return (t.plan_has_own_pump && t.copy_magnet_live) ? crea_mask
                                                         : GenericProvider::ReserveCreatureHold(s, t, crea_mask); }
    // Treasures-before-creatures on the go-off turn (lump doc §9, USER: "Especially in Mirrorwing
    // Treasures should be used before creatures"): when bodies are multipliers, a tapped creature
    // forfeits its trick copy AND its swing, while a cracked Treasure costs one future mana -- so
    // spend the one-shots freely and let the reserve keep the bodies instead. On a durdle turn the
    // base §2b hold applies (keep the Treasure, tap the dorks -- they untap anyway; the gi81 rule,
    // mana-creature-tap-order.md §6b).
    bool SpendOneShotsFreely(const GameState& /*s*/, const PlanTraits& t) const override
    { return t.bodies_are_multipliers; }
    // Exalted-aware attack declaration (shared with AntiLifegainProvider): hold back a 0-power,
    // no-trigger mana dork rather than swinging it next to the real attacker -- it deals nothing and
    // cancels Ignoble Hierarch's lone-attacker bonus. Became load-bearing when the whole-turn dork
    // reservation started leaving those dorks untapped. MTG_NO_EXALTED_ATTACK reverts.
    bool ShouldAttackWith(const GameState& s, const Permanent& attacker) const override;
    // Enumeration breadth: a post-fan-out hand holds 10-12 castable groups x ~5 trick-target
    // options each -- the full product is computationally infeasible (a single node measured a
    // 4 GiB digit store / billions of positions; see analysis-mirrorwing-dragon.md). The generic
    // cap of 12 groups never binds there; 8 keeps the worst node ~=400k positions. Same gated
    // breadth-policy shape as the base hook (MTG_UNPRUNED / MTG_NO_GROUP_CAP opens it).
    int EnumGroupCap() const override { return 8; }
    // Board-lethal short-circuit (USER 2026-08-16: "any line that produces that many treasures is
    // guaranteed to win on attack"; "you literally only need to attack with the magnet"). When the
    // CURRENT board's attack-all damage already kills, attacking wins now, so the cast-subset
    // odometer is skippable -- win-turn-invariant, so the suite's avg fingerprint cannot move, only
    // which equally-winning plan is recorded.
    //
    // This deck is the shape the cut exists for. Zada/Mirrorwing copy a trick once per creature, so
    // a Gold Rush chain leaves every creature at +2/+2 PER TREASURE: the measured go-off boards run
    // the opponent to -566, -303, -293 life -- 25x lethal -- and the enumerator is meanwhile facing
    // 63 fungible Treasures (a 3^63 group product) deciding which to crack for mana it cannot spend.
    // The pathology was never a game failing to convert (Treasures and the kill land on the SAME
    // turn); it is pure enumeration cost on a turn already won, which is exactly what this cut ends.
    bool UseLethalShortCircuit() const override { return true; }
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
    // Consumed by the opaque apply path's enabler sort in both worlds. Under MTG_MW_ORDERED the
    // body camp gains Luxurious Libation (its token is one more copy target for everything after)
    // and LOSES Gold Rush, whose position becomes the funding ladder below.
    bool CastEnablerFirst(const GameState&, const std::string&) const override;
    // The FULL reviewed order (USER, 2026-08-18): magnets(5) -> creatures(10) -> Libation(11) ->
    // Twinflame(12) -> draws(14) -> Gold Rush(15, ladder) -> Fists(16) -> Draught(18). Gated on
    // MTG_MW_ORDERED; off -> the pre-review ranks (magnet 5 / doubler 12 / treasures 15).
    int  CastOrderRank(const GameState&, const CardDefinition&) const override;
    // Cantrip promotion: measured a consistent win on THIS deck and a loss on Hinata, so it lives
    // here rather than at the root. Default off (MTG_MW_CANTRIP_ORDER) -- see the definition.
    // SUPERSEDED by the USER's reviewed order when MTG_MW_ORDERED is on: bodies (magnets,
    // creatures, Libation, Twinflame) come BEFORE the draws -- each body is one more copy of the
    // mass-draw itself -- so the draws-first promotion this hook awarded is the wrong shape.
    bool PromoteCantripsInCastOrder() const override;
    // MTG_MW_ORDERED: the whole reviewed order replaces search ownership of the trick order
    // (USER: "We need to order everything, not have search own the order in order to avoid high
    // expenses"). Default off -> byte-identical.
    bool OrderOpaqueCastsByRank() const override;
    // Gold Rush is the one order-searched card left, as a deterministic FALLBACK not a fan-out
    // (USER: "might need more searching unless we have a lot of mana up"): prefer after the draws
    // (15), walk to after-Twinflame (13), then after-magnets (6), only while the line cannot pay.
    std::vector<int> CastOrderFallbackRanks(const GameState&, const CardDefinition&) const override;
    // Review-artifact labels for the reviewed tiers (the generic table's names for 14/15/16/18
    // mean unrelated things).
    const char* CastOrderTierName(int rank) const override;
    // Luxurious Libation's {X} (user, 2026-08-17): search exactly X=0 and the X that MAXIMISES
    // ATTACKING BOARD POWER, spending mana dorks only where spending them actually raises that
    // total. The generic prune's single {max_affordable} is "tap every land AND every dork",
    // which wastes each tapped dork's attack and its own +X/+X -- measured, it lost 172 of 172
    // casts to X=0, so the card's late "close the game" role never fired. Other {X} cards keep
    // the generic prune; MTG_UNPRUNED opens the full range.
    std::vector<int> XCandidates(const GameState&, const CardDefinition&, int) const override;
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
    // Breakpoint condemnation: don't re-offer, in the continuation, a cast this section already
    // considered and passed on. Safe HERE specifically -- this deck has exactly ONE breakpoint class
    // (the Puresteel equipment-ETB draw, site 6) and had zero before it existed, so the filter can
    // only ever touch a site-6 continuation; the global lever is measured harmful on decks whose
    // breakpoints are cantrip/staging chains (see the base hook). Worth 44-61% of site 6's search
    // cost for byte-identical play: held-out +32.9% -> +18.3% and train +217% -> +84% over baseline,
    // same -0.1533/-0.1200 delta and the same 24/18 games faster, 1/0 slower (1800-game sweep,
    // logs/kitty_shape). Inert until MTG_EQUIP_DRAW_BP arms the class at all.
    bool CondemnsConsideredAtBreakpoint() const override { return true; }
    // Tutor breadth: score EVERY distinct Equipment, not the generic 6.
    //
    // This is a COVERAGE fix, not a heuristic. GenericProvider::TutorCandidates deliberately
    // encodes no ranking ("no deck-agnostic tutor heuristic worth encoding") and returns targets in
    // LIBRARY ORDER, and the deck has EIGHT distinct Equipment names against a default width of 6 --
    // so two legal Stoneforge targets go unscored every time, and WHICH two is decided by shuffle
    // order rather than by merit. Widening to 8 makes the decision fully searched and deletes the
    // shuffle dependence; there is nothing left to rank.
    //
    // Affordable precisely because the decision is RARE: measured 26 tutor triggers across 96 logged
    // games (0.27/game), and the tutor axis is ADDITIVE (one rollout per extra target, see the base
    // hook) rather than multiplicative, so the whole cost is ~2 extra rollouts on a quarter of games.
    int TutorSearchWidth() const override;
    // The reasoned fetch ranking the NARROW widths rest on (USER 2026-08-21: "narrow the list to a
    // small number and potentially only choose one most to all of the time in a heuristic").
    std::vector<std::string> TutorCandidates(const GameState& s, int controller,
                                             const CardParams& pp) const override;
    // Board-lethal search short-circuit (the Goblins-proven wide-board cut): when attack-all
    // damage already kills this turn, skip the cast-subset odometer and just attack. Win-turn-
    // invariant. This deck's late boards are exactly the pathological shape -- Kemba cats +
    // creatures + 5-8 equipment whose metalcraft {0} equips the mana bound cannot prune, so a
    // rollout-leaf Solve walks ~1M subsets and a d5 game hit 40+ min on one seed (300003 gi=2)
    // once the rollout learned Puresteel draws (enter-cascade fix). Off-switch MTG_NO_LETHAL_CUT.
    bool UseLethalShortCircuit() const override { return true; }
    // NO GREEDY SECOND MAIN on this deck (USER 2026-08-19: "we drop the greedy solves entirely and
    // follow the proper design"). The evidence that it is free here: four d3 arms x 100 games --
    // greedy, MTG_SEARCH_SECOND_MAIN, MTG_PHASE_CLASSIFY, and both -- all return avg 5.0300 and
    // play digest 3e6ea44e9c15d572, so the searched path reaches the same decisions. Kill switch
    // MTG_NO_SEARCH_SECOND_MAIN=1.
    bool SearchedSecondMainInSearch() const override { return true; }
    // The USER-reviewed cast order (review held 2026-08-19; see cast-order-rankings.md for the
    // ruling verbatim). Gated on MTG_KE_ORDER, default OFF -> byte-identical.
    int  CastOrderRank(const GameState&, const CardDefinition&) const override;
    // "Order everything, not have search own the order" -- the same opaque-set adoption Mirrorwing
    // took, on the same lever.
    bool OrderOpaqueCastsByRank() const override;
    // Swords / Unexpectedly Absent are "essentially unused in goldfish" (USER): last, and in the
    // later phase.
    std::optional<MainPhase> MainPhaseOverride(const GameState&, const CardDefinition&) const override;
    // Review-artifact labels: this deck's reviewed tiers land on generic rank numbers whose
    // generic names mean unrelated things.
    const char* CastOrderTierName(int rank) const override;
    // BUCKETED cleanup discard, to the USER's design (2026-08-22, verbatim): "bucket creatures
    // (max 2, preferably Puresteel Paladin and Kor Duelist, or alternatively another enabler
    // (Armored Skyhunter, Kemba, Stoneforge) and a doublestriker or Kemba if none are available),
    // mana sources (up to 3-4 mana, no more than 3 sources and always keep sol ring) and equipment
    // (at least 2 if not 3, preferring high-impact like Colossus Hammer if we have a way to cheat
    // equip it and otherwise look for cheap equipment like bonesplitter and o-naginata)". Same
    // shape as AntiLifegainProvider's bucketed rule. Hatch MTG_KE_BUCKET_DISCARD=0.
    std::vector<int> CleanupDiscardCandidates(
        const GameState& s, const std::vector<std::string>* required_pieces) const override;
    // Enumeration breadth (Mirrorwing's lever on this deck's shape) -- MTG_KE_GROUP_CAP, default
    // OFF -> the generic 12. See the definition for the measured cost/quality dial.
    int EnumGroupCap() const override;
};

// StompySurprise (mono-green elf ramp). Detection keys on the deck's gated params (see the
// stompy flag in DetectDecisionProvider); every hook except the one below stays Generic, so
// routing this deck here instead of g_generic changes nothing but the cleanup-discard ranking.
class StompyProvider : public GenericProvider
{
public:
    const char* Name() const override { return "Stompy"; }
    // Board-lethal search short-circuit (win-turn-invariant; see EquipmentProvider's note). This
    // deck's late boards are the pathological wide shape it exists for -- elf swarm + Hornet
    // tokens + a hoof pump that makes everything lethal at once. Also NOT a behaviour change:
    // until the 2026-08-21 routing fix this deck silently ran under GoblinsProvider, which has
    // the short-circuit on -- dropping to Generic would have silently removed it.
    bool UseLethalShortCircuit() const override { return true; }
    // Tutor target ordering (Worldly Tutor / Natural Order). Generic returns candidates in
    // LIBRARY (shuffle) order, and the width-6 axis window then scores a RANDOM six of this
    // deck's eleven distinct creature names -- the closers themselves fall out of the window.
    // That cost a measured +0.03 avg win turn at d3 when the misroute fix moved the deck off
    // GoblinsProvider's power-ranked list (5.0550 -> 5.0850, both seeds; d5 unmoved). See the
    // definition for the authored order. Honors MTG_UNPRUNE=tutor as the escape hatch.
    std::vector<std::string> TutorCandidates(const GameState&, int, const CardParams&) const override;
    // No TutorSearchWidth override: the CANDIDATE LIST is the branching control (user 2026-08-21,
    // "I hate the width idea in general") -- TutorCandidates emits the decided pick alone when
    // the lethality calculation is certain and the serious contenders when it is not, so a fixed
    // width would be either inert or a second, arbitrary cap. MTG_TUTOR_WIDTH still overrides
    // the axis for A/Bs; MTG_UNPRUNE=tutor restores the full untruncated list.
    // Cleanup discard: USER-AUTHORED bucket policy (2026-08-21) -- <mana> <threats> <enablers>,
    // shed in that order. Hand THREATS are spares in this deck ("many threats are not played
    // from hand... you can fetch to top of deck with Worldly Tutor and drop it off the top"),
    // so the library-deploy ENABLERS are the precious hand cards -- the exact inverse of the
    // generic highest-MV rule, which pitches the fatties' neighbours and keeps flood. See the
    // definition for the full bucket rules. MTG_STOMPY_BUCKET_DISCARD=0 -> generic base (A/B).
    std::vector<int> CleanupDiscardCandidates(
        const GameState&, const std::vector<std::string>*) const override;
    // Cast order: USER-PROPOSED tiers (2026-08-21) -- 1-mana elves, Sol Ring, scaling elves
    // cheapest-first, Call of the Wild, cheat-outs (Turntimber, Natural Order), a dual-position
    // Worldly Tutor (mid when a top-consumer follows, else last), Vaultborn as the drawing
    // creature, fatties, Craterhoof last for maximum pump, Mirri's Guile. Gated on
    // MTG_STOMPY_ORDER (default OFF pending measurement); see the definition + the verbatim
    // ruling and open items in docs/design/cast-order-rankings.md.
    int         CastOrderRank(const GameState&, const CardDefinition&) const override;
    const char* CastOrderTierName(int rank) const override;
    // Tutor-top combo lethality (the d0 half of the MTG_TOP_RESOLVE reset): project the tutored
    // Craterhoof arriving through a still-affordable top-consumer (Call activation / spare
    // Turntimber) into the greedy's plan-lethality check, so depth 0 holds the consumer and takes
    // the combo line the deferred continuation then realises. Gated on TopResolveEnabled().
    bool HasExtraLethalModel() const override;
    int  ExtraLethalDamage(const GameState&, const std::vector<const CardDefinition*>&) const override;
};

// Bogle Auras (hexproof voltron): signature is Light-Paws' aura_cast_tutor_attach (unique to
// this deck). Exists to give the deck the DIG hooks: it runs 4x Horizon Canopy
// ("{1}, {T}, Sacrifice: draw a card") and GenericProvider's dig gate is a hard false, so the
// autonomous search structurally could not consider the sac-draw the deck is built around --
// found by the reference bench (auras s21/gi20: the human sacs Canopy on T4 AND T5, finds
// Ethereal Armor + Light-Paws, wins T5; the search sat on both Canopies and won T6).
class AurasProvider : public GenericProvider
{
public:
    const char* Name() const override { return "Auras"; }
    bool        HasAnyDigSource (const GameState& s) const override;
    bool        ShouldConsiderDig(const GameState& s) const override;
    std::string SelectDigSource(const GameState& s, const ManaPool& pool, bool& out_is_sac) const override;
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
// Is this card a "draw" for cast-ORDER purposes (resolves into information)? Shared by the
// ideal-order rank and the --cast-order-report so the report cannot drift from play.
bool IsIdealOrderDraw(const CardDefinition& def);

// Does this draw want the INFORMATION tier (rank 2)? A draw whose cost is the turn (Magma Opus,
// Apex of Power) does not -- see the definition. Max mv for a cantrip: MTG_IDEAL_CANTRIP_MV.
bool IsIdealOrderCantrip(const CardDefinition& def);
int  IdealOrderCantripMaxMv();

// The COST-EFFICIENT end of a card's cast-order RANGE is its rank WITHOUT the ideal-order
// promotion (docs/design/cast-order-ideal-with-ranges.md, step 2). Recovering it by duplicating
// the tier table would create exactly the twin this codebase keeps paying for, so CastOrderRangeOf
// asks the SAME provider twice, with this suppression set the second time -- the two ends of the
// range can then never drift from the one rank definition. Thread-local: one provider instance
// serves every worker thread. Set ONLY by IdealOrderSuppressScope, and only around a rank query
// (never around a cast), so nothing in play reads a suppressed rank.
extern thread_local bool g_suppress_ideal_order_tier;

class IdealOrderSuppressScope
{
public:
    IdealOrderSuppressScope() : m_prev(g_suppress_ideal_order_tier)
    { g_suppress_ideal_order_tier = true; }
    ~IdealOrderSuppressScope() { g_suppress_ideal_order_tier = m_prev; }
    IdealOrderSuppressScope(const IdealOrderSuppressScope&)            = delete;
    IdealOrderSuppressScope& operator=(const IdealOrderSuppressScope&) = delete;
private:
    bool m_prev;
};

// ---- MANLAND RESERVE RELEASE (human play, replicate-capable cast) ----------------------------
//
// A colourless-only manland (Mutavault) ranks 60 -- past even rainbow -- so the greedy spends it
// only when nothing else can pay. That reserve is MEASURED-BEST for autonomous play (slivers, 1800
// games, seeds 2002/3003/4004: rank 5 and rank 30 are both +0.05 turns WORSE than 60, and identical
// to each other, so the reserve is the whole effect). Keeping the manland untapped to attack is
// simply worth more on average than the colour it occasionally strands.
//
// It is not worth more when the human has a REPLICATE coming. Its {C} can only ever pay a GENERIC
// pip, so spending it there costs no colour at all -- while spending a colour land on that same
// generic pip strands the replicate's coloured pip outright. Repro (slivers seed 30 / game-index 29,
// turn 5): Cavern + Courtyard + 2x Mutavault, Sinew Sliver {1}{W} with replicate {1}{W} exactly
// payable, and the engine offered `replicate ... max_count: 0` because both white sources went to
// the cast. Releasing the reserve for that one payment makes the count reachable.
//
// Scoped to the payment, and to human play, so autonomous ground truth cannot move: the AI keeps the
// measured-better reserve, and the human keeps the option the reserve was silently costing them.
extern thread_local bool g_release_manland_reserve;

class ManlandReserveReleaseScope
{
public:
    ManlandReserveReleaseScope() : m_prev(g_release_manland_reserve)
    { g_release_manland_reserve = true; }
    ~ManlandReserveReleaseScope() { g_release_manland_reserve = m_prev; }
    ManlandReserveReleaseScope(const ManlandReserveReleaseScope&)            = delete;
    ManlandReserveReleaseScope& operator=(const ManlandReserveReleaseScope&) = delete;
private:
    bool m_prev;
};

inline const DecisionProvider& ResolveProvider(const GameState& s)
{
    return s.m_provider ? *s.m_provider : DefaultProvider();
}

// Searched dork attack/hold contested test (MTG_DORK_ATK_SEARCH; DecisionProviders.cpp).
// True when the collapsed-main mana hold pins a dork whose released swing would deal damage --
// the FSLineWin branch then evaluates both combat variants. False whenever the flag is off.
// Searched dork attack/hold trigger. 0 = not contested; 1 = the greedy HOLDS a dork whose release
// would deal damage (search the RELEASE); 2 = the greedy ATTACKS with a non-vigilance mana dork
// whose mana the deferred main would spend (search the HOLD). See EngineFlags.h.
int DorkAtkContestedKind(const GameState& s);
