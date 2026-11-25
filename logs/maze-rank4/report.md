# Maze rank 2HG g783 / g1483 (agent ae28e3e5, 2026-11-25; saved by the parent)

Branch wip/maze-rank4 d1edd0152 = d624a0955 + logs only (no source fix).

| game | base | new |
|---|---|---|
| g1483 (seed 26701483, gi 1483; NOT 34201483) | T4 6f20b863 | T5 e14e2325 |
| g783 (34200783, gi 783) | T5 42ad820c | T6 56e35aff |

Attribution by spend-rank use (tmpdiag.patch, reverted; mask 1 = payment order, 2 = new-name fetch pick,
4 = Guild Summit tap order): g1483 T5 only with mask 1 (payment); g783 T6 only with mask 2 (fetch pick).

## g1483: payment order, not clairvoyance
Same board and draws at the T4 frame (choices 1,0,-1,-1,0,-1,-1,-1,1,4,0,-1,-1,0,-1; hand Archdruid's Charm).
Base wins T4: Maze's End -> Baldur's Gate, replay Maze's End, Charm, second activation, end-step win via
Manabond. Base's first {3} spends Planar Nexus + Golgari + Rakdos by battlefield order (all duals tie 20),
leaving Azorius (no {G}), so Charm's {G}{G}{G} is forced through Baldur's Gate fed mode ({2} -> 6 {G}) and the
float pays the second activation. New: Azorius 10, Rakdos 10, Golgari 11 (+1 major-colour tie-break,
d624a0955): {3} = Azorius + Rakdos, Golgari kept, Charm paid directly, fed mode unused, the second {3}
unpayable. Payment is not searched, so no budget recovers it. Forcing plan 120 wins T4 on both arms.
Root cause: the payer never prefers Baldur's Gate fed mode while a direct coloured payment exists, even
when the line still owes mana this phase. Keeping the G source for a G spell is the right rank call; "pay
via fed mode when it covers the bill and floats extra" is not a dominance -> it would be a heuristic lever.

## g783: clairvoyance
Mulligan bottom differs (clairvoyant rollout) but forcing base's bottom (choices 0,1,5) still T6. First
divergence: T4 opponent-turn Burgeoning trigger (frame 0,1,5,0,-1,-1,0,4,-1,-1,0,0,1,25,4,0,-1,-1,0,0,72,4,4):
base fetches Rakdos (all single-copy duals 20, library order), new ranks Gruul 21. Forcing Rakdos -> T5 and
Gruul -> T6 on BOTH arms: Maze's End shuffles after the fetch; the T5 draw is Sylvan Scrying vs Rakdos
Guildgate. Draw luck; Gruul is no worse in expectation.

## Not re-run
No src change, so the recovered set (g993, g1149/g1770/g2030, 7 play games) and Maze units stand as recorded
for d624a0955.

## A/B generator
logs/maze-rank4/mk_ab_r4.py -> build/scratch/maze-rank-2hg/ab_r4.json (5000 play pairs 38000000+gi d5 b20,
2000 2HG pairs 38200000+gi; 14,000 jobs) on a frozen d624a0955 binary
(build/scratch/claude_maze_rank3/abtb/bin/mtg).

## Decision (USER)
g1483: (a) Baldur's Gate fed-mode payment preference as a default-off lever + A/B, (b) revisit the +1
major-colour tie-break (g2030 relies on it), or (c) accept as base luck. Agent recommends (c) for the rank
adoption, (a) designed separately if wanted.
