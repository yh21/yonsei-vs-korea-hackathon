import json,sys,re,glob
def stat(f,upto):
    d=json.load(open(f)); me='Y' if f.endswith('_Y.json') else 'K'; op='K' if me=='Y' else 'Y'
    sp={me:{'F':0,'W':0},op:{'F':0,'W':0}}; wasted={me:0,op:0}
    for t in d['turns']:
        if t['turn']>upto: break
        for tm in (me,op):
            for c in t['commands'][tm]:
                m=re.match(r'SPAWN (\w) (\d+)',c)
                if m and m.group(1) in 'FW': sp[tm][m.group(1)]+=int(m.group(2))
    st=[t for t in d['turns'] if t['turn']==upto]
    alive={me:{'F':0,'W':0},op:{'F':0,'W':0}}
    if st:
        for tm,k,x,y,c in st[0]['state']['units']:
            if k in 'FW': alive[tm][k]+=c
    return me,op,sp,alive
for f in sorted(glob.glob(sys.argv[1]))[:int(sys.argv[3]) if len(sys.argv)>3 else 100]:
    me,op,sp,al=stat(f,int(sys.argv[2]))
    print(f.split('/')[-1][:18],'me F spawned',sp[me]['F'],'alive',al[me]['F'],'| opp F spawned',sp[op]['F'],'alive',al[op]['F'],'| W spawned me/op',sp[me]['W'],sp[op]['W'],'alive',al[me]['W'],al[op]['W'])
