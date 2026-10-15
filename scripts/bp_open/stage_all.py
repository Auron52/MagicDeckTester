#!/usr/bin/env python3
"""stage_data.sh for ALL suite decks: copy each deck's folder (files only, minus *.raw.json.gz, which play
never reads) from this worktree onto the build volume, once, so a batch reads nothing over 9p per job."""
import os, shutil, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_ab import ALL_DECKS  # noqa: E402

W = "/workspaces/MagicDeckTester/.claude/worktrees/agent-a9469f45bff622cfb"
D = "/workspaces/MagicDeckTester/build/scratch/bp_open/data"
os.makedirs(D + "/src/cards/data", exist_ok=True)
shutil.copyfile(W + "/src/cards/data/cards.json", D + "/src/cards/data/cards.json")
n = 0
for key, deck, prof in ALL_DECKS:
    src = os.path.join(W, os.path.dirname(deck))
    dst = os.path.join(D, os.path.dirname(deck))
    os.makedirs(dst, exist_ok=True)
    for f in sorted(os.listdir(src)):
        sp = os.path.join(src, f)
        if not os.path.isfile(sp) or f.endswith(".raw.json.gz"):
            continue
        dp = os.path.join(dst, f)
        if os.path.exists(dp) and os.path.getsize(dp) == os.path.getsize(sp) and os.path.getmtime(dp) >= os.path.getmtime(sp):
            continue
        shutil.copy2(sp, dp)
        n += 1
head = subprocess.run(["git", "-C", W, "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
open(D + "/SOURCE_HEAD", "w").write(head + "\n")
print("copied", n, "files for", len(ALL_DECKS), "decks")
