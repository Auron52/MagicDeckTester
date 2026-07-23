#!/usr/bin/env python3
"""Rollout-quality diagnosis: digest the greedy-d0-vs-search divergences a run logged via
MTG_DIVERGENCE_LOG, so an agent can HYPOTHESIZE a provider hook that closes the gap.

This is step 2 of the divergence-digest loop documented in
docs/design/dragonstorm-d0-divergence-digest.md. The full loop:

  1. Instrument   MTG_DIVERGENCE_LOG=<file>  (src/ai/AIEngine.cpp; inert unless set) logs, per real
                  pre-combat decision, the search's plan vs what greedy d0 would do at the SAME state.
  2. Digest       THIS SCRIPT: cluster the divergences into candidate rule-shaped patterns.
  3. Hypothesize  map a surviving pattern to a DecisionProvider hook (ExtraLethalDamage / CastOrderRank
                  / ArchetypeCardValue ...), archetype-scoped, never in GenericProvider.
  4. Cost-test    build the hook behind a temp env gate; measure LP on/off (train seeds).

TWO GUARDRAILS this digest cannot enforce for you (both learned the hard way -- see the design doc):
  * COUNT != COST. This digest COUNTS divergences; a tie (two lethal lines, same win turn) counts the
    same as a blunder. A high count is a HINT, not a payoff -- always cost-weight by win-turn impact and
    validate on LP, never on the count. (The Lotus guard had 504 counts and was a measured NO-OP.)
  * BLIND d0 LP VALIDATES ONLY INFORMATION-ADDING RULES. A rule that ADDS lethality/board information
    (the go-off recognizer) helps d0 AND the search -- monotone, safe. A rule that PRUNES a greedy
    option (the slow-dragon guard) can improve blind d0 while WORSENING the shipped search, because the
    rollout's job is faithful simulation, not optimal play. Judge option-prunes on the SHIPPED config LP.

v1 scope: compares CASTS only (land choice not compared -> land/opening divergences undercounted);
counts, not cost-weighted (see guardrail 1). Diagnosis only; changes nothing.

Usage:
    python3 scripts/rollout_divergence_digest.py <log.jsonl> [<log2.jsonl> ...]
"""
import json, sys, glob, collections

files = sys.argv[1:] or glob.glob("logs/divergence/*.jsonl")
if not files:
    print("no divergence logs given (and none in logs/divergence/*.jsonl)"); sys.exit(2)
recs = []
for f in files:
    for line in open(f):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        recs.append(json.loads(line))

def casts(s):
    if s == "(idle)":
        return []
    return [c.strip() for c in s.split(",") if c.strip()]

n = len(recs)
div = [r for r in recs if r["diverge"]]
print("=== d0-vs-search DIVERGENCE DIGEST (v1: casts-only, count-unweighted -- HINTS not payoffs) ===")
print("decisions: %d   divergences: %d   rate: %.1f%%   (files: %d)"
      % (n, len(div), 100.0 * len(div) / max(n, 1), len(files)))

# --- divergence rate by turn ---
byturn_all = collections.Counter(r["turn"] for r in recs)
byturn_div = collections.Counter(r["turn"] for r in div)
print("\n-- divergence rate by turn --")
for t in sorted(byturn_all):
    a, d = byturn_all[t], byturn_div[t]
    print("  T%d: %3d/%3d diverge (%.0f%%)" % (t, d, a, 100.0 * d / a))

# --- pattern buckets ---
greedy_idle = sum(1 for r in div if r["greedy"] == "(idle)")
search_idle = sum(1 for r in div if r["search"] == "(idle)")
both_act    = len(div) - greedy_idle - search_idle
print("\n-- divergence shape --")
print("  greedy IDLES while search acts : %d (%.0f%%)" % (greedy_idle, 100.0*greedy_idle/max(len(div),1)))
print("  search idles while greedy acts : %d (%.0f%%)" % (search_idle, 100.0*search_idle/max(len(div),1)))
print("  both act, different plays       : %d (%.0f%%)" % (both_act, 100.0*both_act/max(len(div),1)))

# --- which cards does the search PLAY that greedy misses (search-only), and vice versa ---
search_only = collections.Counter()
greedy_only = collections.Counter()
for r in div:
    sc, gc = set(casts(r["search"])), set(casts(r["greedy"]))
    for c in sc - gc: search_only[c] += 1
    for c in gc - sc: greedy_only[c] += 1

print("\n-- cards the SEARCH plays that greedy MISSES (top 20 -- candidate 'greedy under-values X') --")
for c, k in search_only.most_common(20):
    print("  %4d  %s" % (k, c))
print("\n-- cards GREEDY plays that the search does NOT (top 20 -- candidate 'greedy wastes X') --")
for c, k in greedy_only.most_common(20):
    print("  %4d  %s" % (k, c))

# --- count-of-spells delta (does search cast MORE per turn -> ramp/develop?) ---
more = sum(1 for r in div if len(casts(r["search"])) > len(casts(r["greedy"])))
fewer = sum(1 for r in div if len(casts(r["search"])) < len(casts(r["greedy"])))
same = len(div) - more - fewer
print("\n-- spells-cast count (search vs greedy) on divergences --")
print("  search casts MORE  : %d (%.0f%%)" % (more, 100.0*more/max(len(div),1)))
print("  search casts FEWER : %d (%.0f%%)" % (fewer, 100.0*fewer/max(len(div),1)))
print("  same count, diff cards: %d (%.0f%%)" % (same, 100.0*same/max(len(div),1)))
print("\nNEXT: pick a pattern, hypothesize a provider hook, cost-test on LP (see the design doc). "
      "Remember: count != cost; validate option-prunes on the SHIPPED config, not blind d0.")
