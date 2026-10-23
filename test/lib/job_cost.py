#!/usr/bin/env python3
"""Measured per-job cost table for the regression harness's LPT scheduling (test/job_cost.tsv).

USER 2026-10-23: smoke ran 13m15s against a 15-minute target while its pooled work was ~140 core-minutes
(~6.5 min at 22 threads). The tail was a few expensive jobs (NivMizzetCombo / Bruna d5) dispatched late
by the depth/budget proxy, so their slowest games finished long after everything else. Ordering whole
jobs by their MEASURED cost puts those chunks first and lets the cheap ones backfill.

The table maps a case key (<deck>_<mode>_d<depth>_s<seed>) to its work UNITS per game -- the
deterministic work meter, not wall time, so it does not depend on how busy the box was. It is written
only by `regression.sh --accept` (the same step that promotes ground truth), and read when the
manifest is emitted, where it becomes the job's `weight`. Scheduling only: the batch is seeded per
game, so order never changes a result.

  job_cost.py update <table> <batch.log> <results.env>   merge the accepted run's keys into the table
  job_cost.py emit   <table>                              print "<key> <weight>" lines for bash
"""
import re
import sys


def read_table(path):
    table = {}
    try:
        with open(path) as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                key, units = line.split()[:2]
                table[key] = int(units)
    except FileNotFoundError:
        pass
    return table


def write_table(path, table):
    with open(path, "w") as f:
        f.write("# Per-job work UNITS per game, from the last accepted run of each key (test/lib/job_cost.py).\n")
        f.write("# Scheduling only (regression.sh turns it into the job's LPT weight) -- never a result.\n")
        for key in sorted(table):
            f.write(f"{key}\t{table[key]}\n")


def update(table_path, batch_log, results_env):
    accepted = set()
    with open(results_env) as f:
        for line in f:
            m = re.match(r"^([A-Za-z0-9_]+)=", line)
            if m:
                accepted.add(m.group(1))
    table = read_table(table_path)
    pat = re.compile(r"^(\S+): played=(\d+) .*\bunits=(\d+)")
    n = 0
    with open(batch_log) as f:
        for line in f:
            m = pat.match(line)
            if not m or m.group(1) not in accepted:
                continue
            played, units = int(m.group(2)), int(m.group(3))
            if played > 0:
                table[m.group(1)] = max(1, units // played)
                n += 1
    write_table(table_path, table)
    print(f"Recorded {n} job cost(s) into {table_path} (LPT scheduling for the next run).")


def emit(table_path):
    for key, units in read_table(table_path).items():
        print(key, units)


if __name__ == "__main__":
    if len(sys.argv) == 5 and sys.argv[1] == "update":
        update(sys.argv[2], sys.argv[3], sys.argv[4])
    elif len(sys.argv) == 3 and sys.argv[1] == "emit":
        emit(sys.argv[2])
    else:
        sys.exit(__doc__)
