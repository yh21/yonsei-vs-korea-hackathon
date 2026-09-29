#!/usr/bin/env python3
"""batch.py BOT OPP [OPP...] --seeds LO N [--maps maps_dev.txt] [--jobs 4] [--env K=V ...] [--keep DIR]
Runs BOT vs each OPP on seeds LO..LO+N-1 as Y and as K (in-process duel harness)."""
import argparse, os, subprocess, sys, json, shutil, atexit
from concurrent.futures import ThreadPoolExecutor
here = os.path.dirname(os.path.abspath(__file__))
ap = argparse.ArgumentParser()
ap.add_argument("bot"); ap.add_argument("opps", nargs="+")
ap.add_argument("--seeds", nargs=2, type=int, default=[10000, 40])
ap.add_argument("--maps", default="maps_dev.txt")
ap.add_argument("--jobs", type=int, default=4)
ap.add_argument("--env", nargs="*", default=[])
ap.add_argument("--keep", default=None)
ap.add_argument("--quiet", action="store_true")
ap.add_argument("--sides", default="YK", help="YK (both) or Y (mirror-symmetric bots: half the cost)")
a = ap.parse_args()
env = dict(os.environ)
DUEL = os.path.join(here, f"duel_run_{os.getpid()}")
shutil.copy(os.path.join(here, os.environ.get("DUEL_SRC", "duel")), DUEL)
atexit.register(lambda: os.path.exists(DUEL) and os.remove(DUEL))
for kv in a.env:
    k, v = kv.split("=", 1); env[k] = v
def play(args):
    opp, seed, botY = args
    y, k = (a.bot, opp) if botY else (opp, a.bot)
    cmd = [DUEL, os.path.join(here, a.maps), str(seed), y, k]
    rp = None
    if a.keep:
        os.makedirs(a.keep, exist_ok=True)
        rp = os.path.join(a.keep, f"{a.bot}_vs_{opp}_{seed}_{'Y' if botY else 'K'}.json")
        cmd.append(rp)
    r = subprocess.run(cmd, capture_output=True, text=True, env=env)
    line = [l for l in r.stdout.splitlines() if l.startswith("R ")]
    if not line: return dict(seed=seed, side='Y' if botY else 'K', res='error', detail=r.stderr[-200:], rp=rp)
    f = line[0].split()
    w = f[4]; me = 'Y' if botY else 'K'
    sy, sk = int(f[6]), int(f[7])
    res = 'draw' if w == 'D' else ('win' if w == me else 'loss')
    my, op = (sy, sk) if botY else (sk, sy)
    mx = float(f[9] if botY else f[11]); av = float(f[10] if botY else f[12])
    return dict(seed=seed, side=me, res=res, reason=f[5], my=my, opp=op, turns=int(f[8]), maxms=mx, avgms=av, rp=rp)
tot = {}
for opp in a.opps:
    jobs = [(opp, s, side) for s in range(a.seeds[0], a.seeds[0] + a.seeds[1]) for side in ((True, False) if a.sides == 'YK' else (True,))]
    with ThreadPoolExecutor(a.jobs) as ex: rs = list(ex.map(play, jobs))
    if a.keep:
        for r in rs:
            if r.get('res') != 'loss' and r.get('rp') and os.path.exists(r['rp']): os.remove(r['rp'])
    w = sum(r['res'] == 'win' for r in rs); d = sum(r['res'] == 'draw' for r in rs); l = sum(r['res'] == 'loss' for r in rs)
    e = sum(r['res'] == 'error' for r in rs)
    ff = sum(r.get('reason') == 'forfeit' for r in rs)
    inst = [(r['seed'], r['side'], r['my'], r['opp']) for r in rs if r.get('reason') == 'instant' and r['res'] == 'loss']
    mm = [r['my'] - r['opp'] for r in rs if 'my' in r]
    mx = max([r.get('maxms', 0) for r in rs] + [0]); av = sum(r.get('avgms', 0) for r in rs) / max(1, len(rs))
    n = len(rs)
    print(f"{a.bot} vs {opp}: {w}W {d}D {l}L /{n} winrate={(w + 0.5 * d) / n:.3f} margin={sum(mm) / max(1, len(mm)):.2f} maxms={mx:.0f} avgms={av:.1f} ff={ff} err={e} instant_losses={inst}")
    if not a.quiet and l: print("   lost:", [(r['seed'], r['side'], r['my'], r['opp'], r['reason']) for r in rs if r['res'] == 'loss'][:30])
    if e: print("   ERR:", [r['detail'] for r in rs if r['res'] == 'error'][:2])
    sys.stdout.flush()
