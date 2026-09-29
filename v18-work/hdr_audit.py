#!/usr/bin/env python3
"""gcc(libstdc++) 는 clang(libc++) 보다 헤더 전이 포함이 적다. 사용하는 std 기능에 대응하는 헤더가 제출 폴더 안에서 직접 #include 되는지 점검.
사용: python3 v18-work/hdr_audit.py <제출폴더>
"""
import os, re, sys
d = sys.argv[1]
files = [f for f in os.listdir(d) if f.endswith((".cpp", ".hpp", ".h"))]
txt = {f: open(os.path.join(d, f), encoding="utf-8", errors="replace").read() for f in files}
inc = set()
for t in txt.values(): inc |= set(re.findall(r'#\s*include\s*<([^>]+)>', t))
strip = lambda s: re.sub(r'//.*|/\*.*?\*/|"(?:\\.|[^"\\])*"', '', s, flags=re.S)
code = {f: strip(t) for f, t in txt.items()}
RULES = [
 (r'\bstd::vector\b|\bvector\s*<', 'vector'), (r'\bstd::string\b|\bstring\b(?!_)', 'string'), (r'\barray\s*<|std::array', 'array'),
 (r'\bmap\s*<|std::map', 'map'), (r'\bset\s*<|std::set', 'set'), (r'unordered_map', 'unordered_map'), (r'unordered_set', 'unordered_set'),
 (r'\bqueue\s*<|priority_queue', 'queue'), (r'\bdeque\s*<', 'deque'), (r'\bstack\s*<', 'stack'),
 (r'\b(sort|stable_sort|min|max|reverse|fill|find|count|swap|clamp|min_element|max_element|nth_element|unique|lower_bound|upper_bound|remove_if|any_of|all_of|copy|transform|next_permutation|shuffle)\s*\(', 'algorithm'),
 (r'\b(iota|accumulate|partial_sum|inner_product)\s*\(', 'numeric'), (r'std::function|\bfunction\s*<', 'functional'),
 (r'\bpair\s*<|make_pair|\bmove\s*\(|std::swap|\bforward\s*<', 'utility'), (r'\btuple\s*<|make_tuple|\btie\s*\(', 'tuple'),
 (r'chrono', 'chrono'), (r'mt19937|uniform_int_distribution|uniform_real_distribution|random_device', 'random'),
 (r'\bcout\b|\bcin\b|\bcerr\b', 'iostream'), (r'\b(snprintf|printf|fprintf|sprintf|fputs|puts)\s*\(', 'cstdio'),
 (r'\b(memset|memcpy|memcmp|strlen|strcmp|memmove)\s*\(', 'cstring'), (r'\b(abs|atoi|atof|getenv|exit|rand|srand|qsort)\s*\(', 'cstdlib'),
 (r'\b(sqrt|exp|log|pow|floor|ceil|fabs|tanh|sin|cos|round|fmod|hypot)\s*\(', 'cmath'),
 (r'\b(u?int(8|16|32|64)_t|size_t)\b', 'cstdint'), (r'\bassert\s*\(', 'cassert'), (r'\bbitset\s*<', 'bitset'),
 (r'\boptional\s*<|nullopt', 'optional'), (r'\bspan\s*<', 'span'), (r'\b(stringstream|istringstream|ostringstream)\b', 'sstream'),
 (r'numeric_limits', 'limits'), (r'unique_ptr|shared_ptr|make_unique|make_shared', 'memory'), (r'\b(popcount|countr_zero|countl_zero|bit_cast|bit_width)\s*\(', 'bit'),
 (r'\bvariant\s*<', 'variant'), (r'\bthread\b|std::mutex|std::atomic', 'thread/atomic/mutex (금지 가능성)'),
]
alt = {'cstdint': {'cstdint', 'stdint.h'}, 'cstdio': {'cstdio', 'stdio.h'}, 'cstring': {'cstring', 'string.h'}, 'cstdlib': {'cstdlib', 'stdlib.h'}, 'cmath': {'cmath', 'math.h'}, 'cassert': {'cassert', 'assert.h'},
       'utility': {'utility', 'algorithm', 'map', 'tuple'}, 'string': {'string'}, 'size_t': {'cstddef', 'cstdlib', 'cstdio', 'cstring', 'vector', 'string'}}
bad = 0
for pat, h in RULES:
    used = [f for f, c in code.items() if re.search(pat, c)]
    if not used: continue
    ok = inc & alt.get(h, {h})
    if h == 'cstdint' or h == 'utility': ok = inc & alt[h]
    if not ok:
        bad += 1; print(f"MISSING <{h}>  used in: {', '.join(used)}")
print("std includes present:", ' '.join(sorted(inc)))
print("AUDIT", "FAIL" if bad else "OK", f"({bad} missing)")
sys.exit(1 if bad else 0)
