import sys,json,subprocess,os
# findplan.py seed:gi arm choices  -> index + actions for every plan (compact)
e=dict(os.environ); e['V']='1'
o=subprocess.run(['python3','step.py']+sys.argv[1:4],capture_output=True,text=True,env=e).stdout
for l in o.splitlines():
  if not l.startswith('  ') or '{' not in l: continue
  i=l.split()[0]
  if not i.lstrip('-').isdigit(): continue
  try: j=json.loads(l[l.index('{'):])
  except Exception: continue
  print(i, 'land=',j.get('land'), [(a['card'],a.get('tutor_target')) for a in j.get('actions',[])])
