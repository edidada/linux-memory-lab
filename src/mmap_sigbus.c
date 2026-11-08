#define _GNU_SOURCE
#include "lab_common.h"

#include <signal.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

/*
 * README 六 / mmap + SIGBUS.
 * A shared file mapping is truncated to zero by "another process"; touching
 * any page whose backing store no longer exists raises SIGBUS, which is a
 * different condition from the unmapped-address SIGSEGV of case 二.
 */
static int sigbus_case(void *arg)
{
    char tmpl[] = "/tmp/lab_sigbus_XXXXXX";
    long ps = sysconf(_SC_PAGESIZE);
    int fd;
    char *map;
    volatile char c;
    (void)arg;

    fd = mkstemp(tmpl);
    if (fd < 0)
        return 10;
    unlink(tmpl);                       /* file survives only through fd */

    if (ftruncate(fd, ps * 2) != 0)    /* two pages of backing store */
        return 11;

    map = mmap(NULL, (size_t)ps * 2, PROT_READ | PROT_WRITE,
               MAP_SHARED, fd, 0);
    if (map == MAP_FAILED)
        return 12;

    map[0] = 'A';                      /* first page is valid while file is 2 pages */

    /* truncate to zero length (as if another process did it).
     * Leave the second page untouched on purpose: accessing it afterwards
     * takes a clean page fault beyond the new EOF -> SIGBUS. */
    if (ftruncate(fd, 0) != 0)
        return 13;

    c = map[ps + 16];                  /* page beyond new EOF -> SIGBUS */
    (void)c;
    return 0;                          /* normally not reached */
}

int main(void)
{
    lab_child_result r;
    int ok;

    lab_log_init("mmap_sigbus");
    lab_log_platform();

    lab_log_section("CASE - mmap + ftruncate -> SIGBUS (README 六)");
    lab_log("  strace -f -e trace=mmap,munmap,mremap,ftruncate ./mmap_sigbus");
    lab_log("  GDB: handle SIGBUS stop print / run / bt / info proc mappings");
    lab_log("A mapped page beyond the truncated file's EOF raises SIGBUS.");
    lab_log("Typical sources: mmap'ed DBs, mmap'ed logs, shm IPC, ftruncate.");

    if (lab_run_child(sigbus_case, NULL, &r) == 0)
        lab_log_child_result(&r);

    ok = (r.signaled && (r.termsig == SIGBUS || r.termsig == SIGSEGV));
    lab_log("=> %s", r.signaled ? r.sigdesc : "no signal (unexpected)");
    if (r.signaled && r.termsig == SIGSEGV)
        lab_log("   (this kernel/filesystem reported SIGSEGV instead of SIGBUS)");
    lab_log("FINAL: %s", ok ? "PASS" : "FAIL");
    lab_log_fini();
    return ok ? 0 : 1;
}
