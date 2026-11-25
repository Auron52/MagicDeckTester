import json,subprocess,sys,os
# step.py <gi> <arm> <choices> [then-auto] [ENV=V...]
W=os.environ.get('WT','/workspaces/MagicDeckTester/.claude/worktrees/maze-rank3')
B=os.environ.get('BIN','/workspaces/MagicDeckTester/build/scratch/claude_maze_rank3/abtb/bin/mtg')
ARMS={'base':{'MTG_RANK_OBJECT_COLORS':'0','MTG_MAZE_RANK_SPENDABLE':'0'},'new':{'MTG_RANK_OBJECT_COLORS':'1','MTG_MAZE_RANK_SPENDABLE':'1'},'rank':{'MTG_RANK_OBJECT_COLORS':'1','MTG_MAZE_RANK_SPENDABLE':'0'}}
seed=int(sys.argv[1].split(':')[0]); gi=int(sys.argv[1].split(':')[1]); arm=sys.argv[2]; ch=sys.argv[3]; rest=sys.argv[4:]
auto = bool(rest) and rest[0]=='auto'
if auto: rest=rest[1:]
e=dict(os.environ); e['MTG_MAZE_HOLD_UP']='0'; e['MTG_PGO']='0'; e['MTG_START_LIFE']='30'; e['MTG_OPPONENT_HEADS']='2'; e.update(ARMS[arm]); e.update(dict(a.split('=',1) for a in rest))
cmd=[B,"decks/Maze's End/Maze's End.cod",'--profile',"decks/Maze's End/Maze's End.profile.json",'--claude-play','--seed',str(seed),'--game-index',str(gi),'--max-turns','10','--reveal','6','--choices',ch]
if auto: cmd+=['--choices-then-auto','--depth','8','--budget-ms','0','--ignore-play-profile']
p=subprocess.run(cmd,cwd=W,env=e,capture_output=True,text=True)
o=p.stdout
if auto:
    import re
    print('WIN', re.findall(r'"win_turn": *[0-9]+',o), [l for l in (o+p.stderr).splitlines() if 'then-auto' in l][:5]); sys.exit()
if '<<<CLAUDE_DECISION>>>' not in o: print(o[-3000:], p.stderr[-2000:]); sys.exit()
j=json.loads(o.split('<<<CLAUDE_DECISION>>>')[1].split('<<<END_DECISION>>>')[0])
print('TYPE',j['type'],'turn',j.get('turn'),'phase',j.get('phase'),'src',j.get('source'))
me=j.get('me',{})
if not os.environ.get('Q'): print(' bf',[(b['name'],b['num'],'T' if b.get('tapped') else '') for b in me.get('battlefield',[])])
if not os.environ.get('Q'): print(' hand',[h['name'] for h in me.get('hand',[])],'draws',me.get('upcoming_draws'))
for k,v in j.items():
    if k in ('me','opponent','note','plans','decision_index','type','turn','phase','source'): continue
    print(' ',k,json.dumps(v)[:800])
for p in j.get('plans',[]):
    print(' ',p.get('index'), p.get('summary'), p.get('drops',''), json.dumps({k:v for k,v in p.items() if k not in ('index','summary','drops')})[:400] if os.environ.get('V') else '')
