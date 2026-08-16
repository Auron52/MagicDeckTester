#!/usr/bin/env python3
"""Full value-leaf x heuristic DEPTH MATRIX (UNBOUNDED) with cost, to (1) separate decks where the value
leaf is ~EXACT (no heuristic needed) from those that GENUINELY need the heuristic, and (2) calibrate the
per-model "trust depth": at what value-leaf depth does it match heuristic-at-D?

CRITICAL (2026-07-11 fix): measures the PURE value-leaf by DISABLING the hybrid redo (--value-min-depth 0,
env MTG_VALUE_MIN_DEPTH=0). The earlier run used MIN_DEPTH=5, which made every committed depth < 5 RE-RUN
the heuristic -- so V3/V4 were the HYBRID (heuristic on the leaf-dependent games) and only V5 was the raw
leaf. That confound produced the spurious "value-leaf exact at d3/d4, worse only at d5" + the impossible
"V4 beats V5" (a deeper search cannot be worse unless d3/d4 were secretly the heuristic). With MIN_DEPTH=0
every V_d is the raw value-leaf at depth d, so the true crossover (where the heuristic wins) is visible.

All UNBOUNDED (budget 0) so each config reaches its nominal depth. Per deck/seed runs heuristic (value OFF)
at each --hdepths and value-leaf (value ON, MIN_DEPTH=<flag>/a8) at each --vdepths; records LP (loss=mt+1,
lower=better) and wall ms/game. Prints per-deck means + the Vi-Hj difference matrix (negative = value-leaf
better). The binary default is value-ON, so the OFF arm sets MTG_VALUE_MODEL=0 EXPLICITLY. Include low
--vdepths (1,2) to see the leaf-dominated regime where the heuristic's full rollout should win.
See learned-d0-policy.md.

    scripts/valueleaf_depth_matrix.py --games 1000 --seeds 8008 9009 10010 11011 \
        --hdepths 1 2 3 4 5 --vdepths 1 2 3 4 5 --value-min-depth 0
"""
import argparse, glob, json, os, re, subprocess, sys, threading, time
from concurrent.futures import ThreadPoolExecutor, wait, FIRST_COMPLETED

MTG = "build/Release/mtg"

# Deck locations come from scripts/deck_registry.py -- pure discovery of decks/*/, no list to maintain.
# There used to be two hand-written dicts here (DECKS and PROFILES) plus an auto-discovery pass that
# patched around them. An unlisted deck did not error: the H-cell ladder is guarded on
# os.path.exists(<value.json>), so a missing entry quietly ran every H cell on the slow path.
#
# A "<deck>_staged" key is the same decklist and the same profile pointing at a model that is NOT yet
# installed in the deck folder (logs/eval/<stem>.value.STAGED.json); the model reaches the engine via
# MTG_VALUE_PROFILE. That is the order the pipeline requires -- measure, then adopt. H cells set
# MTG_VALUE_MODEL=0, so a staged key's H cells are byte-identical to the plain key's; only V moves.
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import deck_registry

_REG = deck_registry.discover()
# key -> (decklist, value-model path, max_turns).  PROFILES is the same keys -> the deck profile.
# Attaching the profile is not optional: measuring profile-less silently describes a deck we do not
# ship, which invalidated every value-leaf table in this repo once already.
DECKS, PROFILES = {}, {}
for _k, _d in _REG.items():
    DECKS[_k] = (_d.deck_file, _d.value, _d.max_turns)
    DECKS[_k + deck_registry.STAGED_SUFFIX] = (_d.deck_file, _d.staged, _d.max_turns)
    PROFILES[_k] = _d.profile
    PROFILES[_k + deck_registry.STAGED_SUFFIX] = _d.profile


# NOTE: the single-invocation helper `run()` that used to live here is DELETED. It ran ONE `mtg` per
# (deck, depth, seed) and was the shared engine of two loop-shaped drivers -- the monolithic branch of
# this file and scripts/attic/valueleaf_incremental.py -- both of which are now gone. Anything that
# needs the matrix goes through run_incremental() -> run_pool(): one manifest, one queue, one tail.

# ==================== INCREMENTAL / TRACTABILITY-AWARE MODE (--incremental) ====================
# The way to build a depth table on decks with a heavy search tail (antilife unbounded deep search has games
# that explode into multi-hour trees). Instead of one monolithic run per (seed,depth) that only reports at the
# very END, sweep EVERY cell in small (50-game) BATCHES, round-robin / breadth-first, so the WHOLE table exists
# at 50 games, then 100, ... Each batch is written the instant it lands (resumable, stoppable, ZERO loss).
#   - TRACTABILITY as we go: from a cell's first (and each) batch, if it is too slow to be production-usable at
#     that depth (sec/game over a threshold), mark it INTRACTABLE and cap it at a small REFERENCE sample -- it
#     still gets a table value, we just don't burn compute pushing an unusable cell to the full target.
#   - WORK-STEALING pool: a cell stuck on a long-tail game ties up ONE core while the rest keep churning other
#     cells' batches, so the box stays full and results keep streaming.
# Batches reproduce a full run EXACTLY: games [off,off+batch) via `--seed (seed+off) --game-index off` (seed_gi
# = seed+global_i, spawn = global_i), verified batched==monolithic. NOTE each concurrent worker is a SEPARATE
# mtg process that loads the deck's keep model; antilife is ~1GB/process (post the share+stream-parse memory
# fix, 82859f7), so --workers 24 ~ 24GB -- was ~6GB/proc before the fix (would OOM). Tune --workers to RAM.

_slow_lock = threading.Lock()   # run_batch is called from a thread pool


def run_batch(deck_file, mt, depth, seed, offset, batch, value_on, value_min_depth, prof,
              deck_profile=None):
    """Run global games [offset, offset+batch) for this cell's base `seed`. Returns (lp, wall_s, games)."""
    env = dict(os.environ)
    for k in ("MTG_EVAL_MODEL","MTG_EVAL_PROFILE","MTG_VALUE_MODEL","MTG_VALUE_PROFILE",
              "MTG_NC_SEARCH","MTG_VALUE_MIN_DEPTH","MTG_VALUE_REDO_MODE","MTG_VALUE_STARTGATE_ALPHA"):
        env.pop(k, None)
    if value_on:
        env["MTG_VALUE_MODEL"]="1"; env["MTG_VALUE_PROFILE"]=prof
        env["MTG_VALUE_MIN_DEPTH"]=str(value_min_depth); env["MTG_VALUE_STARTGATE_ALPHA"]="8"
    else:
        env["MTG_VALUE_MODEL"]="0"
        # H CELL, LADDERED ON THE CHEAP LEAF. The committed pass is still pure heuristic
        # (MTG_VALUE_MODEL=0); only the ladder's warm-up passes use the value leaf, and their results
        # provably cannot reach the committed line -- verified 21/21 cells byte-identical on avg AND
        # play digest, plus 14 cells avg-identical under the MTG_PROFILE counters. Costs 1.35x-84.8x
        # less search work, most of it at d5, which is the cell this table could never afford.
        # Attaching the profile is required: without a model the warm-up passes have nothing cheap to
        # fall back to and this silently reverts to the slow path (a perf cliff, not a wrong number).
        # Cells run unbounded, which is the regime the identity holds in. See
        # docs/design/value-leaf-regeneration-queue.md 8.3(3).
        if prof and os.path.exists(prof):
            env["MTG_VALUE_PROFILE"]=prof
            env["MTG_LADDER_VALUE_LEAF"]="1"
    cmd=[MTG, deck_file, "--seed", str(seed+offset), "--game-index", str(offset), "--games", str(batch),
         "--max-turns", str(mt), "--threads", "1", "--ignore-play-profile", "--depth", str(depth)]
    if deck_profile: cmd += ["--profile", deck_profile]
    t0=time.time(); _cp=subprocess.run(cmd,capture_output=True,text=True,env=env); out=_cp.stdout; wall=time.time()-t0
    # SLOW-GAME capture. The engine reports any game over MTG_SLOW_GAME_MS on STDERR, which this
    # function used to discard -- so a matrix run knew a CELL was slow but never which GAME. Each
    # line already carries a self-contained repro; tag it with the cell so a pathological game can be
    # attributed to an arm/depth/seed and replayed directly. Append-only, so a resumed run accumulates.
    if os.environ.get("MTG_SLOW_GAME_MS") and _cp.stderr:
        arm = "V" if value_on else "H"
        rows = [l for l in _cp.stderr.splitlines() if "SLOW-GAME" in l]
        if rows:
            with _slow_lock:
                with open(os.environ.get("MTG_SLOW_GAME_LOG", "logs/eval/slow_games.log"), "a") as fh:
                    for l in rows:
                        fh.write("%s%d seed=%d  %s\n" % (arm, depth, seed, l.strip()))
    pm=re.search(r"Games played\s*:\s*(\d+)",out); p=int(pm.group(1)) if pm else 0
    m=re.search(r"avg \(turns\)\s*:\s*([\d.]+)",out); lp=float(m.group(1)) if m else float("nan")
    return lp, wall, p


def _cell_key(c): return "%s|%s|%d|%d" % (c["deck"], c["arm"], c["depth"], c["seed"])


# ==================== CHUNK-LEVEL STATE (the unit of consistency) ====================
# A cell's results are stored as the LIST OF CHUNKS that produced them, not as a running total:
#
#     {"off": 375, "n": 25, "lp": 5.31, "ms": 9508.2, "src": "<tree hash of src/>"}
#
# Games are addressed by OFFSET, and the offset is what fixes identity: chunk [off,off+n) always runs
# `--seed (seed+off) --game-index off`, so game 380 of a cell is the SAME GAME in every run, at every
# depth, on both arms. That is what makes a chunk replaceable -- and replaceable at chunk granularity
# is the whole point, because it is what lets a play change be absorbed by re-running the affected
# GAMES rather than the affected CELLS (26 cells / 10,400 games vs 24 chunks / ~590).
#
# The flat fields (games/lp_sum/batches/ms) are DERIVED and kept in sync for other readers
# (scripts/valueleaf.sh status, emit_table). chunks[] is the source of truth.
#
# It also fixes a bias the adaptive chunk size introduced: the table averaged BATCH MEANS, which is
# only correct while every batch is the same size. A 3-game chunk counted as much as a 25-game one.
# cell_mean() weights by games.
def _refresh(c):
    ch = c["chunks"]
    c["games"]   = sum(x["n"] for x in ch)
    c["batches"] = len(ch)
    c["ms"]      = sum(x["ms"] for x in ch)
    c["lp_mean"] = (sum(x["lp"] * x["n"] for x in ch) / c["games"]) if c["games"] else float("nan")
    # Compat shim: readers that predate chunks compute lp_sum/batches to get the mean.
    c["lp_sum"]  = c["lp_mean"] * c["batches"] if c["games"] else 0.0
    return c


# ==================== PER-GAME RESULTS (what makes a chunk divisible) ====================
# A chunk additionally carries `g`: the WIN TURN of each game it covers, in offset order --
#
#     {"off": 375, "n": 25, "lp": 5.31, "ms": 9508.2, "src": "<hash>", "g": [5, 6, -1, ...]}
#
# read from the `<job>.wins` file `--game-log-dir` writes (`index win_turn digest` per line).
#
# This is the ENABLER for everything the chunk mean cannot express, and it is why the mean was
# a problem rather than a compression:
#
#   * A chunk mean cannot be SPLIT. Keeping part of a chunk means the surviving piece inherits the
#     whole chunk's mean over a different game set -- fabricated data (two predecessors of
#     _chunk_inside did exactly that). So retention had to be whole-chunk, and one ragged condemned
#     chunk could force ~37% of blocks to be discarded to drop 2% of games.
#   * It cannot express a SKIPPED game at all, which is what degenerate-game abandonment needs:
#     with per-game results a skip is an analysis-time FILTER over data already on disk, not a
#     re-run and not a retroactive edit.
#   * Comparisons intersect on GAME SETS rather than counts, so "compare only games both cells
#     played" stops being a hand computation done outside the driver -- which is where the
#     FiveColour V6-vs-H5 sign error came from (row means over unequal sets).
#
# Cost is ~250 KB for a 22,400-game matrix. `lp` STAYS the engine's own reported average and stays
# authoritative; `g` is verified against it on arrival (_wins_check) rather than replacing it, so
# this change is purely additive -- no existing number moves.
def _mt_of(c):
    return DECKS[c["deck"]][2]


def _lp_value(wt, mt):
    """One game's loss-penalized score, matching the engine's ComputeAvgTurns EXACTLY
    (runner/GoldFishRunner.h): a win scores its turn, anything <= 0 scores max_turns + 1.
    Divergence here would silently shift every filtered mean off the engine's own scale."""
    return float(wt) if wt > 0 else float(mt + 1)


def _chunk_games(x):
    """[(offset, win_turn)] for a chunk that carries per-game results; None if it predates them.

    Stored as explicit (offset, win_turn) PAIRS rather than a positional list, because a chunk is no
    longer necessarily contiguous: an ABANDONED game is dropped from the job's output, so a chunk can
    hold offsets 25,27,29,... The pair form makes that representable instead of silently
    re-aligning the survivors onto the wrong games."""
    g = x.get("g")
    if not g: return None
    try:
        return [(int(o), int(w)) for o, w in g]
    except (TypeError, ValueError):
        return None


def _read_wins(winsdir, nm):
    """Per-game results for one chunk from `<winsdir>/<nm>.wins`, as [(global_offset, win_turn)].

    Column 0 is the GLOBAL game index (`game_index + position`), which is what makes a SHORT file
    still usable: skipped and voided games are dropped before the file is written, so a positional
    read would silently shift every survivor onto its neighbour's identity. Reading the index
    instead means a chunk that lost games to abandonment reports exactly the games it kept."""
    try:
        out = []
        with open(os.path.join(winsdir, nm + ".wins")) as fh:
            for line in fh:
                f = line.split()
                if len(f) >= 2: out.append((int(f[0]), int(f[1])))
        return out or None
    except (OSError, ValueError):
        return None


def _read_abandoned(winsdir, nm):
    """Global indices the pool ABANDONED at the per-game work ceiling, from `<winsdir>/<nm>.abandoned`.

    Written by the engine only when there are any (BatchJobResult::abandoned), and written BEFORE the
    job's result line is printed, so it is on disk by the time this is read. Missing file = none."""
    try:
        with open(os.path.join(winsdir, nm + ".abandoned")) as fh:
            return [int(l) for l in fh if l.strip()]
    except (OSError, ValueError):
        return []


def _per_game(c):
    """{offset: loss-penalized score} for every game this cell holds a per-game record of."""
    mt = _mt_of(c)
    out = {}
    for x in c["chunks"]:
        for o, w in (x.get("g") or ()):
            out[int(o)] = _lp_value(w, mt)
    return out


def dead_rung(cells, deck, arm, depth, thresh, min_pairs, coverage):
    """Has depth `depth` stopped buying anything over `depth-1`? -> (verdict, detail).

    CONDEMN A RUNG ON QUALITY, NOT ONLY ON COST. A rung whose extra depth changes no decision is
    pure expense: the table already knows what it says, and every further game of it is spent
    confirming a number that is equal to the one below. Across 8 decks the rung deltas are bimodal
    with a real gap -- nothing lands between 0.0044 and 0.0078 -- so 0.0075 separates 'dead' from
    'live' with margin on both sides.

    AN EQUIVALENCE TEST, NOT A POINT ESTIMATE. "the delta is below the threshold" condemns live
    rungs on noise: with enough variance any true effect measures small often enough. The correct
    statement is that the effect is bounded -- the one-sided upper confidence bound on the
    improvement sits below the threshold -- which needs the sample to be large enough to make that
    claim rather than merely small enough to look flat.

    PAIRED, on the games both rungs actually hold. The deltas at stake (0.003) are an order of
    magnitude under the between-GAME spread, so an unpaired comparison would be measuring which
    hands each rung happened to draw."""
    deep    = [c for c in cells if c["deck"]==deck and c["arm"]==arm and c["depth"]==depth]
    shallow = [c for c in cells if c["deck"]==deck and c["arm"]==arm and c["depth"]==depth-1]
    if not deep or not shallow: return False, None
    diffs = []
    n_deep = n_shal = 0
    for dc in deep:
        sc = next((s for s in shallow if s["seed"]==dc["seed"]), None)
        if sc is None: continue
        dg, sg = _per_game(dc), _per_game(sc)
        n_deep += len(dg); n_shal += len(sg)
        # improvement = shallower minus deeper, so POSITIVE means the extra depth helped
        # (lower loss-penalized score is better).
        for o in set(dg) & set(sg): diffs.append(sg[o] - dg[o])
    n = len(diffs)
    if n < min_pairs: return False, None
    # Not enough of the two rungs' games overlap to call this a like-for-like comparison -- a fluke
    # early sample must not be able to condemn a live rung.
    if n < coverage * max(1, min(n_deep, n_shal)): return False, None
    mean = sum(diffs)/n
    var  = sum((d-mean)**2 for d in diffs)/(n-1) if n > 1 else 0.0
    se   = (var/n) ** 0.5
    # RESOLUTION FLOOR. A sample in which the two rungs agreed on EVERY game has se == 0, so the
    # bound is 0 and the rung is certified dead against any threshold from any sample size -- which
    # is exactly what happened on burn (2026-08-15): all nine rungs condemned at the 201-pair
    # minimum, every one reporting "improvement +0.0000, se 0.0000". Zero observed variance is not
    # zero uncertainty. With k=0 differing games in n pairs the rule of three bounds the rate of a
    # differing game at 3/n, and a game that does differ moves this score by at least one whole turn
    # (win turns are integers, a loss scores max_turns+1), so the effect cannot honestly be bounded
    # below 3*step/n however flat the sample looks -- at n=201 that is 0.0149 turns, twice the
    # threshold the sample was claiming to clear.
    #
    # Applied at EVERY k rather than only at 0, because the normal bound understates a
    # handful-of-events sample too (k=1 gives ~2.6/n where the Poisson bound is ~4.7/n). Its
    # practical effect is to make a near-degenerate rung wait for ~400 paired games before it can be
    # condemned, which is the sample a 0.0075-turn claim actually needs; a rung with real variance
    # is unaffected (FiveColour's H4->H5 floor at n=1600 is 0.0019).
    step  = min((abs(d) for d in diffs if d), default=1.0)
    upper = max(mean + 1.645*se, 3.0*step/n)          # one-sided 95%, floored at the resolution
    return (upper < thresh), {"n":n, "improvement":mean, "se":se, "upper":upper}


def cell_mean(c): return c["lp_mean"]


def _covered(c):
    """The set of game offsets this cell holds a result for."""
    return {o for x in c["chunks"] for o in _chunk_offsets(x)}


def _missing(c, target, skip=()):
    """The offsets below `target` still to run -- a list of [lo,hi).

    Neither held nor SKIPPED. A skipped offset is one the pool refused to finish (abandoned by the
    work ceiling, or dropped with a condemned cell); re-queueing it would abandon it again at full
    cost, so it counts as settled here and the cell's target is extended instead (see `target`).

    Computed from the offsets the cell actually holds rather than from a game count, because after a
    resync -- or an abandonment -- coverage is not a prefix and the gaps can be anywhere."""
    have = _covered(c) | set(skip)
    out, lo = [], None
    for o in range(target):
        if o in have:
            if lo is not None: out.append((lo, o)); lo = None
        elif lo is None:
            lo = o
    if lo is not None: out.append((lo, target))
    return out


def _chunk_offsets(x):
    """The game offsets this chunk actually holds.

    Derived from the per-game pairs where they exist, because a chunk is only contiguous when every
    game in its range produced a result -- an abandoned game leaves a hole. Legacy chunks stored
    only a range, so for them off..off+n IS the coverage."""
    g = x.get("g")
    if g:
        try:
            return [int(o) for o, _ in g]
        except (TypeError, ValueError):
            pass
    return range(x["off"], x["off"] + x["n"])


def _chunk_inside(x, allowed):
    """CHUNK ATOMICITY: a chunk survives only if EVERY offset it covers is allowed.

    A chunk stores a MEAN (`lp`) over its `n` games, never the games themselves, so there is no honest
    way to keep part of one -- the surviving piece would have to inherit the whole chunk's mean. Two
    predecessors of this function (_split_chunk, _chunk_minus) did exactly that and documented it as a
    small approximation, on the reasoning that grid-aligned chunks make a mid-chunk cut unreachable.

    A CONDEMNED CELL BREAKS THAT ALIGNMENT. It stops mid-chunk, leaving a ragged length (FiveColour,
    2026-08-13: H6 stopped at n=7 on one seed and n=4 on another), and those ragged offsets propagate
    into the keep set for the entire seed group -- so every 25-game chunk in the group gets cut down
    to 7 games carrying a 25-game mean. On the live run that was 26 chunks of fabricated `lp`, in the
    exact quantity the table reports.

    So retention granularity is now the STORAGE granularity: whole chunks, or nothing.

    A chunk carrying per-game results (`g`) is NOT subject to this -- it can be restricted honestly,
    see _chunk_restrict. This predicate is the rule for the legacy chunks that only stored a mean."""
    return all(o in allowed for o in _chunk_offsets(x))


def _chunk_restrict(x, allowed, mt):
    """Narrow a chunk to `allowed` offsets. Returns the LIST of surviving chunks (empty if none).

    With per-game results this is exact: recompute the mean over the games that actually remain,
    on the engine's own scale (_lp_value). Without them the only honest answers are ALL or NOTHING
    (_chunk_inside), because the surviving piece would otherwise inherit a mean over games it does
    not contain -- so a legacy chunk still goes whole.

    What this buys: 149 degenerate games spread over ~321 blocks put at least one in ~37% of them,
    so whole-chunk retention discards a third of the sample to drop 2% of the games -- and the
    signal lives in 1-4 blocks out of ~50-60, which is the one thing that cannot afford it.

    `ms` is apportioned pro-rata. That is an approximation and deliberately so: the wall time of the
    dropped games is exactly what we do NOT have per-game, and the field feeds cost reporting, never
    a result. Whole-chunk retention had the same property (it dropped the time with the games)."""
    games = _chunk_games(x)
    if games is None:
        return [x] if _chunk_inside(x, allowed) else []
    keep = [(o, w) for o, w in games if o in allowed]
    if not keep:
        return []
    if len(keep) == len(games):
        return [x]
    # One chunk out, not one per contiguous run: the pair form carries each game's own offset, so a
    # hole needs no splitting. `off` stays the chunk's first surviving game purely for ordering.
    return [{"off": keep[0][0], "n": len(keep), "g": [[o, w] for o, w in keep], "src": x.get("src"),
             "lp": sum(_lp_value(w, mt) for _, w in keep) / len(keep),
             "ms": x["ms"] * len(keep) / len(games)}]


def _reference_only(c):
    """A CONDEMNED cell is reference-only: capped at `reference_target`, flagged `*` in the table, and
    never part of a per-game cross-cell comparison.

    It is therefore an OBSERVER of the cross-cell engine invariant, not a participant -- it neither
    votes on which games survive nor forces a drop on the comparable cells. Letting it participate
    is how a row that can never be completed came to hold twelve full rows hostage: H6 was condemned
    by the single-game guard at 7 and 4 games, could never reach its own 50-game target, and so stayed
    'unfinished' forever, capping its seed's banking at its own 7 offsets."""
    return bool(c.get("intractable"))


def _offset_src_conflicts(cells, comparable_only=False):
    """(deck,seed) -> the set of game offsets measured on DIFFERENT engines in different cells.

    A cell may legitimately be a MIX of engines (resync_engine_change deliberately keeps the games
    below the split on their original engine). What may NOT vary is the engine for a GIVEN GAME across
    cells, because every way the table is read -- down a column (does depth d+1 beat depth d?) and
    across arms (V vs H at one depth) -- is a per-game comparison. If game 380 ran on engine A at V8
    and on engine B at H5, that single difference is attributed to DEPTH when it is really a code
    change.

    `comparable_only` drops reference-only cells from the vote -- what ENFORCEMENT acts on, since a
    condemned row cannot be compared per-game anyway. The read-side banner in emit_table deliberately
    leaves it False: detection stays total even where enforcement is scoped, so mixing inside a
    reference row is still named in the artifact rather than silently tolerated."""
    per_seed = {}
    for c in cells:
        if comparable_only and _reference_only(c): continue
        by_off = per_seed.setdefault((c["deck"], c["seed"]), {})
        for x in c["chunks"]:
            for o in _chunk_offsets(x):
                by_off.setdefault(o, set()).add(x.get("src"))
    return {k: {o for o, s in v.items() if len(s) > 1} for k, v in per_seed.items()}


def enforce_offset_src_agreement(cells, log=print):
    """Drop every game whose engine DISAGREES across the cells of its (deck,seed), so it is re-run
    once, on one engine, everywhere it is needed.

    This is the invariant resync_engine_change intends but cannot maintain alone. That function picks
    a single split point B per seed from the state as it stands at ONE resume, and heals everything at
    or above it. It is correct for the change it sees -- but the src can change AGAIN before the games
    it just scheduled have all landed, and the second change is never inspected. That is exactly how
    the pre-merge FiveColour matrix ended up with 63 games (H5 s10010, H4/H5 s11011) on a third engine
    that no V cell ever ran: those three cells were the slowest, so they were the ones still unfinished
    when the source moved, and the contamination therefore landed ONLY on the deep heuristic cells it
    made look good. Checking per OFFSET rather than per prefix is what makes this idempotent: it re-
    derives the invariant from the state itself, so it holds no matter how many times the src moved."""
    conflicts = _offset_src_conflicts(cells, comparable_only=True)
    groups = {}
    for c in cells: groups.setdefault((c["deck"], c["seed"]), []).append(c)
    plan = []
    for key, bad in sorted(conflicts.items(), key=lambda kv: (kv[0][0], kv[0][1])):
        if not bad: continue
        for c in groups[key]:
            keep, dropped, mt = [], 0, _mt_of(c)
            for x in sorted(c["chunks"], key=lambda x: x["off"]):
                # Drop the DISPUTED GAMES. With per-game results that is exactly them; a legacy
                # mean-only chunk still goes whole (_chunk_restrict picks per chunk).
                # Widening a drop can never create a conflict -- a conflict needs two cells HOLDING
                # one offset on different engines -- so this needs no fixpoint, unlike the keep side.
                if any(o in bad for o in _chunk_offsets(x)):
                    surv = _chunk_restrict(x, {o for o in _chunk_offsets(x) if o not in bad}, mt)
                    dropped += x["n"] - sum(s["n"] for s in surv)
                    keep.extend(surv); continue
                keep.append(x)
            if dropped:
                c["chunks"] = keep; _refresh(c)
                plan.append((key[0], key[1], "%s%d" % (c["arm"], c["depth"]), dropped))
        log("resync: s%d has %d game(s) measured on DIFFERENT engines across cells -- re-running them "
            "everywhere (offsets %s)" % (key[1], len(bad), _fmt_ranges(sorted(bad))))
    if plan:
        for deck, seed, cell, n in plan:
            log("  s%-6d %-4s drop %d mismatched game(s)" % (seed, cell, n))
    return plan


def _fmt_ranges(offs):
    """[375,376,...,393] -> '[375,394)' -- compact enough to log every conflicting offset."""
    out, i = [], 0
    while i < len(offs):
        j = i
        while j + 1 < len(offs) and offs[j + 1] == offs[j] + 1: j += 1
        out.append("[%d,%d)" % (offs[i], offs[j] + 1)); i = j + 1
    return " ".join(out)


def _src_fingerprint():
    """Engine identity = the tree hash of src/. Same value => same play, which is the property the
    freeze exists to protect. `git rev-parse HEAD:src` is what scripts/valueleaf.sh records too."""
    try:
        r = subprocess.run(["git", "rev-parse", "HEAD:src"], capture_output=True, text=True)
        return r.stdout.strip() or None
    except Exception:
        return None


def resync_engine_change(cells, target, src_now, log=print):
    """Absorb a play change WITHOUT throwing the run away: re-run the affected GAMES everywhere they
    were already generated, and keep everything below them.

    The rule (user, 2026-08-09): generation may continue under a play mismatch, but any game chunk
    that is not completely done must be regenerated -- "across all entries of the table that have
    generated them already, so some cells will not need to generate them".

    Why the chunk and not the seed. The table is read two ways: down a column (does depth d+1 beat
    depth d?) and across arms (V vs H at the same depth). Both are PER-GAME comparisons averaged over
    the seed. So what has to hold is that game 380 was measured on ONE engine at every depth and in
    both arms -- not that the whole seed was. Splitting at an offset gives exactly that: offsets
    [0,B) are engine A everywhere, [B,target) are engine B everywhere, and every column and every
    arm-difference is computed from games that agree. Re-running whole cells buys nothing extra and
    cost 140x more here (26 cells / 10,400 games against 24 chunks / ~590).

    The result is a SLIGHTLY MIXED matrix and that is deliberate (user, 2026-08-09): the prefix
    below B keeps its original engine in every cell, and only the games that were never finished
    are regenerated -- in each cell where they exist. Re-running whole cells to remove the mixing
    would cost 10,400 games against 587 for the same per-game agreement across cells.

    KEEP EVERY FULL SET, CONTIGUOUS OR NOT (user, 2026-08-12). What has to hold is per GAME, so the
    question for offset o is simply: does EVERY cell that still owes work already hold o? If yes, o was
    measured on the old engine everywhere it appears and no future game can mix it, so it stays --
    whether or not the offsets below it survived. If no, some cell will run o on the NEW engine, which
    would make o mixed across cells, so o goes everywhere.

    This used to be a CONTIGUOUS prefix B = min over incomplete cells of the run of offsets from 0,
    truncated at the first hole. That is sound but needlessly lossy, because cells do not finish in
    order: chunks within a level land at different times, so a perfectly good full set sitting above an
    in-flight hole was thrown away with it. With set-completion ordering (build_queue) holes are
    normal and short-lived, which makes the difference routine rather than exotic.

    WHO VOTES. A cell at target is excluded -- it owes nothing, so it can neither re-run nor mix an
    offset -- and so is a REFERENCE-ONLY cell (see _reference_only), which is never compared per-game
    in the first place. Both remain SUBJECT to the drop, because an offset an unfinished comparable
    cell will redo must not survive anywhere.

    WHAT SURVIVES IS A GAME (2026-08-14), where per-game results exist. Retention used to be
    chunk-atomic because the state stored one mean per chunk, so keeping part of one meant inventing
    the surviving piece's score (see _chunk_inside) -- which meant a single straddling game cost 25.
    Chunks carrying `g` are now restricted offset-wise instead (_chunk_restrict); chunks written
    before that still go whole. The FIXPOINT stays either way: a legacy chunk dropped for straddling
    the boundary still shrinks its cell's coverage, which can strand a chunk in another cell.
    """
    if not src_now: log("resync: no src fingerprint (not a git tree?) -- skipping"); return []
    groups = {}
    for c in cells: groups.setdefault((c["deck"], c["seed"]), []).append(c)

    def covered(c):
        """The offsets this cell holds a result for, by whatever engine."""
        return {o for x in c["chunks"] for o in range(x["off"], x["off"] + x["n"])}

    plan = []
    for (deck, seed), cs in sorted(groups.items()):
        # Reference-only cells get no vote (see _reference_only) -- they are still SUBJECT to the drop.
        incomplete = [c for c in cs if c["games"] < target(c) and not _reference_only(c)]
        if not incomplete: continue                  # seed complete => internally consistent

        def survives(c, allowed):
            """The offsets c would still hold after a drop bounded by `allowed`.

            Per-game chunks survive OFFSET-WISE (a chunk keeps the games inside `allowed` and loses
            only the rest); a legacy mean-only chunk survives whole or not at all. Mirrors exactly
            what the drop loop below does, which is what makes the fixpoint honest."""
            out = set()
            for x in c["chunks"]:
                if x.get("src") == src_now:
                    out.update(_chunk_offsets(x))
                elif _chunk_games(x) is not None:
                    out.update(o for o in _chunk_offsets(x) if o in allowed)
                elif _chunk_inside(x, allowed):
                    out.update(_chunk_offsets(x))
            return out

        # FIXPOINT. Chunk atomicity and the intersection are mutually dependent: dropping a chunk
        # because it straddles the boundary shrinks that cell's coverage, which shrinks the
        # intersection, which can strand a chunk in some OTHER cell. One pass would leave cell A
        # having dropped offsets cell B still holds -- precisely the cross-cell mixing this exists to
        # prevent -- so iterate until it stops moving. It terminates because `survives` returns a
        # subset of `allowed`, making the sequence strictly decreasing.
        keepable = set.intersection(*(covered(c) for c in incomplete))
        while True:
            nxt = set.intersection(*(survives(c, keepable) for c in incomplete))
            if nxt == keepable: break
            keepable = nxt

        for c in cs:
            keep, dropped, mt = [], 0, _mt_of(c)
            for x in sorted(c["chunks"], key=lambda x: x["off"]):
                if x.get("src") == src_now:          # already the current engine: nothing to absorb
                    keep.append(x); continue
                surv = _chunk_restrict(x, keepable, mt)
                dropped += x["n"] - sum(s["n"] for s in surv)
                keep.extend(surv)
            if dropped:
                c["chunks"] = keep; _refresh(c)
                plan.append((deck, seed, "%s%d" % (c["arm"], c["depth"]), dropped))
    if plan:
        tot = sum(p[3] for p in plan)
        log("resync: src changed since these results were written -- %d cells, %d games to redo "
            "(every offset held by all unfinished cells is kept)" % (len(plan), tot))
        for deck, seed, cell, n in plan:
            log("  s%-6d %-4s drop %d game(s) not in a full set" % (seed, cell, n))
    return plan


def apply_skiplist(cells, skipped, log=print):
    """Drop every skipped game from every cell of its (deck, seed), and report what that cost.

    Detection is per-cell -- a game degenerate at H5 may be trivial at V3, so only the cell that hit
    the ceiling knows. Application has to be GLOBAL, because the table is read as a set of per-game
    comparisons down a column and across arms: if H5 excludes game 380 and V6 keeps it, the row
    difference is once again computed over unequal game sets, which is the exact defect per-game
    storage exists to end. So the union of every cell's abandonments is what every cell excludes.

    With per-game results this is a FILTER over data already on disk -- no re-run, no retroactive
    edit -- which is why it can be applied at any point without invalidating anything."""
    if not skipped: return 0
    total = 0
    for c in cells:
        skip = skipped.get((c["deck"], c["seed"]))
        if not skip: continue
        mt, keep, dropped = _mt_of(c), [], 0
        for x in c["chunks"]:
            surv = _chunk_restrict(x, {o for o in _chunk_offsets(x) if o not in skip}, mt)
            dropped += x["n"] - sum(s["n"] for s in surv)
            keep.extend(surv)
        if dropped:
            c["chunks"] = keep; _refresh(c); total += dropped
            log("  skip %s%d s%d: %d game(s) excluded" % (c["arm"], c["depth"], c["seed"], dropped))
    return total


def emit_table(cells, args, skipped=None):
    """Parser-compatible legacy table (mean over seeds) + per-cell game counts, rewritten each batch."""
    L=["",
       "===== DEPTH MATRIX (UNBOUNDED, INCREMENTAL)  games=%d seeds=%s value_min_depth=%d  %s  hgames=%d =====" % (
         args.target, args.seeds, args.value_min_depth,
         "[PURE value-leaf, no redo]" if args.value_min_depth==0 else "[HYBRID redo]", args.target)]
    for dname in args.decks:
        dc=[c for c in cells if c["deck"]==dname]
        nseed=len(set(c["seed"] for c in dc if c["batches"]>0))
        L.append("---- %s (mean over %d seeds) ----" % (dname, nseed))
        # COMPARABILITY IS THE SKIP LIST'S JOB, not this function's. Every cell of a (deck, seed)
        # holds the SAME game set because a game abandoned in ANY cell is dropped from ALL of them
        # (apply_skiplist, re-applied as the list grows). A cell that stopped early is marked and
        # excluded from the comparison instead. So a plain mean is the right thing here -- and if
        # the sets ever diverge, the guard below SAYS SO rather than quietly compensating, because
        # a second mechanism papering over the first is how the divergence would go unnoticed.
        def row(arm, depths, label):
            parts=[]; ann=[]
            for d in depths:
                cs=[c for c in dc if c["arm"]==arm and c["depth"]==d and c["batches"]>0]
                if not cs: continue
                lp=sum(cell_mean(c) for c in cs)/len(cs)
                ms=sum(c["ms"]/max(c["games"],1)*1000 for c in cs)/len(cs)
                g=min(c["games"] for c in cs)
                tag=("*" if any(c["intractable"] for c in cs)
                     else "=" if any(c["qdead"] for c in cs) else "")
                parts.append("%s%d=%.4f[%.1fms]%s"%(arm,d,lp,ms,tag)); ann.append("%s%d:%dg"%(arm,d,g))
            if parts:
                L.append("  %s %s"%(label,"   ".join(parts)))
                L.append("    # games/cell: %s   (*=intractable=reference-only, ==rung measured equivalent to the one below, capped)"%("  ".join(ann)))
        row("H", args.hdepths, "heuristic: ")
        row("V", args.vdepths, "value-leaf:")
        # The invariant, CHECKED. Cells that are still comparable (not reference-capped, not
        # rung-capped) must hold identical game sets; anything else makes the row means above
        # apples-to-oranges. Measured cost of getting this wrong, burn 2026-08-15: H3 read 4.3275
        # over 400 games and H4 4.2222 over 360, an apparent 0.105-turn gain from one rung -- paired
        # on the games both held they were identical to four decimals. The whole difference was the
        # 21 games H4 had abandoned and H3 still carried, i.e. exactly the hardest ones.
        for seed in sorted({c["seed"] for c in dc if c["batches"]>0}):
            comp=[c for c in dc if c["seed"]==seed and c["batches"]>0
                  and not c["intractable"] and not c["qdead"]
                  and any(x.get("g") for x in c["chunks"])]
            sets={frozenset(_per_game(c)) for c in comp}
            if len(sets)>1:
                L.append("    !! UNEQUAL GAME SETS on s%d: %d comparable cells hold %d DIFFERENT game"
                         " sets (sizes %s) -- row means above are NOT comparable" %
                         (seed, len(comp), len(sets), sorted({len(x) for x in sets})))
        # READ-SIDE GUARD. enforce_offset_src_agreement heals this at every resume, so a table
        # written by a current run cannot carry a conflict -- but an OLD state file can, and the
        # table outlives the run that made it (the metadata deriver and every later reader consume
        # this file, not the state). A conflict makes a per-game comparison attribute a code change
        # to depth, so it is named in the artifact itself rather than left to be rediscovered.
        conf={k:v for k,v in _offset_src_conflicts(dc).items() if v}
        if conf:
            L.append("    !! ENGINE-MIXED: %d game(s) were measured on DIFFERENT engines across cells"
                     " -- per-game comparisons below are NOT apples-to-apples" %
                     sum(len(v) for v in conf.values()))
            for (dk,seed),bad in sorted(conf.items()):
                L.append("    !!   s%d: %s" % (seed, _fmt_ranges(sorted(bad))))
        # THE ESTIMAND, stated in the artifact. Skipping degenerate games changes the question the
        # table answers -- "on games that complete within the work ceiling, does the leaf match H5?"
        # rather than "on all games" -- and a reader comparing this against an unfiltered table would
        # otherwise be comparing two different populations with nothing to warn them. The skipped
        # games are BACKFILLED, so the counts above are still the full target; what differs is WHICH
        # games, and that is exactly what has to be disclosed.
        sk={k:v for k,v in (skipped or {}).items() if k[0]==dname and v}
        if sk:
            L.append("    ~~ FILTERED: %d degenerate game(s) abandoned at the per-game work ceiling"
                     " and backfilled -- this row measures games that COMPLETE, not all games" %
                     sum(len(v) for v in sk.values()))
            for (dk,seed),offs in sorted(sk.items()):
                L.append("    ~~   s%d: %s" % (seed, _fmt_ranges(sorted(offs))))
    open(args.out,"w").write("\n".join(L)+"\n")


def run_incremental(args):
    cells=[]
    for dname in args.decks:
        for seed in args.seeds:
            for d in args.hdepths: cells.append(dict(deck=dname,arm="H",depth=d,seed=seed))
            for d in args.vdepths: cells.append(dict(deck=dname,arm="V",depth=d,seed=seed))
    for c in cells:
        c.update(chunks=[], intractable=False, qdead=False, running=False, first_wall=None, ceil=0); _refresh(c)
    state_path=args.out+".cells.json"
    src_now=_src_fingerprint()
    if os.path.exists(state_path):                       # resume: skip already-committed chunks
        try:
            saved={_cell_key(x):x for x in json.load(open(state_path))}
            # A state file written before chunks[] existed carries only totals, so its games can only
            # be attributed to ONE engine -- the src the run was started under, recorded by
            # scripts/valueleaf.sh next to the state. That is right for every cell that was not
            # resumed across a src change; where it is wrong it is wrong CONSERVATIVELY (games the
            # new engine already produced get re-run, which costs time and cannot corrupt anything).
            legacy_src=None
            fs=os.path.join(os.path.dirname(os.path.abspath(state_path)),"freeze.src")
            if os.path.exists(fs): legacy_src=open(fs).read().strip() or None
            for c in cells:
                s=saved.get(_cell_key(c))
                if not s: continue
                c["intractable"]=s["intractable"]; c["qdead"]=s.get("qdead",False); c["first_wall"]=s.get("first_wall")
                # The frozen per-game ceiling, carried across resumes. It CANNOT be recomputed here:
                # its calibration games are below this resume's starting offset, so a fresh
                # calibration would use a different sample, produce a different ceiling and abandon a
                # different set of games -- the run would stop being reproducible halfway through.
                c["ceil"]=s.get("ceil",0)
                if s.get("chunks"):
                    c["chunks"]=s["chunks"]
                elif s.get("games"):
                    c["chunks"]=[{"off":0,"n":s["games"],
                                  "lp":s["lp_sum"]/max(s["batches"],1),"ms":s["ms"],
                                  "src":legacy_src,"legacy":s["batches"]}]
                _refresh(c)
        except Exception: pass
        print("resumed %d cells from %s" % (sum(1 for c in cells if c["batches"]), state_path))

    lock=threading.Lock()
    for c in cells: c["inflight_games"]=0

    # ==================== THE SKIP LIST: (deck, seed) -> offsets that will never complete ==========
    # A game the pool refused to finish -- ABANDONED by the per-game work ceiling, or skipped because
    # its cell was condemned. Three things follow, and they are the whole reason the list is shared
    # per (deck, seed) rather than kept per cell:
    #
    #   1. It is never re-queued. It would just abandon again, at full cost, forever.
    #   2. It does not count toward the target, so the cell BACKFILLS: target grows by the number of
    #      skipped games and build_queue picks up offsets past the nominal end. That is what keeps
    #      every cell at the same GAME COUNT despite holding a different game SET.
    #   3. Every OTHER cell excludes the same offsets. Detection is per-cell (a game degenerate at H5
    #      may be trivial at V3) but application has to be global, or the comparison across a row is
    #      once again over unequal game sets -- the exact defect per-game storage exists to end.
    #
    # THE ESTIMAND CHANGES, and that must reach the artifact rather than living here. The table then
    # answers "on games that complete in reasonable time, does the leaf match H5?" instead of "on all
    # games". That is arguably the better question -- budgeted play never searches a degenerate
    # position to completion, so its unbounded quality was never decision-relevant -- but a reader
    # comparing against an unfiltered table would be comparing different populations without knowing.
    skipped = {}
    skip_path = args.out + ".skipped.json"
    # Loaded BEFORE anything reads `target` (resync, build_queue): a skip list that only appeared
    # later would let the very first resume re-queue every abandoned game, and the run would then
    # spend its budget re-abandoning the same handful at full cost -- the failure this prevents.
    try:
        for _k, _offs in json.load(open(skip_path)).items():
            _deck, _seed = _k.rsplit("|", 1)
            skipped[(_deck, int(_seed))] = set(_offs)
        _n = sum(len(v) for v in skipped.values())
        if _n: print("resumed skip list: %d game(s) across %d (deck,seed)" % (_n, len(skipped)))
    except (OSError, ValueError):
        pass

    # Rows ruled QUALITY-DEAD (see check_quality below) and the file the pool reads them from.
    # Declared HERE, above target(), because target() closes over it and runs during the resume
    # path long before check_quality is ever called.
    quality_dead = set()
    control_path = args.out + ".control"
    try: os.remove(control_path)
    except OSError: pass

    def skiplist(c): return skipped.get((c["deck"], c["seed"]), ())

    # ---------------------------------------------------------------- GRACEFUL DEGRADATION
    # Skipping only makes sense for a heavy TAIL. Past some rate the games being dropped are not a
    # tail any more, they are the distribution, and filtering them changes what the table measures
    # into something no reader would recognise. It is also a RUNAWAY: target() grows by the size of
    # the skip list, so every pass adds games, and a ceiling below a cell's own median means each new
    # game abandons too and the target never converges. Demonstrated 2026-08-15 with a deliberately
    # low ceiling on burn -- H3 (median 7,506 units, ceiling 5,000) went 19 games, then 31, climbing
    # with no end. The relative ceiling makes this unlikely by construction (k x the cell's OWN
    # median cannot sit below it), so this is the backstop for a bad k, not the primary guard.
    #
    # Evaluated per (deck, seed) at manifest-build time: once the list is over the cap the ceiling is
    # DISARMED for that deck+seed, which is the only stable answer. Merely refusing to record further
    # skips would be worse than doing nothing -- the pool would keep abandoning those games while the
    # driver kept re-queueing them, at full cost, forever.
    def skip_capped(deck, seed):
        return len(skipped.get((deck, seed), ())) > args.max_skip_frac * args.target

    def target(c):
        if c["intractable"]: return min(args.reference_target, args.target)
        # A rung ruled QUALITY-DEAD keeps exactly the games that proved it equivalent and stops
        # there. Unlike an intractable cell this is not a cost verdict, so it does not fall back to
        # --reference-target: the sample it already holds is the evidence.
        if (c["deck"], c["arm"], c["depth"]) in quality_dead: return c["games"]
        return args.target + len(skiplist(c))

    # A play change since these results were written is absorbed here, at chunk granularity: the
    # unfinished games are regenerated in each cell where they exist, and the prefix below them keeps
    # its original engine. That is what makes landing an optimization mid-table affordable.
    resync_engine_change(cells, target, src_now)

    # ... and the invariant that absorption is FOR is re-derived from the state itself, every resume.
    # resync_engine_change heals one src change at the split point it can see; this catches a change
    # that landed while its games were still in flight, which is how three deep-H cells (and no V
    # cell) ended up carrying 63 games of a third engine. Idempotent and cheap: with no conflict it
    # walks the offsets once and drops nothing.
    enforce_offset_src_agreement(cells)

    # A game abandoned in ANY cell is excluded from EVERY cell of its (deck, seed). Re-applied on
    # every resume rather than only when it grows, because a cell that ran the game before it was
    # first abandoned elsewhere still holds a result for it.
    apply_skiplist(cells, skipped)

    # ---------------------------------------------------------------- PROPER BATCHING: one work QUEUE
    # ONE queue of fixed-size chunks, handed to `workers` threads that are kept full until the queue
    # drains. Not one chunk per cell, not a chunk size that shrinks: a QUEUE.
    #
    # What this replaces and why. The old scheduler picked the cell with the FEWEST games (breadth
    # first) and allowed ceil(workers/open cells) chunks of it in flight. Two failures follow from
    # that, and both were measured on this deck:
    #   * breadth-first is exactly backwards. It starts the CHEAP work first, so the expensive cells
    #     are the ones still running when everything else has drained -- 3 of 24 cores for fifteen
    #     hours, with two H5 chunks at 54,004s elapsed.
    #   * parallelism was capped by the number of OPEN CELLS, so it collapsed as the run finished.
    #
    # LONGEST-PROCESSING-TIME FIRST fixes the first: start the slowest chunks at the beginning, when
    # there is still cheap work to fill the other cores, and they finish alongside it instead of
    # after it. A single queue fixes the second -- a free worker takes the next chunk from anywhere.
    # The residual tail is then one chunk, which is the honest floor for subprocess-level batching
    # (game-level pooling inside ONE `mtg --batch` needs a manifest game_index field: src, next).
    def est_spg(c):
        # Measured rate when we have one (a resume has 49 of 52), else a depth model: the H arm grows
        # about 4x per level (measured 0.5 / 5.9 / 36 / 161 / 325 s at H1..H5) and the V arm about 3x
        # from a far lower base (0.009 .. 9.4 s at V1..V8). Only the ORDER matters to LPT, so a rough
        # model is fine -- and it is only ever used for a cell with no games yet.
        if c["games"]: return c["ms"]/c["games"]
        return (4.0**c["depth"]) if c["arm"]=="H" else (3.0**c["depth"])*0.02

    def build_queue():
        # Every missing offset up to the cell's target. There is no wave cap any more: the pool
        # admits condemnable cells a game at a time and judges them as they run, so nothing has to
        # be held back behind a barrier.
        # Chunk size still matters even though the pool no longer runs one process per chunk -- it is
        # the unit of DURABILITY (a chunk's result is written the moment it lands) and of resume, so
        # a wave with few games uses smaller chunks rather than leaving most of them un-committed
        # until the end.
        # The work is the set of MISSING OFFSETS, not "target minus a count". After a resync those are
        # not always a suffix -- a cell can be left with a hole in the middle -- so the gaps are
        # computed from the chunks the cell actually holds.
        gaps={id(c):_missing(c,target(c),skiplist(c)) for c in cells}
        total=sum(hi-lo for g in gaps.values() for lo,hi in g)
        chunk=max(1,min(args.batch, total//max(1,args.workers))) if total else args.batch
        q=[]
        for c in cells:
            for lo,hi in gaps[id(c)]:
                off=lo
                while off<hi:
                    # GRID-ALIGNED: a chunk never straddles a multiple of args.batch, so the same
                    # offsets form the same chunk in every cell and a chunk is a comparable unit
                    # across arms/depths. Without this the boundaries follow each cell's own gap
                    # start, so a resynced cell chunks [375,394) while its neighbours hold one
                    # [0,394) -- and a per-chunk engine check has nothing to compare.
                    n=min(chunk, args.batch-off%args.batch, hi-off)
                    q.append({"c":c,"off":off,"n":n})
                    off+=n
        # LEVEL-ORDER THE FIRST 3/4, PACK THE LAST (user, 2026-08-12). LPT and the resync's banking
        # rule pull in opposite directions, and unmediated the banking rule loses: resync_engine_change
        # keeps only offsets below B = min, over the seed's INCOMPLETE cells, of their contiguous
        # prefix -- so ONE cell with zero games forces B=0 and an engine change discards the whole
        # seed. LPT guarantees exactly that state for hours (it runs the costliest chunks first, so the
        # cheap cells have not started at all): measured 2026-08-11, a run holding 4367 games across
        # 7.75 h would have banked NONE of them. A Windows Update then killed the next run outright,
        # which is the same loss arriving by a different route.
        #
        # So advance every cell together through the first 3/4 of its target -- offset level by offset
        # level, all cells at level k before level k+1 -- which keeps B climbing with the run and makes
        # a stop at ANY convenient point cost only the level in flight. Then switch to LPT for the last
        # quarter, where packing is what protects the makespan: the residual tail is one chunk of the
        # most expensive cell, and that is the honest floor for chunk-level batching.
        #
        # CONDEMNED cells keep plain LPT and stay at the front (user, 2026-08-12). They are capped at
        # the reference target rather than run to 400, their whole purpose is to be judged early so the
        # queue is freed, and level-ordering them would delay that verdict for no banking gain.
        #
        # The test is whether a cell IS condemned, never whether its depth makes it condemnABLE. Keying
        # on depth > never-condemn was wrong twice over: V6/V7/V8 run at ~15-17 s/game, nowhere near the
        # 60 s/game cutoff, so they are never condemned and are FULL 400-game rows of the table -- yet
        # the depth test filed them as reference-only and handed 2500 games of them absolute priority.
        # Observed 2026-08-12: every thread sat on that work while H1-H5 and V1-V5 stayed at zero. Only
        # 7 of 56 cells are actually condemned here (H6 at every seed, V6/V7/V8 at s8008 alone), which
        # is the handful this zone is for -- a few of them alongside a whole lot of everything else,
        # not the pool to themselves. An unjudged cell is level-ordered like any other and simply stops
        # being queued if the guard later condemns it.
        #
        # Ordering WITHIN a level stays cost-descending: B is gated by the SLOWEST cell's chunk landing,
        # so starting the expensive chunk of each level first minimises time-to-protection. Cheapest-
        # first would bank trivia instantly but push the binding cell later, moving B off zero LATER.
        # The EXPENSIVE CELL IS NOT SPECIAL-CASED (user, 2026-08-12). H5 leads, exactly as it does under
        # plain LPT -- it is simply that a SET is completed before the next one starts. Cost-descending
        # is the within-level tiebreak, so H5 is the first chunk of every level and the first chunk of
        # the packed quarter; it is never held back. The single thing that changes versus LPT is the
        # primary key: finish the set, then move on.
        # ONE chunk of a condemned cell at a time, but PRIORITISED (user, 2026-08-12). Note what this
        # does and does NOT control: OCCUPANCY of the pool by depth>never-condemn cells is the ENGINE's
        # to decide, not this weight's. BatchRunner pulls every condemnable cell's games OUT of the
        # static list into per-cell queues and meters them in at `drip` games each (see its WORK POOL
        # comment), so a manifest weight only chooses the ORDER those cells seed the metered deque.
        # Keeping one chunk per condemned cell at the head keeps that order sensible -- it does not,
        # and cannot, stop them taking threads. See docs/design/batch-drip-release.md for the part
        # that can.
        first_out = {}
        for ch in sorted(q, key=lambda ch: ch["off"]):
            first_out.setdefault(id(ch["c"]), ch["off"])
        def zone(ch):
            c = ch["c"]
            if c["intractable"] and ch["off"] == first_out[id(c)]:
                return 0                                          # CONDEMNED, next chunk: up front
            return 1 if ch["off"] < 0.75*target(c) else 2         # level-order, then packed
        q.sort(key=lambda ch: (zone(ch),
                               ch["off"] if zone(ch) == 1 else 0,
                               -est_spg(ch["c"])*ch["n"]))
        # ...and CARRY that order into the pool, which does its own sort. Every chunk becomes a job in
        # ONE `mtg --batch`, and BatchRunner re-sorts jobs by the manifest's `weight` DESCENDING (see
        # BatchRunner.cpp, sched_weight). A plain cost weight therefore reimposed pure LPT and made this
        # ordering INERT -- observed 2026-08-12: three minutes in, H1-H4 and V1-V5 still had zero games
        # because the pool was running the costliest chunks first regardless of queue position.
        #
        # So encode the whole key in the weight. sched_weight is a 32-bit int, so the fields are packed
        # to stay well inside it: zone (0..2) at 1e8, level at 1e4, cost at 1e0.
        #   zone   : (2-z)*1e8      -- 0 (condemned) outranks 1 (level-order) outranks 2 (packed)
        #   level  : (target-off)*1e4 for the level-order zone only, so a LOWER offset outranks a
        #            higher one; zero elsewhere, which leaves those zones ordered purely by cost (LPT)
        #   cost   : int(s/game*10), capped below 1e4 so it can only ever break ties within a level
        # Max is ~2.0e8, comfortably inside int32.
        for ch in q:
            z = zone(ch); c = ch["c"]
            lvl = (target(c) - ch["off"])*10_000 if z == 1 else 0
            ch["w"] = (2-z)*100_000_000 + lvl + min(int(est_spg(c)*10), 9_999)
        return q

    # ------------------------------------------------- QUALITY CONDEMNATION (see dead_rung)
    # Rows already ruled dead, and the file the POOL reads them from. The engine cannot reach this
    # verdict itself: on a resume, half the sample lives in the state file and never enters the
    # manifest, so only the driver can pair the two rungs. Writing it out mid-run is what lets the
    # rule pay off on the run that is currently spending on the dead rung -- deferring it to the next
    # resume means never, for a single long phase-C invocation.
    def check_quality(log=print):
        if args.quality_threshold <= 0: return
        fresh = []
        for c in cells:
            key = (c["deck"], c["arm"], c["depth"])
            if key in quality_dead or c["depth"] <= 1: continue
            dead, d = dead_rung(cells, c["deck"], c["arm"], c["depth"],
                                args.quality_threshold, args.quality_min_pairs,
                                args.quality_coverage)
            if not dead: continue
            quality_dead.add(key); fresh.append((key, d))
        if not fresh: return
        # Append-only, and the pool re-reads the whole file each tick, so a partial write is
        # harmless: the row is simply picked up on the following tick.
        with open(control_path, "a") as fh:
            for (dname, arm, depth), _ in fresh: fh.write("%s_%s%d\n" % (dname, arm, depth))
        for (dname, arm, depth), d in fresh:
            # 5 decimals on the BOUND, not 4: the resolution floor (3*step/n) puts a flat rung's
            # verdict right at the threshold, and at 4 decimals the line printed "0.0075 < 0.0075",
            # which reads as a broken comparison rather than 0.00748 < 0.0075.
            log("  QUALITY-DEAD %s %s%d: %s%d->%s%d improvement %+.4f turns, se %.4f, one-sided 95%% "
                "upper bound %.5f < %.4f on %d paired games -- capping the rung"
                % (dname, arm, depth, arm, depth-1, arm, depth,
                   d["improvement"], d["se"], d["upper"], args.quality_threshold, d["n"]))

    def write_state():
        tmp=state_path+".tmp"
        json.dump([{k:c[k] for k in ("deck","arm","depth","seed","games","lp_sum","lp_mean","batches",
                                     "ms","intractable","qdead","first_wall","chunks","ceil")} for c in cells], open(tmp,"w"))
        os.replace(tmp,state_path)
        if skipped:
            tmp2=skip_path+".tmp"
            json.dump({"%s|%d" % k: sorted(v) for k, v in skipped.items()}, open(tmp2,"w"))
            os.replace(tmp2,skip_path)
        emit_table(cells,args,skipped)

    # ------------------------------------------------------- ONE PROCESS, ONE POOL, ONE TAIL
    # The WHOLE matrix -- every cell, both arms, straight to target -- goes into a single
    # `mtg --batch`, which flattens every game of every chunk into one work queue. No waves and no
    # per-arm split, because neither is a real dependency any more:
    #
    #   * The ARM used to be process environment (MTG_VALUE_MODEL and friends, each read once into a
    #     process-wide static), so one process could only BE one arm. It is now a per-job manifest
    #     field, so H and V cells share the queue and the cheap V arm backfills cores while the
    #     expensive H arm drains. See ai/ValueArm.h.
    #   * CONDEMNATION used to need a barrier: run everything to a reference floor, stop the world,
    #     judge. It is now applied inside the pool -- on the cell's running mean (in-flight games
    #     included), on a single-game limit checked while the game is still running, with the cell's
    #     remaining games skipped at dequeue. See BatchRunner's CondemnRule.
    #   * Condemnable cells are METERED rather than sorted: LPT would otherwise put every thread on
    #     the most expensive cell in the table, which is precisely the one most likely to be thrown
    #     away. The pool admits `drip` of each at a time and refills on each uncondemned completion,
    #     while the protected cells keep the rest of the box busy.
    #
    # What this replaces: one subprocess per chunk made the chunk size the tail granularity (measured
    # 2026-08-08, two 25-game H5 chunks still running at 54,004 s with 3 of 24 cores busy for fifteen
    # hours), and the per-(deck,arm)-per-wave pools that followed still idled ~15 of 20 threads for
    # hours (2026-08-10). One queue has one tail, and it is one GAME long.
    def run_pool(chunks):
        by_name={}
        jobs=[]
        capped_seen=set()
        for ch in chunks:
                c=ch["c"]
                dname, arm = c["deck"], c["arm"]
                deck_file, prof, mt = DECKS[dname]
                _capped = skip_capped(dname, c["seed"])
                if _capped and (dname, c["seed"]) not in capped_seen:
                    capped_seen.add((dname, c["seed"]))
                    print("  !! %s seed %d is over the skip cap (%d skipped > %.0f%% of %d): the "
                          "per-game ceiling is DISARMED for it -- these games are the distribution, "
                          "not a tail" % (dname, c["seed"], len(skipped.get((dname,c["seed"]),())),
                                          100*args.max_skip_frac, args.target), flush=True)
                nm="%s%d_s%d_off%d" % (c["arm"], c["depth"], c["seed"], ch["off"])
                by_name[nm]=ch
                job={"name":nm, "deck":deck_file, "profile":PROFILES.get(dname),
                     "games":ch["n"],
                     # The chunk's shuffle seeds are base+off .. base+off+n, and its GLOBAL
                     # game numbers are off .. off+n -- exactly the single-run
                     # `--seed (base+off) --game-index off` form this used to spawn.
                     "seed":c["seed"]+ch["off"], "game_index":ch["off"],
                     "depth":c["depth"], "budget_ms":0,   # 0 = unbounded (the H arm's point)
                     "max_turns":mt, "ignore_play_profile":True,
                     # Every chunk of a cell shares one `cell` (max_game_sec's unit and the skip
                     # unit), and every cell of a (deck,arm,depth) shares one `row` -- the MEAN
                     # rule's judgment unit. The mean judges the ROW's cumulative rate, never one
                     # chunk's (a single unlucky 25-game chunk once condemned a cell averaging
                     # 5.5 s/game against a 60 s/game limit -- dragonstorm H5 s11011, 2026-08-04)
                     # and never one SEED's (one 32-minute game condemned V6/V7/V8 on seed 8008
                     # alone, 1-3% over the limit, while the other three seeds ran 4-10x under it
                     # -- docs/design/condemnation-row-average.md).
                     # PER-GAME WORK CEILING (0 = off). Three states, in priority order:
                     #   * the cell has a FROZEN ceiling (from an earlier pass, stored in the state
                     #     file) -- hand it straight back, because its calibration games are below
                     #     this resume's offsets and could never be re-measured here;
                     #   * --abandon-k is set and the cell has no ceiling yet -- ask the pool to
                     #     calibrate: it runs the cell's first --abandon-calib games under whatever
                     #     absolute cap is set (usually none) and freezes k x their median;
                     #   * neither -- the plain absolute number, or 0 for unbounded.
                     # A ratio is the only form that can serve the whole table: on burn, six cells
                     # spanned 150x in median units, and this deck's H5 is another three orders out.
                     # ...and NOTHING at all once this deck+seed is over the skip cap (see
                     # skip_capped): past that rate the filter is reshaping the population rather
                     # than trimming a tail, and it cannot converge.
                     "abandon_units":(0 if _capped else (c.get("ceil") or args.abandon_units)),
                     "abandon_k":(0.0 if (_capped or c.get("ceil")) else args.abandon_k),
                     "abandon_calib":(0 if (_capped or c.get("ceil")) else args.abandon_calib),
                     # ABSOLUTE FLOOR under the ratio. `k x median` knows nothing about how expensive
                     # the cell is in absolute terms, so on a CHEAP cell it condemns games that cost
                     # nothing: measured on this deck's own matrix, V1 (9.6 ms median) was losing 6.3%
                     # of its games and H1 (611 ms) 6.0%. That is not a saving, it is data loss -- and
                     # because a game abandoned in ANY cell is dropped from ALL of them (see the union
                     # at reduce time), a cheap cell's spurious abandonment costs the EXPENSIVE rows
                     # their games too. The floor makes the ratio inert wherever it is not needed.
                     "abandon_floor_units":(0 if _capped else args.abandon_floor_units),
                     "cell":"%s_%s%d_s%d" % (dname, c["arm"], c["depth"], c["seed"]),
                     "row":"%s_%s%d" % (dname, c["arm"], c["depth"]),
                     # Priority for the pool's own sort (BatchRunner sorts by this DESCENDING). Packed
                     # in build_queue so the queue's order survives into the pool; a bare cost weight
                     # here silently reimposed LPT and made the set-completion ordering inert.
                     "weight":ch["w"]}
                # THE ARM, per job (see ai/ValueArm.h). This is what lets both arms share one queue.
                if c["arm"]=="V":
                    job.update({"value_model":True, "value_profile":prof,
                                "value_min_depth":args.value_min_depth,
                                "value_startgate_alpha":8.0})
                else:
                    # The H-cell ladder runs its warm-up passes on the cheap leaf and only the
                    # committed pass on the heuristic. Guarded on the sidecar EXISTING, so a missing
                    # model does not error -- every H cell just silently takes the slow path
                    # (1.35x-84.8x more work).
                    job["value_model"]=False
                    if prof and os.path.exists(prof):
                        job["value_profile"]=prof; job["ladder_value_leaf"]=True
                jobs.append(job)
        man=args.out+".manifest.json"
        # CONDEMNATION now travels with the manifest and is applied inside the pool, continuously.
        condemn={"median_sec_per_game":args.intractable_median_sec_per_game,
                 "reference_games":args.reference_target,
                 "never_condemn_depth":args.never_condemn_at_or_below,
                 "max_game_sec":args.max_game_sec,
                 "drip":args.drip,
                 # Rows the driver rules QUALITY-DEAD mid-run (see check_quality).
                 "control_file":control_path}
        json.dump({"condemn":condemn, "jobs":jobs}, open(man,"w"))
        env=dict(os.environ)
        # The arm is on the JOB now, so a stray arm variable in the caller's environment would apply
        # to every job that does not override it -- silently measuring one arm twice. Strip them.
        for k in ("MTG_EVAL_MODEL","MTG_EVAL_PROFILE","MTG_VALUE_MODEL","MTG_VALUE_PROFILE",
                  "MTG_NC_SEARCH","MTG_VALUE_MIN_DEPTH","MTG_VALUE_REDO_MODE",
                  "MTG_VALUE_STARTGATE_ALPHA","MTG_LADDER_VALUE_LEAF"):
            env.pop(k, None)
        env.setdefault("MTG_BATCH_HEARTBEAT", os.path.join(os.path.dirname(args.out) or ".",
                                                           "heartbeat.txt"))
        # MEMORY BOUND, not a tuning knob. The matrix runs UNBOUNDED search on every worker at once,
        # and the transposition table is an unbounded memoization of SimulateToEnd -- so the pool's
        # footprint grows without limit. Measured 2026-08-11 on FiveColour: one `mtg --batch` reached
        # 43.9 GB RES / 93.3% of a 47 GB box with 0 GB available, i.e. one bad game away from the OOM
        # killer taking a 13-hour run with it.
        #
        # The cap is RESULT-NEUTRAL by construction (see TranspositionTable::Cap): the table is a pure
        # memoization, so refusing to store past the cap trades recompute for bounded memory and every
        # decision is unchanged. Early shallow high-reuse leaves are stored first and kept; only the
        # deep long tail is dropped. An entry is ~64 B (16 B key + 4 B value + node/bucket overhead),
        # so 8M entries is ~0.5 GB per live table -- roughly 12-24 GB across 24 workers, which leaves
        # real headroom on this box instead of running to the ceiling.
        env.setdefault("MTG_TT_CAP", "8000000")
        games=sum(j["games"] for j in jobs)
        nH=sum(1 for j in jobs if not j.get("value_model"))
        print("ONE pool: %d chunks (%d H, %d V), %d games, %d threads; condemn %s"
              % (len(jobs), nH, len(jobs)-nH, games, args.workers, condemn), flush=True)
        print("  heartbeat -> %s (slowest 100 games, running ones included)"
              % env["MTG_BATCH_HEARTBEAT"], flush=True)
        # --threads only when explicitly asked for; otherwise let the engine resolve to every
        # available core (see --workers). Passing a number always would re-introduce the very
        # hand-picked cap this run exists to avoid.
        # PER-GAME RESULTS. `--game-log-dir` writes one `<job>.wins` per chunk (`index win_turn
        # digest`), which is the only channel that reports below the job aggregate -- the streamed
        # result line carries a single mean. Costs one small file per chunk and nothing at all in
        # the hot path; the directory is also the durable per-game record (digests included), so a
        # question the state file cannot answer can still be answered off disk afterwards.
        winsdir=os.path.join(os.path.dirname(args.out) or ".", "wins")
        os.makedirs(winsdir, exist_ok=True)
        cmd=[MTG,"--batch",man,"--game-log-dir",winsdir]+(["--threads",str(args.workers)] if args.workers>0 else [])
        # stderr carries the engine's SLOW-GAME and CONDEMNED lines (each SLOW-GAME a
        # self-contained repro, tagged job=<cell>_off<offset>). Drained on its own thread so a full
        # pipe can never block the pool, and handled as it arrives rather than collected at exit --
        # on this deck a single game ran for 21 hours, and a report you only get afterwards is not a report.
        pr=subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, env=env)
        slow_path=os.environ.get("MTG_SLOW_GAME_LOG","logs/eval/slow_games.log")
        def drain_slow(stream, tag):
            try:
                for l in stream:
                    # CONDEMNED comes back on the same stream. Mirror it into the cell state so the
                    # table, the resume logic and the pool agree on what was actually judged --
                    # otherwise a resumed run would keep re-queueing a cell the pool has given up on.
                    # Two shapes: the MEAN rule condemns `row=<deck>_<arm><depth>` (every seed's cell
                    # of it goes reference-only), max_game_sec condemns `cell=<...>_s<seed>` (that
                    # one cell). Matching either keeps this compatible with an engine from before
                    # the row rule, whose mean verdicts arrived as cell= lines.
                    if "CONDEMNED" in l:
                        print(l.rstrip(), flush=True)
                        # A DRIVER verdict is a QUALITY one -- the rung was measured equivalent to
                        # the one below and keeps the sample that proved it. Flagging it
                        # "intractable" would be wrong twice over: it is not a cost verdict, and an
                        # intractable cell is excluded from the table's paired comparison basis,
                        # which is precisely the cell that most needs to be in it.
                        qual = "DRIVER" in l
                        m=re.search(r"row=(\S+)", l)
                        if m:
                            for c in cells:
                                if "%s_%s%d" % (c["deck"],c["arm"],c["depth"]) == m.group(1):
                                    if qual: c["qdead"]=True
                                    else:    c["intractable"]=True
                        m=re.search(r"cell=(\S+)", l)
                        if m:
                            for c in cells:
                                if "%s_%s%d_s%d" % (c["deck"],c["arm"],c["depth"],c["seed"]) == m.group(1):
                                    c["intractable"]=True
                        continue
                    # The pool froze a cell's relative per-game ceiling. STORE IT: the calibration
                    # games are the cell's first N offsets, so a later resume -- which starts above
                    # them -- has no way to derive the same number again, and a ceiling derived from
                    # a different sample abandons a different set of games.
                    if "CEILING" in l:
                        print(l.rstrip(), flush=True)
                        mc=re.search(r"cell=(\S+).*?units=(\d+)", l)
                        if mc:
                            for c in cells:
                                if "%s_%s%d_s%d" % (c["deck"],c["arm"],c["depth"],c["seed"]) == mc.group(1):
                                    c["ceil"]=int(mc.group(2))
                        continue
                    # NOT a verdict: the pool DECLINED to condemn a cell whose game ran past
                    # max_game_sec, because that cell's per-game work ceiling is armed and the game
                    # is therefore already bounded (BatchRunner's in-flight rule (1)). Nothing in the
                    # cell state changes -- but this is the one signal that the ceiling is too loose
                    # for this cell or that the box is loaded, and everything not matched here is
                    # dropped, so without this line it would be invisible in a matrix run.
                    if "OVER max_game_sec" in l:
                        print(l.rstrip(), flush=True)
                        continue
                    if "SLOW-GAME" not in l: continue
                    with _slow_lock:
                        with open(slow_path,"a") as fh: fh.write("%s %s\n" % (tag, l.strip()))
            except Exception: pass
        # CONDEMNED lines come back on the same stream; mirror them into the cell state so the table
        # and the resume logic agree with what the pool actually did.
        st=threading.Thread(target=drain_slow, args=(pr.stderr, "pool"), daemon=True)
        st.start()
        done=0
        # Commit each chunk THE INSTANT its line arrives. The pool streams a job's result as
        # soon as that job's last game lands, so durability is per chunk exactly as before --
        # killing the run mid-wave loses only the chunks still in flight.
        for line in pr.stdout:
            m=re.match(r"(\S+): played=(\d+) avg=([\d.]+) digest=(\w+) ms=(\d+)", line.strip())
            if not m: continue
            nm,p,lp,_dg,ms = m.group(1), int(m.group(2)), float(m.group(3)), m.group(4), int(m.group(5))
            ch=by_name.get(nm)
            if ch is None or p<=0: continue
            c=ch["c"]; wall=ms/1000.0; grew=False
            rec={"off":ch["off"],"n":p,"lp":lp,"ms":wall,"src":src_now}
            # Attach the per-game results, but only after checking they REPRODUCE the engine's own
            # reported average. `lp` stays authoritative either way; a mismatch means the .wins file
            # and the result line describe different games (a stale file from a previous run under the
            # same job name is the realistic way that happens), and silently storing it would poison
            # every per-game comparison downstream while the table's own numbers still looked right.
            wins=_read_wins(winsdir, nm)
            if wins is not None and len(wins)==p:
                mt=_mt_of(c)
                got=sum(_lp_value(w, mt) for _, w in wins)/len(wins)
                if abs(got-lp) <= 5e-4:
                    rec["g"]=[[o, w] for o, w in wins]
                else:
                    print("  !! per-game mismatch %s: wins=%.4f vs reported=%.4f -- storing mean only"
                          % (nm, got, lp), flush=True)
            # A chunk that came back SHORT lost games the pool refused to finish -- abandoned by the
            # per-game work ceiling, or skipped because the cell was condemned. Those offsets are
            # recorded so they are neither re-queued (they would abandon again) nor counted toward
            # the target, and so every OTHER cell can exclude the same games (see the skip list).
            if p < ch["n"]:
                if wins is None:
                    # REFUSE IT. A short chunk with no per-game rows cannot say WHICH games it holds,
                    # and _chunk_offsets falls back to off..off+n -- a contiguous range it does not
                    # have. One measured case stored "0..7" while actually holding {1,2,4,7,12,17,19,
                    # 22}, so every paired comparison against it would intersect on the wrong game
                    # identities: silently comparing different games, which is the one defect
                    # per-game storage exists to end. Dropping the chunk costs a re-run of games we
                    # can still identify; keeping it corrupts the table. The engine writes the .wins
                    # file BEFORE announcing the job, so this should now only fire on a real I/O
                    # failure -- loudly, because it means the pool and the driver disagree.
                    print("  !! chunk %s came back %d/%d with NO per-game rows -- DISCARDED (cannot "
                          "identify which games survived; they will be re-queued)" % (nm, p, ch["n"]),
                          flush=True)
                    continue
                ran={o for o, _ in wins}
                lost=[o for o in range(ch["off"], ch["off"]+ch["n"]) if o not in ran]
                # TWO KINDS OF HOLE, and only one of them belongs in the skip list.
                #   ABANDONED  -- the game hit the per-game work ceiling. It is DEGENERATE, it will
                #                 abandon again at full cost, and every cell of this (deck,seed) has
                #                 to exclude it or the comparison stops being over one game set.
                #   NOT RUN    -- the game was skipped at dequeue because its own CELL was condemned.
                #                 It is a perfectly ordinary game everywhere else. Putting it in the
                #                 shared list discards a good game in EVERY other cell of the seed,
                #                 and the condemned cell is capped at its reference sample anyway.
                # These used to be conflated (a short chunk was all "abandoned"), so one condemned
                # cell truncated its whole seed: a burn probe condemned V6/V7 after 5 of 25 games and
                # the table reported the 20 unrun games as "degenerate ... abandoned at the per-game
                # work ceiling". The engine now names the abandoned ones explicitly.
                ab=set(_read_abandoned(winsdir, nm))
                gone=[o for o in lost if o in ab]
                unrun=[o for o in lost if o not in ab]
                if gone:
                    skipped.setdefault((c["deck"], c["seed"]), set()).update(gone)
                    print("  chunk %s came back %d/%d: ABANDONED %s (added to the skip list)"
                          % (nm, p, ch["n"], _fmt_ranges(sorted(gone))), flush=True)
                    # ...and DROP THEM FROM EVERY OTHER CELL, NOW. Detection is per-cell but
                    # application has to be global and IMMEDIATE: the cells that already ran these
                    # games keep their results otherwise, and the run ends with each cell holding a
                    # different game set -- the exact defect the skip list exists to prevent.
                    # Re-applying only at the next resume is not enough, because a run that finishes
                    # never has one: burn 2026-08-15 ended with H1 holding all 28 of seed 8008's
                    # abandoned games while H4 held 7, and H4's row mean came out 0.105 turns
                    # "better" purely because it was missing the hardest ones.
                    grew = True
                if unrun:
                    print("  chunk %s came back %d/%d: %s not run (cell condemned) -- NOT skipped"
                          % (nm, p, ch["n"], _fmt_ranges(sorted(unrun))), flush=True)
            c["chunks"].append(rec)
            _refresh(c)
            # EVERY chunk, not only the ones that grew the list: a chunk landing after the last
            # abandonment would otherwise re-introduce an offset the other cells have dropped, and
            # with no later growth nothing would ever sweep it out. _chunk_restrict keeps n, lp and
            # g consistent -- filtering the incoming rows by hand instead desynced them.
            apply_skiplist(cells, skipped, log=lambda *a: None)
            if c["first_wall"] is None: c["first_wall"]=wall
            done+=1
            write_state()
            check_quality()
            if done % 10 == 0:
                tot=sum(x["games"] for x in cells)
                print("... %d chunks, %d games total" % (done, tot), flush=True)
        pr.wait()
        st.join(timeout=5)

    # ONE pass. No floor wave: the pool judges tractability as it runs (running mean including
    # in-flight games, plus a single-game limit checked while the game is still running) and skips a
    # condemned cell's remaining games at dequeue, so there is nothing for a barrier to synchronise.
    q=build_queue()
    total_chunks=len(q)
    if q:
        print("queue: %d chunks of <=%d games to target, slowest first: %s"
              % (len(q), args.batch,
                 "%s%d s%s" % (q[0]["c"]["arm"], q[0]["c"]["depth"], q[0]["c"]["seed"])), flush=True)
        run_pool(q)
        write_state()

    done_batches=total_chunks
    print("=== incremental generation complete: %d batches, %d cells (%d intractable) ===" % (
        done_batches, len(cells), sum(1 for c in cells if c["intractable"])))


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--games",type=int,default=1000)
    ap.add_argument("--hgames",type=int,default=None,
                    help="games for the EXPENSIVE heuristic arm (combo decks: H4/H5 cost seconds/game). Default "
                         "= --games. Cost-sample it; the derivation's monotone-H envelope guards a noisy deep H.")
    ap.add_argument("--vgames",type=int,default=None,
                    help="games for the CHEAP value-leaf arm (O(1) leaf). Default = --games. Keep this full: the "
                         "value-leaf-by-depth is the useful, decision-setting side of the table.")
    ap.add_argument("--hgames-depth",nargs="+",default=[],metavar="D:G",
                    help="per-heuristic-depth games override for the SLOW cells only (e.g. 3:500 4:200 5:100 on a "
                         "combo deck). Depths not listed use --hgames/--games. Cut games ONLY where the heuristic "
                         "is genuinely expensive -- cheap decks (slivers/knights/burn/TH) keep FULL games at every "
                         "depth incl. H5; do not pass this for them.")
    ap.add_argument("--seeds",nargs="+",type=int,default=[4004,5005,6006,7007])
    ap.add_argument("--hdepths",nargs="*",type=int,default=[1,2,3,4,5])
    ap.add_argument("--vdepths",nargs="*",type=int,default=[1,2,3,4,5])
    ap.add_argument("--value-min-depth",type=int,default=0,
                    help="MTG_VALUE_MIN_DEPTH for the value arm; 0 = PURE value-leaf (no redo). Default 0.")
    ap.add_argument("--threads",type=int,default=6)
    ap.add_argument("--decks",nargs="+",default=list(DECKS))
    ap.add_argument("--out",default="logs/eval/valueleaf_depth_matrix.txt")
    ap.add_argument("--write-profile",action="store_true",
                    help="After measuring, AUTOMATICALLY fold this run's table into each deck's play profile "
                         "(<deck>.value.json) via valueleaf_table_to_metadata.py -- so measure+write is ONE "
                         "atomic command and the profile can never drift from a stale/hand-stitched log. Uses "
                         "ONLY this run's freshly-measured blocks (not the appended --out history). The writer's "
                         "completeness guard REFUSES a truncated ladder (extend --vdepths and re-run).")
    ap.add_argument("--allow-partial",action="store_true",
                    help="pass through to the profile writer: write even a truncated/inconclusive table "
                         "(--write-profile only). Use only when an UNSET trust depth is intentional.")
    # --- incremental / tractability-aware mode ---
    ap.add_argument("--incremental",action="store_true",default=True,
                    help="THE ONLY ROUTE, and now the default -- accepted for compatibility and ignored. "
                         "Round-robin BATCHES across every cell (breadth-first), written incrementally, "
                         "resumable, with cross-cell work-stealing and concurrent batches per cell. The "
                         "monolithic path was removed: it paid a load-imbalance tail PER CELL and could not "
                         "resume, which is how a run ended up at 3 of 24 cores for hours.")
    ap.add_argument("--no-incremental",dest="incremental",action="store_false",
                    help=argparse.SUPPRESS)
    ap.add_argument("--batch",type=int,default=50,help="games per incremental batch (default 50)")
    ap.add_argument("--target",type=int,default=None,help="full-sample target games/cell (default = --games)")
    ap.add_argument("--reference-target",type=int,default=100,
                    help="games/cell to stop at once a cell is marked INTRACTABLE (a slow, not-production-usable "
                         "cell still gets a reference value; default 100)")
    ap.add_argument("--intractable-median-sec-per-game",type=float,default=30.0,
                    help="condemn a ROW whose MEDIAN game costs more than this (wall sec/game) -> capped at "
                         "--reference-target. The question is 'can we escalate to this depth SOME of the time at "
                         "a cost that is not insane?', because that is the only regime the derived policy is ever "
                         "applied in -- shipped play is budgeted and never processes a pathological position at "
                         "depth. So a row is usable when a TYPICAL game is affordable however ugly its tail (the "
                         "per-game ceiling truncates the tail), and unusable only when the majority of its games "
                         "are impractical, which is exactly 'the median exceeds the limit'. NOTE the number is "
                         "smaller than the old mean limit and must be: a mean carried the tail inflation a median "
                         "does not, so the same value means something stricter here.")
    ap.add_argument("--intractable-sec-per-game",type=float,default=None,
                    help=argparse.SUPPRESS)   # RETIRED -- refused below, see --intractable-median-sec-per-game
    ap.add_argument("--never-condemn-at-or-below",type=int,default=5,
                    help="cells at or below this DEPTH are never marked intractable, however slow they measure. "
                         "These are the cells the trust-depth decision reads, so condemning one leaves a hole in "
                         "the answer rather than saving cost -- and since the check is on WALL time it can fire "
                         "purely because the box was oversubscribed. Set to 5 to protect the d<=5 ladder.")
    ap.add_argument("--max-game-sec",type=float,default=3600.0,
                    help="condemn a cell as soon as ONE of its games has been RUNNING this long -- checked on "
                         "in-flight games, so it does not wait for the game to end. Deliberately separate from "
                         "--intractable-median-sec-per-game: that one is a MEDIAN over the row (can we escalate to this "
                         "depth some of the time, affordably?), this "
                         "one is about a single pathological game, and sharing the number would condemn a cell "
                         "with a 5 s/game mean over one 61 s game. 0 disables. Default 3600 (a run measured "
                         "2026-08-10 had a single game at 21.4 HOURS, invisible until it finished).")
    ap.add_argument("--abandon-units",type=int,default=0,
                    help="per-GAME search-work ceiling in units (see ai/GameWorkMeter.h). A game past it is "
                         "ABANDONED: no result, excluded from every cell of its (deck,seed), and BACKFILLED so "
                         "the cell still reaches its game count. 0 = off (default), which is byte-identical to "
                         "the engine without it. Unlike --max-game-sec this is DETERMINISTIC -- work units, not "
                         "wall clock -- so the same games are dropped on every machine, which is what lets the "
                         "skip list be shared and the run stay reproducible. ABSOLUTE for now: the threshold "
                         "that actually wants using is relative to a cell's own median (cells span 11 ms to "
                         "700 s per game), and MTG_DUMP_UNITS exists to measure that distribution before a "
                         "multiplier is chosen. Prefer --abandon-k, which does that per cell.")
    ap.add_argument("--abandon-floor-units",type=int,default=0,
                    help="ABSOLUTE FLOOR under --abandon-k: never abandon a game costing less than this, "
                         "however far above its cell's median it sits. 0 = no floor. A pure ratio has no "
                         "notion of absolute cost, so on a cheap cell it voids games that cost nothing -- "
                         "measured on Mirrorwing, V1 (9.6 ms/game median) lost 6.3% of its games and H1 "
                         "(611 ms) 6.0%, for no saving whatever. And since a game abandoned in ANY cell is "
                         "excluded from ALL of them, that loss propagates to the expensive rows as well. "
                         "Set it to the cost below which a game is simply not a problem; the mechanism then "
                         "governs only the top of the ladder, which is the only place it was meant to act. "
                         "Units are deterministic, so this stays machine-independent -- but converting a "
                         "wall-clock intent into units is DECK-DEPENDENT: measured single-threaded, slivers "
                         "runs ~296k units/core-s and Mirrorwing ~135k, so the same floor buys different "
                         "amounts of time per deck. Quote the deck you calibrated against.")
    ap.add_argument("--abandon-k",type=float,default=0.0,
                    help="RELATIVE per-game ceiling: abandon a game past k x the median cost of its own "
                         "cell's first --abandon-calib games. This is the form that can serve a whole "
                         "matrix -- an absolute number cannot, because cells span 11 ms to 700 s per game "
                         "(measured on burn alone, six cells spanned 150x in median units). The sample is a "
                         "FIXED set of games (the lowest offsets), never a running median, so the abandoned "
                         "set stays a deterministic function of (deck, arm, depth, seed, k, calib) rather "
                         "than of thread interleave. The frozen ceiling is reported by the pool, stored per "
                         "cell, and handed back on every resume. 0 = off.")
    ap.add_argument("--quality-threshold",type=float,default=0.0075,
                    help="condemn a RUNG (depth d of an arm) once the extra depth is measured to buy less "
                         "than this many turns over d-1 -- a QUALITY verdict, not a cost one. Across 8 decks "
                         "the rung deltas are bimodal with a real gap (nothing between 0.0044 and 0.0078), so "
                         "0.0075 sits above every dead rung and below every live one, and is certifiable early "
                         "enough to save the run that is paying for the dead rung (~667 games/arm; a 0.005 "
                         "threshold would need 2.5x a FULL run and could never fire in time). The test is an "
                         "EQUIVALENCE test -- the one-sided 95%% upper bound on the paired improvement must "
                         "fall below it -- not a point estimate, which would condemn live rungs on noise. "
                         "0 disables.")
    ap.add_argument("--quality-min-pairs",type=int,default=200,
                    help="minimum paired games before --quality-threshold may fire. A floor against a "
                         "small-sample fluke; the equivalence test's own standard error does the real work "
                         "(too few games -> wide bound -> no verdict).")
    ap.add_argument("--quality-coverage",type=float,default=0.90,
                    help="the paired intersection must cover this fraction of the smaller rung's games before "
                         "a quality verdict is allowed, so a rung is never condemned off a biased subset.")
    ap.add_argument("--max-skip-frac",type=float,default=0.10,
                    help="stop abandoning once a (deck,seed) has skipped more than this fraction of --target. "
                         "Past that rate the games being dropped are not a tail but the distribution, and "
                         "filtering them silently changes what the table measures; it also cannot converge, "
                         "because the target grows by the skip count and a ceiling below a cell's own median "
                         "abandons every backfilled game too. The ceiling is disarmed for that deck+seed and "
                         "the table says so. 0 disables the cap (not recommended).")
    ap.add_argument("--abandon-calib",type=int,default=25,
                    help="games forming a cell's calibration sample for --abandon-k. Only the first "
                         "kCalibPrefix (3) of them run truly unbounded; the rest run under the PROVISIONAL "
                         "prefix ceiling, and --abandon-units applies throughout as a safety cap. The cell's "
                         "remaining games are held back until the sample completes -- a per-cell dependency, "
                         "not a barrier: every other cell keeps running. Was 10, on the argument that every "
                         "calibration game was unbounded; the provisional ceiling made the unbounded window a "
                         "constant, so the sample size is now free to buy what it is actually for -- a median "
                         "that does not land freakishly low and condemn ordinary games.")
    ap.add_argument("--drip",type=int,default=1,
                    help="games of a single NOT-YET-JUDGED condemnable cell allowed in flight at once. LPT would "
                         "otherwise put every thread on the most expensive cell in the table -- which is exactly "
                         "the one most likely to be discarded. The pool admits this many and refills on each "
                         "uncondemned completion, while the protected (d<=never-condemn) cells, which must run in "
                         "full regardless, keep the rest of the box busy. Default 1.")
    ap.add_argument("--workers",type=int,default=0,
                    help="threads for the ONE pooled batch. DEFAULT 0 = let the engine resolve it, which "
                         "means AffinityCpuCount() -- every core the process is actually allowed to use. "
                         "That is strictly better than a number chosen here: os.cpu_count() over-reports "
                         "inside a cgroup with a restricted cpuset, and any hand-picked value is one more "
                         "way to under-fill the box. Pass a number only to deliberately run small.")
    args=ap.parse_args()
    if not args.incremental:
        sys.exit("the monolithic path is REMOVED -- incremental batching is the only route "
                 "(one pooled queue, resumable, no per-cell tail). Drop --no-incremental.")
    if args.intractable_sec_per_game is not None:
        sys.exit("--intractable-sec-per-game is RETIRED: the tractability rule now judges the MEDIAN "
                 "cost per game, not the mean. The same number means something stricter under a "
                 "median (a mean carried tail inflation a median does not), so the limit has to be "
                 "re-chosen rather than carried over. Use --intractable-median-sec-per-game.")
    if args.never_condemn_at_or_below < 5:
        sys.exit("--never-condemn-at-or-below must be >= 5: the d<=5 H cells ARE the crossover, so "
                 "condemning one leaves a hole in the answer rather than saving cost.")
    if args.incremental:
        args.target = args.target if args.target is not None else args.games
        os.makedirs(os.path.dirname(args.out),exist_ok=True)
        run_incremental(args)
        return
    # NOTE: the monolithic per-cell path that used to live here is DELETED, not disabled. It called
    # run() once per (deck, depth, seed) -- a loop of ~50 separate `mtg` invocations, each with its own
    # load-imbalance tail and a serial gap between them. That is the shape CLAUDE.md forbids, and it is
    # what the 2026-08-10 post-mortem traced 23 hours at 3-of-20 cores back to. The guard above rejects
    # --no-incremental, but dead code that still reads as a working alternative is how a superseded
    # baseline gets re-adopted (see fivecolour-search-cost.md, where exactly that produced a retracted
    # 7x claim). run_incremental() -> run_pool() is the ONLY route: ONE manifest, ONE queue, ONE tail.
    if args.write_profile:
        # Fold THIS run's table straight into the play profile(s) -- one atomic command, no hand-stitched log.
        # Write this run's blocks to a fresh run-scoped log (NOT the appended --out) so the writer sees only
        # the ladder we just measured, then delegate to the completeness-guarded metadata writer.
        thisrun = args.out + ".thisrun.txt"
        with open(thisrun, "w") as f:
            f.write("\n".join(run_lines) + "\n")
        here = os.path.dirname(os.path.abspath(__file__))
        cmd = [sys.executable, os.path.join(here, "valueleaf_table_to_metadata.py"),
               thisrun, "--decks", *args.decks]
        if args.allow_partial:
            cmd.append("--allow-partial")
        print("\n=== --write-profile: folding this run's table into the play profile(s) ===", flush=True)
        rc = subprocess.run(cmd).returncode
        if rc != 0:
            print("!! profile write REFUSED (rc=%d): the ladder is incomplete. Extend --vdepths/--hdepths "
                  "to cover the deck's play target_depth and re-run, or pass --allow-partial." % rc)
            sys.exit(rc)


if __name__ == "__main__":
    main()
