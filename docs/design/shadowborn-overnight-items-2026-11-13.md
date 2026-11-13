# Shadowborn: two same-draws slowdowns from the 2026-11-12 overnight (checkpoint)

Status: OPEN, investigation not started. Owner: whichever session picks this up (the USER asked on
2026-11-13: "look into the Shadowborn items").

Source: the periodic overnight accept `fec16d7d2` (pushed `7723fa1b3`). Both games kept the same hand and
saw the same draws in old and new, and neither recovers at 4x or 16x budget (`classify_turn_later.sh`).
The deck: `decks/Shadowborn Apostle/`, ledger `docs/design/analysis-Shadowborn Apostle.md`.

## Item 1 -- s6006 gi72: the USER's Ritual-after-Stone rule costs a turn (T4 -> T5)

Key `shadowborn_overnight_d5_s6006`, gi 72, seed 6078, play depth/budget from the profile (d5 b20).

```
T1=  land Swamp; Dark Ritual; Shadowborn Apostle; Shadowborn Apostle
T2=  land Swamp; ATTACK            [opp 18]
T3=  land Swamp; ATTACK            [opp 16]
T4   old: Dark Ritual; Thrumming Stone; Shadowborn Apostle; ATTACK   [opp 0]
T4   new: Thrumming Stone; Dark Ritual; Shadowborn Apostle; ATTACK   [opp 14]
T5   new: land Swamp; ATTACK       [opp -1]
```

Suspected mechanism (UNVERIFIED): `496b4ae35` (`ShadowbornProvider::ArrangeCastOrder`, USER 2026-11-11, "Dark
Ritual after Thrumming Stone when mana allows") moves the Ritual after the Stone, so the Ritual is cast with
ripple active. The ripple reveal then changes what the Apostle cast finds (the old order kept the Ritual
off the ripple chain). Confirm with `496b4ae35^` vs `496b4ae35` on this one game, and read the two T4 logs
(`--log-dir`). The USER's rule is the USER's call (ledger line ~1512: "Ritual after stone is a bit messy. I'm
not entirely against..."). If it is the mechanism, ROOT-CAUSE it and bring back an amended rule plus its
measurement (CLAUDE.md: a USER order that measures worse gets a root cause and a proposed fix, never a
silent fallback). The Shadowborn d5 overnight cell is net -55 overall, so this is one game, not a deck verdict.

## Item 2 -- s7007 gi183: the line idles on T4 and T5 (T5 -> T7)

Key `shadowborn_overnight_d5_s7007`, gi 183, seed 7190, d5 b20.

```
T1=  land Swamp; Shadowborn Apostle
T2=  land Lake of the Dead; Diabolic Intent
T3=  land Swamp; Thrumming Stone
T4   old: land Swamp; Demonic Tutor; Shadowborn Apostle   [opp 20]
T4   new: land Swamp                                      [opp 20]
T5   old: land Swamp; ATTACK                              [opp -9]
T5   new: (nothing)                                       [opp 20]
T6   new: land Swamp; Shadowborn Apostle; Demonic Tutor; Diabolic Intent
T7   new: land Swamp; ATTACK                              [opp -7]
```

A real defect until shown otherwise: the old line cast Demonic Tutor + Apostle on T4 off the same board. Likely
suspects among the commits since 2026-11-10 that touch this board: the on-demand sacrifice payer for Lake of
the Dead (`247d61153` "Integrate Shadowborn searched picks and on-demand sacrifice payer", `faa4eca67`,
`573a4837f`, `d0879307a` Lake on-demand crack in the mana oracle, `5359ac24f`, `c1fc9063d`), the depletion tier
(`015bb79d9`, `1b87d18d7`), and the mana-oracle order-proof work (`cbe0633fe`, `7d82d19b1`). Bisect on this
one game: `git log --oneline 83e0ad041..fec16d7d2 -- src` lists the range (83e0ad041 = the 11-10 overnight
accept; its binary reproduced the old T5 line).

## How to reproduce one game

```
./build/Release/mtg <deck> --profile <profile> --depth 5 --budget-ms 20 \
    --seed <seed> --game-index <gi> --games 1 --log-dir build/scratch/sb/<arm>
```
(`python3 test/explain_game.py overnight <key> <gi> --old-bin <bin>` does the paired diff; an OLD binary needs
its own `cards.json` via `--cards-json`, since the current one carries keywords old binaries reject -- wrap it
as in `build/scratch/ovn1112/oldmtg.sh`.) NOTE: `logs/snapshots/overnight-baseline` now holds the NEW
(2026-11-12) binary, so an old-line binary must be BUILT in a scratch worktree under `.claude/worktrees/`
(remove with `scripts/safe_worktree_remove.sh`).
