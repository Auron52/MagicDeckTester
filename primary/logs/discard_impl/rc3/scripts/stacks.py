#!/usr/bin/env python3
"""Summarise gdb 'thread apply all bt' samples: for the worker thread (the one not in main()/join),
print the function-name stack (outermost first, compressed) per sample.
usage: stacks.py <gdb_stdout.txt> [--full]"""
import sys, re

def fname(line):
    # '#3  0x... in Foo::bar(...) (...) at file:line' or '#3  Foo::bar(...) at ...'
    m = re.match(r'#(\d+)\s+(?:0x[0-9a-f]+ in )?(.+?)(?: \(.*\))?(?: at .*| from .*)?$', line)
    if not m:
        return None
    name = m.group(2)
    name = re.sub(r'\(.*', '', name)          # drop argument lists
    name = name.replace('[clone .lto_priv.0]', '').strip()
    return name

def main():
    txt = open(sys.argv[1], errors='replace').read()
    full = '--full' in sys.argv
    samples = txt.split('==== SAMPLE ')[1:]
    for s in samples:
        num = s.split('\n', 1)[0]
        threads = re.split(r'\nThread ', s)
        for th in threads[1:]:
            frames = [l.strip() for l in th.split('\n') if l.strip().startswith('#')]
            names = [fname(f) for f in frames]
            names = [n for n in names if n]
            if any('main' == n or n.startswith('std::thread::join') for n in names[:8]):
                continue
            outer = list(reversed(names))
            # compress consecutive duplicates
            comp = []
            for n in outer:
                short = n.split('::')[-1] if not full else n
                if comp and comp[-1][0] == short:
                    comp[-1][1] += 1
                else:
                    comp.append([short, 1])
            print('SAMPLE %s depth=%d' % (num, len(names)))
            print('   ' + ' > '.join('%s%s' % (c[0], ('x%d' % c[1]) if c[1] > 1 else '') for c in comp))

main()
