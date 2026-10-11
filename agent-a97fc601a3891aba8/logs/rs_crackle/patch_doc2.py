#!/usr/bin/env python3
"""Insert the d0-runner stall section before "## Still open" in the design doc. Numbers come from argv files."""
import sys
p = '/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8/docs/design/reality-spasm-crackle-classification.md'
s = open(p).read()
anchor = "## Still open (the USER's calls; not touched)"
assert s.count(anchor) == 1
body = open(sys.argv[1]).read()
s = s.replace(anchor, body + "\n" + anchor)
# The B4 bullet said "Not root-caused here" -- point it at the new section.
s = s.replace("  1773). Not root-caused here. Also, `HinataCastableFromHand` counts SOURCES, not mana, so Sol Ring and\n"
              "  a bounce land under-count it. On gi 254 it read 3 sources (5 mana) as \"not castable\".",
              "  1773). Root-caused and fixed below (\"The d0-runner stall\"). Also, `HinataCastableFromHand` counted\n"
              "  SOURCES, not mana, so Sol Ring and a bounce land under-counted it (gi 254: 3 sources, 5 mana).")
open(p, 'w').write(s)
print('doc patched')
