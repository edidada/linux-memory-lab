# 虚拟地址空间（Virtual Address Space）

本文对应 README 第一章与第九章，并把概念落到本仓库的
[`src/addr_layout.c`](../src/addr_layout.c)、
[`examples/01-memory-layout/layout.c`](../examples/01-memory-layout/layout.c)
以及 CI 生成的 `logs/<runner>/addr_layout.log` 上。

## 1. 进程看到的是虚拟地址

每个用户态进程都以为自己独占一整片地址空间。CPU 的 MMU 用页表把虚拟地址翻译成物理地址，
页表项同时携带权限位（可读 / 可写 / 可执行 / 用户态 / 已存在）。因此：

- **地址有效** 的意思是：该地址落在一个 VMA（virtual memory area）内，且页表权限允许本次访问。
- **对象有效** 是另一回事：`free()` 之后地址通常仍然有效，但对象生命周期已经结束。

这条区别贯穿本仓库的 `segfault`、`uaf` 与 `addr_layout` 三个实验。

## 2. 典型布局

以 64 位 Linux 为例，概念顺序通常是：

```
高地址
┌──────────────────────────────┐
│ 栈 stack（向下增长）           │  rw-p  [stack]
├──────────────────────────────┤
│ 共享库 / mmap 区域             │  r-xp / r--p / rw-p
├──────────────────────────────┤
│ 堆 heap（brk 向上增长）         │  rw-p  [heap]
├──────────────────────────────┤
│ .bss                          │  rw-p
│ .data                         │  rw-p
│ .rodata                       │  r--p
│ .text                         │  r-xp
├──────────────────────────────┤
│ 未映射 / 保留                  │
└──────────────────────────────┘
低地址
```

注意：

1. 这是**概念图**，不是保证的连续布局。ASLR、PIE、内核版本、CPU 架构、分配器都会移动各区域。
2. x86-64 与 ARM64 的栈都向低地址增长；堆通过 `brk` 向高地址增长，但大块 `malloc` 可能直接走 `mmap`。
3. 动态链接器、vDSO、vvar、vsyscall 等映射会插在共享库区域附近。

## 3. 用 `/proc/PID/maps` 看真实布局

字段格式：

```
start-end  perms  offset  dev  inode  pathname
```

例如：

```
55f0c0d9a000-55f0c0d9b000 r--p 00000000 08:01 123456 /path/to/addr_layout
55f0c0d9b000-55f0c0d9c000 r-xp 00001000 08:01 123456 /path/to/addr_layout
55f0c0d9c000-55f0c0d9d000 r--p 00002000 08:01 123456 /path/to/addr_layout
55f0c0d9d000-55f0c0d9e000 r--p 00002000 08:01 123456 /path/to/addr_layout
55f0c0d9e000-55f0c0d9f000 rw-p 00003000 08:01 123456 /path/to/addr_layout
55f0c0da0000-55f0c0dc1000 rw-p 00000000 00:00 0      [heap]
7f... rw-p ... [stack]
```

`perms` 的四个位置依次是 `r`、`w`、`x`、`p`（private）或 `s`（shared）。
只读映射（`r--p` / `r-xp`）上执行写操作会触发 `SIGSEGV`，这正是
[`src/segfault.c`](../src/segfault.c) 的 CASE 2。

常用命令：

```bash
cat /proc/$$/maps
pmap -x $$
grep -E 'rw-p|r-xp' /proc/$$/maps
```

本仓库的做法是**在进程内部**读取 `/proc/self/maps`，并把对象地址归类到 VMA，
见 `lab_log_addr_map()`（[`src/lab_common.c`](../src/lab_common.c)）。
这样日志里同时有“对象地址”和“它落在哪个映射里”，跨 runner 对比非常直接。

## 4. ASLR 与 PIE

- `ASLR` 由 `/proc/sys/kernel/randomize_va_space` 控制（0 关闭，1 部分，2 完整）。
- `PIE`（Position Independent Executable）让主程序也像共享库一样在随机基址加载。
- 现代发行版默认 `gcc -pie -fPIE`，所以主程序的 `.text` 每次运行地址都不同。

因此日志里的**绝对地址**在不同进程之间没有可比性，但下面这些关系仍然稳定：

- 地址宽度（64 位指针）；
- 各区域之间的相对顺序与权限；
- 对象落在哪个 VMA、该 VMA 的 pathname 是什么。

想验证 PIE 的影响：

```bash
gcc -g -O0 -no-pie -o layout-nopie examples/01-memory-layout/layout.c
gcc -g -O0 -pie    -o layout-pie   examples/01-memory-layout/layout.c
readelf -h layout-nopie | grep Type   # EXEC
readelf -h layout-pie   | grep Type   # DYN
```

## 5. 地址里通常还能看到什么

64 位进程里指向不同区域的指针，其高位往往不同。以本仓库的
[`src/addr_layout.c`](../src/addr_layout.c) 为例，一次典型输出会呈现：

```text
global .data (g_data)   0x55...  -> rw-p   /path/to/addr_layout
global .bss  (g_bss)    0x55...  -> rw-p   /path/to/addr_layout
.rodata literal         0x55...  -> r--p   /path/to/addr_layout
function .text          0x55...  -> r-xp   /path/to/addr_layout
libc printf .text       0x7f...  -> r-xp   /usr/lib/.../libc.so.6
local (stack)           0x7ff... -> rw-p   [stack]
malloc heap             0x55...  -> rw-p   [heap] 或匿名映射
mmap anonymous          0x7f...  -> rw-p   （无 pathname）
```

关键不是记住具体数值，而是理解：

- `.data/.bss/.rodata/.text` 来自同一个 ELF 文件的不同 `PT_LOAD` 段；
- 共享库在更高的 `0x7f...` 区域，由动态链接器映射；
- 栈在最高处并向下增长；
- 大块 `malloc` 可能直接产生匿名 `mmap`，看起来“不在 heap 里”。

## 6. 由地址反推故障类型

| 现象 | 可能原因 | 下一步 |
| --- | --- | --- |
| 故障地址 `0x0` 附近 | 空指针、结构体成员偏移 | `bt`、`info registers` |
| 地址在 `[stack]` 边界外 | 栈溢出、越界写 | `ulimit -s`、`bt`、栈使用量 |
| 地址在 `[heap]` 内但对象已释放 | Use After Free | ASan / LSan / Valgrind |
| 地址落在 `r--p` 或 `r-xp` 映射 | 写只读段 | `info proc mappings` |
| 地址在 mmap 文件映射内但文件被截断 | SIGBUS | `strace`、检查 `ftruncate` |
| 地址完全不在任何 VMA | 野指针、已 `munmap` | 检查释放/映射生命周期 |

`SIGSEGV` 与 `SIGBUS` 的区别不在于“有没有映射”这一句话，而在于内核无法完成本次访问的具体原因。
详见 [page-fault.md](page-fault.md)。

## 7. 在 CI 日志里对比不同机器

同一份源码在 x86-64 与 ARM64 上运行，日志会显示不同的：

- `machine`、`cpu` 行（`uname` 与 `/proc/cpuinfo`）；
- 页大小（`page size`，多数为 4096，某些 ARM64 系统可能不同）；
- 绝对地址与 VMA 分布；
- 地址宽度（本仓库 CI 全部是 64 位）。

对比方法：

```bash
grep -E 'machine|cpu|page size|pointer bits' logs/*/addr_layout.log
grep -A20 'CORE EXPERIMENT' logs/ubuntu-24.04-x86_64/addr_layout.log
grep -A20 'CORE EXPERIMENT' logs/ubuntu-24.04-arm64/addr_layout.log
```

## 8. 动手实验

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/src/addr_layout
./build/examples/example_01_memory_layout

# 或者用脚本观察一个正在运行的进程
./scripts/inspect-memory.sh $$
```

把 `addr_layout` 的输出与 `readelf -S`、`readelf -l`、`/proc/self/maps`
并列阅读，就完成了 README 第九章的第一条链路：
**C 变量 → ELF 段 → VMA → 页表 → MMU → 物理内存**。
