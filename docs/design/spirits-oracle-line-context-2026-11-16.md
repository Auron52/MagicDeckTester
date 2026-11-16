# Spirits: replay the line context in the mana oracle

The Spirits gate on games 5,000–9,999 found 21 oracle gaps in three
Supreme Phantom → Usher of the Fallen signatures. The engine paid Phantom's
`{1}{U}` from Island `{U}` and Azorius Chancery `{W}{U}`, spending the
Chancery's blue unit on the generic pip and leaving its white unit for Usher.
The oracle verified the same taps after `ApplyPlan` had left its
`LineUnpaidCostScope`. Its float-only payment spent white on the generic
pip, so it treated white as the natural leftover and omitted the white
set-aside that the scripted replay needed. Turning off
`MTG_AUTO_LINE_GENERIC` cleared the gap; turning off joint prepayment did
not.

The audit now captures the applying line's unpaid cost, unseen-cast flag,
and activation-hold flag at every payment frontier. It restores them while
enumerating that frontier's payments and distinguishes different line
contexts in the frontier prune key. `MTG_ORACLE_LINE_CONTEXT=0` restores the
old audit for comparison. Ordinary play never enters this code path.

All three exact gate repros (seeds 30,006,463; 30,006,994; 30,008,365)
now have zero oracle gaps. The off-control on seed 30,006,463 restores its
seven gaps. A focused `CoversEngineOutcome` test passes with the fix and
fails with `=0`. The full Release unit suite passes 3,368 cases, including
that test. The next fresh Spirits gate is still owed for this audit change.
