#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

/*
 * examples/02-stack-overflow
 *
 * Bounded, deterministic stack exhaustion:
 *   - fork a child so the fault never kills the example,
 *   - lower RLIMIT_STACK to 1 MiB in the child,
 *   - recurse with a ~16 KiB frame until the stack VMA is exhausted.
 *
 * Build: cmake --build build --target example_02_stack_overflow
 * Run  : ./build/examples/example_02_stack_overflow
 */

#define FRAME_PAD (16 * 1024)

#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 12
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Winfinite-recursion"
#endif
__attribute__((noinline))
static int recurse(int depth)
{
    volatile char frame[FRAME_PAD];
    int deeper;

    frame[0] = (char)(depth & 0xff);
    frame[FRAME_PAD - 1] = (char)((depth >> 8) & 0xff);
    deeper = recurse(depth + 1);
    return deeper + frame[0];       /* use after the call: not a tail call */
}
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 12
#  pragma GCC diagnostic pop
#endif

static int exhaust_stack(void)
{
    struct rlimit rl;

    rl.rlim_cur = 1u << 20;         /* 1 MiB: fast and reproducible */
    rl.rlim_max = 1u << 20;
    if (setrlimit(RLIMIT_STACK, &rl) != 0)
        return 250;
    return recurse(0);
}

int main(void)
{
    pid_t pid;
    int status = 0;

    printf("stack overflow demo: each frame ~%d bytes, RLIMIT_STACK=1 MiB\n",
           FRAME_PAD);
    printf("parents survives because the fault happens in a forked child\n\n");

    pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }
    if (pid == 0)
        _exit(exhaust_stack() & 0xff);

    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        return 1;
    }

    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        printf("child terminated by signal %d (%s)\n", sig, strsignal(sig));
        if (sig == SIGSEGV || sig == SIGBUS) {
            printf("=> PASS: the stack mapping was exhausted\n");
            printf("GDB: bt 20 shows repeated recurse() frames; rsp/sp is near "
                   "the stack VMA boundary\n");
            return 0;
        }
    } else if (WIFEXITED(status)) {
        printf("child exited with code %d (no signal)\n", WEXITSTATUS(status));
    }

    printf("=> FAIL: expected SIGSEGV or SIGBUS\n");
    return 1;
}
