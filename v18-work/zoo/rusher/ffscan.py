import subprocess,sys,re,os
from concurrent.futures import ThreadPoolExecutor
ROOT='/Users/yh/Desktop/yonsei-vs-korea-hackathon'
RUN=ROOT+'/yk-development-tools/bots/dist/starter/play_local.py'
bot=sys.argv[1]; opps=sys.argv[2].split(','); lo=int(sys.argv[3]); n=int(sys.argv[4]); wk=int(sys.argv[5])
def g(job):
    opp,s,side=job
    y,k=(bot,opp) if side=='Y' else (opp,bot)
    r=subprocess.run(['python3',RUN,'--bot',y,'--opponent',k,'--seed',str(s),'--replay',os.devnull],capture_output=True,text=True,cwd=ROOT)
    return opp,s,side,r.stdout,r.stderr
jobs=[(o,s,side) for o in opps for s in range(lo,lo+n) for side in 'YK']
cnt=0
with ThreadPoolExecutor(wk) as ex:
    for opp,s,side,out,err in ex.map(g,jobs):
        cnt+=1
        if 'forfeit' in out or 'Traceback' in err or '몰수' in out:
            print('FORFEIT',opp,s,'bot=',side,out.strip()[-400:],err[-200:],flush=True)
print('scanned',cnt)
