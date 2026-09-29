import subprocess,sys,re,os
from concurrent.futures import ThreadPoolExecutor
ROOT='/Users/yh/Desktop/yonsei-vs-korea-hackathon'
RUN=ROOT+'/yk-development-tools/bots/dist/starter/play_local.py'
bot=sys.argv[1]; opp=sys.argv[2]; lo=int(sys.argv[3]); n=int(sys.argv[4])
def g(job):
    s,side=job
    y,k=(bot,opp) if side=='Y' else (opp,bot)
    r=subprocess.run(['python3',RUN,'--bot',y,'--opponent',k,'--seed',str(s),'--replay',os.devnull],capture_output=True,text=True,cwd=ROOT)
    return s,side,r.stdout.strip().split('\n')[-2:] ,r.stderr[-300:]
jobs=[(s,side) for s in range(lo,lo+n) for side in 'YK']
with ThreadPoolExecutor(2) as ex:
    for s,side,out,err in ex.map(g,jobs):
        line=' '.join(out)
        if 'forfeit' in line or 'Trace' in err: print(s,side,line,err)
print('done')
