#!/bin/bash
# Send SIGINT to the gdb inferior (the mtg process whose parent is a gdb running gdb_probe.sh for OUT)
# every INTERVAL seconds, so gdb prints a backtrace. Stops when the inferior is gone.
# usage: sampler.sh <gdb_pid> <interval_s>
GDBPID=$1; INT=${2:-120}
sleep 20
CHILD=$(ps -o pid= --ppid "$GDBPID" | head -1 | tr -d ' ')
echo "gdb=$GDBPID child=$CHILD"
while kill -0 "$CHILD" 2>/dev/null; do
  sleep "$INT"
  kill -0 "$CHILD" 2>/dev/null || break
  kill -INT "$CHILD"
  echo "sampled at $(date +%T)"
done
echo "inferior gone at $(date +%T)"
