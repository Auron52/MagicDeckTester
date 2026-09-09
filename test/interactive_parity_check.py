#!/usr/bin/env python3
"""Parity check for --interactive (the viewer's persistent-child mode).

The claim --interactive makes is narrow: a persistent child that receives its picks one line at a
time on stdin emits EXACTLY the decision frames a stateless respawn chain emits for the same
growing --choices prefix. This drives one game both ways and byte-compares every frame:

  * interactive: ONE child; read a decision, answer with its own engine default
    (heuristic_default / ai_choice, the same fields viewer_protocol_check trusts), repeat;
  * stateless:   for each prefix the interactive walk produced, a fresh --choices invocation,
    exactly like tools/play/server.js's fallback path.

Any mismatch (frame text, frame count, terminal result) is a FAIL. Run it against a few decks:

    python3 test/interactive_parity_check.py                       # default: EDF seed 1 + seed 8
    python3 test/interactive_parity_check.py deck.cod prof.json 8 7  # one explicit game
"""
import json
import os
import subprocess
import sys

MTG = os.environ.get("MTG_BIN", "./build/Release/mtg")
DEC_BEGIN, DEC_END = "<<<CLAUDE_DECISION>>>", "<<<END_DECISION>>>"
RES_BEGIN, RES_END = "<<<CLAUDE_RESULT>>>", "<<<END_RESULT>>>"


def base_args(deck, prof, seed, gi, max_turns=8):
    return [MTG, deck, "--claude-play", "--seed", str(seed), "--game-index", str(gi),
            "--max-turns", str(max_turns), "--depth", "0", "--profile", prof]


def default_pick(dec):
    ac = dec.get("ai_choice")
    if isinstance(ac, int):
        return [ac]
    if isinstance(ac, dict) and isinstance(ac.get("index"), int):
        return [ac["index"]]
    hd = dec.get("heuristic_default")
    if isinstance(hd, int):
        return [hd]
    if dec.get("type") == "main_phase":
        return [-1]
    return [0]


def read_until(stream, marker, buf):
    """Read stream until `buf` contains `marker` (or EOF); returns (buf, found)."""
    while marker not in buf:
        line = stream.readline()
        if not line:
            return buf, False
        buf += line
    return buf, True


def run_interactive(deck, prof, seed, gi, max_steps=400):
    """Drive one --interactive child by engine defaults. Returns (frames, prefixes, result_raw)."""
    p = subprocess.Popen(base_args(deck, prof, seed, gi) + ["--choices", "", "--interactive"],
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.DEVNULL, text=True, bufsize=0)
    frames, prefixes, choices, buf = [], [], [], ""
    result_raw = None
    for _ in range(max_steps):
        buf, found = read_until(p.stdout, DEC_END, buf)
        if not found:
            result_raw = extract(buf, RES_BEGIN, RES_END)
            break
        raw = extract(buf, DEC_BEGIN, DEC_END)
        buf = buf[buf.index(DEC_END) + len(DEC_END):]
        frames.append(raw)
        prefixes.append(list(choices))
        picks = default_pick(json.loads(raw))
        choices.extend(picks)
        p.stdin.write(",".join(str(c) for c in picks) + "\n")
        p.stdin.flush()
    else:
        p.kill()
        raise RuntimeError("interactive walk did not terminate in %d steps" % max_steps)
    p.wait()
    return frames, prefixes, result_raw


def extract(text, begin, end):
    b, e = text.find(begin), text.find(end)
    if b < 0 or e < 0 or e < b:
        return None
    return text[b + len(begin):e].strip()


def run_stateless(deck, prof, seed, gi, prefix):
    r = subprocess.run(base_args(deck, prof, seed, gi)
                       + ["--choices", ",".join(str(c) for c in prefix)],
                       capture_output=True, text=True)
    if r.returncode == 70:
        return extract(r.stdout, DEC_BEGIN, DEC_END), None
    return None, extract(r.stdout, RES_BEGIN, RES_END)


def check_game(deck, prof, seed, gi):
    frames, prefixes, iresult = run_interactive(deck, prof, seed, gi)
    for i, (frame, prefix) in enumerate(zip(frames, prefixes)):
        sframe, _ = run_stateless(deck, prof, seed, gi, prefix)
        if sframe != frame:
            print(f"  FAIL step {i} (prefix {prefix}):")
            print(f"    interactive: {frame[:300] if frame else frame}")
            print(f"    stateless:   {sframe[:300] if sframe else sframe}")
            return False
    if not frames:
        print("  FAIL: no decision frames emitted")
        return False
    return iresult is not None and compare_results(deck, prof, seed, gi, frames, prefixes, iresult)


def compare_results(deck, prof, seed, gi, frames, prefixes, iresult):
    # full stream = last prefix + the picks given for the last frame
    last_picks = default_pick(json.loads(frames[-1]))
    full = prefixes[-1] + last_picks
    _, sresult = run_stateless(deck, prof, seed, gi, full)
    if sresult != iresult:
        print("  FAIL terminal result differs:")
        print(f"    interactive: {iresult[:300] if iresult else iresult}")
        print(f"    stateless:   {sresult[:300] if sresult else sresult}")
        return False
    ires = json.loads(iresult)
    print(f"  ok: {len(frames)} frames byte-identical; result won={ires.get('won')} "
          f"win_turn={ires.get('win_turn')}")
    return True


def main():
    if len(sys.argv) >= 5:
        games = [(sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]))]
    else:
        edf = ("decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod",
               "decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json")
        games = [(edf[0], edf[1], 1, 0), (edf[0], edf[1], 8, 7)]
    ok = True
    for deck, prof, seed, gi in games:
        print(f"[parity] {deck} seed={seed} gi={gi}")
        ok = check_game(deck, prof, seed, gi) and ok
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
