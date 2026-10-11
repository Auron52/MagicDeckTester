import sys
p = '/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6/src/ai/DecisionProviders.cpp'
s = open(p).read()
mode = sys.argv[1]
reps = [
    ("    if (!UserOrderOn()) { return GenericProvider::CastOrderRank(s, def); }",
     "    if (!UserOrderOn() || (UoDbgOff() & 1)) { return GenericProvider::CastOrderRank(s, def); }"),
    ("    if (!UserOrderOn() || a.splice_count <= 0) { return rank; }",
     "    if (!UserOrderOn() || (UoDbgOff() & 2) || a.splice_count <= 0) { return rank; }"),
    ("    if (!UserOrderOn()) { return true; }\n    // A spent cap",
     "    if (!UserOrderOn() || (UoDbgOff() & 4)) { return true; }\n    // A spent cap"),
    ("    if (!UserOrderOn()) { return true; }\n    bool apex = false",
     "    if (!UserOrderOn() || (UoDbgOff() & 8)) { return true; }\n    bool apex = false"),
    ("    if (!UserOrderOn() || order.size() < 2) { return; }",
     "    if (!UserOrderOn() || (UoDbgOff() & 16) || order.size() < 2) { return; }"),
    ("bool DragonstormProvider::ExactOrderWalk() const { return UserOrderOn(); }",
     "bool DragonstormProvider::ExactOrderWalk() const { return UserOrderOn() && !(UoDbgOff() & 32); }"),
    ("    if (!UserOrderOn()) { return false; }\n    // The order is decided",
     "    if (!UserOrderOn() || (UoDbgOff() & 64)) { return false; }\n    // The order is decided"),
    ("bool DragonstormProvider::UserOrderOn()\n{",
     "static int UoDbgOff() { static const int v = EnvInt(\"MTG_UO_DBG_OFF\", 0); return v; }   // TEMP DIAGNOSTIC\nbool DragonstormProvider::UserOrderOn()\n{"),
]
for a, b in reps:
    if mode == 'apply':
        assert s.count(a) == 1, a
        s = s.replace(a, b)
    else:
        assert s.count(b) == 1, b
        s = s.replace(b, a)
open(p, 'w').write(s)
print('ok', mode)
