# Allocator Benchmark Report

Emmanuel Kalii, SCT212-0329/2023

## Method

`bench.c` generates one fixed workload and replays it for each run: 50,000
operations over 1,000 pointer slots, with sizes from 1 to 4096 bytes and
`srand(12345)`. Each operation picks a slot: if the slot is in use the block is
freed, otherwise a new block is allocated into it. Each mode runs in its own
process (`./bench first`, `./bench best`, `./bench libc`) so every run starts
with a fresh heap.

`heap_stats()` is read after the 50,000 operations, while blocks are still live,
and before the final cleanup. External fragmentation is
`1 - largest_free / free_bytes`. Timing uses `clock_gettime(CLOCK_MONOTONIC)`
around the operation loop only. All 50,000 allocations succeeded in every run,
and `heap_check()` returned 1 after cleanup for both policies.

Environment: WSL2 Ubuntu, gcc 13.3.0, `-O2`.

## Results

| Metric | First-fit | Best-fit |
|---|---|---|
| heap_bytes | 1,310,720 | 1,310,720 |
| free_bytes | 225,728 | 226,672 |
| free_blocks | 225 | 172 |
| largest_free | 100,736 | 66,592 |
| External fragmentation | 0.5537 | 0.7062 |

## Timing (milliseconds, three runs each)

| Run | First-fit | Best-fit | glibc malloc/free |
|---|---|---|---|
| 1 | 474.039 | 309.934 | 6.443 |
| 2 | 249.015 | 395.660 | 7.778 |
| 3 | 262.772 | 390.574 | 6.111 |
| **Median** | **262.772** | **390.574** | **6.443** |

Compared with glibc, first-fit was about 41 times slower and best-fit about 61
times slower. Best-fit was about 1.5 times slower than first-fit. Timings on
WSL vary from run to run (the first first-fit run is an outlier), so the median
is used.

## Discussion

I would choose first-fit for this allocator. On this workload it was about 1.5
times faster than best-fit (median 263 ms against 391 ms) and it ended with lower
external fragmentation (0.55 against 0.71) and a larger biggest hole (100,736
against 66,592 bytes). Best-fit finished with fewer free blocks (172 against
225), but its free space was spread across smaller holes, so a large request
would be more likely to force the heap to grow. Best-fit must also scan the
whole list on every allocation, while first-fit can stop early. This is one
workload and one seed, so I do not claim first-fit always wins.

My allocator is slower than glibc (about 41 times for first-fit) because every
allocation walks a linked list and every free rescans the whole list to
coalesce, which is O(n). glibc keeps free chunks in size-based bins and
thread caches, so most of its operations take almost constant time.
