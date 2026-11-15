#define _GNU_SOURCE
#include "lab_common.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/*
 * examples/05-mmap
 *
 * Two mappings side by side:
 *   - a MAP_SHARED file mapping: writes become file content (msync + pread),
 *   - a MAP_PRIVATE anonymous mapping: demand-zero pages, not backed by a file.
 *
 * Build: cmake --build build --target example_05_mmap
 * Run  : ./build/examples/example_05_mmap
 */

#define MESSAGE "hello from a shared file mapping"

int main(void)
{
    char tmpl[] = "/tmp/lab_mmap_XXXXXX";
    const char *anon_text = "anonymous private mapping";
    long ps = sysconf(_SC_PAGESIZE);
    size_t len;
    int fd = -1;
    char *file_map = MAP_FAILED;
    char *anon_map = MAP_FAILED;
    char check[sizeof MESSAGE];
    ssize_t n;
    int rc = 1;

    if (ps <= 0) {
        fprintf(stderr, "sysconf(_SC_PAGESIZE) failed\n");
        return 1;
    }
    len = (size_t)ps * 2;

    fd = mkstemp(tmpl);
    if (fd < 0) {
        perror("mkstemp");
        return 1;
    }
    unlink(tmpl);                 /* the mapping keeps the file alive */

    if (ftruncate(fd, (off_t)len) != 0) {
        perror("ftruncate");
        goto out;
    }

    file_map = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (file_map == MAP_FAILED) {
        perror("mmap(file)");
        goto out;
    }
    memcpy(file_map, MESSAGE, sizeof MESSAGE);

    if (msync(file_map, len, MS_SYNC) != 0) {
        perror("msync");
        goto out;
    }

    n = pread(fd, check, sizeof check, 0);
    if (n != (ssize_t)sizeof check || memcmp(check, MESSAGE, sizeof MESSAGE) != 0) {
        fprintf(stderr, "pread() did not observe the shared write\n");
        goto out;
    }

    anon_map = mmap(NULL, len, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (anon_map == MAP_FAILED) {
        perror("mmap(anon)");
        goto out;
    }
    memcpy(anon_map, anon_text, strlen(anon_text) + 1);

    printf("MAP_SHARED file mapping and MAP_PRIVATE anonymous mapping\n\n");
    lab_log_addr_map("shared file map", file_map);
    lab_log_addr_map("anonymous map", anon_map);

    printf("\nshared bytes visible through pread(): \"%s\"\n", check);
    printf("anonymous bytes: \"%s\"\n", anon_map);
    printf("file is unlinked but still backed by the mapping until munmap()\n");
    printf("\n=> PASS: MAP_SHARED write reached the file; anonymous pages are private\n");
    rc = 0;

out:
    if (anon_map != MAP_FAILED)
        munmap(anon_map, len);
    if (file_map != MAP_FAILED)
        munmap(file_map, len);
    close(fd);
    return rc;
}
