# Shadowborn Apostle mana-clean: Lake oracle face

The `lastweek-20261110-fast` 5,000-game Shadowborn row (source `8ec9533150e1`) reported 27,015 issues: 23,183 `ORACLE-GAP`, 3,714 `UNMODELED`, 108 `APPLY-DROP-ORDER`, 7 `APPLY-DROP`, and 3 `ENUM-GAP`. Its 23,314 `PHANTOM` entries are disclosed separately. This change addresses one dominant oracle blind spot, not the whole row.

## Cause and change

Lake of the Dead has two payment choices: an ordinary tap for one black mana, and an on-demand crack that sacrifices a Swamp for four black mana. The engine payer can use either during payment. The oracle's `BuildSources` only enumerated the ordinary tap, so it could not replay an engine line that cracked Lake. The sampled gap traces show the engine floating three or four black mana after the crack while the nearest oracle outcome had only `Lake of the Dead:B` and lacked the sacrificed Swamp.

The oracle now offers a distinct `Lake of the Dead:B(crack)` payment face when the land has a legal Swamp victim. The scripted payment route applies the same `ApplySacForMana` transition as the engine and consumes the searched early-crack pin. Ordinary manual taps and the engine's payment policy are unchanged. The oracle's `ENUM-GAP` pass now admits on-demand sacrifice sources while still excluding direct `SacAction` sources that require a plan action.

## Focused evidence

The pre-change ten-game manifest is `/tmp/codex-sb-oracle-lake-ten.json`, using Shadowborn indices 10001, 10005, 10008, 10014, 10109, 10113, 10160, 10170, 10233, and 10331, each with seed `30000000 + index`. Its baseline used the existing PGO binary at `8ec9533150e1`; the changed run used this worktree's Release binary. Both used `MTG_MANA_ORACLE=2`, two batch workers, and the same deck and profile.

| Measure | Before | After |
|---|---:|---:|
| Decisions / plans | 90 / 1,590 | 90 / 1,590 |
| `ORACLE-GAP` | 406 | 0 |
| `MISS` | 0 | 23 |
| `APPLY-DROP` | 0 | 22 |
| `ENUM-GAP` | 0 | 1 |
| `UNMODELED` | 0 | 0 |

All ten `.wins` files are byte identical before and after. At this stage, 19 `APPLY-DROP` flags came from seed 30010014, three from seed 30010160, and the `ENUM-GAP` was seed 30010331. These were previously masked by the oracle's inability to replay Lake's crack. A focused unit case covers the crack face, five black mana floated by pre-tapping Lake and its Swamp victim, Swamp sacrifice, pin consumption, and absence of a crack face without a victim. Its translation unit compiled, but the full isolated `mtg-test` build was stopped after seven of 245 steps to avoid contention with other builds; the unit case has not been run yet.

## Paid-suspend follow-up

The 22 new `APPLY-DROP` flags shared a payer defect. `LineCastCostTotal` counted casts and certain activations but omitted a mana-costed `Suspend` action. In seed 30010014, the first `{1}{B}` payment spent Lake's ordinary `{B}` plus the Swamp's `{B}` because the payer saw no later bill. The following Apostle casts could not pay. A line hold that includes the suspend cost makes the payer account for the complete line; successful `ApplySuspend` then subtracts its paid cost from that hold.

The same ten-game manifest, run after this follow-up against the post-Lake oracle baseline, produced `APPLY-DROP=0`, `ORACLE-GAP=0`, and `MISS=1` (the unchanged seed 30010331 `ENUM-GAP`). All ten `.wins` remain byte identical. The two focused unit translation units compiled through `build.sh`; the full test executable has not been linked or run. This is a focused diagnosis, not a replacement for the 5,000-game gate.

## Remaining seed 30010331

The one remaining focused `ENUM-GAP` is a different mechanism. The rejected plan casts two Thrumming Stones, a Shadowborn Apostle, and Dark Ritual. Oracle mode finds a legal order, Stone > Apostle > Ritual > Stone, using Lake's crack and Lotus Bloom. The first Stone makes the Ritual's ripple live; the rippled Ritual copy supplies the mana for the second Stone. With `MTG_DBG_MULTI=4`, this exact four-action subset reports `cost=12`, `mana_ok=0`, `why=ef-walk-fail`. Setting `MTG_MANA_PRUNE=0` and disabling the walk cuts leaves the `ENUM-GAP`, so the scalar bound is not the refusing gate. The executor-feasibility rescue calls `SubsetPayableSequential`, whose `SeqWalkFixedOrder` credits `ApplyRitualFloat` for the directly cast Ritual but does not resolve its ripple chain. Its modeled line therefore ends short although the oracle's full apply pays. A fix needs a sound ripple-aware rescue or a deliberately bounded optimistic admission; this isolated patch does not change that gate.

## Remaining classification

The 3,714 `UNMODELED` flags are oracle search-budget limits on long combo plans, not proof of a payment defect. Seed 30012345 alone produces 390 such flags with the message `too many actions to try every order`. The 108 historical `APPLY-DROP-ORDER`, seven `APPLY-DROP`, and three `ENUM-GAP` flags need separate engine investigation. In seed 30013493, for example, a Diabolic Intent line drops in engine order although an oracle action order pays. This patch has not rerun the 5,000-game gate, so the new full-row count is unknown.
