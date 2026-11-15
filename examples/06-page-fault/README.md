# 06 - 缺页异常（Page Fault）

用 `getrusage(RUSAGE_SELF)` 观察匿名映射的按需分页：

1. `mmap` 32 MiB，但不触碰 —— 只有 VMA，没有驻留页；
2. 每隔一页写一个字节 —— 每页触发一次 **minor fault**；
3. `madvise(MADV_DONTNEED)` 丢弃驻留页；
4. 再次触碰 —— 重新触发缺页。

## 运行

```bash
cmake --build build --target example_06_page_fault
./build/examples/example_06_page_fault
```

## 观察点

- `minflt` 明显增长，`majflt` 通常为 0（匿名页不需要磁盘 I/O）；
- 具体数值受内核、页大小、其他库活动影响，**不要写死断言**；
- 缺页次数增加不等于 bug，缺页后无法合法处理才是 `SIGSEGV`/`SIGBUS`；
- `madvise(MADV_DONTNEED)` 之后 RSS 会下降，再次访问重新分配。

## 区分故障类型

| 现象 | 含义 |
| --- | --- |
| minor fault | 分配页、COW、页缓存命中，正常 |
| major fault | 需要读磁盘/文件系统，可能变慢 |
| `SIGSEGV` | 无 VMA、权限不足、栈溢出等非法访问 |
| `SIGBUS` | 映射存在但底层对象无法提供数据（如文件被截短） |
| `SIGKILL` by OOM | 内存压力下内核杀进程，不是缺页信号 |

## 延伸

- 相关源码：[`src/mmap_sigbus.c`](../../src/mmap_sigbus.c)
- 概念文档：[`docs/page-fault.md`](../../docs/page-fault.md)
