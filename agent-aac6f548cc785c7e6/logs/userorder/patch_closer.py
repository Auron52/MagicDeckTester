WT='/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6'
h=WT+'/src/ai/HeuristicArm.h'
t=open(h).read()
anchor='    PAYABLE_ORDER_DYNAMIC_DECLINE,'
i=t.index(anchor); j=t.index('\n',i)+1
t=t[:j]+('    DRAGONSTORM_IRENCRAG_CLOSER, // MTG_DRAGONSTORM_IRENCRAG_CLOSER (USER order, rule 7) a Dragonstorm subset holding a cast restrictor (Irencrag Feat) must also hold a CLOSER (Dragonstorm or a Dragon) to spend its one remaining cast on -- {rituals, Apex, Irencrag} cast Irencrag LAST with nothing after it (dragonstorm d0 s7000035 T5); the order-free guard let its seven red "fund" the Apex the USER order puts first. The post-Apex continuation still offers Irencrag -> Dragon off the exile. Default ON; =0 -> admitted\n')+t[j:]
a2='        "MTG_PAYABLE_ORDER_DYNAMIC_DECLINE",\n'
k=t.index(a2)+len(a2)
t=t[:k]+'        "MTG_DRAGONSTORM_IRENCRAG_CLOSER",\n'+t[k:]
open(h,'w').write(t)

p=WT+'/src/ai/DecisionProviders.cpp'
s=open(p).read()
old='''bool DragonstormProvider::OrderedSubsetRealisable(const GameState& s0, const std::vector<Action>& cands,
                                                  const std::vector<int>& sel) const
{
    if (!UserOrderOn()) { return true; }
    bool apex = false, restrictor = false, spliced = false, pre = false;
    for (int j : sel)
    {
        const Action& a = cands[j];
        const CardDefinition* d = a.def ? a.def : CardDatabase::Instance().Lookup(a.card_name);
        if (a.kind == Action::Kind::CastFromHand && d != nullptr && d->params.impulse_exile > 0) { apex = true; }
        if (a.max_casts_after >= 0) { restrictor = true; }
        if (a.kind == Action::Kind::CastFromHand && a.splice_count > 0) { spliced = true; }
        if (a.kind == Action::Kind::SacForMana || a.kind == Action::Kind::Suspend) { pre = true; }
    }
    if (!(apex && restrictor) && !spliced) { return true; }'''
new='''// MTG_DRAGONSTORM_IRENCRAG_CLOSER (DEFAULT ON; =0 -> admitted). USER order rule 7: Irencrag Feat is cast
// second-to-last, and after it only the CLOSER -- Dragonstorm or a hard-cast Dragon (the same test
// CastUnderCastCap applies to the offers after it). A subset holding the restrictor and NO closer casts
// Irencrag last: four mana and a card to float seven red nothing spends (the float empties before combat,
// so not even a firebreathing pump). The order-free credit is what admitted it -- Irencrag's seven "pay"
// for an Apex the USER order casts first (dragonstorm d0 s7000035 T5: {Medallion, Desperate, Seething x2,
// Apex, Irencrag}; the leftover Irencrag was cast alone after the empty continuation). No line is lost: a
// Dragon the Apex exiles is cast by the post-Apex continuation, which still offers Irencrag -> Dragon.
static bool IrencragNeedsCloserOn()
{
    static const bool on = EnvOn("MTG_DRAGONSTORM_IRENCRAG_CLOSER", true);   // DEFAULT ON; =0 disables
    return heurarm::Flag(heurarm::DRAGONSTORM_IRENCRAG_CLOSER, on);
}

bool DragonstormProvider::OrderedSubsetRealisable(const GameState& s0, const std::vector<Action>& cands,
                                                  const std::vector<int>& sel) const
{
    if (!UserOrderOn()) { return true; }
    bool apex = false, restrictor = false, spliced = false, pre = false, closer = false;
    for (int j : sel)
    {
        const Action& a = cands[j];
        const CardDefinition* d = a.def ? a.def : CardDatabase::Instance().Lookup(a.card_name);
        if (a.kind == Action::Kind::CastFromHand && d != nullptr && d->params.impulse_exile > 0) { apex = true; }
        if (a.max_casts_after >= 0) { restrictor = true; }
        else if (a.kind == Action::Kind::CastFromHand && d != nullptr
                 && (d->params.tutor_to_battlefield || d->card.IsCreature())) { closer = true; }
        if (a.kind == Action::Kind::CastFromHand && a.splice_count > 0) { spliced = true; }
        if (a.kind == Action::Kind::SacForMana || a.kind == Action::Kind::Suspend) { pre = true; }
    }
    if (restrictor && !closer && IrencragNeedsCloserOn()) { return false; }
    if (!(apex && restrictor) && !spliced) { return true; }'''
assert old in s
s=s.replace(old,new)
open(p,'w').write(s)
print('ok')
