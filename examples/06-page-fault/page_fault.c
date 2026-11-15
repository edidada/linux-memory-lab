#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>

/*
 * examples/06-page-fault
 *
 * NOT every page fault is a bug. Demand paging and MADV_DONTNEED both cause
 * legitimate minor faults. This program measures them with getrusage().
 *
 * Build: cmake --build build --target example_06_page_fault
 * Run  : ./build/examples/example_06_page_fault
 */

#define REGION_BYTES (32u * 1024u * 1024u)

static void snapshot(long *minflt, long *majflt)
{
    struct rusage ru;

    if (getrusage(RUSAGE_SELF, &ru) == 0) {
        *minflt = ru.ru_minflt;
        *majflt = ru.ru_majflt;
    } else {
        *minflt = -1;
        *majflt = -1;
    }
}

static void report(const char *what, long minflt, long majflt)
{
    printf("%-28s minflt=%-8ld majflt=%ld\n", what, minflt, majflt);
}

int main(void)
{
    long ps = sysconf(_SC_PAGESIZE);
    size_t len = REGION_BYTES;
    size_t i;
    char *p;
    long mn0, mj0, mn1, mj1, mn2, mj2;

    if (ps <= 0) {
        fprintf(stderr, "sysconf(_SC_PAGESIZE) failed\n");
        return 1;
    }

    p = mmap(NULL, len, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    printf("demand paging over %u MiB with %ld-byte pages (%zu pages)\n\n",
           REGION_BYTES / (1024u * 1024u), ps, len / (size_t)ps);

    snapshot(&mn0, &mj0);
    report("after mmap, before touch", mn0, mj0);

    for (i = 0; i < len; i += (size_t)ps)
        p[i] = (char)(i & 0x7f);       /* first touch: allocate + zero a page */

    snapshot(&mn1, &mj1);
    report("after touching all pages", mn1, mj1);
    printf("  -> delta: minor=%ld major=%ld\n", mn1 - mn0, mj1 - mj0);

    if (madvise(p, len, MADV_DONTNEED) != 0)
        perror("madvise(MADV_DONTNEED)");

    snapshot(&mn2, &mj2);
    report("after MADV_DONTNEED", mn2, mj2);

    for (i = 0; i < len; i += (size_t)ps)
        p[i] = (char)(i & 0x7f);       /* fault the pages in again */

    snapshot(&mn2, &mj2);
    report("after touching again", mn2, mj2);

    munmap(p, len);

    printf("\nminor faults are normal: demand paging, COW and page-cache hits\n");
    printf("major faults require I/O (file-backed page not in page cache)\n");
    printf("=> PASS: fault counters were observed (values vary by kernel/machine)\n");
    return 0;
}
