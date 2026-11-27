import json,glob,sys
# turnlog.py <dir> <turn>: compact action list of one turn from a game log
f=glob.glob(sys.argv[1]+'/*.json')[0]; g=json.load(open(f)); T=int(sys.argv[2])
for ph in g['turns']:
    if ph['turn']!=T: continue
    for a in ph['actions']:
        s=' '.join('%s=%s'%(k,v) for k,v in a.items() if k not in ('card',))
        print(ph['phase'], s[:260])
    b=ph['boardAfter']; print('  board:',[ (x['cardName'],x.get('tapped')) for x in b['battlefield']], 'opp', b['opponentLife'])
