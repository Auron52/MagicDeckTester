// Insert in TapForCostShared right after the SwapLeftoverForLine call (before --t_pilgrimage_pay_depth;)
    { // DS2-TEMP-DEBUG (remove before commit)
        static const int s_dbg_turn = EnvInt("MTG_DS2_PAYDBG", 0);
        if (s_dbg_turn > 0 && state.turn_number == s_dbg_turn && outer_pilgrimage_pay && t_pay_nest == 0)
        {
            const Card* pc = PayingSpellCard();
            const ManaPool& f = state.floating_mana;
            std::fprintf(stderr, "[ds2pay] %s paid=%d fc=%d cost g%d B%d R%d G%d lu g%d B%d R%d G%d nc=%d bound=%d hook=%d before B%d R%d G%d fl B%d R%d G%d C%d W%d ->",
                         pc ? pc->m_name.str().c_str() : "-", paid ? 1 : 0, for_creature ? 1 : 0, cost_in.generic,
                         cost_in.black, cost_in.red, cost_in.green, g_line_unpaid_cost.generic, g_line_unpaid_cost.black,
                         g_line_unpaid_cost.red, g_line_unpaid_cost.green, g_line_noncreature_mv, g_line_keep_bound,
                         manaoracle::t_hook != nullptr ? 1 : 0, float_before.black, float_before.red, float_before.green,
                         f.black, f.red, f.green, f.colorless, f.wild);
            for (const Permanent& q : state.battlefield)
            { if (q.controller_index == state.active_player_index) { std::fprintf(stderr, " %s#%d%s", q.card.m_name.str().c_str(), q.card.m_number, q.tapped ? "T" : ""); } }
            std::fprintf(stderr, "\n");
        }
    }
