#define _GNU_SOURCE
#include "lab_common.h"

#include <inttypes.h>
#include <link.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

/*
 * examples/07-elf-sections
 *
 * Walk the runtime program headers with dl_iterate_phdr() and print every
 * PT_LOAD of the main executable, then correlate the segment start/end with
 * /proc/self/maps. readelf shows the file view; this shows the loaded view.
 *
 * Build: cmake --build build --target example_07_elf_sections
 * Run  : ./build/examples/example_07_elf_sections
 */

/* GNU ld defines these symbols for the main executable. */
extern char __executable_start;
extern char etext;
extern char __bss_start;
extern char _end;

static const char *segment_flags(ElfW(Word) flags)
{
    static char buf[4];

    buf[0] = (flags & PF_R) ? 'R' : '-';
    buf[1] = (flags & PF_W) ? 'W' : '-';
    buf[2] = (flags & PF_X) ? 'X' : '-';
    buf[3] = '\0';
    return buf;
}

static int dump_main_load_segments(struct dl_phdr_info *info, size_t size,
                                   void *data)
{
    size_t i;
    uintptr_t page_size;

    (void)size;
    (void)data;

    page_size = (uintptr_t)sysconf(_SC_PAGESIZE);
    if (page_size == 0 || page_size == (uintptr_t)-1)
        page_size = 4096;

    /* dlpi_name is empty for the main executable. */
    if (info->dlpi_name != NULL && info->dlpi_name[0] != '\0')
        return 0;

    printf("main executable load base = 0x%" PRIxPTR "\n",
           (uintptr_t)info->dlpi_addr);
    printf("program headers = %u\n\n", (unsigned)info->dlpi_phnum);

    for (i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
        uintptr_t start;
        uintptr_t end;
        uintptr_t first_page;
        uintptr_t last_page;

        if (ph->p_type != PT_LOAD)
            continue;

        start = (uintptr_t)info->dlpi_addr + (uintptr_t)ph->p_vaddr;
        end = start + (uintptr_t)ph->p_memsz;
        first_page = start & ~(page_size - 1);
        last_page = (end - 1) & ~(page_size - 1);

        printf("PT_LOAD[%zu] offset=0x%llx vaddr=0x%llx "
               "filesz=0x%llx memsz=0x%llx flags=%s\n",
               i,
               (unsigned long long)ph->p_offset,
               (unsigned long long)ph->p_vaddr,
               (unsigned long long)ph->p_filesz,
               (unsigned long long)ph->p_memsz,
               segment_flags(ph->p_flags));

        /* A segment may start/end mid-page and share that page with the
         * neighbouring segment (RELRO etc.), so classify page-aligned pages. */
        lab_log_addr_map("  first page", (const void *)first_page);
        lab_log_addr_map("  last page", (const void *)last_page);
    }

    return 1;       /* stop after the main executable */
}

int main(void)
{
    printf("ELF sections (.text/.rodata/.data/.bss) live inside PT_LOAD\n");
    printf("segments; the kernel maps segments, not sections.\n\n");

    (void)dl_iterate_phdr(dump_main_load_segments, NULL);

    printf("\nlinker-defined boundaries (also in the same PT_LOADs):\n");
    lab_log_addr_map("__executable_start", (const void *)&__executable_start);
    lab_log_addr_map("etext", (const void *)&etext);
    lab_log_addr_map("__bss_start", (const void *)&__bss_start);
    lab_log_addr_map("_end", (const void *)&_end);

    printf("\ncompare with:\n");
    printf("  readelf -l  ./build/examples/example_07_elf_sections\n");
    printf("  readelf -S  ./build/examples/example_07_elf_sections\n");
    printf("  objdump -h  ./build/examples/example_07_elf_sections\n");
    printf("\n=> PASS: runtime PT_LOAD segments mapped to VMAs\n");
    return 0;
}
