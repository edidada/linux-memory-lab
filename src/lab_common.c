#define _GNU_SOURCE
#include "lab_common.h"

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static FILE *g_log_fp = NULL;
static char  g_log_path[1024];
static char  g_prog[128];

/* ---------------------------------------------------------------------- */
static void stamp(char *buf, size_t n)
{
    struct timespec ts;
    struct tm tm;
    char t[16];

    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tm);
    strftime(t, sizeof(t), "%H:%M:%S", &tm);
    snprintf(buf, n, "[%s.%03ld]", t, ts.tv_nsec / 1000000L);
}

/* mkdir -p for a simple POSIX path (ignoring a trailing slash). */
static int ensure_dir(const char *path)
{
    char tmp[1024];
    char *p;
    size_t len = strlen(path);

    if (len == 0 || len >= sizeof(tmp))
        return -1;
    memcpy(tmp, path, len + 1);
    while (len > 1 && tmp[len - 1] == '/')
        tmp[--len] = '\0';

    for (p = tmp + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0777) != 0 && errno != EEXIST)
                return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0777) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

int lab_log_init(const char *prog)
{
    const char *dir = getenv("LAB_LOG_DIR");
    const char *prefix = getenv("LAB_LOG_PREFIX");

    if (dir == NULL || *dir == '\0')
        dir = "logs";
    if (prefix == NULL)
        prefix = "";

    snprintf(g_prog, sizeof(g_prog), "%s", prog);

    if (ensure_dir(dir) != 0)
        fprintf(stderr, "lab_log_init: cannot create '%s': %s\n",
                dir, strerror(errno));

    snprintf(g_log_path, sizeof(g_log_path), "%s/%s%s.log", dir, prefix, prog);
    g_log_fp = fopen(g_log_path, "w");
    if (g_log_fp == NULL)
        fprintf(stderr, "lab_log_init: cannot open '%s': %s\n",
                g_log_path, strerror(errno));

    lab_log_section("linux-memory-lab :: %s", g_prog);
    return g_log_fp ? 0 : -1;
}

void lab_log(const char *fmt, ...)
{
    char stampbuf[64];
    char line[4096];
    va_list ap;

    stamp(stampbuf, sizeof(stampbuf));
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    printf("%s %s\n", stampbuf, line);
    fflush(stdout);
    if (g_log_fp != NULL) {
        fprintf(g_log_fp, "%s %s\n", stampbuf, line);
        fflush(g_log_fp);
    }
}

void lab_log_section(const char *fmt, ...)
{
    char line[1024];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    lab_log("======================================================================");
    lab_log("%s", line);
    lab_log("======================================================================");
}

void lab_log_child_result(const lab_child_result *r)
{
    if (r->signaled)
        lab_log("child terminated by signal %d (%s)", r->termsig, r->sigdesc);
    else if (r->exited)
        lab_log("child exited normally with code %d", r->exitcode);
    else if (r->stopped)
        lab_log("child was stopped by a signal");
    else
        lab_log("child terminated in an unknown way");
}

void lab_log_fini(void)
{
    lab_log("log file: %s", g_log_path);
    if (g_log_fp != NULL) {
        fclose(g_log_fp);
        g_log_fp = NULL;
    }
}

void lab_log_platform(void)
{
    struct utsname u;
    FILE *f;

    lab_log_section("ENVIRONMENT");
    if (uname(&u) == 0) {
        lab_log("sysname      : %s", u.sysname);
        lab_log("release      : %s", u.release);
        lab_log("version      : %s", u.version);
        lab_log("machine      : %s", u.machine);
        lab_log("nodename     : %s", u.nodename);
    }
    lab_log("pid          : %ld", (long)getpid());
    lab_log("pointer bits : %zu", sizeof(void *) * 8);
    lab_log("page size    : %ld bytes", sysconf(_SC_PAGESIZE));
    lab_log("compiler     : %s", __VERSION__);
#if defined(__x86_64__)
    lab_log("target arch  : x86-64 (AMD64)");
#elif defined(__aarch64__)
    lab_log("target arch  : AArch64 (ARM64)");
#elif defined(__arm__)
    lab_log("target arch  : ARM (32-bit)");
#elif defined(__i386__)
    lab_log("target arch  : i386");
#elif defined(__riscv)
    lab_log("target arch  : RISC-V (%d-bit)", __riscv_xlen);
#elif defined(__powerpc64__)
    lab_log("target arch  : POWERPC64");
#else
    lab_log("target arch  : unknown");
#endif
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    lab_log("byte order   : little-endian");
#else
    lab_log("byte order   : big-endian");
#endif

    /* One identifying CPU line, so logs from different runner CPUs differ. */
    f = fopen("/proc/cpuinfo", "r");
    if (f != NULL) {
        char line[256];
        while (fgets(line, sizeof(line), f) != NULL) {
            if (strncmp(line, "model name", 10) == 0 ||
                strncmp(line, "Hardware", 8) == 0 ||
                strncmp(line, "Processor", 9) == 0 ||
                strncmp(line, "CPU part", 8) == 0) {
                line[strcspn(line, "\r\n")] = '\0';
                lab_log("cpu          : %s", line);
                break;
            }
        }
        fclose(f);
    }
}

/* ---------------------------------------------------------------------- */
void lab_dump_maps(void)
{
    FILE *f = fopen("/proc/self/maps", "r");
    char line[512];

    if (f == NULL) {
        lab_log("cannot open /proc/self/maps: %s", strerror(errno));
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        lab_log("  %s", line);
    }
    fclose(f);
}

void lab_dump_status(void)
{
    static const char *const keys[] = {
        "VmSize:", "VmPeak:", "VmRSS:", "VmData:", "VmStk:",
        "VmSwap:", "Threads:", "RssAnon:", "RssFile:", "RssShmem:"
    };
    FILE *f = fopen("/proc/self/status", "r");
    char line[256];
    size_t i;

    if (f == NULL) {
        lab_log("cannot open /proc/self/status: %s", strerror(errno));
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        for (i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
            if (strncmp(line, keys[i], strlen(keys[i])) == 0) {
                line[strcspn(line, "\r\n")] = '\0';
                lab_log("  %s", line);
                break;
            }
        }
    }
    fclose(f);
}

void lab_dump_smaps_rollup(void)
{
    FILE *f = fopen("/proc/self/smaps_rollup", "r");
    char line[256];

    if (f == NULL) {
        lab_log("cannot open /proc/self/smaps_rollup: %s", strerror(errno));
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        lab_log("  %s", line);
    }
    fclose(f);
}

int lab_read_vm(long *vmsize_kb, long *vmrss_kb, long *vmdata_kb)
{
    FILE *f = fopen("/proc/self/status", "r");
    char line[256];
    long vs = -1, rs = -1, da = -1;

    if (f == NULL)
        return -1;
    while (fgets(line, sizeof(line), f) != NULL) {
        if (strncmp(line, "VmSize:", 7) == 0)
            sscanf(line + 7, "%ld", &vs);
        else if (strncmp(line, "VmRSS:", 6) == 0)
            sscanf(line + 6, "%ld", &rs);
        else if (strncmp(line, "VmData:", 7) == 0)
            sscanf(line + 7, "%ld", &da);
    }
    fclose(f);
    if (vmsize_kb) *vmsize_kb = vs;
    if (vmrss_kb)  *vmrss_kb  = rs;
    if (vmdata_kb) *vmdata_kb = da;
    return 0;
}

int lab_describe_region(const void *addr, char *out, size_t outsz)
{
    FILE *f = fopen("/proc/self/maps", "r");
    char line[512];
    uintptr_t target = (uintptr_t)addr;
    int found = 0;

    if (f == NULL)
        return -1;
    while (fgets(line, sizeof(line), f) != NULL) {
        unsigned long long start = 0, end = 0;
        char perms[16] = "";
        char path[256] = "";
        /* start-end perms offset dev inode pathname */
        if (sscanf(line, "%llx-%llx %15s %*x %*x:%*x %*u %255s",
                   &start, &end, perms, path) >= 3) {
            if (target >= start && target < end) {
                snprintf(out, outsz, "0x%llx-0x%llx %s %s",
                         start, end, perms, path[0] ? path : "[anon]");
                found = 1;
                break;
            }
        }
    }
    fclose(f);
    return found ? 0 : -1;
}

void lab_log_addr_map(const char *label, const void *addr)
{
    char desc[512];

    if (lab_describe_region(addr, desc, sizeof(desc)) == 0)
        lab_log("  %-22s %p -> %s", label, (void *)(uintptr_t)addr, desc);
    else
        lab_log("  %-22s %p -> (no VMA / unmapped)",
                label, (void *)(uintptr_t)addr);
}

/* ---------------------------------------------------------------------- */
int lab_run_child(int (*fn)(void *), void *arg, lab_child_result *out)
{
    pid_t pid;
    int status = 0;

    memset(out, 0, sizeof(*out));

    pid = fork();
    if (pid < 0) {
        lab_log("fork() failed: %s", strerror(errno));
        return -1;
    }

    if (pid == 0) {
        struct rlimit nocore = { 0, 0 };
        /* Never write into the parent's duplicated (COW) log stream. */
        g_log_fp = NULL;
        /* Deliberate faults must not leave core dumps behind. */
        setrlimit(RLIMIT_CORE, &nocore);
        _exit(fn(arg) & 0xff);
    }

    for (;;) {
        if (waitpid(pid, &status, 0) < 0) {
            if (errno == EINTR)
                continue;
            lab_log("waitpid() failed: %s", strerror(errno));
            return -1;
        }
        break;
    }

    if (WIFSIGNALED(status)) {
        out->signaled = 1;
        out->termsig = WTERMSIG(status);
        snprintf(out->sigdesc, sizeof(out->sigdesc), "%s",
                 strsignal(out->termsig));
    } else if (WIFEXITED(status)) {
        out->exited = 1;
        out->exitcode = WEXITSTATUS(status);
    } else if (WIFSTOPPED(status)) {
        out->stopped = 1;
    }
    return 0;
}
