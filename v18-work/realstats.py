import json,glob,re
files=sorted(glob.glob("v18-work/replays/real_*.json"))
def first_turn_owned(d,who,typ):
    for t in d['turns']:
        for b in t['observation']['buildings']:
            if b['type']==typ and b['owner']==who: return t['observation']['turn']
    return None
print("game T  end   | first ENG/HAL/HOS/DEP/PLA (me | en) | W@20 W@40 (me/en) F@20 | me S-used | firstcmds")
for f in files:
    d=json.load(open(f)); me=d['side']; en='Y'
    n=int(re.findall(r'real_(\d+)',f)[0])
    fe=lambda w:[first_turn_owned(d,w,t) for t in ('ENG','HALL','HOSPITAL','DEPOT','PLAZA')]
    def W(t,who):
        for tt in d['turns']:
            if tt['observation']['turn']==t:
                return sum(c for tm,k,x,y,c in tt['observation']['units'] if tm==who and k=='W')
        return None
    def F(t,who):
        for tt in d['turns']:
            if tt['observation']['turn']==t:
                return sum(c for tm,k,x,y,c in tt['observation']['units'] if tm==who and k=='F')
        return None
    sused=any('SPAWN S' in ' '.join((t.get('command') or {}).get('lines') or []) for t in d['turns'])
    print(f"{n:3d} {len(d['turns'])-1:3d} {d['result']['reason'][:5]:5s} | me{fe(me)} en{fe(en)} | W20 {W(20,me)}/{W(20,en)} W40 {W(40,me)}/{W(40,en)} F20 {F(20,me)}/{F(20,en)} | S={sused}")
