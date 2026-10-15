#!/usr/bin/env python3
"""One-shot resolution of the 2ac8db0b cherry-pick (CastOrderTotalByName): take the canonical hook, drop
this branch's CastOrderTotal / CastOrderFixedKeyLess, and leave out CastOrderTiesByName (CritterLifegain's
hook, not in this branch's base -- the integration tree keeps it)."""
import os
import re

WT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))


def theirs(text):
    return re.sub(r"<<<<<<< HEAD\n.*?=======\n(.*?)>>>>>>> [^\n]*\n", r"\1", text, flags=re.S)


h = os.path.join(WT, "src/ai/ManaPayment.h")
s = open(h).read()
s = theirs(s)
open(h, "w").write(s)

c = os.path.join(WT, "src/ai/ManaPayment.cpp")
s = open(c).read()
s = theirs(s)
old_tail = """    if (prov.CastOrderTotalByName()) { return CastOrderNameTieLess(state, a, b); }
    // DecisionProvider::CastOrderTiesByName: the deck's TOTAL order -- a tie the ritual rule below
    // leaves standing falls to the NAME, never to plan order. False for every deck that does not
    // opt in, which then takes exactly the old "stable: keep plan order" exits below.
    const bool by_name = prov.CastOrderTiesByName();
    auto name_less = [&]() { return by_name && a.card_name.str() < b.card_name.str(); };
    if (!prov.CastCheapestFirstWithinTier()) { return name_less(); }   // stable: keep plan order
    // Reuse the action's memoized def (back-filled once per node, == Lookup(card_name)) instead of
    // re-hashing the name string on every comparison -- this comparator runs O(n log n) per sort.
    // Byte-identical; def==nullptr (an action from a path that didn't back-fill) falls back to Lookup.
    const CardDefinition* da = a.def ? a.def : CardDatabase::Instance().Lookup(a.card_name);
    const CardDefinition* db = b.def ? b.def : CardDatabase::Instance().Lookup(b.card_name);
    // ONLY among mana accelerants. Applying it to every equal-rank tie also reordered CREATURES,
    // where cost is the wrong key and ETB order carries real value: Scourge of Valkas damages per
    // Dragon that enters, so "Lathliss then Scourge" and "Scourge then Lathliss" differ by 3 damage
    // (dragonstorm_overnight_d3_s7007 gi310 lost a turn to exactly that swap, with identical draws).
    if (!da || !db || !IsManaRitual(*da) || !IsManaRitual(*db)) { return name_less(); }
    const int ma = a.cost.ManaValue(), mb = b.cost.ManaValue();
    if (ma != mb) { return ma < mb; }
    return name_less();
}"""
new_tail = """    if (prov.CastOrderTotalByName()) { return CastOrderNameTieLess(state, a, b); }
    if (!prov.CastCheapestFirstWithinTier()) { return false; }   // stable: keep plan order
    // Reuse the action's memoized def (back-filled once per node, == Lookup(card_name)) instead of
    // re-hashing the name string on every comparison -- this comparator runs O(n log n) per sort.
    // Byte-identical; def==nullptr (an action from a path that didn't back-fill) falls back to Lookup.
    const CardDefinition* da = a.def ? a.def : CardDatabase::Instance().Lookup(a.card_name);
    const CardDefinition* db = b.def ? b.def : CardDatabase::Instance().Lookup(b.card_name);
    // ONLY among mana accelerants. Applying it to every equal-rank tie also reordered CREATURES,
    // where cost is the wrong key and ETB order carries real value: Scourge of Valkas damages per
    // Dragon that enters, so "Lathliss then Scourge" and "Scourge then Lathliss" differ by 3 damage
    // (dragonstorm_overnight_d3_s7007 gi310 lost a turn to exactly that swap, with identical draws).
    if (!da || !db || !IsManaRitual(*da) || !IsManaRitual(*db)) { return false; }
    return a.cost.ManaValue() < b.cost.ManaValue();
}"""
assert old_tail in s
s = s.replace(old_tail, new_tail)
s = s.replace("""    // INTEGRATION NOTE (2026-10-15): two name-tie hooks landed from parallel branches --
    // CastOrderTotalByName (this one; also read by the sequential enumeration walk) and
    // CastOrderTiesByName (below; CritterLifegain). Both are kept exactly as measured; unify them in
    // one follow-up with a per-deck identity check.
""", "")
assert "<<<<<<<" not in s and "CastOrderFixedKeyLess" not in s
open(c, "w").write(s)
print("resolved")
