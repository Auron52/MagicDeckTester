#!/bin/bash
D=/workspaces/MagicDeckTester/logs/discard_impl/rc/selesnya
for g in 175 344; do
  for a in on off distoff; do
    python3 $D/dump.py $D/g${g}_$a > $D/g${g}_$a.txt
  done
done
