import json,sys,glob,re
wins=[(1,30),(31,60),(61,100),(101,160)]
tot={w:{'me':0,'op':0,'spme':0,'spop':0} for w in wins}; n=0
for f in sorted(glob.glob(sys.argv[1])):
    d=json.load(open(f)); me='Y' if f.endswith('_Y.json') else 'K'; op='K' if me=='Y' else 'Y'
    n+=1
    prev={me:0,op:0}
    for t in d['turns']:
        cur={me:0,op:0}
        for tm,k,x,y,c in t['state']['units']:
            if k=='W': cur[tm]+=c
        for tm in (me,op):
            sp=0
            for c in t['commands'][tm]:
                m=re.match(r'SPAWN W (\d+)',c)
                if m: sp+=int(m.group(1))
            lost=prev[tm]+sp-cur[tm]
            for w in wins:
                if w[0]<=t['turn']<=w[1]:
                    tot[w]['me' if tm==me else 'op']+=lost
                    tot[w]['spme' if tm==me else 'spop']+=sp
        prev=cur
for w in wins: v=tot[w]; print(f"T{w[0]}-{w[1]}: W lost me {v['me']/n:.1f} opp {v['op']/n:.1f} | W spawned me {v['spme']/n:.1f} opp {v['spop']/n:.1f}")
