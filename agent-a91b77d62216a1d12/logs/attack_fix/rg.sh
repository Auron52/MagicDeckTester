#!/usr/bin/env bash
# rg.sh <deck> <cell> <gi> <seed> <logdir|-> [extra args...]
# One-game repro on THIS worktree's build (binary from the build volume; BIN overrides), under
# lowprio.sh. Env passes through (MTG_* levers). Put <logdir> under build/scratch, never on 9p.
set -u
W=/workspaces/MagicDeckTester/.claude/worktrees/agent-a91b77d62216a1d12
BIN=${BIN:-$W/build/Release/mtg}
deck=$1; cell=$2; gi=$3; seed=$4; logdir=$5; shift 5
case $deck in
  fivecolour) D=decks/FiveColour/FiveColour.cod; P=decks/FiveColour/FiveColour.profile.json;;
  stompy) D=decks/StompySurprise/StompySurprise.cod; P=decks/StompySurprise/StompySurprise.profile.json;;
  selesnya) D=decks/SelesnyaLifegain/SelesnyaLifegain.cod; P=decks/SelesnyaLifegain/SelesnyaLifegain.profile.json;;
  bruna) D=decks/Bruna/Bruna.cod; P=decks/Bruna/Bruna.profile.json;;
  snow) D=decks/Snow/Snow.cod; P=decks/Snow/Snow.profile.json;;
  giants) D=decks/Giants/Giants.cod; P=decks/Giants/Giants.profile.json;;
  critter) D=decks/CritterLifegain/CritterLifegain.cod; P=decks/CritterLifegain/CritterLifegain.profile.json;;
  angels) D=decks/Angels/Angels.cod; P=decks/Angels/Angels.profile.json;;
  *) echo "unknown deck $deck"; exit 2;;
esac
case $cell in
  d0) CA=(--depth 0 --ignore-play-profile);;
  d3) CA=(--depth 3 --budget-ms 10 --ignore-play-profile);;
  d3b0) CA=(--depth 3 --budget-ms 0 --ignore-play-profile);;
  d8b0) CA=(--depth 8 --budget-ms 0 --ignore-play-profile);;
  ship) CA=(--budget-ms 20);;
  *) echo "bad cell"; exit 2;;
esac
LA=()
if [ "$logdir" != - ]; then mkdir -p "$logdir"; LA=(--log-dir "$logdir"); fi
cd "$W"
bash /workspaces/MagicDeckTester/build/scratch/lowprio.sh "$BIN" "$D" --profile "$P" --seed "$seed" --game-index "$gi" --games 1 "${CA[@]}" --threads 1 "${LA[@]}" "$@"
