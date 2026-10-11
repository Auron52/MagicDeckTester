import json, subprocess, os, glob, copy
R='/workspaces/MagicDeckTester'; K=R+'/logs/discard_impl/rc3'
BIN=K+'/../ab3/mtg_ab3_d6a760e5'
games={'275':4,'374':5,'400':5,'500':4,'839':4,'912':4,'1054':5,'1063':4,'1191':6,'1665':4,'1700':5,'1760':5,'1817':5,'1882':4}
for gi,t in games.items():
    sp=K+'/fivecolour/sc/%s.json'%gi
    if gi=='275': sp=K+'/fivecolour/scen275/spec.json'
    s=json.load(open(sp)); base=s['base']
    L=s.get('real_opp_life')
    if L is None:
        L=15
    if 'library' not in base: base['library']=['Blood Crypt','Stomping Ground','Overgrown Tomb','Steam Vents','Breeding Pool','Godless Shrine']
    out=[]
    for arm,env,cj in (('old',{l:'0' for l in ["MTG_ATTACK_TAP_NEED_EXACT","MTG_ATTACK_TAP_ABILITY_DISCOUNT"]},None),('new',{},None),('new+yield5',{},K+'/fivecolour/cards_yield5.json'),('old+yield5',{l:'0' for l in ["MTG_ATTACK_TAP_NEED_EXACT","MTG_ATTACK_TAP_ABILITY_DISCOUNT"]},K+'/fivecolour/cards_yield5.json')):
        sc=copy.deepcopy(base); sc['opponent_life']=L; sc['env']=env
        if cj: sc['cards_json']=cj
        lo=K+'/fivecolour/sc/p_%s_%s.log.json'%(gi,arm.replace('+','_')); sc['log_out']=lo
        fp=K+'/fivecolour/sc/p_%s_%s.scen.json'%(gi,arm.replace('+','_')); json.dump(sc,open(fp,'w'))
        p=subprocess.run(['nice','-n','10',BIN,'--scenario',fp],cwd=R,capture_output=True,text=True)
        win=[l.split()[1] for l in (p.stdout+p.stderr).splitlines() if l.startswith('scenario: win_turn=')]
        g=json.load(open(lo)); casts=[]
        for tt in g['turns']:
            if tt['turn']==t and tt.get('phase')=='MAIN_1':
                casts+=[a.get('cardName','') for a in tt.get('actions',[]) if a.get('type')=='CAST_SPELL']
        out.append('%s:%s[%s]'%(arm,(win or ['?'])[0].split('=')[1],'+'.join(c.split(',')[0] for c in casts)))
    print('gi%-5s L=%-3s '%(gi,L)+'  '.join(out))
