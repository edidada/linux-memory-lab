#!/usr/bin/env bash
#
# inspect-memory.sh - collect the virtual-memory facts of a process.
#
# Usage:
#   ./scripts/inspect-memory.sh <pid>
#   ./scripts/inspect-memory.sh -- <command> [args...]
#
# The "command" form starts the program, samples it for a moment, then stops it.
# Read-only: nothing is modified except that a command-form process is killed.
#
set -u

usage() {
    cat <<'EOF'
Usage:
  inspect-memory.sh <pid>
  inspect-memory.sh -- <command> [args...]

Prints /proc/<pid>/status, smaps_rollup, maps and the largest VMAs.
EOF
}

if [ "$#" -eq 0 ]; then
    usage
    exit 2
fi
if [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    usage
    exit 0
fi

pid=""
started=""

if [ "$1" = "--" ]; then
    shift
    if [ "$#" -eq 0 ]; then
        echo "error: no command after --" >&2
        exit 2
    fi
    "$@" &
    pid=$!
    started=1
    trap 'kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true' EXIT INT TERM
    sleep 1
else
    case "$1" in
        ''|*[!0-9]*)
            echo "error: not a PID: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
    pid=$1
fi

if [ ! -d "/proc/$pid" ]; then
    echo "error: /proc/$pid does not exist (process gone or no /proc)" >&2
    exit 1
fi

section() {
    printf '\n===== %s =====\n' "$1"
}

section "process"
ps -o pid,ppid,comm,rss,vsz,etime,nlwp -p "$pid" 2>/dev/null || true

section "/proc/$pid/status (Vm*/Rss*)"
grep -E '^(Name|Pid|PPid|Threads|VmPeak|VmSize|VmRSS|VmData|VmStk|VmExe|VmLib|VmSwap|RssAnon|RssFile|RssShmem):' \
    "/proc/$pid/status" 2>/dev/null || echo "(status unavailable)"

section "/proc/$pid/smaps_rollup"
grep -E '^(Rss|Pss|Shared_Clean|Shared_Dirty|Private_Clean|Private_Dirty|Anonymous|Swap|Locked):' \
    "/proc/$pid/smaps_rollup" 2>/dev/null || echo "(smaps_rollup unavailable)"

section "VMA count and permissions"
if [ -r "/proc/$pid/maps" ]; then
    awk 'END { printf "total VMAs: %d\n", NR }' "/proc/$pid/maps"
    awk '{ print $2 }' "/proc/$pid/maps" | sort | uniq -c | sort -nr
else
    echo "(maps unavailable)"
fi

section "largest VMAs (KiB, start-end perms path)"
if [ -r "/proc/$pid/maps" ]; then
    # POSIX awk has no strtonum(); convert the hex addresses by hand so this
    # works with mawk (Ubuntu default), gawk and busybox awk alike.
    awk '
        function hex2dec(h,   i, c, v) {
            v = 0
            h = tolower(h)
            for (i = 1; i <= length(h); i++) {
                c = substr(h, i, 1)
                v = v * 16 + index("0123456789abcdef", c) - 1
            }
            return v
        }
        {
            split($1, range, "-")
            size = (hex2dec(range[2]) - hex2dec(range[1])) / 1024
            path = ""
            for (i = 6; i <= NF; i++)
                path = path (i > 6 ? " " : "") $i
            printf "%10d  %s %-4s %s\n", size, $1, $2, (path == "" ? "[anon]" : path)
        }
    ' "/proc/$pid/maps" | sort -k1,1 -nr | sed -n '1,15p'
else
    echo "(maps unavailable)"
fi

if command -v pmap >/dev/null 2>&1; then
    section "pmap -x (first 20 lines)"
    pmap -x "$pid" 2>/dev/null | sed -n '1,20p' || true
fi

section "/proc/$pid/maps (complete)"
if [ -r "/proc/$pid/maps" ]; then
    cat "/proc/$pid/maps"
else
    echo "(maps unavailable)"
fi

if [ -n "$started" ]; then
    echo
    echo "(sampled a command started by this script; it is stopped on exit)"
fi
