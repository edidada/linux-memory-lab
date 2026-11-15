# malloc 内部机制（glibc malloc Internals）

本文对应 README 第三章、第五章，以及
[`src/uaf.c`](../src/uaf.c)、
[`src/memory_growth.c`](../src/memory_growth.c)、
[`examples/03-heap-use-after-free/uaf.c`](../examples/03-heap-use-after-free/uaf.c)、
[`examples/04-memory-leak/leak.c`](../examples/04-memory-leak/leak.c)。

> 本文以 glibc 为模型。musl、jemalloc、tcmalloc、mimalloc 的策略不同，
> 但“虚拟地址有效 ≠ 对象生命周期有效”这一结论不变。

## 1. malloc 不直接等于一次系统调用

`malloc(size)` 的典型路径：

```text
malloc(size)
  ├─ size 很小（<= tcache/fastbin 范围） -> 从线程缓存/空闲链表复用
  ├─ size 中等 -> 从 arena 的 bin 中切分，必要时扩展 top chunk
  └─ size 很大（>= mmap threshold） -> mmap 一块独立匿名映射
```

只有“需要新内存”时才可能进入内核：

- `brk()`：移动堆顶，扩展主 arena 的 `[heap]`；
- `mmap()`：建立新的匿名 VMA，常见于大块分配或非主线程 arena；
- `munmap()`：`free()` 大块时可能归还给内核。

经典阈值（glibc，64 位默认）：

- `M_MMAP_THRESHOLD` 初始约 **128 KiB**，会动态调整，最大到 **32 MiB**；
- `M_TRIM_THRESHOLD` 控制 `free()` 是否收缩 top chunk；
- 每线程 arena 上限与 CPU 核数相关，可通过 `MALLOC_ARENA_MAX` 限制。

验证：

```bash
strace -e trace=brk,mmap,munmap ./build/src/memory_growth 2>&1 | head -30
```

你会看到：小分配走 `brk`，大分配走 `mmap`，释放后不一定 `munmap`。

## 2. chunk、对齐与元数据

glibc 在返回给用户的指针之前保存 chunk 头：

```text
+----------------+  <- chunk 起始
| prev_size      |  前一个 chunk 空闲时才有效
| size | flags   |  PREV_INUSE / IS_MMAPPED / NON_MAIN_ARENA
+----------------+  <- malloc 返回的指针
| user data ...  |
+----------------+
```

- 64 位下对齐通常是 16 字节；
- `malloc(1)` 也会占用一个最小 chunk（通常 32 字节含头）；
- 越界写会直接破坏相邻 chunk 的 `size`，在后续 `free()` 时可能触发
  `malloc(): corrupted top size` 或 `SIGABRT`。

用 `malloc_usable_size()` 可以看到实际可用字节数，但它**不是**可移植接口：

```c
#include <malloc.h>
void *p = malloc(24);
printf("usable = %zu\n", malloc_usable_size(p));
```

## 3. tcache、fastbin 与 bins

glibc 2.26+ 引入 **tcache（thread cache）**：

- 每个线程、每种大小一个单链表；
- 默认每个大小最多缓存 **7** 个 chunk；
- `free()` 优先放入 tcache，`malloc()` 优先从 tcache 取；
- tcache 中的 chunk 用户数据区会被写入 `fd`/`key`，所以 UAF 读到的值可能被改写。

其余空闲块按大小进入 fastbin、small bin、large bin、unsorted bin。
这解释了为什么：

```c
int *p = malloc(sizeof(int));
*p = 100;
free(p);
printf("%d\n", *p);   /* 常常仍然打印 100 */
```

释放后的地址依然在 `[heap]` 或匿名 VMA 内，页表权限依然是 `rw-p`，
所以普通运行**不会**必然崩溃。要稳定检测，用 ASan：

```bash
cmake -S . -B build-asan -DLAB_ENABLE_ASAN=ON
cmake --build build-asan -j
ASAN_OPTIONS=detect_leaks=0 ./build-asan/src/uaf
```

## 4. free() 之后发生了什么

`free(p)` 通常只做三件事：

1. 校验 chunk 头（可能检测到 double free / corruption）；
2. 把 chunk 放回 tcache/bin；
3. 视情况合并相邻空闲块、收缩 top chunk 或 `munmap`。

它**不会**：

- 把用户数据清零；
- 撤销整个 `[heap]` 映射；
- 让所有指向该块的指针立刻失效。

因此：

- **Use After Free**：读/写已释放对象，可能“看起来正常”，也可能崩溃；
- **Double Free**：同一块释放两次，破坏空闲链表，常在后续分配时崩溃；
- **内存泄漏**：丢失最后一个指向块的指针，进程存活期间无法再释放。

## 5. RSS 增长不等于泄漏

`src/memory_growth.c` 分配并**触碰** 8 × 16 MiB，观察 `VmSize`、`VmRSS`、`VmData`。
`free()` 之后 RSS 不一定下降，因为：

- glibc 可能保留 arena 与空闲 chunk；
- `M_TRIM_THRESHOLD` 未触发时 top chunk 不收缩；
- tcache/fastbin 仍持有空闲块；
- 其他库或线程栈也在占用匿名页。

判断泄漏的正确顺序：

1. 看趋势：长时间运行 RSS 是否**持续单调**增长，而不是一次台阶；
2. 看构成：`/proc/PID/smaps_rollup` 中 `RssAnon`、`RssFile` 的变化；
3. 看分配来源：ASan/LSan、Valgrind、heaptrack、`malloc` hook、业务指标；
4. 复现：固定请求序列，观察分配/释放是否配平。

```bash
cat /proc/$(pgrep -n memory_growth)/smaps_rollup
valgrind --leak-check=full --show-leak-kinds=all ./build/src/memory_growth
```

## 6. 常见错误与检测手段

| 错误 | 典型表现 | 推荐工具 |
| --- | --- | --- |
| heap-buffer-overflow | 破坏相邻 chunk、`free()` 时 abort | ASan |
| heap-use-after-free | 读到旧值或 `fd` 指针，偶发崩溃 | ASan |
| double-free | `free(): double free detected` | ASan、glibc 校验 |
| invalid free | `free(): invalid pointer` | ASan、GDB |
| memory leak | RSS/堆占用持续增长 | LSan、Valgrind |
| 未初始化读 | 结果随机、分支不稳定 | MSan、Valgrind |

ASan 常用环境变量：

```bash
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1:symbolize=1
LSAN_OPTIONS=suppressions=lsan.supp
```

注意：本仓库的故障演示程序会故意制造 UB，CI 的普通构建不启用 ASan；
用 ASan 构建时请只运行对应实验，并按需设置 `detect_leaks=0`。

## 7. 与其他分配器的差异

- **jemalloc / tcmalloc / mimalloc**：更积极的线程缓存、size class 与归还策略，
  RSS 曲线与 glibc 明显不同；
- **musl malloc**：实现更简单，没有 glibc 的 arena/tcache 行为；
- **自定义池**：`free()` 可能只是把块放回自己的链表，完全不接触内核。

排查线上问题时，先确认进程实际使用的是哪个分配器：

```bash
ldd ./your_program | grep -E 'c|jemalloc|tcmalloc'
grep -i malloc /proc/PID/maps
```

## 8. 最小复现模板

```c
#include <stdlib.h>
#include <string.h>

int main(void) {
    char *p = malloc(64);
    if (!p) return 1;
    memset(p, 'A', 64);
    free(p);
    /* 错误：对象已释放，地址仍可能有效 */
    return p[0] == 'A' ? 0 : 1;
}
```

编译并检测：

```bash
gcc -g -O1 -fsanitize=address -fno-omit-frame-pointer uaf.c -o uaf
./uaf
```

这与仓库里的 `src/uaf.c`、`examples/03-heap-use-after-free/uaf.c` 是同一类问题。
