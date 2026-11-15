# 缺页异常与内存故障（Page Fault）

本文对应 README 第一、四、六、九章，以及
[`src/mmap_sigbus.c`](../src/mmap_sigbus.c)、
[`src/stack_overflow.c`](../src/stack_overflow.c)、
[`examples/06-page-fault/page_fault.c`](../examples/06-page-fault/page_fault.c)。

## 1. Page Fault 不一定是错误

CPU 访问虚拟地址时，MMU 查页表。如果页表项不存在、权限不足或需要内核介入，
CPU 触发 **page fault**，由内核的缺页处理程序决定：

```text
CPU LOAD/STORE
  -> MMU 查页表
     -> 页不存在 / 权限不满足 / 需要 COW
        -> page fault
           -> 内核处理
              ├─ 合法：分配页、从文件读入、复制 COW 页、扩展栈 -> 返回用户态重试
              └─ 非法：发送 SIGSEGV / SIGBUS，或 OOM 路径
```

所以看到 `minflt`/`majflt` 增长是**正常现象**，不是 bug。

## 2. 两类缺页

| 类型 | 含义 | 典型场景 |
| --- | --- | --- |
| **minor fault** | 不需要磁盘 I/O，只需分配/建立页表 | 首次触碰匿名内存、COW、页缓存命中 |
| **major fault** | 需要从磁盘/文件系统读入页面 | 首次读 mmap 文件、页被换出后重新读入 |

观察方式：

```bash
# 进程级累计值：/proc/PID/stat 的第 10、12 个字段
awk '{print "minflt=" $10, "majflt=" $12}' /proc/$$/stat

# getrusage
/usr/bin/time -v ./build/src/memory_growth
```

`examples/06-page-fault/page_fault.c` 用 `getrusage(RUSAGE_SELF)` 在
“映射但未触碰”和“逐页触碰”前后取差值，直观展示 demand paging。

## 3. 按需分页（Demand Paging）

`mmap(... MAP_ANONYMOUS)` 或大块 `malloc` 只建立 VMA，不立即分配物理页。
第一次读/写某一页时才触发缺页：

```c
char *p = mmap(NULL, 1 << 20, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
/* 此时 VmSize 增加，VmRSS 基本不变 */
p[0] = 1;          /* 触发一次 minor fault，分配并清零一页 */
```

这解释了为什么 `VmSize` 很大但 `VmRSS` 很小是正常的，
也解释了 `memory_growth` 必须先 `memset` 才能看到 RSS 增长。

## 4. 写时复制（COW）

`fork()` 之后，父子进程共享只读的物理页。任何一方写入时触发 COW：

```c
int *p = malloc(sizeof(int));
*p = 1;
pid_t pid = fork();
if (pid == 0) {
    *p = 2;        /* 触发 COW：为子进程复制一页 */
    _exit(0);
}
waitpid(pid, NULL, 0);
/* 父进程看到的 *p 仍是 1 */
```

COW 也是 minor fault 的常见来源。不要把它误判为内存泄漏或越界。

## 5. 栈增长与栈溢出

主线程栈 VMA 初始很小，访问靠近边界的地址时内核会尝试扩展：

```text
[stack]  rw-p  ...  向低地址增长
         ↓
    访问未映射的 guard 区域 -> 内核拒绝扩展 -> SIGSEGV
```

- 扩展受 `RLIMIT_STACK`（`ulimit -s`）限制；
- 内核有 stack guard gap，防止栈直接撞上相邻映射；
- 线程栈大小由 `pthread_attr_setstacksize()` 决定。

`src/stack_overflow.c` 在子进程里把 `RLIMIT_STACK` 降到 1 MiB，
再用每帧约 8 KiB 的递归快速耗尽栈，父进程通过 `waitpid()` 确认收到 `SIGSEGV`。

验证：

```bash
ulimit -s
./build/src/stack_overflow
# GDB:
gdb ./build/src/stack_overflow
(gdb) run
(gdb) bt 20
(gdb) info registers rsp rbp     # x86-64
(gdb) info registers sp fp       # ARM64
```

## 6. SIGSEGV 与 SIGBUS

两者都可能来自“访问了不该访问的地址”，但触发条件不同：

| 信号 | 典型原因 | 例子 |
| --- | --- | --- |
| `SIGSEGV` | 无 VMA、权限不足、栈溢出 | NULL 解引用；写 `r--p`；递归耗尽栈 |
| `SIGBUS` | 地址有映射，但底层对象无法提供数据 | mmap 文件被 `ftruncate` 截短；对齐错误（部分架构） |

`src/mmap_sigbus.c` 的流程：

```text
mkstemp -> ftruncate(2 pages) -> mmap(MAP_SHARED)
        -> 访问第一页（正常）
        -> ftruncate(0)          （模拟另一个进程截短文件）
        -> 访问第二页 -> SIGBUS
```

注意：某些内核/文件系统可能报告 `SIGSEGV` 而不是 `SIGBUS`，
所以该实验接受两者之一。

排查命令：

```bash
strace -f -e trace=mmap,munmap,mremap,ftruncate ./build/src/mmap_sigbus
gdb ./build/src/mmap_sigbus
(gdb) handle SIGBUS stop print
(gdb) handle SIGSEGV stop print
(gdb) run
(gdb) bt
(gdb) info proc mappings
```

## 7. 其他“内存不足”路径

- **OOM killer**：不是 page fault 信号，而是内核在内存压力下选择进程并发送 `SIGKILL`；
- **overcommit**：`malloc` 可能成功但首次触碰时失败（取决于 `vm.overcommit_memory`）；
- **cgroup 限制**：容器内 `memory.max` 触发 OOM，表现为进程被杀；
- **文件映射写入失败**：磁盘满、只读文件系统、`SIGBUS`/`SIGSEGV`。

检查：

```bash
cat /proc/PID/status | grep -E 'VmPeak|VmRSS|VmSwap'
cat /sys/fs/cgroup/memory.max 2>/dev/null
dmesg | grep -i -E 'oom|killed process'
```

## 8. 用数据而不是猜测

排查内存问题时，至少收集：

1. `VmSize / VmRSS / VmData / RssAnon / RssFile` 的时间序列；
2. `smaps_rollup` 与可疑 VMA 的 `smaps` 明细；
3. `minflt / majflt` 的增长趋势；
4. `strace` 中的 `brk/mmap/munmap/mremap/ftruncate`；
5. 故障时的 `SIGSEGV/SIGBUS`、故障地址、`bt` 与映射。

本仓库的日志把这些信息固化到 `logs/<runner>/`，可以直接 `git diff` 对比不同内核和架构。
