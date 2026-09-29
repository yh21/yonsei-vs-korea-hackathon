import json,sys,subprocess,os
# usage: feed.py replay team turn [bin]   (feeds turns 1..turn; prints the bot's stderr + output for the final turn)
path,team,T=sys.argv[1],sys.argv[2],int(sys.argv[3])
binp=sys.argv[4] if len(sys.argv)>4 else './v18-work/zoo/rusher/bot_dbg'
d=json.load(open(path)); m=d['map']
opp='K' if team=='Y' else 'Y'
lines=['INIT %d %d'%(m['width'],m['height']),'TEAM '+team]
for r in m['terrain']: lines.append('MAP '+(''.join(r) if not isinstance(r,str) else r))
lines.append('BUILDINGS %d'%len(m['buildings']))
for b in m['buildings']: lines.append('%d %d %d %s'%(b['id'],b['x'],b['y'],b['type']))
lines.append('BASE Y %d %d'%tuple(m['bases']['Y'])); lines.append('BASE K %d %d'%tuple(m['bases']['K'])); lines.append('END')
inp='\n'.join(lines)+'\n'
bs={b['id']:b for b in m['buildings']}
def block(turn,state,team,init=False):
    L=['TURN %d'%turn,'RESOURCE %d %d'%(state['resources'][team] if not init else 10, state['resources'][opp] if not init else 10)]
    units=[] if init else state['units']
    order={'Y':0,'K':1}; ko={'F':0,'W':1,'S':2}
    units=sorted(units,key=lambda u:(order[u[0]],ko[u[1]],u[3],u[2]))
    L.append('UNITS %d'%len(units))
    for tm,k,x,y,c in units: L.append('%s %s %d %d %d'%(tm,k,x,y,c))
    L.append('BUILDINGS %d'%len(m['buildings']))
    rev=set(state['revealed'][team]) if not init else set()
    st={b['id']:b for b in state['buildings']} if not init else {}
    for b in m['buildings']:
        o=st.get(b['id'],{'owner':'N','stage':0})
        L.append('%d %d %d %s %s %d %d'%(b['id'],b['x'],b['y'],b['type'],o['owner'],o['stage'],b['score'] if b['id'] in rev else -1))
    L.append('END')
    return '\n'.join(L)+'\n'
inp+=block(1,None,team,True)
for t in range(1,T):
    inp+=block(t+1,d['turns'][t-1]['state'],team)
env=dict(os.environ); env['DBG_TURN']=str(T)
r=subprocess.run([binp],input=inp,capture_output=True,text=True,env=env)
outs=r.stdout.split('END\n')
print(r.stderr[-6000:])
print('OUTPUT turn',T,':',outs[T-1].strip().replace('\n',' | '))
print('ACTUAL   turn',T,':',d['turns'][T-1]['commands'][team] if T-1<len(d['turns']) else None)
