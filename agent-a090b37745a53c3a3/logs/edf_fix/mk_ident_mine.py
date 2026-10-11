#!/usr/bin/env python3
"""Combine the identity manifests into one pooled batch for my binary: def_* (levers at defaults) and
off_* (all five levers =0). The tip binary ran ident_def.json alone (it knows none of the flags)."""
import json
S = "/workspaces/MagicDeckTester/build/scratch/agent-a090b37745a53c3a3"
a = json.load(open(S + "/ident_def.json"))["jobs"]
b = json.load(open(S + "/ident_off.json"))["jobs"]
for j in a:
    j["name"] = "def_" + j["name"]
for j in b:
    j["name"] = "off_" + j["name"]
json.dump({"jobs": a + b}, open(S + "/ident_mine.json", "w"), indent=1)
print("wrote %s/ident_mine.json: %d jobs" % (S, len(a + b)))
