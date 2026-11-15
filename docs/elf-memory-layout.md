# ELF 内存布局（ELF Memory Layout）

本文解释“可执行文件里的段/节”如何变成“进程里的 VMA”，并对应到
[`examples/07-elf-sections/elf_sections.c`](../examples/07-elf-sections/elf_sections.c)、
[`src/addr_layout.c`](../src/addr_layout.c) 与 `logs/<runner>/addr_layout.log`。

## 1. 先分清 section 与 segment

| 概念 | 出现在 | 作用 |
| --- | --- | --- |
| **section（节）** | `readelf -S` / 链接阶段 | 按内容分类：`.text`、`.rodata`、`.data`、`.bss`、`.symtab` … |
| **segment（段）** | `readelf -l` / 运行阶段 | 内核与动态链接器实际映射的单位（`PT_LOAD`） |

内核**不关心** section 名字，它只按 program header 里的 `PT_LOAD` 创建映射。
多个 section 会被合并进同一个 `PT_LOAD`，但权限必须一致。

常见关系：

```text
PT_LOAD (R E)  -> .text .plt .init .fini
PT_LOAD (R)    -> .rodata .eh_frame .dynsym .dynstr
PT_LOAD (RW)   -> .data .bss .got .dynamic
```

开启 `-z separate-code`（很多发行版默认）后，代码段与只读数据段会拆成不同的页边界映射。

## 2. 用 readelf 查看

```bash
readelf -h build/src/addr_layout | grep -E 'Type|Entry'
readelf -l build/src/addr_layout
readelf -S build/src/addr_layout
```

`readelf -l` 的关键字段：

```text
Type  Offset  VirtAddr  PhysAddr  FileSiz  MemSiz  Flg  Align
LOAD  0x0000  0x000000  0x000000  0x0012  0x0012  R   0x1000
LOAD  0x1000  0x001000  0x001000  0x0234  0x0234  R E 0x1000
LOAD  0x2000  0x002000  0x002000  0x00f0  0x00f0  R   0x1000
LOAD  0x2dd0  0x003dd0  0x003dd0  0x0218  0x0220  RW  0x1000
```

- `FileSiz` 是文件里真实存在的字节数。
- `MemSiz >= FileSiz`，差值就是 `.bss`（全零、不占文件空间）。
- `VirtAddr` 是链接时地址；PIE 运行时还要加上加载基址。

## 3. 从段到 VMA

以 PIE 为例，加载基址为 `B`，则：

```text
VMA 起始 = B + p_vaddr（按页对齐）
VMA 结束 = B + p_vaddr + p_memsz（按页对齐）
```

在进程里看到的就是：

```text
B+0x0000 ... r--p  文件偏移 0x0000   /path/to/addr_layout
B+0x1000 ... r-xp  文件偏移 0x1000   /path/to/addr_layout
B+0x2000 ... r--p  文件偏移 0x2000   /path/to/addr_layout
B+0x3000 ... rw-p  文件偏移 0x2000   /path/to/addr_layout   (例)
```

一个 `PT_LOAD` 可能需要多个 VMA，因为权限和文件偏移按页对齐后并不完全一致。
`/proc/PID/maps` 展示的是最终结果，而不是 program header 的原始行。

[`examples/07-elf-sections/elf_sections.c`](../examples/07-elf-sections/elf_sections.c)
用 `dl_iterate_phdr()` 在运行时枚举 `PT_LOAD`，把
`dlpi_addr + p_vaddr`、`p_memsz` 与权限标志打印出来，再和
`lab_log_addr_map()` 的结果对照。

## 4. 各 section 的典型内容

| section | 内容 | 权限 | 生命周期 |
| --- | --- | --- | --- |
| `.text` | 机器码 | `r-xp` | 整个进程 |
| `.rodata` | 字符串字面量、`const` 全局 | `r--p` | 整个进程 |
| `.data` | 有非零初值的全局/静态变量 | `rw-p` | 整个进程 |
| `.bss` | 零初始化的全局/静态变量 | `rw-p` | 整个进程（不占文件） |
| `.got` / `.got.plt` | 动态链接跳转表 | 通常先只读，重定位后可能 `rw-p` | 整个进程 |
| `[heap]` | `brk` 扩展出的堆 | `rw-p` | 由分配器管理 |
| `[stack]` | 主线程栈 | `rw-p` | 整个进程，按需增长 |

验证：

```bash
# 全局变量在哪？
nm -n build/src/addr_layout | grep -E 'g_data|g_bss|g_static'
# 反汇编看代码段
objdump -d --no-show-raw-insn build/src/addr_layout | head
# 只读数据
objdump -s -j .rodata build/src/addr_layout | head
```

## 5. `const` 不一定在 `.rodata`

下面几种情况要小心：

```c
const char *p = "literal";        /* 字面量在 .rodata；p 本身在 .data 或栈 */
const int table[4] = {1,2,3,4};   /* 通常是 .rodata */
static int counter;               /* .bss */
int g = 42;                       /* .data */
```

- 带 `const` 的局部变量通常仍在栈上；
- 编译器可能把只读数据合并、去重或内联成立即数；
- 未使用的 `static` 变量可能被优化掉，`nm` 里看不到；
- `-fdata-sections` / `-ffunction-sections` 会改变 section 粒度，但不改变运行时语义。

所以“源码里写了 `const`”不等于“运行时一定在只读 VMA”。以 `readelf` 和
`/proc/PID/maps` 为准。

## 6. 静态链接、动态链接与加载基址

- **静态链接**：所有代码进入可执行文件，没有 `libc.so` 映射，地址空间更简单。
- **动态链接**：`ld-linux` 先加载，再映射 `libc.so`、`libm.so` 等，最后跳转到入口。
- **PIE**：主程序也是 `ET_DYN`，有随机基址。
- **非 PIE**：主程序是 `ET_EXEC`，链接地址即运行地址（除非内核启用其它随机化）。

观察动态链接过程：

```bash
ldd build/src/addr_layout
LD_DEBUG=libs ./build/src/addr_layout 2>&1 | head -40
readelf -d build/src/addr_layout | grep NEEDED
```

## 7. 常见与 ELF 布局相关的故障

| 现象 | 可能原因 | 排查 |
| --- | --- | --- |
| 写字符串字面量崩溃 | 字面量在 `r--p` | `info proc mappings` |
| 跳到错误地址 | 函数指针被截断/被覆盖 | `bt`、`x/i $pc`、ASan |
| 启动即失败 | 缺少共享库、ABI 不匹配 | `ldd`、`LD_DEBUG`、`readelf -d` |
| 全局数组越界破坏相邻变量 | `.data/.bss` 相邻布局 | ASan、`nm -n`、watchpoint |
| `SIGBUS` | 文件映射被截断 | 见 [page-fault.md](page-fault.md) |

## 8. 一行命令串起来

```bash
# 1) 文件里的布局
readelf -lS build/src/addr_layout | less

# 2) 进程里的布局
./build/src/addr_layout

# 3) 运行时枚举 PT_LOAD
./build/examples/example_07_elf_sections
```

把三者对照，就能回答“这个变量到底在文件的哪个节、被映射成哪个 VMA、权限是什么”。
