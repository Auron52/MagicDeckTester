p = '/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6/src/ai/TurnSolver.cpp'
s = open(p).read()
a = "PoolCredit(pool, eff),"
b = "PoolCredit(pool_noncreature, eff_nc),"
print(s.count(a), s.count(b))
s = s.replace(a, "PoolCredit(pool, eff_cg),").replace(b, "PoolCredit(pool_noncreature, eff_nc_cg),")
open(p, 'w').write(s)
