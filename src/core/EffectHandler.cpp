#include "EffectHandler.h"
#include "SpellEffects.h"
#include "RolloutTouch.h"
#include <algorithm>

// Execution-trace instrumentation sink (off by default -> byte-identical). See RolloutTouch.h.
namespace rollout_touch { thread_local Sink* g_sink = nullptr; }

// ---- Helpers ----

void EffectHandler::EnterBattlefield(GameState& state, const StackEntry& entry,
                                      const CardDefinition& def)
{
    rollout_touch::Record(entry.source.m_name.str());   // execution-trace: permanent's code runs on the battlefield
    Permanent perm;
    perm.card              = def.card;
    perm.card.m_number     = entry.source.m_number;  // preserve per-copy ID from cast
    perm.controller_index  = entry.controller_index;
    perm.owner_index       = entry.controller_index;
    perm.entered_this_turn = true;
    state.battlefield.push_back(perm);

    // Dragonstorm kill-engine (executor side): a Dragon entering fires the shared cascade --
    // Scourge's ETB ping + Lathliss's 5/5 token. No-op for every non-Dragon permanent (early
    // subtype return) so all other decks are byte-identical. Mirrors the rollout's creature-enter
    // site in TurnSolver::ApplyPlanDirect (lockstep).
    OnDragonEnters(state, entry.controller_index, static_cast<int>(state.battlefield.size()) - 1);
    // Goblins tribal ETB cascade (self-tokens / ETB burn / Matron tutor / Muxus reveal). No-op for
    // every non-Goblin permanent (early param return). entry.tutor_target carries a search/human
    // Goblin Matron fetch target (empty -> the provider's pick).
    OnGoblinEnters(state, entry.controller_index, static_cast<int>(state.battlefield.size()) - 1,
                   entry.tutor_target);
}

void EffectHandler::MoveToGraveyard(GameState& state, const StackEntry& entry)
{
    state.players[entry.controller_index].graveyard.push_back(entry.source);
}

// ---- Public dispatch ----

bool EffectHandler::Resolve(GameState& state, const StackEntry& entry, const CardDefinition& def)
{
    const bool ok = ResolveImpl(state, entry, def);
    // Legend rule (CR 704.5j) is a STATE-BASED ACTION: it is checked the moment the duplicate is on
    // the battlefield, NOT deferred. The executor used to enforce it only for Vial-deployed
    // creatures (AIEngine) and at the start of combat (GameEngine::CombatPhase), so a legendary CAST
    // from hand sat beside its twin for the whole main phase -- and anything that counts permanents
    // or triggers off them saw two. The rollout (TurnSolver::ApplyPlanDirect) always enforced it on
    // entry, so this was a rollout/executor DIVERGENCE, not just a rules gap.
    //
    // The pre-fix window ran from the duplicate ENTERING to the next begin-combat step -- those were
    // the only two executor sweep sites. For a duplicate entering in the second main that spans the
    // opponent's turn AND the whole of the next main phase. It could never ATTACK (the sweep precedes
    // DeclareAttackers), which is the one outcome the old placement did prevent; everything else --
    // static abilities, on-enter and on-cast triggers, anything counting permanents -- saw two.
    //
    // Measured on Auras seed 4227 gi223 (per-phase battlefield, pre-fix binary): a second
    // Light-Paws, Emperor's Voice cast on T3 left BOTH copies (#38, #39) on the board for the rest
    // of that main phase, and combat swept it back to one. Spirit Link therefore resolved with two
    // Light-Paws out and its Aura-tutor trigger fired TWICE, fetching an extra Aura and shuffling
    // the library an extra time; the realised draws then stopped matching the line the search had
    // proved (predicted win T4, realised T5 -- now T4). The same window let two Hinata,
    // Dawn-Crowned double-count a static cost discount, so the executor cast a Reality Spasm it
    // could not afford.
    //
    // NOTE if a legend-rule-off effect is ever added (Mirror Gallery), or a name-changing legend
    // (Sakashima): no such card exists in cards.json today, so enforcement is unconditional here.
    // Adding one means gating EVERY EnforceLegendRule site, not just this one.
    //
    // Placed AFTER the dispatch so it runs after the permanent has entered and its ETB effects have
    // resolved (dig, Dragon cascade), which is exactly the rollout's ordering -- and after, not
    // before, so an ETB dig's `battlefield.back()` self-pointer is never invalidated underneath it.
    // No-op for every deck with no legendary permanent.
    if (def.card.HasSupertype(Supertype::Legendary))
    {
        EnforceLegendRule(state, entry.controller_index);
    }
    return ok;
}

bool EffectHandler::ResolveImpl(GameState& state, const StackEntry& entry, const CardDefinition& def)
{
    // Execution-trace: this card's effect is about to run -> record it as touched (no-op when off).
    rollout_touch::Record(entry.source.m_name.str());
    switch (def.tmpl)
    {
        case CardTemplate::BasicLand:
        {
            // Lands do not use the stack (CR 305.1) — should not appear here.
            // If they do, enter the battlefield gracefully.
            EnterBattlefield(state, entry, def);
            return true;
        }

        case CardTemplate::VanillaCreature:
        {
            ResolveVanillaCreature(state, entry, def);
            return true;
        }

        case CardTemplate::ManaDork:
        {
            ResolveManaDork(state, entry, def);
            return true;
        }

        case CardTemplate::DirectDamage:
        {
            ResolveDirectDamage(state, entry, def);
            return true;
        }

        case CardTemplate::CounterSpell:
        {
            ResolveCounterSpell(state, entry, def);
            return true;
        }

        case CardTemplate::Removal:
        {
            ResolveRemoval(state, entry, def);
            return true;
        }

        case CardTemplate::DrawSpell:
        {
            ResolveDrawSpell(state, entry, def);
            return true;
        }

        case CardTemplate::DrawX:
        {
            ResolveDrawX(state, entry, def);
            return true;
        }

        case CardTemplate::PumpSpell:
        {
            ResolvePumpSpell(state, entry, def);
            return true;
        }

        case CardTemplate::LordEffect:
        {
            // Lord effects are static, not triggered — handled by the layer system.
            // The permanent still enters the battlefield.
            EnterBattlefield(state, entry, def);
            return true;
        }

        case CardTemplate::Haste:
        {
            EnterBattlefield(state, entry, def);
            return true;
        }

        case CardTemplate::DrawUntilNonland:
        {
            ResolveDrawUntilNonland(state, entry, def);
            return true;
        }

        default:
        {
            // Unknown or Tier 3 custom card — no built-in effect.
            // Move non-permanents to graveyard; permanents enter the battlefield.
            if (def.card.IsCreature() || def.card.HasType(CardType::Enchantment)
                || def.card.HasType(CardType::Artifact) || def.card.IsLand())
            {
                EnterBattlefield(state, entry, def);
                // Aura (Bogles): attach to the searched creature (enchant_target), then fire
                // Light-Paws' tutor-attach. Set aura_attached_to on the JUST-entered permanent
                // (battlefield.back()) BEFORE PerformLightPawsAttach push_backs the fetched aura.
                // Lockstep with the rollout's apply_one enchantment-enter branch.
                if (def.params.is_aura && !state.battlefield.empty())
                {
                    state.battlefield.back().aura_attached_to =
                        ResolveEnchantTarget(state, entry.controller_index, entry.enchant_target);
                    PerformLightPawsAttach(state, entry.controller_index,
                                           def.card.m_mana_cost.ManaValue(), "EXEC");
                }
                // ETB "each opponent gains N life" (Aria of Flame) -> reversed to damage by
                // a Tainted Remedy / Plague Drone via OpponentGainsLife.
                if (def.params.etb_opponent_lifegain > 0)
                {
                    OpponentGainsLife(state, entry.controller_index,
                                      def.params.etb_opponent_lifegain);
                }
            }
            else
            {
                // Non-permanent custom spell: tutor / destroy-all-enchantments / cascade,
                // then graveyard.
                if (def.params.tutor_to_hand || def.params.tutor_to_top)
                {
                    // Fetch the searched target carried on the stack entry (empty -> the
                    // heuristic's top pick), matching the rollout's ApplyPlanDirect.
                    PerformTutor(state, entry.controller_index, def.params, entry.tutor_target,
                                 def.card.m_name);
                }
                // Dragonstorm (Storm): put min(spells_cast_this_turn, Dragons-in-library) Dragons
                // onto the battlefield, each through the shared OnDragonEnters cascade (Scourge ping
                // + Lathliss token), then shuffle. spells_cast_this_turn was incremented at THIS
                // spell's cast (CastSpellFromHand, before it went on the stack), so it already counts
                // Dragonstorm itself -> it equals (prior spells cast this turn) + 1 = storm copies +
                // the original = the number of Dragons to put. Lockstep with TurnSolver::apply_one.
                if (def.params.tutor_to_battlefield)
                {
                    // preferred = {} (empty): put in the provider's TutorCandidates order. Both this
                    // executor and the rollout (apply_one) pass empty, so they put the identical
                    // Dragons in the identical order (lockstep). The DragonstormProvider / viewer
                    // multi-pick (later steps) supply an explicit order via the `preferred` arg.
                    PerformTutorToBattlefield(state, entry.controller_index, def.params,
                                              state.spells_cast_this_turn, /*preferred=*/{},
                                              def.card.m_name.str());
                }
                // Apex of Power: "Exile the top seven cards of your library. Until end of turn, you may
                // cast spells from among them. If this spell was cast from your hand, add ten mana of any
                // one color." Exile impulse_exile cards as STAGED cards playable THIS turn (m_impulse_no_land
                // so their lands are non-playable); then, IFF cast from hand (not off another Apex's exile),
                // float impulse_float_amount of the searched colour. Staged into Player::staged_cards so the
                // AIEngine draw-breakpoint merges them into hand and re-solves to cast them (mirrors the
                // stages_cards DrawSpell). LOCKSTEP with TurnSolver::apply_one's impulse branch.
                if (def.params.impulse_exile > 0)
                {
                    Player& imp = state.players[entry.controller_index];
                    const int expiry = def.params.impulse_expiry_this_turn
                                     ? state.turn_number : state.turn_number + 1;
                    std::vector<Card> exiled = DrawTopAsImpulseStaged(
                        state, entry.controller_index, def.params.impulse_exile, expiry);
                    for (Card& ec : exiled)
                    {
                        StagedCard sc;
                        sc.card        = std::move(ec);
                        sc.expiry_turn = expiry;
                        imp.staged_cards.push_back(std::move(sc));
                    }
                    // "add ten mana of any one color" -- cast-from-hand only (withheld for Apex-off-Apex).
                    if (entry.cast_from_hand && def.params.impulse_float_amount > 0)
                    {
                        AddChosenColorFloat(state, entry.chosen_float_color,
                                            def.params.impulse_float_amount);
                    }
                }
                if (def.params.destroy_all_enchantments)
                {
                    DestroyAllEnchantments(state);
                }
                if (def.params.cascade_max_mv > 0)
                {
                    ResolveCascade(state, entry, def);
                }
                // Reality Spasm / Irencrag Feat -- mana RITUAL. On resolution, add its floating
                // mana to the turn-scoped reserve so a later same-turn cast (Crackle) can spend
                // it (the Hinata combo). Modelled as floating, NOT a literal untap, so this stays
                // EXACTLY in lockstep with the planner's ritual credit and the rollout's apply_one.
                if (IsManaRitual(def))
                {
                    // Desperate Ritual SPLICE: float (splice_count+1)*{R}{R}{R} (each spliced copy adds
                    // its own {R}{R}{R}; the copies themselves stay in hand). splice_count unset -> +1
                    // copy (a plain ritual) -> byte-identical for every non-splice deck.
                    ApplyRitualFloat(state, def, entry.chosen_x.value_or(0),
                                     entry.splice_count.value_or(0) + 1);
                }
                MoveToGraveyard(state, entry);
            }
            return true;
        }
    }
}

// ---- Per-template resolvers ----

void EffectHandler::ResolveVanillaCreature(GameState& state, const StackEntry& entry,
                                            const CardDefinition& def)
{
    EnterBattlefield(state, entry, def);

    // ETB library dig (Acclaimed Contender): deterministic, identical to the rollout's
    // ApplyPlanDirect dig, so the realised hand/library matches the searched line. The
    // just-entered creature is the last on the battlefield; pass it as `self` so the
    // "control another Knight" condition excludes it.
    if (def.params.etb_dig_count > 0 && !state.battlefield.empty())
    {
        PerformEtbDig(state, entry.controller_index, def.params, &state.battlefield.back());
    }
}

void EffectHandler::ResolveManaDork(GameState& state, const StackEntry& entry,
                                     const CardDefinition& def)
{
    EnterBattlefield(state, entry, def);
    // Tap ability is handled by AIEngine::TapForCost — no ETB effect needed here.
}

void EffectHandler::ResolveDirectDamage(GameState& state, const StackEntry& entry,
                                         const CardDefinition& def)
{
    // An {X} burn deals its chosen X (carried on the stack entry; CR 202.3). A fixed-damage
    // burn uses params.damage, with the landfall boost if a land entered this turn.
    int damage;
    if (def.card.m_mana_cost.has_x)
    {
        // Crackle with Power: "deals five times X damage" -> chosen_x * x_damage_multiplier.
        int mult = def.params.x_damage_multiplier; if (mult < 1) { mult = 1; }
        damage = entry.chosen_x.value_or(0) * mult;
    }
    else
    {
        damage = def.params.damage;
        if (def.params.landfall_damage > 0
            && state.players[entry.controller_index].lands_played_this_turn > 0)
        {
            damage = def.params.landfall_damage;
        }
        // Soulfire Eruption: bounded multi-target dig. Exile + STAGE the top N cards (opponent +
        // opp creatures + self-if-safe), the face takes the highest MV, the controller the lowest.
        // Mirrors ApplyPlanDirect (lockstep). `damage` = face damage, applied to the opp by the
        // target loop below; self-damage is applied here.
        if (def.params.damage_equals_top_mv)
        {
            SoulfireResult sr = SoulfireDig(state, entry.controller_index,
                                            entry.soulfire_own_targets.value_or(0), &def, "exec");
            damage = sr.face_damage;
            state.players[entry.controller_index].life -= sr.self_damage;
        }
    }

    // A scaled divided-damage spell (Magma Opus) commits only the searched face level to the opponent's
    // face (carried on the stack via crackle_targets; the rest was spread onto inert targets to earn the
    // Hinata discount and is not simulated). Unset for a normal cast / the model off -> full damage.
    // Lockstep with the committed cost + ApplyPlanDirect.
    if (def.params.damage_divided && entry.crackle_targets.has_value())
    {
        damage = entry.crackle_targets.value();
    }

    for (const Target& t : entry.targets)
    {
        if (t.type == Target::Type::Player)
        {
            state.players[t.player_index].life -= damage;
            if (t.player_index != entry.controller_index && damage > 0)
            {
                state.opponent_lost_life_this_turn = true;
            }
        }
        else if (t.type == Target::Type::Permanent)
        {
            if (t.permanent_index >= 0
                && t.permanent_index < static_cast<int>(state.battlefield.size()))
            {
                Permanent& target = state.battlefield[t.permanent_index];
                const int before = target.damage;
                target.damage += damage;

                // Delayed "when that creature dies" trigger (Searing Blood), accumulated so two copies
                // on one creature both fire when it dies. See ApplyBurnToCreature (shared lockstep).
                ApplyBurnToCreature(state, target, before, def.params.death_trigger_damage,
                                    entry.controller_index);
            }
        }
    }

    // Crackle with Power: the declared extra targets (creatures/self) take 5X and die (SBA), so a
    // killed creature leaves the target pool for later spells. The opponent FACE damage was applied
    // by the target loop above (Targeting::Any sets only the opp-face target); this adds the extras.
    // Lockstep with ApplyPlanDirect (the rollout resolves the identical set from crackle_targets).
    if (IsCrackleCountSpell(def.params))
    {
        CrackleHitExtraTargets(state, entry.controller_index, damage, entry.crackle_targets.value_or(-1));
    }

    // Rider "target opponent gains N life" (Fiery Justice) -> reversed to damage by a
    // Tainted Remedy / Plague Drone via OpponentGainsLife.
    if (def.params.opponent_lifegain > 0)
    {
        OpponentGainsLife(state, entry.controller_index, def.params.opponent_lifegain);
    }

    // Magma Opus rider: "draw two cards." Drawn to hand on resolution; mirrors apply_one (lockstep).
    if (def.params.cast_draw > 0)
    {
        Player& cp = state.players[entry.controller_index];
        cp.library.DrawN(def.params.cast_draw, cp.hand);
    }

    MoveToGraveyard(state, entry);
}

void EffectHandler::ResolveCounterSpell(GameState& state, const StackEntry& entry,
                                         const CardDefinition& def)
{
    // In Phase 1 goldfishing the stack only ever contains the active player's
    // own spells, so CounterSpell has no target to counter.
    // TODO: Phase 2 — counter the top non-counter spell on the stack.
    (void)def;
    MoveToGraveyard(state, entry);
}

void EffectHandler::ResolveRemoval(GameState& state, const StackEntry& entry,
                                    const CardDefinition& def)
{
    for (const Target& t : entry.targets)
    {
        if (t.type == Target::Type::Permanent
            && t.permanent_index >= 0
            && t.permanent_index < static_cast<int>(state.battlefield.size()))
        {
            // Pump-then-Swords: redirect a free-alt Invigorate onto this creature before
            // capturing its power, so the exile life-loss is +power_bonus larger (autonomous AI
            // only). Shared with the rollout's Removal branch (TurnSolver) for lockstep. Called
            // before taking the `target` reference: FireOnCastTriggers may reallocate the
            // battlefield vector (appending tokens), which would dangle a held reference.
            TryPumpThenSwordsRedirect(state, entry.controller_index, t.permanent_index, def);
            Permanent& target = state.battlefield[t.permanent_index];
            // Rider (Swords to Plowshares): the exiled creature's controller gains life equal
            // to its power. Via OpponentGainsLife so a Tainted Remedy turns the opponent's
            // gain into damage equal to the creature's power. Captured before the erase.
            int tgt_controller = target.controller_index;
            int tgt_power      = target.EffectivePower();
            if (def.params.damage > 0)  // exile
            {
                state.exile.push_back(target.card);
            }
            else
            {
                state.players[target.controller_index].graveyard.push_back(target.card);
            }
            state.battlefield.erase(state.battlefield.begin() + t.permanent_index);

            // The exiled creature's controller gains `power` life. OpponentGainsLife(X) makes
            // (1-X) gain, so pass (1 - tgt_controller). When tgt_controller is the opponent
            // (the usual goldfish case) this routes through OUR Tainted Remedy, turning the
            // opponent's gain into `power` damage; on our own creature it just gives us life.
            if (def.params.controller_lifegain_equals_power && tgt_power > 0)
            {
                OpponentGainsLife(state, 1 - tgt_controller, tgt_power);
            }
        }
    }
    MoveToGraveyard(state, entry);
}

void EffectHandler::ResolveDrawSpell(GameState& state, const StackEntry& entry,
                                      const CardDefinition& def)
{
    Player& controller = state.players[entry.controller_index];
    int n = def.params.draw;

    // Expressive Iteration: look 3 -> 1 hand / 1 exiled-staged-this-turn / 1 bottom (its own model,
    // not the normal draw/scry path). Mirrors the rollout's apply_one DrawSpell branch (lockstep).
    if (def.params.expressive_iteration)
    {
        ResolveExpressiveIteration(state);
        MoveToGraveyard(state, entry);
        return;
    }

    // Scry-then-draw (Preordain): bottom the unwanted, reorder the rest, before drawing.
    if (def.params.cast_scry > 0) { ScryTop(state, def.params.cast_scry, def.card.m_name); }
    // Reorder-or-shuffle-then-draw (Ponder): keep all on top in best order, or shuffle away.
    if (def.params.cast_reorder > 0) { ReorderTopOrShuffle(state, def.params.cast_reorder, def.card.m_name, entry.ponder_keep.value_or(-1)); }

    if (def.params.stages_cards)
    {
        // Cards go to a staged exile zone; playable until end of the player's next turn.
        int expiry = state.turn_number + 1;
        for (int i = 0; i < n && !controller.library.empty(); ++i)
        {
            StagedCard sc;
            sc.card        = controller.library.DrawTop();
            sc.expiry_turn = expiry;
            controller.staged_cards.push_back(sc);
        }
    }
    else
    {
        // Deck-out: if the library cannot supply all n, draw what is left and the drawing player
        // loses (CR 104.3c, drawing from an empty library). Routed through the same loss flag the
        // draw step uses, so PlayOut ends the game. Reachable on this combo deck once a deep
        // Soulfire/cantrip dig has emptied the library; harmless for decks that never deck out.
        int drew = controller.library.DrawN(n, controller.hand);
        if (drew < n) { state.player_lost_on_draw = true; }
    }

    MoveToGraveyard(state, entry);
}

void EffectHandler::ResolveDrawX(GameState& state, const StackEntry& entry,
                                   const CardDefinition& def)
{
    (void)def;
    Player& controller = state.players[entry.controller_index];
    int n = entry.chosen_x.value_or(0);
    if (n > 0)
    {
        controller.library.DrawN(n, controller.hand);
    }
    MoveToGraveyard(state, entry);
}

void EffectHandler::ResolvePumpSpell(GameState& state, const StackEntry& entry,
                                      const CardDefinition& def)
{
    for (const Target& t : entry.targets)
    {
        if (t.type == Target::Type::Permanent
            && t.permanent_index >= 0
            && t.permanent_index < static_cast<int>(state.battlefield.size()))
        {
            Permanent& target = state.battlefield[t.permanent_index];
            // "+N/+M until end of turn" (Invigorate): temp bonuses, reset each cleanup step.
            target.temp_power_bonus += def.params.power_bonus;
            target.temp_tough_bonus += def.params.tough_bonus;
        }
    }
    MoveToGraveyard(state, entry);
}

void EffectHandler::ResolveDrawUntilNonland(GameState& state, const StackEntry& entry,
                                             const CardDefinition& def)
{
    Player& controller = state.players[entry.controller_index];
    // Reveal cards from the top until a nonland is found; put ALL revealed cards
    // (including the triggering nonland) into hand (CR oracle: "put all cards
    // revealed this way into your hand").
    // Capture the revealed -> hand cards for the replay viewer (real play only; the reveal
    // logger is paused during search/rollout, so this never fires off the real game's resolution).
    const bool capture = RevealVisible();   // log AND/or viewer (--claude-play attaches no GameLogger)
    std::vector<int>         revealed_nums;
    std::vector<std::string> revealed_names;
    while (!controller.library.empty())
    {
        Card c = controller.library.DrawTop();
        bool is_land = false;
        {
            auto cdef = CardDatabase::Instance().LookupCached(c);
            is_land = cdef ? cdef->card.IsLand() : c.IsLand();
        }
        if (capture) { revealed_nums.push_back(c.m_number); revealed_names.push_back(c.m_name); }
        controller.hand.push_back(std::move(c));
        if (!is_land) { break; }
    }
    if (capture && !revealed_nums.empty())
    {
        // All revealed cards go to hand, so "kept" = every revealed card (the viewer labels a
        // Treasure-Hunt source as "to hand").
        EmitReveal(state.turn_number, def.card.m_name, revealed_nums, revealed_names,
                                   revealed_nums, /*bottomed*/ std::vector<int>{});
    }
    MoveToGraveyard(state, entry);
}

void EffectHandler::ResolveCascade(GameState& state, const StackEntry& entry,
                                    const CardDefinition& def)
{
    Player& controller = state.players[entry.controller_index];
    int cascade_limit  = def.params.cascade_max_mv;

    // Exile cards from top until a nonland with MV < cascade_limit is found.
    std::vector<Card> exiled;
    int cascade_idx = -1;
    while (!controller.library.empty())
    {
        Card c = controller.library.DrawTop();
        auto cdef = CardDatabase::Instance().LookupCached(c);
        bool is_land = cdef ? cdef->card.IsLand() : c.IsLand();
        int  mv      = cdef ? cdef->card.m_mana_cost.ManaValue()
                            : c.m_mana_cost.ManaValue();
        exiled.push_back(std::move(c));
        if (!is_land && mv < cascade_limit)
        {
            cascade_idx = static_cast<int>(exiled.size()) - 1;
            break;
        }
    }

    // Push the cascade target onto the stack so it resolves before the original spell's
    // graveyard placement (the stack is LIFO; next iteration of ResolveStack pops it).
    if (cascade_idx >= 0)
    {
        const Card& cascade_card = exiled[cascade_idx];
        auto cdef = CardDatabase::Instance().LookupCached(cascade_card);
        if (cdef)
        {
            StackEntry ce;
            ce.type             = StackEntry::EntryType::Spell;
            ce.source           = cdef->card;
            ce.source.m_number  = cascade_card.m_number;
            ce.controller_index = entry.controller_index;
            state.stack.push_back(std::move(ce));
        }
    }

    // Remaining exiled cards go to the bottom of the library in the order exiled.
    for (int i = 0; i < static_cast<int>(exiled.size()); ++i)
    {
        if (i == cascade_idx) { continue; }
        controller.library.push_back(std::move(exiled[i]));
    }
}
