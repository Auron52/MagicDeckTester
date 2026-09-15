# Sagas (CR 714) + World War Hulk — implementation and Stompy screening

Date: 2026-09-15. Engine at `25d92282` + this change. Deck: `decks/StompySurprise/`.

Two things landed together: the **first Saga mechanic** in this engine, and a screening pass that
answers where World War Hulk, a 4th Natural Order and an Apex Altisaur belong in Green Stompy.

---

## 1. The Saga mechanic

`Permanent::lore_counters` + `CardParams::saga_chapters` and one param per chapter. Scheduling is
generic; the chapter effects are card-specific, which is the house idiom.

| piece | where |
|---|---|
| enter (CR 714.2a): lore = 1, chapter I | `FireOwnEtbTriggers` RECORDS into `g_pending_saga_enters`; `DrainPendingSagaEnters` resolves |
| draw step (CR 714.2b): lore += 1, next chapter | `AdvanceSagas` — `GameEngine::DrawStep` **and** the rollout draw in `TurnSolver.cpp` |
| sacrifice after the final chapter (CR 714.4) | inside `AdvanceSagas` / the enter drain |
| memo key | `BuildSimKey` folds `lore_counters` **when nonzero** |
| dominance | `DomAxis::LoreCounters`, `EqualRequired`, folded **when nonzero** |

### Why the enter is deferred rather than resolved in the cascade

Chapter I appends a permanent (it free-casts a creature). Callers of the enter cascade hold saved
battlefield slot indices — the same hazard `g_pending_self_bounces` was built for, and its comment
says so explicitly. Recording and draining at the existing drain points reuses that solution
instead of re-discovering it.

### TWO TRAPS, both of which produce a plausible-looking wrong answer

**1. `EqualRequired` dominance axes fold UNCONDITIONALLY.** Every permanent in every deck
contributes `(0xE00+i, 0)` for each `EqualRequired` axis it does not use, so the match hash depends
on the *set* of axes. Appending `LoreCounters` therefore re-hashes every permanent in every deck —
including decks that cannot reach the axis. The fold is now skipped when the value is zero. This
was **not** observable in smoke (the dominance path is not enabled in shipped play), which is
precisely why it is guarded: switching dominance on later would otherwise silently rebaseline the
fleet. Give any future `EqualRequired` axis the same treatment.

**2. Colour lives on the DATABASE card, never on the zone copy.** The `Card` objects moving between
zones are lightweight — `PutCardOntoBattlefield` itself does `perm.card = d->card` and keeps only
`m_number` from the copy it is handed — so `m_color_mask` is **empty** on a hand card. Chapter I's
first implementation tested the hand copy, every green fatty scored as an illegal candidate, and
the chapter silently never fired. The card then measures as *merely weak* rather than broken, which
is the worst possible failure mode for a screening result. Caught by `MTG_SAGA_TRACE`; use
`d->card` for any colour/type test on a card outside the battlefield.

### World War Hulk — modelling decisions (all disclosed in its `oracle_text`)

**Chapter I is the card.** "The next red or green creature spell you cast this turn can be cast
without paying its mana cost" — in this deck, a free Worldspine Wurm at **mana value 11**.

It is deliberately **not** routed through `GameState::free_casts_available`. That bank is a
phase-boundary mechanism (filled by combat damage, spent post-combat), and the plan enumerator only
emits free-cast variants when the bank is already nonzero *at enumeration time*. A Saga grants its
charge mid-main-phase, which that machinery cannot express without reworking subset validity,
ordering and dedup in the hottest code in the repo.

Instead chapter I resolves **inside the Saga's own resolution** — the house pattern for a
resolution-time choice (Terastodon's put path, the Lackey put, the sac-tutor put-list). The
ordering freedom given up is dominated: the controller already chooses *when* in the main phase to
cast the Saga, so "deploy the elves, then cast this and free-cast Craterhoof" is expressible. Only
"cast it early and the freed spell later in the same turn" is lost, and that is never better.

**The free-cast choice is scored, not guessed.** Candidates are the distinct red/green creature
names in hand, ranked by attack damage added *this turn* — measured by doing the put on a copy of
the state, so a Craterhoof team-pump or a Hornet Queen token wave is measured rather than
approximated — tie-broken by board power left behind. This is load-bearing: a hasty Craterhoof can
beat a summoning-sick Worldspine Wurm, which a naive highest-mana-value pick gets backwards. There
is **no searched axis** on the choice (same disclosure as Terastodon's put path).

Also disclosed: the freed creature is *put* through the ETB cascade rather than cast through the
stack (inert here — no cast triggers, no counterspells, nothing reads storm); chapter III's trample
grant is not modelled (inert — the opponent never blocks).

`MTG_SAGA_TRACE=1` prints what each chapter saw and chose.

---

## 2. Screening results

Apparatus: the shipped R=40 / K=15 keep table with new cards **aliased into the bucket of the card
they replace** (the approved route — nothing generated, K unchanged), `leaf:none` d6/b20 from the
deck's `value_play`, 20,000 paired games per arm, one pooled batch per screen.

Base avg win turn ≈ **4.54**. Negative delta = faster.

### Natural Order and Apex Altisaur (Screen A)

| arm | delta | note |
|---|---|---|
| `no4_guile_elder` NO 4, −Mirri's Guile, −Elderscale Wurm | **−0.1626** | best NO home |
| `no4_cotw2` NO 4, −2 Call of the Wild | −0.1530 | |
| `ref_no4_queen0_elder0` NO 4, −Hornet Queen | −0.1517 | **worse** than cutting Guile/Elderscale |
| `no3_cotw3` NO 3 | −0.0878 | 4 beats 3 |
| `altisaur1_wurm3` | **+0.0415** | costs turns |
| `altisaur2_wurm2` | +0.0467 | 2 is worse than 1 |

Held out (`--confirm no4_guile_elder`): −0.1644 vs −0.1626, shrinkage t = −0.37 → **−0.1635 over
40,000 games**.

**Hornet Queen is a keep.** Cutting her is measurably worse than cutting Mirri's Guile + Elderscale
Wurm for the same slots. Her four Insect tokens are green (`created_token_color G`), so they are
Natural Order sac fodder *and* four extra bodies for Craterhoof's X.

### World War Hulk, count and spot (Screen B)

| arm | delta |
|---|---|
| `wwh4_cotw0` | **−0.0866** |
| `wwh3_cotw1` | −0.0696 |
| `wwh2_guile_elder` | −0.0609 |
| `wwh2_cotw2` | −0.0491 |
| `wwh1_cotw3` | −0.0260 |
| `wwh2_forest12` (−2 Forest) | +0.0086 |
| `wwh2_tutor2` (−2 Worldly Tutor) | **+0.0381** |

Monotone in the count: World War Hulk is strictly better than Call of the Wild, and you want the
maximum. **Do not pay for it with Worldly Tutor** — that is the worst spot tested, which confirms
the tutor→draw→free-cast line is real and load-bearing. Lands are not the payment either.

**A prediction that was wrong, recorded because it was wrong.** Before measuring, the expectation
was that World War Hulk would underperform badly: chapter I frees a creature *from hand*, whereas
Natural Order and Call of the Wild fetch *from the library*, and this deck's fatties measure as bad
to draw (Hornet Queen −0.482, Vaultborn Tyrant −0.365, Worldspine Wurm −0.275 in the shipped
profile) precisely because they are meant to be tutored, not drawn. The measurement refuted the
direction outright. Eleven fatties plus four Worldly Tutors is apparently dense enough that chapter
I connects often, and 5 mana for a free 15/15 beats Call of the Wild's 4-plus-4.

### The combination (Screen C)

| arm | delta |
|---|---|
| **`wwh4_no4`** | **−0.2255** |
| `wwh3_no4` | −0.2133 |
| `wwh4_no4_tera1` (−1 Terastodon) | −0.2128 |
| `wwh2_no4` | −0.1976 |
| **`wwh4_no4_altisaur`** | **−0.1948** |
| `wwh3_no4_altisaur` | −0.1767 |
| `wwh4_no3` | −0.1645 |
| `wwh4_no4_hornet2` (−Craterhoof, +Hornet Queen) | **+0.0431** |

Held out: `wwh4_no4` → **−0.2272 over 40,000 games** (shrinkage t = −0.62);
`wwh4_no4_altisaur` → **−0.1927 over 40,000 games** (shrinkage t = +0.74).

**Craterhoof Behemoth is untouchable.** Trading it for a second Hornet Queen does not merely lose
the gain, it turns a −0.22 list into a +0.04 one — a 0.27-turn swing off one card. Its `ms/game`
also jumped 34.9 → 83.8, i.e. the search works far harder without it.

### Recommended lists

```
wwh4_no4  (fastest measured, -0.2272 over 40,000 games)
  +4 World War Hulk        Natural Order  2 -> 4
  -4 Call of the Wild      Mirri's Guile  1 -> 0
                           Elderscale Wurm 1 -> 0

wwh4_no4_altisaur  (the same, with Apex Altisaur -- -0.1927 over 40,000 games)
  as above, plus:  Apex Altisaur 0 -> 1,  Worldspine Wurm 4 -> 3
```

**Apex Altisaur costs 0.0345 turns** (−0.2272 → −0.1927). That is the price of the slot, and it is
a **floor, not a verdict**: its ETB fight and Enrage are both provably inert against this sim's
passive opponent, which never controls a creature (Terastodon destroys *our* noncreature permanents
and gives *us* the Elephants). The card is played for creature removal, and creature removal does
not exist here. User acknowledged this before implementation.

### What is NOT measured

`--floor` / `--with-floor` generate a table per arm, which the approved alias route forbids, so the
**apparatus bias floor is unmeasured** for every number above. The headline effects (−0.19 to
−0.23) are 20–45x a typical measured floor (0.005–0.01) and the confirmations reproduce on disjoint
seeds, so they are safe. Apex Altisaur's +0.035 is a 3–7x margin — real, but the weakest claim
here. Gaps between adjacent arms (e.g. `wwh4_no4` vs `wwh3_no4`, 0.012) are **not** resolved
against an unmeasured floor; treat the count ordering as a trend, not a per-copy verdict.

Composition fall-through from raising Natural Order 2→4 is 0.39% of hands (≈0.00024t, one-sided),
under the 1% limit. The count-axis arms are exactly bucket-neutral (0%).

Adoption still owes the normal artifacts — `mulligan-profile.md` then `value-leaf.md` — plus its
own regression ground truth. These numbers are a **ranking**, not the adopted deck's strength.

---

## 3. Unrelated: 5 smoke cases are RED at HEAD, before any of this

`fivecolour_smoke_d0/d3/d5`, `fivecolour2hg_smoke_d3`, `critter2hg_smoke_d3` fail against committed
GT at `25d92282`. Verified by building **clean HEAD in a separate worktree**: byte-identical `got=`
digests to this branch, so they are not caused by the Saga work. All five are *faster* than GT
(e.g. `critter2hg` 5.2000 → 4.7600), which is the signature of GT that was not re-accepted for
those tiers after an engine or decklist change — plausibly the CritterLifegain v2 adoption, whose
rebaseline covered the `critter` tier but not `critter2hg`.

This needs an owner's decision to re-accept; it is not this change's to make. With it, smoke is
**164 PASS / 5 FAIL both before and after** the Saga work.
