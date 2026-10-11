p='/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6/test/unit/test_dragonstorm_followups.cpp'
s=open(p).read()
i=s.index('TEST_CASE("Irencrag needs a closer')
s=s[:i]+'''TEST_CASE("Irencrag needs a closer after it: subset and post-Apex leftovers (MTG_DRAGONSTORM_IRENCRAG_CLOSER)")
{
    ArmFu user(heurarm::DRAGONSTORM_USER_ORDER, true);
    ArmFu lo(heurarm::DRAGONSTORM_BP_LEFTOVERS, true);
    const GameState s = BoardFu(std::vector<std::string>(8, "Mountain"),
                                { "Seething Song", "Apex of Power", "Irencrag Feat", "Scourge of Valkas" }, 6);
    const std::vector<Action> cands = { CastFu(s, "Seething Song"), CastFu(s, "Apex of Power"),
                                        CastFu(s, "Irencrag Feat"), CastFu(s, "Scourge of Valkas") };
    for (const bool on : { true, false })
    {
        CAPTURE(on);
        ArmFu arm(heurarm::DRAGONSTORM_IRENCRAG_CLOSER, on);
        // SUBSET. {Seething, Irencrag}: nothing can spend Irencrag's seven red -> refused. With an Apex the
        // exile may bring the closer (s8500074 T6 found two Scourges there) -> admitted; with a Dragon, admitted.
        CHECK(DsProvFu().OrderedSubsetRealisable(s, cands, { 0, 2 }) == !on);
        CHECK(DsProvFu().OrderedSubsetRealisable(s, cands, { 0, 1, 2 }));
        CHECK(DsProvFu().OrderedSubsetRealisable(s, cands, { 0, 1, 2, 3 }));
        CHECK(DsProvFu().OrderedSubsetRealisable(s, cands, { 0, 1 }));   // no restrictor: untouched
        // POST-APEX MERGE. An empty continuation + the leftover Irencrag: not cast (s7000035 T5) ...
        {
            std::vector<Action> cont;
            bool searched = false;
            CHECK(MergeBreakpointLeftovers(s, cont, searched, { CastFu(s, "Irencrag Feat") }));
            CHECK(CastNamesFu(cont) == (on ? std::vector<std::string>{}
                                           : std::vector<std::string>{ "Irencrag Feat" }));
        }
        // ... but a continuation Dragon is its closer, and a continuation Apex may bring one: kept.
        {
            std::vector<Action> cont = { CastFu(s, "Scourge of Valkas") };
            bool searched = false;
            CHECK(MergeBreakpointLeftovers(s, cont, searched, { CastFu(s, "Irencrag Feat") }));
            CHECK(CastNamesFu(cont) == std::vector<std::string>{ "Irencrag Feat", "Scourge of Valkas" });
        }
        {
            std::vector<Action> cont = { CastFu(s, "Apex of Power") };
            bool searched = false;
            CHECK(MergeBreakpointLeftovers(s, cont, searched, { CastFu(s, "Irencrag Feat") }));
            CHECK(CastNamesFu(cont) == std::vector<std::string>{ "Apex of Power", "Irencrag Feat" });
        }
    }
}
'''
open(p,'w').write(s)
print('ok')
