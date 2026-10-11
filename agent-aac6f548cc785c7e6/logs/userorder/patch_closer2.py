WT='/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6'

# 1. shared reader in EngineFlags.h (after SporeSourcePoolEnabled)
e=WT+'/src/ai/EngineFlags.h'
t=open(e).read()
anchor='''inline bool SporeSourcePoolEnabled()
{
    static const bool env_on = EnvOn("MTG_FUNGUS_SPORE_POOL", true);
    return heurarm::Flag(heurarm::FUNGUS_SPORE_POOL, env_on);
}
'''
add='''
// MTG_DRAGONSTORM_IRENCRAG_CLOSER (DEFAULT ON; =0 disables) -- USER order rule 7: Irencrag Feat is cast
// second-to-last, and after it only the CLOSER (Dragonstorm or a hard-cast Dragon -- the test
// DragonstormProvider::CastUnderCastCap applies to the offers after it). A restrictor that nothing can
// follow floats seven red no spell spends (the float empties before combat, so not even a pump). Two
// sites, one reader: the provider's subset test (OrderedSubsetRealisable -- a subset with the restrictor,
// no closer and no Apex whose exile could bring one) and the shared post-Apex merge
// (MergeBreakpointLeftovers -- a LEFTOVER restrictor with no closer after it and no Apex before it is not
// cast; it stays in hand). Executor and rollout both go through the merge -> lockstep.
inline bool DragonstormIrencragCloserOn()
{
    static const bool env_on = EnvOn("MTG_DRAGONSTORM_IRENCRAG_CLOSER", true);
    return heurarm::Flag(heurarm::DRAGONSTORM_IRENCRAG_CLOSER, env_on);
}
'''
assert anchor in t
t=t.replace(anchor,anchor+add)
open(e,'w').write(t)

# 2. provider: Rule A uses the shared reader, exempts a subset holding an Apex
p=WT+'/src/ai/DecisionProviders.cpp'
s=open(p).read()
old_reader_start=s.index('// MTG_DRAGONSTORM_IRENCRAG_CLOSER (DEFAULT ON; =0 -> admitted).')
old_reader_end=s.index('bool DragonstormProvider::OrderedSubsetRealisable(')
new_comment='''// USER order rule 7 at the SUBSET (MTG_DRAGONSTORM_IRENCRAG_CLOSER, DragonstormIrencragCloserOn in
// EngineFlags.h): a subset holding the restrictor needs a closer to spend its one remaining cast on --
// unless it holds an Apex, whose exile may bring the closer (the post-Apex merge then decides, with the
// exile known: a leftover Irencrag nothing can follow is not cast). Refusing the Apex case too was measured
// wrong: dragonstorm d0 s8500074 T6 {Apex, Medallion, Irencrag} found its two Scourges in the second exile
// (T7 win), and without it the d0 runner did not cast the second Apex at all (unwon).
'''
s=s[:old_reader_start]+new_comment+s[old_reader_end:]
old_a='    if (restrictor && !closer && IrencragNeedsCloserOn()) { return false; }\n'
new_a='    if (restrictor && !closer && !apex && DragonstormIrencragCloserOn()) { return false; }\n'
assert old_a in s
s=s.replace(old_a,new_a)
open(p,'w').write(s)

# 3. merge: Rule B
m=WT+'/src/ai/ManaPayment.cpp'
s=open(m).read()
old_b='''    // The casts, in that order, into the cast slots (the continuation's slots, then the appended ones --
    // ascending), non-cast actions where they were.
    std::vector<int> slots = cslots;
    for (int j = static_cast<int>(cont.size()); j < static_cast<int>(u.size()); ++j) { slots.push_back(j); }
    std::vector<Action> out = u;
    for (std::size_t k = 0; k < uord.size(); ++k) { out[static_cast<std::size_t>(slots[k])] = u[static_cast<std::size_t>(uord[k])]; }
    cont = std::move(out);'''
new_b='''    // USER rule 7 (MTG_DRAGONSTORM_IRENCRAG_CLOSER): a LEFTOVER cast restrictor (Irencrag Feat) that no
    // closer follows and no Apex precedes is not cast -- the exile brought nothing for its one remaining
    // cast, and it stays in hand for a turn that has one (dragonstorm d0 s7000035 T5: the pre-Apex plan's
    // Irencrag was cast alone after an empty continuation, seven red for nothing). With an Apex before it, a
    // nested breakpoint takes it up again with that exile known.
    int dropped = 0;
    if (DragonstormIrencragCloserOn())
    {
        auto def_of = [&](int i) -> const CardDefinition*
        { return u[static_cast<std::size_t>(i)].def ? u[static_cast<std::size_t>(i)].def
                                                     : CardDatabase::Instance().Lookup(u[static_cast<std::size_t>(i)].card_name); };
        for (int k = static_cast<int>(uord.size()) - 1; k >= 0; --k)
        {
            const int i = uord[static_cast<std::size_t>(k)];
            if (i < static_cast<int>(cont.size()) || u[static_cast<std::size_t>(i)].max_casts_after < 0) { continue; }
            bool follows = false, apex_before = false;
            for (int q = k + 1; q < static_cast<int>(uord.size()) && !follows; ++q)
            {
                const CardDefinition* d = def_of(uord[static_cast<std::size_t>(q)]);
                if (d != nullptr && (d->params.tutor_to_battlefield || d->card.IsCreature())) { follows = true; }
            }
            for (int q = 0; q < k && !apex_before; ++q)
            {
                const CardDefinition* d = def_of(uord[static_cast<std::size_t>(q)]);
                if (d != nullptr && d->params.impulse_exile > 0) { apex_before = true; }
            }
            if (!follows && !apex_before) { uord.erase(uord.begin() + k); ++dropped; }
        }
    }
    // The casts, in that order, into the cast slots (the continuation's slots, then the appended ones --
    // ascending), non-cast actions where they were. A dropped leftover gives back the LAST appended slot.
    std::vector<int> slots = cslots;
    for (int j = static_cast<int>(cont.size()); j < static_cast<int>(u.size()); ++j) { slots.push_back(j); }
    std::vector<Action> out = u;
    for (std::size_t k = 0; k < uord.size(); ++k) { out[static_cast<std::size_t>(slots[k])] = u[static_cast<std::size_t>(uord[k])]; }
    out.resize(u.size() - static_cast<std::size_t>(dropped));
    cont = std::move(out);'''
assert old_b in s
s=s.replace(old_b,new_b)
open(m,'w').write(s)
print('ok')
