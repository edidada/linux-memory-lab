#define _GNU_SOURCE
#include "lab_common.h"

#include <signal.h>
#include <sys/resource.h>

#define FRAME_PAD 8192

/*
 * README 四 / stack overflow.
 *
 * noinline + a volatile frame buffer + a read AFTER the recursive call keep
 * a real stack frame per invocation (no sibling/tail-call folding). A lowered
 * RLIMIT_STACK in the child makes exhaustion fast and reproducible.
 */
/* The unbounded recursion is intentional: it is what exhausts the stack.
 * Silence GCC's -Winfinite-recursion for this one deliberate function. */
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 12
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Winfinite-recursion"
#endif
__attribute__((noinline))
static int recurse(int depth)
{
    volatile char buffer[FRAME_PAD];
    buffer[0] = (char)(depth & 0xff);
    buffer[FRAME_PAD - 1] = (char)((depth >> 8) & 0xff);
    int deeper = recurse(depth + 1);
    return deeper + buffer[0];   /* use after the call -> not a tail call */
}
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 12
#  pragma GCC diagnostic pop
#endif

static int overflow(void *arg)
{
    struct rlimit rl;
    (void)arg;

    rl.rlim_cur = 1u << 20;     /* 1 MiB stack: bounded, deterministic */
    rl.rlim_max = 1u << 20;
    setrlimit(RLIMIT_STACK, &rl);
    return recurse(0);
}

int main(void)
{
    lab_child_result r;
    int ok;

    lab_log_init("stack_overflow");
    lab_log_platform();

    lab_log_section("CASE - stack overflow via recursion (README 四)");
    lab_log("each frame consumes ~%d bytes of the stack VMA", FRAME_PAD);
    lab_log("child lowers RLIMIT_STACK to 1 MiB for a fast bounded crash");
    lab_log("GDB: 'bt 20' shows repeated recursive() frames, rsp near the");
    lab_log("     stack VMA boundary; 'ulimit -s' reveals the stack limit.");

    if (lab_run_child(overflow, NULL, &r) == 0)
        lab_log_child_result(&r);

    ok = (r.signaled && (r.termsig == SIGSEGV || r.termsig == SIGBUS));
    lab_log("=> recursion exhausted the stack mapping: %s",
            r.signaled ? r.sigdesc : "no signal (unexpected)");
    lab_log("FINAL: %s", ok ? "PASS" : "FAIL");
    lab_log_fini();
    return ok ? 0 : 1;
}
