# 02 - 栈溢出（Stack Overflow）

用“递归 + 降低 `RLIMIT_STACK`”制造一次**有界、可复现**的栈耗尽：

1. 父进程 `fork()` 出子进程；
2. 子进程把栈上限降到 1 MiB；
3. 每层递归占用约 16 KiB，很快越过栈 VMA；
4. 子进程收到 `SIGSEGV`，父进程通过 `waitpid()` 确认并正常退出。

## 运行

```bash
cmake --build build --target example_02_stack_overflow
./build/examples/example_02_stack_overflow
```

## 观察点

- 父进程不会崩溃，退出码为 0；
- 子进程信号是 `SIGSEGV`（个别平台可能是 `SIGBUS`）；
- `ulimit -s` 是默认栈上限，实验中在子进程内临时调低；
- 用 GDB 时 `bt 20` 会看到大量重复栈帧，`rsp`/`sp` 接近栈映射边界。

## 为什么放在子进程里

栈溢出会直接杀死进程，无法在同一进程里继续打印分析结果。
本仓库的统一做法是 `lab_run_child()` / `fork()`：危险访问放在子进程，
父进程只负责读取 `WIFSIGNALED` / `WTERMSIG`。

## 延伸

- 相关源码：[`src/stack_overflow.c`](../../src/stack_overflow.c)
- 概念文档：[`docs/page-fault.md`](../../docs/page-fault.md)、
  [`docs/debugging-guide.md`](../../docs/debugging-guide.md)
