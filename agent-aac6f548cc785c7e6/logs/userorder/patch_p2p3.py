WT='/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6'
h=WT+'/src/ai/HeuristicArm.h'
t=open(h).read()
anchor='    EXACT_WALK_CCO,'
i=t.index(anchor); j=t.index('\n',i)+1
slots=('    PAYABLE_ORDER_EXACT,      // MTG_PAYABLE_ORDER_EXACT MTG_PAYABLE_ORDER\'s apply site judges a PRODUCER-domain order (ritual float / same-turn rock, no maker or reveal cast) with the exact order walk for EVERY deck, not only a provider opting in via ExactOrderWalk(): the plain walk ignores a cast cap (Irencrag Feat -> "only one more spell", hinata d0 gi1427 T6) and pays a generic pip off an any-colour land a later cast needed (Sol Ring\'s {1} vs Gamble\'s {R}, hinata d0 gi1680 T7), so it permuted payable orders away. Default ON; =0 -> plain walk outside Dragonstorm\n')
t=t[:j]+slots+t[j:]
a2='        "MTG_EXACT_WALK_CCO",\n'
k=t.index(a2)+len(a2)
t=t[:k]+'        "MTG_PAYABLE_ORDER_EXACT",\n'+t[k:]
open(h,'w').write(t)

p=WT+'/src/ai/ManaPayment.cpp'
s=open(p).read()
old_fn='''static bool PoCreditedProducer(const GameState& state, const std::vector<Action>& acts, const Action& a)
{'''
new_pre='''// MTG_PAYABLE_ORDER_EXACT (DEFAULT ON; =0 -> the plain walk unless the provider opts in via
// ExactOrderWalk()). The apply site judged a PRODUCER-domain order (MTG_PAYABLE_ORDER_RITUALS) with the
// exact walk only for Dragonstorm; every other deck got FirstUnpayablePosImpl, which (a) ignores a cast
// cap -- hinata d0 gi1427 (seed 1331428) T6, {Ornithopter, Irencrag Feat, Crackle}: it accepted
// "Irencrag, Ornithopter, Crackle", where Irencrag's "only one more spell" leaves Crackle uncastable --
// and (b) pays a generic pip off wild before a colour (PayFromPool), so a land a LATER cast needs is
// spent first -- gi1680 (seed 1331681) T7: Island + Orchard + floating {U}, "Sol Ring, Gamble" read as
// unpayable (the Orchard paid Sol Ring's {1}) and was swapped to "Gamble, Sol Ring", tutoring with the
// Sol Ring still in hand for Gamble's random discard. The exact walk is the plain walk plus those terms
// (cast cap, line-aware generic, positional reducers / escalation / impulse float, creature-only land
// colour) over the same pool and the same per-cast output credit, so the domain and site are unchanged.
static bool PayableOrderExactOn()
{
    static const bool env_on = EnvOn("MTG_PAYABLE_ORDER_EXACT", true);   // DEFAULT ON; =0 disables
    return heurarm::Flag(heurarm::PAYABLE_ORDER_EXACT, env_on);
}

'''
assert old_fn in s
s=s.replace(old_fn,new_pre+old_fn)
old_ex='''    const bool exact = site == 0 && producer_only && extra_wild == 0
                    && ResolveProvider(state).ExactOrderWalk();'''
new_ex='''    // MTG_PAYABLE_ORDER_EXACT extends it to every deck (see PayableOrderExactOn).
    const bool exact = site == 0 && producer_only && extra_wild == 0
                    && (PayableOrderExactOn() || ResolveProvider(state).ExactOrderWalk());'''
assert old_ex in s
s=s.replace(old_ex,new_ex)
open(p,'w').write(s)
print('ok')
