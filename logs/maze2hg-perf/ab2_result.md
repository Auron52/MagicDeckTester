# Maze three-arm A/B (ab2, bin_fold = c8a895804), parent session 11-26 07:53 UTC

Gods fold VERIFY (60 std + 60 2HG, f1 arm): folds=718047 skipped_crossings=262700 violations=0.

## Fold on vs off (f1 vs f0): must be identical
```
A_c000_mazesend_h1_b20                 n= 600 net=  +0 faster=  0 slower=  0 units 1.000 cpu   2910.9 ->   2931.1 (1.007) dig SAME
A_c001_mazesend2hg_h2_b20              n= 600 net=  +0 faster=  0 slower=  0 units 1.000 cpu   2764.2 ->   2771.2 (1.003) dig SAME
A_c015_extraturns2hg_h2_b20            n= 600 net=  +0 faster=  0 slower=  0 units 1.000 cpu   1753.0 ->   1767.1 (1.008) dig SAME
A_c016_gods_h1_b20                     n= 600 net=  +0 faster=  0 slower=  0 units 1.000 cpu   7235.8 ->   7284.3 (1.007) dig SAME
A_c017_gods2hg_h2_b20                  n= 600 net=  +0 faster=  0 slower=  0 units 1.000 cpu   7949.8 ->   7692.6 (0.968) dig SAME
A_c034_shadowborn_h1_b20               n= 600 net=  +0 faster=  0 slower=  0 units 1.000 cpu    861.5 ->    858.7 (0.997) dig SAME
A_c052_antilife2hg_h2_b20              n= 600 net=  +0 faster=  0 slower=  0 units 1.000 cpu    105.5 ->     97.5 (0.924) dig SAME
A_c065_creature_h2_b30                 n= 600 net=  +0 faster=  0 slower=  0 units 1.000 cpu    507.9 ->    503.2 (0.991) dig SAME
A_c077_elementals2hg_h2_b20            n= 600 net=  +0 faster=  0 slower=  0 units 1.000 cpu   3023.7 ->   2844.8 (0.941) dig SAME
A_c102_elementals_h1_b20               n= 600 net=  +0 faster=  0 slower=  0 units 1.000 cpu    832.4 ->    821.7 (0.987) dig SAME
A_c103_martyr_h1_b20                   n= 600 net=  +0 faster=  0 slower=  0 units 1.000 cpu    629.3 ->    620.7 (0.986) dig SAME
A_c104_martyr2hg_h2_b20                n= 600 net=  +0 faster=  0 slower=  0 units 1.000 cpu    821.5 ->    826.0 (1.005) dig SAME

per deck (all formats): net turns, games, units on/off, cpu on/off
  extraturns2hg    net=  +0 n=  600 units 1.000 cpu 1.008 (1753s -> 1767s)
  gods             net=  +0 n=  600 units 1.000 cpu 1.007 (7236s -> 7284s)
  gods2hg          net=  +0 n=  600 units 1.000 cpu 0.968 (7950s -> 7693s)

slower games (shape, gi, off_turn, on_turn):
```

## Price on top of fold (on vs f1)
```
A_c000_mazesend_h1_b20                 n= 600 net=  +0 faster=  0 slower=  0 units 1.003 cpu   2931.1 ->   2748.0 (0.938) dig DIFF
A_c001_mazesend2hg_h2_b20              n= 600 net=  +0 faster=  0 slower=  0 units 1.009 cpu   2771.2 ->   2772.1 (1.000) dig DIFF
A_c015_extraturns2hg_h2_b20            n= 600 net=  +0 faster=  0 slower=  0 units 1.000 cpu   1767.1 ->   1808.2 (1.023) dig SAME
A_c016_gods_h1_b20                     n= 600 net= +19 faster= 12 slower= 24 units 0.881 cpu   7284.3 ->   5030.4 (0.691) dig DIFF
A_c017_gods2hg_h2_b20                  n= 600 net= +24 faster= 14 slower= 39 units 0.925 cpu   7692.6 ->   6349.1 (0.825) dig DIFF
A_c034_shadowborn_h1_b20               n= 600 net=  +0 faster=  0 slower=  0 units 1.000 cpu    858.7 ->    856.8 (0.998) dig SAME
A_c052_antilife2hg_h2_b20              n= 600 net=  +0 faster=  0 slower=  0 units 1.000 cpu     97.5 ->     98.8 (1.013) dig SAME
A_c065_creature_h2_b30                 n= 600 net=  +0 faster=  0 slower=  0 units 1.000 cpu    503.2 ->    501.4 (0.997) dig SAME
A_c077_elementals2hg_h2_b20            n= 600 net=  +0 faster=  0 slower=  0 units 1.002 cpu   2844.8 ->   2825.3 (0.993) dig SAME
A_c102_elementals_h1_b20               n= 600 net=  +0 faster=  0 slower=  0 units 1.001 cpu    821.7 ->    840.7 (1.023) dig SAME
A_c103_martyr_h1_b20                   n= 600 net=  +0 faster=  0 slower=  0 units 1.001 cpu    620.7 ->    618.8 (0.997) dig SAME
A_c104_martyr2hg_h2_b20                n= 600 net=  +0 faster=  0 slower=  0 units 1.001 cpu    826.0 ->    829.2 (1.004) dig SAME

per deck (all formats): net turns, games, units on/off, cpu on/off
  elementals       net=  +0 n=  600 units 1.001 cpu 1.023 (822s -> 841s)
  elementals2hg    net=  +0 n=  600 units 1.002 cpu 0.993 (2845s -> 2825s)
  gods             net= +19 n=  600 units 0.881 cpu 0.691 (7284s -> 5030s)
  gods2hg          net= +24 n=  600 units 0.925 cpu 0.825 (7693s -> 6349s)
  martyr           net=  +0 n=  600 units 1.001 cpu 0.997 (621s -> 619s)
  martyr2hg        net=  +0 n=  600 units 1.001 cpu 1.004 (826s -> 829s)
  mazesend         net=  +0 n=  600 units 1.003 cpu 0.938 (2931s -> 2748s)
  mazesend2hg      net=  +0 n=  600 units 1.009 cpu 1.000 (2771s -> 2772s)

slower games (shape, gi, off_turn, on_turn):
   A_c016_gods_h1_b20 20 6 7
   A_c016_gods_h1_b20 28 6 7
   A_c016_gods_h1_b20 43 5 6
   A_c016_gods_h1_b20 76 5 6
   A_c016_gods_h1_b20 112 6 8
   A_c016_gods_h1_b20 189 5 6
   A_c016_gods_h1_b20 197 5 6
   A_c016_gods_h1_b20 203 5 6
   A_c016_gods_h1_b20 214 5 6
   A_c016_gods_h1_b20 223 6 7
   A_c016_gods_h1_b20 248 5 7
   A_c016_gods_h1_b20 296 6 7
   A_c016_gods_h1_b20 305 6 8
   A_c016_gods_h1_b20 335 6 8
   A_c016_gods_h1_b20 344 7 9
   A_c016_gods_h1_b20 387 5 6
   A_c016_gods_h1_b20 412 6 7
   A_c016_gods_h1_b20 437 6 7
   A_c016_gods_h1_b20 459 5 7
   A_c016_gods_h1_b20 506 5 7
   A_c016_gods_h1_b20 551 6 9
   A_c016_gods_h1_b20 565 5 6
   A_c016_gods_h1_b20 568 7 9
   A_c016_gods_h1_b20 576 5 6
   A_c017_gods2hg_h2_b20 30 6 8
   A_c017_gods2hg_h2_b20 50 6 7
   A_c017_gods2hg_h2_b20 51 6 7
   A_c017_gods2hg_h2_b20 95 7 8
   A_c017_gods2hg_h2_b20 102 6 7
   A_c017_gods2hg_h2_b20 115 7 8
   A_c017_gods2hg_h2_b20 147 6 7
   A_c017_gods2hg_h2_b20 151 5 6
   A_c017_gods2hg_h2_b20 196 5 6
   A_c017_gods2hg_h2_b20 205 4 5
   A_c017_gods2hg_h2_b20 210 5 6
   A_c017_gods2hg_h2_b20 234 6 7
   A_c017_gods2hg_h2_b20 241 5 6
   A_c017_gods2hg_h2_b20 254 6 8
   A_c017_gods2hg_h2_b20 255 5 6
   A_c017_gods2hg_h2_b20 260 7 8
   A_c017_gods2hg_h2_b20 269 4 5
   A_c017_gods2hg_h2_b20 283 4 5
   A_c017_gods2hg_h2_b20 297 5 6
   A_c017_gods2hg_h2_b20 300 5 6
   A_c017_gods2hg_h2_b20 338 6 8
   A_c017_gods2hg_h2_b20 345 7 8
   A_c017_gods2hg_h2_b20 369 6 8
   A_c017_gods2hg_h2_b20 379 5 6
   A_c017_gods2hg_h2_b20 381 7 9
   A_c017_gods2hg_h2_b20 387 4 5
   A_c017_gods2hg_h2_b20 425 5 6
   A_c017_gods2hg_h2_b20 439 5 6
   A_c017_gods2hg_h2_b20 442 5 7
   A_c017_gods2hg_h2_b20 448 7 8
   A_c017_gods2hg_h2_b20 500 8 9
   A_c017_gods2hg_h2_b20 510 6 7
   A_c017_gods2hg_h2_b20 511 4 5
   A_c017_gods2hg_h2_b20 527 4 5
   A_c017_gods2hg_h2_b20 542 6 7
   A_c017_gods2hg_h2_b20 549 5 6
   A_c017_gods2hg_h2_b20 572 6 7
   A_c017_gods2hg_h2_b20 577 6 7
   A_c017_gods2hg_h2_b20 582 6 8
```
