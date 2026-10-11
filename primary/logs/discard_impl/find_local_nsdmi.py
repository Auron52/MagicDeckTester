#!/usr/bin/env python3
"""Find FUNCTION-LOCAL structs with default member initializers in src/, and whether the same function value-constructs
them through a container (std::vector<T> x(n) / .resize / .emplace_back() / std::array) -- the shape MSVC 14.51 rejects
(C2672 std::construct_at). Local = defined at indentation >= 4 inside a function body (heuristic: the line before the
struct's enclosing block is not a namespace/class)."""
import re, sys, glob

files = sorted(glob.glob("/workspaces/MagicDeckTester/src/**/*.cpp", recursive=True) +
               glob.glob("/workspaces/MagicDeckTester/src/**/*.h", recursive=True))
for f in files:
    lines = open(f, encoding="utf-8", errors="replace").read().split("\n")
    for i, l in enumerate(lines):
        m = re.match(r"^(\s{4,})struct ([A-Za-z_][A-Za-z0-9_]*)\s*(\{.*)?$", l)
        if not m:
            continue
        ind, name = m.group(1), m.group(2)
        # collect the struct body
        body, depth, j = [], 0, i
        started = False
        while j < len(lines):
            body.append(lines[j]); depth += lines[j].count("{") - lines[j].count("}")
            if "{" in lines[j]: started = True
            if started and depth <= 0: break
            j += 1
        text = "\n".join(body)
        # NSDMI: a member line with '=' that is not inside a method, crude: a ';'-terminated line with ' = ' or '{...}' init
        nsdmi = any(re.search(r"[A-Za-z_][A-Za-z0-9_<>,:\* ]+\s[A-Za-z_][A-Za-z0-9_]*\s*(=|\{)[^;(]*;", b) and "(" not in b.split("=")[0]
                    for b in body[1:])
        if not nsdmi:
            continue
        # does the enclosing function (next ~400 lines) value-construct it via a container?
        tail = "\n".join(lines[j:j + 400])
        uses = re.findall(r"std::vector<%s>\s+\w+\s*\(|std::vector<%s>\s*\(\s*[^)]|\.resize\(|std::array<%s" % (name, name, name), tail)
        print("%s:%d struct %s (indent %d) NSDMI=yes container-uses=%s" % (f.replace("/workspaces/MagicDeckTester/", ""), i + 1, name, len(ind), uses[:3]))
