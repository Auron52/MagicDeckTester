#!/bin/bash
# Exercise harness_install_bin via test/snapshot_bin.sh, then a second install over the same dest.
set -u
cd /workspaces/MagicDeckTester
bash test/snapshot_bin.sh offmount-selftest
ls -la logs/snapshots/offmount-selftest logs/snapshots/offmount-selftest.meta
readlink logs/snapshots/offmount-selftest
stat -f -c %T "$(readlink -f logs/snapshots/offmount-selftest)"
cmp build/Release/mtg logs/snapshots/offmount-selftest && echo "bytes identical"
# re-install over the existing symlink: must stay a symlink, bytes replaced on the volume
. test/lib/harness.sh
harness_install_bin build/Release/mtg-analyze logs/snapshots/offmount-selftest
ls -la logs/snapshots/offmount-selftest
cmp build/Release/mtg-analyze logs/snapshots/offmount-selftest && echo "re-install bytes identical"
ls build/offmount/logs/snapshots/
