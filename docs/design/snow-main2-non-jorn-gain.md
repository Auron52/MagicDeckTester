# Snow: where did the every-turn second main's extra gain come from?

Status: **deferred** (2026-10-04). Open question, no code change pending.

## Background

Snow plays the MDFC Jorn, God of Winter // Kaldring, the Rimestaff as **Jorn** (USER 2026-10-03).
Jorn's attack trigger untaps every snow permanent, so the deck needs a post-combat main on the
turns Jorn attacks. Two shapes were measured on all Snow tier cases (4,330 games), against the
3de66a1f baseline:

| arm | searched units | avg win turn, searched cells |
|---|---:|---|
| Skred never cast (`NeverCast`), no Jorn | 0.85x | ≈ baseline (±0.02) |
| Jorn, main 2 on **every** turn (`DeckUsesSecondMain`) | 1.66x | 0.05–0.23 better |
| Jorn, main 2 **only on Jorn turns** (`UntapSecondMainLive`) — shipped | 1.30x | 0.03–0.20 better |

The shipped arm keeps most of the gain. The remainder — the every-turn arm is a few hundredths of
a turn better in most cells — comes from playing a main 2 on turns Jorn did **not** attack.

Caveat on the every-turn row: it was measured on a binary with the untap-timing bug (attackers were
tapped by `ResolveCombatDamage` after Jorn's untap), so its Jorn turns were slightly *under*-served.
The gap is therefore, if anything, understated.

## The open question

What does a non-Jorn main 2 buy a deck with no post-combat effects? Every Snow card is at least as
good cast in main 1 (snow permanents feed the Treefolk before combat), so a main-2 cast should be
weakly dominated — unless main 1 **could not** make the play. Candidates:

1. A card main 1 could not see: something found or drawn mid-turn after the main-1 search committed.
2. A tie broken the wrong way. The first suspect, s2002 gi56 T4, turned out to be this and harmless:
   the root searched "cast the Scrying Sheets find (Arcum's Astrolabe) now" and the base line tied
   on win turn (both verified T5), so the base line won the tie. Not a gap.
3. Budget: at d3/d5 b10 the main-1 search may truncate before reaching a line that a cheap main 2
   then picks up.

Only (1) or (3) would be a real defect; (2) is a tie-break preference.

## Method (about 30 minutes)

1. Run Snow's three tiers (`--deck=snow`) on both binaries, keeping each run's
   `test/logs/<mode>/wins` (they are overwritten by the next run — copy them out).
   Every-turn arm: rebuild d08c2c6f (Jorn + `DeckUsesSecondMain`), ideally with the untap-timing fix
   from 6fb10c80 cherry-picked so the arms differ only in the main-2 gate.
2. Diff per game; take the games faster **only** in the every-turn arm.
3. For a few of them: `MTG_FD_TRACE=1 MTG_BP_TRACE=1 MTG_BP_CONT_TRACE=<turn>` on both binaries,
   find the non-Jorn turn whose main 2 acted, and classify it as (1), (2) or (3).
4. Re-run those games at `--depth 8 --budget-ms 0` on the shipped binary: if they reach the
   every-turn arm's turn there, it is budget (3), not expressiveness.
