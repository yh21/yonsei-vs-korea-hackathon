import subprocess,random,sys,time
args=open('blotto_args.txt').read().split()
# use every 2nd game to keep runtime low
pairs=list(zip(args[0::2],args[1::2]))
random.seed(1); random.shuffle(pairs)
sub=pairs[:30]
flat=[x for p in sub for x in p]
def acc(cfg):
    cmd=['./modelfit']+[f'{k}={v}' for k,v in cfg.items()]+['--']+flat
    return float(subprocess.run(cmd,capture_output=True,text=True).stdout.strip() or 0)
space={'riskMul':[0,4,8,13,20,30],'regMul':[0,1,2,3,5],'huntValue':[100,400,660,1100,2000],'defValue':[300,600,960,1600,2500],
       'dF0':[3,5,7,9],'dF1':[3,5,6,8],'dF2':[2,4,5,7],'dF3':[2,3,4,6],'adaptF':[0,1],'holdVal':[0,1],'dispatch':[0,3,5],'huntNeed':[1,2,3,5],'hospBonus':[50,145,300,500],'rally':[0,1],'garrisonN':[0,2,4]}
cfg={'riskMul':13,'regMul':3,'huntValue':660,'defValue':960,'dF0':7,'dF1':6,'dF2':5,'dF3':4,'adaptF':1,'holdVal':0,'dispatch':0,'huntNeed':3,'hospBonus':145,'rally':0,'garrisonN':0}
best=acc(cfg); print('base',best,flush=True)
for rnd in range(2):
    for k,vals in space.items():
        for v in vals:
            if v==cfg[k]: continue
            c=dict(cfg); c[k]=v; a=acc(c)
            if a>best+0.002: best=a; cfg=c; print(rnd,k,v,'->',round(a,4),flush=True)
print('final',best,cfg)
