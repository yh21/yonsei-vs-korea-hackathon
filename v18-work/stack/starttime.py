import sys,os,time,subprocess
ROOT=os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0,os.path.join(ROOT,'yk-development-tools'))
from engine.config import load_config
from mapgen import generate,to_state
from runner.protocol import serialize_init,serialize_turn
cfg=load_config(); st=to_state(generate(10003,cfg)); st.turn=0
init=serialize_init(st,'Y'); turn=serialize_turn(st,'Y',1)
for b in sys.argv[1:]:
    ts=[]
    for i in range(15):
        t0=time.monotonic()
        p=subprocess.Popen(b,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,text=True)
        p.stdin.write(init+turn); p.stdin.flush()
        while True:
            l=p.stdout.readline()
            if l.strip()=='END': break
        ts.append((time.monotonic()-t0)*1000); p.kill()
    ts.sort(); print(b,'first-turn ms: min %.1f med %.1f max %.1f'%(ts[0],ts[len(ts)//2],ts[-1]))
