#!/usr/bin/env python3
"""Suite-wide: is any deck's cleanup-shed RANKING load-bearing inside the search?

USER (2026-08-21): "How on earth does this kind of discard end up in there? Don't we have a proper
path in the Analyzer relating to how it should be created?"

We do -- analyzer stage 8 (`--discard-analysis`) plus the skill's §5i bucket policy, with
FiveColourProvider as the reference. But its EVIDENCE pass labels real cleanup sheds only: the
`[discard_trace]` it parses is emitted inside `AIEngine::ChooseDiscard`, so a deck that never sheds
in real play yields zero labels and the stage returns DISCARD_INERT, "no policy to derive" --
leaving the deck on the shared root ranking (highest-MV-first). That is the SAME blind spot the
KittyEquipment census had, one layer up: the ROLLOUT sheds too, takes index 0 with no search above
it, and is invisible to the labeller.

On KittyEquipment the gap turned out benign (inverting the rule moved 0 win turns in 300). This
sweep asks whether it is benign everywhere, using the same bound: MTG_SHED_WORST makes the ROLLOUT
shed the LAST-ranked candidate instead of the first, so base-vs-worst brackets what any ranking
could pay on that deck.

Reading it:
  * delta 0.0000 with plays-differ 0  -> the lever cannot fire (a provider returning ONE index is
    structurally immune -- cd.size()==1 means worst IS best) or the shed never changes a line;
  * delta 0.0000 with plays-differ > 0 -> the rule fires and changes play but not the metric: no
    ranking is worth authoring;
  * delta != 0 -> the ranking is LOAD-BEARING in the search, and stage 8 would only ever notice it
    if that deck also sheds in real play. Those are the decks the blind spot actually costs.
"""
import json
import sys

DECKS = {
    "slivers":     ("decks/slivers_vial/slivers_vial.txt", "decks/slivers_vial/slivers_vial.profile.json"),
    "burn":        ("decks/burn/burn.txt", "decks/burn/burn.profile.json"),
    "th":          ("decks/treasure_hunt/treasure_hunt.txt", "decks/treasure_hunt/treasure_hunt.profile.json"),
    "knights":     ("decks/Knights/Knights.cod", "decks/Knights/Knights.profile.json"),
    "antilife":    ("decks/Anti-Lifegain/Anti-Lifegain.cod", "decks/Anti-Lifegain/Anti-Lifegain.profile.json"),
    "hinata":      ("decks/Hinata2/Hinata2.cod", "decks/Hinata2/Hinata2.profile.json"),
    "dragonstorm": ("decks/Dragonstorm/Dragonstorm.cod", "decks/Dragonstorm/Dragonstorm.profile.json"),
    "auras":       ("decks/Auras/Auras.cod", "decks/Auras/Auras.profile.json"),
    "goblins":     ("decks/Goblins/Goblins.cod", "decks/Goblins/Goblins.profile.json"),
    "creature_giving": ("decks/Creature Giving/Creature Giving.cod",
                        "decks/Creature Giving/Creature Giving.profile.json"),
    "mirrorwing":  ("decks/Mirrorwing Dragon/v1-twinflame-anger/Mirrorwing Dragon.cod",
                    "decks/Mirrorwing Dragon/v1-twinflame-anger/Mirrorwing Dragon.profile.json"),
    "fivecolour":  ("decks/FiveColour/FiveColour.cod", "decks/FiveColour/FiveColour.profile.json"),
}

ARMS = {"base": {}, "worst": {"MTG_SHED_WORST": True}}

SEED  = 300001
GAMES = 60
DEPTH = 3


def main():
    scale = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    jobs = []
    for deck, (path, prof) in DECKS.items():
        for arm, flags in ARMS.items():
            job = {
                "name":    f"{arm}.{deck}",
                "deck":    path,
                "profile": prof,
                "games":   GAMES * scale,
                "seed":    SEED,
                "depth":   DEPTH,
                # A deck whose profile pins a value_play depth REFUSES an explicit --depth. Override
                # it, exactly as the analyzer's own discard-evidence pass does, so every deck is
                # bracketed at the SAME searched depth -- the arms are paired within a deck, so a
                # deck's shipped depth is not what this compares.
                "ignore_play_profile": True,
            }
            if flags:
                job["flags"] = dict(flags)
            jobs.append(job)
    json.dump({"jobs": jobs}, sys.stdout, indent=1)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
