import json,sys,re
def stats(path):
    d=json.load(open(path))
    sp={'Y':{'F':0,'W':0},'K':{'F':0,'W':0}}
    cnt={'Y':{'F':0,'W':0},'K':{'F':0,'W':0}}
    dead={'Y':{'F':0,'W':0},'K':{'F':0,'W':0}}
    for t in d['turns']:
        s=t['state']
        new={'Y':{'F':0,'W':0},'K':{'F':0,'W':0}}
        for tm,k,x,y,c in s['units']:
            if k in 'FW': new[tm][k]+=c
        for tm in 'YK':
            spawned={'F':0,'W':0}
            for c in t['commands'][tm]:
                m=re.match(r'SPAWN (\w) (\d+)',c)
                if m: spawned[m.group(1)]+=int(m.group(2))
            # spawn may be truncated by resources; approximate
            for k in 'FW':
                dd=cnt[tm][k]+spawned[k]-new[tm][k]
                if dd>0: dead[tm][k]+=dd
                sp[tm][k]+=spawned[k]
                cnt[tm][k]=new[tm][k]
    return d,sp,dead
if __name__=='__main__':
    for p in sys.argv[1:]:
        d,sp,dead=stats(p)
        print(p.split('/')[-1], d['result']['winner'], d['result']['reason'], d['result']['score'], d['result']['turns'])
        print('  spawn',sp,'dead',dead)
