# malloc-SCT_212_0329_2023: a custom memory allocator in C

**Name:** Emmanuel Kathanzu Kalii
**Student ID:** SCT212_0329_2023
**Course:** ICS 2305 Systems Programming

A small `malloc`/`free` implementation built on `sbrk()`, with a block header,
a linked list of blocks, first-fit and best-fit placement, splitting, and
coalescing.

## Build and run

```
make                # builds and runs the provided test harness (29 checks)
make bench          # builds the benchmark
./bench first       # first-fit run
./bench best        # best-fit run
./bench libc        # same workload on glibc malloc/free
make clean
```

Sanitizer run:

```
gcc -std=c11 -g -fsanitize=address,undefined -o t_asan myalloc.c test_myalloc.c
./t_asan
```

## Final test output

```
T8 randomized stress test
  PASS  no data corruption during 20000 operations
  PASS  everything freed
  PASS  fully coalesced (few free blocks remain)
  PASS  heap consistent

Result: 29 passed, 0 failed
```

The same 29 checks also pass under AddressSanitizer and UndefinedBehaviorSanitizer,
and the code compiles with `-Wall -Wextra` with no warnings.

## Tasks implemented

| Task | Description | Status |
|---|---|---|
| 1 | Block header and helpers | Complete |
| 2 | Growing the heap with sbrk | Complete |
| 3 | my_malloc with first-fit | Complete |
| 4 | Splitting blocks | Complete |
| 5 | my_free and coalescing | Complete |
| 6 | my_calloc and my_realloc | Complete |
| 7 | Best-fit, heap_stats, heap_dump, heap_check | Complete |
| 8 | Experiment and report (`bench.c`, `REPORT.md`) | Complete |

## Design explanation

Each block starts with a header of four fields: the payload size, a pointer to
the next block, a `free` flag and a `magic` number. An explicit pad field brings
the struct to 32 bytes, and `HDR` is that size rounded up to a multiple of 16.
The pointer returned to the user is the header address plus `HDR`, and `free`
subtracts `HDR` to find the header again. Every request is rounded up to a
multiple of 16, and `grow()` pads the program break to a 16-byte boundary, so
every payload is 16-byte aligned. All valid blocks carry the same magic number.
This lets `my_free` tell an invalid pointer (wrong magic) from a double free
(right magic but the block is already free).

All blocks, free and used, sit in one singly linked list in increasing address
order, with global `head` and `tail` pointers. `my_malloc` searches the list with
first-fit or best-fit. If nothing fits, `grow()` asks `sbrk` for at least 64 KB
and appends one free block. When a block is much larger than the request, it is
split: the front part is returned and the remainder becomes a new free block.

On `my_free`, the pointer is range-checked and alignment-checked before its
header is read. The block is marked free, and then `coalesce_all()` walks the
list and merges neighbours that are both free and physically adjacent, meaning
the first block's payload ends exactly where the second header begins. The
adjacency test matters because libc's own `malloc` can move the break between
two `grow()` calls and leave a gap. A merged block is unlinked and its magic is
cleared, so stale pointers into it are rejected.

## Questions

**1. Why must the header size be a multiple of 16 as well as the payload sizes?**
The user pointer is the header address plus the header size. If the header were
24 bytes, every payload would sit at 8 mod 16 even when the block itself is
aligned. A header of 32 bytes and payload sizes that are multiples of 16 keep
every block start and every payload on a 16-byte boundary.

**2. Why can printf() inside my_malloc() cause infinite recursion?**
printf may call malloc internally, for example to allocate its output buffer the
first time it is used. If the allocator is the one servicing malloc, printf calls
malloc, which calls printf, and so on forever. The allocator would also be
printing while its list is half-updated. So the allocator builds text with
`snprintf` into a local array and sends it with `write()`.

**3. Your my_free() is O(n). Which extra field would let you find a block's previous neighbor in O(1), and what is the name of the technique?**
A `prev` pointer in the header, or a footer at the end of each block that stores
its size, would let `free` reach the previous block directly. This technique is
called boundary tags. With it, `free` can merge with both neighbours in constant
time instead of rescanning the list.

**4. Explain the difference between internal and external fragmentation, and name one place in your allocator where each one comes from.**
Internal fragmentation is wasted space inside an allocated block. It comes from
rounding sizes up to 16 (a request for 100 bytes gets 112) and from giving away
a whole block when the leftover is too small to split. External fragmentation is
free space that is broken into pieces too small to use, even though the total is
large enough. It comes from holes left between used blocks after frees. In my
benchmark first-fit ended with a fragmentation figure of 0.55.

**5. Why does sbrk() memory never get returned to the OS in your allocator? Describe how you could give back memory at the end of the heap.**
`my_free` only marks a block as free in the list. The allocator never calls
`sbrk` with a negative argument, so the break only moves upward. To give memory
back, `free` could check whether the last block is free and ends exactly at the
current break (`sbrk(0)`). If so, it could unlink that block, call `sbrk` with a
negative size, and update `tail`, `heap_end` and the byte total.

**6. Two blocks are consecutive in your list but not adjacent in memory. How did this happen, and what does your coalescing code do about it?**
Between two `grow()` calls, libc's `malloc` (used by `printf` in the tests) can
move the program break, so the new chunk starts after a gap of memory that
belongs to libc. The two blocks are still neighbours in my list but not in memory.
My coalescing code compares `payload(a) + a->size` with the address of the next
block and only merges when they are equal, so blocks across a gap are left
separate. `heap_check` uses the same test.

## Known limitations

- `my_free` rescans the whole list to coalesce, so it is O(n), and best-fit
  allocation always scans the whole list.
- Memory obtained with `sbrk` is never returned to the operating system.
- The allocator is not thread-safe.
- `my_free` cannot tell a pointer to a block's start from a pointer into
  another block's start that is still valid. It only checks range, alignment
  and magic, so a freed-and-reused address passes as a live block.
- If a block that was absorbed into its neighbour during coalescing is freed
  again, the message is "invalid pointer", not "double free".
- `my_malloc(0)` and `my_calloc` with a zero argument return NULL, as the
  assignment requires for `my_malloc(0)`.
- `heap_dump` writes to standard output (fd 1).

## Sources and AI-tool declaration

- Assignment brief and starter pack provided by the lecturer.
- Linux man pages for `sbrk(2)`, `brk(2)` and `write(2)`.
- I used an AI assistant (Claude) throughout this project to explain the
  concepts, review my design plans, draft code, and help with debugging, and I
  used a second AI tool to cross-check those design plans. I typed, built and
  tested the code myself, ran the test harness, the sanitizers and the benchmark,
  and the benchmark figures in `REPORT.md` come from my own runs.
