#!/usr/bin/env python3
import numpy as np, glob, sys
DIR = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith("--") else "data"
NCOL = int(sys.argv[2]) if len(sys.argv) > 2 and not sys.argv[2].startswith("--") else 28
files = sorted(glob.glob(DIR + "/val_*.csv"))
rows = []
for f in files:
    good = []
    for line in open(f):
        parts = line.strip().split(",")
        if len(parts) == NCOL:
            try: good.append([float(x) for x in parts])
            except ValueError: pass
    rows.append(np.array(good))
D = np.vstack(rows)
print("samples", D.shape)
turn = D[:, 0]; margin = D[:, 2]; win = D[:, 3]; X = D[:, 4:].copy()
DROP = [17, 18]   # collapse indicators: rare, handled by the exact judge
X[:, DROP] = 0.0
y = np.clip(margin, -25, 25)
names = ["dScore","dScore*rem","dW","dW*rem","dF","dRes","dHall*rem","dEng*rem","dHosp*rem","dLib*rem","dStat*rem","safeMine","riskMine","atkOpp","atkNeutral","safeMine*rem","atkOpp*rem","oppNothing","meNothing","rem","dHall","dEng","minW*rem","totW","herf","herf*rem","fcap","fcap*rem"]
n = len(y); idx = np.arange(n); rng = np.random.default_rng(0); rng.shuffle(idx)
tr, te = idx[: int(0.8 * n)], idx[int(0.8 * n):]
def fit(Xtr, ytr, lam):
    mu = Xtr.mean(0); sd = Xtr.std(0) + 1e-9
    Z = (Xtr - mu) / sd
    A = Z.T @ Z + lam * np.eye(Z.shape[1])
    for j in DROP:
        if j < A.shape[0]: A[j, :] = 0; A[:, j] = 0; A[j, j] = 1
    w = np.linalg.solve(A, Z.T @ (ytr - ytr.mean()))
    return mu, sd, w, ytr.mean()
def pred(mu, sd, w, b, X): return ((X - mu) / sd) @ w + b
for lam in [1, 10, 100, 1000]:
    mu, sd, w, b = fit(X[tr], y[tr], lam)
    p = pred(mu, sd, w, b, X[te])
    r2 = 1 - ((y[te] - p) ** 2).sum() / ((y[te] - y[te].mean()) ** 2).sum()
    acc = ((p > 0) == (win[te] > 0))[win[te] != 0].mean()
    print(f"lam {lam}: R2 {r2:.4f} sign-acc {acc:.4f}")
# baseline: only dScore
for cols, nm in [([0], "dScore only"), ([0, 2], "dScore+dW"), ([0,1,2,3,4,5], "basic6")]:
    mu, sd, w, b = fit(X[tr][:, cols], y[tr], 1)
    p = pred(mu, sd, w, b, X[te][:, cols])
    r2 = 1 - ((y[te] - p) ** 2).sum() / ((y[te] - y[te].mean()) ** 2).sum()
    acc = ((p > 0) == (win[te] > 0))[win[te] != 0].mean()
    print(f"{nm}: R2 {r2:.4f} sign-acc {acc:.4f}")
# by turn bucket
lam = 100
mu, sd, w, b = fit(X, y, lam)
p = pred(mu, sd, w, b, X)
for lo, hi in [(0, 30), (30, 60), (60, 100), (100, 130), (130, 161)]:
    m = (turn >= lo) & (turn < hi)
    r2 = 1 - ((y[m] - p[m]) ** 2).sum() / ((y[m] - y[m].mean()) ** 2).sum()
    print(f"turns {lo}-{hi}: R2 {r2:.3f} n={m.sum()}")
# raw coefficients
raw_w = w / sd; raw_b = b - (mu / sd * w).sum()
import sys
if "--write" in sys.argv:
    with open("../valuecoef.hpp", "w") as fo:
        fo.write("// valuecoef.hpp - coefficients of the linear state-value model (tools/fit_value.py, ridge regression of the final\n")
        fo.write("// score margin on state features, samples from self-play rollouts of the v17-family policies on dev seeds)\n#pragma once\nnamespace vc {\n")
        fo.write("constexpr double VB = %.6f;\n" % raw_b)
        fo.write("constexpr double VW[%d] = {%s};\n}\n" % (len(raw_w), ", ".join("%.6f" % v for v in raw_w)))
    print("wrote valuecoef.hpp")
print("// value = b + sum w_i f_i   (points of final margin)")
print("constexpr double VB = %.6f;" % raw_b)
print("constexpr double VW[%d] = {%s};" % (len(raw_w), ", ".join("%.6f" % v for v in raw_w)))
for nm, v in zip(names, raw_w): print(f"  {nm:14s} {v: .5f}")
