# MTG_REAL_PAY_SCRIPT safe-hook work: status (2026-10-31, branch `wip/realpay-safe`)

Checkpoint for the d0 defects the regression tier showed with the script ON. The script stays DEFAULT OFF.

## Breaching s2023 gi21 (d0, T4 win -> unwon): root-caused and fixed

* **It was not the script's payment.** With `MTG_REAL_PAY_TRACE=1`, no script is installed in this game
  (`PlanPaymentScript` returns false). The defect is a side effect of its probe. The probe applies the plan on a
  copy (`EngineApply`), and that apply fills the per-game **mana cache** (`g_mana_cache`, SpellEffects.cpp).
* **Mechanism: a hole in the mana cache.** `MTG_RAMP_FEED_BRANCH` runs the tap backtracker twice. The first pass
  feeds each ramp filter by a fixed pick. When that pass fails after meeting a pick with an alternative, it sets
  `g_bt_ramp_feed_alt`, and the public entry then runs the widened pass. The first pass's NEGATIVE result was
  cached, but a cache hit returned before any DFS ran, so the flag stayed clear and the widened pass never ran.
  Result: on a miss the payment pays; on every later hit of the same board it fails. Protege's {4}{U}{U} (paid
  from UURR floating + Mossfire Valley + Ferrous Lake) pays only through the widened pass. The executor's payment
  hit the negative the probe had stored, and the cast failed on every retry.
* **Fix (`MTG_MC_FEED_ALT`, DEFAULT ON, `=0` legacy):** a cache entry records the flag (`feed_alt`) and a negative
  hit replays it. `MTG_MANA_CACHE_VERIFY` now checks the flag too and no longer perturbs it. gi21: script ON T4
  (was unwon). Breaching regression gi706: script ON 6 -> 5 (now equal to OFF).
* **Search has the same hole.** The search repeats boards, so it saw these payments as unpayable after the first
  visit. That means the fix CHANGES DEFAULT (searched) PLAY on ramp-filter decks (Breaching, Niv, TreasureHunt,
  Bruna, Hinata2). **Not yet measured**: smoke has not been run on this tree.

## SAFE-hook hardening (also in this commit)

* `ScriptedPay` (safe): a scripted tap that SACRIFICES used to be refused after its sacrifice, and the board was
  not put back. Now the board is restored whole (a GameState copy) when the battlefield changed, and a sacrificing
  entry is served like any other entry.
* `ScriptedTapAhead` (safe): this path used to set `ended`, which made every later payment fail. It now gives up
  instead (board restored) and returns false, and the caller runs the engine's tap-ahead.
* `MTG_REAL_PAY_TRACE=1` adds `[rp-script]` / `[rp-pay]` dig lines.

## Still open

* **Alchemist d0 rows** (smoke gi16 7->8, gi818 4->6, gi977 8->9; regression gi546 6->7) are still worse with the
  script ON. This is also a probe side effect, not a payment. In gi818 T4, the probe's apply runs the
  AccomplishedAlchemist combo-route walk (`aaroute::Trial`) and memoises its verdict per board
  (`aaroute::TheMemo`). That walk enumerated a different menu (extra "(pre-tap)" setups) and concluded
  `TRIAL no`. The real executor then reused that verdict and never went off; with the script OFF the walk wins.
  The context difference is not yet identified. Suspects: the executor's pin scopes (ScriptedTutor,
  PlanPaymentPinScope, ...) that are live around the probe (`AIEngine.cpp` ~4600-4635), or a menu read that
  differs between the probe and the executor.
* Searched (d>0) play can hit both mechanisms too. The mana-cache hole was live in search; the probe's memo
  leaks are diluted there, but they exist.

## Next steps

1. Alchemist gi818 (`--seed 1819 --game-index 818 --depth 0 --budget-ms 0 --ignore-play-profile`): diff the route walk's menu between the probe and the executor (`MTG_AA_ROUTE_TRACE=1`).
   Name the context the menu reads, and either key the memo on it or run the probe outside the executor's scopes.
2. Unit tests + `bash test/scenarios.sh` (new fixture `breaching_script_ramp_feed_hit.json`).
3. Measure `MTG_MC_FEED_ALT` on default play: `MTG_PGO=0 THREADS=8 bash test/regression.sh --smoke`. If it moves,
   run a held-out A/B on the ramp-filter decks and take it to the USER (it is a default-play change).
4. Then the requested pooled A/B (seed 1650001, `build/scratch/script_ab/manifest.json` plus d0 arms).
