# The Equipment provider's cleanup-discard buckets are stale for the Puresteel/Sram list

**Status (2026-10-17): FIXED to the USER's ruling of 2026-10-08** in c1914f4e (same day):
`MTG_KE_BUCKET_V3` is `EnvOn(..., true)` (`KittyBucketV3Enabled`, just above
`EquipmentProvider::CleanupDiscardCandidates` in `src/ai/DecisionProviders.cpp`; `=0` restores the
v1-only tables). Sram joins the enabler bucket behind Puresteel and is capped at one, Sigarda's Aid,
{0} Equipment, Hammer/Gauntlets and the one-of Adelbert/Dwalin slot follow the user's allocation
clause by clause. The original header below is kept as history.

**Original status (2026-10-08, SUPERSEDED): FOUND 2026-10-08, NOT FIXED. The fix is a USER RULING (bucket policy is user-only).**
Surfaced while the v3 mulligan generation was ~30 min into a ~10.6 h run; the run was deliberately
**not** killed (see "What to do about the run in flight").

## The finding

`EquipmentProvider::CleanupDiscardCandidates` (`src/ai/DecisionProviders.cpp:19360`) sorts the hand
into buckets and then sheds **everything unbucketed FIRST** (line 19562):

```cpp
std::vector<int> shed;
for (int i : unbucketed) { if (!kept[i]) { shed.push_back(i); } }   // <- leads the shed order
for (auto it = equipment.rbegin(); ...)                              // then surplus equipment
```

That was written for **v1** (Kor Duelist / Balan double-strike + stacked equipment) and the only
cards it names are v1's. Checked mechanically against the shipped **v3** mainboard, 7 of 60 cards
fall through to `unbucketed` and therefore shed **ahead of surplus Equipment**:

| copies | card | why it is unbucketed | why that is wrong |
|---|---|---|---|
| **4** | **Sram, Senior Edificer** | creature, not in `enabler_rank` | the deck's **draw engine** — `draw_on_cast_subtypes` on a list with 33 Equipment |
| 1 | **Adelbert Steiner** | creature, not in `enabler_rank` | the 10th-creature slot the user chose on 2026-10-08 |
| 1 | **Dwalin, Weaponmaster** | creature, not in `enabler_rank` | hone counters on every Equipment, per attack |
| 1 | **Sigarda's Aid** | Enchantment, so `is_equipment` is false | flash + **free attach** — an enabler in all but type |

`enabler_rank` names `Puresteel Paladin / Stoneforge Mystic / Kemba, Kha Regent / Armored Skyhunter`;
only Puresteel is on the v3 list. `ds_rank` names `Kor Duelist / Balan, Wandering Knight` — **neither
is on the list, so the striker bucket is structurally EMPTY.**

### Three consequences, in descending confidence

1. **Sram sheds before a surplus Bone Saw.** Highest-confidence and the one that matters: four copies
   of the card that converts the deck's 20 free Equipment into draws are the first things thrown away.
2. **The creature bucket silently holds ONE card instead of two.** `board_ds` can never be true and
   both the striker and Kemba fallbacks are empty, so `creatures_kept` tops out at 1. The user's rule
   was *"You keep 1 of each"* — one enabler **and** one double striker — and half of it is now
   unreachable. It also feeds the residual: `equip_budget = max(2, 7 - creatures_kept - sources_kept)`
   gets a free slot it was not meant to have.
3. **27 of the 33 Equipment copies tie at the default rank 8.** `equip_rank` names only Colossus
   Hammer (0, correctly conditional on `cheat_secured`) and Shadowspear (3) from this list;
   Golem-Skin Gauntlets — a 4-of scaler, and the card round 8 priced at −0.0390 on its own — ranks
   equal with Bone Saw and is ordered by hand position. Weakest of the three: these are genuinely
   near-interchangeable, and the Equipment copy-fold handles identity elsewhere. Listed for
   completeness, not as a claim that it costs turns.

## Why it matters MORE than the hit rate suggests

The rule's own comment (line 19312) states the denominator, and it is the reason this was worth
stopping to check:

> the cleanup discard almost never fires in PLAY (measured: 1 real shed per 120 games with mulligans
> on, 9 with them forced off). Its denominator is the ROLLOUT — 425/game normal, 591/game forced-keep
> — **and above all the mulligan generator**, where a land-light hand cannot deploy and so crosses
> seven cards every turn. This is a rollout-model and keep-generation rule, not a play rule.

So the workload where this rule is loudest is **exactly** the keep generation. A keep/bottom table
built now is fitted to play in which Sram, Adelbert, Dwalin and Sigarda's Aid are discarded first.

## What to do about the run in flight — and why it was NOT killed

**v2 shipped under this identical condition and still produced a strong table**, which is the single
most important fact here and the reason this is not an emergency: v2 runs the same Sram ×4 / Dwalin /
Sigarda's Aid composition with no Kor Duelist, Balan, Kemba or Stoneforge, so its buckets fell through
the same way — and its adopted keep table measured **−0.6617 t, 16/16 seeds**. A table is the best
keep policy *for the play it was fitted to*; a weak discard heuristic makes the **play** worse, it
does not make the **table** invalid.

Two reasons not to kill it anyway:

* **The fix is not an agent's to make.** These buckets encode the user's own allocation, quoted in the
  source (*"You keep 1 of each"*, *"3 mana sources at most and 2 equipment"*, *"4 mana is pretty much
  the cap"*). Deciding whether Sram is an "enabler", whether Sigarda's Aid joins that bucket, and what
  — if anything — replaces the now-empty double-striker slot is a **bucket ruling, and those are
  user-only**. Killing a run for a fix that cannot be written yet trades 10 h of compute for an idle
  box.
* **The ~10-minute detection deadline had passed** (CLAUDE.md): past it, a user-requested run is the
  user's to cancel, not an agent's.

**The cost of being wrong is asymmetric and bounded, and it is stated plainly so the user can weigh
it:** any bucket fix changes play, which changes the play digest, which **invalidates this table and
requires regenerating** (~10.6 h). So proceeding risks paying that once more; stopping now risks an
idle box until the ruling arrives. Letting it finish also yields a usable table either way, and a
v3 table fitted to current play is strictly better than v2's table on a 4-card-different list.

## The fix, when it is ruled

Minimal shape, for the user to accept/amend rather than a decision taken:

1. Add `Sram, Senior Edificer` to `enabler_rank` — and decide its order against Puresteel. Castability
   argues Sram first ({1}{W} vs {W}{W}); payoff argues Puresteel first (free equips *and* draws).
2. Decide whether `Sigarda's Aid` joins the enabler bucket (it is the only non-creature candidate, so
   it needs its own clause — `is_equipment` is false for it).
3. Decide what the second creature slot means with no double striker on the list: `Adelbert Steiner`
   and `Dwalin, Weaponmaster` are the only candidates, and Adelbert is the one whose body scales with
   the Equipment already in play.
4. Optionally rank `Golem-Skin Gauntlets` ahead of the {0}-cost shields in `equip_rank`.

Each of 1-4 is separately measurable on the **keep-generation** workload, not on play — and the
measurement must be read there, because play fires this rule once per ~120 games.

Related: `docs/design/kitty-tutor-and-discard-heuristics.md` (the original v1 derivation and the user
quotes), `MTG_KE_BUCKET_DISCARD` / `MTG_KE_DISCARD_RESIDUAL` (the two existing A/B arms).
