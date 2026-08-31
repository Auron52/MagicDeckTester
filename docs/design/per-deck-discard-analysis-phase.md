# Per-deck discard analysis (analyzer stage 8)

**Status: BUILT 2026-08-07 (user-directed); first fleet run across the 9 suite decks
(TH excluded) same day; adopted rules are PROVIDER-OWNED.** The stage lives in
`scripts/analyze_deck.py` (`--discard-analysis` for standalone; stage 8 of the full flow).
An adopted rule is a `CleanupDiscardCandidates` override in the deck's provider — a simple
case defers to the shared ranking with a static shed order (`HinataProvider`), a
state-dependent case implements its buckets (`AntiLifegainProvider`). There is NO profile
config for discard policy (an earlier same-day design put `spare_copy_band`/`discard_order`
in the profile; the user ruled all rules provider-owned and that plumbing was removed).
The stage's outcome A/B trials candidate orders via the TESTING-ONLY `MTG_DISCARD_ORDER`
env lever ("Name;Name;..." injected as a tier-A order per arm) — never set outside the
stage.

## The rulings this encodes (user, 2026-08-07)

1. **No general discard rule.** The spare-copy band (adopted 2026-08-06 as a global tier
   with a dragonstorm opt-out) is NOT a defensible general rule — whether a duplicate is
   redundant is deck knowledge. Anything like it is opt-in per deck.
2. **The better approach is an analysis phase per deck that addresses discarding.** Every
   deck gets its discard policy derived empirically at onboarding, not inherited from a
   root heuristic.
3. **Use search to inform the analysis** — don't invent rules from scratch. The retired
   in-play probe (`MTG_SEARCHED_DISCARD`, an out-of-band oracle, retired per
   `searched-discard-as-search-node.md`) is re-enabled OFFLINE as the label generator:
   `MTG_DISCARD_NODE=0 MTG_DISCARD_TRACE=1` makes every real cleanup discard emit a
   searched trial table (per-candidate rollout win turn + mv/copies/land/prot).
4. **AI authors the rule, the harness tests it, the USER approves adoption.** The stage
   ends at a recommendation report; it never writes the profile.
5. **Always attempt a heuristic.** Discarding is one of the easiest decisions to author
   accurately: at hand 8+ you keep 7, so key pieces survive and the shed choice is
   usually among several near-equivalent spares (the fleet's `multi_optimal_rate`
   measures exactly this). Smaller-margin sites (forced pitches: Land's Edge, retrace
   costs) go through the same shared ranking, so an adopted policy governs them too;
   the report buckets evidence by hand size to keep the rule honest there.
6. **Escalation: let the search choose among a few options.** If the labels show real
   regret that no rule captures (`NO_RULE_CONSIDER_SEARCH`), the deck is a candidate for
   the in-search discard axis (`Plan::discard_choice` + executor lockstep pin, committed
   dormant at width 1). Blind global widths are refuted (budget dilution — see the
   searched-discard doc); a per-deck, analysis-justified width is the sanctioned form.

## The bucket doctrine (user, 2026-08-21)

The rulings above say WHO authors the rule and HOW it is tested; a later session fixed the
SHAPE. The authoritative write-up lives in the analyze-deck skill (§5i, "Discard analysis —
the BUCKET policy"); the one-paragraph version: partition the hand into role BUCKETS (mana —
sub-divided lands-for-drops vs dorks-for-acceleration; threats — the catch-all, "extra
spells are always threats"; enablers — behind a threat floor; per-piece 1-item buckets +
a dig bucket for combo decks), build the keep set QUOTA-FIRST with every permanent quota
netted against the board, sub-quotas fungible upward within their parent, and shed only
overflow, ordered by DISTANCE-TO-PLAYABLE rather than raw size. The AI authors its best
shot and reports; the user amends and approves. The provider returns ONE index (the
one-option standing rule; it also skips the executor's trial fan), routed through
`CleanupDiscardRankingWithOrder` so staged/required-piece protection stays engine-enforced,
behind a default-on `MTG_<DECK>_BUCKET_DISCARD` flag. Validation adds a testing-only FAN
lever (return the full order so the trace can grade the pick — a single-entry return skips
it) and demands zero regret vs the searched labels plus non-inferiority through the suite.
`FiveColourProvider::CleanupDiscardCandidates` (adopted 2026-08-21) is the reference: quota
build, distance classes, state promotions (free-cast engine online; lethal-next finisher),
threat floor before Mana Cannons, Bolas last as goldfish-dead.

## The stage, step by step

1. **Evidence** (`_RunBatch`, 1 job, `--threads 1` so trace blocks don't interleave):
   N games at d3/budget 10 with the offline probe labelling every real cleanup discard.
   Parses to decisions: turn, seed, hand size, heuristic pick, and per candidate
   {name, mv, copies, land, prot, win_turn, optimal?}.
2. **Rules vs labels** (`_LabelStats`): candidate visible-info rules are scored by mean
   regret (rule pick's win − best win) and optimal rate against the searched labels:
   - `status_quo` — the deck's current ranking (whatever tier A/B produces today);
   - `band` — spare-copy band pick (max-MV name with ≥2 copies, non-land, non-protected);
   - `order` — a derived shed order: names with ≥5 observations whose mean shed regret
     beats the status quo's, best first (≤8 names) → `mulligan.discard_order`, which
     feeds tier A of the shared ranking exactly like a provider's own order.
3. **Outcome A/B**: only an order that beats status quo ON LABELS gets an arm. Paired
   seeds (d0 ×4000 s910000 + d3/b10 ×800 s920000), per-game `.wins` (losses = 9); the
   order arm runs with `MTG_DISCARD_ORDER` set (env is process-wide, so control and arm
   are separate batch runs on identical job specs — the engine is deterministic, so
   cross-run pairing is exact). The spare-copy band is a LABEL-ONLY hypothesis (scored in
   step 2, no engine support — see "the band" below).
4. **Recommendation**: `RULE_FOUND` (significantly better, |t| ≥ 2, in ≥1 config and
   significantly worse in none) / `STATUS_QUO_OK` / `NO_RULE_CONSIDER_SEARCH` /
   `DISCARD_INERT` (no decisions — the deck rarely discards, or its provider owns the
   ranking and returns one candidate, e.g. TreasureHunt).
5. **Rule-vs-SEARCHED validation (adoption bar, user 2026-08-07)**: before adoption, the
   authored rule is compared against SEARCHED discards — the offline probe
   (`MTG_DISCARD_NODE=0`) as the deciding arm — at the deck's play depth (d3–d4 suffices)
   with UNBOUNDED budget (`budget_ms 0`), apples-to-apples (both arms identical settings).
   Games the probe wins are classified at unbounded: budget churn and CLAIRVOYANCE are
   acceptable; a visible-information miss goes back to authoring. The rules must not add
   unrecoverable issues that are not a result of clairvoyance.

## Adoption flow

Present the report (evidence, label table, A/B, the rule VERBATIM — the user reviews the
rules themselves, and that review has real yield: it rejected the antilife static order as
a flattened shadow of the deck's true bucket policy and supplied the correct one) → on
approval, implement the rule as the deck provider's `CleanupDiscardCandidates` override
with an `MTG_<DECK>_...=0` hatch → rule-vs-searched validation (step 5) → re-run the
3-tier regression gate → inspect → accept → one commit (provider + GT + doc).

## The band (removed engine rule)

A "spare-copy band" (shed any name with 2+ hand copies before unique cards) was briefly a
global tier, then per-deck opt-in, then REMOVED from the engine entirely: it lost to
authored per-deck rules on every deck where duplicates mattered (hinata/antilife orders
beat it head-to-head) and actively hurt dragonstorm (+0.063 overnight; ritual copies are
cumulative fuel; labels agree — band 85.8% vs base 99% over 401 ds decisions). It remains
a scored hypothesis in the stage's label table; if a future deck's labels demand it,
implement it in that deck's provider.

## The vs-searched validation's yield (2026-08-07)

- **Anti-Lifegain (bucket policy): byte-identical to searched** — 2000 paired unbounded
  games (d9/b0), same digest both arms. The rule makes the searched discard every time.
- **Hinata (shed order): 3/2000 searched-better at d3/b0.** Classified: gi1902 =
  clairvoyance (the winning shed is one of two IDENTICAL Preordain copies — card numbering
  flips a future shuffle, invisible to any rule); gi1825 = no clean visible separation;
  gi322 = the case that drove three rounds of user review — see below.

### gi322: a "dead Karoo" that was neither a discard nor a play defect (2026-08-07)

Worth recording because it produced two wrong diagnoses before the right one. Seed 950322 gi2
keeps a ZERO-LAND hand on the second mulligan — defensible, since it holds Sol Ring AND
Ornithopter of Paradise (user: "I have seen 0-land hands work with that to start, because it
is so explosive") — but neither is castable without a land. No land arrives until turn 5, and
it is Izzet Boilerworks, which with no other land bounces ITSELF.

Round 1 read this as a discard problem and added a "dead Karoo sheds first" tier. Round 2
(user) narrowed it — a Karoo is only dead when there is no other land *in hand either*, since
a hand land revives it. Round 3 (user) removed the rule entirely; with the rest of the
priority list in place (Sol Ring never shed, the cantrip counts) the game wins on turn 8 again
**with no Karoo rule at all**.

A third reading — that the engine's land play was buggy because it replayed the Boilerworks on
turn 7 and bounced the Island it had just played — is ALSO wrong, and the log says so
(the user caught this): the Island had already been tapped for Ponder that turn, so bouncing
it cost zero mana, and turn 8 opened with the Boilerworks untapping for {U}{R} plus a fresh
Mountain drop — enough to cast Irencrag Feat *and* Preordain. That is the ordinary, correct
Karoo tempo trade. Likewise the turn-5 self-bounce costs nothing: there was no other land to
play, so the drop would have gone unused anyway.

The honest conclusion is that gi322 is a marginal mull-to-5 game whose win turn (8 vs 9)
moves with the discard sequence, not evidence of a defect anywhere. Generalizable: before
adopting a deck-specific tier because it "fixes" one game, check whether the game is simply
marginal — and verify a suspected play bug against the actual tapped state in the log rather
than the shape of the play.

## THE LABELS ARE BUDGET-LIMITED: classify every residual at UNBOUNDED first (2026-08-07)

The single biggest time-waster on the first fleet run. The evidence stage labels candidates by
playing the game out at **depth 3 with a 10 ms budget** (step 1). Those labels are therefore
budget-limited quantities: an apparent one-turn "regret" can be pure churn — the same rule,
given a full budget, reaches the same win turn as the search. Measured `mean_regret` and
`optimal_rate` are consequently a LOWER BOUND on rule quality, and any individual residual is
a *candidate* for investigation, not a finding.

Worked example (Hinata seed 930631 T2), which produced two successive wrong theories before it
was tested. The labels say shedding **Memory Lapse** — a dead counterspell against a passive
opponent — is a full turn WORSE than shedding the singleton **Crackle with Power**, the deck's
payoff. That is not believable at card level, and the game logs supplied a seductive mechanism:
in the Memory-Lapse line, Preordain bottoms **Irencrag Feat** on turn 3, so the kill slips to
turn 6 at X=2, while the other line draws Irencrag and kills on turn 5 at X=3 — apparently a
defect in the scry/bottoming heuristic being charged to the discard. **Both readings were
wrong.** Re-run unbounded, the rule's own line wins on turn 5 at d3 AND d5, identical to the
probe. There was no discard regret and no scry defect: just churn.

**Rule 0 for residuals: reproduce it at `--budget-ms 0` before forming any theory.** A story
that explains a churn artifact will always be available — the logs will show *some* divergence,
because two different lines were played — and it will be wrong. This is the repo-wide
"classify at unbounded before acting" discipline; the discard stage needs it more than most
because its labels come from a deliberately cheap run.

A second, independent measurement error worth not repeating: compare the rule's ACTUAL pick
against the best label. Do NOT compare two candidates pairwise ("is shedding A better than
shedding B?") unless the rule actually chooses between them — that scores a forced choice that
never happens. This produced a phantom "3 decisive cases, 0 counterexamples" signal for a Karoo
rule; in all three the rule kept BOTH cards and shed a dead card, optimally.

## The labeller is blind to the ROLLOUT's shed (USER, 2026-08-21) — `DISCARD_UNLABELLED`

> "How on earth does this kind of discard end up in there? Don't we have a proper path in the
> Analyzer relating to how it should be created?"

Asked of KittyEquipment, whose shed rule is the shared root ranking (highest-MV-first): on a deck
of 23 lands and a 1–2 MV curve that sheds Balan (4), Armored Skyhunter (4) and Loxodon Warhammer (3)
while keeping Plains **last**. The deck has no `CleanupDiscardCandidates` override — and this stage,
run on it, would have derived nothing, for a structural reason worth stating plainly:

**The evidence pass labels REAL sheds only.** `[discard_trace]` is emitted inside
`AIEngine::ChooseDiscard`. The SEARCH's own cleanup (`TurnSolver::SimulateEndAndStartNextTurn`)
sheds as well, takes index 0 of the same ranking with no search above it, and never reaches the
trace. A deck can shed **0** times in play and hundreds of thousands of times inside the rollouts
that choose its plans — KittyEquipment is `real=0 / rollout=145,888` per 50 games at d3 (51% of them
with under 4 lands out), and Treasure Hunt is `real=41 / rollout=773,314` per 20 games. The old
zero-label verdict (`DISCARD_INERT`, "no policy to derive") therefore claimed more than it knew.

**Fixed:** on zero labels the stage now runs a `MTG_SHED_STATS` census over both callers and
returns `DISCARD_INERT` only when *neither* caller sheds. Otherwise it returns **`DISCARD_UNLABELLED`**
with the rollout denominator and the instruction to BOUND the axis rather than assume it:

    MTG_SHED_WORST=1   # the ROLLOUT sheds the LAST-ranked candidate instead of index 0

That is a deliberate anti-heuristic in the `MTG_LACKEY_RANK=low` tradition — paired against the
default it brackets the whole axis, because no ranking can be worth more than best-vs-worst. Manifest
generator: `test/tools/kitty_ab/gen_shed_suite_manifest.py`.

**Suite-wide result (60 games/deck/arm, d3, `ignore_play_profile`, `logs/shed_suite`):** inverting
the ranking to its worst available setting moves the metric on **one deck of twelve**.

| deck | delta | faster | slower | plays-differ |
|---|---|---|---|---|
| dragonstorm | **+0.0333** (se 0.0234, t=1.42) | 0 | 2 | 2 |
| the other 11 | +0.0000 | 0 | 0 | 0 (antilife 1) |

So the blind spot is real but has cost almost nothing: on 11 of 12 decks the shed cannot pay,
including Treasure Hunt, where the lever had ~2.3M opportunities in the job and changed play in zero
games. Dragonstorm is the one to look at — a weak signal, but it is already the deck known to be
discard-sensitive (its per-deck protect SCOPE in `DiscardPolicy.h` exists because protecting every
copy cost it three overnight games).

**Caveat on reading a zero — FiveColour's is an ARTIFACT.** `FiveColourProvider` ends with
`if (!s_fan && ranked.size() > 1) { ranked.resize(1); }`, so `cd.size() == 1` and the last-ranked
candidate IS the first: the lever cannot fire and its 0.0000 is structural, not evidence. Any
provider returning one index is immune to this bound; use `MTG_5C_DISCARD_FAN=1`-style fan levers to
measure those. (KittyEquipment's own bound, on the full ranking, was measured separately at 150
games/cell: 0 faster / 0 slower of 300 — see `kitty-tutor-and-discard-heuristics.md` §1.)

## The rule-vs-searched check, actually RUN (2026-08-31) — and a THIRD blindness

`analyze-deck.md` requires a rule-vs-searched pass on every authored bucket policy and
"demands zero regret". It had been recorded as UN-RUN for both `MinotaurProvider` and
`DragonsProvider`, with the reason given as "needs a FAN lever neither provider has".
**That reason was wrong.** `CleanupDiscardRankingWithOrder` always returns the FULL hand
(tiers B and C sweep every card the provider did not name), so both providers already
return `cand.size() > 1` and `AIEngine::ChooseDiscard`'s searched pass already trials every
card. FiveColour needs `MTG_5C_DISCARD_FAN` only because it explicitly `resize(1)`s; a
provider that does not narrow needs no lever. The check was runnable all along.

### Minotaur — the labeller cannot see this deck's discards AT ALL

The stage instruments exactly one call site: `AIEngine::ChooseDiscard`, the CR 514.1
cleanup. Minotaur reaches it **zero** times — measured at d0/d3/d5, with and without the
play profile, 200 games per cell. An aggro deck with a Vial and a 1–3 curve does not end a
turn holding eight cards.

But the policy is far from inert. The `g_real_resolution`-gated `MTG_TRACE=discard` (which
fires in the shared ranking builder, i.e. at EVERY real discard site) counts **118
consultations per 200 games, 71 of them genuine choices** (`cands >= 2`). They come from
two non-cleanup sites:

- **Burning-Fist Minotaur** — `{1}{R}, Discard a card:` pump. The activation COUNT is a
  searched axis (`Action::Kind::ActivatePump`, one variant per K), but the card discarded
  to pay is not searched: cards.json says it outright, *"The discarded card is the
  provider's cleanup-discard pick."*
- **Neheb, the Worthy** — "each player discards a card" on combat damage.

`MTG_MINOTAUR_BUCKET_DISCARD=0/1` is NOT byte-identical (200 games d3:
`589e8d275a2717b0` vs `b7aaac0903b83902`), so the policy is live in play — it just fires
somewhere the labeller has no probe. So this is a **third** blindness, alongside the
rollout one above: the verdict vocabulary (`DISCARD_INERT` / `DISCARD_UNLABELLED`) is
keyed on the cleanup site, and a deck can be busy at a site the stage never looks at.

**Consequence: the zero-regret check is not merely un-run for Minotaur, it is
unrunnable as specified.** Closing it is not a lever but a new probe, and not a cheap one:
the searched pass works because cleanup is a clean `ResumeAt` boundary, whereas these
discards happen mid-resolution inside an activation cost and a trigger. The policy's
standing evidence is therefore the OUTCOME A/B (adopted on measured suite wins), which is
the stronger of the two gates anyway.

### Dragons — RUN, and it is not zero

20,000 games, d3/b10, `MTG_DISCARD_NODE=0 MTG_DISCARD_TRACE=1`, 20 single-threaded shards
on disjoint seed bases (the trace is raw unsynchronised `std::cerr`, which is why the
stage pins `threads=1`; sharding by seed buys the cores back without touching the engine).
589 labelled decisions:

| rule | mean regret | win-optimal | misses |
|---|---|---|---|
| status quo (`DragonsProvider`) | 0.0238 | 97.79% | 13 |
| spare-copy band (nonland, max mv) | 0.0153 | 98.47% | 9 |
| spare copy incl. land | 0.0187 | 98.13% | 11 |
| never shed a land (max mv) | 0.0883 | 91.68% | 49 |
| ORACLE (label lower bound) | 0.0000 | 100% | 0 |

**The bar is not met — but the whole decision surface is worth 0.0007 turns/game.** Real
cleanup sheds are 589 / 20,000 = 0.029 per game, so even PERFECT play at this decision
(the oracle row) buys `0.0238 x 0.029 = 0.0007` turns/game. That is at or below the
apparatus floor `deck-screening.md` measures for R=40 (~0.0006t); no rule change here can
be measured, let alone pay.

Two things worth keeping from the residual:

- The labels are near-degenerate: **58.6% of decisions are full ties** (every candidate
  labels identically — unlosable) and **99.8% are multi-optimal**. Grading a rule against
  a single clairvoyant rollout per candidate at this resolution is mostly grading noise.
- All 13 misses are T2/hand=8, and they lean one way: the rule sheds a land or a unique
  fatty where shedding a SPARE COPY ties or wins (`seed=1330237`: shed Lathliss with two
  Atsushi and two Urza's Incubator in hand). That is the spare-copy band — which improves
  labels by 4 decisions in 589 and was already REMOVED as an engine rule for losing on
  OUTCOME (see "The band" above; it cost dragonstorm +0.063 overnight). Label-better,
  outcome-worse is exactly the trap that section exists to record.
- The opposite hypothesis — formed from the shed NAMES, since Gruul Turf is 6 of the 13 —
  is refuted by the table: forbidding land sheds is **3.7x worse** (0.0883). Do not read a
  policy defect off a shed name without the trial table underneath it.

### `MTG_SHED_STATS` `real=` was counting rollouts (FIXED)

`ShedStats::Count(state, /*is_rollout=*/false)` was hard-coded at the top of
`ChooseDiscard`, before its `m_in_rollout` guards — but `GameEngine::PlayOut` reaches the
same `CleanupStep`, so every rollout playout (`RolloutWinTurn`, the bottoming/mulligan
rollouts) was counted as a REAL shed. Dragons measured `real=86` per 200 games where the
genuine count was 7. Now passes `m_in_rollout`; Dragons reads `real=7`, matching the
independent `g_real_resolution` trace exactly. Diagnostic-only (the counter early-returns
when `MTG_SHED_STATS` is unset) and digest-verified byte-identical. This matters because
`_ShedCensus` reads that number to decide whether a deck sheds in play at all.

## Known limits / follow-ups

- Evidence only covers CLEANUP sheds (the probe site); pitch-site labels (Land's Edge,
  retrace, and — see above — activation-cost and trigger discards like Burning-Fist
  Minotaur and Neheb) would need the same trace at those call sites. Minotaur is the
  worked case where that gap swallows a deck's ENTIRE discard surface.
- Discard decisions are rare (~0.5–1% of games at d3), so evidence runs need thousands
  of games; `DISCARD_INERT` on a low-decision deck means "not enough signal", not
  "proven fine" — and if the ROLLOUT sheds, the verdict is now `DISCARD_UNLABELLED`
  instead, with the bound to run (see the section above).
- The derived `discard_order` is a static name ranking; state-dependent orders (ds
  ritual-value: shed Pyretic before Seething — splice/net-mana logic) stay provider work.
- **User preference order (2026-08-07).** The purpose of this flow is to MINIMIZE USER
  TIME without significant quality or performance cost:
  1. **AI-authored heuristics** (this stage) — the working assumption; if it works well
     the rest is unneeded.
  2. **A discard model** learned over the same search labels — the designated FALLBACK
    where authoring fails on a deck: still zero user time, and nothing heavy is
    warranted (small model over the trial-table features, not a new subsystem).
  3. **A mix of searched options and user-authored rules** (per-deck searched width via
     `Plan::discard_choice` + hand-written provider rules) — last resort, because it is
     the one that costs user time.
  `NO_RULE_CONSIDER_SEARCH` verdicts therefore get: another AI authoring attempt (richer
  rule vocabulary) → a small labelled model → only then tier 3.
