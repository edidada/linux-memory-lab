#define _GNU_SOURCE
#include "lab_common.h"

#include <stdlib.h>
#include <string.h>

#define STEP_BYTES (16u * 1024u * 1024u)
#define STEPS       8                 /* up to 128 MiB touched */

/*
 * README 五 / a process whose memory keeps growing.
 * Reproduces the classic investigation loop in miniature: allocate and
 * TOUCH anonymous memory (demand paging), then watch VmSize / VmRSS /
 * VmData and smaps_rollup grow, and free() at the end.
 */
int main(void)
{
    void **blocks;
    long vsz, rss, data;
    int i;

    lab_log_init("memory_growth");
    lab_log_platform();

    lab_log_section("CASE - process memory keeps growing (README 五)");
    lab_log("field investigation commands:");
    lab_log("  cat /proc/<pid>/status | grep -E 'VmSize|VmRSS|VmData|VmStk|Threads'");
    lab_log("  cat /proc/<pid>/smaps_rollup");
    lab_log("  pmap -x <pid>");

    blocks = calloc(STEPS, sizeof(*blocks));
    if (blocks == NULL) {
        lab_log("calloc failed");
        lab_log_fini();
        return 1;
    }

    lab_log_section("touching %d MiB of anonymous memory in %d steps",
                    (int)(STEP_BYTES * STEPS / (1024u * 1024u)), STEPS);
    lab_log("%-6s %-14s %-14s %-14s", "step", "VmSize(kB)", "VmRSS(kB)",
            "VmData(kB)");
    for (i = 0; i < STEPS; ++i) {
        blocks[i] = malloc(STEP_BYTES);
        if (blocks[i] == NULL) {
            lab_log("malloc failed at step %d", i + 1);
            break;
        }
        memset(blocks[i], 0xAB, STEP_BYTES);  /* fault pages in -> RSS grows */
        if (lab_read_vm(&vsz, &rss, &data) == 0)
            lab_log("%-6d %-14ld %-14ld %-14ld", i + 1, vsz, rss, data);
    }

    lab_log_section("/proc/self/status (Vm* and Rss* fields)");
    lab_dump_status();
    lab_log_section("/proc/self/smaps_rollup");
    lab_dump_smaps_rollup();

    lab_log("RSS growth alone does NOT prove a leak: glibc malloc arenas,");
    lab_log("memory pools, application caches, thread stacks and anonymous");
    lab_log("mmap all appear here. Correlate with allocation tracing.");

    for (i = 0; i < STEPS; ++i)
        free(blocks[i]);              /* free(NULL) is a no-op */
    free(blocks);

    lab_log_section("after free()");
    if (lab_read_vm(&vsz, &rss, &data) == 0)
        lab_log("VmSize=%ld kB  VmRSS=%ld kB  VmData=%ld kB", vsz, rss, data);
    lab_log("glibc may retain freed arenas, so VmRSS need not drop at once.");

    lab_log("FINAL: PASS");
    lab_log_fini();
    return 0;
}
