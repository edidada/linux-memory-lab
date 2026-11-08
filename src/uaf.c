#define _GNU_SOURCE
#include "lab_common.h"

#include <stdint.h>
#include <stdlib.h>

/*
 * README 三 / heap Use After Free.
 *
 * free() does not have to revoke the virtual address mapping, so a later
 * read may still "succeed" and even return the old value. That is exactly
 * the point: a VALID virtual address does NOT mean a VALID object lifetime.
 * The reliable detector is AddressSanitizer, not a plain run.
 */
static int use_after_free(void *arg)
{
    /* volatile pointer breaks the SSA chain so -Wuse-after-free (GCC >= 12)
     * does not diagnose this deliberately demonstrated UB. */
    int *volatile p = malloc(sizeof(*p));
    volatile int v;
    (void)arg;

    if (p == NULL)
        return 200;
    *p = 100;
    free(p);
    v = *p;                       /* read after free -> undefined behavior */
    return (int)(v & 0xff);       /* observed value becomes child status   */
}

int main(void)
{
    lab_child_result r;
    int *probe;

    lab_log_init("uaf");
    lab_log_platform();

    lab_log_section("CASE - heap Use After Free (README 三)");
    lab_log("free() does not necessarily unmap the VMA; the address stays");
    lab_log("valid while the object lifetime has already ended.");
    lab_log("A plain run often prints 100 and does NOT crash - that is why");
    lab_log("ASan is required to detect this class of bug deterministically.");

    if (lab_run_child(use_after_free, NULL, &r) == 0)
        lab_log_child_result(&r);
    if (r.signaled)
        lab_log("child died with %s - UB manifested as a fault", r.sigdesc);
    else
        lab_log("child observed *p = %d after free() (meaningless, UB)",
                r.exitcode);

    lab_log_section("heap VMA context");
    probe = malloc(64 * 1024);
    if (probe != NULL) {
        lab_log("fresh malloc(64 KiB) @ %p", (void *)(uintptr_t)probe);
        lab_log_addr_map("malloc block", probe);
        free(probe);
    }
    lab_log("build with -DLAB_ENABLE_ASAN=ON then run ./uaf to get a");
    lab_log("'ERROR: AddressSanitizer: heap-use-after-free' report.");

    lab_log("FINAL: PASS (demonstration completed; parent never crashed)");
    lab_log_fini();
    return 0;
}
