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

/* ---- stubs for Tasks 2-7 ---- */

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
