#!/bin/bash
# claude-play replay: print the decision after CHOICES ($1); optional THEN=1 hands off to the search.
R=/workspaces/MagicDeckTester
O=$R/logs/discard_impl/rc/melira
CH="$1"; FLAG=${FLAG:-1}; THEN=${THEN:-0}; TAG=${TAG:-cp}
mkdir -p "$O/$TAG"
EXTRA=""
if [ "$THEN" = 1 ]; then EXTRA="--choices-then-auto"; fi
(cd $R; MTG_POD_BUCKET_DISCARD=$FLAG nice -n 19 build/PGO/mtg "decks/Melira Pod/Melira Pod.cod" \
    --profile "decks/Melira Pod/Melira Pod.profile.json" --claude-play $EXTRA --seed 2035 --game-index 33 \
    --depth 3 --budget-ms 10 --ignore-play-profile --threads 1 --reveal 3 --choices "$CH" --log-dir "$O/$TAG" \
    > "$O/$TAG/out.txt" 2> "$O/$TAG/err.txt"; echo "exit=$?" >> "$O/$TAG/out.txt")
cat "$O/$TAG/out.txt" | head -${LINES_OUT:-120}
grep -E "then-auto|play\]|win" "$O/$TAG/err.txt" | head -20
