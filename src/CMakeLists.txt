# Shared support library: structured logging, /proc parsing, signal isolation.
add_library(lab_common STATIC lab_common.c)
target_include_directories(lab_common PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(lab_common PUBLIC lab_warnings)

set(LAB_DEMOS
    segfault
    uaf
    stack_overflow
    memory_growth
    mmap_sigbus
    addr_layout
)

foreach(demo IN LISTS LAB_DEMOS)
    add_executable(${demo} ${demo}.c)
    target_link_libraries(${demo} PRIVATE lab_common)
    add_test(NAME ${demo} COMMAND ${demo})
    set_tests_properties(${demo} PROPERTIES TIMEOUT 90)
endforeach()
