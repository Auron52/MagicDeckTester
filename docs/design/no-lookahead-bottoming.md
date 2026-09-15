# The profile replaces BOTH keep and lookahead — no deferral to the lookahead bottomer

**USER DIRECTIVE, 2026-09-14:** *"I don't want to use lookahead bottoming for anything"* — *"The
point of the profile is to replace both keep and lookahead."*

An exhaustive mulligan profile exists to answer **every** decision it covers. A policy that hands
part of its bottoming back to `AIEngine::BottomCards`'s lookahead bottomer has not replaced
lookahead; it has hidden it. This is a **standing constraint on what may ship**, not a tuning
preference, and it overrides any measurement that recommends deferral.

## Why this is being written down

On 2026-09-13 FiveColour adopted a bottoming table built with the winner's-curse gate of
[keepgen-bottoming-winners-curse.md](keepgen-bottoming-winners-curse.md) at `k=1.0`. That gate's
mechanism is *"decline to answer when the margin is noise"* — it emits an empty target vector,
`ExhaustiveKeepPolicy::DecideBottom` rejects it, and **the engine's lookahead bottomer takes the
decision**. Measured on the shipped artifact:

```
K=27 entries=1977898 slots=27690572 deferred=15621158 (56.4%)
```

**56.4 % of FiveColour's bottoming decisions were being made by the lookahead bottomer in real
play.** The gate was adopted on a genuine measurement (confounded A/B +0.0184 → −0.0021 t) and the
diagnosis behind it is sound; what nobody surfaced is *what the improvement was spending* — the
deck's own table, on the majority of its slots.

## The constraint does not mean "accept the winner's curse"

The bias the gate was built to fix is real: `best_sub`'s argmin over noisy candidate estimates
systematically selects the luckiest estimate, and at the mulligan depth that dominates play **half
the apparent spread between candidates is pure sampling noise**. That analysis stands.

What the constraint rules out is only the *deferral* half of the remedy. The correction splits
cleanly in two, and the code already separates them (`src/analyzer/ExhaustiveKeep.cpp:400`,
`gate_k <= 0 disables the gate`):

| lever | env | what it does | allowed? |
|---|---|---|---|
| **shrinkage** | `MTG_KEEP_BOTTOM_SHRINK=1` | re-ranks the argmin on posterior means `Z` shrunk toward a structural prior | **yes** — the table still answers every slot |
| **gate** | `MTG_KEEP_BOTTOM_GATE_K=k` | emits nothing when the margin is within noise → lookahead decides | **no** — must be `0` |

From the doc's own gate sweep, the shrinkage-only point is not a compromise — it is most of the win:

| gate k | defer % | predicted delta |
|---|---|---|
| **0.00** | **0 %** | **+0.0094** |
| 1.00 | 56 % | −0.0032 |
| (ungated, no shrink) | 0 % | +0.0184 |

So `k=0` with shrinkage on roughly **halves** the regret versus the raw ungated table while deferring
nothing. That is the configuration this repo ships.

## Consequences for the existing gates

1. **`MTG_KEEP_BOTTOM_GATE_K` must be `0` on every profile build.** `MTG_KEEP_BOTTOM_REFINE=1`
   alone defaults `gate_k` to **1.0** — the flag's default is deferral, so it must always be set
   explicitly.

2. **The confounded bottoming A/B is no longer an adoption *gate* for bottoming — it is a
   diagnostic.** Its question is "does the blind table beat the lookahead bottomer?", and both
   answers to that question now lead to the same action, because neither "ship bottoming off" nor
   "defer to lookahead" is available. When the table loses, the response is the one
   `mulligan-profile.md` already prescribes — **raise R or improve the estimator** — never route
   decisions back to lookahead. Keep running it; read it as a measure of how much estimator work is
   still owed.

3. **`scripts/mullgen.sh`'s quarantine path is now backwards for bottoming.** On a confounded
   bottoming failure it renames the profile to `.DISABLED.json`, which is presence-gated
   deactivation — the deck then falls back to lookahead bottoming **for 100 % of decisions**. Under
   this directive that is strictly the worst available outcome, worse than shipping the imperfect
   table it was protecting against. A bottoming failure should quarantine nothing; it should report
   and leave the table live. (The *keep* half of that gate is unaffected and still sound.)

4. **Generation does not apply the correction at all** — `BuildPolicyFromTables` is called at
   `ExhaustiveKeep.cpp:1198` with no refine argument (defaults `nullptr`); only the merge path at
   :4909 passes `brc_p`. So a freshly generated deck ships an **ungated, unshrunk** table: no
   deferral (which satisfies this directive) but the full winner's curse. Closing that is open
   follow-up #1 in the winner's-curse doc and is now the higher priority of the two.

## How to build a compliant profile from an existing raw

No rollouts, no regeneration — the rebuild is exact (verified: `entries` byte-identical, `D_opt` to
6 figures). `MTG_MERGE_BOTTOM_FLOOR` must reproduce the generation's adaptive-bottom filter (`2` for
a `fast`/R30 run); omitting it silently re-admits floor-R cells the generation excluded.

```bash
zcat decks/<D>/<D>.keepmodel.exhaustive.raw.json.gz > /tmp/<D>.raw.json
MTG_KEEP_MERGE=1 MTG_MERGE_INPUTS=/tmp/<D>.raw.json \
  MTG_MERGE_BOTTOM_FLOOR=2 \
  MTG_KEEP_BOTTOM_SHRINK=1 \
  MTG_MERGE_OUT_PROFILE=/tmp/<D>.profile.json MTG_MERGE_OUT_RAW=/tmp/<D>.raw.out.json \
  build/Release/mtg-analyze decks/<D>/<D>.cod --cards-json src/cards/data/cards.json
```

> **Flags changed 2026-09-15.** `MTG_KEEP_BOTTOM_SHRINK` is now the *only* knob;
> `MTG_KEEP_BOTTOM_REFINE` and `MTG_KEEP_BOTTOM_GATE_K` are **retired and no longer exist**. The gate
> was deleted from `BuildPolicyFromTables` outright rather than left switchable, so the
> non-compliant configuration this document warns about — `REFINE=1` alone defaulting `gate_k` to
> 1.0 — is now *unrepresentable* rather than merely off. The shrunk argmin always emits a
> full-length target, so **deferral is zero by construction** and the verification below cannot fail
> for a profile built by this path. Run it anyway on anything you did not build yourself.

Verify compliance before shipping — this check is the point of the whole document:

```bash
python3 - <<'PY'
import json
ek = json.load(open('/tmp/<D>.profile.json'))['exhaustive_keep']
K = len(ek['buckets']); tot = defer = 0
for e in ek['entries']:
    for row in e.get('bottom_keep') or []:
        tot += 1; defer += (len(row) != K)
print(f'slots={tot} deferred={defer} ({100.0*defer/tot:.2f}%)   MUST BE 0.00%')
PY
```

## The `fast` recipe is what creates the hazard — `complete` does not (surveyed 2026-09-15)

`test/keepraw_pool_survey.py` over all 22 committed raw sidecars. Only four decks have any cells at
the refinement floor, and they are **exactly the four generated with `--gen-mulligan fast`** (cap
R=30, adaptive bottoming). Every `complete` deck (R=40, `sub_target:40`) refines every sub-cell, so
it has no floor cells and none of this hazard:

```
deck                    K   R     cells  floor%      dV   corr  se(fl)  sigma
Creature Giving        21  30    438322   35.8%  +1.186  -0.67   0.363   3.27
FiveColour             27  30   1317366   41.2%  +1.237  -0.70   0.464   2.66
Goblins                21  30    359376   58.2%  +1.628  -0.69   0.558   2.92
Melira Pod             23  30    600866   45.4%  +1.446  -0.72   0.472   3.06
```

`dV` = meanV(floor) − meanV(refined), positive meaning floor cells are genuinely worse. It is
**+1.19 to +1.63 turns on 4 of 4 fast decks and absent on 18 of 18 complete decks**, with
`corr(cnt,V)` between −0.67 and −0.72 throughout.

**So `fast` is not merely "less R".** It buys its ~22–38 % saving by leaving the *worst* cells
unrefined, which creates a candidate pool where the noisiest options are also the bad ones. That is
a different kind of cost from uniform imprecision and it is not mentioned in the recipe study. Weigh
it when choosing a recipe: `complete` is the safer artifact, not just the more precise one.

(What this hazard does **not** do is corrupt the keep policy — see
[keep-argmin-winners-curse.md](keep-argmin-winners-curse.md) §7, where it was tested and found inert
because the affected hands are clipped by the mulligan alternative. `best_sub` already filters it out
of the bottoming targets. So the measured harm is confined to the ~6 % of bottoming decisions where
no candidate is refined and the filter falls back to the unfiltered set.)

## The offline gate sweep does NOT generalise across decks

`test/keepraw_hybrid_sim.py` hard-codes `L_LOOK = 0.0185` (line 46) — the confounded-lookahead regret
**back-solved from FiveColour's own A/B**. It prints that constant as "vs lookahead" for whatever raw
it is pointed at. Run on Melira it predicts the table *loses* by +0.0161 t; Melira's actual confounded
bottoming A/B measured **−0.091 t**, the table winning 16/16 seeds. Sign wrong, magnitude out ~6x.

Because `delta(k) = F · (reg(k) − L)`, the constant **cancels when two gate settings are
differenced** — which is the only way it has been used successfully (predicted k=0-vs-k=1 gap
0.0171 t against a measured 0.0142 t). Use it to rank table variants against each other; do **not**
read its PASS/reject verdicts against lookahead on any deck but FiveColour.

## Status

- **Melira Pod** — compliant. Generated before the gate existed; artifact check reported
  `malformed_bottom_keep=0`, i.e. every slot answered by the table. Adopted `f935285e`. The gate was
  considered for it and **rejected on this directive**.
- **FiveColour** — **NON-COMPLIANT at 56.4 %**, adopted `9feb6bf2`. Rebuild at `k=0` in progress.
- **Every other deck** — generated before the gate existed, and generation never applies it, so all
  are structurally compliant (and all carry the uncorrected winner's curse).
