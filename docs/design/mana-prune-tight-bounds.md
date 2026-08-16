# Tight mana bounds for pruning — what is SHIPPED, what is BROKEN, and the order to fix it

Status: one fix SHIPPED (`160c44e`), one defect FOUND AND MEASURED (not fixed), one design agreed.
Direction set by the user 2026-08-16: *"we should be looking for a relatively tight bound that is not
highly computationally expensive and using that to prune. Dropping individual cards from
consideration based on these is another way we can prune many plans."* Self-contained.

## 1. SHIPPED: the flow oracle no longer bails on domain sources

The infeasibility oracle used to list `domain_mana` (Bloom Tender, Faeburrow Elder) as unmodellable
and **switch the whole oracle off** whenever one was on board. Domain is not unmodellable — it is
*more* constrained than an ordinary source: "for each color among permanents you control, add one
mana of that color" is not a choice, so total yield is `|domain set|` with at most **one** mana into
any single colour. Splitting the source capacity into `amt` (tap total) and `per_col` (per-colour
cap) expresses both shapes exactly; `per_col == amt` keeps every other source byte-identical.

Measured on FiveColour, 20 games at the gen settings (d3/b3):

| | before | after |
|---|---|---|
| backtracker nodes | 1,922,459 (66.5/entry) | **959,350 (33.2/entry)** |
| nodes spent proving UNPAYABLE | 1,069,147 = 51.2% | **36,208 = 3.6%** (83.6 → 2.8 per entry) |
| oracle bailed | 13,072 = 45.2% | **0** |
| oracle pruned | 2,458 = 8.5% | 4,665 = 16.1% |

Lossless, verified: smoke 36 passed / 0 failed, 0 searched and 0 d0 play-changed, 0 play-drift over
196 references.

## 2. FOUND: `MTG_EMIT_PRUNE`'s ceiling is LOSSY today — fix this BEFORE tightening anything

`OptimisticTurnMana` is the enumeration-side ceiling (drop a subset whose total MV exceeds the
turn's optimistic mana). It ships **default OFF**, and the reason is not conservatism — it is wrong:

```
                       prune OFF          prune ON (HEAD)
fivecolour d0          6.1850             6.2340
fivecolour d3          5.1200             5.1667
fivecolour d5          5.2133             5.2133 (digest differs)
mirrorwing d0          6.3480             6.4070
mirrorwing d3          5.1867             5.2133
mirrorwing d5          5.0800             5.0933
```

Every difference is WORSE, so the ceiling **under-credits** some mana source and drops legal plays.
That is exactly the failure its own comment records ("a bound built from them broke 12 smoke cases
across four decks"). **Any tightening built on this bound inherits the defect**, so the order is:
find the under-credit first, make prune-ON byte-identical to prune-OFF, and only then relax
bail-outs or add a per-card filter.

The A/B that settles it is cheap and should be the acceptance test for every later change here —
a lossless prune must be BYTE-IDENTICAL, not merely "close":

```bash
./build/Release/mtg --batch <manifest> --threads 0 | grep -oP '^\S+: played=\d+ avg=[\d.]+ digest=\w+' | sort > off.fp
MTG_EMIT_PRUNE=1 ./build/Release/mtg --batch <manifest> --threads 0 | grep -oP '...' | sort > on.fp
diff off.fp on.fp     # must be empty.  NB: strip ms= -- wall time differs per run and is not a result
```

### 2a. A correct sub-fix, held until the above is fixed

`budget_can_grow` declines the prune whenever any permanent or hand card has `mana_rock`. Both sides
of that are wrong (user: *"Ancient Cornucopia is not a net mana adder"*):

* **on the battlefield** a rock is already counted — `AvailableManaPool` credits `mana_rock`
  unconditionally — so it cannot grow the budget at all;
* **from hand** it costs its mana value and returns `ManaProducedPerTap`, so it grows the budget only
  when NET POSITIVE (Sol Ring, {1} for two). Ancient Cornucopia is `{2}{G}` for one mana of any
  colour: **net −2**. It does not raise the ceiling, it means a line containing it needs two MORE
  mana. Crediting it as growth is backwards.

The fix is a `from_hand` parameter plus `ManaProducedPerTap(d) > d.card.m_mana_cost.ManaValue()`.
Measured: it is a no-op on Mirrorwing (no rock — bit-identical to HEAD, which confirms the change
touches only rock decks) and on FiveColour it makes the prune fire more, which under a LOSSY prune
reads as a further regression (d0 6.2340 → 6.2500). **Correct in itself, harmful while §2 stands.**
Apply it after, not before.

## 3. AGREED DESIGN: per-card feasibility as an odometer-dimension prune

The user's framing: *"can I play this single card"* — if a card cannot be cast on its own using the
whole untapped board, it cannot be cast in any subset either (the other members only consume mana),
so it can be dropped from the candidate pool entirely. That removes a **dimension** from the
powerset rather than rejecting subsets one at a time, which is where the leverage is.

Sound except where a set member can genuinely ADD mana, and those are enumerable:

| exception | how to bound it |
|---|---|
| rituals | credit the float they add (the params already carry it, incl. the per-copy escalation) |
| net-positive rocks (Sol Ring) | credit `ManaProducedPerTap − ManaValue`; ordinary rocks are ≤ 0 and need no credit (§2a) |
| sac-for-mana / Treasures | credit the sacrificeable bodies |
| cost reducers / alt costs | a discount is equivalent to extra mana — credit the max discount |
| **domain widening** | see below — this is the one worth being tight about |

### 3a. The tight domain bound

Loose version (what `OptimisticTurnMana` does today): credit every live domain dork the full
headroom, `live_dorks * (5 - have)`. On a two-colour board with two dorks that is **+6 mana of pure
fiction**, and a ceiling that loose prunes almost nothing.

The user's tightening, which is both cheaper and much tighter:

* A domain source's own colours are always in its domain (it is a permanent you control), so
  Bloom Tender `{1}{G}` guarantees ≥1 and can gain **at most 4**; Faeburrow Elder `{1}{G}{W}`
  guarantees ≥2 and can gain **at most 3**.
* Widening requires a colour-adding permanent to actually RESOLVE this turn — which requires paying
  for it. *"Most of the time you need to use your scaling sources to play your first 5-colour
  permanent, so you cannot tap them for 5 mana."* The two uses are mutually exclusive: if the only
  way to cast the colour-adder is by tapping the dorks, the widened yield is unreachable.
* Therefore: **the headroom is ZERO unless some colour-adding hand permanent is castable using
  non-domain sources alone** (with `D` untapped dorks, using at most `D−1` of them). One flow-oracle
  call per colour-adding hand permanent, restricted to non-domain sources — cheap, and the oracle is
  now domain-aware.
* And once the board is at five colours the headroom is zero by definition — *"once that permanent
  is out, your scaling sources no longer do additional scaling."*

The rare line this must not break is real but narrow, and the user named it: Birds + 4 lands +
Bloom Tender + Faeburrow Elder → cast a rainbow permanent **without tapping either dork** →
Progenitus. The `D−1` formulation keeps it.

Note `ComputeAvailableColors` already has a widen for the COLOUR gate (credit colours a hand
permanent would add once it resolves, `fivecolour_domain_widen.json`) and it is deliberately loose —
"no castability test: a looser necessary condition is still sound". The same tightening applies
there, and it is the same one flow call.
