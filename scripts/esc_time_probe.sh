MTG=build/Release/mtg
for spec in "treasure_hunt/treasure_hunt.txt:th:8" "burn/burn.txt:burn:8"; do
  f="${spec%%:*}"; rest="${spec#*:}"; name="${rest%%:*}"; mt="${rest##*:}"
  echo "=== $name d5 80g s1001 ==="
  for cfg in "NOESCAL:MTG_VALUE_MODEL=1 MTG_VALUE_MIN_DEPTH=0" "HYBRID:MTG_VALUE_MODEL=1"; do
    label="${cfg%%:*}"; envs="${cfg#*:}"
    t0=$(date +%s.%N)
    out=$(env $envs $MTG "decks/$f" --games 80 --seed 1001 --depth 5 --max-turns "$mt" --lookahead-bottoming --threads 12 2>/dev/null)
    t1=$(date +%s.%N)
    w=$(echo "$out"|grep -oP 'won\s*:\s*\K\d+'); a=$(echo "$out"|grep -oP 'Avg win turn\s*:\s*\K[\d.]+')
    printf "  %-9s %6.1fs  won=%s avg=%s\n" "$label" "$(echo "$t1-$t0"|bc)" "$w" "$a"
  done
done
