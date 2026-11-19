# Goblins mana oracle: the eat-ORDER fold (2026-11-19)

Branch `wip/gob-eat-order` (base `86ec7c2f4`, which holds the goal fold). Follows
`goblins-oracle-eat-fold-2026-11-19.md` (MTG_ORACLE_GOAL_FOLD) and `goblins-sac-mana-oracle-2026-11-18.md`.
Analysis-only: the oracle never feeds play.

## 1. The problem

After the goal fold, decision s30131075 (gi 131075) T2 main 2 still needed ~14.1M applies / 965 s unbounded against a
600-priced-second decision budget that covers ~6.6M. Its 446 plans cast Bolt / King / Matron / Chieftain and shoot
Siege-Gang with every land tapped, so every payment eats bodies through Skirk Prospector, and the payments eat the
SAME bodies in every ORDER: a line eating a token for the first payment and a Piledriver for the second is enumerated
beside the one eating them the other way round. The frontier prune merges the two only after both are applied.

## 2. The fold (MTG_ORACLE_EAT_ORDER_FOLD, default ON)

Code: `src/ai/ManaOracle.cpp`, block "THE EAT-ORDER FOLD" above `Realize`; card premises in
`src/cards/CardDatabase.cpp` (`eat_body_audited`, `eat_item_audited`, `eat_perm_audited`).

**Window.** A run of consecutive payments of one realisation such that between any two of them nothing can tell which
audited bodies are alive -- only how many are.

**Exchange.** Inside a window, a line that eats a body of fodder class D at payment j and a body of class C at a later
payment j1, where C still had an uneaten copy at j, is the same line as the one eating C at j and D at j1:
- both payments make the same mana (one {R} per body through the same outlet), so both stay legal and minimal;
- every event between j and j1 cannot tell the two bodies apart (the window's definition);
- from j1 on, the two boards hold the same permanents: the table eats a class's copies in battlefield order (the
  earliest entered first) and copies released later are appended, so the copies eaten per class are the first n of
  that class whatever the timing.
Repeated exchanges reach a CANONICAL order (each raises sum(rank x payment); the space is finite): no payment eats a
class C after an earlier payment of the window ate a class ranked above C while C still had an uneaten copy (rank =
FNV hash of the class key, a fixed total order). The fold carries per script the classes the canonical order forbids
from now on (`PayOption::eat_forbid`) and leaves them out of the frontier's payment table (`t_eat_forbid` in
`BuildSources`). Every line has a canonical twin with the same end board, line and resources, so nothing findable is
lost, at any budget.

**What closes a window / what is never exchanged** (per card, from cards.json; the whitelists are raw-JSON keys, so a
param added tomorrow is unaudited):
- Exchanged bodies (`eat_body_audited`): no static, no mana or {T} ability, no death / leave trigger, nothing attached,
  not paired, not named by the plan (KeyedApart: the activation's source and baked victim are their own class), not
  the outlet. Goblin Lackey and Goblin Piledriver (combat-only triggers), Muxus and Goblin Matron and Siege-Gang
  Commander (enter triggers are spent before the body can be eaten; Siege-Gang's outlet is used only by a plan action,
  which names its source), Goblin Chainwhirler, Twinshot Sniper, Stingscourger (echo / channel are upkeep / hand),
  generated vanilla tokens. NOT exchanged: Goblin King, Goblin Chieftain, Goblin Warchief, Rundvelt Hordemaster
  (lords / haste grant / cost reducer: their presence is readable), Skirk Prospector (the outlet), Mogg War Marshal
  and Pashalik Mons (death triggers), Krenko (a {T} ability). A non-exchanged body keeps its payment.
- The gap after a payment must be the resolution of an audited ITEM (`eat_item_audited`): a creature spell whose
  statics read nothing and start at the same moment on both sides (King, Chieftain, Warchief: lords, haste, a Goblin
  cost reducer; no audited event reads power, toughness or haste, and a lord entering never lowers a toughness),
  whose enter trigger reads nothing (Siege-Gang's three vanilla tokens off the shared counter, Chainwhirler's ping of
  the opponent), a second Skirk, or a burn spell while the opponent has no creature or planeswalker to target (Lightning
  Bolt); or a damage outlet's activation (Siege-Gang: face damage only) whose baked victim is still alive (a missing
  TOKEN victim is re-picked off the live board, ApplySacCreatureOutlet). NOT items: Goblin Matron (its tutor ranks the
  live board), Muxus (its reveal can put any library Goblin), Twinshot Sniper (a targeted ping), Mogg War Marshal,
  Pashalik, Rundvelt, Krenko.
- Every permanent of ours on the board at the gap is `eat_perm_audited`: no watcher of OTHER deaths (Pashalik,
  Rundvelt), no enter / cast watcher; a self-only death trigger (Mogg War Marshal) is fine because such a body is never
  exchanged and dies at the same payment on both sides; Three Tree City's mana is a COUNT of Goblins, equal on both
  sides.
- No continuation applied before the next payment (`cont_sig`: a continuation is solved on the board), no batch /
  tap-ahead / cast-sacrifice / combat-hold payment.
- The plan has no action without a coloured payment (`EatPlanOk`): a free action (Vial, Krenko's {T}, a {0}
  activation) would sit unseen between two payments.
- The goal fold's deck premises (`GoalFoldAdmissible`: no graveyard reader, no same-phase untapper, no copy / rename /
  steal / transform), whether or not the goal fold itself is on.

**Frontier prune.** A frontier with a non-empty forbidden set merges only with an identical one (same core, same
forbidden set, same resources: both hashed into the prune key). A frontier that covers it strictly may have a different
history in the window, and the canonical twin of a line through that one can change its history, so dominance is not
sound there. With an empty set no exchange reaches before the frontier, and the prune is the usual one.

**Side channels** (`AuditPlan`):
- a frontier's table is restricted only under the goal fold's unscripted guard (`goal_cands_scripted`), so no
  unscripted outcome can hide below a skipped option;
- a folded realisation that does not cover the engine's board is realised again without the fold (its ORACLE-GAP /
  UNMODELED / CONTINUATION-DIFFERS texts are the unfolded ones);
- a folded realisation that would report a DOMINATED-* is realised again without the fold too: that text names the
  last strictly-better outcome's script, and the fold keeps a different representative of an order class;
- a frontier whose table the restriction emptied is not expanded (every line through it has a canonical twin);
- the apply notes of skipped scripts feed only the UNMODELED census on a covered plan (as with the goal fold).

`MTG_ORACLE_EAT_ORDER_FOLD_CHECK=1` (abort) / `=2` (count): every folded plan is realised without the fold too, and a
mismatch is any difference in cover, a DOMINATED-* on the unfolded one, on-key outcome resource sets that do not cover
each other both ways, or an unscripted outcome in the unfolded one. The unfolded realisation is the one judged.

## 3. Measurements

(see below)
