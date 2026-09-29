"""리플레이를 엔진으로 재생하며 전투 손실(W/F)을 턴별/누적 집계. usage: battle.py replay.json [every]"""
import json,sys,os
ROOT=os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0,os.path.join(ROOT,'yk-development-tools'))
from engine import pipeline
from engine.commands import Move,Move2,Tele,Spawn,Priority
from mapgen import generate,to_state
d=json.load(open(sys.argv[1])); every=int(sys.argv[2]) if len(sys.argv)>2 else 10
cfg=d['config']; st=to_state(generate(d['seed'],cfg))
def mk(c):
    k=c['cmd']
    if k=='Spawn': return Spawn(c['kind'],c['count'],c['x'],c['y'])
    if k=='Move': return Move(c['x'],c['y'],c['kind'],c['count'],c['direction'])
    if k=='Move2': return Move2(c['x'],c['y'],c['count'],c['dir1'],c['dir2'])
    if k=='Tele': return Tele(c['x'],c['y'],c['kind'],c['count'],c['tx'],c['ty'])
    if k=='Priority': return Priority([tuple(p) for p in c['coords']])
orig=pipeline.step_combat
loss={'Y':{'W':0,'F':0},'K':{'W':0,'F':0}}; rec={}
def wrap(state,events=None):
    b={t:{k:sum(c for (x,y,tt,kk),c in state.units.items() if tt==t and kk==k) for k in 'FW'} for t in 'YK'}
    orig(state,events)
    a={t:{k:sum(c for (x,y,tt,kk),c in state.units.items() if tt==t and kk==k) for k in 'FW'} for t in 'YK'}
    for t in 'YK':
        for k in 'FW': loss[t][k]+=b[t][k]-a[t][k]
pipeline.step_combat=wrap
print(d['teams']['Y']['name'],'(Y) vs',d['teams']['K']['name'],'(K)',d['result'])
for t in d['turns']:
    st,res=pipeline.run_turn(st,[mk(c) for c in t['applied']['Y']],[mk(c) for c in t['applied']['K']],[])
    n=t['turn']
    if n%every==0 or res:
        s=t['state']; u={'Y':[0,0],'K':[0,0]}
        for tm,k,x,y,c in s['units']:
            if k=='F':u[tm][0]+=c
            if k=='W':u[tm][1]+=c
        print(f"t{n:3d} lostW Y{loss['Y']['W']:4d} K{loss['K']['W']:4d} lostF Y{loss['Y']['F']:3d} K{loss['K']['F']:3d} | alive F/W Y{u['Y']} K{u['K']} | pts Y{pipeline.team_score(st,'Y')} K{pipeline.team_score(st,'K')}")
    if res: break
