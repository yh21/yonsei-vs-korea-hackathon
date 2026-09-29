import json,sys
def load(n): return json.load(open(f"v18-work/replays/real_{int(n):02d}.json"))
def summarize(d, every=5, upto=None, cmds=0):
    me=d['side']; en='Y' if me=='K' else 'K'
    print("me",me,"result",d['result'],"turns",len(d['turns']))
    for t in d['turns']:
        ob=t['observation']; n=ob['turn']
        if upto and n>upto: break
        if not (n%every==0 or n<=cmds or n==d['turns'][-1]['observation']['turn']): continue
        u={me:[0,0,0],en:[0,0,0]}
        for tm,k,x,y,c in ob['units']: u[tm]['FWS'.index(k)]+=c
        own={me:[],en:[]}
        for b in ob['buildings']:
            if b['owner'] in own: own[b['owner']].append(b['type'][:3])
        print(f"T{n:3d} res me{ob['resources'][me]:2d}/en{ob['resources'][en]:2d} FWS me{u[me]} en{u[en]} own me{len(own[me])}:{' '.join(own[me])} | en{len(own[en])}:{' '.join(own[en])}")
        if n<=cmds: print("     cmd:", (t.get('command') or {}).get('lines'))
if __name__=="__main__":
    summarize(load(sys.argv[1]), int(sys.argv[2]) if len(sys.argv)>2 else 5, cmds=int(sys.argv[3]) if len(sys.argv)>3 else 0)
