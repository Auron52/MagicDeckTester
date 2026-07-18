#!/usr/bin/env python3
"""Per-deck escalation FALLBACK A/B: budget renewal (MTG_ESCALATION_FRESH_FRAC) x single-depth fallback
(MTG_ESC_SINGLE / MTG_ESC_SINGLE_OFFSET), measured against the deck's SHIPPED play (its enabled value_play
block, i.e. a bare run) on QUALITY (LP = avg win turn, loss=max_turns+1; lower=better) AND SPEED (ms/game).

These levers ONLY bite for decks that actually ESCALATE (a value-leaf line committed below the trust depth
falls back to a heuristic re-search) -- i.e. the HEAVY decks (antilife/hinata), which escalate at their d5
shipped depth (value_trust_depth unset). The light decks (burn trusts at d6, others at d5) barely escalate, so
these are inert for them.

Runs each deck BARE so its enabled value_play block drives play (d5/b20 for the heavy decks); the escalation
knobs are supplied via env. NOTE: this requires the env-precedence fix in TurnSolver (an explicitly-set
MTG_ESCALATION_FRESH_FRAC wins over the block's escalation_fresh_frac) -- else the block's -1 would mask the env.

Baseline arm = no env (legacy shared-budget escalation, fresh_frac=-1, no single). Each variant is PAIRED
against the baseline on the SAME seed, so the per-seed delta is the lever's real effect (not sample noise).

Usage:  esc_fallback_ab.py <deck>[,<deck>...] [nseeds] [games]
        deck in {antilife, hinata}.  Defaults: nseeds=6, games=300.
Run this AFTER the deck's value-leaf table is generated (antilife gap-fill; hinata mulligan profile) so the
escalation behaviour reflects the real table. Logs to logs/eval/esc_fallback_ab.log.
"""
import subprocess, re, sys, os, time
from concurrent.futures import ThreadPoolExecutor, as_completed

BIN = "build/Release/mtg"
DECKS = {
    "antilife": "decks/Anti-Lifegain/Anti-Lifegain.cod",
    "hinata":   "decks/Hinata2/Hinata2.cod",
    "th":       "decks/treasure_hunt/treasure_hunt.txt",   # ~6% escalation rate (control deck, many decisions)
    "slivers":  "decks/slivers_vial/slivers_vial.txt",     # ~6.5%
    "knights":  "decks/Knights/Knights.cod",               # ~4.7%
    "burn":     "decks/burn/burn.txt",                     # ~2.8%
}
# held-out seeds (disjoint from the 2002/3003 regression train seeds)
SEEDS = [4004, 5005, 6006, 7007, 10010, 11011, 12012, 13013, 14014, 15015]
AVG = re.compile(r"avg \(turns\)\s*:\s*([0-9.]+)")  # merged metric (was "Avg win turn : X" pre-a4f2be7)

# variant name -> extra env (baseline = {}). fresh_frac renews the escalation budget; single = one heuristic
# pass at committed-offset instead of the 1..depth ladder; the combo tests both together.
# NOTE: the heavy decks now SHIP the combo in value_play, so a bare run == the adopted combo. To measure the
# TRUE pre-adoption baseline, force legacy via env (MTG_ESC_BEAM=0 disables the beam; FRESH_FRAC=-1 = legacy
# shared budget). Every variant sets both envs explicitly so the comparison is clean regardless of the JSON.
_OFF = {"MTG_ESC_BEAM": "0", "MTG_ESCALATION_FRESH_FRAC": "-1"}
def _v(beam=0, ld=None, fresh="-1", static=False):
    e = {"MTG_ESC_BEAM": str(beam), "MTG_ESCALATION_FRESH_FRAC": str(fresh)}
    if ld is not None: e["MTG_ESC_BEAM_LEAFDEPTH"] = str(ld)
    if static: e["MTG_ESC_BEAM_STATIC"] = "1"   # prune by static MoveOrder (no value reorder)
    return e
# WIDTH sweep (ld1 = protect top plies): find the beam width that preserves quality at d3, where the shallow
# value-leaf is a weaker ranking proxy so the heuristic-best line can fall outside a narrow top-W.
# VALUE-RANKED vs STATIC beam at d3: does pruning by static MoveOrder (no value reorder) avoid the shallow-leaf
# reorder drift? (v = value-ranked = current adopted; s = static.)
# HINATA d3 width ladder: the leaf beam hurts hinata quality (+0.006..+0.010); W7 was less-bad than W3/W5,
# so the winning combo line is often ranked outside top-W at the leaf. Does WIDER recover quality (and at what
# speed cost)? If even W20 stays worse, the beam fundamentally mis-prunes hinata's deep combo at d3.
# ADOPTION A/B (2026-07-18): measure the SHIPPED depth-adaptive beam vs beam-off, at d3 (the only depth it now
# bites -- production d5/d6 is byte-identical). "baseline" = MTG_ESC_BEAM=0 => beam OFF. "adopted" = a BARE run
# (no MTG_ESC_BEAM env) so the per-deck depth-adaptive beam drives (shallow regime = static W20 ld1 at d3).
VARIANTS = {
    "baseline": dict(_OFF),   # MTG_ESC_BEAM=0 => escalation beam OFF (legacy frontier)
    "adopted":  {},           # bare => per-deck depth-adaptive beam fires (d3 => static W20 ld1)
}
# Legacy variants (budget-renewal / cold single-depth fallback) — kept for reference, not in the default sweep:
#   "fresh1.0":       {"MTG_ESCALATION_FRESH_FRAC": "1.0"}
#   "single_off1":    {"MTG_ESC_SINGLE": "1", "MTG_ESC_SINGLE_OFFSET": "1"}
#   "single_off2":    {"MTG_ESC_SINGLE": "1", "MTG_ESC_SINGLE_OFFSET": "2"}   (cold pass -> slower on hinata)
LOG = "logs/eval/esc_fallback_ab.log"
_lock = __import__("threading").Lock()


def say(s):
    with _lock:
        print(s, flush=True)
        os.makedirs(os.path.dirname(LOG), exist_ok=True)
        open(LOG, "a").write(s + "\n")


def run(deck_file, seed, games, env_extra):
    env = dict(os.environ)
    env.update(env_extra)
    # BARE run: no --depth/--budget so the deck's enabled value_play block drives play (heavy decks: d5/b20).
    # ESC_AB_DEPTH=D overrides to an explicit depth (+ --ignore-play-profile to bypass the block's depth lock),
    # e.g. ESC_AB_DEPTH=3 to study the beam where every deck escalates heavily (~30-58%).
    cmd = [BIN, deck_file, "--seed", str(seed), "--games", str(games), "--max-turns", "8", "--threads", "1"]
    _depth = os.environ.get("ESC_AB_DEPTH")
    if _depth:
        cmd += ["--depth", _depth, "--ignore-play-profile"]
    # ESC_AB_BUDGET=M pairs the depth override with a real virtual-ms budget cap (else budget=0=unlimited).
    # The PRODUCTION d3 config caps at budget 10, so measure the beam against a budgeted baseline, not an
    # uncapped one -- with a cap the beam can spend the same budget on fewer/deeper lines (quality) vs frontier.
    _budget = os.environ.get("ESC_AB_BUDGET")
    if _budget:
        cmd += ["--budget-ms", _budget]
    t0 = time.time()
    out = subprocess.run(cmd, capture_output=True, text=True, env=env)
    wall = time.time() - t0
    m = AVG.search(out.stdout)
    lp = float(m.group(1)) if m else None
    return (lp, wall / games * 1000.0)   # (LP, ms/game)


def main():
    if len(sys.argv) < 2:
        print(__doc__); sys.exit(2)
    decks = [d for d in sys.argv[1].split(",") if d in DECKS]
    if not decks:
        print(f"unknown deck(s); pick from {list(DECKS)}"); sys.exit(2)
    nseeds = int(sys.argv[2]) if len(sys.argv) > 2 else 6
    games = int(sys.argv[3]) if len(sys.argv) > 3 else 300
    seeds = SEEDS[:nseeds]
    open(LOG, "w").write("")
    say(f"=== escalation fallback A/B  decks={decks} seeds={seeds} games={games} ===")
    say(f"    variants: {list(VARIANTS)}")

    tasks = [(dk, v, s) for dk in decks for v in VARIANTS for s in seeds]
    res = {}
    with ThreadPoolExecutor(max_workers=int(os.environ.get("ESC_AB_WORKERS", "6"))) as ex:
        futs = {ex.submit(run, DECKS[dk], s, games, VARIANTS[v]): (dk, v, s) for (dk, v, s) in tasks}
        done = 0
        for fut in as_completed(futs):
            res[futs[fut]] = fut.result()
            done += 1
            if done % 10 == 0:
                say(f"  ... {done}/{len(tasks)} runs done")
    say("")

    for dk in decks:
        say(f"### {dk} (bare = shipped value_play; LP lower=better, ms lower=faster) ###")
        base = {s: res.get((dk, "baseline", s)) for s in seeds}
        for v in VARIANTS:
            if v == "baseline":
                rows = [base[s] for s in seeds if base[s] and base[s][0] is not None]
                if rows:
                    say(f"  {v:15} LP={sum(r[0] for r in rows)/len(rows):.4f}  "
                        f"ms/game={sum(r[1] for r in rows)/len(rows):.2f}  (reference)")
                continue
            dlp, dms, nb, wl, ws, be = [], [], 0, 0, 0, 0
            for s in seeds:
                a = base[s]; b = res.get((dk, v, s))
                if not a or not b or a[0] is None or b[0] is None:
                    continue
                dl = round(b[0] - a[0], 5); dm = b[1] - a[1]
                dlp.append(dl); dms.append(dm); nb += 1
                if dl < -1e-9: be += 1          # variant lower LP = better quality
                elif dl > 1e-9: wl += 1
                if dm < -1e-9: ws += 1          # variant lower ms = faster
            if not dlp:
                say(f"  {v:15} (no results)"); continue
            mlp = sum(dlp) / len(dlp); mms = sum(dms) / len(dms)
            say(f"  {v:15} dLP={mlp:+.4f} (better:{be} worse:{wl} of {nb})  "
                f"dms/game={mms:+.2f} (faster:{ws}/{nb})")
    say("=== DONE ===")


if __name__ == "__main__":
    main()
