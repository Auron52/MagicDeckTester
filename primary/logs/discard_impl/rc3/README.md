# ab3 slower-game root-cause kit (2026-10-10)

ab3 = ONE pooled A/B of the 2026-10-10 bug-fix round, binary `logs/discard_impl/ab3/mtg_ab3_d6a760e5`
(PGO, src d6a760e5; byte-identical to Release). Arms: `new` = defaults; `old` = every FIX lever =0:
  MTG_DRAGONSTORM_ORDER_OPAQUE MTG_PAYABLE_ORDER_RITUALS MTG_COLOR_EXACT_SCALED MTG_ATTACK_TAP_NEED_EXACT
  MTG_ATTACK_TAP_ABILITY_DISCOUNT MTG_ROLLOUT_LAND_ACT_COLOR MTG_SOLVE_VERIFIED_WIN MTG_EDF_GORGE_RED_SIZING
  MTG_SOLVE_PENDING_AURA_EXACT
(old = the committed GT behaviour before the round). Each lever is also an env var (=0 / =1).
What each fixes: docs/design/discard-round-play-defects-2026-10-10.md (s1 COLOR_EXACT_SCALED, s2 ATTACK_TAP_*,
s3 SOLVE_VERIFIED_WIN + EDF_GORGE_RED_SIZING, s4 SOLVE_PENDING_AURA_EXACT, s6 ROLLOUT_LAND_ACT_COLOR) and
docs/design/executor-ritual-payable-order.md (ORDER_OPAQUE, PAYABLE_ORDER_RITUALS).

Slower games (new slower than old): logs/discard_impl/rc3/slower_<deck>.txt, format
  <deck> <cell> gi=<g> old=<turns> new=<turns> seed=<S>      (unwon = 9)

REPRO of one game (from the repo root /workspaces/MagicDeckTester; deck/profile from test/regression_cases.sh
DECK_FILE / DECK_PROF, edf = decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.{cod,profile.json}):
  BIN=logs/discard_impl/ab3/mtg_ab3_d6a760e5
  cell d0  : $BIN <deck> --profile <prof> --seed S --game-index g --games 1 --depth 0 --ignore-play-profile
  cell d3  : ... --depth 3 --budget-ms 10 --ignore-play-profile
  cell ship: ... --budget-ms 20            (the profile's value_play depth; NO --depth)
  cell 2hg : a one-job manifest via `$BIN --batch m.json --threads 1`:
             {"jobs":[{"name":"x","deck":..,"profile":..,"games":1,"seed":S,"game_index":g,"budget_ms":10,
               "depth":3,"ignore_play_profile":true,"starting_life":30,"opponent_heads":2}]}
  old arm  : prefix the env with every lever above =0.   Add --log-dir <dir> for JSON game logs.
Bisect a slower game by turning levers back on ONE at a time on the old arm (or off one at a time on new).
Current-tree binary (adds Giants rock rule, Anti-Lifegain tutor move, Dragonstorm USER order
MTG_DRAGONSTORM_USER_ORDER default ON): logs/discard_impl/rc3/mtg_head_ccacda4e.
