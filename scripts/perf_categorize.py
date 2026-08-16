#!/usr/bin/env python3
"""Bucket a `perf report --no-children` self-time listing into cost categories.

WHY THIS EXISTS (and the trap it defuses): matching demangled symbols on their FULL SIGNATURE
buckets any function that merely TAKES a `std::vector` parameter as "allocation". On Mirrorwing
2026-08-16 that inflated an "alloc churn" category to 20% by catching CanTapNow, SubsetPayable,
SubsetWastesCreatureSacMana and the SolveUncached lambda -- all of which take vector params. The
real figure was 9.5%. So every rule here matches the FUNCTION NAME ONLY: the symbol is truncated
at the first '(' before any pattern is applied.

Usage:  perf report --no-children --stdio -i <perf.data> | python3 scripts/perf_categorize.py
"""
import re
import sys
import collections

# Ordered: first match wins, so put the specific patterns above the generic ones.
RULES = [
    ("backtracker",   [r"TapForCostBacktrack"]),
    ("other mana",    [r"CanTapNow", r"SubsetPayable", r"SubsetWastes", r"ManaCache", r"ManaPool",
                       r"PayableMana", r"FlowPrune", r"ProducibleMana", r"ManaSource",
                       r"TapForCost", r"CollectManaSources", r"ManaCost"]),
    ("plan/search",   [r"TurnSolver", r"SolveUncached", r"EnumeratePlan", r"CollectActions",
                       r"BuildSimKey", r"Rollout", r"SearchBudget", r"Evaluate", r"ValueLeaf",
                       r"ChooseAction", r"Simulate"]),
    ("alloc/copy",    [r"^operator new", r"^operator delete", r"_int_malloc", r"_int_free",
                       r"\bmalloc\b", r"\bfree\b", r"memmove", r"memcpy", r"memset",
                       r"~vector", r"push_back", r"_M_realloc", r"_M_rehash", r"std::_Rb_tree",
                       r"unordered_map", r"unordered_set", r"_M_insert"]),
]


def category(name):
    for cat, pats in RULES:
        for p in pats:
            if re.search(p, name):
                return cat
    return "other"


def main():
    # perf --stdio rows look like:  "    12.34%  mtg-analyze  mtg-analyze  [.] Foo::Bar(std::vector...)"
    row = re.compile(r"^\s*([\d.]+)%.*?\[[.k]\]\s+(.*)$")
    tot = collections.Counter()
    detail = collections.defaultdict(list)
    for line in sys.stdin:
        m = row.match(line)
        if not m:
            continue
        pct = float(m.group(1))
        sym = m.group(2).strip()
        name = sym.split("(")[0].strip()          # NAME ONLY -- see the docstring
        cat = category(name)
        tot[cat] += pct
        detail[cat].append((pct, name))

    grand = sum(tot.values())
    print("total self-time accounted: %.1f%%\n" % grand)
    print("%-14s %8s" % ("category", "self%"))
    for cat, pct in tot.most_common():
        print("%-14s %7.1f%%" % (cat, pct))
    for cat, _ in tot.most_common():
        print("\n--- %s ---" % cat)
        for pct, name in sorted(detail[cat], reverse=True)[:12]:
            print("  %6.2f%%  %s" % (pct, name[:110]))


if __name__ == "__main__":
    main()
