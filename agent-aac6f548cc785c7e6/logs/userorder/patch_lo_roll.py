p = '/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6/src/ai/TurnSolver.cpp'
s = open(p).read()

a1 = "    bool bp_truncate = false;\n"
assert s.count(a1) == 1
s = s.replace(a1, a1 + '''    // BREAKPOINT LEFTOVERS (DecisionProvider::MergesBreakpointLeftovers): the remaining casts of the list
    // whose cast is about to open an impulse-exile breakpoint, handed to that branch of apply_one (taken
    // at its entry). When it merges them into the continuation it sets bp_truncate, so the rest of the
    // list no-ops. Lockstep twin: AIEngine::TakeTurn's rdb_leftovers / bp_trunc_exec.
    std::vector<Action> lo_pending;
    bool lo_pending_set = false;
''')

a2 = '''            if (!s_human_play)
            {
                if (out_breakpoint && my_bp_sink) { bp_sink_push(my_bp_sink); }
                TurnSolver::Plan extra;
                bp_searched_plan(2, extra);   // resolves to the plan's continuation or EMPTY
                ContPinFrame _cpf(extra, my_bp_sink);   // the continuation's own during-cast pins (ContPins)
                bp_play_searched_land(extra, my_bp_sink);
                // Lotus Bloom: the staged Dragonstorm/rituals grew this pre-pass first (the
                // executor's breakpoint replay had the same gap) -- now the shared loop.
                apply_continuation_precasts(extra);
                apply_continuation_plan(extra);
                apply_continuation_activations(extra, out_breakpoint && my_bp_sink);
                if (out_breakpoint && my_bp_sink) { bp_sink_pop(); }
            }'''
assert s.count(a2) == 1
s = s.replace(a2, '''            if (!s_human_play)
            {
                // BREAKPOINT LEFTOVERS: taken before any nested breakpoint can hand over its own.
                std::vector<Action> my_lo;
                const bool my_lo_on = lo_pending_set;
                if (my_lo_on) { my_lo.swap(lo_pending); lo_pending_set = false; }
                if (out_breakpoint && my_bp_sink) { bp_sink_push(my_bp_sink); }
                TurnSolver::Plan extra;
                bp_searched_plan(2, extra);   // resolves to the plan's continuation or EMPTY
                ContPinFrame _cpf(extra, my_bp_sink);   // the continuation's own during-cast pins (ContPins)
                bp_play_searched_land(extra, my_bp_sink);
                // Lotus Bloom: the staged Dragonstorm/rituals grew this pre-pass first (the
                // executor's breakpoint replay had the same gap) -- now the shared loop.
                apply_continuation_precasts(extra);
                // ...the interrupted list's remaining casts join the continuation, ordered together by
                // the deck's order (or are dropped) -- the executor merges at the same point.
                bool lo_taken = false;
                if (my_lo_on && ResolveProvider(state).MergesBreakpointLeftovers(def))
                { lo_taken = MergeBreakpointLeftovers(state, extra.actions, extra.searched_order, my_lo); }
                apply_continuation_plan(extra);
                apply_continuation_activations(extra, out_breakpoint && my_bp_sink);
                if (out_breakpoint && my_bp_sink) { bp_sink_pop(); }
                if (lo_taken) { bp_truncate = true; }   // the rest of the interrupted list is handled
            }''')

# explicit-order loop
a3 = '''                else if (a.kind == Action::Kind::CastFromHand && !a.sacrifice_land)
                {
                    if (inline_taps) { flush_pre_taps(pre_tap_slot++); }
                    line_order_trace("cast", a);
                    prep_free(a);
                    cast_loyalty_ability = a.loyalty_ability; cast_devour_count = a.devour_count; cast_twobrid_colored = a.twobrid_colored; apply_one(a.card_name, false, false, 0, a.alt_cost, a.alt_lifegain, a.tutor_target, a.chosen_x, a.soulfire_own_targets, a.ponder_keep, a.crackle_targets, a.splice_count, a.chosen_float_color, a.enchant_target, a.bestow, a.replicate_count, a.convoke_green, a.convoke_other, a.phyrexian_life, a.evoke, a.adventure);
                    fire_unlock();
                }'''
assert s.count(a3) == 1
s = s.replace(a3, '''                else if (a.kind == Action::Kind::CastFromHand && !a.sacrifice_land)
                {
                    if (inline_taps) { flush_pre_taps(pre_tap_slot++); }
                    line_order_trace("cast", a);
                    prep_free(a);
                    // BREAKPOINT LEFTOVERS: an impulse-exile site the provider merges for gets this
                    // list's remaining casts (see lo_pending). Param test first: no other deck pays.
                    {
                        const CardDefinition* lod = a.def ? a.def : CardDatabase::Instance().Lookup(a.card_name);
                        if (lod != nullptr && lod->params.impulse_exile > 0
                            && ResolveProvider(state).MergesBreakpointLeftovers(*lod))
                        {
                            lo_pending.clear();
                            for (const Action* r = &a + 1; r < acts.data() + acts.size(); ++r)
                            { if (r->kind == Action::Kind::CastFromHand && !r->sacrifice_land) { lo_pending.push_back(*r); } }
                            lo_pending_set = !lo_pending.empty();
                        }
                    }
                    cast_loyalty_ability = a.loyalty_ability; cast_devour_count = a.devour_count; cast_twobrid_colored = a.twobrid_colored; apply_one(a.card_name, false, false, 0, a.alt_cost, a.alt_lifegain, a.tutor_target, a.chosen_x, a.soulfire_own_targets, a.ponder_keep, a.crackle_targets, a.splice_count, a.chosen_float_color, a.enchant_target, a.bestow, a.replicate_count, a.convoke_green, a.convoke_other, a.phyrexian_life, a.evoke, a.adventure);
                    lo_pending_set = false; lo_pending.clear();
                    fire_unlock();
                }''')

# opaque ord loop
a4 = '''                    const int dbg_before = dbg_count(a);
                    prep_free(a); cast_loyalty_ability = a.loyalty_ability; cast_devour_count = a.devour_count; cast_twobrid_colored = a.twobrid_colored; apply_one(a.card_name, false, false, 0, a.alt_cost, a.alt_lifegain, a.tutor_target, a.chosen_x, a.soulfire_own_targets, a.ponder_keep, a.crackle_targets, a.splice_count, a.chosen_float_color, a.enchant_target, a.bestow, a.replicate_count, a.convoke_green, a.convoke_other, a.phyrexian_life, a.evoke, a.adventure); fire_unlock();
                    dbg_cast("ord", a, dbg_before);'''
assert s.count(a4) == 1
s = s.replace(a4, '''                    const int dbg_before = dbg_count(a);
                    // BREAKPOINT LEFTOVERS (see lo_pending): this list's remaining casts.
                    {
                        const CardDefinition* lod = a.def ? a.def : CardDatabase::Instance().Lookup(a.card_name);
                        if (lod != nullptr && lod->params.impulse_exile > 0 && a.kind == Action::Kind::CastFromHand
                            && ResolveProvider(state).MergesBreakpointLeftovers(*lod))
                        {
                            lo_pending.clear();
                            auto it = std::find(ord.begin(), ord.end(), i);
                            for (auto r = (it == ord.end() ? it : it + 1); r != ord.end(); ++r)
                            {
                                const Action& ra = acts[static_cast<std::size_t>(*r)];
                                if (ra.kind == Action::Kind::CastFromHand && !ra.sacrifice_land) { lo_pending.push_back(ra); }
                            }
                            lo_pending_set = !lo_pending.empty();
                        }
                    }
                    prep_free(a); cast_loyalty_ability = a.loyalty_ability; cast_devour_count = a.devour_count; cast_twobrid_colored = a.twobrid_colored; apply_one(a.card_name, false, false, 0, a.alt_cost, a.alt_lifegain, a.tutor_target, a.chosen_x, a.soulfire_own_targets, a.ponder_keep, a.crackle_targets, a.splice_count, a.chosen_float_color, a.enchant_target, a.bestow, a.replicate_count, a.convoke_green, a.convoke_other, a.phyrexian_life, a.evoke, a.adventure); fire_unlock();
                    lo_pending_set = false; lo_pending.clear();
                    dbg_cast("ord", a, dbg_before);''')
open(p, 'w').write(s)
print("ok")
