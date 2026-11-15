# 调试指南（Debugging Guide）

本文把 README 第七、八章落成可执行的步骤，串联本仓库的 6 个实验程序、
`scripts/` 下的辅助脚本以及 `logs/<runner>/` 中的 CI 运行记录。

## 1. 先分类，再动手

拿到“进程崩了 / 内存涨了”的报告，先回答：

1. 进程是**崩溃**（信号、core）还是**内存增长**（RSS/堆）？
2. 是**确定性**复现还是偶发？
3. 有无 core dump、日志、监控时间线？
4. 运行环境：架构、内核、glibc、分配器、容器限制。

不同问题用不同工具：

| 问题 | 第一工具 | 补充 |
| --- | --- | --- |
| 崩溃、非法地址 | GDB + core | `info proc mappings`、`x/i $pc` |
| 堆越界 / UAF | ASan | Valgrind、GDB watchpoint |
| 内存泄漏 | LSan / Valgrind | heaptrack、分配计数 |
| 栈溢出 | GDB `bt` | `ulimit -s`、`pthread_attr_getstacksize` |
| mmap / SIGBUS | `strace` | `/proc/PID/smaps` |
| RSS 增长 | `/proc/PID/status` | `smaps_rollup`、`pmap -x` |

## 2. 编译：保留符号与调试信息

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
```

`RelWithDebInfo` 兼顾优化与 `-g`，适合线上问题的本地复现。
需要精确变量值时用 `-DCMAKE_BUILD_TYPE=Debug`，并关闭会内联/重排的优化。

ASan / UBSan 构建：

```bash
cmake -S . -B build-asan -DLAB_ENABLE_ASAN=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-asan -j
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 ./build-asan/src/uaf
```

UBSan：

```bash
cmake -S . -B build-ubsan -DLAB_ENABLE_UBSAN=ON
cmake --build build-ubsan -j
```

## 3. Core Dump

确认系统允许 core：

```bash
ulimit -c                 # 0 表示关闭
cat /proc/sys/kernel/core_pattern
```

临时开启：

```bash
ulimit -c unlimited
./build/src/segfault
ls -l core*
```

容器/CI 中往往禁用 core；本仓库的故障子进程会显式设置 `RLIMIT_CORE=0`，
避免在仓库里留下 core 文件。

分析：

```bash
gdb -q ./build/src/segfault core
(gdb) bt
(gdb) bt full
(gdb) info registers
(gdb) x/8i $pc
(gdb) info proc mappings
(gdb) p $_siginfo
```

或使用脚本：

```bash
./scripts/analyze-core.sh ./build/src/segfault core
```

systemd 系统：

```bash
coredumpctl list
coredumpctl gdb <PID>
```

## 4. GDB 常用命令

```gdb
set pagination off
set print pretty on

run
bt 20
frame 3
info args
info locals
info registers
x/16gx $rsp            # x86-64
x/16gx $sp             # ARM64
x/8i $pc
info proc mappings
info sharedlibrary
p/x pointer
watch *pointer        # 硬件/软件观察点
```

崩溃时优先看：

1. `bt`：是否在预期函数、是否有重复栈帧；
2. `$pc` 与反汇编：指令是否合法、是否跳飞；
3. 故障地址与 `info proc mappings`：地址是否有 VMA、权限是否匹配；
4. 寄存器中的指针是否明显异常（`0x0`、小整数、已释放地址）。

## 5. 运行中观察内存

```bash
PID=$(pgrep -n memory_growth)

cat /proc/$PID/status | grep -E 'VmPeak|VmSize|VmRSS|VmData|VmStk|Threads'
cat /proc/$PID/smaps_rollup
pmap -x $PID
cat /proc/$PID/maps
```

或使用脚本一次性收集：

```bash
./scripts/inspect-memory.sh $PID
./scripts/inspect-memory.sh -- ./build/src/memory_growth
```

关注：

- `VmSize` 与 `VmRSS` 的差值（保留但未驻留）；
- `RssAnon` 是否持续增长（堆/匿名映射）；
- `RssFile` 是否增长（共享库/文件映射）；
- `Threads` 是否异常增加（每线程栈与 arena）。

## 6. strace / perf / valgrind

```bash
# 内存相关系统调用
strace -f -e trace=brk,mmap,munmap,mremap,ftruncate ./build/src/memory_growth

# 附加到运行中的进程
strace -f -p $PID -e trace=brk,mmap,munmap

# 缺页与调度事件
perf stat -e page-faults,minor-faults,major-faults ./build/src/addr_layout

# 泄漏与非法访问
valgrind --leak-check=full --show-leak-kinds=all ./build/src/uaf
```

`strace` 会显著改变时序，不要用它判断性能；用它确认“是否调用了某个系统调用”。

## 7. 本仓库的 6 个实验与预期结果

| 程序 | 预期 | 失败含义 |
| --- | --- | --- |
| `segfault` | 子进程收到 `SIGSEGV`（空指针、写只读页） | 编译器把故障优化掉、平台异常 |
| `uaf` | 父进程正常退出；UAF 可能不崩溃 | 这是 UB 的正常表现，用 ASan 验证 |
| `stack_overflow` | 子进程收到 `SIGSEGV`/`SIGBUS` | `RLIMIT_STACK` 未生效或递归被优化 |
| `memory_growth` | RSS/VmData 随触碰增长，`FINAL: PASS` | 分配失败或 `/proc` 不可读 |
| `mmap_sigbus` | 子进程收到 `SIGBUS`/`SIGSEGV` | 文件系统未按预期截断 |
| `addr_layout` | 打印各对象地址与 VMA，`FINAL: PASS` | `/proc/self/maps` 不可读 |

运行全部：

```bash
cd build && ctest --output-on-failure
```

CI 使用同样的 CTest，并把每个程序的日志归档到 `logs/<runner>/`。

## 8. 用 CI 日志做跨机器对比

`logs/<runner>/` 由 GitHub Actions 生成，包含 `uname`、CPU 型号、
glibc、编译器与完整程序输出。典型用法：

```bash
# 同一程序在不同架构上的差异
git diff --no-index logs/ubuntu-24.04-x86_64/addr_layout.log \
                   logs/ubuntu-24.04-arm64/addr_layout.log

# 某一类信息
grep -E 'machine|cpu|page size|pointer bits|FINAL' logs/*/addr_layout.log

# 检查 CI 是否全部通过
gh run list --limit 5
gh run watch
```

## 9. 修复后的回归

1. 为问题写一个**最小复现**（本仓库的 `examples/` 就是模板）；
2. 在 ASan/UBSan 下跑通；
3. 加 CTest 或单元测试，防止回归；
4. 在目标架构（x86-64 / ARM64）上各跑一次；
5. 观察长时间运行下的 RSS 曲线，而不是只看一次快照。

## 10. 常见误区

- **“地址能读就说明对象有效”**：错。`free()` 后地址通常仍可读。
- **“RSS 下降才算释放”**：错。glibc 可能保留 arena。
- **“SIGSEGV 一定是空指针”**：错。权限不足、栈溢出、野指针都会。
- **“valgrind 没报就没事”**：错。它看不到部分自定义分配器与内核行为。
- **“本地复现不了就不用管”**：错。架构、内核、分配器差异正是 CI 矩阵的价值。

把复现、证据（日志/core/映射）、修复与回归串起来，才是一次完整的内存问题排查。
