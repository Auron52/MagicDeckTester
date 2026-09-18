# Tokens can never be equipped — a rules bug in the equip host enumeration

**Status:** DIAGNOSED 2026-09-18, fix NOT yet made (it moves GT — see "What the fix costs").
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

## Why it matters on Angels specifically

Serra the Benevolent's −3 makes a **4/4 flying, vigilance** Angel — routinely the **largest body on
the board**, and therefore the host the deck's own stated ranking rule would pick first (*"equip to
the largest power and/or effect creature that doesn't already have haste and is summoning sick"*,
user, 2026-08-08). A freshly-made token is summoning sick, so Greaves granting it haste is exactly
the intended play pattern — and it is unreachable. Resplendent Angel's end-step trigger makes the
same token, so the deck hits this repeatedly.

In the user's seed 1 the **win turn was unchanged** (T4), so this is not a demonstrated
strength loss in that game — but the option was absent, which is what the Ajani-0
never-narrow-a-legal-choice doctrine forbids independently of outcome.

## The fix (sketch, not applied)

Make the lookup null-tolerant instead of fatal, since a token legitimately has no definition and
therefore has default params (not an equipment, no protection from everything):

```cpp
const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
if (!d && !p.is_token) { continue; }          // unknown NON-token: skip, as today
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

### What the fix costs

This **widens the search** — it adds legal plans that did not exist before — so unlike the
human-play-only Swords relaxation it **will move GT** on any deck that makes generic tokens and
plays equipment. Expect:

* a plan-count increase on token+equipment decks (Angels, KittyEquipment) and therefore a search
  cost change, possibly a meaningful one on the equipment deck;
* genuine play changes, hopefully improvements (the token is usually the best host);
* a rebaseline across all three tiers.

So it needs the full treatment: implement, **trace that the new option actually fires** (the
`MTG_EQUIP_ALL_HOSTS` probe above is the ready-made liveness check — it must start naming the
token), then A/B for quality and cost, then rebaseline. It should NOT be waved through as inert.

### Check the same pattern elsewhere before fixing

`if (!LookupCached(...)) continue;` is a common shape. Any site that walks the battlefield and bails
on a null definition is token-blind by the same mechanism. Audit those before landing this, so the
fix is one coherent change rather than a whack-a-mole: search
`LookupCached` call sites that guard with an early `continue`/`return` and ask whether a token
reaching that line should be skipped.

## Related

* `docs/design/analysis-Angels.md` — the deck ledger.
* The Swords self-target work (981fd9e0) is the *contrasting* case: human-play-only, chooser-guarded,
  provably inert to the search. This one cannot be done that way, because the search is wrong too.
