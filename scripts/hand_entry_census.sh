#!/usr/bin/env bash
# SIZE THE HOLE, before building step 2 of docs/design/breakpoints-should-key-on-hand-entry.md.
#
# THE QUESTION. Today a breakpoint can only be armed for a hand entry that happens INSIDE a cast's
# apply window: both the param-keyed sites and the general site-10 rule hang off a before/after hand
# comparison bracketing one cast (`hand_at_cast` in the rollout, `rdb_hand` in the executor). An
# entry OUTSIDE that window -- a combat-damage trigger, an activated ability, a death trigger, an
# upkeep put -- arms nothing, at any depth or budget, and the failure is silent.
#
# The design doc asks for this number before the refactor: *"a counter at the unarmed routes, run
# over all suite decks, says which of them actually fire and how often. That decides whether this is
# a Goblins fix or an everything fix."* MTG_HAND_ENTRY_CENSUS=1 prints it per route, split into
# in-cast and OUTSIDE.
#
# WHY ONE PROCESS PER DECK rather than one pooled batch: the census is a per-PROCESS report, so a
# pooled run over every deck would merge them and lose exactly the per-deck split the question is
# about. The decks run CONCURRENTLY at THREADS each, sized so the box stays saturated to one tail --
# the pooling rule's purpose (no idle cores, no barrier), met without merging the reports.

set -u
cd /workspaces/MagicDeckTester2

OUT=logs/handentry/census
mkdir -p "$OUT"
LOG=$OUT/census.log
log() { echo "[$(date -u '+%m-%d %H:%M:%S')] $*" | tee -a "$LOG"; }

BIN=build/Release/mtg
GAMES=${GAMES:-40}
DEPTH=${DEPTH:-3}
BUDGET=${BUDGET:-10}
THREADS=${THREADS:-3}
SEED=${SEED:-1001}

# The decks that can answer the question. goblins is the doc's named live instance (Lackey /
# Muxus put a Goblin Matron into play OUTSIDE any cast, and Matron's ETB tutors); Fungus is the
# deck currently being optimised; the rest span the breakpoint classes (kitty = site 6 equipment
# draw, th = DrawUntilNonland, hinata = cantrip chains, melira = pod, mirrorwing = trick payload).
DECKS=(
  "goblins    decks/Goblins/Goblins.cod"
  "fungus     decks/Fungus/Fungus.cod"
  "kitty      decks/KittyEquipment/KittyEquipment.cod"
  "th         decks/treasure_hunt/treasure_hunt.txt"
  "hinata     decks/Hinata2/Hinata2.cod"
  "melira     decks/Melira Pod/Melira Pod.cod"
  "mirrorwing decks/Mirrorwing Dragon/Mirrorwing Dragon.cod"
  "knights    decks/Knights/Knights.cod"
)

log "=== hand-entry census START (HEAD $(git rev-parse --short HEAD)) ==="
log "games=$GAMES depth=$DEPTH budget=$BUDGET threads=$THREADS seed=$SEED, ${#DECKS[@]} decks concurrently"

pids=()
for row in "${DECKS[@]}"; do
    tag=${row%% *}
    file=$(echo "$row" | sed 's/^[^ ]* *//')
    prof="${file%.*}.profile.json"
    if [ ! -f "$file" ]; then log "SKIP $tag: no decklist at $file"; continue; fi
    if [ ! -f "$prof" ]; then log "note $tag: no profile at $prof -- running without one"; prof=""; fi
    args=("$file")
    [ -n "$prof" ] && args+=(--profile "$prof")
    args+=(--seed "$SEED" --games "$GAMES" --depth "$DEPTH" --budget-ms "$BUDGET"
           --threads "$THREADS" --max-turns 12)
    MTG_HAND_ENTRY_CENSUS=1 MTG_BATCH_HEARTBEAT=0 \
        "$BIN" "${args[@]}" > "$OUT/$tag.log" 2>&1 &
    pids+=($!)
    log "launched $tag (pid ${pids[-1]})"
done

for p in "${pids[@]}"; do wait "$p"; done
log "all decks finished"

# ---- Report ------------------------------------------------------------------------------------
log ""
log "=== THE HOLE, per deck (entries OUTSIDE a cast apply arm nothing today) ==="
for row in "${DECKS[@]}"; do
    tag=${row%% *}
    [ -f "$OUT/$tag.log" ] || continue
    line=$(grep -m1 'new material, of which' "$OUT/$tag.log")
    log "$(printf '%-12s %s' "$tag" "${line:-(no hand entries recorded)}")"
done

log ""
log "=== per-route detail ==="
for row in "${DECKS[@]}"; do
    tag=${row%% *}
    [ -f "$OUT/$tag.log" ] || continue
    if grep -q 'hand-entry' "$OUT/$tag.log"; then
        log "---- $tag ----"
        grep 'hand-entry' "$OUT/$tag.log" | tee -a "$LOG" > /dev/null
        grep 'hand-entry' "$OUT/$tag.log"
    fi
done
log "=== census COMPLETE -- full logs in $OUT ==="
