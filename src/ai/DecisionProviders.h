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
    // Remedy-aware ordering of Grove-of-the-Burnwillows-style drip lands (tap_opponent_lifegain > 0):
    // its coloured tap gifts the opponent 1 life -- a cost without a Remedy (tap it LAST among equals so
    // a painless source is spent first), a benefit with one (1 damage -> tap it FIRST). Base rank from
    // GenericProvider; the nudge is archetype logic, kept out of the root ManaSourceRank.
    int  ManaSourceRank(const GameState&, const CardDefinition&) const override;
    // Exalted-aware attack declaration: a 0-power, no-attack-trigger dork must NOT swing alongside a
    // real attacker (it deals nothing and breaks the lone-attacker Exalted bonus from Ignoble
    // Hierarch); it attacks only as the sole eligible creature (to switch Exalted on / carry an
    // Invigorate pump). Required for mana-source reservation to be lossless here (a HELD dork that
    // then pointlessly attacked would forfeit Exalted).
    bool ShouldAttackWith(const GameState&, const Permanent&) const override;
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
    bool        HasExtraLethalModel() const override;
    int         ExtraLethalDamage(const GameState&, const std::vector<const CardDefinition*>&) const override;
    bool        ArchetypeCardValue(const GameState&, const CardDefinition&, int, int&) const override;
};

// Aether Vial decks (Slivers, Knights): the hand-aware vial charge policy.
class VialProvider : public GenericProvider
{
public:
    bool WantVialCharge(const GameState&, const Permanent&) const override;
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
    // Ponder reorder keep-vs-shuffle (Hook 9b): Hinata is in a class of her own -- while she is not
    // in play or hand the combo (and even an affordable Soulfire) is out of reach, so a top set is
    // only worth keeping if it advances toward her: it contains Hinata, OR a dig/tutor toward her
    // plus at least one other useful card. Otherwise shuffle and dig fresh. Once she is online, keep
    // iff any card is wanted (the generic rank-threshold behaviour).
    bool KeepReorderTop(const GameState&, const std::vector<Card>&) const override;
    // Situational "what do I need THIS turn" ranking (Hook 19): drives EI / Ponder / Preordain
    // card selection deterministically. ScryKeepOnTop above is re-expressed as a threshold on this
    // rank, so the keep/bottom gate and the ordering share one source of truth.
    int  SituationalCardRank(const GameState&, const Card&) const override;
    // Cast a mana RITUAL (Reality Spasm / Irencrag Feat) BEFORE the payoff so its floating mana
    // funds the same-turn Crackle: Hinata (creature, 10) -> ritual (15) -> Crackle (noncreature, 20).
    int  CastOrderRank(const GameState&, const CardDefinition&) const override;
    // Archetype gates relocated out of the solver (audit B1/B2): the untap-ritual cast variant and
    // Soulfire's own-target branch only earn their keep with Hinata's discount online.
    bool ShouldEmitUntapRitual(const GameState&) const override;
    bool BranchSoulfireOwnTargets(const GameState&) const override;
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
