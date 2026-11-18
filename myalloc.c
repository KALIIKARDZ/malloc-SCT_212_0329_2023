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

/* grow(): obtain memory from sbrk, append one new free block. */
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

/* find_fit(): first free block with size >= need, or NULL. */
static block_t *find_fit(size_t need)
{
    for (block_t *b = g_head; b != NULL; b = b->next) {
        if (b->free && b->size >= need)
            return b;
    }
    return NULL;
}

/* split_block(): carve `need` off the front of b, leave free remainder. */
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

/* my_malloc: first-fit with splitting. */
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

/* my_free: bounds check, magic check, mark free, coalesce. */
void my_free(void *ptr)
{
    (void)ptr;
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

/* heap_stats: real numbers from a list walk. */
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
