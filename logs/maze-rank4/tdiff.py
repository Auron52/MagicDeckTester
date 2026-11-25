import json,sys
# tdiff.py A.json B.json [-v]  : per phase-record diff incl. board (tapped) and hand
def load(p):
  d=json.load(open(p)); names={}
  out=[]
  for t in d['turns']:
    acts=[]
    for a in t.get('actions',[]):
      s=a.get('type','')+' '+a.get('cardName','')+'#'+str(a.get('card',''))
      for k in ('manaPaid','source','lookedAt','kept','bottomed'):
        if k in a: s+=f' {k}={a[k]}'
      acts.append(s)
    b=t.get('boardAfter',{})
    for c in b.get('battlefield',[]): names[c['card']]=c['cardName']
    bf=sorted(f"{c['cardName']}#{c['card']}{'(T)' if c.get('tapped') else ''}" for c in b.get('battlefield',[]))
    out.append((t.get('turn'),t.get('phase'),acts,bf,b.get('hand',[]),b.get('graveyard',[]),b.get('opponentLife')))
  return d,out
da,a=load(sys.argv[1]); db,b=load(sys.argv[2])
print('result',da['result'],'|',db['result'])
first=True
for i,(x,y) in enumerate(zip(a,b)):
  same=x==y
  print(f'--- rec {i} T{x[0]} {x[1]} / T{y[0]} {y[1]}'+('' if same else '  <<< DIFF'))
  if not same or '-v' in sys.argv:
    print('  A acts:',x[2]); print('  B acts:',y[2])
    if x[3]!=y[3]: print('  A bf:',x[3]); print('  B bf:',y[3])
    else: print('  bf same:',x[3])
    print('  A hand:',x[4],'B hand:',y[4])
    if x[5]!=y[5]: print('  A gy:',x[5],'B gy:',y[5])
    if '-all' not in sys.argv and not same:
      if first: first=False; 
