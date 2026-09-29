import json,sys,glob,re
wins=[(1,30),(31,60),(61,100),(101,160)]
tot={w:{'me':0,'op':0} for w in wins}; n=0
for f in sorted(glob.glob(sys.argv[1])):
    d=json.load(open(f)); me='Y' if f.endswith('_Y.json') else 'K'; op='K' if me=='Y' else 'Y'
    n+=1
    prevF={me:0,op:0}
    for t in d['turns']:
        cur={me:0,op:0}
        for tm,k,x,y,c in t['state']['units']:
            if k=='F': cur[tm]+=c
        for tm in (me,op):
            sp=0
            for c in t['commands'][tm]:
                m=re.match(r'SPAWN F (\d+)',c)
                if m: sp+=int(m.group(1))
            died=prevF[tm]+sp-cur[tm]
            if died>0:
                for w in wins:
                    if w[0]<=t['turn']<=w[1]: tot[w]['me' if tm==me else 'op']+=died
        prevF=cur
for w in wins: print(f"T{w[0]}-{w[1]}: F deaths me {tot[w]['me']/n:.1f}  opp {tot[w]['op']/n:.1f}")
