import json,sys
def ev(f):
  o=open(f).read(); j=json.loads(o.split('<<<CLAUDE_RESULT>>>')[1].split('<<<END_RESULT>>>')[0])
  return [f"T{e['turn']} {e['kind']}: {e['text'][:160]}" for e in j['events'] if 'opponent' not in e['text']]
a=ev(sys.argv[1]); b=ev(sys.argv[2])
import difflib
for l in difflib.unified_diff(a,b,lineterm='',n=int(sys.argv[3]) if len(sys.argv)>3 else 3): print(l)
