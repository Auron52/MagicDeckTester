# Analysis ledger — AccomplishedAlchemist

Deck: `decks/AccomplishedAlchemist/AccomplishedAlchemist.cod` (mono-green + Living Wish sideboard).
Started 2026-10-18 on branch `accomplished-alchemist` (worktree `.claude/worktrees/accomplished-alchemist`,
forked from `phase-1-2-deck-analyzer` @ 2176032d).

## RESUME POINT

Chunks A, B, C1, C2a, C2b, P, E1 landed (E1 = the Stage-5 / claude-play engine fixes: fd-diverge lockstep,
Arbor Elf's aura yield, site-9 re-open + chained loop payoffs, loop colour, wish singletons). The SUITE ENTRY
was attempted 2026-10-19 and STOPPED before adding rows (see "Suite entry attempt (2026-10-19)"): the
status-quo deck is over the 8,907 ms/game line, no `bottom_eval_*` arm met the adoption rule, and NOTHING was
adopted or added. Chunk P2 (lossless per-node cuts, 2026-10-19) made the per-node tail class 1.51x cheaper
per unit but the deck is still 1.55x over the line at d5 b20, and its `MTG_ENUM_CHARGE` lever (OFF) measured a
quality loss. Chunk P3 (2026-10-19) landed three more byte-identical cuts (the gi286 tail game 1.13x, the
cells ~2%) and found nothing lossless left that reaches the line; it surfaced the continuation-cache
eviction (g60 -17% with an unbounded cache) as a USER call. The mulligan-trial optimization (2026-10-20, branch `aa-opt-mull`) landed `MTG_BOTTOM_PREF_ORDER` (trial order + strict stop, byte-identical; d5 b20 units -4.8%, ms -6%) and assessed the winless certificate as inert for budgeted play -- still over the line. Next: a USER decision on the bottom_eval arm (or on running the mulligan stage under
`MTG_ALLOW_UNTESTED_DECK=1`), and/or the per-node enumeration cost found in the same attempt; then the suite
rows, then profile -> mulligan -> value leaf. Branch `aa-opt-branch` (2026-10-20, "Optimization: branching") then landed four
branching folds (greedy untap count / target, greedy + searched Crab Umbra host): play unchanged on 1,598
paired games, d5 b20 0.743x instructions / 0.897x core time -- still ~1.15x over the line. 2026-10-20: the USER's
Living Wish bucket rule landed DEFAULT ON (branch `aa-wish`; -12% units on the held-out d5 cell, gi286 6,157 s
-> 492 s); see "USER Living Wish buckets (2026-10-20)". The three optimization branches (mull, branch, wish) were
combined onto `accomplished-alchemist` 2026-10-20; the combined measurement is in "Combined optimization (2026-10-20)". 2026-10-20 (branch `aa-discard`): the USER's cleanup-discard
bucket rule landed DEFAULT ON (held-out -0.015 d3 / -0.022 d5); see "USER discard buckets (2026-10-20)".
2026-10-20: the USER's Elvish Harbinger rule landed DEFAULT ON (branch `aa-harbinger`); it measures
+0.005..+0.035 worse (-11..-17% units); two PROPOSED amendments (default OFF) make it better than the control on every
cell -- see "USER Elvish Harbinger rule (2026-10-20)".
2026-10-20 (branch `aa-perf`, off `aa-land` @ 88126463, NOT pushed): Chunk P4 landed eight byte-identical per-node
cuts -- d5 b20 0.931x instructions / ~0.94x core time, the costliest game 0.885x; see "Chunk P4 (2026-10-20)".
2026-10-20: COMBO OFF for this deck (branch `aa-combooff`) -- the rule table, the mechanical untap-loop route
and the search's query, all DEFAULT ON as the USER asked; -0.40..-0.48 turns and 0.81-0.87x ms on every cell; see
"Combo Off for AccomplishedAlchemist (2026-10-20)". 2026-10-21 (branch `aa-witchwin`, off `aa-land` @ c49997f1, NOT
pushed): the USER's decision that Seedcradle Witch counts as a win condition (Harbinger, wish and discard rules; the
4 life rule retired) landed DEFAULT ON -- -0.005 / +0.005 / -0.010 on the three cells, smoke d0 +0.024 from one
clause (the Harbinger fallback order); a PROPOSED amendment (`MTG_AA_HARB_WITCH_FALLBACK_FIRST`, OFF) makes it
-0.005 / -0.005 / -0.012 with zero slower games -- see "USER: Seedcradle Witch as a win condition (2026-10-21)".
2026-10-21 (branch `aa-discardmana`, at 846135c7, NOT pushed): the USER's discard MANA CAP (mana useful up to 6, then a
second real wincon / untapper, land Auras ahead of Arbor Elf) landed DEFAULT ON -- -0.005 / -0.005 / -0.002 with zero
slower games; see "USER: the discard mana cap (2026-10-21)".
Open USER items are listed under "Open questions surfaced to USER".

## The deck

Main (60): 3 Accomplished Alchemist, 4 Living Wish, 4 Crab Umbra, 4 Utopia Sprawl, 4 Stroke of Genius,
18 Forest, 4 Nourish, 4 Staff of Domination, 4 Elvish Harbinger, 1 Ezuri Renegade Leader,
1 Seedcradle Witch, 3 Arbor Elf, 1 Wild Growth, 1 Weather the Storm, 4 Feed the Clan.

Side (15, ALL reachable via Living Wish — creature or land): 1 Accomplished Alchemist, 2 Galvanic Alchemist,
1 Trufflesnout, 2 Scholar of Athreos, 1 Forest, 1 Island, 1 Eternal Witness, 1 Arbor Elf, 1 Brindle Boar.

### Intended engine (to be confirmed against Scryfall oracle text, never recall)

Accomplished Alchemist taps for X = life gained this turn (one colour). Lifegain (Nourish, Feed the Clan,
Weather the Storm, Staff of Domination's lifegain, sideboard lifegain creatures) pumps X. An untapper
(Crab Umbra on the Alchemist, Staff of Domination's untap-creature ability, Galvanic Alchemist) turns
X >= the untap cost into unbounded mana. Payoffs: Stroke of Genius aimed at the OPPONENT (deck them,
the opponent has a 53-card library when `opponent_library_dealt`), Ezuri's repeatable Elf pump,
Staff's draw. Elvish Harbinger tutors an Elf (Alchemist / Ezuri) to the top.

## Decisions carried into every subagent (do not re-litigate)

* Stroke of Genius's existing note ("target player = self") is WRONG now that the opponent has a library —
  `docs/design/passive-opponent-no-library.md` says so explicitly. Targeting the opponent is the deck's kill.
  Any forced-draw-on-opponent param must be added to `GoldFishRunner::DeckTouchesOpponentZones`.
* Drawing from an empty library loses at the next SBA check (CR 704.5b) — not at the next draw step.
* Everything faithful; a deferral is PROVISIONAL until the user signs it off.

## Cards (Stage 2)

| card | board | status | tier | notes |
|---|---|---|---|---|
| Nourish | main | implemented (chunk A) | 1 | cast_lifegain 6 (ApplyCastLifegain); no viewer decision; opponent's-turn cast PROVISIONAL inert |
| Eternal Witness | side | implemented (chunk A) | 1 | etb_return_gy_to_hand (Timeless Witness); viewer: dig (bucket A); PROVISIONAL 'may'->mandatory |
| Staff of Domination | main | implemented (chunk C2a) | 2+3 | self_untap_cost {1} (never its own action: paid ahead of each repeat of a {T} ability while tapped) + tap_lifegain_flat 1 (TapLifegain, K repeats via SpendSelfUntapRepeats) + untap_target_creature_cost {3} (ActivateUntap shape 3, SEARCHED target, K) + tap_draw_cost {5} (TapDraw, K repeats); {4},{T} tap target = PROVISIONAL inert |
| Crab Umbra | main | implemented (chunk C2a) | 3 | untap_enchanted_cost {2}{U} (ActivateUntap shape 1, K searched); host = searched enchant_target; umbra armor PROVISIONAL inert |
| Trufflesnout | side | implemented (chunk C1) | 3 | NEW etb_modal_self_counters 1 + etb_modal_lifegain 4; mode = SEARCHED cast axis (ETB CHOICE AXIS, chosen_x 1 counter / 2 life, etb_kx channel, both worlds); unpinned = EtbModalDefault; viewer: attack_mode generalised (trigger "etb") |
| Scholar of Athreos | side | implemented (chunk A) | 2 | drain params + NEW drain_each_opponent (x heads on loss AND gain; DrainLossPerActivation / DrainGainPerActivation at every drain site); lethal drain COUNT needs the chunk-C provider (ManaSinkActivationCounts) |
| Weather the Storm | main | implemented (chunk A) | 2 | cast_lifegain 3 + NEW storm bool (one GainLife per instance); ProjectPlanWalk now counts the plan's casts; cast order = USER decision (generic kept, storm-last only proposed) |
| Ezuri, Renegade Leader | main | implemented, fair pump (chunk A) | 1 | team_pump_* +3/+3 Elves (combat converter); main-phase lethal-K pump PENDING chunk C; regenerate/trample PROVISIONAL inert |
| Galvanic Alchemist | side | implemented (chunk C2a) | 3 | soulbond + soulbond_grants_untap_self_cost {2}{U} on BOTH halves (SoulbondGrantedUntapCost; shape 2); PARTNER SEARCHED on the ETB CHOICE AXIS (one variant per legal partner + decline, SoulbondPartnerPinScope, both worlds); viewer = existing attach_host soulbond chooser |
| Elvish Harbinger | main | implemented (chunk A) | 1 | Giant Harbinger tutor_to_top Elf + 5c dork; tutor axis AND the 'may' decline are searched variants (tutor_optional) -- no deferral needed |
| Seedcradle Witch | main | implemented (chunk C2a) | 3 | untap_pump_cost {2}{G}{W} + 3/3 (shape 4, SEARCHED target incl. untapped own creatures, K); {G}{W} off a single-colour Alchemist needs a second source (exact payer, pinned) |
| Utopia Sprawl | main | implemented (chunk C1) | 2/3 | NEW land_aura_host_subtype "Forest" (LandAuraHostOk at every host site) + etb_choose_color on a land aura (menu = land_aura_produces; locked colour credited per permanent via LandAuraProducesFor); colour = SEARCHED cast axis restricted by the provider to G/U/W/B; viewer: choose_color (full list) |
| Brindle Boar | side | implemented (chunk A) | 2 | self-sac outlet + NEW sac_outlet_lifegain 4; gate = SelfSacOutletUseful (death payoff OR read lifegain); self-only victim now baked correctly (CanonicalSacVictim self_only) |
| Stroke of Genius | main | implemented (chunk B) | 3 | NEW draw_target_player (+ targeting player): searched (target, X) cast variants via DrawXCandidates (opp = minimal lethal X only; self = cap/half/1..3 capped at library; human = all X); opponentdeck::ForcedDraw deck-out (decked at n >= library); shared PerformDrawX (executor ResolveDrawX + rollout); self = plain cantrip (site 3, IsSelfDrawXCast); DeckTouchesOpponentZones trait; fixtures aa_stroke_decks_opponent / aa_stroke_short_of_lethal. OPEN (user): the opponent prune drops non-lethal multi-turn deck-outs |

## USER references (2026-10-20/21) -- 10 hand-played games; all replay, the search matches or beats every win turn

The USER played `references/AccomplishedAlchemist/claude_s1..s9, s11` through the viewer (primary checkout, committed
there: 810c8f37, a0c7bdf3, a8d0b374, c7651b23, 546c4135 with `test/ref_bench.json`). Two games drove engine changes:
seed 2 (Ezuri on board, no way to pay its pump except the Alchemist "loop" option) -> "Pay as needed + combat
finishers"; seed 10 (the Witch loop misused the mana) -> `MTG_UNTAP_PAY_SMART` + FIN-WITCH. One drove a viewer fix
(9e9e9cb1): the X box silently clamped an over-max X (9 -> 8); it now refuses X above `max_x` and says why.

* `test/viewer_protocol_check.py --only AccomplishedAlchemist` (primary 9e9e9cb1, Release): 10 / 10 ok. After the
  pay-as-needed / combat-finisher / Witch-as-wincon changes: 7 ok + 3 repaired (decision points the references
  predate), 0 play-drift / board-diverged / enum-gap.
* `scripts/ref_bench.py --deck accomplishedalchemist`: 0 / 10 short; search mean win turn 4.80 vs the USER's 5.30.
  The search is faster on s1 (-2), s4, s7, s8 (-1 each) and equal on the other six.
* Max-X note (USER, s7 gi6): max_x 8 on T4 was right -- 13 mana (Sprawl Forest 2, Arbor Elf re-untaps it for 2 more,
  3 Forests, Harbinger 1, the Alchemist 5 after Feed the Clan); Feed's 2 leaves 11 = X 8 + {2}{U}. The USER's typed 9
  was silently clamped to 8 (recorded max 8 / chosen 8), which read as "9 accepted, max wrong" -- the viewer fix above.
  Scratch: `build/scratch/aa/refcheck/`.

## USER: Seedcradle Witch as a win condition (2026-10-21)

Branch `aa-witchwin` (worktree `.claude/worktrees/aa-witchwin`, forked from `aa-land` @ c49997f1), NOT pushed.
Commits fd7aeaf6 (the three rules + unit cases), d714f5f1 (the PROPOSED fallback lever below). Scratch:
`/workspaces/MagicDeckTester/build/scratch/aa-witchwin/` (`ab/` the pooled A/B, `x10/` the 10x reruns, `tr/` the
single-game traces + `diff.py` / `cls.py`, `fbf/` the amendment arm, `smoke_on/` this branch's smoke run).

**The USER, verbatim** (2026-10-21, after playing a seed where the Witch's untap pumped the Alchemist for lethal):
"Technically the Seedcradle Witch is still workable as a win-con here because it pumps the alchemist for lethal."
Asked whether the Witch should count as a win condition in the Harbinger, Living Wish and discard bucket rules:
"Sure, let's do that." "This also makes the 4 life rule unnecessary." "Though, in a real game this combo is much
worse than the actual win-cons." And: "Fortunately, it can fit in both buckets, so there is more opportunity to fit
it in."

**What changed** (all keyed on params through the shared `AaIs*` / wish census; each lever's `=0` is the aa-land
behaviour):

| rule | lever (heurarm slot) | 2026-10-21 reading, DEFAULT ON |
|---|---|---|
| Elvish Harbinger | `MTG_AA_HARB_WITCH_WIN` (AA_HARB_WITCH_WIN) -- the 2026-10-20 PROPOSED amendment, now the USER's decision | a held Witch fills Untap AND Wincon; the Witch is offered for a lacking Wincon too. Untap lacking -> the Witch FIRST (with Wincon lacking as well, one fetch fills both, ahead of Ezuri); only Wincon lacking -> Ezuri, then the Witch; the fallback (nothing lacking) -> Ezuri, Witch, Arbor Elf. `MTG_AA_HARB_ALCH_DEFAULT` stays OFF (still the USER's open call) |
| Living Wish | `MTG_AA_WISH_WITCH_WIN` (AA_WISH_WITCH_WIN), new | a held Witch fills Untap WHATEVER the held lifegain -- the "4 life rule" is retired (the 2026-10-20 reading counted the Witch as an untapper only while the held lifegain summed to >= 5, so Galvanic Alchemist could still be wished beside a Witch on a Boar's 4; at X = 4 the Witch's untap is mana-neutral but each one pumps +3/+3, which is the kill) -- and fills Wincon/Draw beside Stroke / Staff / Scholar. The Witch is a main-deck Elf, not in the sideboard, so only the census changes |
| cleanup discard | `MTG_AA_DISCARD_WITCH_WIN` (AA_DISCARD_WITCH_WIN), new | the Witch is a member of BOTH Untap and Wincon/Draw, like Staff: kept as either bucket's best it fills both (so a lone Witch leaves a Living Wish free for another gap); in play it fills both. Wincon order: Stroke > Staff > Scholar > Ezuri > **Witch** |

**The real-game caveat (the USER's, recorded in the code too):** the Witch's pump wins only in COMBAT, through
blockers and removal a goldfish never has, so it is much worse than the deck's real win-cons; it fills the bucket
as the weakest filler, which is why every Wincon ranking puts it last.

**Validation.** Unit (mtg-test) 1617 / 1617 -- 5 new cases (wish: 4 life rule gone, Wincon from hand / play, one
Witch fills both; Harbinger: first when it fills both, last among wincons, a held Witch fills both; discard: both
buckets, last in Wincon, Ezuri before the Witch among the kept) + 6 updated; the AA cases also green under
`MTG_UNPRUNE=tutor` and `MTG_UNPRUNED=1`. Scenarios 241 / 241. **Smoke** (`MTG_PGO=0`, 8 threads, NOT accepted):
124 / 132 PASS -- the four known moves at their known digests (selesnya d3 a1937cded0ab6f58, d5 9bd0e14ec876faa9,
critter d0 ccb5b47d5ef529c9, melira d0 0f633c1012b4fb11) and the four alchemist rows:

| row | GT (aa-land) | this branch |
|---|---|---|
| alchemist_smoke_d0_s1001 | 7.2220 / aaf70d40edc6eb07 | **7.2460** / 2b7f2c1a10f642de (19 games: 14 slower, 5 faster -- see below) |
| alchemist_smoke_d3_s1001 | 5.4333 / 353a32b5029685e0 | **5.4467** / fb0dadaad0b5f192 (gi75 5->6, gi99 5->7, gi110 5->6; gi101 7->6, gi113 6->5) |
| alchemist_smoke_d5_s1001 | 5.2667 / 89402437aa2efb07 | 5.2667 / bb76bcdf8d97884b (play only) |
| alchemist2hg_smoke_d3_s1001 | 5.5600 / dd447dd3be18f545 | 5.5600 / 027382fce7fbb153 (play only) |

With all three levers `=0` the four alchemist rows reproduce the aa-land GT byte-identically (same digests).

**A/B** (one pooled batch, 2,000 games, both arms via heurarm, 25-game chunks interleaved; binary d1e9af99; win
turn with an unwon game = 9; 8 threads on a box shared with the running mulligan generation and a sibling build,
so ms is noisy -- units are contention-free). The `old` arm reproduces aa-land's cells exactly (5.3000 / 5.2700 /
5.2133, the Combo Off port's numbers):

| cell | aa-land (old) | USER rule (new) | delta | faster / slower | units mean old -> new | units p90 | ms mean old -> new |
|---|---|---|---|---|---|---|---|
| s70001 d3 b10 (200) | 5.3000 | 5.2950 | **-0.005** | 2 / 1 | 85.3k -> 83.4k | 239k -> 223k | 7,006 -> 6,787 |
| s70001 d5 b20 (200) | 5.2700 | 5.2750 | +0.005 | 1 / 2 | 157.5k -> 154.0k | 462k -> 457k | 22,112 -> 21,319 |
| s90001 d5 b20 (600 held-out) | 5.2133 | 5.2033 | **-0.010** | 7 / 1 | 142.8k -> 140.5k | 357k -> 339k | 14,412 -> 13,749 |

Net over the 1,000 paired games: -6 turns (-0.006 / game); unwon unchanged (9 / 6 / 30 in both arms); total units
0.978x / 0.978x / 0.984x. The play digest moved in 5 / 5 / 20 of the 8 / 8 / 24 chunks (the rules reach the rollouts
everywhere), the win turn in 14 of 1,000 games.

**Where the faster games come from** (shipped-budget traces, `MTG_TUTOR_CHOSEN_RANK=1 MTG_TRACE=aaharbproof`):
9 of the 10 faster game-cells (s70001 g63 d3 + d5; s90001 g44, g279, g337, g443, g514, g585, g586) are the
Witch-for-a-lacking-Wincon clause -- Crab Umbra held, no wincon, the old rule offered Ezuri alone; the search takes
the Witch from **rank 2 of {Ezuri, Witch}** (in g337 / g443 / g514 / g585 the line already bends toward that fetch a
turn or two earlier). The tenth (s70001 g195 d3) is search re-valuation at T1.

**The slower games, both arms at 10x budget (d3 b100 / d5 b200):** 4 slower game-cells -> **3 recover, 1 persists**.
* s70001 **g192** (d3 and d5, 5 -> 6): the T3 Harbinger with NOTHING lacking (Staff fills Untap + Wincon) -- the
  fallback now lists Ezuri before the Witch, the search takes rank 1 in both arms, and the Witch line (old) wins T5
  where Ezuri wins T6. **Recovers** at 10x (both T5).
* s70001 **g195** d5 (6 -> 7): first divergence T1 (Utopia Sprawl cast vs held) -- search re-valuation through the
  rollouts. **Recovers** at 10x (both T5).
* s90001 **g136** d5 (5 -> 6): first divergence is the mulligan BOTTOM (old bottoms Crab Umbra + Wild Growth, new
  Accomplished Alchemist + Crab Umbra -- the clairvoyant bottom trials play each candidate out under the rules).
  **Persists** at 10x (new T6, old T5).

**Root cause of the d0 move, and a PROPOSED amendment (default OFF -- the USER's call).** All 19 changed smoke d0
games trace to ONE clause: the Harbinger FALLBACK (nothing lacking; a Staff of Domination is held in every one).
Ranking the Witch last there moved the greedy pick from the Witch to Ezuri: 14 slower (6 of them now unwon) vs 5
faster. It is also the clause behind the one rule-driven searched slower game (g192). The fallback is not a lacking
bucket being filled -- both Untap and Wincon are already held -- so "the weakest wincon" may not be the right
reading there. `MTG_AA_HARB_WITCH_FALLBACK_FIRST=1` (env-only) keeps the Witch first in the fallback (the 2026-10-20
order) and changes nothing else:

One pooled batch (2,325 games; binary 2 = the same tree + this env-only lever; the env is inert with the Witch levers
off -- two `old` chunks rode along and reproduced their digests e9b313dcd1f55289 / bbf4685550c81442 -- and with the
lever unset the smoke d0 row reproduces 7.2460 / 2b7f2c1a10f642de):

| cell | aa-land (old) | USER rule (new) | + FALLBACK_FIRST | f / s vs old | f / s vs new | units mean (vs old) |
|---|---|---|---|---|---|---|
| s70001 d3 b10 (200) | 5.3000 | 5.2950 | **5.2950** | 1 / 0 | 1 / 1 | 84.7k (0.99x) |
| s70001 d5 b20 (200) | 5.2700 | 5.2750 | **5.2650** | 1 / 0 | 2 / 0 | 155.4k (0.99x) |
| s90001 d5 b20 (600 held-out) | 5.2133 | 5.2033 | **5.2017** | 7 / 0 | 1 / 0 | 141.3k (0.99x) |
| smoke d0 s1001 (1000) | 7.2220 | 7.2460 | **7.2210** / 6ebd8a25b74427cd | 0 / 0 of 616 timed | 14 / 4 of 616 timed | -- |
| smoke d3 s1001 (150) | 5.4333 | 5.4467 | **5.4267** / 2be74cd5a9615962 | 2 / 1 | 2 / 0 | 92.3k |
| smoke d5 s1001 (75) | 5.2667 | 5.2667 | 5.2667 / 28b064a71c4d701e | 0 / 0 | 0 / 0 | 217.2k |
| smoke 2HG d3 s1001 (50) | 5.5600 | 5.5600 | 5.5600 / 027382fce7fbb153 | 0 / 0 | 0 / 0 | 187.3k |

(The d0 per-game pairing covers the 616 games slow enough to print a timing line; the aggregates cover all 1,000.)
With the amendment the rule is **better than aa-land on every searched cell with ZERO slower games in 1,000**
(-0.005 / -0.005 / -0.012), and the d0 canary returns to aa-land (-0.001). All three slower games of the USER rule
come back: g192 (T5, the fallback itself), g195 d5 (T6 = old) and the persistent s90001 g136 (T5 = old). The
9 Witch-from-rank-2 faster game-cells are all kept (they live in the "only Wincon lacking" clause, which the
amendment does not touch); only s70001 g195 d3 (the T1 re-valuation) goes back to its old T6. **Recommended: adopt `MTG_AA_HARB_WITCH_FALLBACK_FIRST`** (it narrows "Witch last among
wincons" to the case where a Wincon is actually being looked for) -- the USER's call; it ships OFF until then.
Wall ms in the amendment batch is not comparable to the A/B's (the box's load changed between the two runs).

**Readings taken (the USER's to correct):** (1) a HELD Witch fills both buckets in all three rules -- so the Harbinger
no longer fetches Ezuri for Wincon while a Witch is held, and the wish no longer offers Scholar for Wincon; (2)
"last among wincons" applies to every Harbinger order including the fallback (the amendment above narrows that);
(3) a Witch on the BATTLEFIELD fills Wincon in the discard rule, so a Stroke / Scholar / Ezuri in hand becomes a
spare and is shed before lands, exactly as with a Staff in play -- although the Witch is the weakest wincon
(ANSWERED by the USER the same day -- the mana cap below); (4) the
wish rule still does NOT count Ezuri as a held wincon (the 2026-10-20 reading) while it now counts the weaker
Witch; (5) Combo Off's Witch rows (X >= 5 for the unbounded loop) are untouched -- loop arithmetic, not a bucket
rule, and pump kills are not combo-off rows (the sibling `aa-ezuri` branch owns Combo Off).


### USER: the discard mana cap (2026-10-21) -- landed DEFAULT ON

Branch `aa-discardmana` (worktree `.claude/worktrees/aa-discardmana`, at phase-1-2-deck-analyzer 846135c7), NOT
pushed. Commit 791a3f21. Scratch: `/workspaces/MagicDeckTester/build/scratch/aa-discardmana/` (`ab/` the pooled A/B +
the `=0` smoke rows, `base/` the 846135c7 binary on the smoke rows, `smoke_on/`, `ev_on/` + `ev_off/` the label
evidence, `labels.py`).

**The USER, verbatim**, answering reading (3) above (a Stroke made spare by a Witch in play shed before lands): "I
wouldn't do that unless the lands and such are actually useful. You don't need more than 5-6 mana in any
situation." Then: "Let's cap it at 6. So, the "filled buckets", but using Witch would next fill lands up to 6 and
then it would fill in another win-con and/or untapper." On the mana order: "the land enchantments are better than
Arbor Elf in a lot of cases. The only cases the elf is better are where you already have an enchantment in play or
hand and can afford the tempo loss." And: "Wild Growth and Utopia Sprawl can effectively be played for free by
casting them from one land to another."

**The rule** (`AccomplishedAlchemistProvider::CleanupDiscardShedOrder`; `MTG_AA_DISCARD_MANA_CAP`, heurarm
AA_DISCARD_MANA_CAP, DEFAULT ON; `=0` = 846135c7 exactly). KEEP priority:
1. the filled buckets, best of each (the Witch for Untap + Wincon, Staff for both), + a Living Wish per gap -- unchanged;
2. MANA up to **6** total (the USER's number, a constant): the battlefield's projected mana (the wish census: a land 1,
   a land Aura's bonus, a dork 1, an Arbor Elf's enchanted-Forest bonus) + the hand's sources in keep order -- a
   land 1, Utopia Sprawl / Wild Growth +1 (with a land to enchant), Arbor Elf +2 on a Forest with a land Aura (board,
   or one kept before it) else +1, Elvish Harbinger 1; a source is useful while the running total is below 6, every
   source after that is SURPLUS;
3. a SECOND wincon and/or untapper -- the REAL ones (Stroke > Staff > Scholar > Ezuri; Crab Umbra > Staff > Galvanic;
   never the Witch): the best spare of each, one Staff may be both;
4. everything else, shed first: SURPLUS mana, then the other spare bucket cards (worst rank first), then spare Wishes.

Mana keep order: lands, then Utopia Sprawl > Wild Growth > Arbor Elf > Elvish Harbinger (an Aura is +1 mana at no
tempo cost with 2+ Forests -- the USER's "played for free" -- and +1 off the next land otherwise). EXCEPTION: a land
Aura on our battlefield, or the best one kept in hand, AND the tempo affordable -> Arbor Elf ahead of any ADDITIONAL
Aura. **"Can afford the tempo loss" is the coordinator's reading** (the Elf is cast next turn at the earliest, an
Aura makes its mana the turn it lands): board mana >= `kAaElfTempoMana` = **4**; below that the Auras stay ahead.
Prefix-stable (a source's value reads only the board and the sources before it in the keep order).

**Wish / Harbinger Mana buckets: no change needed.** Both offer mana only while projected mana is below 4, under the
cap; the wish cannot reach Utopia Sprawl / Wild Growth (not in the sideboard) and the Harbinger fetches Elves only, so
the Aura-vs-Elf order has nothing to act on there. Only their fallbacks (the wish's whole list when nothing is
admitted; the Harbinger's {Ezuri, Witch, Arbor Elf} when nothing is lacking) can add a source past 6, as a searched
last-ranked option -- not changed.

**Validation.** Unit 1622 / 1622 (5 new cases: a Witch in play + a spare Stroke under / at / over 6; valued sources;
the Aura-vs-Elf order in both branches incl. the tempo threshold; Arbor Elf +2 / +1; the second tier, one Staff for
both, the Witch never second-tier; 4 updated, each with its `=0` expectation); AA cases green under
`MTG_UNPRUNE=tutor`; scenarios 245 / 245. **Smoke** (`MTG_PGO=0`, 8 threads, NOT accepted): only the four known moves
at their digests + the alchemist rows. GT predates the aa-ezuri merge, so the alchemist rows are compared with the
846135c7 binary run on the same rows (and `=0` reproduces that binary byte-identically on all four):

| row | GT (aa-land) | 846135c7 | mana cap ON |
|---|---|---|---|
| alchemist_smoke_d0_s1001 | 7.2220 | 7.0760 / 87fe2d52050de8af | **7.0740** / 349c5d0b037bbe1b (7 faster / 5 slower of 722 timed) |
| alchemist_smoke_d3_s1001 | 5.4333 | 5.3933 / b9bae1a6cfc0a5b6 | 5.3933 / 6a8caa1f3e0e1077 (play only) |
| alchemist_smoke_d5_s1001 | 5.2667 | 5.2000 / 6301cee5a8a3b7e7 | 5.2000 / 7b6a437ec0938e7d (play only) |
| alchemist2hg_smoke_d3_s1001 | 5.5600 | 5.4600 / e9c1958b6ee17d84 | 5.4600 / 548890b3a7121b8d (play only) |

**A/B** (one pooled batch, 2,000 games + the `=0` smoke rows, heurarm arms in interleaved 25-game chunks; binary =
commit 791a3f21; 8 threads on an oversubscribed box, so ms is noise -- units are contention-free):

| cell | 846135c7 (old) | mana cap (new) | delta | faster / slower | units mean old -> new |
|---|---|---|---|---|---|
| s70001 d3 b10 (200) | 5.2600 | 5.2550 | **-0.005** | 1 / 0 (g117 5 -> 4) | 78.4k -> 77.5k |
| s70001 d5 b20 (200) | 5.2250 | 5.2200 | **-0.005** | 1 / 0 (g117 5 -> 4) | 146.5k -> 145.9k |
| s90001 d5 b20 (600 held-out) | 5.1383 | 5.1367 | **-0.002** | 1 / 0 (g452 6 -> 5) | 127.6k -> 127.6k |

**Zero slower games in 1,000** (so no 10x reruns were needed); unwon unchanged (9 / 6 / 27). The play digest moved in
3 / 2 / 10 of the 8 / 8 / 24 chunks: the rule bites rarely (a real cleanup shed is ~0.05 / game) but reaches the
rollouts' cleanups. Units 0.989x / 0.996x / 1.000x.

**Discard-only evidence** (analyze_deck.py's labelling stage by hand: 400 games d3 b10 seed 930000 per arm,
`MTG_DISCARD_NODE=0 MTG_DISCARD_TRACE=1`, the rule fanned with `MTG_AA_DISCARD_FAN` so the searched cleanup trials
every card and labels the rule's pick; one batch per arm, 6 threads, because the trace lines carry the seed and not
the job): the mana cap **16 decisions, 16 optimal, mean regret 0.000**; 846135c7's rule **26 / 26, 0.000**. On the 13
decisions both arms reached, the picks differ in 5 and tie on the label every time (e.g. seed 930069 T4: the cap
sheds a Forest, the old rule an Elvish Harbinger, both T5). At ~0.05 real sheds per game the labels cannot separate
the two rules; the paired A/B above (3 faster, 0 slower) is the evidence that does.

## Suite entry (2026-10-20) -- ADDED to all three tiers; suite_gate PASS at 1.58x

Tree `aa-land` (rebase onto phase-1-2-deck-analyzer @ 8e771a39 + Harbinger + discard + Chunk P4 + Combo Off),
binary a3c592d7, Release (`MTG_PGO` stamp stale), full regression tier on a quiet box (makespan 3m33s):

| case | games | avg | ms/game |
|---|---|---|---|
| alchemist_regression_d0_s2002 | 1000 | 7.0670 | 3 |
| alchemist_regression_d3_s2002 | 150 | 5.1400 | 3,423 |
| alchemist_regression_d3_s3003 | 150 | 5.4467 | 3,144 |
| alchemist_regression_d5_s2002 | 75 | 5.0000 | 4,354 |
| alchemist_regression_d5_s3003 | 75 | 5.1867 | **4,681** (worst searched case) |

`suite_gate.py --cost --no-run`: 4,680.5 / 2,969.1 (galadriel) = **1.58x <= 3x -> PASS** (was ~1.55x OVER the
8,907 line on 2026-10-19; Combo Off, the USER's wish/Harbinger rules, the folds and P2-P4 brought it in).
Rows (key `alchemist`; `test/regression_cases.sh`): smoke d0 1000 / d3 150 b10 / d5 75 b20 + `alchemist2hg` d3
50 b10 canary; regression d0 1000 / d3 2x150 b10 / d5 2x75 b20; overnight ENTRY set d0 4x2000 / d3 4x400 b10 /
d5 4x300 b20 (expand to the FULL set just before the keep table is adopted). GT accepted for the alchemist keys
only (smoke 4, regression 5, overnight 12); `check_gt_logs.py` consistent.

**Harness defect found:** `regression.sh --accept --deck=alchemist` after a FULL tier run merged EVERY key's
aggregate from `test/results/<mode>.env` into `regression_gt.txt` but promoted `.wins` for the filtered deck only
(17 foreign keys went STALE). The 17 lines were restored by hand; a filtered accept should filter the aggregates
too.

**NOT accepted -- 13 non-alchemist regression keys moved on this tree** (selesnya d3 x2 / d5 s3003, goblins d3
x2 / d5 s3003, critter d0 / d3 s3003, melira d0 / d3 s3003, knights d3 s3003, stompy d3 s3003, fungusv2 d0):
searched slower 0 / faster 6 / play-changed 7; d0 slower 2 (fungusv2 gi89 4->5, melira gi229 6->8) / faster 4.
Attribution against the upstream binary is in progress (see below / the next section).

No hand-played reference games exist for this deck yet (USER 2026-10-20: "we won't have any references"); the
reference-replay gates are empty, not passed.

## Chunk P4 (2026-10-20) -- landed: eight byte-identical per-node cuts; d5 b20 0.93x instructions

Branch `aa-perf` (off `aa-land` @ 88126463; local commits, NOT pushed). Scratch:
`/workspaces/MagicDeckTester/build/scratch/aa-perf/` (`mtg.*` = one binary per commit, `pstat/` = the 200-game
cells under `perf stat`, `one/` = single-thread single-game counts, `cg/` = callgrind, `prof/` = perf profiles,
`cgline.py` / `cgcalls.py` / `callers.py` = the callgrind readers).

* **Where the time is now.** The gi60 tail of the earlier chunks is gone on this tree (8 s single-thread, 168,877
  units). The d5 b20 cell's cost is its heavy games: the top 20 of 200 are 55% of the core time, and they are
  BOTTOM-TRIAL games (g47: 99% of its 242 G instructions is `AIEngine::BottomCards` -- 21 full rollout games).
  The profile is flat: callgrind on g47 / g60 / g138 puts `BuildSimKey` first (9.4% / 5.5% / 5.8% self, ~57% of
  it the `Fold` hash chain itself -- inherent), then the greedy walk (`SolveUncached` + its subset lambda ~4-7%),
  the payer (`TapForCostShared*` 16% inclusive on g47), `GameState` copies + memcpy ~4%. Nothing over ~6% that
  is not hashing, payment or copying. The cuts below are the redundancies the callgrind call counts exposed:
  the same pure answer computed several times on one unchanged board.
* **The commits** (each byte-identical by construction; the argument is in its code comment and commit message).
  Instructions = `perf stat -e instructions:u` over seed 70001 x 200 at d5 b20 (the deck profile, 12 threads,
  one pooled batch per binary); every row reproduces digest fa69d0e08a901ddf (and 46,922,146 units, save the
  pool-noise runs below):

  | commit | cut | d5 b20 instructions | ratio |
  |---|---|---|---|
  | 88126463 | base | 7.1501 T | -- |
  | b2cb228e | A: `CollectActions` prices `AvailableManaPool(state)` once per call (lazy memo; it asked ~3x on one const board) | 7.1008 T | 0.993 |
  | c1ea8b7e | B: `SolveWithLookahead` shares its candidates' pre-apply snapshots (`SnapshotActivatableAbilities`, `PrePlanAvailabilityKeys`) -- each candidate is a fresh copy of the same state; one-shot address-checked hand-off, not taken with damage events armed | 6.9378 T | 0.977 |
  | af9f4a4f | C: per-walk land-Aura memo (`LandAuraMemoScope`) in the four const pool walks -- `AvailableManaPool` derived an Arbor Elf's Forest classes 4x and every land's Aura bonus 2x per pool | 6.8623 T | 0.989 |
  | eb6af05d | D: `PayloadReserveMask` decides "no domain anchor" before pricing the board (it ran a `DomainColors` walk + a `PermanentManaYield` per source on every plan payment, then returned 0) | 6.7595 T | 0.985 |
  | 3cf03399 | F: `heurarm::t_arm` constinit (`constexpr Unset`) -- every `heurarm::Flag` read went through the TLS-init wrapper (22 M + 13.7 M calls/game); `MTG_EQUIP_ALL_HOSTS` read once | 6.6673 T | 0.986 |
  | 158263c7 | B2: the breakpoint-wave applies share the snapshots too (no prefix-snapshot hit) | (with G) | |
  | dbc76420 | G: `GenericTutorList` short-circuits its four pure filters (Elvish Harbinger's Elf search: 34 M `TutorNumericFilterOk` calls/game) | 6.6589 T | 0.999 (B2+G) |
  | 13d34eed | I: `ApplyActivatedUntap` asks the live-mana-creature test once per activation and reuses its ability buffer | 6.6419 T | 0.997 |

  The pooled cells carry ~+-0.3% of noise: at 12 threads the 3.2 GB plan-cache pool refuses stores (the
  `[determinism]` warning; 3-34 refusals per run), which re-spends work and once moved units (+869 on one run,
  +88 / -74 on two others; single-thread reruns of the moved game gave identical units). So the small cuts were
  ALSO measured single-thread, where the counts are exact (identical units and digests in every pair):

  | game (d5 b20, single thread) | base | HEAD (B2+G) | +I | total |
  |---|---|---|---|---|
  | g123 (seed 70124 gi123, the cell's costliest) | 330.78 G | 296.48 G | 292.81 G | 0.885x |
  | g47 (seed 70048 gi47, 2.3 M units, 21 bottom trials) | 234.58 G | 218.99 G | 218.22 G | 0.930x |
  | g60 (seed 90061 gi60) | 25.40 G | 23.75 G | 23.67 G | 0.932x |
  | g138 (seed 70139 gi138) | 17.16 G | 16.06 G | 16.03 G | 0.935x |

  Per-change single-thread: B2 g138 0.998 / g60 0.999 / g47 0.999; G g47 0.995 / g138 0.999 / g60 1.000;
  I g123 0.988 / g47 0.996 / g60 0.997 / g138 0.998.
* **Total, paired** (base and the B2+G tree as two concurrent 12-thread batches, so contention is shared):
  d5 b20 instructions 7.1321 T -> 6.6708 T (0.935x), task-clock 1,330.7 -> 1,253.2 core-s (0.942x; 6,654 ->
  6,266 core-ms/game at load ~20-30); d3 b10 4.4723 T -> 4.1644 T (0.931x), task-clock 0.958x (that pair shared
  the box with a build). With I (HEAD 13d34eed): **d5 b20 6.6419 T = 0.931x of the paired base, d3 b10 4.1514 T
  = 0.928x**. Core time moves less than instructions: what was removed is cheap, cache-resident work; what is
  left is hashing, payment and copying.
* **Byte-identity.** The d5 b20 cell on every commit, and on the stacked trees (after D, after B2+G, after I)
  also the d3 b10 cell, mtg-test, scenarios and smoke: cells identical (d5 b20 fa69d0e08a901ddf, d3 b10
  7931b57b4049f19d, units unchanged); mtg-test 1611 / 1611; scenarios 241 / 241; smoke 124 / 128 with exactly the four known pre-existing moves at their known digests (selesnya d3
  a1937cded0ab6f58, d5 9bd0e14ec876faa9, critter d0 ccb5b47d5ef529c9, melira d0 0f633c1012b4fb11). Not accepted.
* **Measured and dropped:** an explicit `(first, second)` comparator for `BuildSimKey`'s canon sort (callgrind:
  `BuildSimKey` inclusive 1,487 M -> 1,488 M, no change -- the compiler already emitted the same compare); skipping
  `ComputePlanTraits`' attacker pick in a post-combat plan without an own pump (0.000x on g47 / g60 / g138 -- the
  case does not occur on this deck's hot path).
* **Lossless but NOT done (deferred, with the reason):**
  - A library-order digest cached on `Library` (5.6% of `BuildSimKey`, ~0.6% of a heavy game): every non-const
    accessor would have to invalidate it, a held `Card&` across a `BuildSimKey` call would go stale silently,
    and a cache written from a `const` method races if a `GameState` is ever read by two threads.
  - Incremental cost sums in the greedy walk's odometer (`SolveUncached`, ~4-7% of a game): sound, but a
    refactor of the most delicate loop in the engine for a few percent.
  - The payer's per-source land-Aura answers (`UntapLandHostsOfUncached` is still ~0.6-1%): the payer mutates
    the board (a pay-sac Treasure leaves the battlefield mid-payment), so an index-keyed memo is not safe there.
* **Where this leaves the line.** On today's footing the base tree read 7,084 core-ms/game (load ~36) and 6,654
  (paired, load ~20-30) at d5 b20; HEAD reads ~0.94x of that in core time (6,266 paired before I). That is under
  the 8,907 line on both of today's footings, but the earlier contended footings ran up to 1.3x higher, so only
  `suite_gate.py --measure-all` on a quiet box decides it. The heavy games are bottom trials (99% of g47's
  instructions), so the big remaining lever is still the USER's: the bottom_eval arm / the mulligan stage.

## Combined optimization (2026-10-20) -- mull + branching + wish on `accomplished-alchemist`; at the line within contention noise

The three branches were cherry-picked onto 39848afe: `MTG_BOTTOM_PREF_ORDER` (4a6fb7bd), the branching folds
(39803388, 8048a434) and the USER's Living Wish buckets (a3dd986a..8ab567c4). Conflicts in `src/ai/HeuristicArm.h` and
`test/unit/test_accomplished_alchemist.cpp` were resolved as a union. The heurarm enum and its name table line up, with
the four fold slots and then the three wish slots in the same order in both. The `static_assert` on the count holds.
The test-file union had dropped the closing brace of the late-bound Crab Umbra test case, so the build failed. That
is fixed in 0d16858b. Scratch: `/workspaces/MagicDeckTester/build/scratch/aa/comb/`. It holds `mtg.base` (39848afe,
the `mopt` snapshot), `mtg.new` (0d16858b), `base/` and `new/` (the pooled batches), `pstat/` (paired `perf stat`
cells), `g23*/` (the one moved game) and `an.py`.

* **Build/tests.** Full rebuild (objects deleted first). mtg-test 1517 / 1517 and scenarios 228 / 228. Smoke is
  124 / 128, and only the four known pre-existing moves failed, each at its known digest: selesnya d3
  a1937cded0ab6f58, d5 9bd0e14ec876faa9, critter d0 ccb5b47d5ef529c9, melira d0 0f633c1012b4fb11. Smoke was not
  accepted.
* **Measurement.** Base and new ran as two concurrent 16-thread batches over ONE manifest each: seed 70001 x 200
  and held-out seed 90001 x 600, both cells, 1,600 games per arm. An unwon game counts as win turn 9. The base arm
  reproduces the committed off-arm digests on all four cells (ed03ab6957a09593, f4d9cf88b0c3a26c, 1971ba6aee67bfd0,
  c3735ac8352a1d27).

  | cell | avg base -> new | faster / slower | units mean base -> new | units p90 | units max | slowest game base -> new |
  |---|---|---|---|---|---|---|
  | s70001 d3 b10 | 5.8150 -> 5.6950 (-0.120) | 25 / 12 | 166.9k -> 149.7k (0.897x) | 458k -> 444k | 1,973k -> 1,887k | 242 s -> 143 s |
  | s70001 d5 b20 | 5.7450 -> 5.6300 (-0.115) | 25 / 7 | 313.5k -> 271.7k (0.867x) | 947k -> 798k | 4,090k -> 3,979k | 773 s -> 63 s |
  | s90001 d3 b10 | 5.6900 -> 5.6883 (-0.002) | 48 / 53 | 160.4k -> 132.8k (0.828x) | 422k -> 350k | 5,341k -> 5,330k | gi286 6,137 s T8 -> 446 s unwon |
  | s90001 d5 b20 | 5.6400 -> 5.6167 (-0.023) | 48 / 48 | 294.5k -> 243.6k (0.827x) | 786k -> 693k | 8,293k -> 8,266k | gi286 7,890 s unwon -> 287 s T7 |

  Net <= 0 on every cell. The box was shared throughout: sibling agents' builds and batches pushed the load average to
  40-97 on 32 logical cores, and new finished about 1.5 h before base's gi286 tail. **The batch wall ms therefore
  cannot be compared across arms.** For example, new's s70001 d3 mean was 23.9 s against base's 17.6 s, on fewer
  units. Units are contention-free. Wall ms is given only for the tail games, where the gap is >10x.
* **Composition: each branch's expected effect holds.**
  - **Wish rule.** New reproduces the aa-wish on-arm's win turns on 1,599 of 1,600 games. The s70001 d3 digest
    (6d9f924c48f68526) matches the wish A/B's exactly. Three cells match its averages and faster/slower counts (s70001
    d3 5.6950, 25/12; s90001 d5 5.6167, 48/48; s90001 d3 5.6883, 48/53). s70001 d5 is 5.6300 and 25/7, against the
    wish A/B's 5.6350 and 24/7; the difference is gi23, below. The gi286 collapse carries over.
  - **Folds.** No win turn moved: the held-out digests differ from aa-wish's at identical win turns, which is the
    branching section's PLAY-CHANGED same-turn class. Units vs the aa-wish on-arm are 0.924-0.939x per cell, from the
    folds' greedy tie-breaks plus the trial order.
  - **`MTG_BOTTOM_PREF_ORDER` moved ONE game: s70001 d5 gi23, T8 -> T7.** This is the only win-turn difference from
    the wish-on arm. In a single-game rerun it reproduces (T7, 1,712,723 units). Each fold off, and all four off,
    leaves it unchanged. `MTG_BOTTOM_PREF_ORDER=0` restores T8 (1,773,133 units).
  - **The gi23 mechanism (`MTG_BOTTOM_TRIAL_TRACE=1`).** Trial id ...218 scores T6 when it runs THIRD (old order) but
    T8 when it runs FIRST (preference order). So the pass minimum G=6 is held by {218, 219} in the old order and by
    {219} alone in the new one. The bottomed card differs, and so does the game.
  - **What this means for the proof.** This is the cross-trial channel the mulligan-trials section named, a shared
    history across trials: the bottoming TT plus the caches a finite budget re-spends. That section reported it did
    not show on base. It shows here, under the wish rule. So `MTG_BOTTOM_PREF_ORDER` is "same bottoms GIVEN the trial
    values", not byte-identical in general: trial values depend on trial order. The move is 1 of 1,600 games and
    faster. It is surfaced for the USER, not changed.
* **Cost: paired `perf stat` over s70001 d5 b20 x 200.** Base and new ran as two concurrent 12-thread batches.
  Instructions do not depend on contention; task-clock (core time) does, so read it as the paired ratio:

  | | instructions | core-s | core-ms / game |
  |---|---|---|---|
  | base 39848afe | 11.164 T | 2,665 | 13,327 |
  | new 0d16858b | 7.454 T (0.668x) | 2,005 (0.752x) | 10,026 |

  s70001 d3 b10, new alone: 4.209 T instructions (base 5.951 T on the branching run, 0.707x) and 5,740 core-ms per
  game. The held-out d5 cell costs 0.90x of s70001's units per game (243.6k vs 271.7k).
* **The suite_gate figure.** The gate is the mean core-ms per game at the worst searched cell (d5 b20) against
  8,907 ms. The same base binary has measured 10,227 (mopt, 24-way wall), 11,416 (branching task-clock), 12,643 (P3,
  30-way) and 13,327 (today, load 40-97). Applying today's paired 0.752x to those gives:
  - new at 7,694 / 8,586 / 9,510 / 10,026 ms;
  - 0.86x / 0.96x / 1.07x / 1.13x of the line;
  - on the held-out seeds, ~0.90x of each figure (~6.9-9.0 s).

  **The combined deck is at the line, inside the measurement's contention noise.** It is under the line on the two
  quieter footings and over it on the two contended ones. Only `suite_gate.py --measure-all` can decide it: one pooled
  tier run on a quiet box, on the same footing as galadriel's 2,969 ms. That needs the suite rows, which are not added
  yet (the USER's suite-entry decision is still open).
* **For the USER (not changed):**
  1. The `MTG_BOTTOM_PREF_ORDER` trial-order dependence above (s70001 d5 gi23).
  2. The deck is now within noise of the 3x line. Whether to attempt the suite entry (rows + a quiet-box
     `--measure-all`) is the USER's call.

## Combo Off for AccomplishedAlchemist (2026-10-20)

Branch `aa-combooff` (worktree `.claude/worktrees/aa-combooff`, forked from `accomplished-alchemist` @ 8ab567c4 +
the 0d16858b test fix). Scratch: `/workspaces/MagicDeckTester/build/scratch/aa-co/` (`fx/` fixture drafts,
`sweep*/` the sweeps, `ab/` the pooled A/B + `an.py`).

**The USER's instruction** (2026-10-20): *"We should use the combo off logic for this deck. It is very much the type
of deck that needs it."* And the shape: *"it is another combo that can have many steps just like EDF. After
playing the lifegain you untap and tap Alchemist a whole lot to generate mana, then either cast a finishing spell
like Stroke of Genius targeting the opponent or wish for the win-con there."* Everything below is written in those
three stages -- LIFE, LOOP, FINISH -- and under the EDF doctrine: display AGGRESSIVE (the rule table), execution
EXACT (the route).

### The rule table (`AccomplishedAlchemistProvider::ComboOffPossible`) -- for the USER to argue with

A rule is the conjunction `LIFE + LOOP + FINISH`; the name is the three parts, e.g. `LIFE-HAND+LOOP-UMBRA+FIN-STROKE`.
Inventory only (battlefield, hand, the sideboard a Living Wish reaches); no search; no floating pool.

* **ALCHEMIST** (every row): an Accomplished Alchemist we control that can tap this turn (not summoning-sick;
  tapped is fine -- the loop's first untap re-enables it).
* **LIFE-GAINED** -- life already gained this turn reaches the untapper's threshold with no further cast.
* **LIFE-HAND** -- plus a subset of the hand's lifegain spells (Nourish 6, Feed the Clan 5 / 10 ferocious, Weather
  the Storm 3 x storm count -- the deploy's casts count only when the deploy is paid FIRST), paid exactly
  (`ManaPool::CanPay`) from the board's other untapped mana + the turn's land drop, cast before the Alchemist taps.
* **LIFE-STAFF** -- no lifegain spell: a Staff of Domination's {2},{T}: gain 1 life bootstraps X.
* **LOOP-UMBRA / -UMBRA-HAND** -- Crab Umbra on the Alchemist / in hand ({U}); {2}{U} per untap (X >= 4).
* **LOOP-GALVANIC / -GALVANIC-HAND / -GALVANIC-WISH** -- the Alchemist soulbonded to a Galvanic Alchemist / one in
  hand ({2}{U}) / Living Wish -> one from the sideboard ({1}{G} + {2}{U}); {2}{U} per untap.
* **LOOP-STAFF / -STAFF-HAND** -- Staff of Domination in play / in hand ({3}); {3},{T} + {1} per untap (X >= 5).
* **LOOP-WITCH / -WITCH-HAND** -- Seedcradle Witch in play / in hand ({G/W}); {2}{G}{W} per untap (X >= 5), and a
  non-Alchemist {G}-or-{W} unit left for the first untap's second colour.
* LOOP exactness: after the LIFE casts and the deploy, the loop's own bootstrap search `UntapLoopBootOp` (the
  function the resolver re-runs before every op) must reach "Alchemist untapped, X > per-iteration cost" from
  (other mana, X, Staff tapped, Alchemist tapped). Two deploy orders: from the other mana (before the gain), or
  out of the Alchemist's first tap (after the gain; one colour per tap, so a deploy whose pips are one colour).
* **FIN-STROKE** -- Stroke of Genius in hand at the opponent: {X}{2}{U}, X = the lethal X.
* **FIN-SCHOLAR / -SCHOLAR-HAND** -- Scholar of Athreos in play / in hand (+{2}{W}): {2}{B} per opponent life.
* **FIN-WISH-SCHOLAR** -- a Living Wish in hand (not the one LOOP-GALVANIC-WISH spends) + Scholar in the sideboard.
* **FIN-STAFF-DIG** -- a Staff (in play or the loop's own) + a Stroke in the library above the last card:
  {5}+{1} per draw to it, then the Stroke (the loop's draw-to-find; the search is clairvoyant about order).
* BANK: (kUntapLoopCeiling / 2 - 16) x the net per iteration + the other mana must cover the finisher; with a Staff
  able to GAIN the bank does not bind (each gain raises X for every later iteration).
* MAIN-PHASE KILLS ONLY: Ezuri's pump and the Witch's +3/+3 win in combat, which the route's verdict
  (OpponentHasLost after its last link) cannot prove before combat. They stay the search's (sweep class c').

### The route (execution EXACT) -- `AlchemistComboRouteOffer` / `AlchemistComboRouteApply`

* ONE `ComboRoute` action ("Combo Off", the EDF / KittyEquipment kind -- every existing reader of a verified
  go-off works unchanged; `ComboRouteApplyFor` is now the one dispatch of the kind). Emitted from `CollectActions`
  when the provider's `UntapLoopRouteEnabled()` (`MTG_AA_COMBO_ROUTE`, heurarm `AA_COMBO_ROUTE`, DEFAULT ON) and
  the RULE fires (the microsecond pre-filter) and -- in autonomous play -- a walk on a copy WINS this turn. In human
  play a rule-only fire is still emitted (display aggressive): the menu labels it "not yet proven", and a click
  that does not win rolls back and emits `combo_off_failed` (the existing AIEngine branch).
* THE WALK: a bounded DFS (6 links, 24 applies) over the engine's OWN plans under `ExecutorMenuScope`, in the
  USER's stage order: FINISH when the float already pays the cheapest main-phase kill (the kill-fold list, now
  `TurnSolver::UntapLoopKillPlans`, shared with `BpDeriveContinuationList`); else LOOP plans (the closed-form
  `ActivateUntap kUntapLoopToNeed`) ranked no-finisher-first / most lifegain / fewest actions; then SETUP plans
  (lifegain, untapper, Living Wish casts, the Staff's gain), two orders interleaved (untapper-first for Weather's
  storm, life-first for a {U} untapper paid off the Alchemist), each optionally after tapping the Alchemist for
  the untapper's colour (the payer otherwise taps it white). Children fold on a relabelling-invariant board key.
  Inside the walk the loop funds main-phase kills only (`g_untap_loop_no_pump`). It ORDERS the engine's plans and
  tries them until one proves a kill; it never invents an action, and nothing is removed from the menu.
* EXACT: the path is recorded as the PLANS (not indices -- the enumerator's breadth reads its context), memoised
  per game on the full mid-turn state + the whole library order; the apply replays it on a COPY in its own
  context first and re-walks there if it does not kill, so nothing half-plays. `MTG_AA_ROUTE_TRACE`,
  `MTG_AA_ROUTE_STATS`, `MTG_AA_CO_WHY` (rule refusals).
* Engine fixes the sweep forced (both worlds): `MTG_UNTAP_LOOP_SURPLUS_GENERIC` (heurarm, default ON) -- inside the
  closed-form loop the untap's generic eats the colour most in surplus over what the loop owes, and a tap whose
  owed pips are covered floats the payer's first generic colour; `UntapLoopFloatPays` prices the payoff outside
  that scope (the real payer). Before it, a Staff loop funding Scholar ran to the 256-step ceiling with the kill
  unfunded (0 of 33 offers). The enumeration memo stands down inside the walk.

### The search's query (`edfco`)

`MTG_EDF_CO_ROOT` / `MTG_EDF_CO_LOOK` now default to the provider's `ComboOffSearchDefault()` when unset (EDF
false -> byte-identical; AA true) and the shortcut's gate accepts a provider with a route
(`UntapLoopRouteEnabled`). The precondition the rule's comment demands is met by construction: the AA route
action exists autonomously only after its own walk killed. Both arms still verify (root always; lookahead by
`MTG_EDF_CO_LOOK_TRIAL`, default ON), and the trial replays the proven plans.

### Viewer

Nothing deck-specific: the plan JSON's `combo_off` / `combo_off_verified` / `combo_off_rule` drive the existing
⚡ Combo Off button; the history lists each link's summary and every untap / drain event, never a macro.
DECISIONS.md row added (`main_phase` reuse). No AA references exist yet, so no reference replays move.

### Sweep (`bash test/aa_combo_off_sweep.sh`; classes as the EDF catalogue)

1,154 states: a synthetic matrix of 1,008 over the table's ingredients (LIFE none / Nourish / Feed / Weather /
already-gained x LOOP Umbra / Staff / Witch on board or in hand, Galvanic in hand, none x FINISH Stroke / Living Wish
/ Scholar in hand / none x 2-6 lands x Island x a sick Alchemist) and 146 MAIN_1 frames of 30 autonomous games
(seed 91001, d3 b10). Every state asked through `EnumerateMainPlans` under `MTG_HUMAN_PLAY` and the flagged plan
re-applied as a click; the win oracle is the autonomous engine on the SAME board (`resume_at: main1`, d5 b100,
then b1000), and its game log says whether the win was a combo (Stroke / drain / the route) or combat.

| population | a offered+wins | b offered, no win (combo exists) | c missed (combo exists) | c' missed (combat only) | d offered, no main-phase win | e |
|---|---:|---:|---:|---:|---:|---:|
| synthetic (1,008) | 328 | 0 | 2 | 31 | 26 | 621 |
| engine (146) | 15 | 1 | 0 | 12 | 1 | 117 |

* `combo_off_verified` is again a perfect oracle: 343 verified, 343 won. Offered 371, won 343 (92.5%).
* Per stage (offered / wins): LOOP-UMBRA 51/51, -UMBRA-HAND 61/60, -STAFF 53/53, -STAFF-HAND 60/59,
  -GALVANIC-HAND 48/48, -WITCH 43/35, -WITCH-HAND 55/37; FIN-STROKE 129/124, -WISH-SCHOLAR 120/109,
  -SCHOLAR-HAND 118/107, -STAFF-DIG 4/3.
* **d = 27: all 26 synthetic ones are Seedcradle Witch boards**: the closed-form loop with a
  TWO-colour untap ({2}{G}{W}) off a ONE-colour Alchemist starves -- at X = 5 (Feed the Clan, net 1) the float runs
  out of {G} with the Alchemist tapped (`S-d625d49799`: W5 B16 G0, loop stopped), and with the Witch cast from hand
  the payer pays its {G/W} with a Forest before the floating white (`S-4bf6b611b3`), leaving the first untap's {G}
  unpayable. The autonomous engine wins these only in combat (the Witch's own +3/+3). The arithmetic says the
  loop exists, so the rule keeps offering it (display aggressive; narrowing is the USER's call); the fix belongs to
  the loop's colour plan / the payer. The engine d (X-c618e2a6d9) is a Staff dig to a Stroke ~45 cards deep
  in the sweep's canonical (alphabetical) library -- a fixture-fidelity artifact plus the loop's 256-step ceiling.
* **b = 1** (X-36693ec269): Weather the Storm + Staff + Crab Umbra in hand; the engine wins by an order the walk's
  width does not reach (Umbra, Staff gain, then Weather at storm 3). **c = 2** (weather/staff_bf/wish): the engine
  casts Living Wish FIRST (storm + a fetched gain) -- LIFE through a wish is not a rule ingredient.
* What the sweep fixed on the way (counts are the first sweep -> the last, binary 119b9342): b 61 -> 1, c 52 -> 2, d 62 -> 27. The
  causes: the walk's order (standalone loops first; then stage order), the payer tapping the Alchemist white for a
  {U} untapper (the pre-tap), the loop's generic eating the colour it was banking (MTG_UNTAP_LOOP_SURPLUS_GENERIC),
  Weather's storm count, the land drop, the Witch's second colour, and an untap-step confound in the oracle.
* Fixtures: `test/combo_off/aa_co_{1..12}` (7 offered + verified wins incl. the Wish -> Scholar finish, 5 absent),
  all green with the 34 EDF ones (`bash test/combo_off_check.sh`: 46/46).

### A/B (one pooled batch, 4,800 games, three arms via heurarm; binary 119b9342)

Arms: **base** = route, both queries and `MTG_UNTAP_LOOP_SURPLUS_GENERIC` OFF (the committed tree's play);
**fix** = only the loop's colour fix ON; **on** = everything ON (the shipped defaults). Win turn with an unwon game
= 9; cost in ms (the box was shared, so ms is noisy) and in work UNITS (contention-free; note the route's walk is
NOT metered in units -- its own time is `walk_ms` below and is inside the ms).

| cell | base avg | on avg | delta | faster / slower | fix alone vs base |
|---|---|---|---|---|---|
| s70001 d3 b10 (200) | 5.6950 | 5.2550 | **-0.440** | 72 / 1 | +0.000 (1 / 1) |
| s70001 d5 b20 (200) | 5.6300 | 5.2300 | **-0.400** | 66 / 1 | +0.005 (0 / 1) |
| s90001 d3 b10 (600 held-out) | 5.6883 | 5.2100 | **-0.478** | 226 / 8 | +0.005 (0 / 3) |
| s90001 d5 b20 (600 held-out) | 5.6167 | 5.1717 | **-0.445** | 211 / 5 | -0.003 (3 / 3) |

Unwon games: 14 -> 9, 12 -> 7, 41 -> 33, 40 -> 27.

| cost per game | base ms mean / p90 / max | on ms mean / p90 / max | ms ratio | units mean base -> on |
|---|---|---|---|---|
| s70001 d3 b10 | 4,339 / 11,117 / 41,712 | 3,738 / 11,801 / 65,456 | 0.86x | 149.7k -> 104.4k (0.70x) |
| s70001 d5 b20 | 15,153 / 47,760 / 155,380 | 13,207 / 33,201 / 234,167 | 0.87x | 271.7k -> 200.6k (0.74x) |
| s90001 d3 b10 | 6,201 / 15,271 / 190,670 | 5,042 / 11,899 / 179,131 | 0.81x | 132.8k -> 92.3k (0.69x) |
| s90001 d5 b20 | 9,812 / 24,950 / 282,614 | 8,449 / 19,623 / 319,397 | 0.86x | 243.5k -> 171.9k (0.71x) |

Route census over the 1,600 on-arm games (`MTG_AA_ROUTE_STATS`): 45.5 M emission-site visits, the rule refused
44.8 M (98.4%), 442 k memo hits, 648 k walks (307 k won), 20.6 M applies, walk time 2,984 s = **1.9 s per game**
(inside the ms above), 846 applies re-walked in their own context. Against suite_gate's line (8,907 ms/game, the
worst searched case's mean) the deck is still over on the shared box (d5 b20 13.2 s at s70001 / 8.4 s held-out),
but every cell is cheaper ON than OFF -- the go-off ends games sooner and skips the search's chain.

**Before the last two fixes** (binary ee4b5c2f, the first full run): -0.405 / -0.350 / -0.467 / -0.433 with 3 / 3
/ 9 / 6 slower; the duplicate-child fix (119b9342) took the route no-ops of s70001 gi118 / gi142 and s90001 gi134
back to the control's turn (gi118 T7 -> T4).

**Every slower game, both arms re-run at 10x budget (d3 b100 / d5 b200):** on vs base 15 slower games -> **8
recover, 7 persist** (4 distinct games); fix vs base 8 -> 6 recover, 2 persist (1 game).
* s70001 gi1 (both depths, 6 -> 8): RECOVERS (on T5 at 10x, base T6).
* s90001 gi293 / gi401 / gi446 / gi584 (d3), gi198 / gi401 (d5): RECOVER.
* **s90001 gi340** (both depths, T3 -> T4; also the fix arm alone): the control wins T3 with the Witch's pump loop
  (Feed the Clan, X = 5, net 1 -- the colour-starved shape above); with `MTG_UNTAP_LOOP_SURPLUS_GENERIC` the turn-3
  search picks a Living Wish line instead. Reconstructed at turn 3 (b100) both settings win T3, so this is the
  loop-payment change moving a knife-edge allocation on the real board, not a lost line.
* **s90001 gi521** (both depths, 5 -> 6): a route NO-OP -- the committed turn-5 line casts Living Wish, which at
  REAL resolution fetched Galvanic Alchemist with the Alchemist already tapped by the line's payments, and the
  route the search had proven on its own board could not be walked on the executor's (an fd-divergence that
  surfaces through the route; the line plays on and wins T6).
* **s90001 gi560** (both depths) and **gi198** (d3), 5 -> 6: play diverges at turn 2-3 (the lookahead now prices the
  route's future kill turns), and the control's T5 is again a Witch pump loop the on arm's line never sets up.
  Search re-valuation, not a route failure.

**Validation** (binary 119b9342): mtg-test 1517 / 1517; scenarios 228 / 228; `combo_off_check.sh` 46 / 46 (34 EDF
+ 12 AA). Smoke (not accepted): 124 / 128, exactly the four known moves at their known digests (selesnya d3
a1937cded0ab6f58, d5 9bd0e14ec876faa9, critter d0 ccb5b47d5ef529c9, melira d0 0f633c1012b4fb11) -- EDF and every other
deck byte-identical. FD oracle (`MTG_FD_ORACLE=1`, on arm, s70001 d5 b40 x 200): **0 [fd-diverge]**, avg 5.200 (E1's
committed cell: 5.685). The search's query (`MTG_EDF_CO_STATS=1`, same run): root 3 route candidates, 3 verified;
lookahead 3,684 asked, 3,684 verified, **0 refuted (false-positive rate 0.00%)**.

**Port onto `aa-land` (2026-10-20).** The eight Combo Off commits cherry-picked onto `aa-land` @ ae8cb607 (the
rebase onto 78 upstream commits + the USER Harbinger and discard rules + Chunk P4); conflicts were unions only
(HeuristicArm.h: the AA Harbinger / discard slots, then `AA_COMBO_ROUTE`, `UNTAP_LOOP_SURPLUS_GENERIC` -- enum and
kNames in the same order; this ledger). Validation: mtg-test 1612 / 1612, scenarios 241 / 241, combo-off fixtures
46 / 46; smoke (MTG_PGO=0, not accepted) = the four known moves at their known digests, EDF byte-identical, plus the
four NEW alchemist rows of the coordinator's uncommitted suite draft (d0 7.2220/aaf70d40edc6eb07, d3
5.4333/353a32b5029685e0, d5 5.2667/89402437aa2efb07, 2HG d3 5.5600/dd447dd3be18f545 -- no GT). The effect holds on
the integrated tree (one pooled batch, 2,000 games, ON vs everything OFF):

| cell | base avg | on avg | delta | faster / slower | units mean base -> on | ms mean base -> on |
|---|---|---|---|---|---|---|
| s70001 d3 b10 (200) | 5.7400 | 5.3000 | **-0.440** | 71 / 3 | 140.0k -> 85.3k (0.61x) | 4,143 -> 3,088 |
| s70001 d5 b20 (200) | 5.6400 | 5.2700 | **-0.370** | 62 / 3 | 234.6k -> 157.4k (0.67x) | 6,276 -> 5,500 |
| s90001 d5 b20 (600) | 5.6217 | 5.2133 | **-0.408** | 193 / 6 | 223.0k -> 142.8k (0.64x) | 6,739 -> 5,267 |

The 12 slower games at 10x: 5 recover, 7 persist, all in the classes already named above -- s90001 gi340 (the loop's
colour fix), s70001 gi36 and s90001 gi134 (a route NO-OP on the executor's board), s70001 gi45, s90001 gi337 / gi373
(the route wins a turn later than the control's line: search re-valuation). Scratch:
`/workspaces/MagicDeckTester/build/scratch/aa-co/land/`.

**Verdict vs the line.** The USER asked for this ON and it measures far better on every cell (-0.40 to -0.48
turns, 211-226 faster vs 5-8 slower on the held-out 600s) and CHEAPER on every cell (0.81-0.87x ms, 0.69-0.74x
units). It does not by itself bring the deck under suite_gate's 8,907 ms/game line on this (shared) box: the
held-out d5 b20 mean is 8.4 s ON vs 9.8 s OFF, s70001 d5 b20 13.2 s vs 15.2 s -- the line needs a dedicated
uncontended `suite_gate.py --cost` measurement before any suite-entry claim.

### Pay as needed + combat finishers (2026-10-20/21, branch `aa-ezuri` off `aa-land`)

Two USER viewer games, both "this should win and the menu would not let me":
* **Seed 2 T7** (reference `claude_s2_gi1`): Ezuri cast, Nourish for 6, the Alchemist soulbonded to Galvanic
  Alchemist, opponent at 17. *"No ability to activate Ezuri for the win"* -- the line existed only as "run the
  untap loop, THEN pump off the float" -- *"you should just be able to pay it as needed"*; and *"Combo Off also never
  showed up there"* (the table had no combat finisher).
* **Seed 10 T6** (no reference saved yet): 4 Forests, Elvish Harbinger, Accomplished Alchemist, Seedcradle Witch,
  Brindle Boar cast and sacrificed for 4; opponent at 16. *"This should be possible to win, but it misuses the
  mana."* X = 4 against the Witch's {2}{G}{W} is mana-NEUTRAL, yet five untaps pump the Alchemist to 17. Verified on
  the reconstructed board: the old engine plays "Witch: untap Elvish Harbinger" and does not win; the USER's own
  clicks stall because every Alchemist tap floated white (C2a's fixed colour) and the WUBRG payer spent the green.

What changed (three levers, all heurarm slots, all DEFAULT ON):
* `MTG_AA_LOOP_FUNDED_PUMP` -- ActivatePump **mode 4**: a stacking team pump (Ezuri's +3/+3) offered when only an
  Accomplished Alchemist untap loop can fund it (FindFundingUntapLoop); zero plan mana; the apply forces the C2b
  loop's need to exactly that cost (FundTeamPumpByLoop) and pays from the float, both worlds. Autonomous: the
  lethal count; human: every count 1..lethal ("Ezuri: +3/+3 x2 (the untap loop pays)").
* `MTG_UNTAP_PAY_SMART` -- an activated untapper OUTSIDE the closed-form loop (a human's single "Witch: untap
  Alchemist", a searched K) runs under an empty owed set: the Alchemist's pre-tap floats a pip THIS untap lacks,
  generic comes out of the colour most in surplus. And the closed-form loop with a pump payoff whose banking goal is
  unreachable (net zero) now runs exactly k_pump activations instead of stopping before its first op; it is offered
  when those k activations really pay (proved on a copy).
* `MTG_AA_COMBO_COMBAT` -- the rule table's combat finishers **FIN-EZURI** (Ezuri on the battlefield: K x
  {2}{G}{G}{G}, K = TeamPumpLethalCount), **FIN-EZURI-HAND** (+{1}{G}{G}), **FIN-WITCH** (the Witch loop's own
  +3/+3 on the attacking Alchemist, at net zero when K taps of X + the other mana pay K x {2}{G}{W}); LIFE now also
  counts a lifegain creature (Brindle Boar's self-sac, Trufflesnout's ETB life). The route funds pump kills and
  accepts a COMBAT-LETHAL board (CombatLethalNow; a goldfish never blocks) as its verdict; the button's verify, the
  click's rollback test and the fixture harness use AlchemistComboOffWon (== OpponentHasLost for every other deck).
  The walk admits Ezuri / Brindle Boar casts and the Boar's sac.
* Viewer: mode 4 has its own label (the old mode-2 "Elfs +6/+0 x2" text is kept byte-for-byte: saved references
  match plans by summary). New scenario key `paired_with` (a soulbond pair).

Validation (binary 3ace089d; built pinned to cores 24-31 beside the running mulligan generation):
* mtg-test 1612 / 1612; scenarios 245 / 245; combo-off fixtures 48 / 48 (34 EDF + 14 AA). New fixtures, each also run
  with its lever OFF as the control: `aa_co_13` (seed-2 T7, Combo Off offered + verified,
  LIFE-HAND+LOOP-GALVANIC+FIN-EZURI-HAND, a combat-lethal board), `aa_ezuri_loop_pump_seed2_t7` (autonomous T7),
  `aa_human_ezuri_loop_funded_pump` (the menu offers Ezuri x1 / x2 with the loop paying; the x2 click wins T7),
  `aa_co_14` (seed-10 T6, LIFE-HAND+LOOP-WITCH+FIN-WITCH), `aa_witch_netzero_pump_seed10_t6` (autonomous T6; no win
  with MTG_AA_COMBO_COMBAT=0), `aa_human_witch_pay_smart_seed10_t6` (the USER's own five Witch clicks now pay and
  win T6; no win with MTG_UNTAP_PAY_SMART=0).
* AA references (all 10, `viewer_protocol_check --strict --only AccomplishedAlchemist`): 7 ok, 3 repaired, 0
  play-drift; claude_s2_gi1 replays to its T7 win.
* Smoke (MTG_PGO=0, not accepted): the four known moves at their known digests, every other deck byte-identical,
  and the four alchemist rows move -- d0 7.2220 -> 7.0520 (6915209f0ff5787b; 95 games changed, 90 faster / 5
  slower), d3 5.4333 -> 5.3867 (037cffa10ea37b72; 6 changed, all faster), d5 5.2667 -> 5.2000
  (338edf58a4ca95f8; 6 changed, 5 faster, gi35 7 -> 8), 2HG d3 5.5600 -> 5.4800 (776d4b5f7bbf7013; 4 changed, all
  faster).
* A/B (one pooled batch, the three levers OFF vs ON, s70001 x 200, 8 threads on cores 24-31 beside the generation):

  | cell | off avg | on avg | delta | faster / slower | units mean off -> on | ms mean off -> on |
  |---|---|---|---|---|---|---|
  | s70001 d3 b10 | 5.2850 | 5.2600 | **-0.025** | 6 / 1 (gi179 6 -> 7) | 80.4k -> 79.5k | 12,653 -> 11,697 |
  | s70001 d5 b20 | 5.2450 | 5.2250 | **-0.020** | 6 / 2 (gi111, gi179 6 -> 7) | 150.7k -> 149.7k | 25,985 -> 24,395 |

  Net <= 0 on both cells and no dearer (ms is heavily contended: the generation owned the box). Three slower games
  (two distinct) -- a handful, so not 10x-classified. Scratch: `/workspaces/MagicDeckTester/build/scratch/aa-ez/`.

## Optimization: mulligan trials + certificate (2026-10-20) -- landed: trial order + strict stop (sound, ON)

Branch `aa-opt-mull` (from accomplished-alchemist @ 39848afe). Scratch: `/workspaces/MagicDeckTester/build/scratch/aa/mopt/`
(`mtg.base` = 39848afe, `trace_*` = the per-trial census, `base` / `new1` = the cost cells, `tr.py` the census reader).
The `bottom_eval_*` fields are untouched (that arm is the USER's call, "Suite entry attempt").

* **Where the bottom-trial units are (new diagnostic `MTG_BOTTOM_TRIAL_TRACE=1`, reads only: one `[btrial]` line per
  trial with its stop, result and per-turn work units).** Seed 70001, 200 games: 71 games mulligan, 542 trials; trials
  are **80% of all units** (d5 b20 50.3 M of 62.7 M; d3 b10 27.1 M of 33.4 M). By trial turn (d5): T1 18%, T2 34%,
  T3 23%, T4 12%, T5 7%, T6+ 5% -- the cost is the EARLY turns every trial must play identically. Bounds computed from
  the census: perfect turn-synchronous interleaving (every trial stops at the pass minimum G) 48.8 M (-3%); every trial
  but the chosen one stopped after G - 1 ("ideal strict") 46.4 M (-8%); the last turn of every stopped trial (the
  most a turn-B winless certificate could remove) 1.5 M (3%). So no exact trial-side change can approach the line;
  the remaining lever on trial cost is what the trial searches with (bottom_eval, the USER's), not how many turns it plays.
* **The change -- `MTG_BOTTOM_PREF_ORDER` (default ON; sound dominance, the COLLAPSE rule).** The pick is
  HeuristicBottomPick over the candidates at the pass minimum G. `AIEngine::BottomPreferenceOrder` peels
  HeuristicBottomPick into one order and VERIFIES over all 2^h allowed sets (127 calls at seven cards) that it is a
  priority choice -- the pick is always the allowed set's first card in that order; where that fails (or h > 10) the
  old order and P3's stop run unchanged. Then (per-candidate loop) the trials run in that order, and (subset table)
  the masks run in the order of the key the step greedy would give each mask (the preference rank of each step's
  pick from it, per hand state, memoised) -- the greedy's final set is the key-minimum mask at G. Every trial after a
  W is less preferred than the holder of W, so it can matter only by winning STRICTLY before W: it stops after turn
  W - 1 (P3: after W), and a stopped trial reads W + 1. The final allowed set loses only candidates less preferred
  than an allowed one and keeps the most-preferred one at G, so the bottomed cards are identical. Off with the
  two-stage refine, an external bottom chooser, the blind bottomer and forced replays (where values are read for
  more than that). Name-deduped twins now always play the lowest-index copy's trial (as before), even when another
  copy comes first in preference order. The only cross-trial channel is the shared bottoming TT's history -- the
  same channel as P3's stop (a key collision across different kept hands), and it did not show.
* **Tests.** `test/unit/test_bottom_pref_order.cpp`: (1) on 40 real AA opening hands the order verifies and
  HeuristicBottomPick equals the order's first allowed card on all 127 sets of each; (2) end to end,
  RolloutKeepWinTurn at 1 and 2 mulligans on 6 hands bottoms the SAME physical cards and returns the same win turn
  with the lever forced on and off (`AIEngine::TestForceBottomPrefOrder`). mtg-test 1501/1501.
* **Play identity.** The deck's 200-game cells: d3 b10 `ed03ab6957a09593`, d5 b20 `f4d9cf88b0c3a26c` -- identical to
  base, avg 5.8150 / 5.7450, 0 faster / 0 slower; the units moved in 61 of the 71 mulligan games (the lever fired)
  and nowhere else. Scenarios 228/228; smoke 124/128 with exactly the four known moves at their
  known digests (selesnya d3 a1937cded0ab6f58, d5 9bd0e14ec876faa9, critter d0 ccb5b47d5ef529c9, melira d0
  0f633c1012b4fb11); nothing else moved. Not accepted.
* **Cost, seed 70001, 200 games** (base 39848afe and new as two concurrent 12-thread batches; ms per game):

  | cell | base mean / p90 / max | new mean / p90 / max | core-s | units total |
  |---|---|---|---|---|
  | d3 b10 | 8,834 / 20,369 / 94,694 | 8,164 / 21,019 / 90,409 | 1,767 -> 1,633 (0.92x) | 33.37 M -> 30.17 M (-9.6%) |
  | d5 b20 | 10,227 / 28,008 / 120,914 | 9,608 / 25,000 / 116,179 | 2,045 -> 1,922 (0.94x) | 62.71 M -> 59.71 M (-4.8%) |

  Trial units: d5 50.3 M -> 47.3 M (-6%, against the -8% "ideal strict" bound), d3 27.1 M -> 23.9 M (-12%).
  **Still over the 8,907 ms line at d5 b20 (9,608 here at 24-way contention; P3 measured 12,643 at 30-way).**
* **The certificate (`ProvenWinlessThisTurn` for AccomplishedAlchemistProvider) -- assessed, NOT built.** By its own
  contract (DecisionProvider.h, TurnSolver's `WinlessCertificateActive` / `WinlessDevelopActive`) the certificate is
  consulted only inside `LabelSearchScopeActive()` / `UnbudgetedWorkScopeActive()` -- the offline label ladder and
  unbounded cells -- because pruning consumes no work units and would change budgeted play. Every game in the suite
  and both 200-game cells is budgeted, so a certificate here is byte-inert for this deck's cost, play AND bottom trials.
  The one budget-safe use is a trial stop (a trial proven winless on its stop turn ends at that turn's start): the
  census caps that at the last turn of the stopped trials, 3% of trial units before this change and less after it
  (the strict stop already removes most of those turns). Against that, a sound bound for this deck must price
  unbounded Alchemist loops (four untappers), mid-turn lifegain, Stroke's deck-out and Living Wish's 15-card
  sideboard -- a large one-sided proof whose failure mode is a silently lost win. Not worth it for the line; it stays
  NotAssessed. (It would pay on an unbudgeted label run of this deck, if one is ever needed.)
* **Residual.** At d5 b20 the deck sits ~1.08x over the line on this sample; the trials are 79% of units and their
  cost is turns 1-3 of each trial at full play settings. What is left on the bottoming side is a USER decision: the
  `bottom_eval_*` arm (Suite entry attempt) or running the mulligan stage (keep table) under
  `MTG_ALLOW_UNTESTED_DECK=1`.

## Optimization: branching (2026-10-20) -- landed: four greedy/searched branching folds; still over the line

Branch `aa-opt-branch` (forked from `accomplished-alchemist` @ 39848afe). Commits: e0440a96 (the census
instruments + MTG_GREEDY_UNTAP_K_DOM, MTG_GREEDY_UNTAP_TARGET_TIE, MTG_GREEDY_AURA_HOST_LATE,
MTG_AURA_HOST_LATE_BIND), 84426f1a (the {T}-ability count extension), and this section. Scratch:
`/workspaces/MagicDeckTester/build/scratch/aa/opt/` (`cen/` repro censuses, `shard/` the 200-game d5 census
in 25 single-thread processes, `pstat/` instruction counts, `cost/` pooled A/Bs). Base = 39848afe
(`opt/mtg.base`).

* **The census.** `MTG_BRANCH_SHAPE` / `MTG_BRANCH_HEAVY` only ever captured the SEARCH walk
  (EnumeratePlans). Added: the same heavy capture for the GREEDY walk (SolveUncached, the rollout leaf),
  and both walks' subset VISITS aggregated by group shape (card x kind x size). Seed 70001, d5 b20, 200
  games, single-thread shards: **greedy walk 772.4 M visits, search walk 108.3 M** (the greedy walk is
  ~35% of the cell's CPU, EnumeratePlans ~15%; perf, 24 threads). Shapes covering 92% of the listed greedy
  visits held an activated-untapper family (Staff of Domination: target x K x the {T} modes, up to 24
  options; Crab Umbra untap x K); 63% of the listed search visits held a Crab Umbra cast group (up to 15
  hosts: the battlefield creatures plus every creature in hand, three identical Alchemists among them).
* **Fold 1 -- MTG_GREEDY_UNTAP_K_DOM (default ON, heurarm).** Greedy walk only. Every K of one
  (source, target, shape) carries ONE activation's bill (`cost`), and consider() reads nothing off an
  ActivateUntap but the bill, the source and `eval` (= K; 5000 for the loop) -- the payers, colour gates,
  attack-tap projection and the one-use-per-source rule never see the count. So the higher-eval twin wins
  every comparison a lower K could win: the lower K are never chosen. Extended to a LONE source's
  repeatable {T} ability (Staff's draw / lifegain counts; damage-scaling modes never match the key);
  refused when another source shares an interchangeable-source tag (the source fold reads tags across
  sources).
* **Fold 2 -- MTG_GREEDY_UNTAP_TARGET_TIE (default ON, heurarm).** Greedy walk only. No consider() term
  reads an untap TARGET either, so two variants differing only in the target tie EXACTLY and the walk's
  (wins, rank, smallest mask) order gives the tie to the lower candidate index: only the first-emitted
  target can be chosen. (The greedy cannot tell untapping a 6-life Alchemist from untapping an Elf --
  surfaced below.)
* **Fold 3 -- MTG_GREEDY_AURA_HOST_LATE (default ON, heurarm).** Greedy walk only. A host-inert creature
  Aura (`CreatureAuraHostInert`: no P/T, keyword, enter-tap, restriction, swap, bestow face -- Crab Umbra)
  ties on every host, and materialize_best's RetargetSolveAuraHosts re-picks the host over EVERY host the
  candidates offer anyway; the walk keeps the first host option only.
* **Fold 4 -- MTG_AURA_HOST_LATE_BIND (default ON, heurarm).** The SEARCH walk (EnumeratePlans,
  autonomous; human play, the un-pruned oracle and the AURA_HOST_BRANCH control walk every host). The
  name-keyed plan signature puts every host variant of a plan in one dedup class and the dedup keeps the
  best AuraPlanHostKey member (USER 2026-10-06), so the walk emitted ~H plans to keep one. Now the Aura's
  group keeps one representative and its plan binds, when built, the best-keyed LEGAL host (battlefield
  creature, or a hand creature the subset casts; the uncast-host rule reads "some legal host exists").
  Same class maximum; frames whose host product could exceed 4096 stay unfolded. The key itself has an
  exact fast path: for a host-inert Aura the board half (lethal / dmg / now) is the same for every host
  unless a Kor-style aura_self_buff or Bruna's attack_gather_auras is on the post-plan board, so the
  comparison is the dork tie-break -- first legal non-mana-creature host.
* **Soundness evidence.** (1) Unit tests (test_accomplished_alchemist.cpp): the greedy plan is
  byte-identical with each greedy fold on/off over 10 boards; the searched plan LIST is identical with the
  late binding on/off over 4 boards (one and two Umbras, hand hosts). (2) `MTG_GREEDY_FOLD_VERIFY=1`
  re-runs every greedy walk unfolded: seed 70007 gi6 20,737 walks / 645 differ, seed 70124 gi123 251,281 /
  7,517 differ -- **0 of the differences with <= 31 candidates**. Every difference is the walk's tie-break
  mask ALIASING (`int mask |= 1 << j` past 31 candidates is undefined and in practice wraps): there the
  walk's tie among equal-scored subsets was already settled by visit order, and the folds settle it
  differently -- a different member of a set the greedy scores as equal, never a lower-scored plan.
  (3) `MTG_AURA_LATE_BIND_VERIFY=1` (fast key vs the full odometer): seed 90061 gi60, 434,001 bindings,
  0 disagreements. (4) mtg-test 1502/1502 (3 new); scenarios 228/228; smoke 124/128 with exactly the
  four known moves at their known digests (selesnya d3 a1937cded0ab6f58, d5 9bd0e14ec876faa9, critter d0
  ccb5b47d5ef529c9, melira d0 0f633c1012b4fb11) -- every other case byte-identical.
* **Work removed** (d5 b20 seed 70001, 200 games, single-thread shards): greedy visits 772.4 M -> 236.8 M
  (0.31x; 317.3 M before the {T}-ability count extension), search visits 108.3 M -> 71.9 M (0.66x). Instructions, single thread: seed 90061 gi60
  466.4 G -> 285.6 G (0.61x), seed 70124 gi123 808.0 G -> 354.1 G (0.44x), seed 90287 gi286 d3 b10 9,321.8 G -> 5,660.4 G (0.61x; same 1,794,357
  units and win turn).
* **Play: no win turn moved.** Base and the final binary as two concurrent 15-thread batches over ONE
  manifest each (seed 70001 x 200 and held-out seed 90001 x 600, both cells; 1,600 games per arm, gi286
  included): every game has the SAME win turn (0 faster, 0 slower in every cell; avg d3 b10 5.8150 /
  5.6900 held-out, d5 b20 5.7450 / 5.6400 held-out, both arms). The seed-70001 cells keep their job digests
  (ed03ab6957a09593, f4d9cf88b0c3a26c), 0 of 400 per-game digests moved. Held-out: 3 of 1,200 games
  PLAY-CHANGED at the same win turn, attributed by per-job flags on the final binary -- d3 gi132 (T5 win
  either way) moves only with MTG_GREEDY_UNTAP_K_DOM (an aliased-mask tie inside a rollout), d3 gi311
  and d5 gi311 (T6 either way) only with MTG_AURA_HOST_LATE_BIND: at T5 the search commits "Alchemist +
  Staff + Umbra" instead of "Harbinger + Alchemist + Umbra" (the reordered equal-valued candidates
  re-spend a finite budget; 42,638 -> 43,121 units). Work units
  move slightly (d5 62.71 M -> 62.32 M on seed 70001): the aliased tie-breaks pick different equal-scored
  greedy plans inside rollouts, which moves leaf values, never a committed decision on these games.
  (Base-vs-base units also differ by ~0.01% between pooled runs here -- the plan-cache pool binding.)
  No slower game, so nothing to recover at 10x.
* **Cost** (seed 70001, 200 games per cell, base and final as two concurrent batches, `perf stat` over the
  whole batch -- instructions are contention-free, task-clock is core time under the box's load):

  | cell | instructions | core-s (task-clock) | core-ms / game |
  |---|---|---|---|
  | d3 b10 | 5.951 T -> 4.480 T (0.753x) | 1,342 -> 1,154 (0.860x) | 6,712 -> 5,771 |
  | d5 b20 | 11.164 T -> 8.290 T (0.743x) | 2,283 -> 2,047 (0.897x) | 11,416 -> 10,237 |

  The 1,600-game play batches above, `perf stat` per arm (both cells, held-out included, gi286 ran 3,797 /
  5,009 s base vs 2,834 / 3,831 s final at d3 / d5): 93.88 T -> 65.17 T instructions (0.694x), 24,264 ->
  19,842 core-s (0.818x).

  Core time falls less than instructions: what was removed is the cheap inner work of the walks (a
  visit is an increment, a few predicate reads and a score sum), and what is left is the expensive
  per-node part (plan application, payment, the host key, allocation). **Still over the 8,907 ms line at
  d5 b20: 10.2 s per game here, 1.15x over** (P3's base measured 12.6 s under heavier contention, i.e.
  ~11.3 s / 1.27x on that footing).
* **What is left in the walks (census after).** Greedy: the largest shapes are now plain cast subsets
  ("+4 single", ...) and a Staff group of 3-4 options (one untap target at its top K, the loop where
  offered, the draw and the lifegain at their top count); two identical Staffs keep their {T} counts
  (shared tags). Search: Utopia
  Sprawl's colour x host-class options (real choices: the locked colour and a tapped vs untapped host),
  Stroke's (target, X), Galvanic's partner, the untap K x target product, Living Wish's six targets.
* **Candidates examined and NOT folded:**
  - Living Wish variants inside continuations (seed 90287 gi286: the tutor axis returned 300.7 M of the
    396 M plans scored, continuation lists 8,185 entries on average, 66,559 at the worst T8 state). Each
    variant fetches a different card: not an identity. The [bp-cands] probe's "11.1% duplicate entries"
    was a BpCandFingerprint blind spot (ability_mode: Staff's draw and lifegain at the same count hashed
    alike; 40 of 40 sampled pairs); fixed in the fingerprint (diagnostic keys only, no play effect). The
    narrowing is the USER's bucket rule, being built separately.
  - Utopia Sprawl / Wild Growth host classes: identical hosts already prefix-fold (chunk P); the name-keyed
    dedup folds the remaining hosts too, but a land Aura's host changes the subset's PAYMENT (a tapped vs
    untapped Forest), so a late binding would still have to re-check feasibility per host -- not done.
  - Untap K x target in the SEARCH walk: real outcomes (mana, pump, the Alchemist's state). "More
    net-positive iterations never hurt" is a dominance only with a per-shape proof (colour of the
    Alchemist's mana, the untapper's end state); LOSSY until proven -- not prototyped.
  - Accomplished Alchemist's colour is a payment-time choice, not an enumerated axis; Stroke (target,
    X), Galvanic's partner and Trufflesnout's mode are distinct outcomes.
* **Findings for the USER (not changed):**
  - **The greedy's tie-break mask overflows.** SolveUncached's `int mask |= 1 << j` is undefined for
    j >= 31; on the verify repros every walk the folds changed (645 of 20,737; 7,517 of 251,281) had more
    than 31 candidates, i.e. its tie between equal-scored subsets was being settled by an aliased bit
    pattern (in practice the first one visited). The
    same mask keys the mana-verdict cache (`gmask`): on a deck with mana-inert independent actions AND
    more than 31 candidates two different group selections could share a cached verdict. AccomplishedAlchemist
    has no independents (cache never armed); other decks were not checked. A fix (64-bit or a sorted-index
    compare) moves play on every such deck -- USER's call.
  - **The greedy cannot see an untap target or count.** Its score for an untapper is `eval = K` and the
    bill of ONE activation, so it picks the highest K and, among targets, the first emitted -- never the
    target that pays (the 6-life Alchemist). The folds preserve that policy exactly; a value term for the
    untap would be a playout-heuristic change (A/B-gated), not a fold.
  - **The tail games' biggest single waste is an aborted pass, not branching.** gi60 T1 spent 450 k of
    the game's 495 k units, gi123 T1 455 k of 1.55 M, gi286 T6 226 k: each the deepest ID pass, admitted by
    the start gate and aborted at the 25x overrun guard, its work discarded (rescuable 0). That is the
    start-gate estimate on this deck (MTG_OVERRUN_MULT / the estimate) -- a budget question for the USER.
## USER discard buckets (2026-10-20) -- landed DEFAULT ON (the USER's rule); one amendment PROPOSED, OFF

Branch `aa-discard` (worktree `.claude/worktrees/aa-discard`, forked from `accomplished-alchemist` @ 0d16858b).
Commits a6cf4f88 (the rule + 6 unit cases), 5f7e330f (the PROPOSED `MTG_AA_DISCARD_STAFF_FIRST`, + 1 case).
Scratch: `/workspaces/MagicDeckTester/build/scratch/aa-discard/` (`ab/` the pooled A/B, `x10/` the 10x reruns,
`amend/` the amendment arm, `ev_off/` + the worktree's `logs/discard_analysis/` the evidence traces, `tr/` the
single-game traces). **Supersedes the agent proposal** at `build/scratch/aa/discard_buckets_proposal.md`
(2026-10-19; never implemented).

**The rule, verbatim (USER 2026-10-20):** "the discard heuristic for this deck is simple. Buckets are the same as
all of my other rules. You keep the best of each bucket and lands etc. for the rest. Living Wish can fill any gaps
and Staff can fill both." Before it the deck shed by the INHERITED SelesnyaLifegain buckets, whose PAYOFFS bucket
is empty for this list, so they shed Nourish / Feed the Clan as "lifegain with no payoff".

**The implemented reading** (`AccomplishedAlchemistProvider::CleanupDiscardShedOrder`; roles keyed on params via
the wish rule's shared `AaIs*` predicates):

| step | reading |
|---|---|
| buckets | ALCHEMIST (Accomplished Alchemist); LIFEGAIN (Nourish, Feed the Clan, Weather the Storm, Trufflesnout, Brindle Boar); UNTAP (Crab Umbra, Staff of Domination, Galvanic Alchemist, Seedcradle Witch); WINCON/DRAW (Stroke of Genius, Staff of Domination, Scholar of Athreos, Ezuri). MANA = lands, Arbor Elf, Utopia Sprawl, Wild Growth, Elvish Harbinger (a dork) |
| net of board | a bucket filled on OUR battlefield owes nothing: Alchemist -> ALCHEMIST; any untapper incl. the Witch -> UNTAP; Staff -> UNTAP + WINCON; Scholar / Ezuri -> WINCON; Brindle Boar -> LIFEGAIN (its sacrifice is a gain still to come; the wish rule's reading). Spells are consumed, so nothing else fills LIFEGAIN |
| keep | the best HAND card of each owed bucket. Agent default orders (the USER's to correct): LIFEGAIN Nourish > Feed the Clan > Weather the Storm > Trufflesnout > Brindle Boar; UNTAP Crab Umbra > Staff > Galvanic > Seedcradle Witch; WINCON Stroke > Staff > Scholar > Ezuri. Copies tie -> the earlier hand card |
| Staff | a member of BOTH UNTAP and WINCON; kept as either bucket's best it fills both. With a Crab Umbra AND a Stroke in hand it is the best of neither, so it is a spare (agent reading of "Staff can fill both") |
| Living Wish | one Wish per bucket still empty after the dedicated cards (hand order); a Wish past the gaps is a spare. Wishes fill only the four non-mana buckets |
| the rest | MANA: lands first (an Island moves to the front while no blue source is on our battlefield and a KEPT card needs {U}), then Utopia Sprawl > Arbor Elf > Wild Growth > Elvish Harbinger |
| shed order | unclassified (Eternal Witness) -> spare bucket cards, worst within-bucket rank first (equal ranks: WINCON, UNTAP, LIFEGAIN, ALCHEMIST) -> spare Wishes -> MANA worst first -> the kept bucket cards |

ONE index through `CleanupDiscardRankingWithOrder` (staged / required-piece protections engine-enforced);
prefix-stable (no verdict reads a card that sheds before it). `MTG_AA_BUCKET_DISCARD` (heurarm
`AA_BUCKET_DISCARD`), default ON; =0 is the inherited Selesnya policy exactly (unit-checked).
`MTG_AA_DISCARD_FAN=1` (testing only) returns the whole order for the evidence run.

**Evidence (the labels; 400 games d3 b10, seed 930000, `MTG_DISCARD_NODE=0 MTG_DISCARD_TRACE=1`, both arms
fanned):** inherited Selesnya 18 decisions, 100% optimal, regret 0; the USER's rule 22 decisions, 90.9% optimal,
mean regret 0.091 (2 rule-worse). Rate ~0.05 sheds/game, so the label stakes are ~0.005 turns/game.
* seed 930213 T1 (hand 8: Alchemist, Forest x3, Arbor Elf, Feed the Clan, Crab Umbra, Nourish): the rule sheds
  the spare lifegain (Feed); the labels want a mana card (Forest / Arbor Elf -> 6) -- and score shedding the ONLY
  untapper (Crab Umbra) just as well (6), while either lifegain spell costs a turn. Rule-structural (one LIFEGAIN
  keep) and, given the Umbra label, read as **clairvoyance**, not a visible-info miss. Surfaced to the USER.
* seed 930224 T4 (hand 9): the rule sheds a spare Staff (7); identical Crab Umbra copies label 7 and 6 -- **churn**.
The analyzer's own stage-3 A/B (`MTG_DISCARD_ORDER`) is structurally inert for this provider (the lever is read
only when the provider names no order), so it was stopped after the evidence stage; the labels above are its
trace, scored with its own `_LabelStats`.

**A/B (one pooled batch, heurarm arms, 3,200 games; unwon = 9; cost in contention-free work units -- the box was
shared with two sibling agents, so wall ms is not comparable):**

| cell | inherited | USER rule | delta | faster / slower | units mean | units p90 |
|---|---|---|---|---|---|---|
| s70001 d3 b10 (200) | 5.6950 | 5.7150 | +0.020 | 2 / 5 | 149.7k -> 151.4k | 444k -> 443k |
| s70001 d5 b20 (200) | 5.6300 | 5.6400 | +0.010 | 4 / 6 | 271.7k -> 280.0k | 798k -> 925k |
| s90001 d3 b10 (600 held-out) | 5.6883 | 5.6733 | **-0.015** | 24 / 16 | 132.8k -> 135.5k | 350k -> 347k |
| s90001 d5 b20 (600 held-out) | 5.6167 | 5.5950 | **-0.022** | 26 / 15 | 243.6k -> 247.4k | 693k -> 710k |

Pooled per depth (800 games): d3 -0.006, d5 -0.014. The inherited arm reproduces the wish branch's held-out cells
(5.6883 / 5.6167). Cost +1-3% units mean.

**Slower games at 10x budget (both arms):** 42 slower; **35 recover**, **7 persist** = five games (s70001 g6 d3,
g158 d3+d5, g188 d5; s90001 g146 d3, g558 d3+d5). First divergences (single-game logs, `tr/`):
* g188: the rule's own T1 real shed -- Staff (spare: Umbra + Stroke in hand) where the inherited arm's searched
  pass kept the Staff and shed the Umbra; no blue source held.
* g558: T4 cast Crab Umbra (rule) vs Staff of Domination (inherited); g6: T2 Elvish Harbinger vs Staff; g158: T2
  Harbinger's tutor-to-top takes a second Harbinger instead of the Alchemist; g146: T1 Utopia Sprawl held. No real
  shed precedes these: the rule reaches them only through the ROLLOUT's cleanup (search allocation), not a
  visible-info miss at a real cleanup. g158 is the case the sibling Harbinger-tutor rule addresses.

**Amendment, built and measured, PROPOSED, default OFF:** `MTG_AA_DISCARD_STAFF_FIRST` -- UNTAP becomes Staff >
Crab Umbra (the Staff is colourless and fills Wincon/Draw too). Same seeds, pooled (on-arm identity re-checked:
digest 6d12938b54ca185a both runs):

| cell | inherited | USER rule | + STAFF_FIRST | vs rule f / s |
|---|---|---|---|---|
| s70001 d3 b10 | 5.6950 | 5.7150 | 5.6950 | 3 / 0 |
| s70001 d5 b20 | 5.6300 | 5.6400 | 5.6200 | 3 / 0 |
| s90001 d3 b10 | 5.6883 | 5.6733 | 5.6750 | 9 / 10 |
| s90001 d5 b20 | 5.6167 | 5.5950 | 5.6100 | 4 / 13 |

It repairs the regression-seed cells (and is <= the inherited policy on all four), but gives back +0.002 / +0.015
on the 1,200 held-out games; units 149.3k / 275.8k / 135.2k / 244.0k. Not recommended over the rule's order; a
blue-conditional version (Staff first only while no blue source is held) is the untested next candidate.

**Smoke:** 124 / 128 -- only the four known pre-existing moves at their known digests (selesnya d3
a1937cded0ab6f58, d5 9bd0e14ec876faa9, critter d0 ccb5b47d5ef529c9, melira d0 0f633c1012b4fb11); the deck is not
in the suite. Not accepted. Unit: mtg-test 1524 / 1524; scenarios 228 / 228.
## USER Elvish Harbinger rule (2026-10-20) -- landed DEFAULT ON (the USER's rule); measures WORSE; two amendments PROPOSED, OFF

Branch `aa-harbinger` (worktree `.claude/worktrees/aa-harbinger`, forked from `accomplished-alchemist` @ 0d16858b).
Commits 56bcc019 (the rule + 9 unit cases + the `aaharbproof` trace), 3b330b2c (two PROPOSED amendments, default
OFF, + 1 unit case). Scratch: `/workspaces/MagicDeckTester/build/scratch/aa-harbinger/` (`ab/` the pooled A/B,
`x10/` the 10x reruns + shipped-budget traces of every slower/faster game, `amend/` the amendment arms, `x10b/`
the amendments' 10x reruns; `an.py` / `cls.py` / `clause.py` / `why.py`).

**The rule, verbatim (USER 2026-10-20):** "we should also have a heuristic for Elvish Harbinger. This one is even
easier. Search for the piece you are missing. Accomplished Alchemist is the priority if you don't have it and
should be pretty much an auto-pick. Otherwise Seedcradle for untaps or Ezuri for a win condition. In rare cases
you might get an arbor elf, but that wouldn't be common." And: "You would never search another Harbinger."

**The implemented reading** (`AccomplishedAlchemistProvider::TutorCandidates` -> `AaHarbingerSet`, Harbinger's
library Elf tutor only; the wish rule's census, "held" = hand OR our battlefield):
* No Accomplished Alchemist held and one in the library -> **{Accomplished Alchemist}** alone.
* Else, in this order, the union of: **Seedcradle Witch** while UNTAP is lacking (no Crab Umbra / Staff / Galvanic
  / Witch held -- a held Witch fills it whatever the lifegain total); **Ezuri** while WINCON is lacking (no Stroke /
  Staff / Scholar / Ezuri held -- Ezuri counts here, unlike the wish rule); **Arbor Elf** while MANA is lacking
  (projected mana < 4, the wish threshold; the entering Harbinger itself counts as a dork).
* Never Elvish Harbinger. Union empty or not in the library -> {Witch, Ezuri, Arbor Elf} in the library; that empty
  too -> {} (PerformTutor fetches nothing). The engine's tutor_optional decline variant is unchanged.
* Human play / the Tutor unprune gate keep the whole library list. `MTG_AA_HARBINGER_BUCKETS` (heurarm
  AA_HARBINGER_BUCKETS), default ON; =0 is the generic list. `MTG_TRACE=aaharbproof` + `MTG_TUTOR_CHOSEN_RANK=1`
  prints the rule's set and census beside each Harbinger fetch in either arm (diagnostic only).

**Measurement** (one pooled batch, both arms via heurarm, 3,200 games; unwon = 9; units are contention-free --
the box was shared with sibling batches and a compile, and the ON arm ran first under the heaviest load, so its
wall ms is inflated and is NOT comparable):

| cell | off avg | on avg | delta | faster / slower | units mean off -> on | units p90 | units max |
|---|---|---|---|---|---|---|---|
| s70001 d3 b10 (200) | 5.6950 | 5.7250 | **+0.030** | 17 / 18 | 149.7k -> 132.6k (-11%) | 444k -> 432k | 1.89M -> 1.25M |
| s70001 d5 b20 (200) | 5.6300 | 5.6350 | **+0.005** | 22 / 18 | 271.7k -> 225.7k (-17%) | 798k -> 732k | 3.98M -> 1.99M |
| s90001 d5 b20 (600 held-out) | 5.6167 | 5.6450 | **+0.028** | 52 / 57 | 243.6k -> 212.1k (-13%) | 693k -> 573k | 8.27M -> 5.53M |
| s90001 d3 b10 (600 held-out) | 5.6883 | 5.7233 | **+0.035** | 60 / 65 | 132.8k -> 117.2k (-12%) | 350k -> 284k | 5.33M -> 3.44M |

Wall ms/game (off arm, mean / p90 / max): d3 s70001 7,770 / 20,693 / 85,788; d5 s70001 10,578 / 30,366 / 106,699;
d5 s90001 11,627 / 29,806 / 422,522; d3 s90001 7,856 / 17,169 / 200,419. Digests: off d3/d5 s70001
6d9f924c48f68526 / c707a6f66e5dd288, on 903592183114220e / ec78e703098b8e69 (the on arm reproduced exactly in
the amendment batch).

**The USER's rule is worse on every cell (+0.005 .. +0.035) and 11-17% cheaper.** It stays the default (the
USER's rule); the root cause and an amended version follow.

**Slower games at 10x budget (both arms):** 158 slower games; **89 recover**, **69 persist**. Every slower (and
faster) game was re-run at the shipped budget with the proof trace and classified by the FIRST Harbinger fetch
where the arms differ (the clause that decided it):

| clause (control's fetch vs the rule's set) | slower | persist @10x | faster |
|---|---|---|---|
| never-Harbinger (control chained another Harbinger) | 47 | 18 | 47 |
| Alchemist auto-pick; control took Ezuri | 45 | 25 | 38 |
| Alchemist auto-pick; control took Arbor Elf | 14 | 8 | 2 |
| Alchemist auto-pick; control took Seedcradle Witch | 12 | 6 | 6 |
| Alchemist auto-pick; control took it on another turn | 3 | 0 | 6 |
| Witch excluded (untap held, WINCON lacking -> rule offers Ezuri) | 12 | 10 | 3 |
| in set, other pick / timing / same set different pick | 9 | 2 | 24 |
| Arbor Elf excluded | 0 | 0 | 7 |
| off declined / no Harbinger in the off arm | 2 | 0 | 11 |
| second Alchemist excluded | 1 | 0 | 3 |
| no Harbinger-fetch difference (search allocation) | 13 | 0 | 4 |

Root cause: **"never Harbinger" is neutral** (47 / 47), but **the Alchemist auto-pick loses** (74 slower vs 52
faster: with no Alchemist yet, the to-TOP fetch of Ezuri for a beatdown turn, or an Arbor Elf / Witch first and
the Alchemist from the next Harbinger, is often faster) and **the Witch is a wincon too** (12 vs 3: with Crab
Umbra held and no Stroke / Staff / Scholar, the control fetches the Witch, whose own +3/+3 pump is the untap loop's
kill, over the rule's Ezuri).

**Two amendments, built and measured, PROPOSED, default OFF (the USER's call):**
* `MTG_AA_HARB_WITCH_WIN` -- the Witch is also offered while WINCON is lacking, beside (before) Ezuri.
* `MTG_AA_HARB_ALCH_DEFAULT` -- "pretty much an auto-pick" as the DEFAULT, not the only option: no Alchemist held
  -> the Alchemist FIRST (the rollout's default pick), then Witch / Ezuri / Arbor Elf, all searched. Never Harbinger
  in either.

| cell | off | USER rule | + WITCH_WIN | + ALCH_DEFAULT | + both | units mean (both) |
|---|---|---|---|---|---|---|
| s70001 d3 b10 | 5.6950 | 5.7250 | 5.7200 | 5.6850 | **5.6800** (12 f / 8 s vs off) | 128.3k (-14% vs off) |
| s70001 d5 b20 | 5.6300 | 5.6350 | 5.6400 | 5.6100 | **5.5950** (12 / 6) | 234.1k (-14%) |
| s90001 d5 b20 | 5.6167 | 5.6450 | 5.6317 | 5.5967 | **5.5817** (36 / 16) | 213.0k (-13%) |
| s90001 d3 b10 | 5.6883 | 5.7233 | 5.7017 | 5.6517 | **5.6367** (48 / 17) | 118.2k (-11%) |

Both amendments together are better than the control on every cell (-0.015 / -0.035 / -0.035 / -0.052) and keep
the rule's ~11-14% unit saving. 10x on the 47 games "both" loses vs the control: **42 recover, 5 persist** (3
distinct games: s90001 gi233 -- the control's T2 Harbinger -> Harbinger -> Ezuri chain, the never-Harbinger
clause; gi341 and d3 gi151 -- the arms had already diverged before the first Harbinger). **Recommended: adopt both
amendments** -- the USER's call.

**Smoke:** 124 / 128 -- only the four known pre-existing moves at their known digests (selesnya d3
a1937cded0ab6f58, d5 9bd0e14ec876faa9, critter d0 ccb5b47d5ef529c9, melira d0 0f633c1012b4fb11). Not accepted.
Unit: mtg-test 1527 / 1527 (10 new Harbinger cases; the Harbinger and wish cases also green under
`MTG_UNPRUNE=tutor` and `MTG_UNPRUNED=1`); scenarios 228 / 228.

## USER Living Wish buckets (2026-10-20) -- landed DEFAULT ON (the USER's rule); two amendments PROPOSED, OFF

Branch `aa-wish` (worktree `.claude/worktrees/aa-wish`, forked from 39848afe). Commits e474b968 (the rule +
12 unit cases), 07038489 (the `aawishproof` trace), 177b4e98 (two PROPOSED amendments, default OFF).
Scratch: `/workspaces/MagicDeckTester/build/scratch/aa/wish/` (`ab/` the pooled A/B, `x10/` the 10x reruns,
`trace*/` the proof traces, `amend/` the amendment arms; `an.py` / `cls.py` / `why.py`).

**The rule, verbatim (USER 2026-10-20):** "We should create a heuristic for Living Wish as there are only 7
relevant targets and most can be eliminated based on what you have in hand. Accomplished Alchemist is good
whenever you don't have one. Arbor Elf or lands are specifically when you don't have 4-5 mana, (the elf
preferred when we can untap for more than 1 mana) I would only take Brindle Boar if we have no other lifegain
in hand. Galvanic Alchemist should only be taken if we have no untapper (Crab Umbra, Staff of Domination) in
hand. (maybe we can allow it when you have Seedcradle Witch if you only have something that gains 4 like the
Boar) I would kind of like to skip Trufflesnout ... Scholar of Athreos is the wincon and should be taken only
if we have no wincon or draw in hand or if we need nothing else. (it's fine to end the game with the Scholar
once we have the infinite mana) Essentially everything divides into buckets Mana, Accomplished Alchemist,
Lifegain, Untap, Wincon/Draw and we only need to search for buckets where our current hand is lacking." Plus
"Staff of Domination is a special card in the list as it can do the untap and the draw", and on Eternal Witness:
"We don't need Eternal Witness in Goldfishing. It is good in real games because it can do things like bringing
back a killed Alchemist" (in a goldfish nothing kills our creatures).

**The implemented reading** (`AccomplishedAlchemistProvider::TutorCandidates` -> `AaWishBucketSet`, wish
fetches only; keyed on params; "held" = hand OR our battlefield):

| bucket | lacking iff | offers |
|---|---|---|
| Mana | projected mana < 4 (the USER's "4-5": 4 taken as the threshold) = lands in play + land-Aura extra + mana creatures in play (Arbor Elf also counts its best Forest's Aura bonus) + lands in HAND; the Alchemist's X not counted | Arbor Elf ALONE when it re-taps a land-Aura Forest (> 1 mana); else Arbor Elf + Forest, + Island only with no blue source held (the Alchemist is not one: its X is 0 without a gain) |
| Alchemist | no Accomplished Alchemist held | Accomplished Alchemist |
| Lifegain | no cast_lifegain / etb_modal_lifegain card in hand, no sac_outlet_lifegain creature in hand or play; the Staff's {2},{T}: gain 1 does NOT count | Brindle Boar (Trufflesnout never) |
| Untap | no Crab Umbra / Staff / Galvanic held, and no Seedcradle Witch -- the Witch counts unless the held lifegain SUMS to < 5 (printed base: Nourish 6, Feed 5, Weather 3, Boar 4, Trufflesnout 4) | Galvanic Alchemist |
| Wincon/Draw | no Stroke of Genius / Staff of Domination / Scholar held | Scholar of Athreos -- also when NO bucket is lacking |

Order = the USER's bucket order (Mana: Elf, Forest, Island; Alchemist; Lifegain; Untap; Wincon) -- the rollout's
default pick. Eternal Witness is never offered. Nothing admitted -> the whole legal list. Width 9 while on
(coverage of the widest admitted set, 7, and the fallback). The loop's Scholar kill is untouched (the closed-form
loop binds the wish's target by name). Human play / the Tutor unprune gate keep the full list (the rule's set
ranked first by RankedTutorCandidates). `MTG_AA_WISH_BUCKETS` (heurarm AA_WISH_BUCKETS), default ON; =0 is the
old sideboard-order list at width 6 -- byte-identical: the off arm's s70001 d5 b20 digest is f4d9cf88b0c3a26c,
P3's committed cell.

**Measurement** (one pooled batch, both arms via heurarm, 3,200 games; win turn with an unwon game = 9; cost in
work UNITS, which are contention-free -- the box was shared with a sibling agent's batches, so wall ms is noisy
and given only for the tail):

| cell | off avg | on avg | delta | faster / slower | units mean off -> on | units p90 | slowest game |
|---|---|---|---|---|---|---|---|
| s70001 d3 b10 (200) | 5.8150 | 5.6950 | **-0.120** | 25 / 12 | 166.9k -> 160.3k | 458k -> 447k | 133 s -> 69 s |
| s70001 d5 b20 (200) | 5.7450 | 5.6350 | **-0.110** | 24 / 7 | 313.5k -> 292.3k | 947k -> 874k | 360 s -> 279 s |
| s90001 d5 b20 (600 held-out) | 5.6400 | 5.6167 | **-0.023** | 48 / 48 | 294.5k -> 259.5k (-12%) | 786k -> 698k | 6,157 s -> 591 s |
| s90001 d3 b10 (600 held-out) | 5.6900 | 5.6883 | -0.002 | 48 / 53 | 160.4k -> 143.6k (-10%) | 422k -> 376k | 4,859 s -> 263 s |

Net <= 0 on every cell. The off arm reproduces P3's committed cells exactly (digests ed03ab6957a09593 /
f4d9cf88b0c3a26c). **The gi286 class collapses**: seed 90287 gi286 -- the 40-minute "Living Wish variant
width" game of P3 -- is 6,157 s and UNWON (off) vs 492 s and T7 (on) at d5 b20; at d3 b10 it is 4,859 s T8 vs
215 s unwon, and the on arm wins T7 at 10x (b100, 321 s) -- no wish ever RESOLVES in that game, so the d3 move is
search allocation through the wish axis's rollouts, not a fetch.

**Slower games at 10x budget (both arms):** 119 slower games; **83 recover** (on <= off at 10x), **36 persist**
(s70001: 5 at d3, 5 at d5 -- the same five games; s90001: 14 at d3, 12 at d5). The 17 persistent d5 games were
traced (`MTG_TUTOR_CHOSEN_RANK=1 MTG_TRACE=aawishproof`, both arms; 6 of 238 single-game reruns differ from the
pooled game by cache history) and every one attributes to a named rule clause:
* **Trufflesnout excluded (7):** s70001 g19 / g26 / g178 (lifegain HELD -- the control wished Trufflesnout on T2
  with 3 mana, a 3-drop body / 4 life), s90001 g15 / g189 / g257 / g571 (lifegain lacking -- the control preferred
  Trufflesnout to the Brindle Boar the rule offers).
* **A second (or third) Alchemist excluded (4):** s90001 g116 / g410 / g524 / g550 -- an Alchemist already in
  PLAY (with Staff + Umbra, or Witch), the control fetched another; more Alchemists multiply each gain.
* **Blue (3):** s90001 g74 / g147 / g160 -- no blue source, Stroke / Crab Umbra in hand, but mana >= 4 (hand lands
  counted), so the rule offered no Island; g550's first fetch is the same (Elvish Harbinger IN HAND counted as
  blue).
* **Forest at mana 4 (1):** s70001 g167 -- the hand land made it 4, the control wanted the 5th mana.
* **Same set, different pick (2):** s70001 g147, s90001 g376 -- the rule admitted the control's card; the search
  took another (the default front moved: Mana first, where the sideboard order had the Alchemist first).

**Two amendments, built and measured, PROPOSED, default OFF (the USER's call):**

| cell | USER rule | + `MTG_AA_WISH_BLUE` | + `MTG_AA_WISH_TRUFFLE` |
|---|---|---|---|
| s70001 d3 b10 | 5.6950 | 5.6900 (8 f / 5 s vs rule) | 5.7950 (2 / 13) |
| s70001 d5 b20 | 5.6350 | 5.6350 (6 / 6) | 5.6400 (4 / 6) |
| s90001 d5 b20 | 5.6167 | 5.6233 (23 / 26) | **5.6000 (29 / 20)** |
| s90001 d3 b10 | 5.6883 | 5.6883 (24 / 23) | **5.6500 (34 / 16)** |
| units mean (d5 held-out) | 259.5k | 272.1k | 254.5k |

* `MTG_AA_WISH_BLUE` (blue as its own need: Island whenever no blue source is FIRMLY held -- a dork in hand no
  longer counts -- while a held card needs {U}): neutral to slightly worse and ~5% dearer. Not recommended.
* `MTG_AA_WISH_TRUFFLE` (Trufflesnout beside the Boar when lifegain is lacking): better on the 1,200 held-out games
  (-0.017 / -0.038) and cheaper, worse on the 200-game d3 set (+0.100); pooled over all 800 games per depth
  -0.011 (d5) / -0.004 (d3). The USER's own "slight question mark" on Trufflesnout shows up as a real but mixed
  effect -- a candidate, not a recommendation.
* NOT built: admitting a second Alchemist (it reverses the USER's "whenever you don't have one"), and counting
  only ONE hand land toward the mana threshold (one game).

**Smoke:** 124 / 128 -- only the four known pre-existing moves at their known digests (selesnya d3
a1937cded0ab6f58, d5 9bd0e14ec876faa9, critter d0 ccb5b47d5ef529c9, melira d0 0f633c1012b4fb11); the deck is
not in the suite, so nothing else could move. Not accepted. Unit: mtg-test 1512 / 1512 (13 new wish cases, also
green under `MTG_UNPRUNE=tutor` and `MTG_UNPRUNED=1`); scenarios 228 / 228.

## Chunk P3 (2026-10-19) -- landed: three byte-identical per-node cuts; the deck is still over the line

Commits (branch `accomplished-alchemist`): 86ba02b3 (AppendSubdecisionAxes), 22d15f95 (UntapLandHosts),
e6f38737 (bottom-trial early stop). Scratch: `/workspaces/MagicDeckTester/build/scratch/aa/p3/` (perf data,
`cost/` = the 200-game cells, `t_*` / `ps_*` / `te_*` = the repro runs). Base = 5e7998d6 (`p3/mtg.base`).

* **The gi286 class (seed 90287 gi286 d3 b10, a 40-minute game).** perf (callgraph, a 40 s window): the
  resolve-mode TUTOR axis was 18% of the game on its own -- one `Plan v = p` copy per Living Wish variant
  (9.6%) plus a move into a side vector, its reallocation moves and a second move into the list (6.5%), and
  the decline variant (1.7%) -- and the ~30 per-cast passes' action walks another ~6%. (P2's
  `BpCandFingerprint` 5% was its own MTG_TURN_CENSUS instrument: `s_rollout_stats` is on under the census.)
  - 86ba02b3: the tutor axis sizes its variants in one loop and copy-constructs them onto the end of the list
    (capacity grown exactly as `vector::insert` grew it -- it feeds the plan cache's byte accounting); one
    walk per plan records which per-cast traits it holds (wish x2, dig, Ponder, sac-land, Saga I, sac-creature
    colour, Wave, untap loop) and each pass skips a plan whose bit is clear (every bit is a necessary
    condition of that pass's own test); the dominated Stroke+loop erase is `remove_if` spelled out so the
    traits move with the plans. AppendSubdecisionAxes 27.6% -> 19.2% of the window; what is left is the
    variant copies themselves.
  - Full game, base vs P3 as two concurrent single-thread runs: **2,601 s -> 2,307 s (1.13x)**, digest
    df7a74d6161cbaa3 and 1,794,357 units in both (690 -> 778 units/s; the deck norm is ~23 k).
* **22d15f95 (every Alchemist game).** The land-Aura readers zero-initialised a 424-byte `UntapLandHosts`
  for every NONLAND permanent before `UntapLandHostsOf` rejected all but the Arbor Elf (LandAuraBonus +
  UntapLandHostsOf were 3.4% of the cell's samples); the same source test now runs first.
* **e6f38737 (bottom trials).** Every reader of a bottom trial's win turn asks only whether it EQUALS the
  pass's best, so a trial with no win by the end of turn B (the best an earlier trial of the pass reached)
  stops there and reports B + 1 -- decision-exact by construction (argument in the code; off under the
  two-stage refine and, on the subset table, under an external chooser). Measured: units -1.6% (d5 b20) /
  -1.7% (d3 b10) only -- at d5 a trial's early turns already search to the horizon, so the turns after B are
  a small share. The one channel by which it could move a later trial is the shared bottoming TT (fewer
  late-turn entries): reachable only through a key collision across different kept hands, or the TT's
  store cap binding; neither showed (identity below).
* **Byte-identity:** mtg-test 1499/1499; scenarios 228/228; smoke: all 128 case digests equal to HEAD's
  (P2's smoke run), i.e. only the four known moves at their known digests (selesnya d3 a1937cded0ab6f58, d5
  9bd0e14ec876faa9, critter d0 ccb5b47d5ef529c9, melira d0 0f633c1012b4fb11); the deck's 200-game cells
  identical (d3 b10 ed03ab6957a09593, d5 b20 f4d9cf88b0c3a26c). Not accepted.
* **Cost, seed 70001, 200 games** (base and P3 as two concurrent 15-thread batches; ms per game):

  | cell | base mean / p90 / max | P3 mean / p90 / max | core-s | units mean |
  |---|---|---|---|---|
  | d3 b10 | 7,031 / 17,987 / 65,181 | 7,075 / 18,443 / 63,058 | 1,406 -> 1,415 | 170 k -> 167 k |
  | d5 b20 | 12,861 / 36,156 / 147,815 | 12,643 / 34,375 / 150,638 | 2,572 -> 2,529 (0.98x) | 319 k -> 314 k |

  Single thread (perf stat, instructions -- the cycle counts were SMT-contended): g60 (seed 90061 gi60 d5 b20)
  468.7 G -> 463.6 G (-1.1%), 67.4 s -> 66.2 s; g123 (seed 70124 gi123 d5 b20) 809.0 G -> 807.9 G (-0.1%).
  **Still 1.42x over the 8,907 ms line at d5 b20.**
* **Residual: there is no lossless per-node target left that is worth the line.** The cell profile
  (callgraph, 12 threads) is flat: SolveWithLookahead 74% inclusive; SolveUncached 33%; SimulateToEndImpl
  34%; ApplyPlanDirect 33%; EnumeratePlans 19%; mana payment (TapForCostShared) 15% -- 6.5% from the
  enumeration's SubsetPayableWithFilters, 2.4% from ApplyActivatedUntap's per-iteration payer; BuildSimKey
  4.6%; nothing else over ~3%. The line needs the USER's levers (bottom_eval, the mulligan stage,
  MTG_SOLVE_CHARGE_W, MTG_ENUM_CHARGE), not more per-node work.
* **One finding for the USER (not changed): the breakpoint-continuation cache thrashes on the tail class.**
  `BpEnumEntryFor` clears its WHOLE cache when the plan-cache byte pool (3.2 GB, global) or the 8,192-entry
  cap binds, and the tail games re-derive what it threw away. g60 single-thread: default 12,733 misses / 5
  clears / 59.6 s; pool and cap unbounded 9,992 / 0 / **49.2 s (-17%)**, same digest and units
  (pool only: 10,844 / 1 / 54.0 s; cap only: unchanged). gi286 is a different animal: with the pool
  unbounded the cache reached 32.9 GB in 68 s and the process hit the 35 GB RSS cap -- its continuation
  lists are GB-scale (the Living Wish variants of every plan, re-derived per breakpoint state), so no cache
  size covers that class; it is a list-WIDTH problem. For the g60 class a smarter eviction (two
  generations, the hot half kept) would recover most of the 17% inside the same memory bound -- but cache
  HISTORY can move work units and so play (ClearPerGameCaches' note: a KittyEquipment game's digest moved
  with it), so an eviction policy is not byte-identical by construction and was left to the USER.

## Chunk P2 (2026-10-19) -- landed: lossless per-node cuts; the enumeration-charge lever measured, OFF

Commits (branch `accomplished-alchemist`): 93b53a61 (`CardDatabase::Lookup(InternedName)`), 3f014b9c
(EnumeratePlans dedup + Aura host key), 77362b04 (the default-OFF lever `MTG_ENUM_CHARGE`).
Scratch: `/workspaces/MagicDeckTester/build/scratch/aa/p2/` (perf data, folded stacks, `cost/`, `abB/`).

* **Where the repro's time was.** Seed 90061 gi60 (`--seed 90061 --game-index 60 --games 1 --budget-ms 20`,
  the deck profile, d5): one T1 decision is 90 of the 91 s -- 453 k units, 13.9 M odometer visits, 8.7 M emitted
  plans, 12.5 k breakpoint-continuation cache misses (`MTG_TURN_CENSUS`). 85% of the CPU is
  `BpDeriveContinuationList -> EnumeratePlans`, and inside it the creature-Aura host ranking in the dedup
  (Crab Umbra: 83% of the plans in those enumerations get an `AuraHostKey`, 2.64 M keys on 839 k bases) was
  28%, `plan_signature` 10%, the string-keyed card lookup ~4%.
* **The cuts (all byte-identical by construction):** the CastFromHand signature appended into one buffer (it
  was ~30 chained `cond ? (...) : ""` temporaries) and the signature buckets thread-local/reused; the host
  bases pooled per thread and refilled by copy-assignment (`AuraPlanHostBaseInto`); per-plan key slots in a
  flat vector; each creature's swing memoised across the key's now/next sums and the dork walk
  (`AuraHostKeyCreatureSwing`); one `try_emplace` on the pick map, which also replaces the second `seen` set;
  the plan value sort done on (wins, value, index) keys with one permute; the two per-plan Aura-order
  stable_sorts replaced by `StableSortByBoolKey` (key once per action, no buffer when already ordered);
  `plan.actions` reserved; `CardDatabase::Lookup(InternedName)` (pointer hash, same map coverage).
* **Repro, single thread (Release):** 88.1 s -> 58.2 s at the identical 494,867 units and digest
  095c8c1e7b9ce516 -- **5,617 -> 8,503 units/s (1.51x)**. A greedy-walk-bound game is untouched, as expected:
  seed 70124 gi123 d5 b20 (1,550,573 units) 54.4 s base vs 56.8 s P2 (two concurrent single-thread runs; noise).
* **Byte-identity:** mtg-test 1499/1499; scenarios 228/228; smoke 124/128 with exactly the four known moves at
  their known digests (selesnya d3 a1937cded0ab6f58, d5 9bd0e14ec876faa9, critter d0 ccb5b47d5ef529c9, melira
  d0 0f633c1012b4fb11); the deck's 200-game cells identical (d3 b10 ed03ab6957a09593, d5 b20 f4d9cf88b0c3a26c).
* **Deck cost, seed 70001, 200 games** (base = fc7b8a54 snapshot `build/scratch/aa/mtg.p2base`; the two
  binaries ran as two concurrent 16-thread batches so contention is shared; ms per game):

  | cell | base mean / p90 / max | P2 mean / p90 / max | core-s |
  |---|---|---|---|
  | d3 b10 | 7,246 / 19,177 / 69,118 | 7,249 / 18,493 / 64,525 | 1,449 -> 1,450 |
  | d5 b20 | 14,606 / 40,888 / 178,414 | 13,826 / 39,477 / 153,584 | 2,921 -> 2,765 (0.95x) |

  **Still over the 8,907 ms line at d5 (1.55x).** The per-node class the repro exposed is a TAIL class (seed
  90001's gi60 / gi286 / gi408); this 200-game set is dominated by other cost: its heaviest games run 1.5-4.1 M
  units each (the pre-keep-table bottom trials) at a normal ~23 k units/s contended, and the low-ratio ones are
  greedy-walk bound (gi123, 10 k units/s: `SolveUncached` 63% of CPU -- the consider loop, `ColorFeasibility`,
  `AttackTapLoss`), i.e. chunk P's walk, which `MTG_SOLVE_CHARGE_W` prices (a USER call, chunk P).
* **B -- `MTG_ENUM_CHARGE` (DEFAULT OFF; per-job `"enum_charge_w"`, env `MTG_ENUM_CHARGE_W`, default 4).**
  Bills EnumeratePlans' odometer visits, ceil(visits / W) units per enumeration, to `TurnSolver::DecisionBudget()`
  (nullptr at an unlimited root, so inert at `--budget-ms 0`; the enumeration itself always completes -- a pure
  re-pricing). Census bucket `u_enum_charge`. Repro: off 494,867 units / 79 s, W=16 627 k / 32 s, W=4 755 k /
  12 s (3-way contended). Held-out seed 90001, 600 games, one pooled batch (32/32 busy), paired over the 599
  games every arm finished (gi286 is excluded -- see below):

  | cell | arm | avg | faster / slower vs off | mean ms | p90 | core-s |
  |---|---|---|---|---|---|---|
  | d5 b20 | off | 5.6344 | -- | 12,588 | 30,695 | 7,540 |
  | d5 b20 | W=16 | 5.6444 | 1 / 5 | 11,560 | 29,757 | 6,925 (0.92x) |
  | d5 b20 | W=4 | 5.6528 | 1 / 11 | 10,416 | 25,701 | 6,239 (0.83x) |
  | d3 b10 | off | 5.6861 | -- | 7,961 | 17,872 | 4,769 |
  | d3 b10 | W=16 | 5.6895 | 2 / 4 | 7,443 | 16,572 | 4,458 (0.93x) |
  | d3 b10 | W=4 | 5.6962 | 6 / 12 | 6,408 | 14,881 | 3,839 (0.80x) |

  THE TAIL GAME gi286 (a bottom-trial game: several full trial games, one T6 decision alone 226 k units /
  35 s, 13 M greedy-walk visits): W=16 617 s (d3) / 629 s (d5), W=4 378 s / 614 s; the OFF arm was still
  running at both cells after ~36 min (> 2,100 s) and was stopped (my own probe; the pre-P2 binary ran it > 45 min). That single game is worth +1 s or more per game on a 600-game
  mean, which is why the cells' means sit near the line or over it whatever the arm. Its profile is not the
  Aura-key class: `AppendSubdecisionAxes` ~29% (Plan variant copies 10.5%, 33 full scans of the plan list
  9.6% self), `BpCandFingerprint` 5%, EnumeratePlans 10% -- a candidate for the next per-node pass.
  gi408 (d5): off 163 s, W=16 191 s, W=4 360 s (7.4 M units) -- the charge can make a game DEARER, by
  driving it into the no-completed-pass fallbacks.

  Smoke with the lever on (not accepted): W=16 moves 21 cases beyond the four known (searched 6 slower / 5
  faster games; hinata d3 +0.013, hinata2hg +0.027, soldiers2hg +0.020, antilife d3 +0.008), W=4 moves 30
  (11 slower / 5 faster). **Verdict for the USER (not flipped):** a net quality LOSS on this deck at both
  weights, and neither arm reaches the line at d5 (W=4 is 10.4 s/game). Not recommended as the route to the
  line; it stays OFF.
* **Residual cost location.** (1) Search units: the pre-keep-table bottom trials (the pending bottom_eval /
  mulligan-stage USER decision is still the big lever). (2) The greedy horizon walk (`SolveUncached` visit
  loop) on the low-ratio games. (3) The tail class (continuation re-enumeration with the Aura host key):
  now 1.5x cheaper per unit, still ~3x the deck norm per unit; the remaining enumeration cost is per-plan
  work (Action copies, Plan moves, the host key's power walk, the base GameState copy) rather than waste.

## Suite entry attempt (2026-10-19) -- bottom_eval A/B; NOTHING adopted, NO rows added

Binary 814d8954 (Release; mtg-test 1499/1499). Arms are per-job profile variants in ONE pooled `mtg --batch`
per round (32 threads, heartbeat 32/32), `MTG_SLOW_GAME_MS=1` for per-game ms + units. Raw:
`build/scratch/aa/beval{,2,3}/` (an.py there). Arms: a = unset (status quo); b = depth 0 + topk 5 (Melira);
c = depth 0 + topk 3; d = depth 0, no refine (floor); e = budget_ms 2 + topk 3 (stage 1 SEARCHED at the
real depth, cheap); f = b2 k5; g = b5 k3; h = b2 k2. Rationale for e-h: Melira's depth-0 stage 1 is a
greedy rollout, which cannot rank this deck's hands (b/c/d lose 0.05-0.15 turns everywhere).

Seed 70001, 200 games (avg win turn, unwon = 9; faster/slower vs a; mean ms/game, 32-way contended):

| arm | d3 b10 | d3 b20 | d5 b20 | d5 b40 | ms/game d3b10 / d5b20 |
|---|---|---|---|---|---|
| a | 5.8150 | 5.7550 | 5.7450 | 5.6850 | 7,509 / 13,414 |
| b d0 k5 | 5.8850 (0/11) | 5.8100 (0/8) | 5.8000 (0/8) | 5.7300 (0/8) | 5,645 / 10,317 |
| c d0 k3 | 5.8850 (1/13) | 5.8150 (0/9) | 5.8050 (0/9) | 5.7350 (2/11) | 4,182 / 7,170 |
| d d0 | 5.9650 (1/20) | 5.9100 (0/19) | 5.9000 (0/19) | 5.8350 (1/19) | 1,967 / 3,398 |
| e b2 k3 | 5.8100 (3/2) | 5.7600 (1/1) | 5.7600 (0/2) | 5.7000 (2/4) | 5,462 / 7,977 |
| f b2 k5 | 5.8200 (2/2) | 5.7600 (1/1) | 5.7600 (0/2) | 5.7100 (2/5) | 7,440 / 10,343 |
| g b5 k3 | 5.8200 (2/3) | 5.7600 (1/1) | 5.7600 (0/2) | 5.6950 (2/3) | 6,987 / 9,474 |
| h b2 k2 | 5.8050 (4/2) | 5.7650 (2/2) | 5.7600 (2/3) | 5.6950 (4/5) | 4,727 / 7,092 |

10x budget re-runs of e's / h's slower games (both arms): RECOVER gi72 (3/3 in every cell), gi93 (7/7), gi164
(6/6), gi34 (5/5); PERSIST gi21 (a 4, e/h 6 at d5 b400), gi48 (a 5, e/h 6), gi173 (a 5, h 6 at every 10x cell).

Held-out seed 90001, 600 games, suite cells (paired over 599; gi286 of arm a ran > 45 min at both cells and
was not waited for): d5 b20 a 5.6344, e 5.6144 (-0.020, 13 faster / 6 slower), h 5.6077 (-0.027, 16 / 7);
d3 b10 a 5.6861, e 5.6795 (-0.007, 10 / 5), h 5.6728 (-0.013, 13 / 6). Held-out slower games NOT re-run at 10x.
Mean ms/game at d5 b20: a 14,841, e 10,532, h 9,269 -- ALL OVER the 8,907 line on this sample.

**Verdict (decision rule; mulligan policy is the USER's): no arm qualifies, nothing adopted, no rows added.**
No arm is win-turn-identical; e/h are net better on the 600-game held-out sample but net +0.015 at d5 on the
200-game set, and three slower games persist at 10x (gi21, gi48, gi173). And no arm is reliably under the
line: h at d5 b20 is 7,092 ms on seed 70001 but 9,269 on seed 90001.

**Why bottoming is not the whole story -- per-node cost the work meter does not see.** The held-out tail is
games whose ms/unit ratio is ~5x the norm, IDENTICAL across arms (so not bottoming): 90061 (gi60) d5 b20 is
494,867 units in every arm and ~450 s contended / 94 s single-thread (5.2 k units/s vs ~25 k typical); gi286,
gi408 likewise. perf on gi60 (Release, single thread): plan enumeration dominates --
`EnumeratePlansWithLandUncached` 13%, `BpDeriveContinuationList` 14% (`AppendSubdecisionAxes`), string
construction from `char*` + `operator+` 9%, `CardDefinition` hashtable `find` by string 2%, `vector<Action>`
push/destroy 6.5%, allocator ~20%. This unmetered work is ALSO why the contended wall is ~4.8x single-thread
on these games (allocator-bound). Per the collapse rule, this is the per-node stage: a candidate engine chunk
(cache the enumeration per state / remove per-node string building) ahead of, or alongside, the bottom_eval
decision.

## Chunk E1 (2026-10-19) -- landed: the Stage-5 / claude-play engine fixes

Commits (branch `accomplished-alchemist`): c05eed09 (fd lockstep), bb2ab20c (Arbor Elf), 700c5041 (site-9
re-open + chained payoffs), b13de112 (loop colour, Stroke+loop drop, wish singletons). Scratch:
`/workspaces/MagicDeckTester/build/scratch/aa/e1/`.

* **fd-diverge lockstep (c05eed09).** (1) AIEngine's clean-set cast loop replayed the recorded segment AT the
  cast for every cast `equip_bp_truncates` answers true -- but only the inline site-6 class is armed at the
  cast by ApplyPlanDirect; the plain-cantrip partition (a self-targeted Stroke) and the BP_NODE_D56 deferred
  5/6 classes run their continuation after the trailing pass, after site 9 and after the Karoo drop.
  Replaying early also set `bp_replayed` and stood the site-9 twin down (gi54). Now those classes defer to the
  end-of-main catch-all (committed) / `acq_pending` (no-commit), exactly as `acq_deferred`; d0 keeps the
  inline re-solve. (2) The site-9 twin's `pe_cast` loop now truncates after a partitioning cast like the
  rollout's `bp_truncate` (gi120). Scenario `aa_site9_cont_partition_truncates` (gi120's board: old binary T6,
  new T5). gi54's board could not be staged (the fixture names an Aura host only by card name); covered by the
  seed repro and the FD sweep.
* **Arbor Elf (bb2ab20c, sub-agent).** "Untap target Forest" = one more tap of the best Forest, every land
  Aura included, in every projection and payer (the four land-Aura readers route an Arbor Elf to the Forest it
  re-taps, `UntapLandHostsOf`); a Forest whose bonus is a sub-multiset of another's is dropped as weakly
  dominated, incomparable ones (Sprawl-U vs Wild Growth) stay separate payer choices. 4 unit cases + scenario
  `aa_arbor_sprawl_retap_stroke` (old T3, new T2). Card note rewritten. Known residue: the greedy payer takes
  the highest-yield Forest without colour look-ahead (the backtracker covers a failure); no human Forest picker.
* **Site-9 re-open + chained payoffs (700c5041).** `MTG_SITE9_REOPEN` (default ON, EngineFlags.h): after an
  applied site-9 continuation, an ability it made activatable opens the next counted occurrence in BOTH worlds,
  only when the plan's choice targets it (bp_all / bp_at), safety ceiling 32 (never a lever). New need kinds
  kLoopPayCastDrain / kLoopPayWishDrain / kLoopPayCastPump (setup casts priced in front, kill fold admits the
  setup cast), ranked SHORTEST CHAIN FIRST: one-step kill > Staff draw to a Stroke > chained > draw to a
  chained card. (The first build ranked purely by cost and made 80005 T4 -> T5: Ezuri-from-hand undercut the
  Staff's dig to Stroke; and an UNTARGETED re-open's section-window reset erased the hand entry the drawn
  Stroke's site-10 continuation arms on -- both fixed before landing.)
* **Loop colour, Stroke+loop, wish singletons (b13de112).** `BurstPendingActColour`: an Alchemist tapped for a
  generic pip floats the colour the plan's pending trailing activations still owe (PlanTraits::act_pips), at
  both greedy generic-pip tap points. "Non-lethal opponent Stroke + loop" plans are dropped (dominated world by
  world by loop-only / Stroke-only, argument in the code). A 0-iteration loop is pinned to do nothing (sound:
  scores identical to no-loop). Wishes: autonomous play resolves targets at resolution (MTG_TUTOR_AXIS_RESOLVE,
  default ON -- the TurnSolver comment calling it default-off is stale), so two wishes already fetch distinct
  cards (pinned); the human menu drops a plan naming one sideboard copy twice.
* **Staff {5},{T} draw standalone (80005):** VERIFIED offered (unit test); no change.
* **Budget (sub-agent; NO code change).** No leak: every heavy decision stops at the designed pass ceiling
  (`FullSearchLine`, used + `kOverrunBudgetMult` 25 x the decision budget = 900,000 units at a 36,000 budget;
  overshoot <= 0.2%). Passes reach the ceiling because the start gate extrapolates earlier passes' growth and
  the next ply lands on a very wide turn (true next-pass costs 1.4-2.4 M units, ~1,000-1,900x). The wall is
  mostly MULLIGAN BOTTOM-TRIAL games (no keep table yet, so every candidate bottom is a full d5 game):
  gi40 98% (4.98 M of 5.1 M units), 80012 214 s of 241 s, 80019 1,068 s of 1,094 s. Options, all USER calls:
  the mulligan stage (keep table) or `bottom_eval_depth/_budget_ms` in the profile; a lower
  `kOverrunBudgetMult` (calibration notes warn it lost Melira games at 11x); `MTG_SOLVE_CHARGE_W=16` (chunk P);
  a start-gate estimate aware of a wide next ply (engine-wide design work). Raw: `build/scratch/aa/budget/`.
* **Validation.** mtg-test 1499/1499; scenarios 227/227. FD oracle d5 b40 x200 seed 70001: **0 [fd-diverge]**
  (was 2); MTG_FLAG_NONCONV d3 x200: **0** -- both on the FINAL binary (b13de112; d5 b40 avg 5.6850, d3 b20
  5.7550 vs 5.8200 before; after c05eed09 alone: 0 / 0 as well). Smoke (not
  accepted): 124/128, the four known moves with their KNOWN digests (selesnya d3 a1937cded0ab6f58, d5
  9bd0e14ec876faa9, critter d0 ccb5b47d5ef529c9, melira d0 0f633c1012b4fb11) -- no other case moved, searched
  slower 0.
* **Deck measurement** (one pooled batch per arm, run concurrently at 15 threads each, seed 70001, 200 games):

  | cell | avg before | avg after | faster | slower | unwon b/a |
  |---|---|---|---|---|---|
  | d5 b20 (play) | 5.8100 | 5.7450 | 22 | 8 | 12 / 13 |
  | d5 b40 (suite) | 5.8050 | 5.6850 | 27 | 6 | 13 / 13 |

  Every slower game re-run at 10x budget, both arms (d5 b200 / b400): ALL recover -- after <= before at 10x in
  all 14 (b20: gi4 4->6 [10x 4/4], 18 5->6 [5/5], 21 5->6 [before 5, after 4], 83 4->5 [5/5: before's T4 was
  budget luck], 152 5->6 [before 6, after 5], 155 5->7 [4/4], 168 8->unwon [8/8], 188 5->6 [5/5]; b40: gi18,
  101, 140, 188 5->6 [5/5], 86 6->7 [6/6], 168 8->unwon [8/8]). Budget churn, not a regression.
  Claude-play seeds (d5 b20, gi 0): 80017 6 -> 5, 80020 5 -> 4, 80005 4 -> 4, 80010/80012/80013/80018 unchanged.

  | cost (contended, per game) | mean | p90 | max | mean units |
  |---|---|---|---|---|
  | d5 b20 before / after | 11,850 / 11,875 ms | 37.7 / 34.4 s | 100.6 / 109.1 s | 312 k / 319 k |
  | d5 b40 before / after | 20,385 / 21,754 ms | 64.0 / 62.6 s | 286.0 / 250.0 s | 513 k / 512 k |

  E1 leaves the cost flat (units within 2%). The absolute mean at this seed is ~2.4x suite_gate's 8,907 ms
  line (chunk P measured 7.1 s at seed 9100 / 60 games): the bottom-trial games above, which the mulligan stage
  removes. Flagged for the suite entry.

## Chunk P (2026-10-18) -- landed: performance (the 3x cost rule)

Goal: per-game cost under `suite_gate.py`'s line (3 x galadriel's 2,969 ms = **8,907 ms/game**, the max over
the deck's searched cases of case-ms / games). Measured on one pooled batch, 60 games x {d3 b20, d5 b40},
seed 9100, 32 workers (contended wall), before = C2b HEAD 0875a951.

* **Where the time was** (perf, d3 gi55 / d5 gi53): the GREEDY subset walk (`SolveUncached`, the rollout
  leaf) was 56-81% of CPU, and inside it `SubsetPayableAsExecuted` -- the land-Aura payer check (a whole-turn
  prepay + a per-cast walk on two GameState copies) -- was ~46%. It answered "payable" on 6,239,728 of
  6,239,848 calls.
* **Fold 1 -- `MTG_HAND_COPY_CLASS_FOLD` (default ON, heurarm slot).** The Aura-copy class
  (`AppendFungibleAuraClasses`, digits non-increasing in group order) generalised to ANY hand-cast group
  (`AppendFungibleHandCopyClasses`) and to battlefield activation groups of identical non-mana, non-creature
  permanents (`AppendFungibleActivationClasses`: two Staffs of Domination). WHY THE EXISTING FOLDS MISSED
  THEM: `MTG_FOLD_HAND_CASTS`'s CONDITION 2 refuses any slot with >1 action (Stroke's (target, X), Sprawl's
  host x colour, Galvanic's partner, Harbinger's tutor, Living Wish), the canonical-prefix rule reaches the
  search walk only on Snow (`FoldSearchOdometerOptIn`), and the plan-signature dedup catches the copies only
  AFTER each was walked, priced and scored (the greedy walk has no dedup at all). Identity bar: card content
  (`HandCardContentHash`) + m1 side, option lists equal member for member under `ActionFoldSig` (+ the fields
  it omits); refuses mana-side/accelerant/splice/restrictor casts, free casts, Equipment spells and Magnetic
  Theft (MTG_EQUIP_HOST_FOLD's restricted-growth string reads those in card order -- claiming them failed
  test_equip_host_fold.cpp's mirror cases), a copy another candidate names by number, and a class whose group
  order disagrees with candidate order (the equiv_tag prefix keeps the earliest candidates).
* **Fold 2 -- `MTG_LAND_AURA_HOST_CLASS_FOLD` (default ON, heurarm slot).** (a) a land carrying only land
  Auras joins a (name, tapped, sorted (Aura, locked colour) multiset) class instead of always keeping its own
  slot; (b) `AppendLandAuraHostPrefix`: the odometer keeps only positions whose used hosts are a PREFIX of
  each class (relabelling identical lands is a symmetry of the whole option set; compatible with the copy
  class -- relabel first, then sort the copies' digits). The candidate fold still keeps `keep` = land Auras
  in hand members, so a SPREAD and a STACK both survive.
  **NOT built: one host per class.** C2b priced it at 3.7x, but (1) that pricing collapsed every candidate of
  an Aura group to one host, i.e. it also priced Sprawl's COLOUR axis away (unsound); (2) one-per-class forces
  two Auras to stack, which is a dominance only within one phase -- a stacked host's surplus floats away at a
  phase end where a spread host's stays on the battlefield (main 1 needs {G}{U}, main 2 needs {G}{B}). Lossy,
  so not an identity fold. (Moot for play anyway: the autonomous plan dedup already folds land-Aura hosts by
  name except on the karoo branch -- the prefix removes walk positions that dedup would discard.)
* **Lossless payer work.** (1) `MTG_SOLVE_AURA_PAYER_DEFER` (default ON): the greedy keeps ONE subset, so the
  land-Aura payer is asked only of a subset that would REPLACE the best (a rejected subset and a non-improving
  one are both discarded); stands down while the walk's mana-verdict cache is armed (its one non-local
  consumer). 6.24 M -> 231 k calls on d5 gi53. (2) `MTG_SOLVE_RESCUE_DEFER` (default ON): the same for the
  filter-board real-payment rescue of a flat-unpayable subset (3.95 M calls, 33% rescued) -- only when a filter
  makes it the expensive kind. (3) `MTG_PAY_PROBE_INPLACE` (default ON): after a successful prepay the
  per-cast walk runs on the prepay's own scratch board instead of copying it again (the only reads of the
  original board precede the first mutation when the self-funding taps are off; falls back otherwise).
  `MTG_PAY_PROBE_STATS=1` prints the probe counts.
* **Work removed** (single-threaded census, before -> after): d3 gi55 greedy visits 11.83 M -> 6.84 M, search
  walk 6.95 M -> 4.59 M, payment calls 53.9 M -> 7.9 M, wall 50.1 s -> 9.8 s; d5 gi53 visits 19.6 M -> 16.0 M,
  payment calls 55.5 M -> 4.3 M, wall 65.8 s -> 16.9 s. Distinct plans after dedup unchanged (89,595 on gi55).
* **Deck cost** (pooled, contended): d3 b20 total 545.1 -> 244.5 core-s (0.449x), mean 9.1 -> 4.1 s, p90 29.4
  -> 10.3 s, max 62.3 -> 22.8 s; d5 b40 895.2 -> 424.5 core-s (0.474x), mean 14.9 -> 7.1 s, p90 55.0 -> 19.2 s,
  max 85.4 -> 54.8 s. **Play byte-identical**: both digests unchanged (c8103c26751617f1, 52fbfad72e5b0a2c),
  avg 5.6500 / 5.5333, 0 games faster, 0 slower -- so no slower game to classify.
* **Smoke** (not accepted): 124/128; the three known moves carry their C2a digests (selesnya d3 a1937cded0ab6f58,
  d5 9bd0e14ec876faa9, critter d0 ccb5b47d5ef529c9); ONE new move, `melira_smoke_d0_s1001` (avg 6.3380
  unchanged, 14 games play-changed at the SAME win turn, 0 slower / 0 faster, digest 0f633c1012b4fb11). Traced
  (gi230, gi463): the first diverging greedy call scores the IDENTICAL set of distinct subsets (gi463: 38 = 38,
  best rank 4 both), the fold only halves the duplicates, and the pick differs among tied subsets -- the
  greedy's tie-break `mask |= 1 << j` overflows past 31 candidates (Melira's Chord variants), so which of
  several equal-rank plans wins depends on which positions exist. Tie-break churn on an isomorphic board, all
  on d0. Searched cases: no other digest moved.
* **Residual** (the tail, not the mean): d5 gi40 (54.8 s, 5.09 M units) is NODE-bound, not walk-bound -- three
  decisions (T4/T5, `root=2`) spend ~900 k units each against a 36,000-unit budget, 95% of it in `u_la_cand`
  (SolveWithLookahead's candidate loop): a budget-honouring question at that site, not branching. The rest
  of the per-node cost is the exact payment DFS (`TapForCostShared*`) at the search walk's payability checks
  (EnumeratePlans emits every feasible plan, so its checks cannot be deferred).
* **Measured, NOT adopted (USER's call, not needed for the line):** `MTG_SOLVE_CHARGE_W=16` (bill the greedy
  walk, Snow's setting) on top of the folds: d3 0.52x core-ms at avg 5.6500 (1 faster / 1 slower), d5 0.55x at
  5.5333 -> 5.6000 (2 faster / 4 slower).
* Tests: 4 new cases (Stroke copies + a soulbond-named copy refused; Sprawl + Wild Growth prefix -- stack AND
  spread kept, mirrors dropped; decorated-host classes, blue/blue folds and blue/green does not; two Staffs,
  enumeration + greedy pick identical up to relabelling). mtg-test 1486/1486, scenarios 225/225.

## Chunk C2b (2026-10-18) -- landed: the closed-form untap loop and its payoffs

* **Shape.** No new Action kind: the loop is `ActivateUntap` with `chosen_x = kUntapLoopToNeed` (1<<20), so
  every ActivateUntap site (family key, trailing pass, dedup sig, line hold, viewer) already carries it. ONE
  dispatch `ApplyUntapAction` (rollout trailing pass + AIEngine twin) -> `ApplyUntapLoop` (SpellEffects.h),
  which drives ONLY the C2a primitives: `ApplyActivatedUntap` one activation at a time, `SpendSelfUntapRepeats`
  (the Staff's gain-life bootstrap and its draws), `TapSourceIntoFloat`. Safety ceiling 256 steps (never a lever).
* **Sizing = the exact need** (`ComputeUntapLoopNeed`): the cheapest this-turn kill -- Stroke of Genius from hand
  at the opponent (X = minimal lethal), a drain source ON the battlefield (Scholar, lethal count), Ezuri's team
  pump to lethal on the creatures that can attack (the loop's Alchemist counted untapped); failing all, the
  Staff's draws to the first Stroke in the library (never the last card). The loop stops when the FLOAT pays it
  EXACTLY (`UntapLoopFloatPays`: the payer's own float drain, payment by payment -- K separate drains can strand a
  {B} that a summed CanPay passes). Seedcradle Witch: its own +3/+3 on an attacking target is a kill by itself
  (`UntapLoopPumpIterations`). Every K >= need is outcome-equivalent (same board + mana each iteration, no burn,
  passive opponent), so the stop is a dominance fold; one variant per (untapper, target), not per payoff (every
  win this turn scores the same).
* **Colour per tap** (`UntapPreTapColour`, scoped `g_untap_loop_owed`): a pip of THIS untap the float lacks
  first (the Witch's {G}{W} alternates), else the colour with the largest remaining deficit (U for Stroke, B for
  the drain, G for Ezuri), else C2a's rule. Null outside the loop: C2a byte-identical.
* **Staff bootstrap** (`UntapLoopBootOp`): an exact BFS over (P, X, Staff tapped, target tapped) with ops gain /
  tap / untap -> the first op of a shortest path to "target untapped, X > steady cost"; re-run off the REAL state
  before every op. Minimal P matches the hand formula (X = 4/3/2 -> P = 2/5/8; unit-pinned). The loop ends by
  untapping the Staff out of the float's SURPLUS colour (never the payoff's pip).
* **Funding the payoff: a breakpoint, not a credit.** When the loop stops, the new site-9 key
  `kActUntapLoopFunded` (need MV in bits 40+) arms the continuation, which prices the payoff against the real
  float. PlanOpensBreakpoint marks any loop plan. Provider recognition (`UntapActivationCounts` override ->
  `UntapLoopWorthOffering`): the bootstrap search with the HAND's lifegain projected onto X (so "cast Nourish,
  then loop" is one plan; autonomous only -- human play gets no projection, the Wirewood rule), and only when the
  board cannot already pay the need. A loop on an UNTAPPED mana creature is priced at zero plan mana (its first
  activation is paid by its own first tap). Enumerate-optimistic, the resolver validates.
* **THE KILL FOLD** (`MTG_UNTAP_LOOP_KILL_FOLD`, DEFAULT ON, =0 hatch) in `BpDeriveContinuationList`: when the
  float pays the need, the continuation list is the proven kill only (CollectActions under
  `g_untap_loop_kill_only`). Without it the continuations ran to 9k-360k plans (every hand subset affordable);
  seed 9113 d5 went 9 s / 148 MB -> 175 s / 4.7 GB and the first pooled probe tripped the RSS cap. A win this turn
  dominates every alternative in a goldfish: sound.
* **Payoff hooks.** Chunk B's `DrawXCandidates` now also offers the opponent the MAX-affordable X when the lethal X
  is not affordable (the two-turn split deck-out; dominated when lethal is affordable, so not added then).
  `AccomplishedAlchemistProvider::ManaSinkActivationCounts`: Scholar's lethal drain count; the Staff's draw-to-find
  depth. New `DecisionProvider::MainPhaseTeamPumpCounts` (generic empty; the engine asks only while FLOATING mana
  exists) -> Ezuri's main-phase ActivatePump mode 2 with the lethal K; `ApplyActivatePump` mode 2 now adds
  `team_pump_tough` too (0 for Sethron / Valiant Knight / Vito).
* **Not reachable THIS turn (engine structure, not a card deferral):** Scholar from hand / via Living Wish and
  Ezuri from hand -- each needs a second post-entry re-plan after its own cast, and site 9 opens once per apply.
  Their two-turn lines stay searched.
* **Proven-loop memo marker: not built** (post-loop states are folded to the kill; folding life_gained would be
  unsound while a need is still being funded off a specific X).
* **Viewer.** `untaploop=<source>[#n][@target]` (`UntapActSpec::loop`; plan JSON `"untap_loop": true` on the
  untapact verb; linebuild writes the verb; index.html lists it in the ordinary picker beside the single
  activation, never the board-click arm). DECISIONS.md row; audit manifest note (rides the four untapper params).
  New scenario key `life_gained_this_turn` (mid-turn frames). Scenarios `aa_loop_umbra_stroke` (autonomous:
  Nourish -> loop -> Stroke X = 40 T4) and `aa_human_untaploop_crab_umbra`; linebuild case.
* **Tests** (test_accomplished_alchemist.cpp): Umbra loop to Stroke's lethal X (need, exact stop, offered as one
  more variant, both worlds win); thresholds (Umbra X = 3 no loop / X = 4 loop; Staff minimal P 2/5/8; tapped
  target); Staff bootstrap from X = 3 with P = 5 (and no loop at P = 4), Staff left untapped, both worlds win;
  Galvanic pair -> Scholar drains (black floated, both worlds win); Ezuri main-phase pump (lethal K = 3, +9/+9,
  hook float-gated, both worlds); Witch alternating G/W, its own +3/+3 as the kill (6 iterations); Staff draws to
  a Stroke at depth 3 inside the loop then the kill (both worlds). mtg-test 1482/1482; scenarios 225/225;
  viewer linebuild 0 FAIL.
* **Deck measurement** (60 games seed 9100, pooled, contended wall): d3 b20 avg 6.3167 -> 5.6500 (24 faster,
  1 slower: gi30 T7 -> T8), d5 b40 6.2667 -> 5.5333 (27 faster, 1 slower: gi40 T6 -> T7); job ms d3 1,991,498 ->
  551,414, d5 2,804,299 -> 898,106; slowest game 1,161 s (gi54, T8) -> 82 s (d5 gi53, T5); C2a's unwon seed 9118
  now T5; still unwon gi24, gi48 (both arms). The two slower games recover at 10x budget (d3 gi30 b200 T6,
  d5 gi40 b400 T5; C2a HEAD T7 / T7 there). Smoke: every non-AA case byte-identical to C2a HEAD (selesnya /
  critter_d0 carry only their known earlier moves).
* **Remaining search cost (diagnosed, engine-wide, NOT changed):** per-node cost in the greedy subset walk
  (`SolveUncached`, d3 gi55: 11.8 M subset visits, 7.9 M payment calls, 250-800 payment solves per unit) and the
  EnumeratePlans odometer; MTG_BRANCH_SHAPE prices an interchangeable-copy fold at 7.26x odo and a one-per-class
  land-aura host fold at 3.7x (Utopia Sprawl x 18 Forests x its colour axis). Both move other decks' GT: USER.

## Chunk C2a (2026-10-18) -- landed: the four untappers, Galvanic's searched partner, the lifegain breakpoint

* **ONE action kind, ONE resolver.** `Action::Kind::ActivateUntap` (appended last): shape in `gy_exile_mode`
  (1 Crab Umbra `untap_enchanted_cost`, 2 Galvanic grant `soulbond_grants_untap_self_cost`, 3 Staff
  `untap_target_creature_cost`, 4 Seedcradle Witch `untap_pump_cost` +3/+3), `sac_source_id` = source,
  `sac_victim_id` = TARGET (searched: one variant per legal creature -- never the Wirewood Lodge auto-pick),
  `chosen_x` = K (`DecisionProvider::UntapActivationCounts`, generic 1..min(3, affordable)), `cost` = ONE
  activation (the ActivateBlink self-funding convention). `ApplyActivatedUntap` (SpellEffects.h) is called by
  the rollout's trailing pass AND AIEngine's twin; it re-reads the ability through the oracle each activation
  (`CollectCreatureUntapAbilities` / `UntapUnitCost` / `SoulbondGrantedUntapCost`), pays through the unified
  `TapForCostDirect`, stops at the first unpayable or no-op activation. An UNTAPPED target is a no-op unless it
  is a live mana creature, which is tapped for mana into the float first (dominant: no mana burn, untapped
  again at the end); colour = the cost's first coloured pip it makes, else G (mana-payment exemption).
  Enumeration drops only DOMINATED targets (untapped + nothing this main can tap it; opponent creatures) --
  the Witch keeps every own creature (the pump is never a no-op). Human play: only targets helpable now.
* **Staff of Domination.** `self_untap_cost {1}` is never an action: the resolver pays it ahead of each repeat
  whenever the Staff is tapped -- shape 3's K is "untap target, {1} untap Staff, untap target again ..." as ONE
  action (the one-activation-per-source family rule therefore costs nothing), and the Staff's {T}
  PermAbility modes (`tap_lifegain_flat 1` on TapLifegain, `tap_draw_cost {5}` on TapDraw) get a K axis whose
  repeats pay {1}+cost (`SpendSelfUntapRepeats`, both worlds; `ManaSinkActivationCounts` with max_affordable =
  1 + (total - cost)/(cost + 1)). Different Staff abilities in ONE main = the site-9 continuation (the Staff's
  family is shared). {4},{T} tap target = PROVISIONAL inert.
* **Galvanic Alchemist's partner is SEARCHED** on the ETB CHOICE AXIS (`CastCarriesEtbChoice` +
  `SoulbondPartnerPinCandidates`): one variant per own unpaired creature on the battlefield / creature card in
  hand + `kSoulbondDeclinePin`; the pin rides the cast's chosen_x into soulbond Trigger A in both worlds
  (`SoulbondPartnerPinScope` at the two cast sites); an illegal pin falls back to the provider pick. Excluded
  from the Plan::soulbond_decline twin. Silverblade Paladin (no grant) is untouched (unit-pinned; smoke
  byte-identical for knights/whiteknights).
* **THE LIFEGAIN BREAKPOINT = site 9, new route (not a new site number).** `CollectActivationKeys` emits
  `kActLifeYield` for an untapped, able-to-tap `mana_per_life_gained` source once >= 1 life was gained, with the
  YIELD in the key's bits 40+, so the delta rule fires exactly when a resolution grew it (or an untap re-offers
  it); `PlanOpensBreakpoint` gets a state-keyed lifegain clause (an Alchemist that can tap + a plan action that
  may gain life -- `PlanActionMayGainLife`, a superset -- or untaps). Both worlds already share the gate, the
  snapshot and the continuation list, so lockstep is by construction; the Khalni kActQuestLandSearch precedent.
  WHY NOT A NEW SITE NUMBER: site 9's gate is exactly "an option exists now that did not at plan start",
  evaluated after the trailing pass -- where Brindle Boar / Staff gain-life / Scholar drains land -- and a new
  number would need renumbering BpSiteMask / kBpSites / the executor's counted classes for no behavioural gain.
  Also `kActUntapper` (a cast untapper re-plans with its ability) + CardHasPostEntryActivation terms.
  Fixture `aa_nourish_then_alchemist_six` (Nourish -> Alchemist for 6 -> Stroke X=6 decks the opponent T4;
  FAILS with MTG_POST_ENTRY_BP=0). Scoped by VALIDITY (no tappable Alchemist -> no key, no fan-out), applies to
  budgeted and unbudgeted play alike.
* **Alchemist bracket note corrected**: Seedcradle Witch's {2}{G}{W} and blue untap costs + green payoffs DO ask
  a single-colour burst for two colours; sound because the exact payer refuses (unit-pinned: Alchemist alone
  cannot pay {2}{G}{W}, + a Forest can). The note also records that the same-main conversion is now reachable.
* Provider signature OR-s the untappers' params (routing test: any single untapper keeps the provider).
* Viewer: `untapact=<source>[#num][@target]` (blink shape, target chooser on the board) and `tapdraw=<source>`
  (the Staff's draw; `taplife=` already covered its gain); Galvanic = existing attach_host soulbond chooser.
  DECISIONS.md rows; audit manifest BOARD_ACTIVATIONS + INERT_PARAMS (`self_untap_cost`, `untap_pump_power/
  _tough`, `tap_lifegain_flat` -- sign-off owed). Scenarios aa_human_untapact_crab_umbra /
  aa_human_untapact_staff_target / aa_human_tapdraw_staff. `PermAbilityLabelFor`: the Staff's gain reads
  "gain 1 life" (Wellwisher's label unchanged).
* **Smoke**: everything byte-identical except the known critter_smoke_d0 4-digest move and SelesnyaLifegain
  (the lifegain breakpoint is a real play change there -- it runs Accomplished Alchemist): selesnya d3
  5.2000 -> 5.1800, d5 5.2533 -> 5.2267; per game FASTER d3 gi9/gi25/gi130 and d5 gi9/gi25 (T5 -> T4),
  SLOWER none, same-score play changes d3 gi22/gi98/gi132, d5 gi22. NOT accepted. The reference bench's
  selesnyalifegain search score moved 5.5 -> 5.4 (human 5.6) in the harness re-stamp (reverted, not accepted).
* **Probe** (20 games, seed 9100, d3 b20): avg 5.95, 1 unwon (seed 9118); real Alchemist taps 17, 12 for
  more than 1 mana (6 of them for 6; max 12). Slow games: gi5 30 s, gi9 83 s, gi17 90 s (1.7M units) --
  repro `--seed 9100+gi --game-index gi --games 1`; a perf question to settle before the suite-cost gate.

### Hand-off to C2b (closed-form loop + payoff sizing)

1. Drive the SAME primitives: `ApplyActivatedUntap` (any K, any shape), `SpendSelfUntapRepeats` (Staff {T}
   modes), the oracle `CollectCreatureUntapAbilities` + `UntapUnitCost` (one activation's effective cost incl.
   the Staff's {1} when tapped) + `SoulbondGrantedUntapCost`. Never a second resolver.
2. The count hooks are where the loop size goes: `UntapActivationCounts` (generic caps at 3) and
   `ManaSinkActivationCounts` for the Staff's gain/draw K. An `UntapLoop` action that sequences
   untap -> payoff in ONE action is still needed: today a plan holds one action per SOURCE family, the
   Staff's gain-life/untap/draw share one family, and the K-loop's float is only spendable by a later
   site-9 continuation (once per apply) -- so "untap K times THEN Stroke for X" in one main needs either the
   loop action to size and cast the finisher (the SpendSurplusOnExile shape) or the continuation to see the
   float (it does: the continuation's pool includes floating mana).
3. Float COLOUR: the pretap colour rule (cost's first pip, else G) is fixed. A blue-cost untapper (Umbra,
   Galvanic) floats blue -- right for Stroke, wrong for Ezuri's {G}{G}{G}; the Staff's generic {3} floats
   green -- wrong for Stroke's {U}. The closed form must choose the colour per payoff (colour of the LAST
   tap only matters, each tap is one colour).
4. No ritual_float credit: the enumerator prices one activation and does not credit the mana a K-loop
   makes (the MTG_EDF_BLINK_CREDIT precedent measured neutral). The lifegain-breakpoint continuation sees the
   post-loop pool, which is how a payoff gets sized today.
5. A Staff that is TAPPED at enumeration still cannot offer its gain-life / draw PermAbility modes
   (PermAbilitySourceLive wants it untapped); only shape 3 handles a tapped Staff. Wire the {1} prefix into
   the PermAbility enumeration if the loop needs "gain-life from a tapped Staff".
6. Loop thresholds unchanged from alchemist-untap-loop.md: Umbra / Galvanic net X-3 (X >= 4), Witch X-4
   needing a second colour source, Staff X-4 per {1}+{3}.

## Chunk C1 (2026-10-18) -- landed: deck provider, Utopia Sprawl, Trufflesnout

* **Provider (Stage 4a).** Before: `SelesnyaLifegain` (Accomplished Alchemist's `mana_per_life_gained`).
  After: `AccomplishedAlchemistProvider : SelesnyaLifegainProvider` (derived from what it routed to, so
  play-neutral by construction), signature `draw_target_player` (Stroke of Genius) OR `storm` (Weather the
  Storm), routed ABOVE `selesnya`. Certificate NotAssessed (what a bound must price is in the class).
  **INHERITED judgement hooks the USER must see:** SelesnyaLifegain's USER-settled cleanup-discard buckets
  (`CleanupDiscardCandidates` + `CleanupDiscardShedStable`, `MTG_SELESNYA_BUCKET_DISCARD` default ON) --
  settled for ANOTHER deck's cards; this list mostly falls into no role. Kept (not changed) until this
  deck's own discard rule is decided. Everything else is DeckProvider/Generic. `TutorSearchWidth` = 6
  (inherited) >= the five Elf names. ONE override: `EtbChoiceCandidates` restricts Sprawl's colour to
  G/U/W/B (a restriction; still searched).
* **ETB CHOICE AXIS** (CollectActions post-pass, decision space only, never human play): a cast whose
  as-enters/ETB choice is searched is replaced by one variant per `DecisionProvider::EtbChoiceCandidates`
  value, carried as `chosen_x >= 1` down the existing etb_kx channel (the Felidar Guardian vehicle):
  rollout apply_one -> FireOwnEtbTriggers; executor CastSpellFromHand records the positive chosen_x on the
  stack entry -> EnterBattlefield -> FireOwnEtbTriggers. chosen_x already rides every plan signature;
  the autonomous name dedup keys it as `#EC`. Rollout leaves keep the unpinned base (provider default).
* **Utopia Sprawl.** `land_aura_host_subtype` enforced by `LandAuraHostOk` in LegalEnchantTargets,
  ResolveEnchantTarget (target + fallback, `land_pp`), AuraCouldEnchant, the in-loop land-aura deploy, the
  same-turn karoo host (`KaroosHostAdmitted`), and a provider-narrowed host list is intersected with the
  legal set. Colour: `etb_choose_color` on a land aura reads `land_aura_produces` as its menu; credited via
  `LandAuraProducesFor` (pool credit, LandAuraColorMask, BoardCanProduceColor, the payability-cache key);
  `EffectiveProducesFor` ignores land auras (not a source of their own).
* **Trufflesnout.** Modal ETB in FireOwnEtbTriggers (PutPlusCounters / one GainLife event).
* Viewer: Sprawl = existing `choose_color` (full five-colour menu); Trufflesnout = `attack_mode` generalised
  (emitter adds `"trigger":"etb"` + the card's option texts; Tectonic Giant byte-identical; GUI sub-heading).
  DECISIONS.md rows; audit manifest: `etb_modal_self_counters` -> attack_mode, `etb_modal_lifegain` /
  `land_aura_host_subtype` -> INERT_PARAMS (sign-off owed).

## Chunk B (2026-10-18) -- landed: Stroke of Genius

* `draw_target_player` (bool, new) + `targeting: player`. The TARGET PLAYER and X are both searched cast
  variants (`Action::draw_target` 0 self / 1 opponent; CastRiders -> rollout record + executor Player target).
  Every signature folds both (`#DX<x>/<t>` in the autonomous dedup; plan hash / fingerprint / PlansEqual /
  human sig fold draw_target only when nonzero, so no other deck's key moves).
* `opponentdeck::ForcedDraw(state, n)`: decked when the draw EMPTIES their library (n == L) or over-asks it
  (n > L, the CR 704.5b loss). n == L is flagged too, on TakeFromTop's argument: their simulated end-of-turn
  draw then fails on THIS turn, so the reported win turn is identical and only the within-turn win checks
  differ (they now see it). Minimal lethal X = max(1, L) (`ForcedDrawLethalX`). X = 0 decks nobody.
* `PerformDrawX` (SpellEffects.h) is the ONE resolver for both worlds; a self over-draw sets
  `player_lost_on_draw` (the old ResolveDrawX drew what was left and carried on).
* `DecisionProvider::DrawXCandidates` (new hook, default in DecisionProviders.cpp): opponent = the minimal
  lethal X when affordable, else nothing; self = {cap, ceil(cap/2), 1, 2, 3} with cap = min(max, own
  library); MTG_UNPRUNED=xspell opens 1..max; human play gets 0..max for both targets.
  **USER QUESTION (surfaced, default taken):** a non-lethal forced draw is a no-op THIS turn but not across
  turns (4 Strokes + a large finite pool can split a deck-out over two turns). Default = prune it (the brief's
  "dominated" reading); MTG_UNPRUNED=xspell is the A/B arm.
* Self-draw X is a PLAIN CANTRIP for the breakpoint machinery (site 3) via `IsSelfDrawXCast` -- the rollout's
  DrawSpell branch, IsPlainCantrip, BpSiteMask, the executor's equip_bp_truncates and cand_draws all read it;
  the opponent variant opens nothing. `ParamKeyedDrawClass` now claims DrawX (so site 10 never double-arms).
* Viewer: a `main_phase` plan variant labelled "(X=n) -> you draw" / "-> opponent draws", plan JSON
  `"draw_target"`; DECISIONS.md row + `draw_target_player` -> main_phase in audit_viewer_decisions.py.
  No resolution-time board click (the target fixes X's meaning, so it is chosen with the plan).
* Braingeyser / Unite the Coalition notes reworded to name the limitation (FiveColour deals no opponent
  library), param NOT added.
* Mana: the AA fixture pays {6}{U} with the Alchemist's one blue through the exact payer. The pre-combat
  pool credits the Alchemist exactly 1 (no lifegain-growth credit), so a Nourish -> Alchemist -> big-X Stroke
  line in ONE main is not sized by the enumerator yet -- chunk C.

## Chunk A (2026-10-18) -- landed

Nourish, Eternal Witness, Elvish Harbinger, Ezuri (fair pump), Scholar of Athreos, Brindle Boar,
Weather the Storm. New params: `drain_each_opponent`, `sac_outlet_lifegain`, `storm` (tests:
test/unit/test_accomplished_alchemist.cpp + F4c in test_attack_projection.cpp). Also:
`deck_reads_lifegain_in_play` now scans a wish-reachable sideboard; ProjectPlanWalk counts the plan's
casts (storm); stale "MTG_SL_SECOND_MAIN default ON" notes corrected (Alchemist, Feed the Clan,
Bogbeast, HeuristicArm.h) -- the default stays OFF. Separate commit: a self-only sac outlet now bakes
itself as the victim (was a silent no-op) -- moves critter_smoke_d0 4 digests, win turns unchanged;
smoke GT for that NOT accepted yet. ROUTING measured: the deck rides SelesnyaLifegainProvider today
(pinned in test_pirates_provider.cpp until chunk C's own provider).

## Open risks

* ROUTING: mana_per_life_gained + cast_lifegain_ferocious are terms in the selesnya signature (DecisionProviders.cpp ~21151) -> deck likely rides SelesnyaLifegainProvider. Stage 4a must give it its own provider derived from what it routes to today.

* ROUTING (2): Arbor Elf's mana_requires_land_subtype sets the stompy signature (checked before anti) -> may ride StompyProvider. tutor_to_top -> anti (TutorSearchWidth 2). Own provider, width >= 5.
* Alchemist bracket note 'no cost asks for two different colours' is FALSE in this deck (Seedcradle Witch {2}{G}{W}); also blue (Umbra/Galvanic/Stroke), black (Scholar).
* Utopia Sprawl colour + Galvanic soulbond partner are provider picks today = greedy-in-window concern; prefer searched axes.
* SEQUENCING (central): Alchemist's bracket note says DeckUsesSecondMain is ON for Alchemist decks, but MTG_SL_SECOND_MAIN went DEFAULT OFF 2026-09-30 (measured on SelesnyaLifegain, GoldFishRunner.cpp:234). Sac-outlet / tap-lifegain activations are TRAILING (after casts) and Alchemist is credited 1 pre-gain -> same-main lifegain->Alchemist conversion likely unreachable. Architecture design must settle this; the m2 lever for THIS deck is a USER-visible decision (measure both).

## Viewer decisions (2c-ter)

## Verification (Stage 5)

Run 2026-10-18 on c96b7f46 (play-identical to 251c426e), profile = the freshly generated
`AccomplishedAlchemist.profile.json` (no value_play, so play settings = the built-in d5 b20). Seed base
70001 (disjoint from the suite's 1001..4004 and the chunk measurements' 9100). Scratch:
`/workspaces/MagicDeckTester/build/scratch/aa/s5/`. Single-game repro for batch gi=N:
`--seed $((70001+N)) --game-index N --games 1`.

**5a -- mismatch harnesses: FINDINGS (2 fd-diverge, 0 nonconv).**
* `MTG_FLAG_NONCONV` over 800 games (d0, d3 b20, d5 b20, d5 b40 x 200): **0 [nonconv]**.
* `MTG_FD_ORACLE` d5 b40 x 200: **2 [fd-diverge]**, both `leaf_est=none` (simulated, not a leaf guess), both
  the same class -- **a LOCKSTEP defect between breakpoint site 9 (post-entry activation) and a self-targeted
  Stroke of Genius (site 3, the partitioned plain-cantrip shape)**. Not a card bug: the cards resolve
  identically in both worlds; the executor applies the scored continuation in a different ORDER.
  * gi120 (seed 70121, predicted T5 at T4, realised T6). Rollout T5: Staff + Nourish, site 9 picks candidate 3
    = `Stroke(X=1) + Staff#2 + Staff loop`; the self-Stroke truncates that continuation (partition) and its
    site-3 continuation runs the loop -> Staff draws -> Stroke X=51 at the opponent. Executor (site-9 twin,
    `AIEngine.cpp` ~7316): `pe_cast` casts Stroke X=1 AND the second Staff (no partition truncation inside the
    site-9 twin), which spends the Alchemist's float, so the loop never fires and the recorded Stroke X=51
    has no card. Trace: `MTG_SITE9_TRACE=5 MTG_FD_TRACE=1 MTG_BP_TRACE=1`.
  * gi54 (seed 70055, predicted T6 at T6, realised T7). Committed T6 plan `Witch, Stroke X=2 (self), ...`
    with bp_choice 1. Rollout: after the Stroke, site 9 fires FIRST (idx 0: Feed the Clan + the Witch's
    untap of the Alchemist), then the Stroke's site-3 continuation (idx 1: Feed, Umbra, Umbra -> loop, X=14
    float). Executor: the Stroke's recorded continuation is replayed at the cast (`[replay-bp]` right after
    it), which sets `bp_replayed`, so the site-9 twin stands down (`!bp_replayed`) and the Witch untap never
    happens -- the Alchemist stays tapped, the loop is unfunded.
  * WHERE (both games): the rollout's plain-cantrip partition is DEFER + TRUNCATE (`TurnSolver.cpp` ~47905:
    `deferred_cantrip_resolve` + `bp_truncate`; the continuation re-solves after the trailing passes and
    after site 9), while the executor, for a committed plan, REPLAYS the partitioned cast's records AT THE
    CAST (`AIEngine.cpp` ~7171, `equip_bp_truncates` -> `replay_segment()`), which sets `bp_replayed` and so
    stands the site-9 twin down (gi54); and the site-9 twin's own `pe_cast` loop has no partition
    truncation at all (gi120). Engine-wide (any deck whose committed plan holds a self-partitioning
    cantrip AND opens site 9), so a fix moves other decks' play -- convergence loop, USER-visible.
  * Folds ruled out: both reproduce with `MTG_HAND_COPY_CLASS_FOLD=0` and/or `MTG_LAND_AURA_HOST_CLASS_FOLD=0`.
  * STATUS: RESOLVED c05eed09 (chunk E1) -- the executor's clean-set loop now defers every deferred-class
    partition cast to the end-of-main catch-all (acq_pending for the no-commit fallback), and the site-9 twin
    truncates its continuation after a partitioning cast. FD oracle d5 b40 x200 seed 70001: 0 [fd-diverge].
* Note: `MTG_FULL_DEPTH` is no longer read by this binary (startup warning); `MTG_FD_ORACLE` alone arms the
  oracle. The skill's 5a recipe is stale on that point.

**5b -- multi-depth sanity: monotone in depth, plausible; one budget non-monotonicity.** 200 games each:

| cell | avg | T4 | T5 | T6 | T7 | T8 | unwon |
|---|---|---|---|---|---|---|---|
| d0 | 8.395 | 1 | 5 | 16 | 17 | 14 | 147 |
| d3 b20 | 5.820 | 20 | 81 | 50 | 26 | 10 | 13 |
| d5 b20 (play) | 5.810 | 20 | 81 | 50 | 27 | 10 | 12 |
| d5 b40 (suite) | 5.805 | 20 | 83 | 50 | 23 | 11 | 13 |

* d3 -> d5 b20: 2 faster, 0 slower. d5 b20 -> b40: 11 faster, **8 slower** (gi45 6->7, 47 6->8, 54 6->7,
  83 4->5, 146 5->6, 149 7->unwon, 152 5->6, 198 4->5). Median kill T5, 10% T4: plausible for the combo.
* **d0 is 147/200 unwon** -- the greedy leaf policy almost never assembles the loop. Every searched leaf
  finishes with that policy, so 5i (d0-vs-search divergence digest) is the obvious next lever; not run here.
* The 13 d5-b40 unwon games, read: 9 never had a castable Alchemist (gi7, 39 stuck on two lands; gi51, 78,
  139, 172, 184 never drew one; gi127 three lands; gi160 Harbinger only on T7 -- an Alchemist from it could
  not tap before T9); gi89, 15, 187, 149 recover with budget (below). No unexplained unwon game.

**5c -- budget starvation: thresholds noted; ONE game is a narrowing loss, not starvation.**
* Recovered by budget (d5): gi89 unwon -> T5 at b400; gi15 unwon -> T8 (b400); gi187 unwon -> T8 (b400);
  gi54 7 -> 6, gi146 6 -> 5, gi198 5 -> 4 at b400; gi45 7 -> 6 and gi83 5 -> 5 only at b2000; gi47 8 -> 6 at
  b2000 (19.7 M units, 772 s); gi149 unwon -> T8 (b400) -> T7 (b2000). So b40 is starving a few-percent tail;
  the deck wants ~b400 at d5 to settle, and the d5 b20->b40 slower games are budget noise (each restored or
  bettered at b400/b2000). gi83 is the oddest: b20 T4, b40 T5, b400 T6, b2000 T5.
* **gi152 is NOT starvation**: d5 b400 and b2000 spend the SAME units (681,327) and both realise T6, while
  d3 b20 / d5 b20 realise T5. `MTG_UNPRUNED=1` at d5 b400 -> T5; gate bisection (`--gate-probe`, then each
  live gate alone): **`MTG_UNPRUNE=searchorder` -> T5 and `MTG_UNPRUNE=xspell` -> T5**, every other live gate
  T6. With search ordering the T2 play is `Staff of Domination + Utopia Sprawl` (Sprawl on the Forest Arbor
  Elf then untaps, funding the Staff) instead of `Utopia Sprawl + Nourish` -- a line the canonical cast order
  does not reach. A 5e finding: a narrowing (cast order, and/or Stroke's X menu) costs a turn at full budget.
  Cast order is the USER's decision; reported, not changed.

**5c2 -- horizon-honest tie-break: KEEP THE DEFAULT (ON).** `leaf_tiebreak_check.py` at play settings
(d5 b20), seeds 1000000/1100000/1200000/1300000, 3,998 paired games (both arms; the base arm from the
script's own run, stopped once it was clear the default 24,000-game sizing would take ~6 h at this deck's
cost -- the binding rate is 20%, so 4 blocks are already decisive; the leaf arm from the identical
manifest rows; one straggler, s1100000 gi821, dropped from both arms): **net -78 turns (377 worse / 427
better), binding 804 / 3,998 = 20.1%**. Halves: A +7 (neutral), B -85, so the gain is real but modest
(-0.02 turns/game) and the per-game churn is large -- the expected shape for a stored-value combo deck,
where opponent life at the horizon prices the build-up at zero. No opt-out. Paired data:
`build/scratch/aa/s5/tb_{base,leaf}.out`.

**5h -- viewer decision surface: PASS.** `audit_viewer_decisions.py ... --sideboard`: expected
{attach_host, attack_mode, choose_color, dig}; the 40-game sweep surfaced attach_host, choose_color,
tutor_etb; the two UNVERIFIED (Eternal Witness `dig`, Trufflesnout `attack_mode`) were VERIFIED by the
targeted seed search. No HARD MISS, no SELF-GUARD, no DRIVER failure. Oracle advisories (Staff/Witch
'target', Trufflesnout 'modal') are already modelled or carry inert notes. Hand-driven claude-play check
(seed 70121 gi 120, T4): Stroke of Genius is offered as every (target, X) pair -- self and opponent, X = 0
and 1 (the affordable max) -- in `main_phase` plans, i.e. the human menu is unpruned.

**verify_deck.py:** coverage PASS (21/21 full); card_costs FAIL = did-not-run (7 cards 429'd -- none of
them in this deck; every AA card compared clean); card_fields SKIP (snapshot incomplete); regression_tiers
FAIL + suite FAIL (not yet in test/regression_cases.sh -- expected, Stage 5j); viewer PASS; viewer_wiring
was FAIL ("choose_color not in registry") -> a verify_deck.py parser defect, FIXED in c96b7f46, now PASS;
mismatch PASS (its own 2 x 60 games at d5 b20, seeds 7001/8001 -- the sample did not hit the site-9 class);
play_invariants PASS (8 games / 236 decisions); claude_sweep SKIP (un-run).

**`--game-log-dir` on a non-batch run (C1's "12 games all unwon, avg 9.0"):** not that flag. The single-run
parser silently IGNORES unknown arguments, so `--game-log-dir D` was a no-op (results byte-identical with and
without it; no logs written). The "all unwon, avg 9.0" signature reproduces exactly by launching `mtg` from a
directory without `src/cards/data/cards.json`: the CWD-relative default was skipped silently and the deck
played as empty definitions. FIXED in c96b7f46 (missing database = exit 1 in single and batch; ignored
arguments warned), with `test/mtg_cli_selftest.py`. Related, NOT changed: `ApplyHeuristicDefaults` resolves
`heuristic_defaults.env` by walking up from the EXECUTABLE first, and a worktree's `build/` is a symlink into
the primary's volume, so a worktree binary reads the PRIMARY checkout's defaults file.

## Open questions surfaced to USER (defaults taken)

* Stroke at opponent offered only at minimal lethal X (chunk B). Lossy vs completeness (a 2-turn split deck-out). Default taken: chunk C2 adds a max-affordable opponent variant too; MTG_UNPRUNED=xspell is the full arm.
* Second main for this deck: global default OFF kept; m2 arm to be measured.
* Weather the Storm cast order: generic kept; storm-last proposed.
* Utopia Sprawl colour: made SEARCHED (chunk C1), provider-restricted to G/U/W/B. Galvanic partner: C2.
* Inherited SelesnyaLifegain cleanup-discard buckets on AccomplishedAlchemistProvider (chunk C1): kept
  for play-neutrality; this deck's own discard rule is the USER's to settle.
* critter_smoke_d0_s1001 moves 4 digests (avg unchanged) from the chunk-A self-only sac-outlet victim fix -> accept at landing.

* Chunk C2a INERT_PARAMS classification (`self_untap_cost`, `untap_pump_power`, `untap_pump_tough`,
  `tap_lifegain_flat` -- each the payload of a verb, never a separate choice): sign-off owed.
* SelesnyaLifegain play moved by the lifegain breakpoint (5 games faster, 0 slower in smoke) -- GT not
  accepted; acceptance is the USER's call through the normal flow.
* Chunk C2b: (1) the continuation KILL FOLD (MTG_UNTAP_LOOP_KILL_FOLD default ON) -- a sound dominance fold
  (a proven win this turn), but it is what made the deck tractable; (2) Stroke at the opponent now also offers
  the max-affordable X when lethal is unaffordable (your completeness point from chunk B, taken as the default);
  (3) two engine-wide folds priced but NOT built -- interchangeable hand copies (7.26x odo on the slow games) and
  one-per-class land-aura hosts (3.7x) -- they move every deck's GT, so they are yours to call; (4) Scholar via
  Living Wish / Ezuri from hand cannot kill the SAME turn as the loop (site 9 opens once per apply) -- an engine
  structure question, not a card deferral.

* Chunk P: (1) the three folds and the two payer deferrals are DEFAULT ON engine-wide (sound identity
  folds / lossless reorders; the collapse doctrine). Note the hand-copy class also folds ONE-option copies on
  the SEARCH walk for every deck -- the thing `FoldSearchOdometerOptIn` kept per deck (Snow only) after the
  2026-10-01 fleet screen; smoke shows no searched digest moved. (2) `melira_smoke_d0_s1001` moves 14 d0 games
  at the same win turn (tie-break churn, traced) -> accept through the normal flow. (3) a latent defect worth a
  decision: the greedy tie-break `mask |= 1 << j` in SolveUncached is undefined past 31 candidates, so ties
  there are arbitrary on Chord/Pod-sized menus; fixing it moves GT wherever it bites. (4) the one-host-per-class
  land-Aura fold was NOT built (a dominance that fails across a phase boundary -- lossy); C2b's 3.7x pricing of
  it was unsound. (5) `MTG_SOLVE_CHARGE_W=16` measured (0.55x, d5 +0.067 turns), not adopted -- not needed.

* **Suite entry (2026-10-19) -- USER decision owed, nothing adopted.** Recommendation: `bottom_eval_budget_ms 2 +
  bottom_eval_topk 2` (arm h) as the pre-keep-table cost setting -- net -0.027 / -0.013 turns on 600 held-out
  games at d5 b20 / d3 b10, ~35% cheaper, but +0.015 at d5 on the 200-game set with 3 games that persist at
  10x, and still over the 8,907 ms line on the held-out sample (9.3 s/game) because of unmetered per-node
  enumeration cost (see "Suite entry attempt"). So the deck needs BOTH (or the mulligan stage first, which
  needs `MTG_ALLOW_UNTESTED_DECK=1`, a USER call) before it can enter the suite.
* Chunk E1: (1) `MTG_SITE9_REOPEN` is DEFAULT ON engine-wide (site 9 re-opens after an applied continuation,
  targeted occurrences only); smoke moved no case. (2) `BurstPendingActColour` changes which colour a burst
  source floats when a plan's trailing activation owes a pip (a payment choice; AA-only in practice).
  (3) "non-lethal Stroke + loop" plans dropped as dominated. (4) Budget: no leak -- the deck's cost is
  bottom-trial games + the 25x pass ceiling; which lever (mulligan stage first, profile bottom_eval_*,
  kOverrunBudgetMult, MTG_SOLVE_CHARGE_W, a start-gate estimate) is the USER's. (5) Per-game cost at seed
  70001 d5 b40 is ~2.4x the 3x line before the mulligan stage -- the suite entry needs a decision on that.
* Chunk P3: (1) the bottom-trial early stop (e6f38737) is ON engine-wide: decision-exact by construction, byte-
  identical on smoke and the cells; its one theoretical channel is the shared bottoming TT (a cross-hand key
  collision or the TT store cap binding). (2) The continuation-cache eviction (clear-all on the 3.2 GB pool /
  8,192-entry cap) costs the g60 tail class 17%; a two-generation eviction would recover most of it in the
  same memory, but cache history moves work units on some decks, so it is a play change -- USER's call. The
  gi286 class is not a cache problem: its continuation lists are GB-scale (an unbounded cache hit 33 GB in
  68 s), i.e. the Living Wish variant width inside continuation derivations.

* **Seedcradle Witch as a win condition (2026-10-21) -- landed DEFAULT ON (the USER's decision).** Adopt
  `MTG_AA_HARB_WITCH_FALLBACK_FIRST` (PROPOSED, OFF: the Witch stays first in the Harbinger FALLBACK, where nothing is
  lacking)? With it the rule is -0.005 / -0.005 / -0.012 vs aa-land with zero slower games in 1,000 and the d0 canary
  back at 7.2210; without it, -0.005 / +0.005 / -0.010 and d0 +0.024. Five readings taken (listed in the section):
  a held Witch fills both buckets; "last among wincons" includes the fallback; a Witch in play makes a Stroke /
  Scholar / Ezuri in hand a discard spare (ANSWERED: the mana cap, next bullet); the wish rule counts the Witch but
  still not Ezuri as a held wincon; Combo Off untouched.
* **Discard mana cap (2026-10-21) -- landed DEFAULT ON (the USER's rule).** One reading is the coordinator's, stated to
  the USER: "can afford the tempo loss" = board mana >= 4 (`kAaElfTempoMana`) before Arbor Elf moves ahead of an extra
  land Aura. Also taken: the second tier is one card per role (a third Stroke is "everything else"); spare Wishes
  are "everything else" too (after the other spares, before the second tier); the cap counts the board's projected
  mana as the wish census does (dorks 1, the Alchemist's X not counted).
* **Elvish Harbinger rule (2026-10-20): it measures WORSE; adopt the two amendments?** (2026-10-21: the USER adopted
  `MTG_AA_HARB_WITCH_WIN` -- now DEFAULT ON, the Witch ranked after Ezuri for a lacking Wincon; `MTG_AA_HARB_ALCH_DEFAULT`
  is still open.) The USER's rule is +0.005 ..
  +0.035 turns worse on all four cells (11-17% cheaper). Root cause: the Alchemist auto-pick (74 slower vs 52 faster
  games) and the Witch-as-wincon (12 vs 3); never-Harbinger is neutral. `MTG_AA_HARB_ALCH_DEFAULT` (Alchemist first
  but not alone) + `MTG_AA_HARB_WITCH_WIN` (Witch offered for a lacking wincon) together measure -0.015 / -0.035 /
  -0.035 / -0.052 vs the control. Both default OFF until the USER decides. Readings taken: a held Witch fills Untap
  whatever the lifegain total; Ezuri counts as a held wincon here (it does not for the wish rule); the fallback
  excludes a second Alchemist.
* **Living Wish buckets (2026-10-20), the readings taken (defaults; say if any is wrong):** (1) mana threshold 4
  of the USER's "4-5", counting lands + dorks + land-Aura bonuses in play and EVERY land in hand (not dorks /
  Auras in hand); (2) the Staff's gain-1 is not lifegain; (3) the Witch counts as an untapper when the held
  lifegain SUMS to >= 5 (RETIRED 2026-10-21 by the USER: the Witch fills Untap at any gain, and Wincon too); (4) Scholar also counts as a held wincon (so a second is not wished), Ezuri does NOT
  count as a wincon; (5) the Alchemist is not a blue source for the Island test; (6) order = the USER's bucket
  order (Mana first), which moved the rollout's default pick off the Alchemist. Measured better on every cell.
  Two amendments measured and left OFF: `MTG_AA_WISH_TRUFFLE` (mixed: -0.017 / -0.038 held-out, +0.100 on the
  200-game d3 set) and `MTG_AA_WISH_BLUE` (neutral). Four persistent losses come from the rule excluding a
  SECOND Alchemist -- the USER's call whether "good whenever you don't have one" should read "... in play".

* **Discard buckets (2026-10-20), the readings taken (defaults; say if any is wrong):** (1) within-bucket orders
  LIFEGAIN Nourish > Feed > Weather > Trufflesnout > Boar, UNTAP Crab Umbra > Staff > Galvanic > Witch, WINCON
  Stroke > Staff > Scholar > Ezuri; (2) with Umbra AND Stroke in hand the Staff is a spare (it fills both only when
  it is a bucket's best) -- the measured alternative `MTG_AA_DISCARD_STAFF_FIRST` repairs the 200-game seeds but
  is +0.002 / +0.015 on held-out, left OFF; (3) Ezuri counts as WINCON here (the wish rule does not count it);
  (4) a Brindle Boar or any untapper (Witch included) in play fills its bucket; (5) Wishes fill only non-mana
  gaps; (6) lands strictly before dorks / land Auras in the MANA remainder; (7) one LIFEGAIN keep -- the one
  rule-worse label (seed 930213) wanted both Feed and Nourish over a third Forest.

* **Combo Off (2026-10-20) -- what to decide / argue with (defaults taken, all ON as you asked):**
  (1) the rule table above, row by row -- in particular whether LIFE should also count a Living Wish for a lifegain
  creature (the sweep's 2 c's) and whether the Witch rows should stay offered at X = 5 (26 of the 27 d's: the
  loop's two-colour untap starves off a one-colour Alchemist -- arithmetic says the loop exists, our executor does
  not run it; the fix is the loop's colour plan / the payer's hybrid pip, and narrowing the display is your call);
  (2) the route is DEFAULT ON (`MTG_AA_COMBO_ROUTE`) and so are the search's two queries for this deck
  (`ComboOffSearchDefault`), with EDF's defaults unchanged; (3) `MTG_UNTAP_LOOP_SURPLUS_GENERIC` (default ON) is a
  mana-payment change to the closed-form loop itself, i.e. it moves the search's own loops too -- measured alone it
  is +0.000 / +0.005 / +0.005 / -0.003 turns (see the table), needed for the Staff -> Scholar finish; (4) pump
  finishers (Ezuri, the Witch's +3/+3) are not combo-off rows because they win in combat; (5) the persistent
  slower games (below) include route NO-OPs where the executor's state at the route differs from the one the
  search proved it on (a Living Wish resolving to a different card / a payment that tapped the Alchemist) -- an
  fd-divergence surfacing through the route; the line plays on, a turn later.

## Claude-play sweep
- commit: `251c426e`
- seeds: 80001-80020 (claude-play deals by seed only) games: 20
- flags: 9 unresolved
- Result: Claude faster than the search in 3 (80017 T4 vs T6, 80019 T6 vs T8, 80020 T4 vs T5), slower in 5 (deal mismatch / Claude tempo), tied 10, both unwon 2. Raw: build/scratch/aa/sweep_results.jsonl.
- ENGINE flags -- RESOLVED in fix chunk E1 (see "Chunk E1" below for mechanisms and measurements):
  - RESOLVED bb2ab20c -- Arbor Elf credited a flat +1 G; untapping an aura-enchanted Forest yields 2-3 (80020 now T4, was T5; the T2 Alchemist is offered and payable, the search prefers Wild Growth + Feed + Wish there). Card note rewritten.
  - RESOLVED 700c5041 -- one mid-turn re-plan per plan application: site 9 now RE-OPENS after an applied continuation (MTG_SITE9_REOPEN); chained loop payoffs Scholar-from-hand / Living Wish -> Scholar / Ezuri-from-hand (80017 now T5, was T6; Claude T4). 80018 ("second loop after a Staff draw finds Stroke"): autonomous play already draws INSIDE the loop and re-funds for the drawn Stroke (C2b); the re-open now also covers a draw made by a continuation. The game stays T8 at d5 b20 (search T8 vs Claude T10 in the sweep; not re-diagnosed).
  - RESOLVED b13de112 -- loop colour: a burst tap for a generic pip now floats the colour the plan's pending activation needs (80019's Staff + Umbra untap).
  - RESOLVED b13de112 -- zero-iteration loop offers (80010): sound option kept -- an unreachable loop does nothing (test-pinned), so it scores identical to no-loop; "Stroke small X + LOOP" plans (80013, 80018) dropped as dominated.
  - RESOLVED b13de112 -- Living Wish singleton (80012): autonomous resolution already ranks the live sideboard; the human menu drops a plan naming one sideboard copy twice.
  - VERIFIED 700c5041 (test) -- Staff {5},{T} draw IS offered standalone whenever 5 mana is available (80005); not a defect.
  - DIAGNOSED, not a leak (see Chunk E1 "Budget") -- budget overrun at d5 b200 (80012 402 s, 80019 33 min).
- VIEWER (not chunk E1 -- the viewer worktree):
  - RESOLVED (chunk E2, x_value decision) -- after a loop the human menu dropped Stroke of Genius entirely (80016).
  - RESOLVED (chunk E2) -- life_gained_this_turn / opponent library / yield / P/T / activation_cost exposed; Ezuri pump = firebreathe decision under --firebreathe-prompt (the viewer always passes it; the sweep agents did not).
  - (E1 note) human-play: no Forest picker for Arbor Elf's untap (the default re-taps the best Forest); a human after a loop who draws a Stroke with the Staff gets the loop re-offered only if UntapLoopWorthOffering's need test passes against the live float (80018's human report).
- DISMISSED: Harbinger decline leaves library unshuffled (declining = not searching; correct). Trufflesnout via attack_mode (deliberate reuse). dropped_casts on unpayable cast orders (existing enumerate-optimistic pattern, disclosed).

## Chunk E2 (2026-10-19) -- viewer flags from the claude-play sweep (branch `accomplished-alchemist-viewer`)

Every VIEWER flag of the sweep above, each verified through the `--claude-play` protocol. Autonomous play
is untouched: every change is human-play-only (a chooser nulled by `RevealLogPause`, `HumanPlayActive()`,
an output-only event sink, or the external-chooser segment loop).

* **RESOLVED -- the human menu dropped Stroke of Genius after a big loop (80016).** Root cause: human play
  enumerated one plan per (target, X) for every X 0..max, so after a 53-mana untap loop the Stroke group was
  ~110 digits wide; the viewer plan-space valve priced the frame at 1.57M positions and dropped the
  card. **New design: X is not a plan axis under human play.** `DrawXCandidates`' human branch returns ONE
  representative per TARGET (opponent: the minimal lethal X when the pool pays it, else X = 0; you: X = 1,
  or 0), and the X is the new **`x_value`** decision at the cast's payment (`g_play_x_value_chooser`,
  called from `TurnSolver` `apply_one`; `WriteXValueDecisionJson`; `xValuePanelHtml`). Options = every X
  0..max_x on the CURRENT pool (so X is chosen against the mana the line has left), reply = X itself on
  the positional `--choices` stream, default = minimal lethal at the opponent (else max), min(max, library)
  for yourself. The menu reads "Stroke of Genius (X chosen at cast; lethal X=50) → opponent draws"; the
  CheckLine variant dialog asks the TARGET, not a representative X. The stale `plans_truncated.dropped_names`
  under `MTG_VIEWER_EXECUTOR_FALLBACK` is replaced by `executor_menu: true`. The segment loop now also stops
  prompting once the opponent is DECKED (it already did at life 0). Verification (seed 80016):
  `0,1,0,0,4,-1,-1,2,4,-1,-1,0,-1,-1,0,8,4555,50` -> **win T4** (the search's T4); with X=49 the opponent keeps
  1 card. Fixture `test/scenarios/aa_human_stroke_one_per_target.json` pins two variants (was 7+). Found on
  the way: the enumerator's DrawX `dmax` counts the floating pool TWICE (`AvailableManaPool` already credits
  it) -- harmless search over-generation, left for the engine chunk; the human branch takes it back out.
* **RESOLVED -- state not exposed (80005/80009/80011/80012/80019).** Decision JSON now carries
  `me.life_gained_this_turn` (while an X-per-life source is ours, or non-zero), `opponent.library_size`
  (+ `decked`) when a library was dealt, per-permanent `yield` for scaled sources (Alchemist X) and
  `power`/`toughness` (CombatPowerOf / LethalToughness: counters, pumps, lords, Auras, Equipment),
  `activation_cost` on every activation (Crab Umbra's untap reads "{2}{U}"; `cost_pips` stays pips-only by
  design) and `attached_to_idx` beside the unchanged `attached_to` (= host `num`, which the GUI and saved
  references key on). The floating pool was already published (`me.floating_mana`, when non-empty). The
  loop's "untap loop xN" event now names the float's colours ("{U}x1 {G}x53") -- there is no Alchemist
  colour AXIS in the human menu (the loop picks each tap's colour inside the resolver), so that is where
  the colour becomes visible. GUI: P/T and yield badges on thumbs, "gained N this turn" and the opponent's
  library in the life bars.
* **RESOLVED -- Ezuri's combat pump "silent" (80003/80006/80008).** Not a wiring gap: the `firebreathe`
  decision covers `team_pump_cost`, but it is installed only under `--firebreathe-prompt` (the viewer
  always passes it); the sweep agents ran bare `--claude-play`. The auditor's driver now passes it and
  answers on the turn-keyed side channel, Ezuri is a NAME_CHOICES `firebreathe` expectation, the decision
  JSON names its `pump_sources` ("{2}{G}{G}{G}: Elves you control get +3/+3"), and claude-play.md says so.
  Verification (80008): `1,0,4,0,0,0,0,0,0,0,0,0,0,0,0,0` + `--firebreathe-prompt` -> `firebreathe` T5
  max 1; `--firebreathe 5:0` leaves the opponent at 12, `5:1` at 3.
* **RESOLVED -- the loop's Staff draws were silent (80016).** Draw-to-find stays the loop's documented
  default; each draw round now emits "Staff of Domination: untap loop drew 9 card(s) looking for the payoff
  ({5},{T} each, paid from the float): Feed the Clan, ..., Stroke of Genius". A human who wants another
  count runs the untaps by hand (`untapact=`).
* **Gates (2026-10-19, this branch):** `audit_viewer_decisions.py ... --sideboard` (40 games) observed
  attach_host / choose_color / discard / firebreathe / tutor_etb / x_value, escalations VERIFIED Eternal
  Witness (dig) and Trufflesnout (attack_mode): **5h PASS**; `--no-sweep` expects attach_host, attack_mode,
  choose_color, dig, firebreathe, x_value. verify_deck `viewer_wiring` PASS (choose_color, firebreathe,
  x_value). `regression.sh --smoke` (VPC_ALWAYS=1): 226/226 scenarios, 124/128 cases -- the 4 moves are the
  known pre-existing ones (critter_smoke_d0, melira_smoke_d0, selesnya d3/d5), no new move; reference
  replay 630 refs, 0 play-drift / 0 enum-gap / 0 contract-fail (identical tally to the pre-change binary).
  mtg-test 1486/1486. viewer_client_check: the same 2 pre-existing failures (equip, equip from hand) on
  the pre-change binary.
* **OPEN (engine-side, surfaced):** the post-loop 80016 frame now enumerates in full (~43k positions, 55k
  plans, ~8-12 s per click; it was 0.7 s only because the valve had dropped Stroke). The width is the four
  identical TAPPED Forests as Utopia Sprawl / Wild Growth hosts (16 combos) times cast-order permutations
  -- the land-Aura host fold the C2b notes defer (moves other decks' GT: USER).

## Approved deferrals

(none yet)

## PROVISIONAL deferrals (awaiting user sign-off)

* Eternal Witness: 'you may return' collapsed to mandatory (returning is weakly dominant; no graveyard-count/discard payoff in deck).
* Staff of Domination: {4},{T}: Tap target creature -- inert (opponent creatures never attack/block; nothing reads tapped state; tapping own creature is never better).
* Ezuri: regenerate inert (nothing destroys our creatures); trample inert (no blockers).
* Crab Umbra: Umbra armor inert (nothing destroys our creatures).
* Seedcradle Witch: targeting an opponent's creature not offered autonomously (inert -- nothing reads their P/T or tapped state).
* Instant-speed activity on the OPPONENT's turn (Crab Umbra untap, Nourish) not modelled -- engine acts in our phases; an end-step kill lands strictly later than the same line in our main.
