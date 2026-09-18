# Tokens can never be equipped — a rules bug in the equip host enumeration

**Status:** FIXED 2026-09-18 (`MTG_EQUIP_TOKEN_HOST`, default ON) — see "The fix, as built".
**Found by:** the user, hand-playing Angels seed 1 via the play viewer — *"I had no means to equip
the new token with Lightning Greaves. I'm not sure if search has the same issue."*

## The bug

A creature **token** is never offered as an equip host, in either world. Equip targets "creature you
control" (CR 702.6b) and a token is a creature, so this is a **rules/modelling bug**, not a heuristic
choice.

**The search has the same issue — it is the same code path.** The viewer renders the plans the
search enumerates, so the missing option is missing from the autonomous search too. That is the
answer to the user's question, and it makes this a play-quality bug rather than a UI gap.

## Evidence

`references/Angels/claude_s1_gi0.json`, decision **[13]** (T4 pre-main). Board:

```
4/4 Angel Token, 4/4 Angel Token, Lightning Greaves (UNATTACHED),
Righteous Valkyrie, Youthful Valkyrie, Seraph Sanctuary, Sol Ring, Plains x2
```

All 21 enumerated plans offer only `equip Lightning Greaves → Youthful Valkyrie` or
`→ Righteous Valkyrie`. **Neither token appears in any plan.** Same at decision [12], where one
Angel token was already on board.

This is *not* the host-ranking heuristic. Re-running the reference to that decision with
`MTG_EQUIP_ALL_HOSTS=1` — the documented A/B switch that emits **every** host rather than the single
heuristic pick — produces the **identical eight equip summaries**, still with no token. So the token
is being excluded at the legality level, before ranking.

Repro:

```bash
CH="1,2,-1,-1,6,0,-1,-1,5,0,-1,-1,20"
MTG_EQUIP_ALL_HOSTS=1 ./build/Release/mtg decks/Angels/Angels.cod \
  --profile decks/Angels/Angels.profile.json --games 1 --seed 1 --claude-play --choices "$CH"
```

## Root cause

`TurnSolver.cpp`, the equip host/equipment collection loop:

```cpp
for (const Permanent& p : state.battlefield)
{
    if (p.controller_index != state.active_player_index) { continue; }
    const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
    if (!d) { continue; }                      // <-- tokens die here
    if (d->params.is_equipment) { ... }
    if (p.card.IsCreature() || p.is_animated) { ... hosts.push_back(...) }
}
```

`CreateTokenOnce` (`SpellEffects.h`) builds a token with a **synthetic name** —
`token.card.m_name = token_name` (e.g. `"4/4 Angel Token"`), `token.is_token = true` — and there is
no `CardDefinition` under that name. So `LookupCached` returns null and the `if (!d) { continue; }`
skips the permanent **before** `IsCreature()` is ever tested. The guard exists to skip unknown
cards; it silently also skips every generic token.

**Copy-tokens are NOT affected.** `CreateTokenCopyOfCard` copies a real card wholesale, and its own
comment says so: *"LookupCached resolves the REAL definition"*. Only tokens minted by
`CreateTokenOnce` (`CreateToken`) are invisible here.

## Why it matters on Angels — and where it does NOT

Angels mints generic tokens from exactly two places, and they are **not** equally affected. The
distinction is the user's (2026-09-18) and it is the right one:

* **Serra the Benevolent −3 — THIS is the case that matters.** A loyalty ability is sorcery-speed,
  so the 4/4 flying/vigilance Angel arrives in a **main phase, before combat**. It is summoning
  sick, and Greaves' haste turns it into an attacker *that turn*. It is also routinely the biggest
  body on the board, so it is precisely what the deck's own ranking rule reaches for (*"equip to the
  largest power and/or effect creature that doesn't already have haste and is summoning sick"* —
  user, 2026-08-08). Losing this host loses real damage.

* **Resplendent Angel's trigger — same bug, no consequence.** *"At the beginning of each end step,
  if you gained 5 or more life this turn, create a 4/4 white Angel…"*. End step is **after combat**,
  so that token was never going to attack this turn no matter what, and by its controller's next
  turn it is no longer summoning sick — haste has nothing left to buy. Greaves' other grant, shroud,
  is inert against a passive goldfish opponent with no removal. USER: *"the end of turn Angel
  doesn't have the same issue … since it cannot attack either way."* The fix covers it too, but it
  should not be counted as part of the win.

So the expected quality effect is **narrower than the raw host count suggests** — one token shape in
one deck, not "every token on every board".

Worth noting for the ranking, not just the legality: in the seed 1 state the controller is at 28
life vs a starting 20, so Righteous Valkyrie's *"+2/+2 while 7 life above starting"* is live and the
tokens read as **6/6**. `EffectivePower()` sees that, so the token does not merely *join* the host
list — it **outranks** Youthful Valkyrie (1 base + 2 counters + 2 anthem = 5). This change therefore
moves the single heuristic pick, not only the enumerated set.

In the user's seed 1 the **win turn was unchanged** (T4), so this is not a demonstrated
strength loss in that game — but the option was absent, which is what the Ajani-0
never-narrow-a-legal-choice doctrine forbids independently of outcome.

## The fix, as built

`MTG_EQUIP_TOKEN_HOST` (default **ON**; `=0` restores the old exclusion for the A/B). The lookup
becomes null-tolerant instead of fatal, because a token legitimately has no definition and therefore
has default params:

```cpp
const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
if (!d && !(s_token_host && p.is_token)) { continue; }   // unknown NON-token: skip, as before
if (d && d->params.is_equipment) { equips.push_back(...); attached_to.push_back(p.equipped_to); }
if (p.card.IsCreature() || p.is_animated)
{
    if (d && d->params.protection_from_everything) { continue; }
    const bool src = d && ((d->tmpl == CardTemplate::ManaDork) || d->params.mana_rock);
    const int  sc  = p.EffectivePower()
                   + (src ? PermanentManaYield(state, p, *d) : 0)
                   + (d ? attack_payoff(*d) : 0);
    hosts.push_back({ p.card.m_number, p.entered_this_turn,
                      p.card.HasKeyword(Keyword::Haste), sc, /*in_hand=*/false });
}
```

`p.EffectivePower()` already works for a token, and `m_number` is a real unique id
(`next_token_number`, base 1000), so host identity is sound.

### The one thing it makes WORSE: the greedy host ranking is double-strike-blind

Smoke, token fix on: **searched depths are clean — slower=0, faster=0**, 12 play-changed at
identical score. The single blemish is at **d0** (greedy, no search): `kitty_smoke_d0_s1001`
4.9900 → **4.9910**, one game of 1000.

That game, `gi406` (seed 1407, `--game-index 406 --depth 0`), is worth reading because the cause is
NOT the token relaxation being wrong — it is a **pre-existing ranking weakness the relaxation
exposed**. Both arms put Bonesplitter on Kor Duelist. They differ on Lightning Greaves:

```
off:  T4  Bonesplitter -> Kor Duelist ; Greaves -> Kor Duelist     ATTACK 14  opp 0   WIN T4
on:   T4  Bonesplitter -> Kor Duelist ; Greaves -> 2/2 Cat Token   ATTACK 10  opp 4
      T5  ATTACK 36  opp -32                                                         WIN T5
```

Kemba's upkeep made a 2/2 Cat (it had Bonesplitter attached since T3). The Cat is summoning sick, so
by the host score — `EffectivePower() + mana-if-tapped + attack_payoff` — it reads **2** and beats a
freshly-cast **1/1 Kor Duelist at 1**. But Kor Duelist has `double_strike_while_equipped`, so with
Bonesplitter it is worth (1+2)x2 = **6**, and Greaves' haste is what lets it swing at all. The score
function models mana and attack-triggers but **not the double-strike multiplier**. `rider_delta`
*does* model it — but `rider_delta` ranks the (equipment, host) PAIR, while the single greedy pick
is made from `score`, so at d0 nothing corrects it. At d3/d5 the search enumerates both hosts and
finds the right one, which is exactly why the searched tiers show no slowdown.

**A first attempt at fixing this failed, and the reason is the useful part.** Adding the doubled
half — `if (ds_after) sc += p.EffectivePower();` behind `MTG_EQUIP_HOST_DS` — moved Kor Duelist from
1 to **2**, a *tie* with the Cat, which the tie-break still resolved to the Cat. Measured: d0
play-changed=1, slower=0, and `kitty_smoke_d0` stayed at 4.9910 — it did not recover gi406. The
credit has to include the **equipment bonus being doubled too**, which the host score cannot see
because it is computed once, equipment-agnostically, before `equips` is even ordered. Reverted
rather than shipped half-tuned; it belongs in a proper `heuristic-optimization` sweep with
`reachable_bonus`-style lookahead, not bolted onto a correctness fix.

### What the fix cost, measured

It **widens the search** — it adds legal plans that did not exist before — so unlike the
human-play-only Swords relaxation it moves GT wherever a deck makes generic tokens and plays
equipment. What actually happened, smoke tier:

| arm | configs changed | searched slower / faster | d0 slower |
|---|---|---|---|
| `MTG_EQUIP_TOKEN_HOST=0` | **0 of 83** (byte-identical) | 0 / 0 | 0 |
| `MTG_EQUIP_TOKEN_HOST=1` | 9 of 83 | **0 / 0** (12 play-changed at equal score) | 1 |

The off-arm coming back byte-identical is the part that matters methodologically: it proves the
null-tolerance refactor itself is inert, so every moved digest is attributable to the widening and
nothing else. The nine moved configs are exactly the token+equipment decks — `angels`, `kitty`,
`kitty2hg`, `fivecolour`. Batch makespan was 29 s in both arms, i.e. **no measurable search-cost
increase** — the extra host multiplies the Equip action count but those plans are cheap and the
budget absorbs them.

**Liveness was traced before the A/B was read**, per the trap that a widening emitting nothing looks
exactly like a no-op by digest. `MTG_EQUIP_ALL_HOSTS=1` on the user's seed 1 now names
`"equip_host": 1000, "equip_host_name": "4/4 Angel Token"`; the off-arm still offers only the two
Valkyries. A permanent counter (`EquipTokenHosts`, printed under `MTG_HYBRID_STATS`) guards against
the same silent-no-op regressing later — the lesson from the Marit Lage name match in
`DecisionProviders.cpp`, which was caught only by its counter.

### The same pattern elsewhere — audited 2026-09-18

`if (!LookupCached(p.card)) continue;` guarding a battlefield walk appears ~33 times across
`TurnSolver.cpp`, `DecisionProviders.cpp` and `AIEngine.cpp`. Almost all are **correct as written**:
they go on to read `d->params.X` for a specific ability (mana source, cycling sting, minus-counter
prevention, colour coverage), and a vanilla token genuinely has none of those — skipping it and
reading default params are the same answer. Two are worth naming:

* `DecisionProviders.cpp:8483` (`ShouldAttackWith`) already does the right thing by accident:
  `if (!d) { return true; }` — attack — which is the correct default for a vanilla body.
* **`DecisionProviders.cpp:12368` is genuinely token-blind.** The Fluctuator "going off" check
  tallies `swing += p.EffectivePower()` for creatures that can attack, *after* bailing on a null
  definition, so a token's power is never counted. It is **conservative** — understating `swing`
  makes `going_off` harder to satisfy, so it can only decline a line, never take a wrong one — and
  Fluctuator makes no generic tokens today, so it is latent rather than live. Left alone
  deliberately: fixing it would move Fluctuator's GT for no measurable gain. Fix it if Fluctuator
  ever gains a token maker.

The general rule for new code: a null definition means **vanilla body**, not **not a creature**.
Guard on the *ability you are about to read*, not on the lookup.

## Related

* `docs/design/analysis-Angels.md` — the deck ledger.
* The Swords self-target work (981fd9e0) is the *contrasting* case: human-play-only, chooser-guarded,
  provably inert to the search. This one cannot be done that way, because the search is wrong too.
