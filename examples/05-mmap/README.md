# 05 - mmap 与文件映射

并排演示两种映射：

| 映射 | 用途 | 写回文件 |
| --- | --- | --- |
| `MAP_SHARED` 文件映射 | 多个进程共享同一份文件内容 | 是（`msync`/`munmap` 后） |
| `MAP_PRIVATE | MAP_ANONYMOUS` | 私有、按需分配的匿名内存 | 否 |

程序用 `mkstemp` 创建一个临时文件，`ftruncate` 到两页，`mmap(MAP_SHARED)` 后写入，
再用 `pread` 从文件描述符读回，证明写操作确实落到了文件。

## 运行

```bash
cmake --build build --target example_05_mmap
./build/examples/example_05_mmap
```

## 观察点

1. 两个映射都会出现在 `/proc/self/maps`，但只有文件映射带 pathname；
2. 文件被 `unlink` 后，映射仍然有效（inode 由映射持有）；
3. `MAP_PRIVATE` 的写入不会影响文件，也不会影响其他进程；
4. 匿名映射首次写入才触发缺页（demand paging）。

## 常见故障

- 映射长度超过文件大小后访问 → 可能 `SIGBUS`；
- 另一个进程 `ftruncate` 截短文件 → 访问超出新 EOF 的页 → `SIGBUS`；
- 忘记 `munmap` → 虚拟地址空间持续增长；
- 写 `PROT_READ` 映射 → `SIGSEGV`。

## 延伸

- 相关源码：[`src/mmap_sigbus.c`](../../src/mmap_sigbus.c)
- 概念文档：[`docs/page-fault.md`](../../docs/page-fault.md)
