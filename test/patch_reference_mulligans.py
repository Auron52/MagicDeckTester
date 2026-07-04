#!/usr/bin/env python3
"""Backfill the `mulligan` field into existing reference games.

A saved references/<deck>/claude_s<seed>_gi<gi>.json records the played line but not
the mulligan decisions, so it breaks when the keep/bottoming heuristic changes (the
engine opens a different hand -> the recorded choices no longer apply). The per-mulligan
reshuffle is seeded by game_seed + mulligan_count, so the pre-bottom 7-card hand at each
depth is engine-version-independent; only the keep COUNT and the BOTTOMED cards are
heuristic. This derives both from the reference's own recorded opening hand:

  count    = 7 - |opening hand|            (London: keep 7, bottom N -> hand is 7-N)
  bottomed = (deterministic depth-`count` draw)  minus  (recorded opening hand), by name

and writes `"mulligan": {"count": N, "bottom": [card numbers]}` so a replay with
--force-mulligan reconstructs the exact hand on any engine version. Idempotent; verifies
the reconstruction before writing.

Usage:  python3 test/patch_reference_mulligans.py            # patch all, in place
        python3 test/patch_reference_mulligans.py --dry-run   # report only
"""
import json, os, re, subprocess, sys, glob, collections, itertools

MTG = os.environ.get("MTG_BIN", "./build/Release/mtg")
DRY = "--dry-run" in sys.argv[1:]
DECKS = {
    "Anti-Lifegain": ("decks/Anti-Lifegain.cod", "decks/Anti-Lifegain.profile.json"),
    "Hinata2":       ("decks/Hinata2.cod",       "decks/Hinata2.profile.json"),
    "Knights":       ("decks/Knights.cod",       "decks/Knights.profile.json"),
    "slivers_vial":  ("decks/slivers_vial.txt",  "decks/slivers_vial.profile.json"),
    "burn":          ("decks/burn.txt",     "decks/burn.profile.json"),
    "treasure_hunt": ("decks/treasure_hunt.txt", "decks/treasure_hunt.profile.json"),
}
DEC = re.compile(r"<<<CLAUDE_DECISION>>>\s*(\{.*?\})\s*<<<END_DECISION>>>", re.S)


def decision0_hand(deck, prof, seed, gi, force=None):
    """The opening-hand cards ({num,name}) at decision 0 (optionally under --force-mulligan)."""
    args = [MTG, deck, "--claude-play", "--seed", str(seed), "--game-index", str(gi),
            "--max-turns", "8", "--depth", "0", "--profile", prof, "--choices", ""]
    if force is not None:
        args += ["--force-mulligan", force]
    out = subprocess.run(args, capture_output=True, text=True).stdout
    m = DEC.search(out)
    if not m:
        return None
    return json.loads(m.group(1)).get("me", {}).get("hand", [])


def derive(path):
    ref = json.load(open(path))
    deck_dir = os.path.basename(os.path.dirname(path))
    if deck_dir not in DECKS:
        return None, "unknown deck dir"
    deck, prof = DECKS[deck_dir]
    seed, gi = ref["seed"], ref["game_index"]
    d0 = ref["decisions"][0]["decision"]
    ref_hand = sorted(c["name"] for c in d0["me"]["hand"])
    # decision 0 is turn-1 pre-main AFTER the draw step, so the drawing player already drew 1:
    # |hand| = (7 - count) + (0 on the play, 1 on the draw)  ->  count = (7 or 8) - |hand|.
    on_play = bool(d0.get("on_the_play", False))
    count = (7 if on_play else 8) - len(ref_hand)
    if count < 0:
        return None, f"unexpected hand size {len(ref_hand)} (on_play={on_play})"

    def reproduces(bottom_nums):
        rebuilt = decision0_hand(deck, prof, seed, gi, force=f"{count}:" + ",".join(map(str, bottom_nums)))
        return rebuilt is not None and sorted(c["name"] for c in rebuilt) == ref_hand

    if count == 0:
        return ({"count": 0, "bottom": []}, "no mulligan") if reproduces([]) \
               else (None, "count=0 but hand does not reproduce (shuffle changed?)")

    # Bottoming changes the library top (hence the turn-1 draw), so we can't diff directly.
    # Search: the candidate cards are the depth-`count` PRE-bottom hand (force bottom-0). Any
    # size-`count` subset whose removal reproduces the recorded hand is correct (same-named copies
    # are interchangeable). A subset containing the turn-1 draw can't be bottomed -> won't match.
    pool = decision0_hand(deck, prof, seed, gi, force=f"{count}:")
    if pool is None:
        return None, f"could not read the depth-{count} candidate hand"
    nums = [c["num"] for c in pool]
    if len(nums) > 9 and count > 3:
        return None, f"search too large (pool={len(nums)}, count={count})"
    for combo in itertools.combinations(nums, count):
        if reproduces(list(combo)):
            return {"count": count, "bottom": list(combo)}, f"count={count} bottom={list(combo)}"
    return None, f"no size-{count} bottom set reproduced the recorded hand (likely genuine drift)"


def main():
    refs = sorted(glob.glob("references/*/claude_s*_gi*.json"))
    patched = failed = 0
    for path in refs:
        ref = json.load(open(path))
        rel = path[len("references/"):]
        if "mulligan" in ref and not DRY:
            print(f"  skip (already has mulligan)  {rel}"); continue
        mull, detail = derive(path)
        if mull is None:
            print(f"  FAIL  {rel}: {detail}"); failed += 1; continue
        print(f"  {'would patch' if DRY else 'patched'}  {rel}: {detail}")
        if not DRY:
            # Minimal text insertion: add the `mulligan` field right after `"won": <val>,`, matching
            # the engine's own layout (mulligan on its own line before `decisions`). Avoids a full
            # JSON reformat so the diff is just the added line.
            text = open(path).read()
            bottom_csv = ", ".join(str(n) for n in mull["bottom"])
            mull_line = f'\n  "mulligan": {{ "count": {mull["count"]}, "bottom": [{bottom_csv}] }},'
            new_text, n = re.subn(r'("won": (?:true|false),)', r'\1' + mull_line, text, count=1)
            if n != 1:
                print(f"    (could not find insertion point in {rel})"); failed += 1; continue
            open(path, "w").write(new_text)
            patched += 1
    print(f"\n{'would patch' if DRY else 'patched'} {patched}, failed {failed}  ({len(refs)} refs)")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
