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

/*
 * grow() -- obtain at least `need` payload bytes from sbrk() and append
 * one new free block to the end of the list.
 *
 * `need` is assumed already 16-byte aligned (my_malloc aligns it).
 *
 * Returns the new block, or NULL on overflow / sbrk failure.
 * Silent on failure: stdio is forbidden inside the allocator.
 */
static block_t *grow(size_t need)
{
    /* Reject sizes that overflow want, align_up, or sbrk's intptr_t. */
    if (need > (size_t)PTRDIFF_MAX - HDR - MIN_CHUNK - 16u)
        return NULL;

    size_t want = need + HDR;
    if (want < MIN_CHUNK)
        want = MIN_CHUNK;

    /* Current break; we add alignment padding so the header lands on 16. */
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
    if (old != cur)                 /* defensive: glibc interposition? */
        return NULL;

    block_t *b = (block_t *)((char *)old + pad);
    b->size  = want - pad - HDR;    /* payload bytes actually available */
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

/* ---- stubs for Tasks 3-7 ---- */

void *my_malloc(size_t size)
{
    if (size == 0)
        return NULL;
    size_t need = align_up(size);
    (void)need;
    return NULL;
}

void my_free(void *ptr)
{
    if (ptr == NULL)
        return;
    block_t *b = header(ptr);
    (void)b;
}

void *my_calloc(size_t nmemb, size_t size)
{
    (void)nmemb; (void)size;
    return NULL;
}

void *my_realloc(void *ptr, size_t size)
{
    (void)ptr; (void)size;
    return NULL;
}

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
}

void heap_dump(void)
{
    (void)payload;
    (void)grow;    /* temporary: grow() is static and not yet called by my_malloc */
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
