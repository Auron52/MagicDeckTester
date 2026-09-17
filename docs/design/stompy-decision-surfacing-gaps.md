# Decision-surfacing gaps found by hand-playing the new StompySurprise list

Found by the USER on 2026-09-17, playing the first reference on the promoted list
(`references/StompySurprise/claude_s1_gi0.json`, seed 1 gi 0, won T4). Three defects and one UX
request. Diagnosed from that game by replaying its `--choices` stream frame by frame.

**Two of these narrow a legal choice, which this repo forbids outright.** One of the two narrows it
for the SEARCH as well, so it is not merely a viewer problem.

---

## STATUS 2026-09-17 — all four FIXED for the human; the search half of 1 and 2 stays open

Every decision below is now offered, and each one in the zone the user asked for. The sections that
follow are the original diagnosis, kept verbatim as the record of what was wrong; **read this block
for what the engine does today.**

| # | what was missing | how it is offered now | zone |
|---|---|---|---|
| 1a | Hulk ch. I — which creature, and whether to decline | `free_cast` frame with `zone:"hand"` (`PerformSagaFreeCast` -> `g_play_free_cast_chooser`) | HAND |
| 1b | Hulk ch. II / III — the target | `target` frame with a `loyalty` prompt (`ChooseSagaChapterTargetIndex` -> `g_play_loyalty_chooser`) | BATTLEFIELD |
| 2 | Natural Order — victim, incl. a creature cast in the same plan | `sacrifice` frame at RESOLUTION (`PerformSacrificeCreatureCost` -> `g_play_sacrifice_chooser`); the queue-time sub is deleted | BATTLEFIELD |
| 3 | the victim serialized as `soulfire_targets` | split into `sac_victim` + `sac_victim_name` (`src/main.cpp`), keyed on `sac_additional_creature_color` | — |
| 4 | everything asked in a dialog | 1a is a bottom prompt over a clickable hand; 1b and 2 are board clicks | — |

**What was verified, not assumed.**

* **Autonomous play is byte-identical.** 5,600 games of StompySurprise across four configurations
  (d0 4000, d3 800, d5 400, 2HG-d3 400) produce identical `--batch` digests before and after:
  `836b8fdb5b184efc` / `d63523fcc2be1059` / `ed84bd4d21d4a254` / `f62445436da48ea8`. Every chooser is
  nulled by `RevealLogPause` in search and rollouts, and `saga_chapters` /
  `sac_additional_creature_color` are carried by exactly two cards, both in this deck alone.
  **So these fixes do NOT move ground truth** — contradicting this doc's original Sequencing note,
  which predicted fixes 1 and 2 would. The 21 stale `stompy`/`stompy2hg` GT keys are stale from the
  LIST PROMOTION only, and this work neither adds to that debt nor depends on it.
* **The reference corpus still replays.** `test/viewer_protocol_check.py` over 318 references:
  0 play-drift, 0 enum-gap, 0 contract-fail, and the one board-diverged / ten mull-drift entries are
  present on the baseline binary too. The only change anywhere in the corpus is the user's own
  `StompySurprise/claude_s1_gi0.json` moving `ok` -> `repaired`: it now meets three decisions it
  predates (the Hulk chapters), answers them from `heuristic_default`, and **reproduces its T4 win**.
  It is left untouched on disk — re-saving it is the user's call.
* **Defect 2 reproduces and is closed.** On seed 16 the declared line
  `land=Forest;cast=Fyndhorn Elves;cast=Natural Order` used to fan **26** queue variants whose victim
  set was `{Llanowar Elves, Elvish Archdruid}` — the board *before* the line, with the Fyndhorn Elves
  the line itself casts absent, exactly as reported. It now fans **13** (fetch targets only) and the
  resolution frame offers Fyndhorn Elves.

---

## STATUS 2026-09-17b — the SEARCH half of defect 2 is fixed; the Hulk chapters are sized, not built

`MTG_SAC_CREATURE_AXIS` (default ON, `=0` restores the old behaviour exactly) adds a post-dedup plan
axis in `AppendSubdecisionAxes` (`src/ai/TurnSolver.cpp`) that re-points a Natural Order cast's victim
at a creature **the same plan casts earlier in the turn** — the set `CollectActions` structurally
cannot see, because it runs before any plan exists. So "deploy the worst body, then eat it" is now one
enumerable turn for the SEARCH, as it already was for the human.

**What it does, shown rather than asserted.** Seed 1001 game 56, turn 4, d3:

| arm | the turn | result |
|---|---|---|
| `=0` (old) | cast Elvish Archdruid; Natural Order eats **Llanowar Elves** → Craterhoof | won T4, 28 dmg |
| default (new) | cast **Vaultborn Tyrant**; Natural Order eats **the Tyrant it just cast** → Craterhoof | won T4, 20 dmg |

The new line keeps the Llanowar Elves *and* banks the Tyrant's dies-trigger token copy. It is exactly
the shape the user described, and the old arm could not express it at any budget.

**What it is worth, measured, and the answer is "nothing the goldfish can see".** 13,000 held-out
games (d3 4000 x2 seeds, d5 2000, 2HG-d3 3000; seeds 770001/880002) move the average win turn by
**−0.0004t on one cell and 0.0000 on the other three**, and a per-game diff over 1500 d3 games moved
**zero** games' win turn. All four `--batch` digests DO change, so play genuinely differs — the lines
change, the clock does not. Wall clock is inside run-to-run noise (one d3 cell faster, one slower).

That null is a fact about the HARNESS, not about the line: against a passive opponent with no
blockers and no removal, which body you eat almost never changes the turn you win on. The value here
is that the search's legal space now matches the rules and matches what the viewer offers the human —
not a win rate. Shipping it ON is the [searched-choice doctrine](searched-choice-audit.md) call the
user already made for the sac-LAND axis (*"It should be a branch by default and be overridden by a
heuristic"*, 2026-08-25); the off-switch is there because the measurement bought no quality.

**Three implementation facts worth keeping** (each cost a debugging round trip):

* **A hand `Card` carries identity, not type/colour.** `m_type_mask` / `m_color_mask` are filled when
  the card becomes a permanent, so `hand[i].IsCreature()` is **false for every card in hand**. Read
  both off `CardDatabase::LookupCached(c)->card`. `PerformSagaFreeCast` documents the same trap
  ("testing the hand copy silently reports every card colourless") — it is a repo-wide rule, not a
  Saga quirk. Getting it wrong made the axis silently emit zero variants.
* **`Action::hand_index` is NOT an index into the state you are holding.** `AppendSubdecisionAxes`
  runs over the POOLED post-dedup set, but each plan came from `EnumeratePlans(copy)` where `copy` is
  the state *after that plan's own land drop left the hand*. Reading `state.hand[act.hand_index]`
  names the wrong card (measured: `Natural Order@4 => Forest`). The axis reconstructs the per-plan
  hand from `p.land_to_play` using `PlayLandByName`'s own pick rule and then **verifies every action's
  `card_name` against it**, skipping any plan that fails to verify.
* **The victim is named by `m_number`, not pinned by rank.** The sac-LAND axis pins a rank because a
  mid-plan land set can grow in ways the enumerator cannot name; here the growth is exactly this
  plan's own action list, so naming the copy keeps the entire existing carrier working
  (`plan_signature`'s `#V`, `PaySacVictimScope`'s fodder-pays ordering, `main.cpp`'s
  `sac_victim`/`sac_victim_name`, `PerformSacrificeCreatureCost`'s lookup).

### DONE 2026-09-17c: all three Hulk chapters are searched-with-heuristics

**USER's brief, which this implements:**

> *"Technically they are decisions like that of Aether Vial."*
> *"To be fair, they would be implemented as a heuristic anyway, in this case."*
> *"The decision is quite easy for the search."*
> ***"Only the human really needs the axis in general, but we should still design it as
> searched-with-heuristics like everything else after the pause."***

**The Aether Vial reference was exact, not an analogy, and it dissolved the objection this item was
parked on.** I had recorded the turn boundary as an open design problem: chapters II/III fire in
`AdvanceSagas` at the NEXT turn's draw step, so on the committed turn they resolve before any
main-phase solve exists. The Vial CHARGE axis already solves precisely that — it decides something
that fires at the next turn's UPKEEP — and its answer is to let the pick ride **`GameState`** rather
than a scoped guard. Chapters II/III are that problem one step over and take the same answer.

| chapter | fires | pin | carrier | flag |
|---|---|---|---|---|
| I | this plan's apply (the Saga's own enter) | rank into `cand_slots`, or DECLINE | `ScriptedSagaCh1` (RAII) | `MTG_SAGA_CH1_AXIS` |
| II / III | NEXT turn's draw step | rank into `SagaChapterTargetCandidates` | `GameState::scripted_saga_target` | `MTG_SAGA_TARGET_AXIS` |

Both default ON, both with exact off-switches. The split is not stylistic: chapter I's guard is still
standing when it fires, chapter II/III's would be long gone.

**Both pins are RANKS, never permanent identities.** For II/III that is forced — a whole turn elapses
between pinning and reading, during which we untap, draw and deploy, so a named permanent would go
stale constantly. For chapter I it is forced the other way: the plan's own creature casts leave the
hand *before* the Saga resolves (creatures rank 10, enchantments 20), so the list shrinks. Both clamp
on over-range (duplicate-not-whiff), so drift costs a duplicate world, never a lost target.

**The heuristics keep both doctrine jobs** — branch ORDER (entry 0 is the front, so a base plan and a
k=0 variant agree) and the only decision on every plan-less path (d0, rollout leaves, and the human's
preselected default). `DefaultSagaChapterTarget` is now just `SagaChapterTargetCandidates().front()`,
byte-identical to the old scan.

**What it measured, and the user called it in advance.** 13,000 held-out games per axis:

| axis | d3 s770001 | d3 s880002 | d5 | 2HG-d3 | wall |
|---|---|---|---|---|---|
| chapter II/III | 0.0000 | +0.0004 | −0.0005 | −0.0003 | +1.5% … +4.9% |
| chapter I | +0.0002 | −0.0002 | 0.0000 | −0.0003 | −1.3% … +8.1% |

Flat, exactly as *"the decision is quite easy for the search"* predicted. **Neither axis buys measured
quality; both ship ON for the reason the user gave** — branch-by-default consistency, and
reachability of the cases the heuristics structurally cannot see (chapter II's counters are
PERMANENT, so a body about to be eaten by Natural Order is the wrong home; chapter I's
trial-on-a-copy is blind to a same-plan Natural Order, which resolves after the Saga).

**Verified in the order that catches a DEAD axis**, which is the trap this session already fell into
once — an axis emitting zero variants also produces identical digests, and that reads as success:

1. **It fires.** 24,919 chapter-target and 11,652 chapter-I variants over 60 games, and Fyndhorn
   Elves (1/1) now appears as a chapter II target — a body "biggest that can swing" never reaches.
2. **Off-Saga decks are byte-identical.** Not a formality for chapter I: its TT-key fold sits inside
   a gate shared with every other scripted pin, so it perturbs the key of any FiveColour/Critter
   state carrying a tutor or dig pin. Measured — all five off-Saga digests unchanged.
3. **Each off-switch reproduces its predecessor commit's four StompySurprise digests exactly.**
4. **The human path is untouched.** 318 references replay 15 ok / 292 repaired / 0 play-drift /
   0 enum-gap / 0 contract-fail — character-for-character the same result as before the axes.
5. Smoke 71/80 byte-identical to GT; the 9 reds are the 4 stompy keys (stale from the promotion plus
   this work) and the 5 pre-existing non-stompy ones, whose values are unchanged.

**One detail worth keeping**, because it is the class of bug that is silent: the TT-key gate for
chapter I tests `!= kSagaCh1Heuristic`, not the `>= 0` its neighbours use. A pending DECLINE is −2
and is every bit as future-determining as a rank, so a `>= 0` gate would let two genuinely different
states share a key.

---

## 1. World War Hulk offers NO decision at ANY chapter (SEARCH + HUMAN)

USER: *"Hulk gave me no Targeting decisions."* and *"Hulk didn't ask for the creature to be played or
the target of the 3 counters. I didn't get to test the last phase, but you should check it as well."*

**All three chapters are affected. Chapter III is confirmed by code reading** (the user could not
reach it): `wants_target` covers chapter 3, and `DefaultSagaChapterTarget` is its only selection path,
identical to chapter II.

### 1a. Chapter I — which creature to free-cast, and whether to do it at all

*"The next red or green creature spell you cast this turn can be cast without paying its mana cost."*

`PerformSagaFreeCast` (~18888) collects one candidate per distinct creature NAME in hand, then picks
by simulating each on a copy of the state and scoring
`(OwnAttackingPower delta) * 1000 + total board power` (~18935). No human chooser, no plan axis.

Two distinct losses:
* **WHICH creature is never asked.** The trial-on-a-copy scoring is genuinely good — it fires ETBs, so
  it does see Ghalta deploying the hand — but it is still a fixed greedy rule the human cannot override.
* **DECLINING IS IMPOSSIBLE, and that is a rules narrowing.** The card says the spell *can* be cast
  without paying — permission, not obligation. The code only declines when `cand_slots.empty()`. A
  human may legitimately want to keep a green creature in hand (for a later Ghalta deploy, or to cast
  it on their own terms) and the engine will not let them.

### 1b. Chapters II and III — the target

Both chapters target:
* **II** — *"Put three +1/+1 counters on target creature you control."*
* **III** — *"Choose target creature you control. Until end of turn, double its power and toughness…"*

`FireSagaChapter` (`src/core/SpellEffects.h` ~18974) resolves the target with a single hard-coded
heuristic and no hook of any kind:

```cpp
const int ti = DefaultSagaChapterTarget(state, controller);
```

`DefaultSagaChapterTarget` (~18831) is `key = (swings_now ? 1000 : 0) + EffectivePower()`, i.e.
**"the biggest creature that can attack right now."** There is no human chooser and **no plan axis** —
so the search cannot consider any other target either.

*Why the default is not always right:* chapter III DOUBLES power, so it wants the biggest body, but
chapter II's three +1/+1 counters are PERMANENT. Putting them on a creature that is about to be
sacrificed to Natural Order, or on a mana dork that will be chump-blocked forever in a real game, is
a different decision from "biggest attacker". The heuristic also cannot know that the chapter-III
target is about to be doubled AGAIN by a Craterhoof pump.

**Fix:** a searched target axis is the honest answer (it is one variant per distinct own creature, the
same shape Natural Order's victim already uses), plus a `target` viewer decision. At minimum the
viewer must offer it — the never-narrow-a-legal-choice rule is not conditional on the search caring.

---

## 2. Natural Order cannot sacrifice a creature cast EARLIER IN THE SAME PLAN (SEARCH + HUMAN)

USER, precisely: *"I was able to choose a victim if I put Fyndhorn Elves + Natural Order as the plan,
I just wasn't able to choose Fyndhorn Elves."*

Confirmed at frame 10 of the reference (Fyndhorn Elves still in hand; board had Craterhoof #4,
Elvish Mystic #11/#12, Priest of Titania #42):

| plan | victims offered |
|---|---|
| Natural Order ALONE | 4, 11, 42 |
| cast Fyndhorn Elves **+** Natural Order (130 plans) | 4, 11, 42 |

**Identical.** Fyndhorn Elves (#28) is never a victim, in any of the 130 plans where it enters the
battlefield before Natural Order resolves. The candidate set is snapshotted from the board as it
stands BEFORE the plan runs.

This is legal in real Magic and it is a real line: sacrificing the dork you just played keeps a
better body on the board, and board width feeds Craterhoof's X. **The search is narrowed too** — it
cannot find "deploy the worst creature, then eat it" as a single turn.

Note the victim enumeration is otherwise correct: it emits one variant per distinct green creature
NAME (same-name copies fungible), which is why #11 appears but #12 does not.

---

## 3. The Natural Order victim is serialized under the WRONG NAME and omitted from the summary (VIEWER)

Even the victims that ARE offered are indistinguishable in the UI. Four plans render identically:

```
id=1364  "land=none; cast: Natural Order → Ghalta, Stampede Tyrant"   soulfire_targets: 11
id=1365  "land=none; cast: Natural Order → Ghalta, Stampede Tyrant"   soulfire_targets: 42
id=1366  "land=none; cast: Natural Order → Ghalta, Stampede Tyrant"   soulfire_targets: 28
id=1367  "land=none; cast: Natural Order → Ghalta, Stampede Tyrant"   soulfire_targets: 4
```

11 = Elvish Mystic, 42 = Priest of Titania, 28 = Fyndhorn Elves, 4 = Craterhoof Behemoth.

Two separate problems:
* **Field overload.** `TurnSolver.h:335 soulfire_own_targets` is documented as "Soulfire Eruption:
  searched COUNT of own creatures". Natural Order reuses that int slot to carry its victim's CARD
  NUMBER (`TurnSolver.cpp` 22136, 23454). A *count* of 42 is impossible on a 9-permanent board. The
  engine-internal overload is defensible; blindly emitting it as `"soulfire_targets"` is not
  (`src/main.cpp:1612`). A separate `sac_victim_id` field already exists and is used elsewhere.
* **The summary never renders the victim**, so the four plans are visually identical.

**Fix (presentation only, must be play-neutral — verify with a digest):** emit
`"sac_victim": "<card name>"` when the card has `sac_additional_creature_color`, and render it in the
summary, e.g. `cast: Natural Order (sac Fyndhorn Elves) → Ghalta, Stampede Tyrant`.

---

## 4. UX: EVERY one of these selections is made IN ITS ZONE, never in a dialog

USER: *"targeting should be on the board, not in a dialog"*; *"(sacrifice is not targeting, but it
deserves the same treatment)"*; and, generalising it: ***"All of these decisions should be done on the
board or hand"*, *"(not in a separate dialog)"***.

This is a requirement on ALL of 1–3, not a separate nice-to-have. You pick the object by clicking it
where it already lives:

| decision | mechanic | zone to click |
|---|---|---|
| Hulk ch. I — which creature to free-cast | additional permission (CR: *can* be cast) | **HAND** |
| Hulk ch. I — decline | — | a plain "no" affordance, not a modal |
| Hulk ch. II — three +1/+1 counters | **targets** | **BATTLEFIELD** |
| Hulk ch. III — double P/T | **targets** | **BATTLEFIELD** |
| Natural Order — sacrifice victim | **additional COST** (CR 601.2h) | **BATTLEFIELD** |

**The user's rules correction is right and worth preserving:** Natural Order's sacrifice is an
additional cost, not targeting — chosen as the spell is cast, never using the word "target", so it is
unaffected by shroud/hexproof and not rechecked on resolution. Hulk's chapters II/III genuinely do
target. Three different mechanics (cost, target, permission) that all want the same interaction:
**click the object in its zone.** The dialog is the wrong affordance for every one of them.

Applies to `tools/play/index.html` (the `target`/`sacrifice` decision rendering path around lines
3087 and 3707), which must also learn a HAND-click selection mode for chapter I.

---

## Sequencing

> **SUPERSEDED — see the STATUS block at the top.** The prediction below (that fixes 1 and 2 would
> move GT, so they had to land before the rebaseline) turned out to be wrong, and measurably so:
> all four were implementable as human-play-only overrides behind choosers that `RevealLogPause`
> nulls, and 5,600 games produce byte-identical digests. The GT rebaseline and this work are
> independent; either can go first. Kept for the record.

Fixes 1–3 touch `src/`, so they change the binary. **The 21 stale `stompy`/`stompy2hg` GT keys have
not been rebaselined yet — land these fixes FIRST**, so the rebaseline measures the engine we intend
to ship rather than needing a second pass. Fix 3 is presentation-only and should be provably
play-neutral (digest check); fixes 1 and 2 change play and legitimately move GT.
