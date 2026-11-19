#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "myalloc.h"

#define NOPS   50000      /* total operations */
#define NSLOTS 1000       /* pointers that can be live at once */
#define MAXSZ  4096       /* sizes are 1..4096 bytes */
#define SEED   12345      /* fixed seed: identical workload every run */

static int    op_slot[NOPS];
static size_t op_size[NOPS];
static void  *slots[NSLOTS];
static int    failures = 0;

static double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* If the chosen slot is in use, free it; otherwise allocate into it. */
static void run(int use_libc)
{
    for (int i = 0; i < NOPS; i++) {
        int s = op_slot[i];
        if (slots[s] != NULL) {
            if (use_libc)
                free(slots[s]);
            else
                my_free(slots[s]);
            slots[s] = NULL;
        } else {
            void *p = use_libc ? malloc(op_size[i]) : my_malloc(op_size[i]);
            if (p == NULL)
                failures++;
            else
                *(char *)p = 1;          /* touch the memory */
            slots[s] = p;
        }
    }
}

static void usage(const char *prog)
{
    fprintf(stderr, "usage: %s first|best|libc\n", prog);
    exit(1);
}

int main(int argc, char **argv)
{
    int use_libc = 0;

    if (argc != 2)
        usage(argv[0]);
    if (strcmp(argv[1], "first") == 0)
        set_policy(FIT_FIRST);
    else if (strcmp(argv[1], "best") == 0)
        set_policy(FIT_BEST);
    else if (strcmp(argv[1], "libc") == 0)
        use_libc = 1;
    else
        usage(argv[0]);

    /* build the workload once */
    srand(SEED);
    for (int i = 0; i < NOPS; i++) {
        op_slot[i] = rand() % NSLOTS;
        op_size[i] = 1 + (size_t)(rand() % MAXSZ);
    }

    double t0 = now_sec();
    run(use_libc);
    double t1 = now_sec();

    /* record statistics while blocks are still live */
    heap_stats_t st;
    memset(&st, 0, sizeof st);
    if (!use_libc)
        heap_stats(&st);

    /* clean up so nothing is left allocated */
    for (int i = 0; i < NSLOTS; i++) {
        if (slots[i] != NULL) {
            if (use_libc)
                free(slots[i]);
            else
                my_free(slots[i]);
            slots[i] = NULL;
        }
    }

    printf("mode=%s time_ms=%.3f failed_allocs=%d\n",
           argv[1], (t1 - t0) * 1000.0, failures);

    if (!use_libc) {
        double frag = 0.0;
        if (st.free_bytes > 0)
            frag = 1.0 - (double)st.largest_free / (double)st.free_bytes;
        printf("heap_bytes=%zu used_bytes=%zu free_bytes=%zu "
               "free_blocks=%zu largest_free=%zu\n",
               st.heap_bytes, st.used_bytes, st.free_bytes,
               st.free_blocks, st.largest_free);
        printf("external_fragmentation=%.4f\n", frag);
        printf("heap_check_after_cleanup=%d\n", heap_check());
    }
    return 0;
}
