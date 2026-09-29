import json,sys,re
d=json.load(open(sys.argv[1]))
tot={'Y':{'F':0,'W':0},'K':{'F':0,'W':0}}
for t in d['turns']:
    for tm in 'YK':
        for c in t['commands'][tm]:
            m=re.match(r'SPAWN (\w) (\d+)',c)
            if m: tot[tm][m.group(1)]+=int(m.group(2))
print(d['teams']['Y']['name'],d['teams']['K']['name'],tot, d['result'])
