# README

Linux 进程虚拟地址空间在实际 C/C++ 开发中，主要用于定位段错误（SIGSEGV）、内存泄漏、栈溢出、堆内存破坏、非法指针、动态库加载错误、内存映射问题以及 OOM。

它不仅是操作系统理论知识，也是 GDB 调试、Core Dump 分析、性能优化和线上故障排查的基础。

## 一、虚拟地址空间与实际故障的对应关系

典型 Linux x86-64 用户态进程：

仅为概念图。实际地址布局受 ASLR、PIE、内核、架构和分配器影响；不同映射区域并不保证按图示连续排列。

| 内存区域          | 常见故障                            | 主要排查工具                           |
| ------------- | ------------------------------- | -------------------------------- |
| Stack         | 栈溢出、栈上越界、悬空指针                   | GDB、ASan、`ulimit -s`             |
| Heap          | Use After Free、Double Free、内存泄漏 | ASan、LSan、Valgrind               |
| mmap          | 映射越界、SIGBUS、虚拟内存持续增长            | `pmap`、`strace`、`/proc/PID/maps` |
| Data / BSS    | 全局变量越界、数据竞争                     | GDB、ASan、TSan                    |
| Text / rodata | 执行非法地址、写只读内存                    | GDB、`readelf`、`objdump`          |
| 动态库映射         | 缺失符号、ABI 不兼容、加载失败               | `ldd`、`readelf`、`LD_DEBUG`       |

## 二、案例一：SIGSEGV 段错误

例如：

```
#include <stdio.h>

int main(void) {
    int *p = NULL;
    *p = 100;
    return 0;
}
```

编译：

```
gcc -g -O0 -o segfault segfault.c
gdb ./segfault
```

GDB：

```
run
bt
info registers
p/x p
info proc mappings
```

关键在于 `info proc mappings`：可以查看当前进程有哪些有效虚拟地址映射。

如果 `p = 0x0`，通常说明空指针解引用。若故障地址为其他值，就需要判断该地址是否属于某个有效映射、是否具有对应读写权限。

需要注意：SIGSEGV 不一定是访问了完全没有映射的地址。 例如写入只读映射，也可能触发 SIGSEGV。

## 三、案例二：堆内存 Use After Free

```
#include <stdlib.h>
#include <stdio.h>

int main(void) {
    int *p = malloc(sizeof(int));
    if (!p) return 1;

    *p = 100;
    free(p);

    printf("%d\n", *p);  // 错误：释放后访问
    return 0;
}
```

普通运行时，这段程序可能输出 `100`，也可能崩溃，或者出现其他不可预测行为。

因为 `free()` 不保证立即撤销对应虚拟地址映射。被释放的内存可能仍然处于进程有效的地址空间中，但已经不再属于该对象。

使用 AddressSanitizer：

```
gcc -g -O1 -fsanitize=address \
    -fno-omit-frame-pointer \
    -o uaf uaf.c

./uaf
```

典型报告：

```
ERROR: AddressSanitizer: heap-use-after-free

READ of size 4
...
freed by thread T0 here:
...
previously allocated by thread T0 here:
...
```

这说明一个重要事实：

虚拟地址有效，不代表对象生命周期有效。

GDB 能帮助查看地址和调用栈，但 ASan 更适合直接检测此类生命周期错误。

## 四、案例三：栈溢出 Stack Overflow

```
#include <stdio.h>

void recursive(int n) {
    volatile char buffer[8192];
    buffer[0] = (char)n;
    buffer[8191] = (char)n;

    recursive(n + 1);
}

int main(void) {
    recursive(0);
    return 0;
}
```

编译时可以使用：

```
gcc -g -O0 -fno-optimize-sibling-calls \
    -o stack_overflow stack_overflow.c
```

查看栈限制：

```
ulimit -s
```

查看进程的栈映射：

```
cat /proc/$$/maps | grep stack
```

GDB 中可以检查：

```
run
bt 20
info registers rsp rbp
```

如果调用栈中出现大量重复的 `recursive()`，并且栈指针接近栈映射边界，就很可能是栈溢出。

实际开发中，栈溢出也可能由以下原因导致：

- 局部数组过大。
- 无限递归。
- 线程栈配置过小。
- 深层嵌套调用。

## 五、案例四：Linux 进程内存不断增长

这是线上 C/C++ 服务最常见的排查场景之一。

假设 HTTP Server 运行几个小时后，RSS 从 100 MB 增长到 2 GB。

首先查看：

```
PID=12345

cat /proc/$PID/status | grep -E \
    'VmSize|VmRSS|VmData|VmStk|Threads'

cat /proc/$PID/smaps_rollup

pmap -x $PID
```

几个指标的区别：

| 指标      | 含义               | 排查价值       |
| ------- | ---------------- | ---------- |
| VmSize  | 进程虚拟地址空间总量       | 是否保留大量虚拟地址 |
| VmRSS   | 当前驻留在物理内存中的页面量   | 实际驻留内存规模   |
| VmData  | 数据段相关虚拟内存规模，包含堆等 | 数据映射增长情况   |
| VmStk   | 栈相关虚拟内存规模        | 栈占用情况      |
| RssAnon | 匿名驻留内存           | 堆、匿名映射等    |
| RssFile | 文件映射驻留内存         | 共享库、文件映射等  |

进一步分析：

```
cat /proc/$PID/smaps
```

这里能够看到各个映射的：

```
Size:
Rss:
Pss:
Private_Clean:
Private_Dirty:
Anonymous:
```

例如：

```
7f...-7f... rw-p 00000000 00:00 0
Size:              524288 kB
Rss:               500000 kB
Private_Dirty:     500000 kB
Anonymous:         500000 kB
```

这提示某个匿名映射占用了大量驻留内存，但不能仅凭这份输出就认定存在内存泄漏。

还可能是：

- glibc malloc 的 arena 缓存。
- 内存池保留空闲块。
- 应用缓存增长。
- 大量线程栈。
- 匿名 mmap 分配。

下一步应结合分配追踪、业务指标或 Heap Profiling 定位分配来源。

## 六、案例五：mmap 与 SIGBUS

假设一个程序使用 `mmap()` 映射文件，随后另一个进程将文件截短。

原进程继续访问映射中超出文件有效范围的页面时，可能触发 `SIGBUS`。

这与普通空指针访问的 SIGSEGV 不同。

排查：

```
strace -f -e trace=mmap,munmap,mremap,ftruncate \
    ./program
```

GDB：

```
handle SIGBUS stop print
run
bt
info proc mappings
```

这种问题常见于：

- mmap 文件数据库。
- 内存映射日志。
- 共享内存 IPC。
- 文件被其他进程 truncate。

## 七、实际工作中的排查流程

### Linux C/C++ 进程内存故障排查

1. 先确定故障类型

   SIGSEGV、SIGBUS、SIGABRT、OOM Kill、RSS 增长还是内存破坏。
2. 定位故障地址和调用栈

   GDB、Core Dump、`bt`、`info registers`。
3. 检查虚拟地址映射

   `/proc/PID/maps`、`smaps`、`pmap -x`。
4. 确定内存所属区域及权限

   栈、堆、匿名 mmap、文件映射、共享库、只读段。
5. 检查对象生命周期与分配来源

   ASan、LSan、Valgrind、heap profiler。
6. 修复并进行回归测试

   单元测试、压力测试、长时间运行监测。

## 八、建议掌握的 Linux 工具

| 工具                | 主要用途                     |
| ----------------- | ------------------------ |
| `gdb`             | 调用栈、寄存器、指针、Core Dump     |
| `readelf -l/-S`   | ELF 段、节、加载信息             |
| `objdump -d`      | 反汇编分析                    |
| `nm`              | 查看符号                     |
| `pmap -x`         | 进程虚拟内存映射                 |
| `/proc/PID/maps`  | 地址区间、权限、映射文件             |
| `/proc/PID/smaps` | 各映射的内存占用详情               |
| `strace`          | 跟踪 mmap、brk、munmap 等系统调用 |
| `perf`            | CPU 与部分内存性能分析            |
| `valgrind`        | 内存泄漏、非法访问分析              |
| `ASan / LSan`     | 内存越界、生命周期错误、泄漏检测         |

## 九、最值得研究的三个底层关联

对于需要深入 Linux、C/C++、汇编以及操作系统源码的开发者，我建议重点打通下面三条链路。

第一条：C 变量如何变成实际虚拟地址

```
C 源代码
  ↓
GCC 编译
  ↓
ELF .text / .data / .bss
  ↓
Linux ELF Loader
  ↓
虚拟内存映射 VMA
  ↓
页表 Page Table
  ↓
CPU MMU 地址转换
  ↓
物理内存
```

第二条：malloc 如何使用 Linux 虚拟内存

```
malloc(size)
  ↓
glibc malloc / allocator
  ↓
复用已有内存或申请新映射
  ↓
brk / mmap（按需）
  ↓
Linux 内存管理
  ↓
首次访问可能触发缺页异常
  ↓
分配或映射物理页
```

第三条：段错误如何产生

```
C 指针访问
  ↓
CPU 执行 LOAD / STORE
  ↓
MMU 检查页表及权限
  ↓
Page Fault 异常
  ↓
Linux 内核处理
  ↓
无法合法处理该访问
  ↓
向进程发送 SIGSEGV
  ↓
GDB / Core Dump 分析
```

其中第三条尤其重要：并不是所有 Page Fault 都是错误。 Linux 的按需分页、写时复制（COW）也会产生正常的缺页异常；只有无法合法完成的内存访问才会引发相应故障信号。

如果是为了提升真实项目的调试能力，最值得先做的实验是：编写一个 C 程序，分别创建全局变量、局部变量、malloc 内存和 mmap 映射，然后使用 GDB、readelf 和 `/proc/PID/maps` 对照实际地址。 这样可以把 C 语言、ELF、Linux 虚拟内存、汇编指令和 CPU MMU 串联起来。

---

## 十、本仓库的代码实现（与上文案例一一对应）

本仓库把上述案例落成为一套 **CMake 组织、仅 Linux、工业级** 的 C11 实验程序。每个程序都把输出**同时**写到标准输出和 `logs/` 下的日志文件，便于 `git` 追踪以及跨机器、跨 CPU 对比。

| 章节 | 概念 | 源程序 | 关键机制 |
| --- | --- | --- | --- |
| 二 | SIGSEGV 段错误 | `src/segfault.c` | 空指针（无 VMA）、写只读 VMA（权限不足） |
| 三 | 堆 Use After Free | `src/uaf.c` | 对象生命周期 vs 虚拟地址有效性、ASan |
| 四 | 栈溢出 | `src/stack_overflow.c` | 递归 + `RLIMIT_STACK`、GDB `bt` 思路 |
| 五 | RSS 持续增长 | `src/memory_growth.c` | `/proc/self/status`、`smaps_rollup`、缺页 |
| 六 | mmap 与 SIGBUS | `src/mmap_sigbus.c` | `mmap` + `ftruncate` 后越界访问 |
| 九 | 核心实验：变量到虚拟地址 | `src/addr_layout.c` | 全局/局部/malloc/mmap 与 `/proc/self/maps` 对照 |
| 公共 | 日志、`/proc` 解析、信号隔离 | `src/lab_common.c` | 结构化日志、VMA 归类、`fork` + `waitpid` |

目录结构：

```
CMakeLists.txt            # 顶层：C11、Linux-only 校验、编译选项、Sanitizer 开关
include/lab_common.h      # 公共 API
src/CMakeLists.txt        # liblab_common + 6 个可执行程序 + CTest
src/lab_common.c          # 日志 / /proc 解析 / 信号隔离实现
src/*.c                   # 6 个实验程序
.github/workflows/ci.yml  # 多 CPU / 多 Ubuntu 的 CI 与日志回写
logs/                     # 程序日志（由 CI 生成并提交，git 追踪）
```

**为什么“崩溃类”程序也能运行成功**：`segfault / uaf / stack_overflow / mmap_sigbus` 把危险访问放进 `fork()` 出的子进程；父进程用 `waitpid()` 读取 `WIFSIGNALED / WTERMSIG`，确认捕获到预期的 `SIGSEGV / SIGBUS` 后打印分析并返回 0。这样既真实触发了故障、讲解了信号与 VMA，又能让 CTest / CI 稳定通过（"目标程序运行成功"）。

### 构建与运行（Linux）

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cd build && ctest --output-on-failure

# 单独运行某个实验（日志写入 logs/<程序名>.log）
./src/addr_layout

# 用 AddressSanitizer 复现第三章的 heap-use-after-free
cmake -S . -B build-asan -DLAB_ENABLE_ASAN=ON
cmake --build build-asan -j
ASAN_OPTIONS=detect_leaks=0 ./build-asan/src/uaf
```

程序读取环境变量 `LAB_LOG_DIR`（默认 `logs/`）与 `LAB_LOG_PREFIX`（日志文件名前缀）。CMake 另提供 `-DLAB_ENABLE_ASAN=ON` / `-DLAB_ENABLE_UBSAN=ON`。

### GitHub Actions CI 矩阵

`.github/workflows/ci.yml` 在 **GitHub 支持的全部 Linux CPU 类型（x86-64 与 ARM64）** 以及 **Ubuntu 26.04（`ubuntu-latest` 的迁移目标）、24.04、22.04（已弃用，best-effort）** 上：用 CMake 编译、用 CTest 运行全部 6 个程序，把每个 runner 的程序日志作为 artifact 上传，最后由 `publish-logs` 任务汇总并提交回仓库的 `logs/<runner>/` 目录（`[skip ci]` + `GITHUB_TOKEN` 推送不会递归触发）。

因此 `logs/<runner>/` 下保存着不同内核、不同 glibc、不同 CPU 型号上的真实运行记录，可直接 `git` 追踪，并对比 `uname -m`、`/proc/cpuinfo`、指针宽度（64 位）以及各对象所在 VMA 的差异。本地开发机**只维护源码**，编译与运行全部交给 CI。