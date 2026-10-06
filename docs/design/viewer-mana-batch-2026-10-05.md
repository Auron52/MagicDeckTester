# Viewer batch 2026-10-05: five reports, three mana-tap defects, two rulings

The user played five games in the viewer on another worktree and reported them. That worktree was
run from a temporary folder (the defect `c7487fb9` guards against) and its saved references were
lost, so every game below was reproduced from `(deck, seed, choices)` with
`test/tools/intent_replay.py` -- a small intent-replay driver that walks `--claude-play` one
decision at a time, answering each from an ordered list of regex matchers on the viewer's own plan
summaries. The reconstructions reproduce the user's hands, draws and lines exactly (the viewer's
`buildArgs` passes no environment the driver does not: same deck, profile, `--depth 0`,
`--max-turns 8`, `--game-index 0`).

| # | Game | Report | Root cause | Fix |
|---|------|--------|------------|-----|
| 2 | SelesnyaLifegain seed 4, T5 Ageless Entity | "Wirewood Lodge is left untapped ... should not be kept over creatures" | The Lodge's live untap-burst reserve (rank 68, past the whole creature band) fired unconditionally | Hold only when the burst is NEEDED by the turn's remaining bill (`UntapBurstNeededByLine`, `MTG_LODGE_HOLD_NEEDED`) |
| 3 | SelesnyaLifegain seed 9, T5 Blighted Steppe | "It should tap all lands rather than the lord" | `ActLineHoldMask` held a {W} provider for the Steppe's own {W} WHILE paying the Steppe | Pending-only demand: the activation being paid, and ones already paid, no longer count (`ActLinePayScope` / `ActLinePassScope`) |
| 4 | SelesnyaLifegain seed 12, T5 Priest + Steppe | "the Alchemist should not be tapped" | Same mask: the Sanctuary held for the Steppe's {W} during the Steppe's own payment | Same fix |
| 5 | WhiteKnights seed 13, T3 Silverblade Paladin | "No decision was given about soulbond" | ONE legal partner (Adeline; the Human token is created when she attacks, after the cast) and the viewer auto-took one-option attach dialogs by the 2026-08-27 / 2026-09-07 rulings | USER 2026-10-06: "We should always be able to choose to pair with any creature OR not pair at all" -- soulbond is now ALWAYS a dialog (engine flags `soulbond: true` on the attach_host decision; the viewer exempts it from the one-option auto rule and offers "Don't pair"). The Equipment attach keeps the auto rule. |
| 6 | Snow seed 13, T4 Scrying Sheets | "we should not be holding up Red" | NOT REPRODUCED: on the user's line the current tree AND origin `c7487fb9` both pay the dig's `{1}{S}` with Forest + Mountain (Red is spent); the ideal is Mountain + Boreal Druid, keeping {G} and {U} for the found Druid and the Frost Augur in hand | None at default; see "Open questions" |

## The two mechanisms, in detail

### Activation line hold held a colour for the activation being paid (seeds 9 and 12)

`ActLineHoldMask` (MTG_ACT_LINE_HOLD, default ON) holds back, while a plan apply is paying, the
`{T}` sources and enough colour providers for the plan's trailing activations. Its colour half was
built from `PlanTraits::act_pips` -- the SUMMED pips of every activation the plan carries -- and that
sum is static for the whole apply. So when the trailing pass reached the activation itself, the
mask still held a provider for that activation's own pip:

* seed 9: `{3}{W}` on Brushland x2, Pathway, Forest, Mystic, a 2-Elf Archdruid. The second Brushland
  was reserved for the `{W}`; the pip went to the first Brushland and the last generic pip to the
  Archdruid (2 mana for 1, the lord tapped). `[nddbg] reserved: Brushland#20` on every pip.
* seed 12: `{3}{W}` after Priest of Titania. The Sanctuary was reserved for the `{W}` (narrowest
  provider); the Alchemist paid it instead and the Sanctuary sat untapped.

The `{T}` half was already self-limiting (a fired activation's source is tapped and drops out); the
colour half was not. Now both trailing-pass twins (AIEngine executor, TurnSolver `apply_one`) bracket
each activation's own payment in `ActLinePayScope` and the pass in `ActLinePassScope`, and the mask
subtracts the paid and current activations' pips: `need[c] = act_pips[c] - paid[c] - current[c] -
float_left`. Nested applies restore the outer values.

### The Lodge's burst reserve fired whether or not the burst was needed (seed 4)

A Wirewood Lodge with a live burst (a 2+ scaled Elf to untap) was ranked 68 -- past every creature
-- unconditionally. That tier is a 2026-08-25 strict-bar fix for Stompy, where the burst is what
makes a `{Priest, Symbiosis}` line payable. On seed 4 the turn needed nothing of the kind: Ageless
Entity's `{3}{G}{G}` on 6 mana. The hold tapped both creatures (and Brushland for pain) and kept a
`{C}` nobody could use.

`UntapBurstNeededByLine` now compares the turn's still-unpaid bill (this payment's remaining pips via
`g_pay_remaining_mv`, the line's later casts via `g_line_unpaid_cost` minus the cast being paid via
the new `g_pay_full_mv`, the plan's activations via `PlanTraits`) with the plain supply
(`SpareUntappedMana` with the burst credit taken back out and the Lodge's own `{C}` put back). Hold
only when the plain supply falls short. The activations' generic pips are not tracked, which errs
toward spending the Lodge like a land.

## Verification

* Driver reconstructions on the fixed binary: seed 4 pays Sands, Lodge, Archdruid, Brushland (Elves
  up); seed 9 pays Brushland, Brushland, Pathway, Forest (lord and Mystic up); seed 12 pays Priest +
  Sanctuary (Alchemist up).
* Unit tests SUCCESS; scenarios 139/139 at default and with all three mana levers on.
* Smoke: only Selesnya moves -- searched 0 slower / 5 faster / 15 digest-only, d0 0 slower / 3
  faster / 83 digest-only (d0 6.2790 -> 6.2760, d3 5.4467 -> 5.4267, d5 5.3467 -> 5.3200).
* Regression: only Selesnya moves -- searched 0 slower / 2 faster / 36 digest-only, d0 0 slower /
  8 faster / 89 digest-only (d0 6.2470 -> 6.2390, d3 s2002 5.1800 -> 5.1667, three cells digest-only).
  Every other deck byte-identical in both tiers; Stompy's current list has no Lodge, so only
  Selesnya can exercise the Lodge tier. Both tiers accepted with the prior notes carried.
* Reference gate: unchanged except ONE archived-list reference,
  `StompySurprise/v1-arborelf-worldspine4/claude_s11_gi10`, now BOARD-DIVERGED (non-gating). Traced
  with the driver: on turn 4 the Lodge now pays Natural Order's generic pip instead of the Priest of
  Titania, the Priest attacks for 1, and the human's own line reaches exactly 20 on turn 5 -- a turn
  EARLIER than the recorded 6. The recorded turn-6 follow-up ("Wirewood Lodge: untap an Elf") was
  unpayable even on the old binary (its `{G}` failed: no green source left after Turntimber
  Symbiosis), so nothing the human actually got is lost. The gate's own 2026-08-25 comment names
  this reference and this shape as the re-play-only class. It is owed a USER re-save, not a revert.
  **CORRECTION 2026-10-06:** not owed a re-save — the divergence was the checker's ordinal-keyed
  pins addressing the wrong frames (see `reference-gate-red-on-origin-tip.md`, 2026-10-06
  update); with the ordinal remap the recorded line replays (`repaired`, T6).

The driver mirrors one viewer behaviour a reference never records: a main phase whose only option
is "cast: (nothing)" is answered by the viewer itself, so the driver passes it without consuming a
matcher.

## Open questions (surfaced, not blocking)

1. **Soulbond pair-or-wait is now SEARCHED (USER 2026-10-06: "even in the search I would say it is
   wrong to pair with a 1/1 ... you could have only a 1/1 token unpaired and a paladin enter. In that
   case it could make sense to search ... searching is cheap enough here, since this is an unusual
   case").** `Plan::soulbond_decline`: for a base plan that casts a soulbond creature while every legal
   partner on the board is below 2 power (a lone 1/1 token), `AppendSoulbondDeclineVariants` appends
   a twin that declines the pairing, so pair-now (+double strike from this attack, locked to the
   token) is scored against wait-for-a-real-partner instead of being the provider's guess. Gated
   tightly: no twin with a real partner up (the pairing is not in doubt), none when the plan also
   casts a 2+-power creature (the pairing would land on that), none in human play (the viewer's
   dialog is the decision), none for a deck without a soulbond card (`deck_has_soulbond`). Applied
   lockstep by `ApplyPlanDirect` and `AIEngine::TakeTurn` through `PlanSoulbondDeclineScope`, which
   the pairing resolution honours only when no human chooser is installed. Folded into the plan
   hash, `SamePlan` and the dedup signature; summaries read "; no pairing".
   `MTG_SOULBOND_DECLINE_AXIS=0` disables; `MTG_SOULBOND_DECLINE_AUDIT=1` prints twins enumerated and
   declines applied at exit (150 WhiteKnights games at the smoke d3 settings: 244 twins; d0: 0).
   The printed list's creatures are all 2+ power and its 1/1s are tokens whose makers are 2+-power
   creatures themselves, so a Paladin normally pairs with the maker on entry; the twin exists for
   the residual case only. Suite results: see the commit that lands it.
2. **Snow seed 13.** The report does not reproduce on the user's line on either binary. The better
   payment (Mountain + Druid) needs the needs-based lever to value the dig's expected find; the
   lever's library-reveal expectation does not yet cover Scrying Sheets / Frost Augur finds. That is
   a follow-up inside `MTG_NEEDS_TAP_ORDER`, not a default-path defect.

## Later reports, 2026-10-06

| Game | Report | Root cause | Fix |
|------|--------|------------|-----|
| Soldiers seed 7, T5 | Sixteen "Rick: chose Vigilance + Lifelink" prompts before Rick was cast | The "as this enters" choosers (Rick's ability pair, Coldsteel Heart's colour) were missing from `RevealLogPause` / `AllPlayHooksNull` / `ComboOffApplyPause`, so every SIMULATED Vial put of Rick in the T5 search asked the human | Paused like every other play hook (`bd5da4d5`). A real put still asks once. |
| Snow seed 9, T3 | A "Frost Augur X? / this line doesn't do that / X=1" dialog on a plain Augur activation | A board click wrote `cast=Frost Augur`, the same token a cast of the second Augur in HAND writes, so CheckLine matched both plans and the walk asked the human to separate them under the generic `X` label | New `act=<name>` verb (`LineSpec::board_acts`): kept in the ordered cast multiset, but the matched plan must ACTIVATE that source. Mirror: a plain `cast=` for a card both in hand and on the battlefield must be a HAND cast (`MTG_LINE_HAND_CAST_MIRROR=0` disables). The viewer writes `act=` for every plain board activation. |
| Snow seed 9, T4 | "Why 2 of the same dialog?" (two Marit Lage's Slumber scries) | Correct: Scrying Sheets (snow land) and Abominable Treefolk (snow creature) each trigger "scry 1"; the deck runs eight Snow-Covered Islands, so two in a row on top is ordinary | None |
| WhiteKnights seed 5, T2 | Plains + Sol Ring + Accorder Paladin + Dauntless Bodyguard was disallowed | In the human's order the Paladin's `{W}` tapped Remote Farm and its `{1}` ate the Farm's second `{W}`; Sol Ring stayed up and the Bodyguard's `{W}` was unpayable | Line-float hold (`MTG_PAY_LINE_FLOAT_HOLD`, human play only): a generic pip that would eat floating mana the rest of the declared line still needs taps a spare source of unneeded mana first |

`test/viewer_validate_check.js` rebuilt every recorded name as a HAND click, which was harmless while
board activations also wrote `cast=`. It now rebuilds a plain activation as `act=` and a blink as
`blink=<outlet>@<target>*<count>`, and skips COMBO-OFF plans (committed from their own button, never
as a queued line). Its baseline dates from 2026-08-08: the three Soldiers references saved
2026-10-06 (`s3_gi2`, `s4_gi3`, `s5_gi4`) fail it with both new rules OFF, so they were never
validated; they involve Aether Vial puts and belong with the Vial-order work.

**Open (surfaced to the user): Marit Lage's Slumber's scry is NOT searched.** The searched scry axis
(`MTG_SCRY_SEARCH`, `docs/design/searched-scry-disposition.md`) covers only a LAND's own ETB scry. A
TRIGGERED scry (Slumber, on any snow permanent entering) takes the provider's single pick: the generic
`ScryKeepOnTop` keeps every nonland and bottoms every land once two lands are in play -- the default
the viewer pre-selects, and a poor rule for a deck with a seven-drop and Scrying Sheets.
