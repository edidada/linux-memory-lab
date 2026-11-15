# Standalone teaching examples referenced by docs/ and examples/*/README.md.
#
# They are compiled and run by CI so they cannot silently rot. Unlike the six
# src/ demos they do NOT write into logs/: the tracked logs/<runner>/ tree is
# owned by the main demonstrations. Requires the lab_common support library
# from src/, so LAB_BUILD_DEMOS must stay ON (the top-level CMake enforces it).

add_executable(example_01_memory_layout 01-memory-layout/layout.c)
add_executable(example_02_stack_overflow 02-stack-overflow/stack_overflow.c)
add_executable(example_03_heap_use_after_free 03-heap-use-after-free/uaf.c)
add_executable(example_04_memory_leak 04-memory-leak/leak.c)
add_executable(example_05_mmap 05-mmap/mmap_demo.c)
add_executable(example_06_page_fault 06-page-fault/page_fault.c)
add_executable(example_07_elf_sections 07-elf-sections/elf_sections.c)
add_executable(example_08_assembly_memory 08-assembly-memory/stack_frame.c)

set(LAB_EXAMPLE_TARGETS
    example_01_memory_layout
    example_02_stack_overflow
    example_03_heap_use_after_free
    example_04_memory_leak
    example_05_mmap
    example_06_page_fault
    example_07_elf_sections
    example_08_assembly_memory
)

foreach(target IN LISTS LAB_EXAMPLE_TARGETS)
    target_link_libraries(${target} PRIVATE lab_common)
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES TIMEOUT 60)
endforeach()

# example_04 deliberately leaks; keep an ASan build from failing the test.
set_tests_properties(example_04_memory_leak PROPERTIES
    ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0;LSAN_OPTIONS=exitcode=0")
