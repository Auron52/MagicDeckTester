import json, os, sys
A = '/workspaces/MagicDeckTester/build/scratch/claude_ovn1120/agent'
arm = sys.argv[1] if len(sys.argv) > 1 else 'new'
man = 'd8b0.json' if arm == 'new' else 'd8b0_old.json'
if len(sys.argv) > 2:
    man = sys.argv[2]
names = [j['name'] for j in json.load(open(f'{A}/{man}'))['jobs']]
d = sys.argv[3] if len(sys.argv) > 3 else f'{A}/{arm}/wins'
have = {f[:-5] for f in os.listdir(d)} if os.path.isdir(d) else set()
miss = [n for n in names if n not in have]
print(len(miss), 'pending:', ' '.join(miss[:40]))
