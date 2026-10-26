# Anti-Lifegain keep table regenerated 2026-10-25 -- REJECTED, kept for a possible larger test

Artifact queue job anti_lifegain (worker MagicDeckTester2-76d2cc), `mullgen.sh run decks/Anti-Lifegain complete`
(R=40) frozen at ae41aaad0's predecessor run. Old-vs-new versus A/B, 3 rounds x 32 seeds x 1000 games: new is
+0.005021 t WORSE but 0.950x the work units (se 0.006) -- a trade. USER 2026-10-26: leave it out unless it does
better than that on a larger set. To test: gunzip this beside the decklist as the candidate and run the versus A/B
(test/keepmodel_exhaustive_ab.sh KM_MODE=versus) on more seeds against the shipped table.
