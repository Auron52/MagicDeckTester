p = '/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6/src/ai/AIEngine.cpp'
s = open(p).read()

# 1. state shared with the breakpoint lambda: declared just before it.
anchor = "    std::function<void(int)> resolve_draw_breakpoint = [&](int bp_depth)\n"
assert s.count(anchor) == 1
decl = '''    // BREAKPOINT LEFTOVERS (DecisionProvider::MergesBreakpointLeftovers, MergeBreakpointLeftovers):
    // the remaining casts of the list a breakpoint interrupted, handed to the NEXT resolve_draw_breakpoint
    // call (taken at its entry), and whether it took them -- then the caller stops its list. Lockstep
    // twin: ApplyPlanDirect's lo_pending / bp_truncate around its impulse-exile branch.
    std::vector<Action> rdb_leftovers;
    bool rdb_lo_taken = false;
    // Gather a list's remaining CASTS for the breakpoint the cast at `from` (a paid impulse-exile site
    // the provider merges for) is about to open. Returns false (nothing gathered) otherwise.
    auto gather_leftovers = [&](const std::vector<Action>& list, const std::vector<int>& rest,
                                const std::string& site_name) -> bool
    {
        rdb_leftovers.clear();
        const CardDefinition* sd = CardDatabase::Instance().Lookup(site_name);
        if (sd == nullptr || sd->params.impulse_exile <= 0
            || !ResolveProvider(state).MergesBreakpointLeftovers(*sd)) { return false; }
        for (int i : rest)
        {
            const Action& r = list[static_cast<std::size_t>(i)];
            if (r.kind == Action::Kind::CastFromHand && !r.sacrifice_land) { rdb_leftovers.push_back(r); }
        }
        return !rdb_leftovers.empty();
    };
'''
s = s.replace(anchor, decl + anchor)

# 2. take them at entry (after the cap check), report on every exit.
cap = "        if (bp_depth >= kMaxDrawBreakpointDepth || ++rdb_calls > kMaxDrawBreakpointCalls) { return; }\n"
assert s.count(cap) == 1
take = cap + '''        // BREAKPOINT LEFTOVERS: taken here, before any nested breakpoint can hand over its own.
        std::vector<Action> my_leftovers;
        my_leftovers.swap(rdb_leftovers);
        const CardDefinition* const my_lo_site = rdb_site;
        bool my_lo_taken = false;
        struct LoTakenOut { bool& out; const bool& v; ~LoTakenOut() { out = v; } } _lo_out{ rdb_lo_taken, my_lo_taken };
'''
s = s.replace(cap, take)

# 3. merge right before the continuation's cast order is built (after its land + pre-casts).
co = "        std::vector<int> cont_order;\n"
assert s.count(co) == 1
merge = '''        // BREAKPOINT LEFTOVERS: the interrupted list's remaining casts join this continuation, ordered
        // together by the deck's order (or dropped when they would cost it a payment). Lockstep twin:
        // ApplyPlanDirect's impulse-exile branch, at the same point (after land + pre-casts).
        if (!my_leftovers.empty() && my_lo_site != nullptr
            && ResolveProvider(state).MergesBreakpointLeftovers(*my_lo_site))
        { my_lo_taken = MergeBreakpointLeftovers(state, extra.actions, extra.searched_order, my_leftovers); }
'''
s = s.replace(co, merge + co)

# 4. nested call inside the continuation loop.
old4 = '''        for (int ci : cont_order)
        {
            const Action& a = extra.actions[ci];
            if (a.kind == Action::Kind::ActivateVial) { deploy_via_vial(a.card_name); resolve_now(); continue; }
            {'''
assert s.count(old4) == 1
new4 = '''        for (std::size_t cpos = 0; cpos < cont_order.size(); ++cpos)
        {
            const int ci = cont_order[cpos];
            const Action& a = extra.actions[ci];
            if (a.kind == Action::Kind::ActivateVial) { deploy_via_vial(a.card_name); resolve_now(); continue; }
            {'''
s = s.replace(old4, new4)
old5 = '''                    resolve_draw_breakpoint(bp_depth + 1);
                }
            }
        }
        for (const Action& a : extra.actions)'''
assert s.count(old5) == 1
new5 = '''                    // BREAKPOINT LEFTOVERS: this continuation's remaining casts go to the nested one.
                    const bool lo_gathered = cont_cast_paid
                        && gather_leftovers(extra.actions,
                                            std::vector<int>(cont_order.begin() + static_cast<long>(cpos) + 1,
                                                             cont_order.end()),
                                            a.card_name);
                    rdb_lo_taken = false;
                    resolve_draw_breakpoint(bp_depth + 1);
                    rdb_leftovers.clear();
                    if (lo_gathered && rdb_lo_taken) { break; }
                }
            }
        }
        for (const Action& a : extra.actions)'''
s = s.replace(old5, new5)

# 5. the two main-plan loops (searched vector order; opaque ord).
old6 = '''                if (fd_plan_committed)
                { if (cast_paid) { replay_segment(); } }
                // MTG_EXEC_FAILED_CAST_NO_RESOLVE: an UNPAID cast opened no breakpoint (see the reader).
                else if (cast_paid || !ExecFailedCastNoResolve())
                {
                    rdb_site = CardDatabase::Instance().Lookup(a.card_name);
                    pin_rdb_hand();
                    rdb_site_activated = false;   // a CAST-armed site
                    if (TurnSolver::BreakpointHandSnapshotWanted(state))
                    {
                        rdb_plan_casts.clear();
                        for (const Action& pa : plan.actions)
                        {
                            if (pa.kind != Action::Kind::CastFromHand) { continue; }
                            rdb_plan_casts.push_back(std::hash<std::string>{}(pa.card_name));
                        }
                    }
                    resolve_draw_breakpoint(0);
                }
            }'''
assert s.count(old6) == 2, s.count(old6)
new6 = '''                // BREAKPOINT LEFTOVERS: this plan's remaining casts go to the continuation (taken by
                // resolve_draw_breakpoint; on a committed line the rollout already merged them into
                // the recorded segment), and the plan stops here. Lockstep twin: ApplyPlanDirect.
                const bool lo_gathered = cast_paid && gather_leftovers(plan.actions, LO_REST, a.card_name);
                rdb_lo_taken = false;
                if (fd_plan_committed)
                { if (cast_paid) { replay_segment(); } }
                // MTG_EXEC_FAILED_CAST_NO_RESOLVE: an UNPAID cast opened no breakpoint (see the reader).
                else if (cast_paid || !ExecFailedCastNoResolve())
                {
                    rdb_site = CardDatabase::Instance().Lookup(a.card_name);
                    pin_rdb_hand();
                    rdb_site_activated = false;   // a CAST-armed site
                    if (TurnSolver::BreakpointHandSnapshotWanted(state))
                    {
                        rdb_plan_casts.clear();
                        for (const Action& pa : plan.actions)
                        {
                            if (pa.kind != Action::Kind::CastFromHand) { continue; }
                            rdb_plan_casts.push_back(std::hash<std::string>{}(pa.card_name));
                        }
                    }
                    resolve_draw_breakpoint(0);
                }
                rdb_leftovers.clear();
                if (lo_gathered && (fd_plan_committed || rdb_lo_taken)) { bp_trunc_exec = true; break; }
            }'''
i1 = s.index(old6)
i2 = s.index(old6, i1 + 1)
rest1 = "[&]{ std::vector<int> r; const int me = static_cast<int>(&a - plan.actions.data()); for (int q = me + 1; q < static_cast<int>(plan.actions.size()); ++q) { r.push_back(q); } return r; }()"
rest2 = "[&]{ std::vector<int> r; auto it = std::find(ord.begin(), ord.end(), oi); if (it != ord.end()) { r.assign(it + 1, ord.end()); } return r; }()"
s = s[:i2] + new6.replace("LO_REST", rest2) + s[i2 + len(old6):]
s = s[:i1] + new6.replace("LO_REST", rest1) + s[i1 + len(old6):]
open(p, 'w').write(s)
print("ok")
