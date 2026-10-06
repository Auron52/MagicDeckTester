# The reference reproducibility gate is RED on `origin/phase-1-2-deck-analyzer`

## UPDATE 2026-10-06 — sweep at cf7c95aa: 4 non-replaying + 6 never-replayed; 3 checker fixes

Full `--strict` sweep at `cf7c95aa` (472 refs, incl. the new Fungus/Snow/WhiteKnights saves):
`102 ok, 366 repaired, 2 play-drift, 1 shuffle-dead, 1 board-diverged, 0 contract-fail` — and the six
Soldiers references were not in it at all. After the fixes below: `103 ok, 368 repaired,
1 play-drift, 0 shuffle-dead, 0 board-diverged` — only the four targeted references moved.

* **Soldiers ×6 — never replayed (resolver bug).** The list ships as `decks/Soldiers/soldiers.cod`;
  the user's Windows viewer matches the stem case-insensitively, every Linux resolver
  (`deck_registry.discover`, this checker, `server.js listDecks/resolveDeck`) did not. All four now
  match the stem case-insensitively (`test/deck_case_resolution_check.py`). All six replay `ok`.
* **Angels `s5_gi4` — play-drift, CHECKER.** The T4 `--tap-pref` omitted Sol Ring because it was
  cast in the same line (pins were restricted to first-frame permanents). A pin outranks every
  unpinned source, so both Plains paid Youthful Valkyrie and Giada (Angel-only `{W}`) paid the
  Inquisitor and could not attack: T4 → T5. A permanent cast from the first frame's HAND and tapped
  by the second is now pinned (a land fetched tapped from the library still is not). `ok`.
* **StompySurprise v1 `s11_gi10` — board-diverged, CHECKER (not a re-save).** Ordinal-keyed pins
  used the RECORDING's `main_ordinal`; the recorded T4 frame after Natural Order (which offered an
  unpayable Worldly Tutor) is now pass-only and takes no ordinal, so every later pin addressed the
  wrong frame and T6's Turntimber Symbiosis payment ran unpinned (Lodge tapped as a land). The walk
  now learns recorded → replay ordinals as it aligns frames (`OrdinalMap`) and rewrites
  `--tap-pref` / `--cast-order` / `--full-enum` keys. `repaired`, T6 = T6.
* **Mirrorwing v1 `s29_gi28` — "shuffle-dead", CHECKER.** Not a reshuffle: the T4 summary gained a
  per-copy label (`→ Zada, Hedron Grinder #1`), the exact-summary tier missed, and the
  order-insensitive (land, casts) tier took `Expedite, Ignoble Hierarch` for the recorded
  `Ignoble Hierarch, Expedite` — Zada had no second creature, the copy's draw never happened.
  New ordered copy-label tier + recorded-cast-order narrowing. `repaired`, T5 = T5.
* **Mirrorwing v2 `s51_gi50` — unchanged**, root-caused below (payment heuristic,
  `MTG_ATTACK_BODY_TAP_ORDER=1` replays it at T4); awaits the held-out A/B + user sign-off.
* `--strict` now also FAILS on BOARD-DIVERGED (identical hand ⇒ no reshuffle ⇒ something diverged).

## UPDATE 2026-10-05 (later) — all three now REPLAY

* **Snow `s4_gi3` — FIXED (engine, human play only).** The cause was narrower than "needs-based
  generic payment": `SnowProvider::ManaSourceRank`'s Scrying Sheets hold judged affordability on the
  board BEFORE the payment (spare 3 ≥ dig 2, so it held the second Sheets at rank 60) and the dig's
  `{1}{S}` took both Islands — yet spare AFTER the payment was 0, so the held Sheets could never dig
  again. The hold now subtracts what the payment still owes (`g_pay_remaining_mv`). Replays `ok`.
* **Mirrorwing v2 `s51_gi50` — replays at T4 under `MTG_ATTACK_BODY_TAP_ORDER=1`** (lowest-power
  mana creature taps first on an attack turn; 21 damage). Default OFF until the A/B in
  `test/mana_tap_order_ab.sh` clears the adoption bar; the gate at default still reports it as the
  one play-drift until then.
* The generalised needs-based choice (`MTG_NEEDS_TAP_ORDER`) is built alongside; design and status
  in `mana-payment-rollback.md`.

## UPDATE 2026-10-05 — all three ROOT-CAUSED; one fixed, two owed a payment heuristic

Found by following each recorded line to the frame where the engine refuses it (no bisect needed).
**None of the three is from the 10-01..10-04 executor block named under "Suspects" below.** All
predate it, and the `f20b2f88` "reference gate pass" was measured before that commit was rebased
onto `68a21c31`, the first commit to gate archived-list references at all.

* **Hinata2 `s1_gi0` — FIXED (checker).** The recorded 5-target Soulfire option is still offered
  verbatim (option 15). `58b3a7e8` (10-01) unified the protocol's P/T label sites on
  `ProtocolPTLabel`, so the label went from `Hinata, Dawn-Crowned (4/4, yours)` to
  `… (4/4) (yours)`; `_LABEL_VOLATILE` stripped one trailing group, left `(4/4)`, and reported an
  ENUM-GAP. The normaliser now strips repeatedly. Full sweep after: `74 ok, 365 repaired,
  1 play-drift, 1 shuffle-dead, 1 board-diverged, 0 enum-gap` — only Hinata2 moved.
* **Mirrorwing v2 `s51_gi50` — payment heuristic (play-drift).** T4 pays Luxurious Libation X=5;
  every creature then gets +5/+5 via Mirrorwing copies and attacks. The 08-29 engine tapped one
  Elvish Mystic + one Ignoble Hierarch, leaving Mystic 6 + Hierarch 5 + Dragon 9 = **20, lethal**.
  HEAD taps **both Mystics** (the scarcity-first rank spends the mono-G source before the tri-colour
  one), leaving 5 + 5 + 9 = **19**. The reference records no `--tap-pref`, so the human never chose
  these taps — the 08-29 split was luck, not a rule. No existing lever (`MTG_DORK_TAP_LAST`,
  `MTG_SCALER_PLAN_BIAS`, `MTG_PUMP_TARGET_HOLD`, `MTG_ONESHOT_RESERVE`, `MTG_M2_PAYLOAD_RESERVE`)
  changes it. Owed: on an attack turn, among creatures that CAN tap for mana, tap the ones that will
  not attack first, then the lowest attack value; colour flexibility only as a needs-based tiebreak.
* **Snow `s4_gi3` — payment heuristic (board-diverged); has NEVER replayed** (fails at 09-06, at
  `cac41b5f` which added it, and at HEAD). T5: the human activated the new Scrying Sheets #38 and
  paid `{1}{S}` with Sheets #39's `{C}` + one Island, keeping Island #56 for the Frost Augur the dig
  found. The engine activates #39 and pays with both Islands, stranding `{U}`. Needs-based generic
  payment — see `mana-payment-rollback.md`, which uses this game as its worked example.

**Status: OPEN defect, found 2026-10-04, NOT introduced by the finder.** Four of 442 saved
references no longer replay. One of them is a **play-drift**, which the standing user rule treats as
an engine bug rather than an aged-out reference.

## What fails

```
play-drift      Mirrorwing_Dragon/v2-instigator-entrance/claude_s51_gi50.json
                replay won=True win_turn=5  vs  ref won=True win_turn=4
                (9 decisions the ref predates answered by engine default)

ENUM-GAP        Hinata2/claude_s1_gi0.json
                recorded option ('label', ('Opponent (face)', 'You (face)', '1/1 Spirit Token',
                '1/1 Spirit Token', 'Hinata, Dawn-Crowned')) no longer offered at
                ('target', 6, None, 'Soulfire Eruption')  (noptions 16->16)
                "also absent with the recorded line PINNED by name, so this is the enumeration
                 and not the plan-space bound"

BOARD-DIVERGED  Snow/claude_s4_gi3.json
                recorded plan 'land=none; cast: Frost Augur' no longer enumerated at
                ('main_phase', 5, 'pre_main', None)  (nplans 2->1); hand identical, board differs
                (TAP STATE only): ref-only ['Snow-Covered Island'] vs now ['Scrying Sheets'];
                first divergence at ('dig', 5, None, 'Scrying Sheets')

shuffle-dead    Mirrorwing_Dragon/v1-twinflame-anger/claude_s29_gi28.json
                (the ACCEPTED class -- restore only by re-playing by hand; does not gate)
```

Tally: `74 ok, 364 repaired, 1 play-drift, 1 shuffle-dead, 1 board-diverged, 1 enum-gap (442 refs)`.
`--strict` fails on play-drift and ENUM-GAP, so the regression tier returns non-zero.

## It predates the branch's local work — measured, not argued

A clean `./build.sh` of **pure `origin/phase-1-2-deck-analyzer`** in a scratch worktree reproduces
the **identical** four references in the identical classes, with the identical tally. The
`references/` tree is byte-identical between that worktree and the integrated one
(`git rev-parse HEAD:references` matches), and the local commits touch nothing under `test/` except
one new unit-test file. So the only variable was the engine, and the engine that fails is origin's.

Reproduce:
```bash
git worktree add /tmp/origin-pure --detach origin/phase-1-2-deck-analyzer
cd /tmp/origin-pure && ./build.sh
MTG_BIN=/tmp/origin-pure/build/Release/mtg python3 test/viewer_protocol_check.py --strict --threads 24
```

## WHY IT WAS MISSED, which is the reusable part

**The reference gate runs in REGRESSION MODE ONLY.** `test/regression.sh` sets `VPC_RUN=1` only for
`MODE = regression` (`VPC_ALWAYS=1` forces it, `VPC_SKIP=1` suppresses it) — a deliberate 2026-08-17
user decision, because each replay is a `--claude-play` process with `MTG_UNPRUNED` set and running
it in all three tiers re-measured the same corpus three times.

The consequence is that **smoke is not a sufficient gate for anything that can move play.** Smoke
checks fingerprints; it never replays a human game. A change pushed on a green smoke, or on green
CI (which builds and runs unit tests + a determinism sample, not the reference corpus), cannot
reveal a reference drift. The repo's own `regression-cadence` rule — smoke AND regression before
commit — exists for exactly this, and the gap this file records is what skipping the second half
looks like.

## What is owed

* **The play-drift needs an engine root-cause.** The standing user rule (2026-08-27): *"repairing it
  is always acceptable, but the win turn must not change. If it does, there is something wrong"*, and
  *"things like 'draws diverged' are not going to cut it"*. A reference is hand-played, user-owned
  ground truth; losing its recorded win turn means realized play got **worse**. Do not re-save it.
  The sole exception is a recorded line that rode an engine bug, and that must be PROVEN to the user
  and signed off, never silently accepted.
* **The ENUM-GAP needs investigating before anything is re-saved** — the harness says so itself: the
  hand is identical yet a previously-offered plan is gone, i.e. the engine changed under the same
  state. The check already rules out the plan-space bound by pinning the recorded line by name.
* **BOARD-DIVERGED is tap state only**, so it is likely a payment/ordering divergence rather than a
  decision change — cheapest of the three to chase, and a plausible shared cause with the Snow
  work on this branch (`6fb10c80` play main 2 only on turns Jorn attacked, `9dc0a9b2` the MDFC as
  Jorn, `b01edca9` never enumerate Skred, `ecb422af` short-circuit UntapSecondMainLive). That is a
  LEAD, not an attribution — none of it has been bisected.

## Suspects worth bisecting first

The incoming work most likely to touch these three decks is the executor/search block on this
branch: `2e85e9fa` (finish a game the search has proven unwinnable; clamp the ladder; lazy leaf for
unlimited budgets), `0c1619e5` (never search again on a committed line or after a d8 b0 no-win),
`3de66a1f` / `e7e512d8` (the committed-line mirror), `60c168f8` (replay deferred acquisition
continuations at the deferred point), `88173a32` (acquisition deferral applies to SEARCHED decisions
only). Several of those change *when* the engine re-searches a committed line, which is precisely
the mechanism a reference replay exercises.

Bisect with the command above over that range rather than reading code — the repo's own lesson
(`work-units-not-bit-reproducible.md`) is that correct code reasoning reached the wrong conclusion
about a cache and even predicted the wrong sign.

## Not to be confused with

`docs/design/batch-run-to-run-nondeterminism.md` (open) is about **digests of budgeted batch cells**
being unstable run-to-run. This is different: a reference replay is a single-threaded, content-
anchored replay of a recorded human line, and the four verdicts here reproduced identically across
two independent builds — they are stable, not noisy.
