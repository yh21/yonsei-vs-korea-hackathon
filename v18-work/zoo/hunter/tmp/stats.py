import json,sys,re
d=json.load(open(sys.argv[1]))
me=sys.argv[2] if len(sys.argv)>2 else 'Y'
op='K' if me=='Y' else 'Y'
sp={me:{'F':0,'W':0,'S':0},op:{'F':0,'W':0,'S':0}}
for t in d['turns']:
    for tm in (me,op):
        for c in t['commands'][tm]:
            m=re.match(r'SPAWN (\w) (\d+)',c)
            if m: sp[tm][m.group(1)]+=int(m.group(2))
last=d['turns'][-1]['state']['units']
alive={me:{'F':0,'W':0,'S':0},op:{'F':0,'W':0,'S':0}}
for tm,k,x,y,c in last: alive[tm][k]+=c
print('spawned',sp,'alive',alive)
# events by window
w=20
ev={}
for t in d['turns']:
    n=(t['turn']-1)//w
    for e in t['events']:
        if e['type'] in('capture','neutralize'):
            key=(n,e['type'],e.get('team','?'))
            ev[key]=ev.get(key,0)+1
for k in sorted(ev): print(k,ev[k])
