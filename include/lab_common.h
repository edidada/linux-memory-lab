#ifndef LAB_COMMON_H
#define LAB_COMMON_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__)
#  define LAB_PRINTF(fmt, args) __attribute__((format(printf, fmt, args)))
#else
#  define LAB_PRINTF(fmt, args)
#endif

/*
 * Outcome of a deliberately faulting child process.
 * The parent inspects this instead of letting the fault kill the program,
 * so a demonstration can finish with a well-defined exit status.
 */
typedef struct lab_child_result {
    int  signaled;          /* 1: terminated by a signal                  */
    int  termsig;           /* WTERMSIG value (SIGSEGV / SIGBUS / ...)   */
    int  exited;            /* 1: exited normally via _exit()            */
    int  exitcode;          /* WEXITSTATUS value                         */
    int  stopped;           /* 1: stopped by a signal                    */
    char sigdesc[64];       /* human readable signal name                */
} lab_child_result;

/* ---- Structured logging ------------------------------------------------
 * LAB_LOG_DIR    : directory for log files (default: ./logs)
 * LAB_LOG_PREFIX : optional prefix for each generated log file name
 * Every lab_log() line is timestamped and written to BOTH stdout and the
 * per-program .log file, so CI output and git-tracked logs stay identical.
 */
int  lab_log_init(const char *prog);
void lab_log(const char *fmt, ...) LAB_PRINTF(1, 2);
void lab_log_section(const char *fmt, ...) LAB_PRINTF(1, 2);
void lab_log_child_result(const lab_child_result *r);
void lab_log_fini(void);
void lab_log_platform(void);

/* ---- /proc introspection ---------------------------------------------- */
void lab_dump_maps(void);                 /* /proc/self/maps               */
void lab_dump_status(void);               /* selected Vm* / Rss* fields     */
void lab_dump_smaps_rollup(void);         /* /proc/self/smaps_rollup       */
int  lab_read_vm(long *vmsize_kb, long *vmrss_kb, long *vmdata_kb);
void lab_log_addr_map(const char *label, const void *addr);
int  lab_describe_region(const void *addr, char *out, size_t outsz);

/* ---- Fault isolation ---------------------------------------------------
 * Runs fn(arg) inside a forked child. The child MUST NOT call lab_log():
 * it inherits a COW copy of the parent's FILE*, so it writes to stdout only.
 * Core dumps are disabled in the child to keep the workspace clean.
 */
int  lab_run_child(int (*fn)(void *), void *arg, lab_child_result *out);

#ifdef __cplusplus
}
#endif

#endif /* LAB_COMMON_H */
