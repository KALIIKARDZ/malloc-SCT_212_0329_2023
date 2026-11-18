#define _DEFAULT_SOURCE
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include "myalloc.h"

#define MAGIC 0x4D59414C   /* 'MYAL' */

typedef struct block {
    size_t        size;
    struct block *next;
    int           free;
    int           magic;
    size_t        pad;
} block_t;

#define HDR       (((size_t)sizeof(block_t) + 15u) & ~(size_t)15u)
#define MIN_SPLIT (HDR + 16)
#define MIN_CHUNK (64u * 1024u)

_Static_assert(HDR % 16 == 0, "HDR must be a multiple of 16");

static block_t      *g_head       = NULL;
static block_t      *g_tail       = NULL;
static char         *g_heap_start = NULL;
static char         *g_heap_end   = NULL;
static size_t        g_heap_bytes = 0;
static fit_policy_t  g_policy     = FIT_FIRST;

static size_t align_up(size_t n)
{
    return (n + 15u) & ~(size_t)15u;
}

static void *payload(block_t *b)
{
    return (char *)b + HDR;
}

static block_t *header(void *ptr)
{
    return (block_t *)((char *)ptr - HDR);
}

static void write_str(const char *s)
{
    size_t len = 0;
    while (s[len]) len++;
    ssize_t r = write(2, s, len);
    (void)r;
}

static block_t *validate_ptr(void *ptr, int quiet)
{
    if (g_heap_start == NULL) {
        if (!quiet) write_str("my_free: invalid pointer\n");
        return NULL;
    }

    uintptr_t p  = (uintptr_t)ptr;
    uintptr_t lo = (uintptr_t)g_heap_start;
    if (p < lo + HDR || p >= (uintptr_t)g_heap_end || (p % 16u) != 0) {
        if (!quiet) write_str("my_free: invalid pointer\n");
        return NULL;
    }

    block_t *b = header(ptr);
    if (b->magic != MAGIC) {
        if (!quiet) write_str("my_free: invalid pointer\n");
        return NULL;
    }
    if (b->free) {
        if (!quiet) write_str("my_free: double free detected\n");
        return NULL;
    }
    return b;
}

static block_t *grow(size_t need)
{
    if (need > (size_t)PTRDIFF_MAX - HDR - MIN_CHUNK - 16u)
        return NULL;

    size_t want = need + HDR;
    if (want < MIN_CHUNK)
        want = MIN_CHUNK;

    char *cur = sbrk(0);
    if (cur == (char *)-1)
        return NULL;

    size_t pad = align_up((uintptr_t)cur) - (uintptr_t)cur;
    if (pad != 0) {
        if ((size_t)PTRDIFF_MAX - want < pad)
            return NULL;
        want += pad;
    }

    void *old = sbrk((intptr_t)want);
    if (old == (void *)-1)
        return NULL;
    if (old != cur)
        return NULL;

    block_t *b = (block_t *)((char *)old + pad);
    b->size  = want - pad - HDR;
    b->next  = NULL;
    b->free  = 1;
    b->magic = MAGIC;
    b->pad   = 0;

    if (g_head == NULL) {
        g_head = b;
        g_tail = b;
        g_heap_start = (char *)b;
    } else {
        g_tail->next = b;
        g_tail       = b;
    }
    g_heap_end   = (char *)b + HDR + b->size;
    g_heap_bytes += want;

    return b;
}

static block_t *find_fit(size_t need)
{
    for (block_t *b = g_head; b != NULL; b = b->next) {
        if (b->free && b->size >= need)
            return b;
    }
    return NULL;
}

static void split_block(block_t *b, size_t need)
{
    if (b->size - need < MIN_SPLIT)
        return;

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

void *my_malloc(size_t size)
{
    if (size == 0)
        return NULL;
    if (size > SIZE_MAX - 15u)
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

static void coalesce_all(void)
{
    block_t *b = g_head;
    while (b != NULL && b->next != NULL) {
        block_t *n = b->next;
        int adjacent = ((char *)payload(b) + b->size == (char *)n);
        if (b->free && n->free && adjacent) {
            b->size += HDR + n->size;
            b->next  = n->next;
            n->magic = 0;
            n->size  = 0;
            n->next  = NULL;
            n->free  = 0;
            if (g_tail == n)
                g_tail = b;
        } else {
            b = b->next;
        }
    }
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
    if (nmemb > SIZE_MAX / size)
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

    if (size > SIZE_MAX - 15u)
        return NULL;                    /* leave old block untouched */

    block_t *b = validate_ptr(ptr, 1);  /* quiet: standard says NULL on bad ptr */
    if (b == NULL)
        return NULL;

    size_t need = align_up(size);

    /* Case A: fits in place */
    if (b->size >= need) {
        split_block(b, need);
        coalesce_all();                 /* merge any newly adjacent free blocks */
        return ptr;
    }

    /* Case B: next block is free, adjacent, big enough */
    if (b->next != NULL
        && b->next->free
        && (char *)payload(b) + b->size == (char *)b->next
        && b->size + HDR + b->next->size >= need)
    {
        block_t *n = b->next;
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

    /* Case C: allocate new, copy, free old */
    void *np = my_malloc(size);
    if (np == NULL)
        return NULL;
    size_t copy = b->size;              /* b->size < size, so copy all of it */
    memcpy(np, ptr, copy);
    my_free(ptr);
    return np;
}

void heap_stats(heap_stats_t *out)
{
    if (out == NULL)
        return;

    size_t used = 0, fr = 0, blocks = 0, free_blocks = 0, largest = 0;

    for (block_t *b = g_head; b != NULL; b = b->next) {
        blocks++;
        if (b->free) {
            free_blocks++;
            fr += b->size;
            if (b->size > largest)
                largest = b->size;
        } else {
            used += b->size;
        }
    }

    out->heap_bytes   = g_heap_bytes;
    out->used_bytes   = used;
    out->free_bytes   = fr;
    out->blocks       = blocks;
    out->free_blocks  = free_blocks;
    out->largest_free = largest;
}

void heap_dump(void)
{
    /* populated in Task 7 */
}

void set_policy(fit_policy_t p)
{
    g_policy = p;
    (void)g_policy;
}

int heap_check(void)
{
    (void)g_head;
    (void)g_tail;
    (void)g_heap_start;
    (void)g_heap_end;
    return 1;
}
