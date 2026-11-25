#!/bin/bash
# parallel hand-offs: ho2.sh seed gi tag choices arm... (each on 1 lane slot)
seed=$1; gi=$2; tag=$3; ch=$4; shift 4
for a in "$@"; do bash /workspaces/MagicDeckTester/scripts/box_lane.sh --slots 1 --cap-gb 8 -- ./ho.sh $seed $gi $a $tag "$ch" $HOENV 2>/dev/null & done; wait
