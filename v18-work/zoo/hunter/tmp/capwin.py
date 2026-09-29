import json,sys,glob
wins=[(1,30),(31,60),(61,100),(101,130),(131,160)]
tot={w:{'me':0,'op':0,'meN':0,'opN':0} for w in wins}
n=0
for f in sorted(glob.glob(sys.argv[1])):
    d=json.load(open(f)); me='Y' if f.endswith('_Y.json') else 'K'; op='K' if me=='Y' else 'Y'
    n+=1
    for t in d['turns']:
        for e in t['events']:
            for w in wins:
                if w[0]<=t['turn']<=w[1]:
                    if e['type']=='capture': tot[w]['me' if e['team']==me else 'op']+=1
                    if e['type']=='neutralize' and e.get('team'): tot[w]['meN' if e['team']==me else 'opN']+=1
for w in wins:
    v=tot[w]; print(f"T{w[0]}-{w[1]}: captures me {v['me']/n:.1f} op {v['op']/n:.1f} | neutralize by me {v['meN']/n:.1f} op {v['opN']/n:.1f}")
