#!/usr/bin/env bash
#
# analyze-core.sh - non-interactive first pass over a core dump.
#
# Usage:
#   ./scripts/analyze-core.sh <binary> <core>
#
# Prints the crash signal, backtraces for every thread, registers, the faulting
# instruction, and the process mappings. Needs gdb; the binary should have
# debug symbols (RelWithDebInfo or Debug).
#
set -u

usage() {
    cat <<'EOF'
Usage: analyze-core.sh <binary> <core>

Runs gdb in batch mode and prints a compact crash report.
EOF
}

if [ "$#" -ne 2 ]; then
    usage
    exit 2
fi

binary=$1
core=$2

if [ ! -f "$binary" ]; then
    echo "error: binary not found: $binary" >&2
    exit 1
fi
if [ ! -r "$core" ]; then
    echo "error: core not readable: $core" >&2
    exit 1
fi
if ! command -v gdb >/dev/null 2>&1; then
    echo "error: gdb not installed" >&2
    exit 1
fi

printf '===== core configuration =====\n'
printf 'core_pattern : %s\n' "$(cat /proc/sys/kernel/core_pattern 2>/dev/null || echo unknown)"
printf 'kernel       : %s\n' "$(uname -srm 2>/dev/null || echo unknown)"
printf 'binary       : %s\n' "$binary"
printf 'core         : %s\n' "$core"

gdb -q -batch "$binary" "$core" \
    -ex 'set pagination off' \
    -ex 'set print pretty on' \
    -ex 'echo \n===== signal / stop reason =====\n' \
    -ex 'info program' \
    -ex 'p $_siginfo' \
    -ex 'echo \n===== backtrace (current thread) =====\n' \
    -ex 'bt full' \
    -ex 'echo \n===== backtrace (all threads) =====\n' \
    -ex 'thread apply all bt' \
    -ex 'echo \n===== registers =====\n' \
    -ex 'info registers' \
    -ex 'echo \n===== faulting instructions =====\n' \
    -ex 'x/8i $pc' \
    -ex 'echo \n===== mappings =====\n' \
    -ex 'info proc mappings' \
    -ex 'echo \n===== shared libraries =====\n' \
    -ex 'info sharedlibrary'

printf '\nTips:\n'
printf '  - If the core has no symbols, rebuild with -DCMAKE_BUILD_TYPE=RelWithDebInfo.\n'
printf '  - A crash inside libc often starts in the caller frame; inspect "frame N".\n'
printf '  - Compare the faulting address with "info proc mappings" permissions.\n'
printf '  - Enable cores with: ulimit -c unlimited (and check core_pattern).\n'
