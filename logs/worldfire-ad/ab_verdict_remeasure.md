# Worldfire line-keep re-measure on 9ca7deb5e (Worldfire keep table + nlesc d6b20 sidecar adopted), parent 11-26 08:09 UTC

Same 1800 Worldfire jobs as the original A/B (off vs on, 3000 std + 3000 2HG per arm), binary = wip/land-1126 at 69380de37, run from the land-1126 tree.

```
shape                       n    net  slow  fast       t    units      cpu
worldfire2hgplay         3000     +1     2     2   +0.38   +0.12%   +0.81%
worldfireplay            3000     +0     1     1   +0.00   +0.10%   +0.66%
TOTAL                    6000     +1     3     3           +0.11%   +0.74%
SLOWER worldfire2hgplay gi=2272 off=4 on=5
SLOWER worldfire2hgplay gi=2918 off=4 on=6
SLOWER worldfireplay gi=1347 off=4 on=5
```

Slower games at d8 b0 (ignore_play_profile, abandon_units 30M): all recover, off = on = T4 on each.
```
on__worldfire2hgplay__gi2272: played=1 avg=4.0000 digest=bf47b5ce78ef0725
off__worldfire2hgplay__gi2272: played=1 avg=4.0000 digest=bf47b5ce78ef0725
off__worldfire2hgplay__gi2918: played=1 avg=4.0000 digest=facff87b3ed15942
on__worldfire2hgplay__gi2918: played=1 avg=4.0000 digest=facff87b3ed15942
on__worldfireplay__gi1347: played=1 avg=4.0000 digest=5c5b7451fedb4916
off__worldfireplay__gi1347: played=1 avg=4.0000 digest=5c5b7451fedb4916
```

Verdict: PASS (net +1 of 6000 is seed noise at t=+0.38; every slower game recovers at d8 b0). Lands with land-1126.
