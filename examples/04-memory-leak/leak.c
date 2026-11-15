#define _GNU_SOURCE
#include "lab_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * examples/04-memory-leak
 *
 * Allocate and TOUCH memory, lose every pointer (the actual leak), then
 * observe VmRSS / VmData. The process exit reclaims everything; a server
 * that keeps running would not.
 *
 * Build: cmake --build build --target example_04_memory_leak
 * Run  : ./build/examples/example_04_memory_leak
 */

#define BLOCKS       128
#define BLOCK_BYTES  (64u * 1024u)

static void *blocks[BLOCKS];

static void report_vm(const char *what)
{
    long vsz = -1, rss = -1, data = -1;

    if (lab_read_vm(&vsz, &rss, &data) == 0)
        printf("%-14s VmSize=%ld kB  VmRSS=%ld kB  VmData=%ld kB\n",
               what, vsz, rss, data);
    else
        printf("%-14s /proc/self/status unavailable\n", what);
}

int main(void)
{
    unsigned long checksum = 0;
    size_t i;

    printf("memory leak demo: %d blocks x %u KiB, pointers are dropped\n",
           BLOCKS, BLOCK_BYTES / 1024u);
    report_vm("before:");

    for (i = 0; i < BLOCKS; ++i) {
        blocks[i] = malloc(BLOCK_BYTES);
        if (blocks[i] == NULL) {
            printf("malloc failed at block %zu\n", i);
            break;
        }
        memset(blocks[i], (int)(i & 0xff), BLOCK_BYTES);  /* fault pages in */
    }

    /* Touch every block so the compiler cannot elide the allocations. */
    for (i = 0; i < BLOCKS; ++i) {
        if (blocks[i] != NULL)
            checksum += ((unsigned char *)blocks[i])[i % BLOCK_BYTES];
    }

    report_vm("after alloc:");
    printf("checksum=%lu (keeps the allocations observable)\n", checksum);

    /* The leak: the only references are gone, so a long-running process can
     * never free these blocks again. Exiting reclaims them; a server does not. */
    memset(blocks, 0, sizeof(blocks));

    report_vm("after leak:");
    printf("\n=> memory was allocated and is now unreachable (leaked).\n");
    printf("RSS is NOT expected to shrink: glibc may keep arenas and free\n");
    printf("chunks, so only a trend over time proves a real leak.\n");
    printf("Detect with: valgrind --leak-check=full, or LSan/ASan.\n");
    return 0;
}
