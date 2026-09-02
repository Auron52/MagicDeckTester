#pragma once

// Per-deck/archetype heuristic interface.
//
// GUIDING PRINCIPLE: the generic search/decision tree (TurnSolver / AIEngine) must
// contain NO deck/archetype/card-specific DECISION logic. Every such heuristic --
// which card to fetch, whether to dig, how many lands to discard to Land's Edge,
// whether a risky payload is worth casting -- is asked of the deck's DecisionProvider.
// The engine keeps only rules-mechanic EXECUTION (how an effect resolves).
//
// The decisions follow the heuristic-then-search pattern: a candidate-set hook returns
// 1 candidate (heuristic decided -> no search) or >1 (search picks among the narrowed
// set). Other hooks return a gate bool / fire count / policy bit.
//
// Providers are STATELESS and read-only over (GameState, CardDatabase). One shared
// const instance per archetype lives for the whole process (see DecisionProviders.h),
// and GameState carries a non-owning `const DecisionProvider* m_provider` that
// propagates through every search deep-copy. Call sites read state.m_provider; a null
// pointer falls back to DefaultProvider().
//
// The interface grows one hook at a time as decisions migrate out of the engine (see
// the staged plan). Some signatures are deliberately card-specific for now and may be
// generalized once the cross-archetype patterns settle.
//
// ---------------------------------------------------------------------------------------------
// LEGACY "Hook N" DECODER -- do NOT add new numbers.
//
// Hooks used to be identified by number ("Hook 26 force-keep", "Hook 30 splice collapse") in code
// comments, commit messages, and docs/design/. The numbers COLLIDED -- 30, 22 and 16 each named
// two or three unrelated hooks -- which made the historical record ambiguous and had already
// produced a wrong cross-reference (ScaledCastVariants was labelled "Hook 28", which is really
// WantsCastOrderingSearch). Numbering is therefore retired: hooks are referred to by METHOD NAME,
// which is unique and greppable. This table decodes every legacy number so old commits and design
// docs stay readable. Where a number is listed twice, BOTH readings existed -- disambiguate by the
// surrounding text.
//
//    1  TutorCandidates                 16  ShouldAttackWith  |  OpponentPlaysLands
//   1b  TutorToBattlefieldPutOrder      17  CastOrderRank
//    2  FetchCandidates                 18  XCandidates
//    3  ShouldEmitRiskyAltPayload       19  SituationalCardRank
//    4  CanAutoFireAltPayload           20  PreferHoldLandDrop  |  ShouldEmitUntapRitual
//    5  CastEnablerFirst                21  HoldDeferredDropForLethal  |  BranchSoulfireOwnTargets
//    6  HasAnyDigSource /               22  HoldDeferredDropForFurtherDig  |  NcLandDropTempoBonus
//       ShouldConsiderDig /                 |  EnumGroupCap
//       SelectDigSource                23  FetchSearchCap
//    7  LandsEdgeFireCount              24  ManaSourceRank
//    8  WantVialCharge                  25  OpponentLifegainUseful
//    9  ScryKeepOnTop                   26  KeepFloor
//   9b  KeepReorderTop                  27  UseAccelPrefixCollapse
//   10  DiscardLandsFirst               28  WantsCastOrderingSearch
//   11  ShouldStageSpectacleDraw        29  PrunesAcceleratorWithoutPayoff
//   12  ShouldCastDrawEngine            30  UseSpliceCollapse  |  CastCheapestFirstWithinTier
//   13  PostDrawKeepLandName                |  ScaledCastVariants
//   14  HasExtraLethalModel /
//       ExtraLethalDamage
//   15  ArchetypeCardValue
//
// Never assigned: no hook was ever numbered above 30. ImpulseFloatColorRedOnly /
// RestrictSacColorsToHasteAndRed were added unnumbered.
// ---------------------------------------------------------------------------------------------

#include "../core/GameState.h"
#include "../core/ManaPool.h"
#include "../cards/CardDatabase.h"
#include "KeepModel.h"    // KeepGuard (Undecided / ForceKeep / ForceMulligan) for the keep-floor hook
#include "PlanContext.h"  // PlanTraits for the reserve-override hooks (Action-free header)
#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// A "scaled cast" variant of a spell whose mana cost depends on how much OUTPUT it commits -- the
// divided-damage analogue of an {X} value. `face` damage reaches the opponent; the total `cost` is
// what committing that much face costs (all discounts already applied). An archetype's
// ScaledCastVariants() hook returns the sensible levels; the engine emits one mutually-exclusive cast
// per variant and the plan enumerator + search pick among them. See DecisionProvider::ScaledCastVariants.
struct ScaledCastVariant
{
    int      face = 0;   // damage dealt to the opponent's face at resolution
    ManaCost cost;       // finalized total cost for committing that much face
};

// --- EOT dominance axis directions (docs/design/eot-dominance-pruning.md) -------------------------
// Which way is a resource axis MONOTONE for this deck? With everything else equal, does MORE of it
// dominate, does FEWER, or is neither true? EqualRequired is the FAIL-CLOSED default: an axis with
// no declared direction stops being a comparable axis and becomes an exact-match field, so an
// undeclared (or newly added) resource can only cost the prune reach, never soundness. That rule is
// the 2026-08-14 storage-counter key-hole lesson applied preemptively: BuildSimKey omitted
// `storage_counters` and canon exposed it on dragonstorm, so here every counter type is enumerated
// and anything unenumerated fails closed.
enum class DomDir : std::uint8_t { EqualRequired, MoreDominates, FewerDominates };

// The per-permanent resource axes that carry a direction. Everything NOT listed here (name,
// controller, owner, token-ness, attachment wiring, once-per-turn flags) is an exact-match field.
enum class DomAxis : std::uint8_t
{
    ChargeCounters,      // Permanent::charge_counters   -- Aether Vial
    StorageCounters,     // Permanent::storage_counters  -- Dwarven Hold / Mercadian Bazaar
    VerseCounters,       // Permanent::verse_counters    -- Aria of Flame
    AgeCounters,         // Permanent::age_counters      -- cumulative upkeep (Varchild's)
    Loyalty,             // Permanent::loyalty           -- planeswalkers
    PlusOnePlusOne,      // Counter::Type::PlusOnePlusOne
    MinusOneMinusOne,    // Counter::Type::MinusOneMinusOne
    LoyaltyCounter,      // Counter::Type::Loyalty       -- viewer mirror of Permanent::loyalty
    PoisonCounter,       // Counter::Type::Poison
    DepletionCounter,    // Counter::Type::Depletion     -- Sandstone Needle / Saprazzan Skerry
    _Count
};

class DecisionProvider
{
public:
    virtual ~DecisionProvider() = default;

    // Which archetype provider a deck routed to -- OBSERVABILITY, not a decision input. Archetype
    // detection is by card params (SelectDecisionProvider), and it has silently MISROUTED decks three
    // times (Goblins -> AntiLifegain via Goblin Matron's tutor_to_hand; Mirrorwing -> Goblins via
    // Goblin Instigator's etb_self_creates_tokens; FiveColour -> AntiLifegain via its fetchlands),
    // each caught only after a deck had been measured under another archetype's heuristics. Nothing
    // printed which provider was in force, so nothing could have caught them earlier. Pure reporting:
    // no engine path branches on this.
    virtual const char* Name() const { return "Generic"; }

    // TutorCandidates -- tutor priority: ordered library card-name candidates for a tutor
    // (Idyllic / Enlightened). 1 = decided, >1 = search picks, {} = whiff.
    virtual std::vector<std::string>
    TutorCandidates(const GameState& s, int controller, const CardParams& pp) const = 0;

    // TutorToBattlefieldPutOrder -- tutor-TO-BATTLEFIELD put ORDER + SELECTION (Dragonstorm). Returns the
    // ordered list of card NAMES to put onto the battlefield for a `tutor_to_battlefield` resolution that
    // puts up to `max_puts` (= the storm total, already capped at library Dragons by the caller). Names may
    // REPEAT to honour multiplicity (two "Scourge of Valkas" entries == put 2 Scourges); the returned length
    // is <= max_puts. Unlike TutorCandidates (which the engine expands to every library copy of each listed
    // name in library order), this is an EXACT max_puts-aware SUBSET selection PLUS a single deterministic
    // put-order -- so when N is small the provider can reserve a slot for a same-turn-relevant Dragon (the
    // haste-Dragon) instead of letting a run of Scourges crowd it out. The engine keeps the PUT mechanism
    // (find/remove/enter + FireEtbWatchers cascade + reshuffle); only the which-and-in-what-order decision is
    // provider-owned. Default {} (empty) -> the engine falls back to TutorCandidates' library-order
    // enumeration exactly as before, so every non-Dragonstorm deck (and Dragonstorm under MTG_UNPRUNED) stays
    // byte-identical. Only DragonstormProvider overrides it. See PerformTutorToBattlefield +
    // analyze-Dragonstorm.md.
    virtual std::vector<std::string>
    TutorToBattlefieldPutOrder(const GameState& /*s*/, int /*controller*/,
                               const CardParams& /*pp*/, int /*max_puts*/) const { return {}; }

    // SacTutorPutList -- Defense of the Heart upkeep sac-tutor ("search your library for up to two
    // creature cards, put those cards onto the battlefield"): which creature cards (by NAME, an
    // ordered multiset like TutorToBattlefieldPutOrder -- repeats honour multiplicity) to put, in
    // ENTER order. This is an UPKEEP decision the search cannot branch over (same architectural
    // position as the Vial charge heuristic), so the default is a deterministic closed-form
    // immediate-drain maximisation over singles and ordered pairs of library creature names:
    // gift-token makers (etb_opp_creates_tokens) score tokens x enter-drain watchers, sweepers
    // (etb_opp_creatures_debuff) score killable-opp-creatures x death-drain watchers, watcher
    // newcomers raise the multipliers for cards entering AFTER them; total power tiebreak.
    // Human play overrides via g_play_sac_tutor_chooser. Only called for a permanent with
    // upkeep_sac_tutor_creatures > 0 -> never invoked for any other deck. Disclosed in the
    // Creature Giving Stage-6a table; A/B-able like any provider heuristic.
    virtual std::vector<std::string>
    SacTutorPutList(const GameState& s, int controller, const CardParams& pp, int max_puts) const;

    // FetchCandidates -- fetch priority: ordered land-name candidates for a fetchland.
    virtual std::vector<std::string>
    FetchCandidates(const GameState& s, int controller, const CardParams& fetch_pp) const = 0;

    // CanAutoFireAltPayload -- auto-fire safe alt payload: per-card predicate (the engine keeps the
    // deterministic re-scan loop; only the "is this free payload safe to fire" decision
    // is provider-owned).
    virtual bool CanAutoFireAltPayload(const GameState& s, int controller,
                                       const CardDefinition& def) const = 0;

    // HasAnyDigSource / ShouldConsiderDig / SelectDigSource -- dig gate + source
    // (Treasure Hunt / Land's Edge cycling/sac-draw).
    virtual bool        HasAnyDigSource (const GameState& s) const = 0;
    virtual bool        ShouldConsiderDig(const GameState& s) const = 0;
    virtual std::string SelectDigSource (const GameState& s, const ManaPool& pool,
                                         bool& out_is_sac) const = 0;
    // DigDecisionSearched -- opt this deck's dig gate into the SEARCHED axis (Plan::dig_choice):
    // the enumerator emits never-dig / dig-while-affordable variants per base plan and the
    // rollout scores them, with ShouldConsiderDig as the base plan's default and the horizon
    // behaviour (USER 2026-08-28: "searched with heuristics is the way to go"). False keeps the
    // heuristic-only gate (Treasure Hunt's measured greedy; every digless deck trivially).
    virtual bool        DigDecisionSearched() const { return false; }

    // LandsEdgeFireCount -- how many lands to discard to a Land's Edge this activation.
    virtual int LandsEdgeFireCount(const GameState& s, int rate) const = 0;

    // WantVialCharge -- whether to add an Aether Vial charge counter this upkeep.
    virtual bool WantVialCharge(const GameState& s, const Permanent& vial) const = 0;

    // ScryKeepOnTop -- scry/surveil per-card keep decision: keep `top_card` on top (true) or
    // bottom/bin it (false). The engine keeps the reorder/bin MECHANISM (ScryTop/
    // SurveilTop); only the keep DECISION is provider-owned.
    virtual bool ScryKeepOnTop(const GameState& s, const Card& top_card) const = 0;

    // KeepReorderTop -- Ponder-style reorder keep-vs-shuffle: a SET decision over the cards looked at
    // (top N). Return true to KEEP them on top (SituationalCardRank then ORDERS them), false to
    // SHUFFLE them all away. Unlike the per-card ScryKeepOnTop gate, this judges the WHOLE set --
    // e.g. the Hinata deck must shuffle a top set that contains no way to advance toward Hinata,
    // even if one card would individually pass ScryKeepOnTop. Default mirrors the legacy rule
    // (keep iff any single card is wanted), so every non-overriding deck is byte-identical.
    virtual bool KeepReorderTop(const GameState& s, const std::vector<Card>& top) const
    {
        for (const Card& c : top) { if (ScryKeepOnTop(s, c)) { return true; } }
        return false;
    }

    // CastEnablerFirst -- cast-sequencing: should this hand cast go in the ENABLER-FIRST pass (cast
    // + resolve before other spells, so a same-turn payload sees the enabler active)?
    // The engine keeps the multi-pass apply MECHANISM; only the partition is provider-owned.
    virtual bool CastEnablerFirst(const GameState& s, const std::string& card_name) const = 0;

    // DiscardLandsFirst -- discard-to-7 policy: when shedding to hand size, prefer discarding a LAND
    // first (true) over the highest-MV card (false). Used when a Land's Edge land outlet
    // makes lands ammunition. An INPUT to CleanupDiscardCandidates below, not a separate decision.
    virtual bool DiscardLandsFirst(const GameState& s) const = 0;

    // InterchangeableRequiredGroup -- required pieces that are DIFFERENT CARDS filling ONE role,
    // so holding one makes the others redundant ("you only need one of the enabler pieces at a
    // time", user 2026-08-07). Returns the group containing `name`, or nullptr when the card has
    // no interchangeable partners. Consumed by CleanupDiscardProtected, which counts redundancy
    // over the whole group instead of by name: without this, two distinct enablers each look like
    // a "last copy" and BOTH are protected, which silently vetoes a provider's own decision to
    // shed the spare one. Deck knowledge, so it lives in the archetype provider; default nullptr
    // keeps name-only counting (byte-identical for every deck that does not override).
    virtual const std::vector<std::string>* InterchangeableRequiredGroup(const std::string&) const
    { return nullptr; }

    // CleanupDiscardCandidates -- cleanup discard (hand over its size limit): WHICH card to shed,
    // as hand indices in PREFERENCE order. This is the whole rule, provider-owned: the engine keeps
    // only the mechanism (move the chosen card to the graveyard).
    //
    // [heuristic-then-search]: index 0 is what a non-branching caller takes, so returning ONE index
    // decides the discard with no branch (and is byte-identical to the historical single answer);
    // returning several is what lets the search choose among them instead of trusting the ranking.
    // THE RETURN IS THE WHOLE CANDIDATE SET: the executor's searched cleanup pass
    // (AIEngine::ChooseDiscard) trials exactly the returned indices -- no more. (Until 2026-08-06
    // it fanned a probe rollout over the ENTIRE hand and used this ranking only as a tie-break,
    // which on treasure_hunt's 15-25-card cleanups is what made one bounded game cost hours; see
    // docs/design/th-d5-five-hour-game.md.) A provider needing the full ordering for a DIFFERENT
    // question (multi-card pitches: Land's Edge, retrace costs) keeps that ranking internal rather
    // than widening this return -- see TreasureHuntProvider::CleanupDiscardFullRanking.
    //
    // `required_pieces` is the deck's protected combo-piece list. It is passed in rather than read
    // off the state because the executor sources it from its own MulliganProfile while the rollout
    // reads GameState::m_required_pieces -- the same set, but the caller owns which.
    //
    // The base implementation is the engine's historical rule, so a provider that does not override
    // behaves exactly as before:
    //   1. If DiscardLandsFirst (a land outlet makes lands ammunition), the first non-staged LAND.
    //   2. Otherwise the highest-MV non-staged card that is not a protected required piece
    //      (protection scope per DiscardProtectScope; a redundant copy stays discardable).
    //   3. Last resort, when every non-staged card is protected: max-MV overall, staged preferred.
    // Defined out-of-line (DecisionProviders.cpp) because that rule lives in SpellEffects.h, which
    // includes this header.
    virtual std::vector<int> CleanupDiscardCandidates(
        const GameState& s, const std::vector<std::string>* required_pieces) const;

    // CleanupDiscardShedOrder -- the same rule, as the FULL shed ORDER: every card in the hand,
    // most expendable first. It differs from CleanupDiscardCandidates for exactly one reason, and
    // only on the two providers that have it: NARROWING the candidate set is a statement about the
    // executor's searched FAN (TreasureHunt and FiveColour each return a single index, measured, so
    // the trial rollouts have nothing to fan over -- th-d5-five-hour-game.md), not a statement that
    // the deck has no opinion about which card goes SECOND.
    //
    // A cleanup that must shed three cards needs three answers. Reading them off a one-entry
    // candidate list is impossible, which is what used to force the caller to shed one card, build
    // a fresh hand, and re-consult -- the loop this hook exists to retire. Consumed by
    // CleanupDiscardShedSet; the default is the candidate list, which for every other provider
    // already IS the whole ranking, so only the two narrowing providers override it.
    virtual std::vector<int> CleanupDiscardShedOrder(
        const GameState& s, const std::vector<std::string>* required_pieces) const
    { return CleanupDiscardCandidates(s, required_pieces); }

    // CleanupDiscardShedStable -- is the shed order above PREFIX-STABLE? That is: after shedding
    // its own top pick, does re-ranking the smaller hand leave the remaining cards in the same
    // order? When true (the default, and true of every bucket rule in the suite -- they choose which
    // cards to KEEP from the whole hand and shed the complement, and removing a card that was
    // already going does not change that), a cleanup that must shed three cards reads three entries
    // off ONE consultation. When false, the rule has to be asked again after every shed, because
    // shedding changes its answer.
    //
    // Treasure Hunt is the one deck that says false, and it is not an implementation wart: its
    // ranking bands SPARE copies ahead of unique cards, so shedding the duplicate Land's Edge makes
    // the survivor the deck's only outlet and moves it to the back. A prefix would shed both. This
    // is exactly the class of rule that must not be batched, so it is declared rather than assumed
    // -- MTG_DISCARD_SHED_VERIFY=1 checks the claim against the per-shed loop on every cleanup.
    virtual bool CleanupDiscardShedStable() const { return true; }

    // AttackDigPutCandidates -- Armored Skyhunter's attack trigger: WHICH of the revealed
    // Aura/Equipment cards to put onto the battlefield (ranked best-first; empty = decline the
    // "may"). `examined` is the looked-at top-N in library order; `legal` the indices of
    // Aura/Equipment cards among them. Base rule (out-of-line, DecisionProviders.cpp): the
    // largest realized-power put -- equip_power_bonus (auras: aura_power_bonus) descending, ties
    // to lower index. Provider-owned per the core invariant: the trigger resolves inside combat
    // where no plan-variant branching exists, so this pick IS the decision (disclosed 6a; the
    // human chooser overrides it in the viewer).
    virtual std::vector<int> AttackDigPutCandidates(
        const GameState& s, int controller,
        const std::vector<Card>& examined, const std::vector<int>& legal) const;

    // AttackDigAttachHost -- the same trigger's second choice: WHICH controlled creature the put
    // Equipment attaches to (0 = leave unattached). Base rule (out-of-line): the host whose
    // realized damage THIS combat rises the most -- delta = (power+bonus)*(ds_after?2:1)
    // - power*(ds_before?2:1) over the ATTACKING creatures (a non-attacker realizes nothing this
    // turn), respecting equip_min_power; ds_after counts the incoming equipment (a bare Kor
    // Duelist flips to double strike). Ties to lower card number.
    virtual int AttackDigAttachHost(
        const GameState& s, int controller, const Card& equip_card,
        const std::vector<int>& attacker_bf_indices) const;

    // JitteSpendCount -- Umezawa's Jitte: how many charge counters to spend on "+2/+2 until end
    // of turn" for THIS attacker's combat damage. Default -1 = greedy spend-all INCLUDING the
    // double-strike mid-step earnings (see JitteDamageMath; per-turn damage-optimal). The one
    // real approximation is never SAVING counters for a future double-strike turn -- an A/B-able
    // judgment call, human-overridable in the viewer. A non-negative return spends exactly that
    // many pre-strike. Consulted by the combat core AND both attack projections, so an override
    // stays lockstep by construction.
    virtual int JitteSpendCount(const GameState& s, int available_counters) const
    {
        (void)s; (void)available_counters;
        return -1;
    }

    // FirebreatheActivations -- combat pump ("firebreathing"): how many activations to pay for.
    // Returns candidate counts in PREFERENCE order; index 0 is what a non-branching caller takes.
    // A NEGATIVE entry means "as many as the pool affords" -- the greedy maximum -- which is the
    // default, so this costs no probe and is byte-identical to the historical behaviour.
    //
    // Provider-owned because "spend the whole pool on pump" is an ASSUMPTION, not a rule: it is
    // right for a goldfish with no second main and wrong for a deck that needs to hold mana (a
    // Scourge ping, an instant). Returning several concrete counts is what lets the search decide.
    // The engine keeps the mechanism (which activation, in what order -- ApplyFirebreathing).
    virtual std::vector<int> FirebreatheActivations(const GameState& s) const
    {
        (void)s;
        return std::vector<int>{ -1 };
    }

    // ShouldEmitRiskyAltPayload -- whether to EMIT a risky alt-cost payload (Reverent Silence: free, but its
    // destroy-all-enchantments wipes the caster's own Aria/Remedy) as a searched action.
    // The engine keeps the alt-cost preconditions (alt_lifegain_cost>0 + Forest control)
    // and builds the Action; this is the wipe-vs-value gate.
    // `at_cast_time` distinguishes the two callers, and the antilife (c) replacement term is
    // scoped to it: EMISSION (false -- CollectActions deciding whether the GREEDY may take the
    // line; search nodes bypass via search_risky_live) vs the CAST-TIME GUARD (true -- vetoing an
    // already-committed cast at apply). A term open at emission hands the line to the greedy
    // policy, which cannot judge the wipe (measured d0 red on held-out twice); open at cast time
    // only, it lets a SEARCH-committed chain execute while the greedy never initiates one.
    virtual bool ShouldEmitRiskyAltPayload(const GameState& s, int controller,
                                           const CardDefinition& def, bool at_cast_time) const = 0;

    // ShouldStageSpectacleDraw -- whether to enumerate spectacle-staging plan variants for a draw spell
    // (cast a cheap damage spell first to unlock its cheaper Spectacle cost). The engine
    // keeps the variant-building mechanism; this is the archetype gate.
    virtual bool ShouldStageSpectacleDraw(const GameState& s, int controller,
                                          const CardDefinition& draw_def) const = 0;

    // ShouldCastDrawEngine -- whether to CAST a "flood engine" card (Treasure Hunt's DrawUntilNonland,
    // and cascade/retrace cards like Throes of Chaos that can cascade into Treasure Hunt).
    // Firing the engine when the drawn lands cannot be used wastes them: they hit cleanup
    // discard with no Land's Edge to throw them. The engine asks this BEFORE emitting the
    // cast candidate (so the search and the bottoming rollouts both inherit the gate).
    // Generic = always cast (byte-identical); the Treasure-Hunt provider gates on having a
    // payoff this turn (Land's Edge in play, mana to cast Land's Edge after the engine, or
    // a no-max-hand-size land to keep the draw). `def` is the engine card being considered.
    virtual bool ShouldCastDrawEngine(const GameState& s, int controller,
                                      const CardDefinition& def) const = 0;

    // TrickCastSensible -- whether casting this Treasure-making solo-target trick (Gold Rush) is a
    // line a competent pilot considers on this board. USER doctrine (2026-08-12): magnetless GR is
    // 2 mana for 1 Treasure -- NEVER a this-turn mana play; it is a ramp / mana-screw-mitigation
    // play ("essentially a way to drop a Zada or Mirrorwing a turn or more earlier") or a
    // no-point-holding-back play (redundant copies + a board). A cast with a magnet out, or one
    // whose pump could matter for this turn's lethal, is always sensible. Asked at the same
    // enumeration choke point as ShouldCastDrawEngine so the search, rollouts, and greedy
    // re-solves all inherit the gate (lockstep by construction). This is a game-understanding
    // filter, not a width cap; MTG_UNPRUNE=treasuretrickcast (UnprunedGate::TreasureTrickCast)
    // restores the ungated enumeration for audit. Default true -> every deck without a provider
    // rule is byte-identical.
    virtual bool TrickCastSensible(const GameState& /*s*/, int /*controller*/,
                                   const CardDefinition& /*def*/) const { return true; }

    // HasExtraLethalModel / ExtraLethalDamage -- deck-specific extra damage toward THIS turn's lethal, BEYOND
    // the generic combat + direct-damage total the engine already sums. This is the Treasure Hunt / Land's
    // Edge model: lands in hand are Land's Edge ammunition, and a clairvoyant Treasure Hunt cast this turn
    // adds the run of lands on top of the library as further ammo. `casting` lists the CardDefinitions this
    // plan casts this turn, so the provider counts a Land's Edge or Treasure Hunt being cast NOW (not only
    // one already on the battlefield). The engine keeps the generic win-check (projected attackers + direct
    // damage + THIS addend >= opp life); only the deck-specific addend is provider-owned.
    // HasExtraLethalModel() is the cheap gate: when false the engine skips building `casting` entirely
    // (byte-identical fast path), so a deck with no such model pays nothing. Generic = false / 0.
    virtual bool HasExtraLethalModel() const = 0;
    virtual int  ExtraLethalDamage(const GameState& s,
                                   const std::vector<const CardDefinition*>& casting) const = 0;

    // ProjectsAlternateWin -- does this board win THIS turn by a loss condition that is NOT damage?
    // Today that means decking the opponent out (CR 104.3c), which no amount of ExtraLethalDamage
    // can express: returning a huge number from a function named "damage" to signal a deck-out
    // would be a lie propagating into every consumer that reads a damage rate.
    //
    // Same contract as ExtraLethalDamage: an input to the win PROJECTION, never a win. Execution
    // stays the arbiter, so an over-claim costs a mis-ranked plan and cannot report a win the game
    // did not produce. Gated behind HasExtraLethalModel() like the addend, so a deck with no such
    // model pays nothing and stays byte-identical. Generic = false.
    virtual bool ProjectsAlternateWin(const GameState& s,
                                      const std::vector<const CardDefinition*>& casting) const
    { (void)s; (void)casting; return false; }

    // ArchetypeCardValue -- archetype-specific per-card VALUE for the candidate-ordering heuristic
    // (TurnSolver's EvalCard), for cards whose worth is a combo / clairvoyant assumption rather
    // than a generic single-card estimate. The Treasure Hunt provider values a Treasure Hunt
    // (clairvoyant count of lands on top of the library x Land's Edge rate) and a Land's Edge
    // (lands-in-hand x rate). `dmg_unit` is EvalCard's damage-equivalent unit. Returns true and
    // sets `out` when the provider owns this card's value; false -> EvalCard uses its generic
    // estimate (so the engine keeps every generic card type). Generic = false.
    virtual bool ArchetypeCardValue(const GameState& s, const CardDefinition& def,
                                    int dmg_unit, int& out) const = 0;

    // CastOrderRank -- cast-order rank for a non-sacrifice hand cast (LOWER = cast earlier). The
    // engine stable-sorts the turn's hand casts by this rank, so the RELIABLE ordering rules
    // live here as a heuristic instead of in an expensive (and budget-diluting) permutation
    // search. The point is to make the canonical line REALISE what EnumeratePlans already
    // projects -- e.g. prowess. Generic ranks (all provably non-negative in a goldfish):
    //   * creatures (10) before noncreature spells (20): a haste prowess creature cast BEFORE
    //     a noncreature spell catches that spell's prowess trigger and attacks bigger; casting
    //     it after wastes the pump (the executed order, not the SET, decides this).
    //   * on-cast SELF-damage sources (30, LAST): an Eidolon of the Great Revel cast after this
    //     turn's other MV<=3 spells avoids them triggering its self-ping (the spells already
    //     resolved before it entered). Same casts either way, so only the ORDER changes life.
    // Archetypes override (antilife: enablers rank 0 so a same-turn payload sees the flip).
    virtual int CastOrderRank(const GameState& s, const CardDefinition& def) const = 0;

    // CastOrderRankLatest -- the LATE end of a card's position RANGE (USER 2026-08-30: "you would
    // have a potential range of positions for a few specific spells, but most would have one spot
    // in the order"). CastOrderRank stays the NOMINAL spot -- it alone drives execution sequencing,
    // the canonical continuation, and every existing consumer. This bound is consumed ONLY by
    // breakpoint condemnation (BpSlotIsAfterSite): a card is condemned at a site solely when even
    // its LATEST legitimate position precedes the site, i.e. when its whole range has passed. The
    // default (latest == nominal) is exactly the pre-range semantics, so a deck that declares no
    // ranges is byte-identical. A range REPLACES a soundness exemption, never adds one: it widens
    // re-admission only for the declared cards, and it is part of the deck's reviewable order.
    virtual int CastOrderRankLatest(const GameState& s, const CardDefinition& def) const
    { return CastOrderRank(s, def); }

    // PromoteCantripsInCastOrder -- does this deck want its cheap cantrips promoted to the
    // information tier (rank 2, "draw first")? This is a PER-DECK question and the measurement says
    // so plainly: the same promotion, on both seed sets, is a consistent WIN on Mirrorwing (-20.0
    // game-turns on regression, -16.0 on smoke) and a consistent LOSS on Hinata (+15.0 / +13.0).
    // At the root they cancel -- the global arm's margin over ordering-alone flipped sign between
    // seed sets (-0.00019 smoke, +0.00004 regression), i.e. it is not there. So the promotion
    // belongs where the decks disagree: the archetype provider, never the root.
    // Default false. See docs/design/cast-order-ideal-with-ranges.md.
    virtual bool PromoteCantripsInCastOrder() const { return false; }

    // OrderOpaqueCastsByRank -- per-deck opt-in to the MTG_ORDER_OPAQUE behaviour: rank-sort the
    // casts of an ORDER-OPAQUE set (draw / staging / solo-target trick) instead of leaving the
    // order to the search. USER doctrine (Mirrorwing review, 2026-08-18): "We need to order
    // everything, not have search own the order in order to avoid high expenses. Most spells
    // don't care about the order, so this becomes relatively easy." The adoption route for the
    // arc's opaque lever: the global env flag measures, this hook is where a reviewed deck
    // adopts. Default false -> byte-identical.
    virtual bool OrderOpaqueCastsByRank() const { return false; }

    // SearchedSecondMainInSearch -- per-deck opt-in to the MTG_SEARCH_SECOND_MAIN behaviour:
    // the search's INTERIOR second mains (SolveSecondMainInSearch, every full-search ply) run a
    // real budgeted search instead of the greedy Solve. The global lever's recorded rejection
    // (2026-08-09: antilife/hinata red, does not recover with budget -- truncation-shaped,
    // classify-stack 2026-08-16) rested on "no suite deck's post-combat main carries a real
    // decision"; a deck that adopts a main-phase specification (ClassifiesMainPhases) CREATES
    // that deck, and the global arm re-measured 2026-08-19 shows exactly the split: fivecolour
    // green, antilife/hinata still red. So the adoption is per-deck, like the rest of the
    // cast-order arc: opt in where the m2 decision is real, keep greedy where searching it only
    // dilutes the shared budget. Default false -> byte-identical.
    //
    // This is also the per-deck adoption route for the standing USER directive "search should be
    // truly search at every level. Greedy is simply too unreliable to be part of it."
    // (main-phase-classification.md). KittyEquipment opted in on the same evidence shape as
    // fivecolour: four d3 arms x 100 games (greedy, MTG_SEARCH_SECOND_MAIN, MTG_PHASE_CLASSIFY,
    // both) all returned avg 5.0300 and play digest 3e6ea44e9c15d572, so the searched path reaches
    // the same decisions there for free. (Rebase 2026-08-20 collapsed a duplicate hook of mine,
    // SearchesSecondMain, into this one -- two hooks with identical semantics is the drift this
    // file warns about.)
    virtual bool SearchedSecondMainInSearch() const { return false; }

    // SearchesRolloutSecondMain -- the OTHER call site of the interior second main, split out
    // 2026-08-22 because the two are not the same kind of thing:
    //
    //   * SearchedSecondMainInSearch (above) is the BRANCH site -- SolveWithLookahead's candidate
    //     loop, where the m2 prices "what does passing buy me". That is a DECISION the search makes,
    //     and it is what the USER directive "search should be truly search at every level" is about.
    //   * This hook is the ROLLOUT site -- SimulateToEndImpl's per-turn second main, i.e. the
    //     playout policy of the LEAF ESTIMATOR. Not a decision: a scoring device.
    //
    // Anti-Lifegain measured the difference and it is large and one-directional. Searching the
    // BRANCH site is byte-identical to the greedy Solve over 26,000 games (6000 train + 8000
    // held-out + 12,000 across four shuffle salts) -- the search and greedy simply agree there.
    // Searching the ROLLOUT site costs +12 turns / 3000 train games at d3, reproduced in all five
    // shuffle realisations (+52 / 30,000) and all four DECOUPLED search salts (+43 / 12,000), so it
    // is neither draw-order variance nor reshuffle clairvoyance. It is also non-monotone: dropping
    // the rollout's m2 entirely costs +7, i.e. greedy is an interior optimum -- more playout
    // fidelity is not more ranking accuracy, which is the standing LAW's "HONEST where you SCORE"
    // half. Roughly two thirds of the cost is budget dilution (the gap closes 12 -> 4 at 20x budget;
    // the rollout charges the shared budget per simulated turn-step, and a searched m2 multiplies
    // that) and the rest survives 20x.
    //
    // DEFAULT = whatever the deck answered for the branch site, so every deck that already adopted
    // the hook (fivecolour, KittyEquipment) is byte-identical. A deck overrides this to false to
    // keep the cheap greedy playout while its DECISIONS are fully searched.
    // See docs/design/antilife-main-phase-split.md 2026-08-22y.
    virtual bool SearchesRolloutSecondMain() const { return SearchedSecondMainInSearch(); }

    // GradesNoWinLeaf -- DEFAULT ON. When the rollout reaches the horizon with no win, publish the
    // resulting position's OPPONENT LIFE as the tie-break instead of letting every hopeless line
    // score the identical `max_turns + 1` and fall through to `plan.value`. See
    // docs/design/horizon-honest-leaf.md for the measurement (48,300 paired games across all 14
    // suite decks: -58 turns global, 50 worse : 91 better; hinata -45, treasure_hunt -13,
    // fivecolour -4, mirrorwing -3).
    //
    // ON BY DEFAULT, and deliberately so (USER 2026-08-23: "we should have a sensible default...
    // per-deck logic requires per-deck work"). A new deck inherits the fix instead of needing
    // someone to notice and opt in. **NO DECK OPTS OUT TODAY** -- dragonstorm did until 2026-08-23,
    // when re-testing showed its one piece of evidence supported neither gate (see below).
    //
    // WHEN TO OVERRIDE THIS TO FALSE. The predicted failure mode is that opponent life at the
    // horizon is a DAMAGE-RACE proxy, so it undervalues a deck whose value is STORED rather than
    // expressed as damage by the horizon -- combo/storm/ramp banking resources for a discontinuous
    // payoff a shallow rollout cannot see land. That remains the shape to look for, but note that
    // the ONE deck we believed exhibited it did not, so do not assume an archetype qualifies:
    // measure it. Run `scripts/leaf_tiebreak_check.py <deck>` and clear BOTH of the USER's adoption
    // gates (2026-08-23), which are separate and both blocking:
    //   1. "are we improving the play generally" -- aggregate average AT PLAY SETTINGS (the deck's
    //      resolved value_play depth/budget, NOT the suite's d3/d5 gate cells) on a large sample.
    //      Escalating depth/budget proves nothing here: this tie-break fires only when a rollout
    //      reaches the horizon WITHOUT a win, so more budget just stops it firing.
    //   2. "are we preventing search from finding a win" -- game by game, at UNLIMITED budget and
    //      the depth the game won at BEFORE the change. Escalate to CONVERGENCE, not to a round
    //      number: dragonstorm's case was filed as "survives 20x" when it recovers at 100x.
    virtual bool GradesNoWinLeaf() const { return true; }

    // PhaseFilterRootTurnOnly -- per-deck ROOT-TURN AUTHORITY for the pre-combat Main2 filter
    // (the condemnation arc's lesson applied to the phase split, 2026-08-21): the filter fires
    // at REAL decision turns (executor play + the search's root turn, incl. its interior m2 and
    // breakpoint re-solves) but NOT at projected future turns inside rollouts. Rationale: a
    // rollout's future turns are played by the GREEDY tail, and a filtered greedy future plays
    // the deferred casts badly -- deflating hold-lines in projection (measured on Anti-Lifegain:
    // 38 budget/depth/SSM-immune +1-turn held-out games, all early-dump-over-hold flips, zero
    // nonconvergence). An UNfiltered greedy future is the closer approximation of the filtered
    // SEARCHED decisions those turns get in real play. Default false -> filter-everywhere (the
    // FiveColour-adopted behaviour, byte-identical).
    virtual bool PhaseFilterRootTurnOnly() const { return false; }

    // CondemnsPassedMainPhase -- per-deck opt-in to the ORDER-CONDEMNATION post-combat filter
    // (USER model, 2026-08-19): a Main1-classified card the pre-combat decision passed on is
    // condemned for the rest of the turn -- no post-combat harvest (real m2, interior
    // projections, continuations) re-offers it; newly drawn/acquired cards are exempt via the
    // GameState::m1_hand snapshot. The complement of ClassifiesMainPhases: that filter stops
    // main 1 from considering main-2 cards, this one stops main 2 from re-litigating main-1
    // cards. Default false -> byte-identical.
    virtual bool CondemnsPassedMainPhase() const { return false; }

    // CondemnsConsideredAtBreakpoint -- the BREAKPOINT twin of the hook above, and per-deck for
    // exactly the same reason. A card the section already considered and passed on is not re-offered
    // in the continuation the section's own draw produced; the DRAWN card is exempt (it is a
    // duplicate of nothing) via the pre-draw hand snapshot, and a card the plan CASTS is exempt via
    // BpPlanCasts. That is USER's "we should only be considering spells that have not been
    // considered already at every point, making the breakpoints fully distinct from each other".
    //
    // WHY IT IS NOT A GLOBAL FLAG. MTG_BP_CLASSIFY is the same filter as a process-wide lever, and
    // turning it on generically is MEASURED HARMFUL: smoke 27/36, 30 searched games slower, 120
    // play-changed, Dragonstorm gi11/gi146 6 -> loss. Where a deck's breakpoints are cantrip/staging
    // chains, a card the plan declined really is worth reconsidering after a dig, so the filter is a
    // genuine quality prune there. It is safe only on a deck whose breakpoints do not have that
    // property -- KittyEquipment has exactly one breakpoint class (the Puresteel equipment-ETB draw)
    // and had ZERO before it existed, so nothing else can be filtered. Default false ->
    // byte-identical. See docs/design/equipment-etb-draw-breakpoint.md.
    virtual bool CondemnsConsideredAtBreakpoint() const { return false; }

    // CastOrderFallbackRanks -- the FUNDING ladder for a cast whose ideal position is LATE but
    // whose output (Treasures, ritual float) may be needed earlier to pay for the line. Returns
    // the ranks to try in preference order (first = preferred); the range ladder walks down the
    // list only while FirstUnpayablePos says the line cannot be paid. Distinct from the
    // ideal->cost-efficient range, which walks a card LATER when its early position starves the
    // line -- this walks a producer EARLIER when the line starves without it (USER, Mirrorwing
    // review 2026-08-18: Gold Rush "should go after the Magnets at the earliest, but preferably
    // you would be able to wait until after Twinflame or after draw"). Empty = no ladder.
    virtual std::vector<int> CastOrderFallbackRanks(const GameState& s,
                                                    const CardDefinition& def) const
    { (void)s; (void)def; return {}; }

    // LandDropCastOrderRank -- WHERE THE LAND DROP SITS IN THE CAST ORDER, or -1 for "no declared
    // slot" (every deck by default -> byte-identical). The drop has always been a play the order had
    // no opinion about; giving it a rank makes it condemnable like any other, which is the whole of
    // the USER's 2026-08-27 specification:
    //
    //   "we condemn if we can play a card in the order and choose not to do so" ... "no land drop is
    //   a true play the game can make" ... "and this play condemns all lands in hand".
    //
    // Declining the drop at its slot is therefore a real decline, and a land that was in hand when
    // the breakpoint's spell was cast may not be played in the continuation. A land the breakpoint
    // DRAWS is a new card and stays enumerable under the existing drawn-card exemption -- which is
    // what makes the rule simple enough to be safe: "there is an advantage to deferring in the case
    // we draw a better land, but that case is not condemned".
    //
    // WHY A RANK RATHER THAN A BOOL. It is condemnable only where the SITE is later in the order, so
    // this composes with BpSlotIsAfterSite instead of duplicating it: rank 0 (Mirrorwing, "land drop
    // can go first in this deck") is before every site; a deck that wants the drop after its
    // accelerants would say so with a number and the same test still holds.
    //
    // TWO CONDITIONS A DECK MUST MEET BEFORE IT MAY PIN THE DROP.
    //
    //  1. HOLDING THE DROP CAN NEVER PAY -- a land drawn later is still playable later at no loss
    //     ("we can always play a drawn land later; there is no cost to this"). Deferring is
    //     otherwise an INFORMATION play, and this search is clairvoyant, so it has nothing left to
    //     buy. MECHANICAL reasons to defer (a Karoo that must bounce an already-tapped land,
    //     storage/Land's-Edge timing, landfall) are NOT covered by that argument -- they are handled
    //     by karoo_deferred / HoldDeferredDropForLethal, which this rule does not touch.
    //
    //  2. NOTHING IN THE DECK CAN *FORCE* THE DEFER BRANCH, because a forced defer is not a
    //     decline and condemning off the back of one would delete lines the search never chose to
    //     skip. Treasure Hunt is the live counter-example and the reason this is written down: the
    //     strict flood gate (ShouldCastDrawEngine / MTG_TH_STRICT_FLOOD) suppresses every
    //     "play land THEN cast the draw engine" plan, so the defer plan is the ONLY route to casting
    //     it -- see the note at add_for_land("", "") in EnumeratePlansWithLandUncached. A deck with a
    //     flood engine, or any other gate that removes the land-first plans, must leave this at -1.
    virtual int LandDropCastOrderRank() const { return -1; }

    // CastOrderTierName -- the --cast-order-report's label for a rank TIER this provider defines.
    // nullptr = use the generic tier table (main.cpp). Display only; exists because an archetype's
    // ranks land on generic numbers with unrelated meanings (Mirrorwing's Fists@16 is not the
    // generic COST REDUCER tier) and the report is the USER's review artifact.
    virtual const char* CastOrderTierName(int rank) const { (void)rank; return nullptr; }

    // XCandidates -- candidate X values for an {X} spell (a branching-PRUNE heuristic). The engine
    // asks BEFORE emitting cast variants and emits one cast per returned value (the variants
    // share hand_index, so they are mutually exclusive in the plan), letting the search pick
    // among the narrowed set. `max_affordable` is the largest X the current mana can pay (spare
    // mana after the base cost). Generic proposes {max_affordable} (goldfish-optimal: an X burn
    // / X effect wants all available mana); MTG_UNPRUNED opens the full 1..max_affordable range
    // for the search to confirm. Empty -> the spell is not cast this turn. See analyze-deck 5f.
    virtual std::vector<int> XCandidates(const GameState& s, const CardDefinition& def,
                                         int max_affordable) const = 0;

    // BlinkActivationCounts -- how many times to activate a blink outlet ("{cost}: Exile another
    // target creature, then return it") on `source`, blinking `target`, this turn. Returns the
    // candidate K values the search branches over; one entry is a decided heuristic, several is
    // narrowed-but-searched, empty declines the activation entirely.
    //
    // Generic returns 1..min(3, max_affordable) -- the same hard bound every other K-count
    // activation (ActivateRevealTop, ActivatePump) carries, because more than three activations of
    // a value ability in one turn is fringe and the branching is not free.
    //
    // THE HOOK EXISTS FOR THE ONE CASE THAT BOUND IS WRONG: a blink loop can be SELF-FUNDING.
    // Blinking a Peregrine Drake ("untap up to five lands") refunds more mana than the activation
    // costs, so K is not bounded by present mana at all -- it is bounded by what you want to do
    // with the mana. A generic cap of 3 does not merely play the deck badly, it makes the deck's
    // only win line invisible to the search: the kill needs ~20 iterations and the enumerator would
    // never offer one. A combo provider recognises the loop and proposes the go-off count as a
    // further candidate, which is the sanctioned shape (provider proposes; search picks) and keeps
    // the narrowing in one named, A/B-testable place instead of in the enumerator.
    //
    // `max_affordable` is how many activations the CURRENT pool could pay for outright, ignoring
    // any refund -- so it is a floor on a self-funding loop, never a ceiling.
    virtual std::vector<int> BlinkActivationCounts(const GameState& s, const Permanent& source,
                                                  const Permanent& target, int max_affordable) const
    {
        (void)s; (void)source; (void)target;
        std::vector<int> out;
        const int kmax = std::min(3, max_affordable);
        for (int k = 1; k <= kmax; ++k) { out.push_back(k); }
        return out;
    }

    // ManaSinkActivationCounts -- how many times to activate a REPEATABLE, {T}-less permanent
    // ability ("{1}{C}: target opponent loses 1 life" / "... exiles the top card of their library")
    // on `source` this turn. Exactly the BlinkActivationCounts contract, and it exists for exactly
    // the same reason: these two are the EldraziDisplacerFlicker deck's win conditions, and both
    // are cashed by an unbounded blink loop, so a generic cap of 3 would not merely play the deck
    // badly -- it would make the kill invisible to the search (the kill needs ~20 drains or ~53
    // exiles). The provider proposes the go-off count; the search picks.
    //
    // Generic returns 1..min(3, max_affordable), the same hard bound every other K-count activation
    // carries. `max_affordable` is what the CURRENT pool could pay for outright, ignoring any
    // refund, so it is a floor on a self-funding loop and never a ceiling.
    virtual std::vector<int> ManaSinkActivationCounts(const GameState& s, const Permanent& source,
                                                     PermAbilityMode mode, int max_affordable) const
    {
        (void)s; (void)source; (void)mode;
        std::vector<int> out;
        const int kmax = std::min(3, max_affordable);
        for (int k = 1; k <= kmax; ++k) { out.push_back(k); }
        return out;
    }

    // BlinkTargetCandidates -- which creatures a blink outlet may target, as m_numbers. Empty means
    // "no narrowing": the enumerator offers EVERY legal target, which is the generic answer and the
    // only correct one absent a measured reason (dropping a target would be the enumerator stealing
    // a decision -- the Gamble-tutor precedent). A combo provider narrows because targets x counts
    // multiply into the odometer across every outlet on the board.
    virtual std::vector<int> BlinkTargetCandidates(const GameState& s,
                                                   const Permanent& source) const
    { (void)s; (void)source; return {}; }

    // LandAuraHostCandidates -- which LANDS an "Enchant land" Aura may be cast onto, as m_numbers.
    // Empty means "no narrowing" (every land the controller has). Generic returns empty; only a
    // deck that actually plays land auras has a reason to narrow, and it must justify it, because
    // this is a cast-target decision like any other.
    virtual std::vector<int> LandAuraHostCandidates(const GameState& s, int controller) const
    { (void)s; (void)controller; return {}; }

    // ShouldAttackWith -- combat: should this eligible creature be DECLARED as an attacker this turn?
    // The engine keeps combat eligibility (CanAttackFull: summoning sickness, tap state,
    // haste) and the damage MECHANISM; this is only the attack/hold DECISION over an
    // already-eligible attacker. Generic = true (attack with everything that can attack --
    // correct for a goldfish with no blockers). An archetype overrides to HOLD a creature
    // back (e.g. a creature whose {T} activated ability beats attacking). Honoured in
    // lockstep by the real declaration (AIEngine::DeclareAttackers) and every search-side
    // attack projection (PendingAttackDamage / prowess count / rollout combat), so an
    // override never makes the search predict an attack the executor won't make.
    virtual bool ShouldAttackWith(const GameState& s, const Permanent& attacker) const = 0;

    // AttackWith -- the gate every combat site actually calls (declaration + all projections;
    // NEVER call ShouldAttackWith directly at a combat site). Non-virtual: it applies the
    // engine-level collapsed-main mana hold first -- with the main-phase filter active
    // (TurnSolver::CollapsedMainActive) the turn's casts run AFTER combat, so an attacking
    // mana creature would tap a source the deferred main still needs; a 0-power mana dork is
    // held when some hand spell is affordable only with creature mana (the phase boundary the
    // filter removed was ALSO the mana-allocation order: main 1 spent mana, the attack got
    // the leftovers). Then defers to the archetype's ShouldAttackWith. Inert (pure
    // pass-through) whenever the filter is off -> byte-identical base behaviour.
    bool AttackWith(const GameState& s, const Permanent& attacker) const;

    // LandDropAfterHandLandTutor -- depth-0 greedy: defer the turn's land drop until after a
    // hand-land tutor resolves, so the fetched land can BE the drop (USER, Creature Giving
    // review 2026-08-19: "Sylvan Scrying is the one card that can go before the Land drop" --
    // it fetches another Forbidden Orchard; fallback "a land may be played before if we cannot
    // afford that order" = the payability condition the provider checks). The deferred drop is
    // played by the existing depth-0 second-main TryPlayLand, with the fetched land then in
    // hand. Depth>0 is NOT covered: the searched land fold cannot name a card that is not in
    // hand at enumeration, and the tutor opens no re-solve breakpoint -- recorded in
    // cast-order-rankings.md as the depth-side gap. Default false.
    virtual bool LandDropAfterHandLandTutor(const GameState& s, int controller) const
    { (void)s; (void)controller; return false; }

    // PostDrawKeepLandName -- which land to play AFTER a deferred draw-engine (Treasure Hunt) resolves and
    // the draw is known. Returns the NAME of a card in hand to play as the deferred land drop:
    // the Treasure-Hunt provider returns a drawn no-max-hand-size land (Reliquary Tower) when the
    // hand is flooding and no such land is already in play, so the whole draw is KEPT as Land's
    // Edge ammo (gi=65); "" means "play the best normal land via the engine's generic land-play".
    // The engine keeps the land-play MECHANISM (PlayLandByName / breakpoint land + recording) and
    // the open-land-drop precondition; only this card-choice is provider-owned. Generic = "".
    virtual std::string PostDrawKeepLandName(const GameState& s, int controller) const = 0;

    // SituationalCardRank -- SITUATIONAL card rank: "how much do I want THIS card on THIS turn" (HIGHER =
    // more wanted). Unlike ScryKeepOnTop (a binary keep-on-top / bottom gate) this is a continuous
    // priority that lets the dig spells (Expressive Iteration, Ponder, Preordain) ORDER their
    // looked-at cards DETERMINISTICALLY -- most-wanted to hand/top, least-wanted to bottom -- instead
    // of leaving the selection to a search branch. The decisive nuance is that situational NEED
    // overrides static card power: a land ranks at the top on a turn you need the land drop, even
    // though it is a generically weak card; once lands are covered the missing combo pieces outrank
    // the digging cantrips, which outrank the dead/duplicate payoffs. The engine keeps the selection
    // MECHANISM (which slot each ranked card goes to); only the ranking is provider-owned.
    //
    // Default (here) mirrors ScryKeepOnTop (keep -> 1, bottom -> 0) so every non-overriding deck
    // keeps its current 2-level ordering byte-identical (a stable_sort by a 2-valued key is a no-op
    // within each group). Only HinataProvider overrides it with the real situational ranking, and
    // re-expresses its ScryKeepOnTop in terms of this rank (single source of truth).
    virtual int SituationalCardRank(const GameState& s, const Card& card) const
    {
        return ScryKeepOnTop(s, card) ? 1 : 0;
    }

    // PreferHoldLandDrop -- land-banking: among EQUAL-VALUE plans, prefer HOLDING the land drop (play no
    // land) over developing it. Burn banks spare lands once it has enough mana (its curve tops at MV 2) so a
    // future topdecked Searing Blaze has a land to play for its landfall (3-to-face instead of 1). This only
    // INVERTS the develop tiebreak in EnumeratePlansWithLand -- it never reorders plans of DIFFERENT value,
    // so on a turn that actually casts Blaze the land drop (which raises the plan's value via landfall) still
    // wins on value, not the tiebreak. Honoured identically in the search and the rollout (both call
    // EnumeratePlansWithLand). DEFAULT false -> every other deck always develops (byte-identical); only
    // BurnProvider opts in, gated on lands-in-play.
    virtual bool PreferHoldLandDrop(const GameState& s, int controller) const { return false; }

    // ForcedEarlyLandName -- collapse the EARLY land-choice branch to one named land. Returns the land
    // NAME to play unconditionally this turn (when it is in hand), or "" to let the search fan out over
    // every distinct land as usual.
    //
    // This is a PRUNE, and it is deliberate. EnumeratePlansWithLand emits one plan group per distinct
    // land name in hand, so a hand holding two different lands doubles the candidate set on EVERY turn
    // until one of them is played -- and under a fixed ms budget that breadth is paid for out of the
    // spell decisions. Measured on Goblins: leaving a singleton Cavern of Souls in hand instead of
    // playing it costs +18% interior nodes (gi28) and +97% (gi166) on games whose win turn is
    // unchanged. Forcing the opening land removes that fan-out for turns 1..2, where the land choice is
    // least interesting (the deck wants its unrestricted coloured source down early regardless).
    //
    // Honoured at the single enumeration choke point, so the search and the rollout prune identically.
    // DEFAULT "" -> no prune, every deck byte-identical; only an archetype provider opts in.
    virtual std::string ForcedEarlyLandName(const GameState& s, int controller) const
    { (void)s; (void)controller; return {}; }

    // HoldDeferredDropForLethal -- after a deferred Treasure Hunt (DrawUntilNonland) resolves, HOLD the
    // still-open land drop entirely rather than developing it, because the lands now in hand are the marginal
    // Land's Edge ammunition for a lethal THIS turn. Generically the engine plays the deferred drop
    // (play_drawn_flood_keep_land), but with Land's Edge in play a land in HAND is worth `rate` damage this
    // turn; playing it as the drop removes it from the ammo pool and can drop the count below lethal -- and
    // the fire-count heuristic (LandsEdgeHeuristicFireCount) then HOLDS the rest, slipping the kill a full
    // turn (the s1 gi0 T4-vs-T3 shortfall: 10 lands in hand -> play one -> 9 -> no-longer-lethal -> hold ->
    // win T4 instead of T3). Return true only when the hand is ALREADY lethal ammo and developing the drop
    // would push it BELOW lethal (the marginal case), so the subsequent auto-fire discards them all for the
    // kill. DEFAULT false -> every other deck (and every non-marginal TH turn) develops the drop
    // byte-identically. The engine keeps the land-play MECHANISM and the open-drop precondition; only this
    // hold decision is provider-owned.
    virtual bool HoldDeferredDropForLethal(const GameState& s, int controller) const { return false; }

    // HoldDeferredDropForFurtherDig -- after a deferred Treasure Hunt (DrawUntilNonland) resolves with the
    // drop still open and NO flood-keep land revealed (PostDrawKeepLandName returned ""), HOLD the drop
    // instead of developing it when ANOTHER dig is affordable this turn. The generic fallback develops the
    // best normal land immediately (gi=881: an undeveloped drop meant a drawn land was discarded) -- correct
    // for a turn with ONE dig, wrong for a turn with more digs to come: the drop is the ONLY way to play a
    // Reliquary Tower, so spending it after dig 1 means a Reliquary revealed by dig 2 cannot be played and
    // the whole flood is discarded at cleanup (the s2 gi1 T4-vs-T5 shortfall). Holding costs nothing
    // structural: this same step runs again after the next dig and develops then if no keep land shows up.
    // Return true only when the hand is ALREADY flooding, no no-max-hand-size land is in play, and another
    // dig is payable from mana available WITHOUT the held land (so holding cannot starve the dig it is
    // waiting for). DEFAULT false -> every other deck develops byte-identically. The engine keeps the
    // open-drop precondition and the land-play mechanism; only the hold is here.
    virtual bool HoldDeferredDropForFurtherDig(const GameState& s, int controller) const { return false; }

    // NcLandDropTempoBonus -- NON-CLAIRVOYANT search tempo bonus (avg win-turns) for MAKING a land drop this
    // turn. The reshuffle-averaged NC search (TurnSolver::ReshuffleAvgChoosePlan) is mana-OPTIMISTIC: it
    // shuffles the true library away, so its mean future has normal land density and it undervalues a land
    // drop as screw-insurance -- it will DEFER the drop (a fetch-crack costs 1 life) and durdle in a
    // genuinely land-light game. This bonus prices developing mana back: it is subtracted from the averaged
    // win-turn of any land-drop plan before the min is taken, so it breaks decisions the objective considers
    // close WITHOUT overriding a real win-turn difference larger than the bonus. DEFAULT 0.0 -> inert (only
    // used inside the experimental MTG_NC_SEARCH path anyway). GenericProvider supplies the SAFE conservative
    // rule (small bonus, only while still building the mana base, and never when PreferHoldLandDrop wants to
    // bank/hold); archetypes that specifically benefit (mana- hungry, no land-as-resource mechanic) override
    // to be more aggressive. Land-pitch decks (Land's Edge / Seismic Assault) are protected by the mana-base
    // gate + PreferHoldLandDrop.
    virtual double NcLandDropTempoBonus(const GameState& s, int controller) const { (void)s; (void)controller; return 0.0; }

    // OpponentPlaysLands -- does this deck's goldfish opponent play lands? Decks whose spells target the
    // OPPONENT'S permanents for value (Hinata: Magma Opus taps them, the spread-damage / cost-
    // reduction targeting points at them) need a realistic opponent board. When true the engine
    // gives the passive opponent one land on each of the first three turns (a realistic floor:
    // most opponents have >=3 lands, and aggressive decks with fewer bring creatures = better
    // targets anyway). DEFAULT false -> every existing deck's opponent stays boardless-of-lands
    // (byte-identical); only HinataProvider opts in. NOT pure (defaulted) so no other provider
    // needs to implement it.
    virtual bool OpponentPlaysLands() const { return false; }

    // UseLethalShortCircuit -- opt IN to the board-lethal search short-circuit (Solve + EnumeratePlans):
    // when the current board's attack-all damage already kills the opponent this turn, skip the cast-subset
    // odometer and just attack. WIN-TURN-INVARIANT (winning this turn is the min win-turn), but it changes
    // WHICH winning plan is chosen -> the play log / digest differs. DEFAULT false so every deck's play stays
    // byte-identical; a provider opts in when the modest rollout speedup is worth re-accepting ITS play
    // digest (GoblinsProvider). Off-switch MTG_NO_LETHAL_CUT disables it even where opted in.
    virtual bool UseLethalShortCircuit() const { return false; }

    // DeferSacOutletPreCombat -- should a Goblin-style creature-sac OUTLET (Siege-Gang / Pashalik value
    // sacs, Skirk Prospector's sac-for-mana) be DROPPED from the PRE-COMBAT action enumeration and left
    // to the second (post-combat) main? Vs the passive goldfish opponent a VALUE sac is >= as good AFTER
    // attacking (you keep the attack), so deferring the value outlets is near-lossless while removing
    // them from the O(2^candidates) pre-combat cast-subset explosion on a wide Goblin board. Skirk's MANA
    // outlet is gated, not blanket-deferred: its pre-combat float only buys tempo the second main can't
    // recover when it funds a SAME-TURN attacker (needs haste), so the Goblins provider keeps it pre-combat
    // only when a Goblin haste lord is in play or castable from hand. `is_mana_outlet` distinguishes the two.
    // DEFAULT false -> every non-Goblins deck enumerates sac outlets pre-combat byte-identically. NOT pure
    // (defaulted) so no other provider needs to implement it. Measured quality-neutral (Goblins d3/400-game
    // 4.4375->4.4350) + 2.5-4.6x faster on the Skirk-amplified rollout outliers. See analysis-goblins.md.
    virtual bool DeferSacOutletPreCombat(const GameState& s, const Permanent& src,
                                         bool is_mana_outlet) const
    { (void)s; (void)src; (void)is_mana_outlet; return false; }

    // PayEchoToKeep -- when an echo obligation comes due and the cost is AFFORDABLE, PAY (keep the body,
    // return true) or DECLINE (sacrifice it, return false)? The engine owns the affordability gate and the
    // sacrifice/OnCreatureDies mechanism; only this pay-vs-decline JUDGEMENT is provider-owned so the
    // executor (AIEngine) and the rollout (TurnSolver) share ONE decision function -> lockstep by
    // construction. DEFAULT reproduces the historical fixed heuristic verbatim: a self-replacing body
    // (dies_watch_includes_self + a death token, e.g. Mogg War Marshal) DECLINES (the death token replaces
    // it, saving the mana); every other echo creature (Stingscourger) PAYS. So every deck is byte-identical
    // until a provider overrides -- GoblinsProvider adds the lethal/no-gas keep exceptions for Mogg.
    virtual bool PayEchoToKeep(const GameState& s, const Permanent& p) const
    {
        (void)s;
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (!d) { return true; }
        const CardParams& ep = d->params;
        const bool self_token = ep.dies_watch_includes_self && ep.dies_trigger_creates_tokens > 0;
        return !self_token;
    }

    // DominanceAxisDirection -- EOT dominance: which way is this resource axis monotone?
    //
    // The DEFAULT table below is the GENERIC truth -- an axis is listed monotone here only when
    // more (or fewer) of it is STRICTLY BETTER for any deck that can hold it, under goldfish rules
    // (the opponent never attacks and never blocks, so an opposing body is inert rather than a
    // threat). An axis whose direction is deck-dependent stays EqualRequired here and is declared
    // by the archetype provider that knows its deck -- that split is the USER's (2026-08-15): the
    // default provider carries what is strictly better, archetypes carry the overrides.
    //
    // The two judgement calls worth naming:
    //   * ChargeCounters is EqualRequired, NOT MoreDominates. Aether Vial's useful charge TRACKS
    //     THE CURVE (2 is ideal for a deck of 2-drops; 5 overshoots and puts nothing onto the
    //     battlefield), so it is the one counter where more is genuinely worse. Aim-for-a-value
    //     counters are never monotone.
    //   * AgeCounters is EqualRequired because cumulative upkeep is a COST generically (each
    //     counter is another upkeep payment) but FUEL for a deck built on what it pays out --
    //     Varchild's War-Riders gifts the opponent Survivors, which is exactly what a
    //     creature-giving shell wants. Deck-dependent, so it belongs to the archetype.
    virtual DomDir DominanceAxisDirection(DomAxis a) const
    {
        switch (a)
        {
            // Strictly better with MORE: each is a stored resource that only ever buys something.
            case DomAxis::StorageCounters:  return DomDir::MoreDominates;  // bigger burst
            case DomAxis::VerseCounters:    return DomDir::MoreDominates;  // Aria deals verse-count
            case DomAxis::Loyalty:          return DomDir::MoreDominates;  // more activations left
            case DomAxis::LoyaltyCounter:   return DomDir::MoreDominates;  // mirror of the above
            case DomAxis::PlusOnePlusOne:   return DomDir::MoreDominates;  // bigger creature
            // Depletion counters are the ones REMAINING (DecrementDepletionOnTap removes one per
            // tap and the land is sacrificed at zero), so more counters = more taps left.
            case DomAxis::DepletionCounter: return DomDir::MoreDominates;
            // Strictly better with FEWER.
            case DomAxis::MinusOneMinusOne: return DomDir::FewerDominates; // smaller creature
            // Deck-dependent or non-monotone -> exact match (see the note above). PoisonCounter is
            // fail-closed for a structural reason rather than a deck one: its direction depends on
            // WHOSE it is (ours is a loss clock, an opponent's is a win con) and this per-axis table
            // cannot say "per side". Nothing in the card pool creates one today, so equality is free.
            case DomAxis::ChargeCounters:   return DomDir::EqualRequired;
            case DomAxis::AgeCounters:      return DomDir::EqualRequired;
            case DomAxis::PoisonCounter:    return DomDir::EqualRequired;
            default:                        return DomDir::EqualRequired;
        }
    }

    // DominanceOpponentBoard -- EOT dominance: how do the OPPONENT's permanents compare? For nearly
    // every goldfish deck the axis is inert (the passive opponent gains permanents only from our own
    // effects), so equality holds trivially and the fail-closed default costs nothing. A deck that
    // GIVES the opponent permanents as fuel declares a direction instead -- creature-giving drains
    // per enemy body, so for it MORE enemy creatures dominates (USER, 2026-08-14). Undeclared ->
    // EqualRequired, the same fail-closed rule as an unknown counter.
    virtual DomDir DominanceOpponentBoard() const { return DomDir::EqualRequired; }

    // ShouldEmitUntapRitual -- emit the untap-RITUAL cast variant for an {X} untap spell (Reality Spasm)?
    // The variant floats mana for a same-turn payoff and only earns its keep with Hinata's
    // discount making the {X} free, so the solver must NOT branch on it otherwise. This is the
    // archetype GATE only -- the cost math and the ManaSourceCount stay engine-side. Was an inline
    // `HinataInPlay(state)` check in TurnSolver (audit B2); default false = byte-identical for every
    // non-Hinata deck, HinataProvider returns HinataInPlay(s).
    virtual bool ShouldEmitUntapRitual(const GameState& s) const { (void)s; return false; }

    // ModalSplitCandidates -- which splits S of a "choose N, repeats allowed" modal spell to EMIT.
    // Unite the Coalition is {2}{W}{U}{B}{R}{G} modelled as S x (2 damage to face) + (N-S) x (draw),
    // so the solver branches on all N+1 splits. They share a cost and are strictly ordered by static
    // eval, so the search rolls out six same-cost lines differing only in a damage-vs-draw trade --
    // measured as a x7 group factor and the single largest branching source on the deck. Narrowing
    // the emission is the same move that retired Ponder's variants (the #1 source at ~47%).
    // Default = every split, so every deck without an override is byte-identical.
    virtual void ModalSplitCandidates(const GameState& s, const CardDefinition& def,
                                      std::vector<int>& out) const
    {
        (void)s;
        for (int k = 0; k <= def.params.modal_choose_n; ++k) { out.push_back(k); }
    }

    // TrickTargetCandidates -- narrow a solo-target trick's enumerated creature targets (Zada/
    // Mirrorwing deck). Fills `out` with candidate card.m_numbers (battlefield creatures and/or
    // same-plan HAND creatures); leaving it EMPTY means NO narrowing -- CollectActions enumerates
    // every legal target (the search-primary default, and every deck without an override). A
    // PERFORMANCE prune (5f): the per-target variant group's size multiplies the plan odometer,
    // and a swarm board makes it the top branching driver. The narrowing provider must keep every
    // line that can matter (magnets; the best ready attacker; sick/hand bodies for haste/copy
    // payloads) and is opened back to ALL targets by MTG_UNPRUNED / MTG_UNPRUNE=tricktarget.
    virtual void TrickTargetCandidates(const GameState& s, const CardDefinition& def,
                                       std::vector<int>& out) const
    {
        (void)s; (void)def; (void)out;   // default: no narrowing
    }

    // BranchSoulfireOwnTargets -- branch on Soulfire Eruption's OWN-creature target count (0..K)?
    // Own-targeting only pays off with Hinata's per-target discount (which can enable an
    // otherwise-unaffordable cast) plus a deeper dig; without it the K+1 variants are dead weight every
    // non-combo turn. The count itself (SoulfireOwnCreatureCount) stays an engine mechanic -- this is the
    // archetype gate only. Was an inline `HinataInPlay(state)` check in TurnSolver (audit B1); default false
    // = byte-identical (K collapses to 0), HinataProvider returns HinataInPlay(s).
    virtual bool BranchSoulfireOwnTargets(const GameState& s) const { (void)s; return false; }

    // StriveCountMaxOnly -- strive-count BREADTH policy: when true, a strive trick's extra-target
    // axis enumerates only K=0 and the largest mana-ceiling-affordable K, instead of every count.
    // Deck judgment (a goldfish strive is a lethal burst: more copies is strictly more damage, so
    // intermediate counts only exist for mana-coupling with other casts), so provider-owned;
    // default false = full range (byte-identical). Opened by MTG_UNPRUNED(TrickTarget).
    virtual bool StriveCountMaxOnly(const GameState&, const CardDefinition&) const { return false; }

    // EnumGroupCap -- enumeration BREADTH policy: the max number of card GROUPS the plan enumerator
    // keeps for a turn (groups beyond this, lowest by SituationalCardRank, drop out). A tractability
    // cap -- a deep dig can leave ~20 distinct nonland casts whose powerset dominates the whole
    // search -- so it is provider-OWNED policy now rather than a hardcoded solver constant (audit
    // A1): a combo deck that needs wider enumeration can raise it. Default 12 = the prior generic
    // value (byte-identical; inert for any hand with <= cap groups). MTG_SOLVE_GROUP_CAP /
    // MTG_NO_GROUP_CAP / MTG_UNPRUNED still override engine-side for A/B.
    virtual int EnumGroupCap() const { return 12; }

    // EquipHostWidth -- haste-equip host enumeration width (how many top-scored fresh/no-haste
    // hosts get an "equip -> host" action per haste equipment). DEFAULT 1 = the measured
    // FiveColour trade-off, byte-identical for every existing deck; EquipmentProvider returns 2
    // (the KittyEquipment gi=39 same-subset-host case). MTG_EQUIP_HOST_WIDTH overrides both, and
    // MTG_EQUIP_ALL_HOSTS / MTG_UNPRUNED(equiphost) / human play bypass the width entirely.
    virtual int EquipHostWidth() const { return 1; }

    // ConsolidatesEquips -- USER equip-consolidation doctrine (2026-08-14): stack extra power on
    // ONE creature. Rider-equip candidates collapse to the top double-strike-potential host plus
    // Kemba (both offered -- ds-vs-Kemba is a SEARCHED decision, per the user: ds usually finishes
    // faster but Kemba can win slow games / low-power equipment); with neither, the single best
    // rider host. Moving an ATTACHED equipment is only offered from a non-ds host to a
    // ds-potential host or Kemba (Grafted Wargear: ds only -- its "free" equip sacrifices the
    // prior host). Haste equips (Greaves) are exempt from the move rule and always offer the
    // Kemba park + the Stoneforge tap-put enable. Default false = byte-identical everywhere;
    // EquipmentProvider returns true. Opened by MTG_EQUIP_ALL_HOSTS / MTG_UNPRUNE=equiphost /
    // human play like every other equip-width policy.
    virtual bool ConsolidatesEquips() const { return false; }

    // --- Main-phase classification (USER design 2026-08-14, docs/design/main-phase-classification.md) ---
    // Classify a hand cast by ONE question: does it help (or potentially help) this turn's ATTACK?
    //   Main1 -- yes: lords, haste creatures/granters, pumps incl. equipment. Kept pre-combat.
    //   Main2 -- no: face damage, spectacle staging, sick vanilla bodies. Enumerated ONLY in the
    //            post-combat main, where it weakly DOMINATES the pre-combat cast: combat can only
    //            ADD options between the mains (spectacle turned on, surviving vigilant attackers'
    //            mana, post-attack targets), never remove them.
    //   Both  -- the boundary of that dominance argument (draws/rituals/floating mana can feed a
    //            Main1 cast): offered in BOTH phases.
    // The post-combat enumeration is NEVER filtered, so a Main2 class moves a line to the phase
    // where it is >= as good -- it cannot delete one. When in doubt classify Main1: that is
    // current behaviour, only wider; Main2 is the assertive claim, and a per-game win-turn
    // regression under the filter is a MISCLASSIFICATION signal, not an acceptable trade.

    enum class MainPhase { Main1, Main2, Both };

    // ClassifiesMainPhases -- opt IN to the pre-combat Main2 filter (TurnSolver::CollectActions).
    // ONLY valid for a deck that actually plays a second main (GoldFishRunner::DeckUsesSecondMain):
    // there every dropped cast reappears post-combat; on a single-main deck the filter would simply
    // LOSE the cast. Default false = byte-identical everywhere. MTG_PHASE_CLASSIFY forces it on for
    // A/B measurement (same second-main caveat); MTG_NO_PHASE_CLASSIFY kills it; human play and
    // MTG_UNPRUNE=mainphase keep the full pre-combat set.
    virtual bool ClassifiesMainPhases() const { return false; }


    // SkipsUnproductiveSecondMain -- opt IN to the post-combat PRODUCTIVITY gate
    // (SolveSecondMainInSearch): inside the SEARCH, do not solve the post-combat main on a turn
    // where combat created nothing (hand and battlefield both unchanged across combat, see
    // GameState::hand_size_at_combat). USER 2026-08-19: "we need to limit the search to productive
    // options and skip it for unproductive ones."
    //
    // The claim is a DOMINANCE one, not an equivalence: on a turn where combat created no resource,
    // every m2 candidate was already enumerable pre-combat off the same mana, so main 1 -- which is
    // SEARCHED, where this m2 is greedy -- has already considered and priced it. What is given up is
    // the greedy second chance to spend leftover mana on a play main 1 declined.
    //
    // Measured on KittyEquipment before this hook existed (analysis-KittyEquipment.md): removing
    // the in-search second main ENTIRELY changes exactly ONE game in 100 at d3 (gi=7, and that one
    // diverges at T1 on rollout value, not on a lost line) while costing ~18% of runtime. This gate
    // is a strict subset of that removal, so one game is its worst case by construction.
    // Default false = byte-identical everywhere. MTG_M2_PRODUCTIVE=1 forces it on for A/B;
    // MTG_NO_M2_PRODUCTIVE=1 kills it; human play never sees it (the gate is search-only).
    virtual bool SkipsUnproductiveSecondMain() const { return false; }

    // MainPhaseOverride -- per-card doctrine consulted BEFORE the engine's template rules (deck
    // knowledge lives in the provider, like the discard doctrines). nullopt = defer to the base
    // template classifier (DirectDamage/spectacle -> Main2, Draw* -> Both, sick Vanilla/ManaDork
    // with no haste access -> Main2, everything else incl. Tier-3 `None` -> Main1-by-doubt).
    virtual std::optional<MainPhase> MainPhaseOverride(const GameState& s,
                                                       const CardDefinition& def) const
    { (void)s; (void)def; return std::nullopt; }

    // FetchSearchCandidates -- the fetch targets the SEARCH actually branches on. The provider
    // returns a list of VARYING SIZE and the engine fans over exactly what comes back -- no
    // engine-side cap (USER design 2026-08-21: "the return IS the candidate set", the doctrine
    // every heuristic prune follows). `ranked` is the full FetchCandidates list already computed
    // at the call site (best-first for ranking providers), so overrides select from it instead of
    // re-running an expensive ranking.
    //
    // DEFAULT = ONE option, the top pick (USER standing rule 2026-08-21: "if I don't specify, the
    // assumption should be that the heuristic picks only one option to return" -- the silent
    // top-n widths this replaces were "a major performance headache and I don't even know it is
    // happening"). Widening is a USER-REVIEW gate ("the AI will need to convince me it is
    // necessary"), and when granted it should be CONFIDENCE-CONDITIONAL, per STATE, not a
    // per-deck constant: "sometimes you are 100% confident in the decision and other times need
    // to search multiple. This design allows that" -- return 1 when the doctrine is sure, the
    // genuinely-ambiguous set when it is not. Engine-side, MTG_UNPRUNED and HUMAN PLAY bypass
    // this entirely (the audit must see every target; the viewer keeps every legal option).
    virtual std::vector<std::string>
    FetchSearchCandidates(const GameState& s, int controller, const CardParams& fetch_pp,
                          const std::vector<std::string>& ranked) const
    {
        (void)s; (void)controller; (void)fetch_pp;
        if (ranked.size() <= 1) { return ranked; }
        return { ranked.front() };
    }

    // TutorSearchWidth -- tutor BREADTH policy: how many of TutorCandidates' ordered targets the
    // post-dedup tutor axis actually scores, INCLUDING the provider's best (so 1 == the old
    // heuristic-only behaviour, byte-identical). Sibling of FetchSearchCap, and provider-owned for a
    // measured reason rather than a symmetry one: the axis is ADDITIVE (cost P+W, one rollout per
    // extra target), so unlike a multiplicative width it does not dilute the budget the same way --
    // and the per-deck optima genuinely DIVERGE. A deck whose tutor fetches a combo piece wants to
    // look deep; a deck whose tutor is a value grab is best served by its own top pick, because the
    // targets past rank ~2 are ones the provider already judged worse and the variants only spend
    // budget the rest of the turn wanted. Default 6 = the prior global constant.
    // MTG_TUTOR_WIDTH still overrides engine-side for A/B. See docs/design/searched-action-subdecisions.md.
    virtual int TutorSearchWidth() const { return 6; }

    // CleanupDiscardSearchWidth -- how many of CleanupDiscardCandidates' ranked cards the ROLLOUT's
    // end-of-turn cleanup branches over (1 == the ranked pick only, byte-identical to no branch).
    //
    // The EXECUTOR already searches this decision; the rollout did not, so every line the search
    // scored assumed the heuristic's shed. Provider-owned and DEFAULT 1 because the decision is
    // dead in most decks -- per 400 d0 games, five of nine suite decks never reach a cleanup
    // discard at all and three more are under 40 -- so a global width would buy plan variants that
    // pin an index nothing ever consumes. A deck that actually makes this decision opts in.
    // Width > 1 was SWEPT AND REFUTED as a blind per-plan emission (2026-08-06: monotonically
    // worse on nearly every deck at W=2/4/8 -- budget dilution; the decision fires on a tiny
    // fraction of turns while every base plan pays a variant). If the axis is ever widened it
    // must be bp-style -- fan only plans whose simulation actually reaches an over-limit
    // cleanup. See docs/design/searched-discard-as-search-node.md.
    virtual int CleanupDiscardSearchWidth() const { return 1; }

    // ManaSourceRank -- mana-source TAP ORDER: flexibility rank of a mana source (LOWER = tap earlier).
    // The greedy mana solver (AIEngine::TapForCost / TurnSolver::TapForCostDirect, scarcity path)
    // pays each pip from the lowest-ranked qualifying source, so the flexible sources stay up and the
    // exponential TapForCostBacktrack fallback is rarely entered. This is a QUALITY heuristic for a
    // sub-decision the search does NOT branch over (searching tap orderings is the very blowup we
    // avoid); tapping is always a single committed choice, so this only picks WHICH legal payment --
    // never whether one is found (the complete backtracker remains the fallback). Provider-owned so a
    // deck can override (e.g. a filter's float-colour preference). GenericProvider supplies the
    // default (basic/bounce 10, dual 20, filter 25, tri 30, rainbow 50, {C}-only manland 60). Not
    // pure-virtual-defaulted here because the ranking needs SpellEffects helpers unavailable in this
    // header; GenericProvider implements it and every archetype inherits that.
    virtual int ManaSourceRank(const GameState& s, const CardDefinition& def) const = 0;

    // ReserveCreatureHold -- which of the turn's reservable mana CREATURES the whole-turn reserve
    // ladder (BatchPrepayMainCasts) should keep holding once the full "hold every dork" rung has
    // FAILED, i.e. when the bodies genuinely compete for mana. `crea_mask` is the battlefield-index
    // bitmask of every reservable untapped mana creature; return the subset worth holding hardest.
    // Consulted only under MTG_PUMP_TARGET_HOLD with live PlanTraits (see
    // docs/design/mana-order-and-reserve-overhaul.md layer 3); returning `crea_mask` unchanged is
    // "no narrowing" and reproduces the ladder exactly as shipped.
    //
    // Base rule (USER 2026-08-25: "with targeted pumps I would have the default only reserve the
    // pumped creature"): when the plan casts an own-creature pump, hold ONLY its projected target
    // (the body the pump lands on -- losing it to a mana tap wastes the trick); with no pump, no
    // narrowing. On a bodies-are-multipliers plan (a live copy magnet + a targeted trick -- the
    // former MirrorwingProvider override, generalised 2026-09-02 because the condition is
    // param-keyed and deck-agnostic) the narrowing is exactly wrong: EVERY untapped creature is a
    // copy target and an attacker, so keep holding the whole board. Byte-identical for any deck
    // without a copies_solo_targeted_spells magnet (the trait is then always false).
    virtual std::uint64_t ReserveCreatureHold(const GameState& s, const PlanTraits& t,
                                              std::uint64_t crea_mask) const
    {
        if (t.bodies_are_multipliers) { return crea_mask; }
        if (t.pump_target_card == 0) { return crea_mask; }
        std::uint64_t hold = 0;
        const int n = static_cast<int>(std::min<std::size_t>(s.battlefield.size(), 64));
        for (int i = 0; i < n; ++i)
        {
            if (((crea_mask >> i) & 1ull)
                && s.battlefield[static_cast<std::size_t>(i)].card.m_number == t.pump_target_card)
            { hold |= (1ull << i); }
        }
        return hold;   // may be 0: a non-mana-source target is never in the tap set anyway
    }

    // SpendOneShotsFreely -- flip the one-shot (§2b) hold OFF for this plan: true = let the payment
    // spend pay-sac one-shots (Treasures) like any ranked source, false = hold them whenever the
    // turn pays without them (the default doctrine: a spent one-shot is gone forever, a held one is
    // worth exactly one mana on any later turn). Consulted under MTG_ONESHOT_RESERVE; `t` may
    // legitimately be a default-constructed PlanTraits when no plan is in scope (per-payment path
    // outside an apply), which the base rule treats as "no reason to spend" -> hold.
    //
    // The go-off case (lump doc §9, USER: "Especially in Mirrorwing Treasures should be used
    // before creatures"): on a turn whose bodies are multipliers (a live copy magnet + a targeted
    // trick in the plan), a tapped creature forfeits its trick copy AND its swing while a cracked
    // Treasure costs one future mana -- so spend the one-shots freely and let the reserve keep the
    // bodies. Formerly a MirrorwingProvider override; generalised 2026-09-02 (the trait is
    // param-keyed -- copies_solo_targeted_spells x solo_target_trick -- so it is deck-agnostic and
    // false for every magnet-less deck, byte-identical by construction). On any other plan the
    // base doctrine holds: a spent one-shot is gone forever, a held one is worth exactly one mana
    // on any later turn -- and the dork it spares untaps anyway (the gi81 rule).
    virtual bool SpendOneShotsFreely(const GameState& /*s*/, const PlanTraits& t) const
    { return t.bodies_are_multipliers; }

    // OpponentLifegainUseful -- is making the OPPONENT gain life USEFUL to us right now? A
    // Grove-of-the-Burnwillows- style drip land (tap_opponent_lifegain) normally GIFTS the opponent life -- a
    // downside -- so the engine dodges the drip: a generic pip uses Grove's painless {C} mode and leftover
    // drip lands are NOT swept at end of main. A deck whose combo turns that gift into value flips this true,
    // and the two drip rules invert: Grove taps COLOURED (drips) even for a generic pip, and the sweep fires.
    // Anti-Lifegain returns true when a Tainted Remedy / Plague Drone is active (the gain is reversed into 1
    // damage); a Grove + Punishing Fire deck would return true too (the gain buys the Fire back). Default
    // false. NOTE: the rules-level lifegain->loss reversal in OpponentGainsLife stays keyed on RemedyActive
    // -- that is a FACT about the board, not a decision, so it is not routed through here.
    virtual bool OpponentLifegainUseful(const GameState& /*s*/, int /*controller*/) const { return false; }

    // KeepFloor -- keep-floor: an archetype override that can FORCE the mulligan keep decision for a
    // hand the exhaustive keep table (or static rule) would otherwise misjudge. Consulted in the play
    // path (AIEngine::HandleMulligan) BEFORE the table, so ForceKeep overrides a table mulligan and
    // ForceMulligan overrides a table keep; Undecided (the default) falls through unchanged. This is a
    // provider HEURISTIC (empirically-backed, not a rules fact) -- e.g. the Treasure Hunt archetype
    // force-keeps a castable-TH hand that also holds a flood payoff. Base returns Undecided so every
    // deck (and the analyzer's reference/static keep path, which never consults a provider) is
    // byte-identical; only an overriding archetype changes any decision.
    virtual KeepGuard KeepFloor(const std::vector<Card>& /*hand*/, int /*mulligan_count*/,
                                bool /*on_the_play*/) const { return KeepGuard::Undecided; }

    // UseAccelPrefixCollapse -- Dragonstorm acceleration-prefix collapse gate. When true, TurnSolver's Solve
    // / EnumeratePlans odometer enumerates only the K+1 CHEAPEST-FIRST PREFIXES of the ritual accelerants
    // (actions with ritual_floating_mana > 0: Rite of Flame, Pyretic/Desperate Ritual, Seething Song,
    // Irencrag Feat) instead of their full 2^K powerset (see GroupChoiceNonPrefixAccel). A self-funding
    // ritual chain is cheapest-first-optimal -- for any storm count j the cheapest j accelerants dominate
    // every other size-j subset (same storm count, >= mana) -- so the reachable (storm, mana) frontier is
    // preserved while the go-off hand's combinatorial straggler collapses to linear. This is a HEURISTIC (it
    // changes which action masks are enumerated -> NOT byte-identical), so it lives in the archetype provider
    // and is opened by MTG_UNPRUNED (UnprunedGate::AccelPrefix) for the standing pruned-vs-unpruned A/B,
    // exactly like EnumGroupCap / SituationalCardRank. Base returns false -> every non-Dragonstorm deck (and
    // Dragonstorm under MTG_UNPRUNED) stays byte-identical; only DragonstormProvider opts in. See
    // docs/design/dragonstorm-search-pruning.md (Step 2).
    virtual bool UseAccelPrefixCollapse() const { return false; }

    // FungibleSacSourceCap -- how many INTERCHANGEABLE sac-for-mana sources (Treasure tokens, a pile
    // of identical Lotus/Black Lotus) CollectActions enumerates. N identical untapped Treasures are
    // fungible: cracking any k of them produces the same mana, so k is the only real decision and the
    // WHICH is noise -- but each source emits one Action per candidate colour and each multi-colour
    // source becomes its own odometer GROUP, so the plan space carries a (1+variants)^N factor for a
    // choice with N+1 distinct outcomes. Measured on Mirrorwing (USER sighting 2026-08-16): a rollout
    // board reached 63 untapped Treasures -- 126 SacForMana actions, a 3^63 group product -- because
    // Zada copies Gold Rush once per creature and a rollout never spends the tokens, so they simply
    // ACCUMULATE. Capping at K keeps the first K of each fungible class; the rest stay on the
    // battlefield and are re-enumerated at the NEXT breakpoint once some have been used (USER: "when
    // they are used and we have another breakpoint at that point we can use the remainder"), so no
    // mana is unreachable -- it is deferred, in the same doctrine as the deferred group waves.
    //
    // NOT byte-identical only where a board holds MORE than K identical untapped sac sources; every
    // deck in the suite that runs one (Dragonstorm's Lotus Bloom, FiveColour's conjured Lotus) is far
    // below the cap, so they are unaffected. USER 2026-08-16: "only combo decks might want more and we
    // can figure that problem out when we get to it" -- hence a provider hook rather than a constant.
    // MTG_SAC_DUP_CAP=N overrides (0 = uncapped) for a one-binary A/B.
    //
    // THE NUMBER IS DERIVED FROM THE LIMIT, NOT PICKED (USER 2026-08-16: "I would just do it in the
    // context of the gate -- i.e. how many do we need to ignore"). 64 is the engine's standing
    // bitmask width: the tap failure memo and the payable-mana cache both key a 64-bit source set,
    // and a sac source is a potential mana contributor to the same payment. So the cap is the number
    // that keeps a fungible class inside that width -- ignore only the excess, never more. USER on
    // why 64 is the right ceiling: "64 mana is enough for everything except infinite mana win lines".
    // A combo deck that genuinely wants an unbounded pile overrides this hook ("only combo decks
    // might want more and we can figure that problem out when we get to it").
    //
    // Deliberately a CAP and not an auto-float of the excess (the other option the user raised): for
    // this deck the Treasures are not merely mana, they are the Gold Rush pump SCALER
    // (pump_per_treasure_power, "+2/+2 for each Treasure you control"), so cracking a Treasure the
    // line does not need actively shrinks every later pump. At the magnitudes involved this is moot
    // (20 Treasures is already lethal), but dropping a choice is unconditionally safe where taking
    // one is not.
    virtual int FungibleSacSourceCap() const { return 64; }

    // UseSpliceCollapse -- Desperate Ritual SPLICE-count collapse gate. When true, TurnSolver's
    // CollectActions emits only TWO splice variants per same-named splice_onto_arcane copy -- the BARE cast
    // (k=0) and the position's MAX-CHAIN cast (k = N-1-pos, N = copies in hand) -- instead of the full
    // k=0..N-1 fan-out, and the odometer keeps only PREFIX selections of one family (bare-prefix OR max-chain
    // prefix) via SpliceCollapseViolated. Splicing keeps the revealed copies in hand and adds no storm, so
    // splice-then-cast is net-positive mana at the SAME eventual storm count -- the maximal splice chain
    // {N-1,...,N-m} dominates every other size-m assignment on mana (user's directive: "if you can afford to
    // splice you should; only search the bare line as a fallback when the spliced line can't be cast").
    // Collapses the go-off hand's ~N^N splice-count powerset (the residual atom after the accel / Lotus
    // prefix collapses) to the 2 dominant families. HEURISTIC (it drops the intermediate splice assignments
    // -> NOT byte-identical), so it lives in the archetype provider and is opened by MTG_UNPRUNED
    // (UnprunedGate::SpliceCollapse) for the standing A/B, exactly like AccelPrefix. Base returns false ->
    // every non-Dragonstorm deck (and Dragonstorm under MTG_UNPRUNED) enumerates the full splice fan-out
    // unchanged; only DragonstormProvider opts in.
    virtual bool UseSpliceCollapse() const { return false; }

    // WantsCastOrderingSearch -- cast-ORDERING search gate. When true, EnumeratePlansWithLand expands each
    // action set into the DISTINCT orderings of its non-sacrifice hand casts (deduped by end-of-phase state)
    // and the search scores each, committing the best via Plan::searched_order (the executor replays that
    // vector order -> lockstep). The archetype opt-in for the global MTG_SEARCH_ORDER knob. Dragonstorm's
    // combo turns leave a lot on the table under the fixed CastOrderRank (rituals@15/Irencrag@18/payoff@20)
    // -- a specific interleave (fund -> reducer -> discounted rest, or a Dragon hard-cast between rituals)
    // routinely beats the canonical bucket -- so letting the search FIND the order recovers it. Measured
    // (regression): d3 5.56->4.95, d5 5.36->4.82 (~0.55 turns) at ~+47% makespan. EXPENSIVE (applies each
    // tried ordering on a copy, k! capped at 120), so it lives in the archetype provider; base returns false
    // -> every non-Dragonstorm deck stays byte-identical. Also openable globally via MTG_SEARCH_ORDER /
    // MTG_UNPRUNED (UnprunedGate::SearchOrder) for the standing A/B.
    virtual bool WantsCastOrderingSearch() const { return false; }

    // PrunesAcceleratorWithoutPayoff -- payoff-prune gate (the ritual-guard's search-side analog; the user's
    // spec). A mana ritual is a ONE-TURN accelerant: its float empties at end of turn (identical to Hinata's
    // Reality Spasm untap). So a plan that casts a ritual but no PAYOFF -- a Dragon (creature), Dragonstorm
    // (tutor_to_battlefield), or Apex of Power (impulse_exile) -- burns the ritual for nothing (the mana has
    // no same-turn sink; storm count doesn't carry across turns). When true, both Solve::consider (leaf) and
    // EnumeratePlans (search branch list) DROP those accelerant-only subsets, focusing the search budget on
    // payoff lines. Deliberately NOT enabled for Hinata: there the ritual IS a useful mid-combo accelerant
    // (it powers a bigger cantrip/dig turn), so the same prune measured -0.05 tempo (see
    // docs/design/hinata-spasm-gate-rootcause.md). Dragonstorm has no such mana sink, so the prune should
    // net-help here. Base returns false -> byte-identical; only DragonstormProvider opts in.
    virtual bool PrunesAcceleratorWithoutPayoff() const { return false; }

    // CastCheapestFirstWithinTier -- within one CastOrderRank tier, cast same-tier MANA ACCELERANTS
    // cheapest-first by the action's ACTUAL cost. A ritual chain funds itself cheapest-first, so a dearer
    // accelerant attempted before a cheaper one may be unpayable and is then SILENTLY DROPPED
    // (CastSpellFromHand returns void) -- stranding the mana it would have produced and leaving the payoff
    // short (Dragonstorm d0 seed 8585 gi1578: led with Seething Song, floated 7, could not pay Dragonstorm's
    // 9, whole chain burned). Default ON (root). An earlier measurement had this default OFF "because at the
    // ROOT it regressed slivers/Hinata/Anti-Lifegain/Knights and cost 2 searched slowdowns" -- that
    // measurement predates the IsManaRitual restriction in CastOrderLess and was really measuring the
    // tie-break reordering CREATURES (Scourge/Lathliss ETB order), which cost is the wrong key for.
    // Restricted to accelerants it is inert for every deck that never holds two of them at once, so it is a
    // generic rule, not archetype tuning. A provider may still override to false;
    // MTG_LEGACY_CAST_TIER_ORDER=1 is the global hatch for a byte-identical A/B.
    virtual bool CastCheapestFirstWithinTier() const { return true; }

    // Float-colour collapse for "add N mana of ANY ONE colour" effects (HEURISTIC, provider-owned; NOT
    // byte-identical). These effects emit one cast/sac variant PER candidate colour, and with several Lotus
    // Blooms + Apex in play the per-colour fan-out is the top enumeration driver (branch-stats: up to
    // ~590k plans in a single node). For a red-primary archetype the off-colours are almost never worth it:
    //   * ImpulseFloatColorRedOnly: Apex of Power always floats RED -- its mono-red chain never needs
    //     another colour, and one colour can't fund a multicolour card by itself.
    //   * RestrictSacColorsToHasteAndRed: Lotus Bloom floats RED unless a HASTE creature castable THIS turn
    //     (in hand, which includes Apex-staged exile cards) demands an off-colour -- the only time an
    //     immediate off-colour Dragon (Karrthus/Kolaghan) is worth the sacrifice. Most turns => RED only.
    // Both open to the full candidate set under MTG_UNPRUNED(SacColor). Base returns false -> the old
    // library-scoped, all-colour behaviour, byte-identical for every other deck.
    virtual bool ImpulseFloatColorRedOnly()       const { return false; }
    virtual bool RestrictSacColorsToHasteAndRed() const { return false; }

    // HoldsSacLandBurnUntilLethal -- opt IN to the sac-land burn hold (Shard Volley). A spell whose
    // additional cost is "sacrifice a land" spends a permanent mana source for a fixed lump of damage,
    // which in a goldfish is worth the same on any later turn -- so the enumerators drop plans that cast
    // one unless it wins this turn or enables Spectacle. See HoldSacLandBurn in TurnSolver.cpp for the
    // rule, the two exceptions and the measurement. Base false -> every other deck keeps the full
    // (unpruned) plan set; MTG_UNPRUNED / MTG_UNPRUNE=saclandhold reopens it for burn too.
    virtual bool HoldsSacLandBurnUntilLethal() const { return false; }

    // ScaledCastVariants -- SCALED-CAST variants: for a spell whose cost depends on how much output it
    // commits, the candidate (opponent-face damage, cost) levels to branch on. This is the DIVIDED-damage
    // analogue of XCandidates: Magma Opus deals 4 damage divided among any number of targets, and committing
    // more of it to the opponent's FACE leaves fewer distinct spread/tap targets for Hinata's per-target
    // discount -- so more face costs more mana, exactly like paying more for a bigger {X}. The engine emits
    // one mutually-exclusive cast per returned variant and the plan enumerator picks per affordability +
    // value, so with several scaling spells in hand (a Crackle {X} + a Magma face) the SEARCH allocates the
    // spare mana across them (Crackle's X moves in 3-mana steps, Magma's face in 1-mana steps, so the
    // fine-grained face soaks up mana the coarse X cannot). The archetype owns the WHOLE model -- which
    // levels, and what each costs; every card-specific number lives in the provider -- while the engine keeps
    // only the emit/thread/resolve MECHANISM (the committed face rides to resolution and is dealt to the
    // opponent). Base returns {} -> the spell's normal single-line cast (byte-identical for every other
    // deck/card); only an archetype with a scaling card returns variants, and only when its model is enabled.
    // The search still picks by default; a provider may narrow the set as a heuristic (like XCandidates
    // does).
    virtual std::vector<ScaledCastVariant>
    ScaledCastVariants(const GameState& /*s*/, const CardDefinition& /*def*/) const { return {}; }

    // ---- Ported engine built-ins (2026-08-01 audit) ------------------------------------------
    // Each of the hooks below replaces a rule that was hard-coded in the engine with no provider
    // involvement at all -- found by auditing the g_play_* human-play choosers, which mark exactly
    // the points where a human must be able to override an engine default. Every base
    // implementation reproduces the historical rule EXACTLY, so adding them is byte-identical; the
    // point is ownership first, review and branching second (see
    // docs/design/engine-heuristics-to-providers.md).

    // CombatCheatCandidates -- Goblin Lackey: "whenever this deals combat damage to a player, you MAY
    // put a <subtype> permanent card from your hand onto the battlefield." WHICH card (or decline) is
    // a real choice, and it is NOT a cast: it is free, and it resolves during the combat-damage step,
    // so the put permanent is summoning-sick and cannot attack this turn. That makes it the same
    // KIND of decision as an Aether Vial deploy -- "which permanent do I want on board going
    // forward" -- rather than "what is the biggest thing I can sneak in".
    //
    // Returns hand indices in PREFERENCE order; index 0 is what a non-branching caller takes, so
    // returning ONE index decides it with no branch. Empty == decline the "may".
    //
    // The base implementation is the engine's historical rule -- highest MV, ties by power, then by
    // card number -- so a provider that does not override behaves exactly as before. Highest-MV is a
    // reasonable default and is usually right; it is offered FIRST precisely so that a search over
    // this axis inherits it as the tie-break winner (defect class 3: strict improvement means the
    // first-enumerated option owns every tie).
    virtual std::vector<int> CombatCheatCandidates(
        const GameState& s, int controller, const CardDefinition& source,
        const std::vector<int>& hand_indices) const;

    // EtbDigCandidates -- ETB "look at the top N, you may reveal a <subtype> card and put it into your
    // hand" (Acclaimed Contender). Returns indices INTO `examined` in PREFERENCE order, restricted to
    // `legal` (the subtype-matching looks); index 0 is what a non-branching caller takes. Empty ==
    // take nothing (it is a "may").
    //
    // The base implementation is the historical rule: the FIRST legal match in look order. That rule
    // is poor and is retained only for byte-identity -- look order is library order, i.e. shuffle
    // order, so which Knight you take is effectively random among the matches. It is the same defect
    // as "first land in hand order" in the cleanup-discard rule: a ranking that is really insertion
    // order. Providers should override; see docs/design/engine-heuristics-to-providers.md.
    virtual std::vector<int> EtbDigCandidates(
        const GameState& s, int controller, const std::vector<Card>& examined,
        const std::vector<int>& legal) const;

    // ReplicateCounts -- Hatchery Sliver replicate: how many token copies to pay for on cast.
    // Returns candidate counts in PREFERENCE order; index 0 is what a non-branching caller takes. A
    // NEGATIVE entry means "as many as the pool affords" (greedy max), which is the default.
    //
    // Provider-owned because greedy-max is a PER-DECK fact, not an engine truth. It is right for
    // slivers -- a deck of one-drops and lords, where an extra body is almost always the best use of
    // the mana -- and that is the deck that has the card today. It need not hold for a deck whose
    // replicate target competes with an expensive payoff. Unlike firebreathing (whose pool is a
    // by-value copy, making greedy-max provably dominant), replicate taps REAL sources:
    // MTG_REPLICATE_TRACE measured 59 of 189 replicate events squeezing at least one hand card out of
    // affordability, so the pool is genuinely contended.
    virtual std::vector<int> ReplicateCounts(const GameState& /*s*/,
                                             const CardDefinition& /*def*/) const
    {
        return std::vector<int>{ -1 };
    }

    // ---- Remaining engine built-ins, ported byte-identically -----------------------------------
    // Every hook below returns candidates in PREFERENCE order with index 0 equal to the historical
    // inline pick, so a non-branching caller is unchanged. Several of these base rules are known to
    // be weak (see the individual notes and engine-heuristics-to-providers.md); they are preserved
    // as-is here because the port must not move play. Fixing them is the next step, per rule.

    // LightPawsAuraCandidates -- Light-Paws, Emperor's Voice: which Aura the on-cast trigger fetches
    // onto the battlefield attached to her. `legal` is library indices already filtered to the legal
    // fetches (MV <= the cast Aura's, a name you do not already control, enchant restriction
    // satisfied); return them ranked, or {} to decline (it is a "may search").
    //
    // Provider-owned because this is a TUTOR, and every other tutor in the engine already routes
    // through TutorCandidates -- this one was the exception. The base rule ranks by the power the
    // Aura would REALIZE if attached now (scaling Auras counted against the live board), which is a
    // genuinely good rule; it is deck-agnostic and stays the default.
    virtual std::vector<int> LightPawsAuraCandidates(
        const GameState& s, int controller, const Permanent& lightpaws,
        const std::vector<int>& legal) const;

    // RetraceDiscardCandidates -- retrace (Throes of Chaos) casts from the graveyard by discarding a
    // LAND as an additional cost. Which land, as hand indices in preference order.
    //
    // The base rule is the historical FIRST land in hand order -- i.e. insertion order, the same
    // arbitrary-ranking defect as rule 1 of the cleanup discard. Lands are not fungible; this wants
    // a real ranking (ManaSourceRank is already provider-owned and is the obvious input).
    virtual std::vector<int> RetraceDiscardCandidates(
        const GameState& /*s*/, int /*controller*/,
        const std::vector<int>& hand_land_indices) const { return hand_land_indices; }

    // SacrificeLandCandidates -- "as an additional cost to cast this spell, sacrifice a land"
    // (Shard Volley). Battlefield indices in preference order.
    //
    // Base rule: the first TAPPED land (already spent this turn, so sacrificing it costs no mana
    // now), else the first land. The tapped-first half is sound; the fallback is hand-order
    // arbitrary again.
    virtual std::vector<int> SacrificeLandCandidates(
        const GameState& s, int controller, const std::vector<int>& land_indices) const;

    // BounceLandCandidates -- a Karoo bounce land (Izzet Boilerworks) returns one of your lands to
    // hand as it enters. Battlefield indices in preference order; never empty (the bounce is
    // mandatory, and the Karoo itself is the forced fallback when it is the only land).
    //
    // Base rule, lexicographic: never bounce another Karoo (replaying it just triggers another
    // bounce), prefer an already-TAPPED land (spent this turn -> no mana lost), prefer one that
    // re-enters UNTAPPED. That is a well-reasoned rule; it is provider-owned for ownership, not
    // because it looks wrong.
    virtual std::vector<int> BounceLandCandidates(
        const GameState& s, int controller, int self_index,
        const std::vector<int>& legal) const;

    // LegendKeepIndex -- legend rule (CR 704.5j): you control two or more legendary permanents with
    // the same name, so you CHOOSE one to keep and the rest go to the graveyard. `duplicates` is the
    // battlefield indices of one such same-name group, ascending; return the index to KEEP.
    //
    // The base rule keeps the OLDEST (lowest battlefield index). The rules make this the
    // controller's choice for a reason: the copies are not interchangeable once one carries an aura,
    // counters, or damage, or once one is summoning-sick and another is not.
    //
    // MEASURED (MTG_TRACE=legend, 600 d0 games per deck) AND DELIBERATELY NOT BRANCHED. The keep is
    // near-inert FOR GOLDFISHING, and the reason is structural rather than a small effect size:
    //
    //   deck      events   what differs between the copies
    //   Hinata      106    91 summoning sickness ONLY, 10 tapped+sick
    //   goblins      19    Muxus: tapped + sick + temp pump
    //   knights       4    2 sickness, 2 nothing at all
    //   TH / burn     0    no legendary duplicates arise
    //
    // Exactly one copy survives, and the ETB is already banked before this state-based action runs,
    // so the choice is only ever "which BODY survives". For a body whose sole use is ATTACKING --
    // Hinata (static + flier), Haytham (ETB + lord), Muxus (its trigger is "enters or attacks") --
    // the only within-turn content is "can the survivor attack now", and keeping the OLDEST already
    // answers that correctly because the oldest is the un-sick one. Every other difference observed
    // (tapped, sickness, temp pump) is erased by the next untap step, after which the copies are
    // interchangeable. So a smarter rule has nothing left to win.
    //
    // WHAT WOULD MAKE IT LIVE (the user's criterion, and it is the right one): a legend with a use
    // OTHER than attacking, since only one copy can attack either way. Krenko, Mob Boss ({T}: create
    // X Goblins) is exactly that -- keeping an untapped, un-sick Krenko would be worth real tokens.
    // It duplicated ZERO times in 600 games. Revisit this hook if a deck runs a legend with a tap
    // ability or a sacrifice outlet that actually duplicates; until then the base rule is not a
    // placeholder, it is the answer.
    virtual int LegendKeepIndex(const GameState& /*s*/, int /*controller*/,
                                const std::vector<int>& duplicates) const
    {
        return duplicates.empty() ? -1 : duplicates.front();
    }

    // StorageLandHold -- a storage land (Mercadian Bazaar: "{T}: put a storage counter") offers a
    // real either/or every main phase: tap it for MANA NOW, or hold it untapped so it CHARGES for a
    // bigger burst later. `counters` is what it already holds.
    //
    // OWNERSHIP GAP THIS CLOSES: the choice existed only for a HUMAN (g_play_storage_hold_chooser).
    // Autonomously and in the rollout the chooser is null, so the engine silently took "never hold"
    // -- a hardcoded answer to a genuine decision that no provider could express or override. It
    // was invisible to the chooser-list inventory precisely BECAUSE it has a chooser: the audit
    // asked "does a chooser exist" (yes) rather than "what happens when it is absent" (a built-in
    // rule). Any decision whose autonomous path bypasses the chooser hides here.
    //
    // Base returns false = never hold = exactly the historical autonomous behaviour, so this is
    // byte-identical. Dwarven Hold ("upkeep_if_tapped") is NOT routed here -- its commitment is
    // pre-draw, in GameEngine::UpkeepStep, and is a separate decision.
    virtual bool StorageLandHold(const GameState& /*s*/, int /*controller*/,
                                 const CardDefinition& /*def*/, int /*counters*/) const
    { return false; }

    // OfferDuplicateLegendCast -- should the search even CONSIDER casting a legendary permanent
    // whose copy we already control? The legend rule (CR 704.5j) kills one immediately on
    // resolution, so for a card with no enter-triggered effect the cast buys literally nothing and
    // costs a card plus that turn's mana. A human never considers it.
    //
    // This is a PRUNE, which is the legitimate use of a heuristic here -- it removes a plan variant
    // that cannot be better, rather than picking between real ones. It matters because plan
    // variants share a FIXED rollout budget: a pointless variant dilutes every real one, the same
    // mechanism that made the rollout discard axis measure worse.
    //
    // WHY IT WAS NOT ALREADY HAPPENING: the legend rule is modelled correctly and immediately in
    // both paths, so the duplicate dies and board value is unchanged -- which makes the cast a TIE,
    // not a loss, and a tie-break casts it. MTG_TRACE=legend found 106 real duplicate Hinatas in
    // 600 games.
    //
    // THE WHITELIST IS DELIBERATELY POSITIVE. Only templates that are provably enter-inert are
    // pruned (VanillaCreature, LordEffect: a body plus a continuous effect, nothing on entry). Any
    // other card -- notably every `custom` one -- is OFFERED. Enumerating etb_* params to prove
    // inertness instead would fail the wrong way: miss one field and a genuinely good cast is
    // pruned. Muxus (etb_reveal_count: a second copy really does fire the reveal) is `custom` and
    // so is never pruned, which is the behaviour that must not regress.
    virtual bool OfferDuplicateLegendCast(const GameState& s, int controller,
                                          const CardDefinition& def) const;

    // LandEntersUntapped -- a land that offers "pay a cost, or enter tapped": a shock land
    // (etb_pay_life_to_untap) or a reveal land (etb_untap_reveal_subtypes). `heuristic` carries the
    // engine's answer (shock: pay whenever mana is needed this turn and life allows; reveal: reveal
    // whenever able). Return true to enter untapped.
    //
    // Base returns `heuristic` unchanged, so this is byte-identical and applies uniformly to the
    // enumeration predicate as well as the real land drop -- the two MUST agree or the plan's mana
    // and the realised mana diverge.
    virtual bool LandEntersUntapped(const GameState& /*s*/, const CardDefinition& /*def*/,
                                    bool heuristic) const { return heuristic; }

    // BurnCreatureTargetCandidates -- a creature-targeting burn that carries a "when that creature
    // dies" rider (Searing Blood: 2 to a creature, then 3 to its controller if it dies). Opponent
    // battlefield indices in preference order.
    //
    // Base rule: prefer a creature this spell KILLS (EffectiveToughness <= damage) so the rider
    // fires, else the first opponent creature. Sound for a goldfish, where which creature dies is
    // irrelevant beyond enabling the rider -- revisit against a real opponent.
    virtual std::vector<int> BurnCreatureTargetCandidates(
        const GameState& s, int active, int damage,
        const std::vector<int>& opp_creatures) const;

    // LifegainRemovalCandidates -- controller-lifegain removal (Swords to Plowshares). Its rider
    // makes the EXILED creature's controller gain life equal to its power, which a Tainted Remedy /
    // Plague Drone turns into that much life LOSS -- so the base rule targets the opponent's
    // LARGEST-power creature.
    //
    // NOTE: the WHETHER-to-cast gate ("only while a Remedy enabler is in play") deliberately stays in
    // the engine helper, not here: it is a castability precondition shared in lockstep by the
    // enumeration gate, the rollout, and the executor, and moving it would let a provider desync
    // them. This hook owns only WHICH creature.
    virtual std::vector<int> LifegainRemovalCandidates(
        const GameState& s, int active, const std::vector<int>& opp_creatures) const;

    // OwnPumpTargetCandidates -- a spell that targets YOUR OWN creature (a pump / prowess enabler):
    // which one. Own battlefield indices in preference order. Base rule: highest effective power
    // among creatures that can attack.
    virtual std::vector<int> OwnPumpTargetCandidates(
        const GameState& s, int controller, const std::vector<int>& own_attackers) const;
};

// ---------------------------------------------------------------------------------------------------
// Payoff / ETB-value ordering primitive (shared, deck-agnostic). Given a multiset of card NAMES that
// will enter the battlefield together -- a mass-ETB put list (Dragonstorm) or a within-turn sequence
// of entering permanents -- return them reordered so an "on-other-ETB" trigger sees the MOST later
// entries: beneficial on-other-ETB sources lead, harmful ones trail. STABLE within each band, so the
// caller's own order among order-independent picks is preserved. Param-driven (CardParams), so a NEW
// deck gets correct payoff-ordering just by handing its selected set here, in any order. Names may
// repeat (multiplicity honoured). This is the value/kind-3 ordering; the LEGALITY kind (Daybreak
// Coronet "another aura", topological/K=1) is a SEPARATE mechanism in TurnSolver (aura sequencing).
// See DecisionProviders.cpp for the band rules + the harmful-trigger extension point, and
// docs/design/sequential-plan-evaluation.md.
std::vector<std::string> OrderEntriesByEtbValue(std::vector<std::string> names);
