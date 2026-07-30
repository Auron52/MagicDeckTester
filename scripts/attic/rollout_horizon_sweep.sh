MTG=build/Release/mtg
for spec in "treasure_hunt.txt:th:8" "burn.txt:burn:8" "Anti-Lifegain.cod:antilife:10"; do
  f="${spec%%:*}"; rest="${spec#*:}"; name="${rest%%:*}"; mt="${rest##*:}"
  echo "=== $name d5 120g s1001 (MTG_VALUE_MODEL=1) ==="
  for K in off 4 3 2 1; do
    envK=""; [ "$K" != "off" ] && envK="MTG_ROLLOUT_HORIZON=$K"
    t0=$(date +%s.%N)
    out=$(env MTG_VALUE_MODEL=1 $envK $MTG "decks/$f" --games 120 --seed 1001 --depth 5 --max-turns "$mt" --lookahead-bottoming --threads 12 2>/dev/null)
    t1=$(date +%s.%N)
    w=$(echo "$out"|grep -oP 'won\s*:\s*\K\d+'); a=$(echo "$out"|grep -oP 'Avg win turn\s*:\s*\K[\d.]+')
    lp=$(python3 -c "w=$w;a=$a;p=120;mt=$mt;print(f'{(w*a+(p-w)*(mt+1))/p:.3f}')")
    printf "  K=%-3s %6.1fs  won=%s avg=%s  LP=%s\n" "$K" "$(echo "$t1-$t0"|bc)" "$w" "$a" "$lp"
  done
done
