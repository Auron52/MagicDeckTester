p='/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6/test/unit/test_dragonstorm_user_order.cpp'
s=open(p).read()
old1='''        UoArms arms(false);
        std::vector<int> ord;
        for (const char* n : { kMed, kRite, kPyr, kSeeth, kScour })
        {
            for (int i = 0; i < static_cast<int>(acts.size()); ++i)
            { if (acts[i].card_name.str() == n) { ord.push_back(i); break; } }
        }
        ApplyPayableCastOrder(s, acts, ord);
        CHECK(NamesUo(acts, ord) == std::vector<std::string>{ kMed, kRite, kPyr, kSeeth, kScour });
    }
}'''
new1='''        // (The plain walk is pinned: MTG_PAYABLE_ORDER_EXACT, default ON since the follow-up round, judges
        // this producer domain with the exact walk for every deck -- see the block below.)
        UoArms arms(false);
        heurarm::t_arm[heurarm::PAYABLE_ORDER_EXACT] = 0;
        std::vector<int> ord;
        for (const char* n : { kMed, kRite, kPyr, kSeeth, kScour })
        {
            for (int i = 0; i < static_cast<int>(acts.size()); ++i)
            { if (acts[i].card_name.str() == n) { ord.push_back(i); break; } }
        }
        std::vector<int> ord_exact = ord;
        ApplyPayableCastOrder(s, acts, ord);
        CHECK(NamesUo(acts, ord) == std::vector<std::string>{ kMed, kRite, kPyr, kSeeth, kScour });
        // With MTG_PAYABLE_ORDER_EXACT the same fallback credits the discount and finds slot 1 even with the
        // USER order off: the nearest order that pays is "Rite, Medallion, ...".
        heurarm::t_arm[heurarm::PAYABLE_ORDER_EXACT] = 1;
        ApplyPayableCastOrder(s, acts, ord_exact);
        CHECK(NamesUo(acts, ord_exact) == std::vector<std::string>{ kRite, kMed, kPyr, kSeeth, kScour });
        heurarm::t_arm[heurarm::PAYABLE_ORDER_EXACT] = -1;
    }
}'''
assert old1 in s
s=s.replace(old1,new1)
old2='''        UoArms arms(lever);
        GameState s = BoardUo(2, { kMed, kRite, kPyr, kSeeth, kScour });
        TurnSolver::Plan plan;'''
new2='''        UoArms arms(lever);
        // The generic arm describes the PLAIN walk (pinned; MTG_PAYABLE_ORDER_EXACT would find the paying order).
        heurarm::t_arm[heurarm::PAYABLE_ORDER_EXACT] = lever ? -1 : 0;
        GameState s = BoardUo(2, { kMed, kRite, kPyr, kSeeth, kScour });
        TurnSolver::Plan plan;'''
assert old2 in s
s=s.replace(old2,new2)
old3='''            // Generic tiers: Scourge (creature 10) first -- unpayable on two Mountains -- and no order the
            // plain walk can find pays the whole line, so the Dragon is never cast.
            CHECK(InHandUo(s, kScour) == 1);
        }'''
new3='''            // Generic tiers: Scourge (creature 10) first -- unpayable on two Mountains -- and no order the
            // plain walk can find pays the whole line, so the Dragon is never cast.
            CHECK(InHandUo(s, kScour) == 1);
        }
        heurarm::t_arm[heurarm::PAYABLE_ORDER_EXACT] = -1;'''
assert old3 in s
s=s.replace(old3,new3)
open(p,'w').write(s)
print('ok')
