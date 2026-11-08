#define _GNU_SOURCE
#include "lab_common.h"

#include <signal.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

/*
 * README 二 / SIGSEGV.
 *
 * Two distinct reasons for the same signal:
 *   (1) the fault address is not covered by ANY VMA (0x0),
 *   (2) the VMA is valid but lacks the required permission (PROT_READ only).
 */

/* Volatile keeps GCC from folding the constant null into a compile-time trap. */
static int null_deref(void *arg)
{
    volatile uintptr_t base = 0;
    volatile int *p = (volatile int *)base;
    (void)arg;
    *p = 100;
    return 0;
}

static int write_readonly_page(void *arg)
{
    long ps = sysconf(_SC_PAGESIZE);
    char *m;
    volatile char c;
    (void)arg;

    m = mmap(NULL, (size_t)ps, PROT_READ,
             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (m == MAP_FAILED)
        return 20;
    c = m[0];            /* bring the read-only page in              */
    (void)c;
    /* Volatile store: guaranteed to be emitted (no dead-store removal). */
    ((volatile char *)m)[0] = 'X';   /* PROT_READ VMA -> SIGSEGV      */
    return 0;
}

int main(void)
{
    lab_child_result r;
    int ok1, ok2;

    lab_log_init("segfault");
    lab_log_platform();

    lab_log_section("CASE 1 - NULL pointer dereference (README 二)");
    lab_log("fault address 0x0 belongs to no VMA -> SIGSEGV");
    lab_log("GDB: 'info proc mappings' shows which ranges ARE valid");
    if (lab_run_child(null_deref, NULL, &r) == 0)
        lab_log_child_result(&r);
    ok1 = (r.signaled && r.termsig == SIGSEGV);
    lab_log("=> %s", ok1 ? "PASS: SIGSEGV observed" : "FAIL: unexpected outcome");

    lab_log_section("CASE 2 - write to a read-only VMA (README 二 note)");
    lab_log("the address IS mapped but has no write permission -> also SIGSEGV");
    lab_log("(page is mmap'ed with PROT_READ only, then written to)");
    if (lab_run_child(write_readonly_page, NULL, &r) == 0)
        lab_log_child_result(&r);
    ok2 = (r.signaled && r.termsig == SIGSEGV);
    lab_log("=> %s", ok2 ? "PASS: SIGSEGV observed" : "FAIL: unexpected outcome");

    lab_log_section("valid VMAs of the parent process (/proc/self/maps)");
    lab_dump_maps();

    lab_log("FINAL: %s", (ok1 && ok2) ? "PASS" : "FAIL");
    lab_log_fini();
    return (ok1 && ok2) ? 0 : 1;
}
