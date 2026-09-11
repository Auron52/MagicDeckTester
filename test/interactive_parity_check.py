#!/usr/bin/env python3
"""Parity check for --interactive (the viewer's persistent-child mode).

The claim --interactive makes is narrow: a persistent child that receives its picks one line at a
time on stdin emits EXACTLY the decision frames a stateless respawn chain emits for the same
growing --choices prefix. This drives one game both ways and byte-compares every frame:

  * interactive: ONE child; read a decision, answer with its own engine default
    (heuristic_default / ai_choice, the same fields viewer_protocol_check trusts), repeat;
  * stateless:   for each prefix the interactive walk produced, a fresh --choices invocation,
    exactly like tools/play/server.js's fallback path.

LAYER 2 (2026-09-11): the same claim for a LATE-DELIVERED CAST-ORDER PIN. A cast-order pin used to
be argv-only, which made the viewer's session key change on every click that recorded one and threw
the persistent child away (EldraziDisplacerFlicker claude_s12_gi11: 31 of 60 decisions carry a pin).
Pins now ride stdin as `@cast-order <spec>` directives, merged into the child's map immediately
before the picks they belong to. That is a NEW way for the two arms to disagree -- a pin applied one
frame too late reorders a line the human already committed -- and nothing else can see it: the
reference sweep replays statelessly with the full spec in argv, and the end-of-turn board usually
cannot tell one cast order from another. So: replay a saved reference's own picks both ways, pins
in argv vs pins delivered late, and byte-compare every frame.

Any mismatch (frame text, frame count, terminal result) is a FAIL. Run it against a few decks:

    python3 test/interactive_parity_check.py                       # default games + late-pin layer
    python3 test/interactive_parity_check.py deck.cod prof.json 8 7  # one explicit game (layer 1)
"""
import importlib.util
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
MTG = os.environ.get("MTG_BIN", "./build/Release/mtg")
DEC_BEGIN, DEC_END = "<<<CLAUDE_DECISION>>>", "<<<END_DECISION>>>"
RES_BEGIN, RES_END = "<<<CLAUDE_RESULT>>>", "<<<END_RESULT>>>"

# ONE definition of "what argv does the viewer send for this reference" -- shared with
# test/viewer_step_latency.py rather than restated, because a second copy that drifted would make
# this check compare two arms of a game the viewer does not play.
_spec = importlib.util.spec_from_file_location(
    "viewer_step_latency", os.path.join(HERE, "viewer_step_latency.py"))
vsl = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(vsl)


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


# ---- LAYER 2: a cast-order pin delivered LATE, on stdin ---------------------------------------

def viewer_args(ref_path, ref, choices, cast_order_spec):
    """The argv tools/play/server.js's buildArgs() sends, for a PREFIX of a reference's picks.
    `cast_order_spec` is passed in rather than derived so the caller can send it in argv (arm A) or
    withhold it and deliver it on stdin (arm B)."""
    deck, prof = vsl.deck_paths(ref_path)
    args = [MTG, deck, "--profile", prof,
            "--cards-json", os.path.join(ROOT, "src", "cards", "data", "cards.json"),
            "--claude-play", "--seed", str(ref["seed"]), "--game-index", str(ref["game_index"]),
            "--max-turns", "8", "--depth", "0",
            "--choices", ",".join(str(c) for c in choices),
            "--firebreathe-prompt", "--storage-hold-prompt"]
    # The reference's OTHER keyed channels stay in argv on both arms (they are not deliverable
    # late by design -- their prompt frames exit 70 and force a respawn anyway).
    side = vsl.side_channels(ref["decisions"])
    i = 0
    while i < len(side):
        if side[i] == "--cast-order":
            i += 2                      # dropped; this arm decides how the pin is delivered
            continue
        args += side[i:i + 2]
        i += 2
    if cast_order_spec:
        args += ["--cast-order", cast_order_spec]
    return args


def cast_order_pins(decisions):
    """(position in the --choices stream, 'ord:A|B') for every recorded pin, in stream order. The
    position is where the PINNED decision's own pick sits, which is when the viewer learns the pin
    -- it is delivered with the step AFTER it, exactly as index.html does."""
    pins, pos = [], 0
    for d in decisions:
        dec = d.get("decision", {})
        t = dec.get("type")
        if t in vsl.SIDE_CHANNEL_TYPES:
            continue
        n = len(d["chosen"]) if isinstance(d["chosen"], list) else 1
        if t == "main_phase" and d.get("cast_order") and dec.get("main_ordinal") is not None:
            pins.append((pos, f'{dec["main_ordinal"]}:' + "|".join(d["cast_order"])))
        pos += n
    return pins


VAL_BEGIN, VAL_END = "<<<CLAUDE_VALIDATION>>>", "<<<END_VALIDATION>>>"


def derived_line(frame_raw):
    """A parseable --validate-line for the frame the child is parked on: the frame's own rank-best
    plan, re-encoded. Derived from the LIVE frame rather than from the reference so both arms ask
    the identical question even where the replay has drifted from the recording."""
    try:
        d = json.loads(frame_raw)
    except Exception:
        return None
    if d.get("type") != "main_phase":
        return None
    plans = d.get("plans") or []
    if not plans:
        return None
    p0 = plans[0]
    parts = []
    if p0.get("land"):
        parts.append("land=" + p0["land"])
    for n in (p0.get("cast_order_canonical") or p0.get("casts") or []):
        parts.append("cast=" + n)
    return ";".join(parts) if parts else "pass"


def run_late_pin_interactive(ref_path, ref, choices, pins, max_steps=600):
    """ONE --interactive child, started with NO --cast-order, fed one pick at a time with each pin
    delivered as `@cast-order` on the line before the pick that follows its decision, and an
    `@validate-line` asked of every main-phase frame before its pick. Mirrors runStepCached's and
    runValidateCached's fast paths. Returns (frames, validations, result_raw), where validations[i]
    is the block for frames[i] (None where the frame could not be asked)."""
    p = subprocess.Popen(viewer_args(ref_path, ref, [], "") + ["--interactive"],
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.DEVNULL, text=True, bufsize=0, cwd=ROOT)
    by_pos = dict(pins)
    frames, validations, buf, sent = [], [], "", []
    result_raw = None
    spec = []                      # the accumulated spec, exactly as the server accumulates it
    for _ in range(max_steps):
        buf, found = read_until(p.stdout, DEC_END, buf)
        if not found:
            result_raw = extract(buf, RES_BEGIN, RES_END)
            break
        frames.append(extract(buf, DEC_BEGIN, DEC_END))
        buf = buf[buf.index(DEC_END) + len(DEC_END):]
        # A VALIDATION consumes no pick: the child answers and stays parked on this same frame,
        # which is exactly the property /api/validate depends on. Asking here, before the pick,
        # is also what the browser does (Commit Line validates, then steps).
        line = derived_line(frames[-1])
        if line:
            p.stdin.write("@validate-line " + line + "\n")
            p.stdin.flush()
            buf, vfound = read_until(p.stdout, VAL_END, buf)
            if not vfound:
                p.kill()
                raise RuntimeError("interactive child did not answer @validate-line")
            validations.append((line, extract(buf, VAL_BEGIN, VAL_END)))
            buf = buf[buf.index(VAL_END) + len(VAL_END):]
        else:
            validations.append(None)
        if len(sent) >= len(choices):
            p.stdin.close()
            break
        # The pin for the decision being ANSWERED NOW rides with this step, directly before its
        # pick -- which is exactly the browser's sequence (index.html records S.castOrder for the
        # decision it is committing, then posts the step carrying that pick) and exactly what
        # runStepCached writes. One position later and the pin arrives after its only reader has
        # run; this check caught that off-by-one on its first run.
        pos = len(sent)
        if pos in by_pos:
            spec.append(by_pos[pos])
            p.stdin.write("@cast-order " + ";".join(spec) + "\n")
            p.stdin.flush()
        pick = choices[pos]
        sent.append(pick)
        p.stdin.write(f"{pick}\n")
        p.stdin.flush()
    else:
        p.kill()
        raise RuntimeError("late-pin walk did not terminate in %d steps" % max_steps)
    p.wait()
    return frames, validations, result_raw


def check_late_pin(ref_path):
    ref = json.load(open(ref_path))
    choices = vsl.flatten_choices(ref["decisions"])
    pins = cast_order_pins(ref["decisions"])
    if not pins:
        print(f"  SKIP {ref_path}: no recorded cast-order pins")
        return True
    full_spec = ";".join(s for _, s in pins)
    frames, validations, iresult = run_late_pin_interactive(ref_path, ref, choices, pins)
    # Arm A: the stateless chain the viewer used to run, full spec in argv, one spawn per prefix.
    n_val = 0
    for i in range(len(frames)):
        r = subprocess.run(viewer_args(ref_path, ref, choices[:i], full_spec),
                           capture_output=True, text=True, cwd=ROOT)
        sframe = extract(r.stdout, DEC_BEGIN, DEC_END)
        if sframe != frames[i]:
            print(f"  FAIL {ref_path} step {i}: a late-delivered cast-order pin changed the frame")
            print(f"    late-pin:  {(frames[i] or '')[:300]}")
            print(f"    argv-pin:  {(sframe or '')[:300]}")
            return False
        if validations[i] is None:
            continue
        line, iblock = validations[i]
        rv = subprocess.run(viewer_args(ref_path, ref, choices[:i], full_spec)
                            + ["--validate-line", line],
                            capture_output=True, text=True, cwd=ROOT)
        sblock = extract(rv.stdout, VAL_BEGIN, VAL_END)
        if sblock != iblock:
            print(f"  FAIL {ref_path} step {i}: @validate-line answered differently from a "
                  f"--validate-line spawn (line: {line[:120]})")
            print(f"    interactive: {(iblock or '')[:300]}")
            print(f"    stateless:   {(sblock or '')[:300]}")
            return False
        n_val += 1
    r = subprocess.run(viewer_args(ref_path, ref, choices, full_spec),
                       capture_output=True, text=True, cwd=ROOT)
    sresult = extract(r.stdout, RES_BEGIN, RES_END)
    if sresult != iresult:
        print(f"  FAIL {ref_path}: terminal result differs")
        print(f"    late-pin: {iresult}")
        print(f"    argv-pin: {sresult}")
        return False
    print(f"  ok: {len(frames)} frames byte-identical with {len(pins)} pins delivered on stdin; "
          f"{n_val} in-child validations byte-identical")
    return True


def refs_with_pins(limit=2):
    """Saved references that recorded a cast-order pin -- the only games that can exercise this."""
    out = []
    base = os.path.join(ROOT, "references")
    for deck in sorted(os.listdir(base)):
        d = os.path.join(base, deck)
        if not os.path.isdir(d) or deck == "suboptimal":
            continue
        for f in sorted(os.listdir(d)):
            if not (f.startswith("claude_s") and f.endswith(".json")):
                continue
            path = os.path.join(d, f)
            try:
                ref = json.load(open(path))
            except Exception:
                continue
            n = sum(1 for e in ref.get("decisions", []) if e.get("cast_order"))
            if n:
                out.append((n, path))
    out.sort(reverse=True)          # the most-pinned games first -- they exercise the most merges
    return [p for _, p in out[:limit]]


def main():
    if len(sys.argv) >= 5:
        games = [(sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]))]
        refs = []
    else:
        edf = ("decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod",
               "decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json")
        games = [(edf[0], edf[1], 1, 0), (edf[0], edf[1], 8, 7)]
        refs = refs_with_pins()
    ok = True
    for deck, prof, seed, gi in games:
        print(f"[parity] {deck} seed={seed} gi={gi}")
        ok = check_game(deck, prof, seed, gi) and ok
    for ref in refs:
        print(f"[late-pin] {os.path.relpath(ref, ROOT)}")
        ok = check_late_pin(ref) and ok
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
