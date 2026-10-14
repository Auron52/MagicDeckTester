# KittyEquipment v2: one reference replay serialises the regression's reference stage

**Status:** open, not investigated (surfaced 2026-10-14).

## Symptom

`test/regression.sh` ends with the reference stage, `test/viewer_protocol_check.py --strict --threads 24`.
The checker's own usage note calls the full sweep a matter of seconds. On 2026-10-14 the stage had been
running for 3h40m and was still going, for commit 69e479bf. Almost all of that time went to ONE reference:

    references/KittyEquipment/v2-puresteel-hammer/claude_s8_gi7.json   (20 decisions; last changed 9d86a8f2, 2026-10-03)

The checker replays a reference one step at a time, with one `mtg` invocation per step
(`--claude-play --depth 0`, and a `--choices` prefix that grows by one per step). A single reference
therefore runs on one thread, and the other 23 sit idle. The run for the rebased tip 03d32461 hit the
same reference within minutes of starting the stage.

## What was observed

* **The slowness is in particular steps.** In the 03d32461 run the first five steps took about a
  minute in total. The step with `--choices 16,-1,-1,25,-1,-1` then ran for more than 10 minutes at
  99% CPU and ~23 MB RSS. In the 69e479bf run, steps near choice 13-14 took 3-15 minutes each.
* **It is depth 0,** so the turn search should not be running at all. Whatever costs minutes is done
  by the depth-0 `--claude-play` path at a decision point. One hypothesis, unverified: the
  enumeration of legal options that the viewer protocol dumps at each decision, which would grow with
  the number of equipment-host pairs.
* **This is separate from** the documented d3 search slowness in
  `analysis-KittyEquipment-v2-puresteel-hammer.md`.

## Repro

Run from the repo root with a Release build:

    build/Release/mtg decks/KittyEquipment/v2-puresteel-hammer/KittyEquipment.cod --claude-play --seed 8 \
      --game-index 7 --max-turns 8 --depth 0 \
      --profile decks/KittyEquipment/v2-puresteel-hammer/KittyEquipment.profile.json \
      --choices 16,-1,-1,25,-1,-1 --force-mulligan 0: \
      --cast-order '10:*|Cid, Freeflier Pilot|Golem-Skin Gauntlets;11:*|Shadowspear|Shadowspear|Dragonfire Blade|Colossus Hammer;15:*|Puresteel Paladin|Colossus Hammer|Skateboard|Accorder'"'"'s Shield|Cathar'"'"'s Shield' \
      --force-attackers '3:Sram, Senior Edificer' \
      --tap-pref '2:pre:3:32,33;3:pre:10:32,33,34;4:pre:15:32,33' \
      --legacy-main-swap --legacy-karoo-float --legacy-pain-pay --legacy-gsz-shuffle

## Next step

Profile that one step: a `./build.sh profile` binary under `perf`. Find which stage of a depth-0
claude-play decision spends the minutes, then decide whether it is waste to collapse or an
enumeration the protocol does not need.

## Why it matters

Every regression run spends hours of wall clock on this one reference, with the box almost idle. That
delays the validation of every commit.
