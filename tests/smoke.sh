#!/usr/bin/env bash
#
# smoke.sh - local/CI regression pass over every program in the lab.
#
# Usage:
#   ./tests/smoke.sh [build-directory]
#
# Configures a Release (or RelWithDebInfo) build, compiles all demos and
# examples, runs CTest, then verifies that every demo log ends with a
# "FINAL: PASS" line.
#
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build_dir=${1:-"$repo_root/build"}
log_dir="$build_dir/lab-logs"

if [ "$(uname -s)" != "Linux" ]; then
    echo "error: this lab is Linux-only (needs /proc, mmap, signals)" >&2
    exit 1
fi

echo "== configure =="
cmake -S "$repo_root" -B "$build_dir" -DCMAKE_BUILD_TYPE=RelWithDebInfo

echo "== build =="
cmake --build "$build_dir" --parallel 2

echo "== ctest =="
rm -rf "$log_dir"
mkdir -p "$log_dir"
(
    cd "$build_dir"
    LAB_LOG_DIR="$log_dir" LAB_LOG_PREFIX="" \
        ctest --output-on-failure --timeout 120
)

echo "== verify demo logs =="
status=0
for prog in segfault uaf stack_overflow memory_growth mmap_sigbus addr_layout; do
    log="$log_dir/$prog.log"
    if [ ! -s "$log" ]; then
        echo "MISSING: $log" >&2
        status=1
    elif ! grep -q "FINAL: PASS" "$log"; then
        echo "NO 'FINAL: PASS': $log" >&2
        status=1
    else
        echo "ok: $prog"
    fi
done

if [ "$status" -ne 0 ]; then
    echo "smoke test FAILED" >&2
    exit 1
fi

echo "smoke test PASSED ($build_dir)"
