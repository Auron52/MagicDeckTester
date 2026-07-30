#pragma once
// THE single-attempt mana payment (backlog C1, unification step 2). The executor
// (AIEngine::TapForCostOnce) and the rollout (TurnSolver's TapForCostDirectOnce) were twin
// ~380-line copies of this function kept in sync by comment discipline -- and every historical
// divergence between them was a real bug (coloured-pip EffectiveProduces, Karoo two-colour
// credit, storage burst; see docs/design/rollout-executor-lockstep.md). Both now delegate here.
//
// What actually differed between the twins, now parameters:
//   available        - the executor's turn-scoped accounting pool, decremented as sources tap;
//                      nullptr for the rollout, which keeps no such pool. NOTE: accounting is
//                      deliberately NOT rolled back on a failed attempt (preserved executor
//                      behaviour -- the TapForCost wrapper snapshots/restores it around the
//                      reserved attempt instead).
//   honor_legacy_cco - the rollout passes true so the MTG_LEGACY_CCO_PAY measurement hatch can
//                      re-enable the old (rules-violating) EffectiveProduces payment in the
//                      scarcity path; the executor passes false (it never had that hatch).
//
// One former divergence was resolved rather than parameterized: in the legacy (MTG_TAP_LEGACY)
// step-1 path the executor still read EffectiveProduces where the rollout read the payment-legal
// ProducesForPayment -- the executor was the unfixed twin of the 6bb2791 coloured-pip fix,
// reachable only under that opt-in hatch. Unified on ProducesForPayment (default config
// byte-identical: the path never runs with MTG_TAP_LEGACY unset).
#include "../core/GameState.h"
#include "TurnSolver.h"   // Action
#include <cstdint>
#include <string>

bool TapForCostSharedOnce(GameState& state, const ManaCost& cost_in, bool for_creature,
                          std::uint64_t reserved_mask, ManaPool* available,
                          bool honor_legacy_cco);

// THE effective spell cost (C1 unit 2): spectacle, splice-onto-Arcane combining, affinity,
// Medallion-style colour reduction, and Hinata's per-target discount (fixed-cost spells only --
// {X} spells apply the discount where the chosen X is known). Was a byte-identical twin pair
// (AIEngine::EffectiveCost / TurnSolver's file-static EffectiveCost); both now delegate here.
ManaCost EffectiveSpellCost(const CardDefinition& def, const GameState& state, int copies = 1);

// THE cast-order comparator and its opaque-set guard (C1 unit 3): provider RANK first, then
// cheapest-first among mana accelerants by the action's ACTUAL cost; the reorder is skipped
// entirely for sets containing a mid-turn re-solve breakpoint (OrderingOpaque -- the search owns
// that ordering). Was a byte-identical twin pair (TurnSolver statics / AIEngine's CastOrderLessAI
// and OrderingOpaqueAI kept in lockstep by comment); both now share these definitions.
bool CastOrderLess(const GameState& state, const Action& a, const Action& b);
bool OrderingOpaque(const std::string& name);

// THE accounting mana pool (C1 unit 4): everything the active player's untapped sources could
// produce this phase, plus the turn-scoped floating reserve. Was a byte-identical twin pair
// (TurnSolver's file-static BuildPool / AIEngine::BuildAvailableMana); both sides now call this.
ManaPool AvailableManaPool(const GameState& state);
