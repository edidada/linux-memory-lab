#define _GNU_SOURCE
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/*
 * examples/08-assembly-memory
 *
 * The compiler turns every C read/write into LOAD/STORE instructions against
 * addresses. Once you take the address of a local, it must live in memory.
 * This example prints arguments, locals, frame pointers and return addresses
 * so the stack frame layout can be compared with objdump output.
 *
 * Build: cmake --build build --target example_08_assembly_memory
 * Run  : ./build/examples/example_08_assembly_memory
 */

#define AS_VOIDP(x) ((void *)(uintptr_t)(x))

__attribute__((noinline))
static int inspect(int arg, int depth)
{
    volatile int local = arg * 10 + depth;
    void *frame = __builtin_frame_address(0);
    void *ret = __builtin_return_address(0);
    ptrdiff_t delta = (char *)frame - (char *)&local;

    printf("depth=%d &arg=%p &local=%p frame=%p ret=%p frame-local=%td\n",
           depth, AS_VOIDP(&arg), AS_VOIDP(&local), frame, ret, delta);

    if (depth < 3)
        return inspect(arg + 1, depth + 1) + local;
    return local;
}

int main(void)
{
    int sum = inspect(1, 0);

    printf("\nsum of locals = %d\n", sum);
    printf("taking &arg/&local forces them into stack memory (not registers)\n");
    printf("the return address shows where .text lives; frames are contiguous\n");
    printf("on x86-64/AArch64 the stack grows toward LOWER addresses, so\n");
    printf("frame-local is normally positive and recursion moves down.\n");

    printf("\ndisassemble to see the LOAD/STORE instructions:\n");
    printf("  objdump -d --no-show-raw-insn ./build/examples/example_08_assembly_memory\n");
    printf("GDB:\n");
    printf("  x/16gx $rsp    # x86-64\n");
    printf("  x/16gx $sp     # ARM64\n");
    printf("  info registers\n");
    printf("\n=> PASS: stack frame addresses and return addresses observed\n");
    return 0;
}
