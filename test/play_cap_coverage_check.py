#!/usr/bin/env python3
"""DISPLAY-CAP COVERAGE: every distinct (action, target, count) in the fan survives the cap.

USER 2026-09-10, EldraziDisplacerFlicker: "sometimes the emiel or displacer can no longer choose
targets."

WHY THIS IS A CHECK AND NOT A NOTE. The viewer's plan list is capped (MTG_PLAY_PLANS_CAP, default
200) and the GUI builds its target pickers from THAT slice, not from the engine's fan. On a go-off
frame the fan is mostly cast-order permutation noise, so a rank-ordered cap can spend all 200 slots
on permutations of one cast set and drop a whole (outlet, target) pair -- the engine enumerated the
blink, the player simply cannot see it. Nothing else notices: viewer_protocol_check runs UNCAPPED
(so the class is invisible there BY CONSTRUCTION), the regression digests cover autonomous play, and
the frame is still well-formed JSON.

METHOD. The display cap is display-only -- it changes neither plan indices nor play (see
MTG_PLAY_PLANS_CAP in main.cpp) -- so the SAME pick stream produces the SAME frames at any cap.
This drives the user's own deep-go-off line (the seed-9 gi=8 turn-4 tools/play rejection artifact)
twice per configuration, once uncapped and once at the viewer's real cap, and asserts frame by frame
that the capped plan list carries every (action, target, count) payload the uncapped one does.

Configurations: the shipped default and MTG_HUMAN_SAT_ENUM=0, each at both MTG_UNTAP_C_STARVED
settings (the starved-{C} promotion changes which activations are payable and therefore which pairs
the fan holds), plus MTG_HUMAN_SAT_LEGACY_ADDER_BAIL=1 -- the engine the artifact's raw pick indices
were recorded on, which is what makes one arm literally the user's frames.

Usage:  python3 test/play_cap_coverage_check.py       (MTG_BIN=<path> to override the binary)
"""
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
MTG = os.environ.get("MTG_BIN", os.path.join(ROOT, "build", "Release", "mtg"))
DEC_B, DEC_E = "<<<CLAUDE_DECISION>>>", "<<<END_DECISION>>>"
RES_E = "<<<END_RESULT>>>"

ARTIFACT = os.path.join(ROOT, "logs", "play", "rejections",
                        "EldraziDisplacerFlicker_cod_s9_gi8_t4.json")
DECK = os.path.join(ROOT, "decks", "EldraziDisplacerFlicker", "EldraziDisplacerFlicker.cod")
PROF = os.path.join(ROOT, "decks", "EldraziDisplacerFlicker",
                    "EldraziDisplacerFlicker.profile.json")


def walk(choices, env_extra, cap):
    """Every decision frame the pick stream reaches, at the given display cap."""
    env = dict(os.environ, MTG_PLAY_PLANS_CAP=str(cap), **env_extra)
    args = [MTG, DECK, "--claude-play", "--seed", "9", "--game-index", "8",
            "--max-turns", "8", "--depth", "0", "--profile", PROF,
            "--choices", "", "--interactive"]
    p = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.DEVNULL, text=True, bufsize=1, env=env)
    frames, buf, picks = [], "", list(choices)
    while True:
        while DEC_E not in buf and RES_E not in buf:
            line = p.stdout.readline()
            if not line:
                buf = None
                break
            buf += line
        if buf is None or DEC_E not in buf:
            break
        frames.append(json.loads(buf[buf.index(DEC_B) + len(DEC_B):buf.index(DEC_E)].strip()))
        buf = buf[buf.index(DEC_E) + len(DEC_E):]
        if not picks:
            break
        try:
            p.stdin.write(f"{picks.pop(0)}\n")
            p.stdin.flush()
        except BrokenPipeError:
            break
    try:
        p.kill()
    except Exception:
        pass
    return frames


def payloads(dec):
    """(card, verb, target, count, ...) identities in a frame's plan list -- the emitted action
    fields, which is exactly what the GUI reads to build its pickers."""
    out = set()
    for pl in dec.get("plans") or []:
        for a in pl.get("actions") or []:
            out.add((a.get("card"), a.get("verb"), a.get("blink_target"),
                     a.get("blink_count", a.get("x")), a.get("enchant_target"),
                     bool(a.get("sacout")), a.get("pod_victim")))
    return out


def blink_pairs(dec):
    """The narrower assertion the user's report names: every (outlet, target, count) blink."""
    return {(a.get("card"), a.get("blink_target_name"), a.get("blink_count"))
            for pl in (dec.get("plans") or []) for a in (pl.get("actions") or [])
            if a.get("verb") == "blink"}


CONFIGS = [
    ("default              ", {}),
    ("MTG_HUMAN_SAT_ENUM=0 ", {"MTG_HUMAN_SAT_ENUM": "0"}),
    ("LEGACY (user's frames)", {"MTG_HUMAN_SAT_LEGACY_ADDER_BAIL": "1"}),
]


def main():
    if not os.path.exists(ARTIFACT):
        print("[cap-coverage] NO ARTIFACT at logs/play/rejections/"
              "EldraziDisplacerFlicker_cod_s9_gi8_t4.json (gitignored).")
        print("[cap-coverage] NOT RUN -- re-save a deep go-off rejection from the viewer to arm it.")
        return 2          # never green on a did-not-run
    choices = json.load(open(ARTIFACT))["priorChoices"]
    print(f"[cap-coverage] driving the user's line ({len(choices)} prior choices), "
          f"cap 200 vs uncapped")

    bad = checked = 0
    for label, base in CONFIGS:
        for starved in ("1", "0"):
            env = dict(base, MTG_UNTAP_C_STARVED=starved)
            full = walk(choices, env, 0)
            cap = walk(choices, env, 200)
            n = min(len(full), len(cap))
            if n == 0:
                print(f"  {label} STARVED={starved}: NO FRAMES -- treated as a failure")
                bad += 1
                continue
            worst = max((len(f.get("plans") or []) for f in full), default=0)
            miss_frames = 0
            for f, c in zip(full[:n], cap[:n]):
                mp = payloads(f) - payloads(c)
                mb = blink_pairs(f) - blink_pairs(c)
                if mp or mb:
                    miss_frames += 1
                    if miss_frames <= 2:
                        print(f"    frame t{f.get('turn')} nplans={len(f.get('plans') or [])}: "
                              f"{len(mb)} blink pair(s), {len(mp)} payload(s) lost to the cap")
                        for m in sorted(mb)[:5]:
                            print(f"      MISSING blink (outlet,target,count): {m}")
            checked += n
            status = "ok" if miss_frames == 0 else f"FAIL ({miss_frames} frames)"
            print(f"  {label} STARVED={starved}: {n} frames, biggest fan {worst} -> {status}")
            if miss_frames:
                bad += 1
    if bad:
        print(f"[cap-coverage] FAIL: {bad} configuration(s) lost a distinct (action,target,count) "
              f"to the display cap")
        return 1
    print(f"[cap-coverage] PASS: {checked} frames, the capped list covers every distinct payload")
    return 0


sys.exit(main())
