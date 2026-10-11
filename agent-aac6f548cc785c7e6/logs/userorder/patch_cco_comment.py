import re,sys
p='/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6/src/ai/ManaPayment.cpp'
s=open(p).read()
old='''// AvailableManaPool books it as one WILD unit, so the walk let it pay a ritual's {R}: dragonstorm d3
// s7100081 T2 -- Mountain + Territory, "Rite, Medallion, Pyretic, Seething, Lathliss" read as payable
// (the Medallion's earliest payable slot), the Territory could not pay Pyretic's {R}, two casts lost.
'''
new='''// AvailableManaPool books it as one WILD unit, so the walk let it pay a noncreature spell's coloured
// pip -- a lone Territory "paid" Rite of Flame's {R} (unit test). Found classifying the "projected
// payable, cast unpaid" audit class on dragonstorm d3 s7100081 T2 (Mountain + Territory): that game's
// own loss is NOT this -- the order does pay with the Territory's {C} on the Medallion's {2}, and the
// executor spends the floating {R}{R} there instead (the payer's float-first generic) -- but the walk
// reads it right only because its line-aware generic happens to pick the wild unit for the {2}.
'''
assert old in s
s=s.replace(old,new)
open(p,'w').write(s)
h='/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6/src/ai/HeuristicArm.h'
t=open(h).read()
old2="so it no longer reads a ritual's {R} as payable off the Territory (s7100081 T2: Medallion slot misplaced, two casts lost). Default ON; =0 -> wild"
new2="so it no longer reads a noncreature spell's coloured pip (Rite of Flame's {R}) as payable off the Territory. Dragonstorm only today (the walk's one caller); 0 of 400 paired games moved. Default ON; =0 -> wild"
assert old2 in t
t=t.replace(old2,new2)
open(h,'w').write(t)
print('ok')
