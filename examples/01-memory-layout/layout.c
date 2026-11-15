#define _GNU_SOURCE
#include "lab_common.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

/*
 * examples/01-memory-layout
 *
 * Minimal version of src/addr_layout.c: create one object per storage class,
 * print its real address, and classify that address against /proc/self/maps.
 *
 * Build: cmake --build build --target example_01_memory_layout
 * Run  : ./build/examples/example_01_memory_layout
 */

#define ANON_SIZE (64 * 1024)
#define AS_VOIDP(x) ((const void *)(uintptr_t)(x))

static volatile int g_data = 0x11223344;   /* .data */
static volatile int g_bss;                 /* .bss  */
static const char g_ro[] = "read-only global (.rodata)";

__attribute__((noinline))
static int code_fn(int x)                  /* .text */
{
    return x + 1;
}

int main(void)
{
    volatile int stack_var = 0x55;                       /* stack        */
    const char *literal = "string literal (.rodata)";    /* .rodata      */
    int *heap = malloc(ANON_SIZE);                       /* heap         */
    void *anon = mmap(NULL, ANON_SIZE, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    /* Union punning: avoids a non-ISO function-pointer -> void* cast. */
    union { int (*fn)(int); void *obj; } code = { .fn = code_fn };

    printf("C object -> virtual address -> VMA\n");
    printf("%-26s %-18s %s\n", "OBJECT", "ADDRESS", "VMA");

    lab_log_addr_map("global .data (g_data)",  AS_VOIDP(&g_data));
    lab_log_addr_map("global .bss  (g_bss)",   AS_VOIDP(&g_bss));
    lab_log_addr_map("const global (g_ro)",    AS_VOIDP(g_ro));
    lab_log_addr_map(".rodata literal",        AS_VOIDP(literal));
    lab_log_addr_map("function .text",         code.obj);
    lab_log_addr_map("local (stack)",          AS_VOIDP(&stack_var));
    lab_log_addr_map("malloc heap",            AS_VOIDP(heap));
    lab_log_addr_map("mmap anonymous",         AS_VOIDP(anon));

    printf("\nlive values: g_data=0x%x g_bss=%d stack=0x%x code_fn(41)=%d\n",
           (unsigned)g_data, (int)g_bss, (unsigned)stack_var, code.fn(41));
    printf("g_ro='%s' literal='%s'\n", g_ro, literal);

    printf("\ncomplete /proc/self/maps:\n");
    lab_dump_maps();

    if (heap != NULL)
        free(heap);
    if (anon != MAP_FAILED)
        munmap(anon, ANON_SIZE);
    return 0;
}
