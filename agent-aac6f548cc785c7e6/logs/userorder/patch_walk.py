p = '/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6/src/ai/ManaPayment.cpp'
s = open(p).read()
start = s.index("// See ManaPayment.h. The extra terms are all card-parameter tests, so the walk carries no deck")
end = s.index("bool UserOrderTraceOn()")
body = s[start:end]
new = body
new = new.replace("""int ExactOrderFirstUnpayable(const GameState& state, const std::vector<Action>& acts,
                             const std::vector<int>& order)
{""", """// `paid` == nullptr: stop at the first cast that cannot be made and return its position. Otherwise the
// walk SKIPS it the way the executor does (it stays in hand: no cost, no output, no cast counted),
// records every position's verdict in (*paid) and still returns the first failure.
static int ExactOrderWalkImpl(const GameState& state, const std::vector<Action>& acts,
                              const std::vector<int>& order, std::vector<char>* paid)
{
    if (paid != nullptr) { paid->assign(order.size(), 0); }
    int first_fail = -1;""")
# failure points: "{ return pos; }" -> skip handling
new = new.replace("if (casts_left == 0) { return pos; }",
                  "if (casts_left == 0) { if (paid == nullptr) { return pos; } if (first_fail < 0) { first_fail = pos; } continue; }")
new = new.replace("if (casts_left > 0 && d != nullptr && d->params.impulse_exile > 0) { return pos; }",
                  "if (casts_left > 0 && d != nullptr && d->params.impulse_exile > 0)\n        { if (paid == nullptr) { return pos; } if (first_fail < 0) { first_fail = pos; } continue; }")
new = new.replace("if (copies - 1 - prior < a.splice_count) { return pos; }",
                  "if (copies - 1 - prior < a.splice_count)\n            { if (paid == nullptr) { return pos; } if (first_fail < 0) { first_fail = pos; } continue; }")
new = new.replace("if (!pool.CanPay(cost)) { return pos; }",
                  "if (!pool.CanPay(cost))\n            { if (paid == nullptr) { return pos; } if (first_fail < 0) { first_fail = pos; } continue; }")
new = new.replace("""        cast_names.push_back(a.card_name);
        if (casts_left > 0) { --casts_left; }
        if (d != nullptr && d->params.max_casts_after >= 0) { casts_left = d->params.max_casts_after; }
    }
    return -1;
}
""", """        cast_names.push_back(a.card_name);
        if (casts_left > 0) { --casts_left; }
        if (d != nullptr && d->params.max_casts_after >= 0) { casts_left = d->params.max_casts_after; }
        if (paid != nullptr) { (*paid)[static_cast<std::size_t>(pos)] = 1; }
    }
    return first_fail;
}

int ExactOrderFirstUnpayable(const GameState& state, const std::vector<Action>& acts,
                             const std::vector<int>& order)
{
    return ExactOrderWalkImpl(state, acts, order, nullptr);
}

int ExactOrderPaidMask(const GameState& state, const std::vector<Action>& acts,
                       const std::vector<int>& order, std::vector<char>& paid)
{
    return ExactOrderWalkImpl(state, acts, order, &paid);
}
""")
assert new.count("return pos;") == 4, new.count("return pos;")
s = s[:start] + new + s[end:]
open(p, 'w').write(s)
print("ok")
