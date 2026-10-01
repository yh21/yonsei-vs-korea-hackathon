// Library-independent introsort with a FIXED tie order.  This is a verbatim-in-behaviour port of the non-bitset path of libc++'s
// std::sort (LLVM 21: insertion sort below 24 elements, median-of-3 / ninther pivot, partition with equals on the right/left,
// "already partitioned" insertion-sort shortcut), so that sorts whose comparator has ties (equal keys) order equal elements exactly as the
// clang/libc++ builds this bot was developed and tuned with did, whatever standard library the server compiles against.
// (std::sort leaves the order of equal elements unspecified; libstdc++ and libc++ differ for n > 16 / n >= 24.)
// Derived from the LLVM Project's libc++ <__algorithm/sort.h> and heap helpers (Apache License v2.0 with LLVM Exceptions,
// https://llvm.org/LICENSE.txt); adapted to plain pointers.  Verified bit-for-bit against libc++'s std::sort on 500,000 random arrays (with ties).
#pragma once
#include <cstddef>
#include <utility>
namespace ord {
template <class T, class C> inline bool sort3(T* x, T* y, T* z, C& c) {
    using std::swap;
    if (!c(*y, *x)) {
        if (!c(*z, *y)) return false;
        swap(*y, *z);
        if (c(*y, *x)) swap(*x, *y);
        return true;
    }
    if (c(*z, *y)) { swap(*x, *z); return true; }
    swap(*x, *y);
    if (c(*z, *y)) swap(*y, *z);
    return true;
}
template <class T, class C> inline void sort4(T* x1, T* x2, T* x3, T* x4, C& c) {
    using std::swap;
    sort3(x1, x2, x3, c);
    if (c(*x4, *x3)) { swap(*x3, *x4); if (c(*x3, *x2)) { swap(*x2, *x3); if (c(*x2, *x1)) swap(*x1, *x2); } }
}
template <class T, class C> inline void sort5(T* x1, T* x2, T* x3, T* x4, T* x5, C& c) {
    using std::swap;
    sort4(x1, x2, x3, x4, c);
    if (c(*x5, *x4)) { swap(*x4, *x5); if (c(*x4, *x3)) { swap(*x3, *x4); if (c(*x3, *x2)) { swap(*x2, *x3); if (c(*x2, *x1)) swap(*x1, *x2); } } }
}
template <class T, class C> inline void insertion_sort(T* first, T* last, C& c) {
    if (first == last) return;
    for (T* i = first + 1; i != last; ++i) {
        T* j = i - 1;
        if (c(*i, *j)) {
            T t = std::move(*i); T* k = j; j = i;
            do { *j = std::move(*k); j = k; } while (j != first && c(t, *--k));
            *j = std::move(t);
        }
    }
}
template <class T, class C> inline void insertion_sort_unguarded(T* first, T* last, C& c) {
    if (first == last) return;
    for (T* i = first + 1; i != last; ++i) {
        T* j = i - 1;
        if (c(*i, *j)) {
            T t = std::move(*i); T* k = j; j = i;
            do { *j = std::move(*k); j = k; } while (c(t, *--k));
            *j = std::move(t);
        }
    }
}
template <class T, class C> inline bool insertion_sort_incomplete(T* first, T* last, C& c) {
    using std::swap;
    switch (last - first) {
    case 0: case 1: return true;
    case 2: if (c(*--last, *first)) swap(*first, *last); return true;
    case 3: sort3(first, first + 1, --last, c); return true;
    case 4: sort4(first, first + 1, first + 2, --last, c); return true;
    case 5: sort5(first, first + 1, first + 2, first + 3, --last, c); return true;
    }
    T* j = first + 2;
    sort3(first, first + 1, j, c);
    const unsigned limit = 8; unsigned count = 0;
    for (T* i = j + 1; i != last; ++i) {
        if (c(*i, *j)) {
            T t = std::move(*i); T* k = j; j = i;
            do { *j = std::move(*k); j = k; } while (j != first && c(t, *--k));
            *j = std::move(t);
            if (++count == limit) return ++i == last;
        }
        j = i;
    }
    return true;
}
template <class T, class C> inline std::pair<T*, bool> partition_equals_right(T* first, T* last, C& c) {
    using std::swap;
    T* const begin = first;
    T pivot = std::move(*first);
    do { ++first; } while (c(*first, pivot));
    if (begin == first - 1) { while (first < last && !c(*--last, pivot)) {} }
    else { do { --last; } while (!c(*last, pivot)); }
    bool already = first >= last;
    while (first < last) {
        swap(*first, *last);
        do { ++first; } while (c(*first, pivot));
        do { --last; } while (!c(*last, pivot));
    }
    T* pivot_pos = first - 1;
    if (begin != pivot_pos) *begin = std::move(*pivot_pos);
    *pivot_pos = std::move(pivot);
    return std::make_pair(pivot_pos, already);
}
template <class T, class C> inline T* partition_equals_left(T* first, T* last, C& c) {
    using std::swap;
    T* const begin = first;
    T pivot = std::move(*first);
    if (c(pivot, *(last - 1))) { do { ++first; } while (!c(pivot, *first)); }
    else { while (++first < last && !c(pivot, *first)) {} }
    if (first < last) { do { --last; } while (c(pivot, *last)); }
    while (first < last) {
        swap(*first, *last);
        do { ++first; } while (!c(pivot, *first));
        do { --last; } while (c(pivot, *last));
    }
    T* pivot_pos = first - 1;
    if (begin != pivot_pos) *begin = std::move(*pivot_pos);
    *pivot_pos = std::move(pivot);
    return first;
}
// depth-limit fallback (libc++ __partial_sort(first, last, last) = make_heap + sort_heap, ported so that even this rare path is identical)
template <class T, class C> inline void sift_down(T* first, C& c, std::ptrdiff_t len, T* start) {
    std::ptrdiff_t child = start - first;
    if (len < 2 || (len - 2) / 2 < child) return;
    child = 2 * child + 1; T* child_i = first + child;
    if ((child + 1) < len && c(*child_i, *(child_i + 1))) { ++child_i; ++child; }
    if (c(*child_i, *start)) return;
    T top = std::move(*start);
    do {
        *start = std::move(*child_i); start = child_i;
        if ((len - 2) / 2 < child) break;
        child = 2 * child + 1; child_i = first + child;
        if ((child + 1) < len && c(*child_i, *(child_i + 1))) { ++child_i; ++child; }
    } while (!c(*child_i, top));
    *start = std::move(top);
}
template <class T, class C> inline T* floyd_sift_down(T* first, C& c, std::ptrdiff_t len) {
    T* hole = first; T* child_i = first; std::ptrdiff_t child = 0;
    while (true) {
        child_i += child + 1; child = 2 * child + 1;
        if ((child + 1) < len && c(*child_i, *(child_i + 1))) { ++child_i; ++child; }
        *hole = std::move(*child_i); hole = child_i;
        if (child > (len - 2) / 2) return hole;
    }
}
template <class T, class C> inline void sift_up(T* first, T* last, C& c, std::ptrdiff_t len) {
    if (len > 1) {
        len = (len - 2) / 2; T* ptr = first + len;
        if (c(*ptr, *--last)) {
            T t = std::move(*last);
            do { *last = std::move(*ptr); last = ptr; if (len == 0) break; len = (len - 1) / 2; ptr = first + len; } while (c(*ptr, t));
            *last = std::move(t);
        }
    }
}
template <class T, class C> inline void heap_sort(T* first, T* last, C& c) {
    std::ptrdiff_t n = last - first;
    if (n > 1) for (std::ptrdiff_t start = (n - 2) / 2; start >= 0; --start) sift_down(first, c, n, first + start);
    for (; n > 1; --last, --n) {
        T top = std::move(*first);
        T* hole = floyd_sift_down(first, c, n);
        T* l = last - 1;
        if (hole == l) *hole = std::move(top);
        else { *hole = std::move(*l); ++hole; *l = std::move(top); sift_up(first, hole, c, hole - first); }
    }
}
template <class T, class C> void introsort(T* first, T* last, C& c, std::ptrdiff_t depth, bool leftmost) {
    using std::swap;
    const std::ptrdiff_t limit = 24, ninther = 128;
    while (true) {
        std::ptrdiff_t len = last - first;
        switch (len) {
        case 0: case 1: return;
        case 2: if (c(*--last, *first)) swap(*first, *last); return;
        case 3: sort3(first, first + 1, --last, c); return;
        case 4: sort4(first, first + 1, first + 2, --last, c); return;
        case 5: sort5(first, first + 1, first + 2, first + 3, --last, c); return;
        }
        if (len < limit) {
            if (leftmost) insertion_sort(first, last, c); else insertion_sort_unguarded(first, last, c);
            return;
        }
        if (depth == 0) { heap_sort(first, last, c); return; }
        --depth;
        {
            std::ptrdiff_t half = len / 2;
            if (len > ninther) {
                sort3(first, first + half, last - 1, c);
                sort3(first + 1, first + (half - 1), last - 2, c);
                sort3(first + 2, first + (half + 1), last - 3, c);
                sort3(first + (half - 1), first + half, first + (half + 1), c);
                swap(*first, *(first + half));
            } else {
                sort3(first + half, first, last - 1, c);
            }
        }
        if (!leftmost && !c(*(first - 1), *first)) { first = partition_equals_left(first, last, c); continue; }
        auto ret = partition_equals_right(first, last, c);
        T* i = ret.first;
        if (ret.second) {
            bool fs = insertion_sort_incomplete(first, i, c);
            if (insertion_sort_incomplete(i + 1, last, c)) {
                if (fs) return;
                last = i; continue;
            } else if (fs) { first = ++i; continue; }
        }
        introsort(first, i, c, depth, leftmost);
        leftmost = false;
        first = ++i;
    }
}
// std::sort replacement: sort(first, last, comp) for contiguous ranges
template <class T, class C> void sort(T* first, T* last, C comp) {
    if (first == last) return;
    std::ptrdiff_t n = last - first, lg = 0;
    while ((n >> 1) > 0) { n >>= 1; lg++; }
    introsort(first, last, comp, 2 * lg, true);
}
} // namespace ord
