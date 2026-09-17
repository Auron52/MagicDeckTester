# Decision-surfacing gaps found by hand-playing the new StompySurprise list

Found by the USER on 2026-09-17, playing the first reference on the promoted list
(`references/StompySurprise/claude_s1_gi0.json`, seed 1 gi 0, won T4). Three defects and one UX
request. Diagnosed from that game by replaying its `--choices` stream frame by frame.

**Two of these narrow a legal choice, which this repo forbids outright.** One of the two narrows it
for the SEARCH as well, so it is not merely a viewer problem.

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

Fixes 1–3 touch `src/`, so they change the binary. **The 21 stale `stompy`/`stompy2hg` GT keys have
not been rebaselined yet — land these fixes FIRST**, so the rebaseline measures the engine we intend
to ship rather than needing a second pass. Fix 3 is presentation-only and should be provably
play-neutral (digest check); fixes 1 and 2 change play and legitimately move GT.
