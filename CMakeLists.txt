cmake_minimum_required(VERSION 3.16)

project(linux_memory_lab
    VERSION 1.0.0
    DESCRIPTION "Hands-on laboratory for Linux process virtual address space and memory faults"
    HOMEPAGE_URL "https://github.com/edidada/linux-memory-lab"
    LANGUAGES C)

# This lab relies on /proc, mmap, POSIX signals and RLIMIT_* : Linux only.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR
        "linux-memory-lab is Linux-only (uses /proc, mmap, signals, RLIMIT_*). "
        "Detected CMAKE_SYSTEM_NAME='${CMAKE_SYSTEM_NAME}'.")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(LAB_BUILD_DEMOS  "Build the demonstration programs" ON)
option(LAB_ENABLE_ASAN  "Build with AddressSanitizer" OFF)
option(LAB_ENABLE_UBSAN "Build with UndefinedBehaviorSanitizer" OFF)

# Warnings + flags that keep the deliberate faults real even at -O2/-O3.
add_library(lab_warnings INTERFACE)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-fno-delete-null-pointer-checks -fno-optimize-sibling-calls)
    target_compile_options(lab_warnings INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wformat)
endif()

if(LAB_ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()
if(LAB_ENABLE_UBSAN)
    add_compile_options(-fsanitize=undefined -fno-sanitize-recover=all)
    add_link_options(-fsanitize=undefined)
endif()

enable_testing()

if(LAB_BUILD_DEMOS)
    add_subdirectory(src)
endif()
