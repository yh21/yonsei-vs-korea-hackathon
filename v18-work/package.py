#!/usr/bin/env python3
"""제출 폴더 생성: main.cpp 가 (재귀적으로) #include "..." 하는 파일만 평면 복사 + submission.json, 컴파일 확인, zip 생성.
사용: python3 v18-work/package.py <소스디렉터리> <출력디렉터리(yjc-submissionNN-cpp)> [zip경로]
"""
import os, re, shutil, subprocess, sys
src, out = sys.argv[1], sys.argv[2]
zipp = sys.argv[3] if len(sys.argv) > 3 else None

DEV_MACROS = {"TUNE", "TIMING", "NOSEARCH", "DEBUG", "TRACE"}
def strip_dev(text):
    """개발용 매크로(#ifdef TUNE 등, 정의 안 된 상태로 취급)를 소스에서 제거. 그 외 조건부는 그대로 둔다."""
    out, stack = [], []   # frame: (tracked, active_now)
    def live(): return all(a for t, a in stack)
    for line in text.split("\n"):
        m = re.match(r'^\s*#\s*(ifdef|ifndef|if|elif|else|endif)\b\s*(\w*)', line)
        if m:
            kw, name = m.group(1), m.group(2)
            if kw in ("ifdef", "ifndef") and name in DEV_MACROS:
                stack.append((True, kw == "ifndef")); continue
            if kw == "if" or kw in ("ifdef", "ifndef"):
                stack.append((False, True))
                if live(): out.append(line)
                continue
            if kw in ("else", "elif"):
                t, a = stack[-1]
                if t:
                    stack[-1] = (True, not a); continue
                if live(): out.append(line)
                continue
            if kw == "endif":
                t, a = stack.pop()
                if t: continue
                if live(): out.append(line)
                continue
        if live(): out.append(line)
    if stack: raise SystemExit("unbalanced preprocessor conditionals")
    return "\n".join(out)

need, todo = set(), ["main.cpp"]
while todo:
    f = todo.pop()
    if f in need: continue
    need.add(f)
    txt = open(os.path.join(src, f), encoding="utf-8", errors="replace").read()
    for m in re.finditer(r'^\s*#\s*include\s+"([^"]+)"', txt, re.M):
        inc = m.group(1)
        if not os.path.exists(os.path.join(src, inc)):
            sys.exit(f"missing include {inc} (from {f})")
        todo.append(inc)
if os.path.exists(out): shutil.rmtree(out)
os.makedirs(out)
for f in sorted(need):
    if os.sep in f or "/" in f: sys.exit(f"nested include not supported: {f}")
    orig = open(os.path.join(src, f), encoding="utf-8", errors="replace").read()
    open(os.path.join(out, f), "w", encoding="utf-8").write(strip_dev(orig))
open(os.path.join(out, "submission.json"), "w").write('{"schemaVersion": 1, "language": "cpp"}\n')
print("files:", sorted(need))
# 금지 API 검사 (외부 값/파일 읽기, 스레드 등): 발견되면 실패
BAN = re.compile(r'\b(getenv|fopen|freopen|ifstream|ofstream|fstream|popen|system|std::filesystem|std::thread|pthread_create)\b|#\s*include\s*<(fstream|filesystem|thread|mutex|atomic)>')
for f in sorted(need):
    for i, ln in enumerate(open(os.path.join(out, f), encoding="utf-8", errors="replace").read().split("\n"), 1):
        if BAN.search(re.sub(r'//.*', '', ln)): sys.exit(f"금지 API 발견: {f}:{i}: {ln.strip()[:100]}")
# 개발 매크로 제거 전후 전처리 결과가 동일한지(=동작 동일) 확인
def pre(d):
    r = subprocess.run(["g++", "-std=c++20", "-E", "-P", os.path.join(d, "main.cpp")], capture_output=True, text=True)
    return r.stdout
if pre(src) != pre(out): sys.exit("전처리 결과가 원본과 다름(개발 매크로 제거가 동작을 바꿈)")
print("dev-code stripped; preprocessed output identical to original")
r = subprocess.run(["g++", "-std=c++20", "-O2", "-Wall", "-Wextra", "-o", os.path.join(out, ".check_bot"), os.path.join(out, "main.cpp")], capture_output=True, text=True)
print("compile exit", r.returncode); print(r.stderr[-1500:])
if r.returncode: sys.exit(1)
os.remove(os.path.join(out, ".check_bot"))
if zipp:
    r = subprocess.run(["python3", "yk-development-tools/bots/dist/starter/make_submission.py", "--source", out, "--output", zipp], capture_output=True, text=True)
    print(r.stdout, r.stderr)
    if r.returncode: sys.exit(1)
