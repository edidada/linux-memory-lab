#define _GNU_SOURCE
#include "lab_common.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#define AS_VOIDP(x) ((const void *)(uintptr_t)(x))
#define ANON_SIZE   (64 * 1024)

/*
 * README 九 / the core experiment:
 * create global (.data/.bss), local (stack), malloc (heap) and mmap objects,
 * print their real virtual addresses and classify each against
 * /proc/self/maps. This ties C variables -> ELF sections -> VMAs together.
 */
static volatile int g_data   = 0x11223344;        /* .data              */
static volatile int g_bss;                         /* .bss               */
static volatile int g_static;                      /* .bss (static)      */

__attribute__((noinline))
static int code_fn(int x)                          /* .text              */
{
    return x + 1;
}

int main(void)
{
    volatile int stack_var = 0x55;                  /* stack              */
    const char *ro_literal = "linux-memory-lab .rodata literal"; /* .rodata */
    int  *heap = malloc(ANON_SIZE);                /* heap (brk/mmap)    */
    void *anon = mmap(NULL, ANON_SIZE, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);  /* anon VMA   */
    /* Union punning avoids a non-ISO function-pointer -> void* cast. */
    union { int (*fn)(int); void *obj; } code = { .fn = code_fn };
    union { int (*p)(const char *, ...); void *obj; } libc = { .p = printf };

    lab_log_init("addr_layout");
    lab_log_platform();

    lab_log_section("CORE EXPERIMENT - C objects -> virtual addresses (README 九)");
    lab_log("PIE + ASLR randomize ABSOLUTE addresses, but the relative layout");
    lab_log("(text/rodata/data/bss < heap < mmap < stack) is preserved.");
    lab_log("Compare 'machine' and address widths across x86-64 and ARM64 logs.");
    lab_log("");
    lab_log("%-26s %-18s %s", "OBJECT", "ADDRESS", "VMA (from /proc/self/maps)");
    lab_log_addr_map("global .data (g_data)",   AS_VOIDP(&g_data));
    lab_log_addr_map("global .bss (g_bss)",     AS_VOIDP(&g_bss));
    lab_log_addr_map("static .bss (g_static)",  AS_VOIDP(&g_static));
    lab_log_addr_map(".rodata literal",         AS_VOIDP(ro_literal));
    lab_log_addr_map("function .text",          code.obj);
    lab_log_addr_map("libc printf .text",       libc.obj);
    lab_log_addr_map("local (stack)",           AS_VOIDP(&stack_var));
    lab_log_addr_map("malloc heap",             AS_VOIDP(heap));
    lab_log_addr_map("mmap anonymous",          AS_VOIDP(anon));
    lab_log_addr_map("&ro_literal (stack ptr)",  AS_VOIDP(&ro_literal));

    lab_log("");
    lab_log("live values: g_data=0x%x g_bss=%d g_static=%d stack=0x%x",
            (unsigned)g_data, (int)g_bss, (int)g_static, (unsigned)stack_var);
    lab_log("rodata='%s'", ro_literal);

    lab_log_section("THE THREE CHAINS (README 九)");
    lab_log("1) C var -> GCC -> ELF .text/.data/.bss -> ELF loader -> VMA");
    lab_log("   -> page table -> MMU -> physical RAM");
    lab_log("2) malloc() -> glibc allocator -> brk/mmap -> demand paging");
    lab_log("3) C pointer -> LOAD/STORE -> MMU -> page fault -> SIGSEGV on error");
    lab_log("Not every page fault is a bug: demand paging and COW are normal.");

    lab_log_section("/proc/self/maps (complete VMA list)");
    lab_dump_maps();

    if (heap != NULL)
        free(heap);
    if (anon != MAP_FAILED)
        munmap(anon, ANON_SIZE);

    lab_log("FINAL: PASS");
    lab_log_fini();
    return 0;
}
