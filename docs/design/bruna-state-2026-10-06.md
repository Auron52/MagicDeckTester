# Bruna — where things stand (2026-10-06, end of session)

A one-page summary for picking Bruna back up. The full record is the ledger,
`docs/design/analysis-Bruna.md` (about 1,500 lines). This page says what is done, what is in flight,
what is owed, and what waits on the user.

## The deck

`decks/Bruna/Bruna.cod` is a Bant Auras/Equipment voltron deck built around Bruna, Light of
Alabaster. Glittering Wish fetches from a multicolored sideboard.

* **Sideboard.** This is the user's list from 2026-10-06, 12 cards and 11 names: Bruna, Almost
  Perfect, Indrik Umbra, Linvala Shield of Sea Gate, Troyan Gutsy Explorer, Detention Sphere,
  Unflinching Courage, Steel of the Godhead, Vexing Shusher, Auroral Procession, and 2 Reborn Hope.
  Every card is multicolored, so Glittering Wish can fetch any of them.
* **Glittering Wish cannot fetch Colossification.** It is mono-green and in the mainboard. Only Open
  the Armory tutors it.
* **The key line (from the user):** put Arcanum Wings on a creature that is not summoning sick, then
  swap in Colossification during combat. This is an automatic win, and a common one.

## Shipped on `phase-1-2-deck-analyzer` (CI green: Linux, Windows, determinism parity)

* **Cards and rules.**
  * All 26 cards are implemented and coverage is clean.
  * Engine rules fixes:
    * Shroud now blocks only casting and targeting, so Bruna's put and the Wings swap get past it.
    * Legend-rule Auras now go to the graveyard.
    * Attack power now counts power from Auras and Equipment.
    * Somberwald Sage's mana is creature-only.
    * Colossification taps its host after it attaches.
    * Haste from any source now lets a creature use {T} abilities (CR 302.6).
    * Troyan's restricted mana that isn't spent now floats.
* **Search work.**
  * The Aura host ranking is one heuristic for every creature-Aura cast. Order: lethal first, then
    team damage this turn plus next, then this turn, then fewer Auras on mana creatures. It is
    proven against the fully branched control (`MTG_AURA_HOST_BRANCH`).
  * The site-9 continuation index fix (`MTG_BP_NODE_SHADOW`) is ON.
  * `MTG_ROLLOUT_AURA_SWAP` is ON.
  * `MTG_SOLVE_COMBAT_SWAP` is ON. The d0 runner and rollouts move a main-phase Wings →
    Colossification swap to combat. Over 9,000 d0 games this was 514 better / 4 worse.
  * Bruna's gather prune and the Wings ranking are both proven.
  * `MTG_LAND_AURA_RAMP_FIRST` puts Wild Growth first, as a generic order. The user approved this.
* **Suite.** Bruna and bruna2hg (2HG) rows are in all three tiers.
  * Smoke and regression GT are accepted (e1171a1d). Net change vs the old list: bruna -25 turns,
    bruna2hg -3.
  * **Overnight GT is NOT accepted yet**, so the `regression_tiers` gate stays PARTIAL.
* **References.** The user's Bruna games are under `references/Bruna/` (plus one under
  `references/suboptimal/Bruna/`). All replay with `--strict` and all are pushed.
* **Viewer.**
  * The host-choice menu marks the ranking's preferred host with a green "suggested" pill. It does
    not reorder the menu.
  * Mother of Runes usability was deferred, with the user's agreement.

## In flight: on branches, NOT landed

1. **`viewer-combat-swap`: LAND THIS FIRST. The user is not recording Bruna references until it is
   in.**
   * What it does, in human play only: a main-phase Wings → Colossification swap onto a creature
     that can attack is applied automatically after attackers are declared. This happens only when
     the {2}{U} is payable without the attackers' mana. The menu says "(in combat, after attacks)".
   * Old references replay with their recorded timing, via a recording stamp and
     `--legacy-main-swap`.
   * 460/460 unit tests pass. The search wins 131/131 of the positions where the swap is lethal.
   * What is left: rebase onto the tip, `./build.sh`, smoke and regression (these must be
     byte-identical, since the change is human-play only), the viewer checks, push, and CI.
   * After that, restart the viewer from the primary checkout:
     `env -C /workspaces/MagicDeckTester PLAY_HOST=0.0.0.0 MDT_REFERENCE_BACKUP_DIR=/home/vscode/.claude/mdt-reference-backups ./play.sh --no-open`
   * Then tell the user it is safe to record.
   * Steps are in `docs/design/viewer-combat-swap-status.md`, on that branch. Logs are in
     `logs/durable/viewer_cswap_2026-10-06/`.
2. **`viewer-combat-swap-engine-parked`: needs the user's decision.**
   * This is a rules fix for CR 508.1f. The engine taps attackers at combat damage rather than when
     they are declared. As a result, an attacking Birds or Pilgrim (even the Wings host) could pay
     for the combat swap, which is illegal.
   * The branch also keeps a mana creature home when the swap needs its mana.
   * Only Bruna moves: smoke +33 turns, regression +23. 21 of the 23 slower smoke games were wins
     that relied on the illegal payment. The other 2 recover at a higher budget.
   * The search wins 158/158 of the positions where the swap is lethal.
   * **Recommendation: land it.** The wins it removes were illegal, as with the Karoo payment fix.
   * The same bug affects firebreathing. That is deferred in
     `docs/design/attackers-tapped-at-declaration.md`.
3. **`wish-heuristic`: the Glittering Wish candidate rule, the user's spec. Proof incomplete.**
   * The rule narrows the 11 names to a small set:
     * Bruna, unless she is already in hand or on the battlefield;
     * the highest-power Aura;
     * a body (Linvala or Vexing Shusher), only when there is no body on the battlefield or cheap in
       hand;
     * Troyan, only when mana is short;
     * a cheap Aura, only as a second Aura and only with no cheat-into-play path.
   * The control is `MTG_WISH_FULL_WIDTH=1`. The viewer marks the candidates as "suggested".
   * Proof against the full-width control, round 3 (better / worse):
     | cell | result |
     |---|---|
     | d5 | 6 / 2 |
     | v5 | 2 / 3 |
     | held-out f5 | 1 / 3 |
     | d3 | 1 / 1 |
     | 2HG | 1 / 0 |

     Some held-out games never recover, even at d8 b0: Linvala's third point of damage is the kill.
   * Per the user's rule, the fix goes into the ranking. It does not fall back to branching.
   * Status, counterexamples and diagnosis: `docs/design/glittering-wish-heuristic.md` on that
     branch.

   * **Final state (agent report, 2026-10-06 close).** The branch is `wish-heuristic` @ `99980da3`;
     no suite has been run on it. The final rule costs 0.74x the full-width search work at d5, 0.62x
     at d3 and 0.60x in 2HG, which is about level with the pre-sideboard list.
   * Still to do: re-check the remaining worse games on the final build (every earlier one caught up
     at a higher budget, or was the search's own allocation inside the candidate set); root-cause
     d3 gi117, which is new; re-measure the candidate-count distribution.
   * **USER DECISION.** The early-turn state offers five candidates (Bruna, Almost Perfect, Linvala,
     Shusher, Troyan), against the user's "1-3 almost always". Every cut of that state that was
     tested lost turns, so the agent kept all five.

## Cost: probably over the 3x rule now

Before the sideboard change, Bruna cost 2.63 s/game against a budget of 3.10 s (3x FiveColour). The
new sideboard widened Glittering Wish from 4 names to 11. Search units rose 1.5x at d5 b20 and 1.8x
at d3 b10, which puts the deck at roughly 3.9 s/game, **likely over the budget**. CLAUDE.md says that
getting the deck back under 3x then becomes **the first goal, ahead of the value leaf and the
mulligan**.

* The Wish heuristic is the intended fix. Once it lands, re-measure with
  `python3 scripts/suite_gate.py --cost decks/Bruna`.
* Separately, 62% of wall time goes to mulligan-bottoming playouts. Those disappear once the
  exhaustive keep table exists.

## Owed (when the user asks; no overnight run without their say-so)

* **Overnight GT** for `bruna` / `bruna2hg`, so that every tier has GT.
* **Value leaf, then mulligan profile.** These are strictly serial, and they come only after play
  settles (cast order and discard, below) and the cost is under 3x. Recommendation: before generating
  the value leaf, correct the power input. `ExtractMidGameFeatures` sums bare power and ignores Auras and Equipment (it reads Bruna + Conscription as 5 power), which
  makes it nearly blind on a voltron deck.
* **Card cost audit.** Re-run `audit_card_costs.py` across all cards once Scryfall stops
  rate-limiting. Bruna's own cards have already been checked and match.

## Waiting on the user (never settled by an agent)

1. **Land the CR 508.1f fix** (branch 2 above)? Recommended: yes.
2. **Cast order.** `MTG_BRUNA_ORDER` (OFF) measured neutral at play settings. Recommendation: keep
   the generic order. The user will look at this.
3. **Discard policy.** `MTG_BRUNA_BUCKET_DISCARD` is shipped ON but only PROPOSED
   (`docs/design/bruna-discard-policy-proposal.md`). It is non-inferior. The user will look at this.
4. **bruna2hg.** Should its +1 be held to its own per-row bar, or to the deck's net?
5. **Smaller items from the ledger.**
   * Troyan's loot discard uses the bucket pick rather than a searched axis.
   * Regrowths rank at overflow 25 in the discard order.
6. **Not Bruna's, but surfaced here.**
   * A Selesnya digest flickers only in full pooled suite runs and needs an owner.
   * `MTG_ATTACK_BODY_TAP_ORDER` adoption for autonomous play needs a held-out A/B.

## Order of work for the next session

Land `viewer-combat-swap`, restart the viewer, then tell the user. Next, get the user's answer on
the CR 508.1f fix. Then finish the Wish proof (fix the ranking), suite, land, and re-run the cost
gate. Then the user's cast-order and discard calls. Then, when the user asks: overnight GT, value
leaf, mulligan.
