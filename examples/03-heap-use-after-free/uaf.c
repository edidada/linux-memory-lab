#define _GNU_SOURCE
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/*
 * examples/03-heap-use-after-free
 *
 * free() normally leaves the virtual address mapped, so reading the object
 * afterwards often "works" and even returns the old value. That is exactly
 * why a plain run is not a detector: use AddressSanitizer for that.
 *
 * Build: cmake --build build --target example_03_heap_use_after_free
 * Run  : ./build/examples/example_03_heap_use_after_free
 */

static int use_after_free(void)
{
    /* volatile pointer breaks the SSA chain so GCC's -Wuse-after-free does
     * not diagnose this deliberately demonstrated undefined behaviour. */
    int *volatile p = malloc(sizeof(*p));
    volatile int v;

    if (p == NULL)
        return 200;
    *p = 100;
    free(p);
    v = *p;                 /* undefined behaviour: read after free */
    return (int)(v & 0xff);
}

int main(void)
{
    pid_t pid;
    int status = 0;

    printf("heap use-after-free demo\n");
    printf("free() does not necessarily unmap the VMA, so a plain run may\n");
    printf("print the old value or crash unpredictably - both are valid UB.\n\n");

    pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }
    if (pid == 0)
        _exit(use_after_free());

    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        return 1;
    }

    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        printf("child terminated by signal %d (%s) - UB manifested as a fault\n",
               sig, strsignal(sig));
    } else if (WIFEXITED(status)) {
        printf("child read *p = %d after free() (meaningless, but often 100)\n",
               WEXITSTATUS(status));
    }

    printf("\nthis example always passes: a plain run cannot reliably detect UAF.\n");
    printf("build with -DLAB_ENABLE_ASAN=ON and run the same code to get\n");
    printf("'ERROR: AddressSanitizer: heap-use-after-free' with a stack trace.\n");
    return 0;
}
