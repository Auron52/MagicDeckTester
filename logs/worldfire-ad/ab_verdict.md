# Worldfire line-keep fix: play A/B verdict (parent session, 11-26 05:20 UTC)

Pooled A/B, seed base 4870000, all-off vs all-on (`MTG_LINE_KEEP_CRIT_MULTI`, `_SUSPEND`, `_RITUAL`; RITUAL_TIGHT is
the shipped default), at each deck's play settings, standard and 2HG. Binary: build/scratch/worldfire-ad/bin_m/mtg.
Full table: ab_table.txt.

- TOTAL: 43,200 paired games, net -2 (7 slower, 9 faster), units +0.12%, CPU +0.30%.
- Worldfire: standard net -2 (2/4) at units +0.19%; 2HG net 0 (3/3) at units +0.46%.
- Every other deck is 0 except Cyclone (0: 1/1), Selesnya v2 (+1: 1/0) and Mirrorwing 2HG (-1: 0/1).

## Slower games (7) at --depth 8 --budget-ms 0 (abandon 30M units); off -> on

| game | A/B off -> on | d8 b0 off / on | verdict |
|---|---|---|---|
| cycloneplay gi322 | 4 -> 5 | 4 / 4 (same digest) | recovers |
| worldfire2hgplay gi1247 | 5 -> 6 | 5 / 5 (same digest) | recovers |
| worldfire2hgplay gi1912 | 5 -> 6 | 5 / 5 | recovers |
| worldfire2hgplay gi1927 | 4 -> 5 | 4 / 4 (same digest) | recovers |
| worldfireplay gi961 | 6 -> 7 | 6 / 6 (same digest) | recovers |
| worldfireplay gi1347 | 4 -> 5 | 4 / 4 (same digest) | recovers |
| selesnyav2play gi178 | 4 -> 5 | 4 / 5 | does not recover; MTG_NC_SEARCH=1 at play settings: off 5 / on 5, so clairvoyant |

Verdict: passes the adoption bar (net <= 0 at play settings, no real slower game, CPU cost +0.3%). Ready to land with GT.
