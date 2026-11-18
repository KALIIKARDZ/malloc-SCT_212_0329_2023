#define _DEFAULT_SOURCE
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>      /* snprintf only: no printf/puts/perror anywhere */
#include <string.h>
#include <unistd.h>
#include "myalloc.h"

#define MAGIC     0x4D59414C
#define MIN_CHUNK ((size_t)64 * 1024)

typedef struct block {
    size_t        size;     /* payload size in bytes */
    struct block *next;     /* next block, in increasing address */
    int           free;     /* 1 = free, 0 = in use */
    int           magic;    /* MAGIC for every valid block */
    size_t        pad;      /* explicit padding */
} block_t;

#define HDR ((size_t)((sizeof(block_t) + 15) & ~(size_t)15))

_Static_assert(HDR % 16 == 0, "HDR must be a multiple of 16");

static block_t     *g_head       = NULL;
static block_t     *g_tail       = NULL;
static char        *g_heap_start = NULL;   /* first block's header */
static char        *g_heap_end   = NULL;   /* end of last block's payload */
static size_t       g_heap_bytes = 0;      /* total bytes ever from sbrk */
static fit_policy_t g_policy     = FIT_FIRST;

/* ---------- helpers ---------- */

static size_t align_up(size_t n)
{
    return (n + 15) & ~(size_t)15;
}

static void *payload(block_t *b)
{
    return (char *)b + HDR;
}

static block_t *header(void *ptr)
{
    return (block_t *)((char *)ptr - HDR);
}

static void write_err(const char *s)
{
    ssize_t r = write(2, s, strlen(s));
    (void)r;
}

/* ---------- growing the heap ---------- */

static block_t *grow(size_t need)
{
    if (need > (size_t)PTRDIFF_MAX - HDR - MIN_CHUNK - 16)
        return NULL;

    size_t want = need + HDR;
    if (want < MIN_CHUNK)
        want = MIN_CHUNK;
    want = align_up(want);

    char *cur = sbrk(0);
    if (cur == (char *)-1)
        return NULL;

    uintptr_t addr = (uintptr_t)cur;
    size_t pad = (16 - (addr % 16)) % 16;      /* bytes to reach 16-alignment */

    char *old = sbrk((intptr_t)(pad + want));
    if (old == (char *)-1 || old != cur)
        return NULL;

    block_t *b = (block_t *)(old + pad);
    b->size  = want - HDR;
    b->next  = NULL;
    b->free  = 1;
    b->magic = MAGIC;
    b->pad   = 0;

    if (g_head == NULL) {
        g_head = b;
        g_tail = b;
    } else {
        g_tail->next = b;
        g_tail = b;
    }

    if (g_heap_start == NULL)
        g_heap_start = (char *)b;
    g_heap_end    = (char *)b + HDR + b->size;
    g_heap_bytes += pad + want;

    return b;
}

/* ---------- placement ---------- */

static block_t *find_fit(size_t need)
{
    if (g_policy == FIT_FIRST) {
        for (block_t *b = g_head; b != NULL; b = b->next) {
            if (b->free && b->size >= need)
                return b;
        }
        return NULL;
    }

    block_t *best = NULL;                      /* FIT_BEST */
    for (block_t *b = g_head; b != NULL; b = b->next) {
        if (b->free && b->size >= need) {
            if (best == NULL || b->size < best->size)
                best = b;
            if (b->size == need)
                break;                         /* exact fit: cannot do better */
        }
    }
    return best;
}

/* Cut b so its payload is exactly need bytes; the rest becomes a free block. */
static void split_block(block_t *b, size_t need)
{
    if (b->size - need < HDR + 16)
        return;                                /* remainder too small: keep whole */

    block_t *rest = (block_t *)((char *)payload(b) + need);
    rest->size  = b->size - need - HDR;
    rest->next  = b->next;
    rest->free  = 1;
    rest->magic = MAGIC;
    rest->pad   = 0;

    b->size = need;
    b->next = rest;

    if (g_tail == b)
        g_tail = rest;
}

/* Merge every run of physically adjacent free blocks. */
static void coalesce_all(void)
{
    block_t *b = g_head;
    while (b != NULL && b->next != NULL) {
        block_t *n = b->next;
        int adjacent = ((char *)payload(b) + b->size == (char *)n);

        if (b->free && n->free && adjacent) {
            b->size += HDR + n->size;
            b->next  = n->next;
            n->magic = 0;                      /* stale pointers now invalid */
            n->size  = 0;
            n->next  = NULL;
            n->free  = 0;
            if (g_tail == n)
                g_tail = b;
            /* stay on b: it may merge with the next one too */
        } else {
            b = b->next;
        }
    }
}

/* Return the header for a valid, in-use pointer, else NULL.
 * quiet = 0 prints the error message, quiet = 1 stays silent. */
static block_t *validate_ptr(void *ptr, int quiet)
{
    uintptr_t p = (uintptr_t)ptr;

    if (g_heap_start == NULL
        || p < (uintptr_t)g_heap_start + HDR
        || p >= (uintptr_t)g_heap_end
        || p % 16 != 0) {
        if (!quiet)
            write_err("my_free: invalid pointer\n");
        return NULL;
    }

    block_t *b = header(ptr);
    if (b->magic != MAGIC) {
        if (!quiet)
            write_err("my_free: invalid pointer\n");
        return NULL;
    }
    if (b->free) {
        if (!quiet)
            write_err("my_free: double free detected\n");
        return NULL;
    }
    return b;
}

/* ---------- public API ---------- */

void *my_malloc(size_t size)
{
    if (size == 0)
        return NULL;
    if (size > SIZE_MAX - 15)
        return NULL;

    size_t need = align_up(size);

    block_t *b = find_fit(need);
    if (b == NULL) {
        b = grow(need);
        if (b == NULL)
            return NULL;
    }

    b->free = 0;
    split_block(b, need);
    return payload(b);
}

void my_free(void *ptr)
{
    if (ptr == NULL)
        return;

    block_t *b = validate_ptr(ptr, 0);
    if (b == NULL)
        return;

    b->free = 1;
    coalesce_all();
}

void *my_calloc(size_t nmemb, size_t size)
{
    if (nmemb == 0 || size == 0)
        return NULL;
    if (nmemb > SIZE_MAX / size)               /* multiplication would overflow */
        return NULL;

    size_t total = nmemb * size;
    void *p = my_malloc(total);
    if (p == NULL)
        return NULL;
    memset(p, 0, total);
    return p;
}

void *my_realloc(void *ptr, size_t size)
{
    if (ptr == NULL)
        return my_malloc(size);

    if (size == 0) {
        my_free(ptr);
        return NULL;
    }

    block_t *b = validate_ptr(ptr, 1);
    if (b == NULL)
        return NULL;
    if (size > SIZE_MAX - 15)
        return NULL;

    size_t need = align_up(size);

    /* Case A: already big enough */
    if (b->size >= need) {
        split_block(b, need);
        coalesce_all();
        return ptr;
    }

    /* Case B: next block is free, physically adjacent, and big enough */
    block_t *n = b->next;
    if (n != NULL && n->free
        && (char *)payload(b) + b->size == (char *)n
        && b->size + HDR + n->size >= need) {
        b->size += HDR + n->size;
        b->next  = n->next;
        n->magic = 0;
        n->size  = 0;
        n->next  = NULL;
        n->free  = 0;
        if (g_tail == n)
            g_tail = b;
        split_block(b, need);
        coalesce_all();
        return ptr;
    }

    /* Case C: allocate new, copy, free old (copy BEFORE free) */
    void *np = my_malloc(size);
    if (np == NULL)
        return NULL;
    size_t copy = b->size < size ? b->size : size;
    memcpy(np, ptr, copy);
    my_free(ptr);
    return np;
}

/* ---------- statistics and debugging ---------- */

void heap_stats(heap_stats_t *out)
{
    if (out == NULL)
        return;

    out->heap_bytes   = g_heap_bytes;
    out->used_bytes   = 0;
    out->free_bytes   = 0;
    out->blocks       = 0;
    out->free_blocks  = 0;
    out->largest_free = 0;

    for (block_t *b = g_head; b != NULL; b = b->next) {
        out->blocks++;
        if (b->free) {
            out->free_blocks++;
            out->free_bytes += b->size;
            if (b->size > out->largest_free)
                out->largest_free = b->size;
        } else {
            out->used_bytes += b->size;
        }
    }
}

void heap_dump(void)
{
    char buf[160];
    int idx = 0;

    for (block_t *b = g_head; b != NULL; b = b->next, idx++) {
        int n = snprintf(buf, sizeof buf, "[%2d] %p size=%zu %s\n",
                         idx, (void *)b, b->size,
                         b->free ? "FREE" : "USED");
        if (n > 0) {
            size_t w = ((size_t)n < sizeof buf) ? (size_t)n : sizeof buf - 1;
            ssize_t r = write(1, buf, w);
            (void)r;
        }
    }
}

void set_policy(fit_policy_t p)
{
    g_policy = p;
}

int heap_check(void)
{
    block_t *prev = NULL;

    for (block_t *b = g_head; b != NULL; b = b->next) {
        if (b->magic != MAGIC)                         /* rule 1: valid header */
            return 0;
        if ((uintptr_t)payload(b) % 16u != 0)          /* rule 2: aligned payload */
            return 0;
        if (prev != NULL) {
            char *prev_end = (char *)payload(prev) + prev->size;
            if ((char *)b < prev_end)                  /* rule 3: increasing order */
                return 0;
            if (prev->free && b->free && prev_end == (char *)b)
                return 0;                              /* rule 4: unmerged neighbors */
        }
        prev = b;
    }
    return 1;
}
