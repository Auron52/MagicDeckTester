MTG=build/Release/mtg
# $1 = deck path RELATIVE to decks/ under the per-deck folder layout (e.g. treasure_hunt/treasure_hunt.txt)
f="$1"; name="$2"; mt="$3"
mkdir -p logs/eval
for s in 1001 2002 3003 4004 5005 6006; do
  MTG_VALUE_MODEL=1 MTG_ESCALATION_DUMP="logs/eval/escdump_${name}.txt" \
    $MTG "decks/$f" --games 250 --seed $s --depth 5 --max-turns "$mt" --lookahead-bottoming --threads 12 >/dev/null 2>&1
done
echo "$name: $(wc -l < logs/eval/escdump_${name}.txt) rows"
