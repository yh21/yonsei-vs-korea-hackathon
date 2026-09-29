"""전투 상세: fights.py replay.json t0 t1  -> 해당 턴 범위에서 전투가 일어난 셀(전/후 병력)"""
import json,sys,os
ROOT=os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0,os.path.join(ROOT,'yk-development-tools'))
from engine import pipeline
from engine.commands import Move,Move2,Tele,Spawn,Priority
from mapgen import generate,to_state
d=json.load(open(sys.argv[1])); t0=int(sys.argv[2]); t1=int(sys.argv[3])
cfg=d['config']; st=to_state(generate(d['seed'],cfg))
def mk(c):
    k=c['cmd']
    if k=='Spawn': return Spawn(c['kind'],c['count'],c['x'],c['y'])
    if k=='Move': return Move(c['x'],c['y'],c['kind'],c['count'],c['direction'])
    if k=='Move2': return Move2(c['x'],c['y'],c['count'],c['dir1'],c['dir2'])
    if k=='Tele': return Tele(c['x'],c['y'],c['kind'],c['count'],c['tx'],c['ty'])
    if k=='Priority': return Priority([tuple(p) for p in c['coords']])
orig=pipeline.step_combat
cur=[0]
def cellstate(state,x,y):
    return {t:{k:state.get_unit(x,y,t,k) for k in 'FWS'} for t in 'YK'}
def wrap(state,events=None):
    cells=sorted({(x,y) for (x,y,_t,_k) in state.units if state.team_has_units_at(x,y,'Y') and state.team_has_units_at(x,y,'K')})
    before={c:cellstate(state,*c) for c in cells}
    orig(state,events)
    if t0<=cur[0]<=t1:
        for c in cells:
            b=before[c]; a=cellstate(state,*c)
            bl=state.building_at(*c)
            print(f" t{cur[0]} ({c[0]},{c[1]}){'['+bl.btype[:3]+']' if bl else ''} Y{list(b['Y'].values())}->{list(a['Y'].values())}  K{list(b['K'].values())}->{list(a['K'].values())}")
pipeline.step_combat=wrap
for t in d['turns']:
    cur[0]=t['turn']
    st,res=pipeline.run_turn(st,[mk(c) for c in t['applied']['Y']],[mk(c) for c in t['applied']['K']],[])
    if cur[0]>t1 or res: break
