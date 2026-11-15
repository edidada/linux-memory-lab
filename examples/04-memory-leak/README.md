# 04 - 内存泄漏（Memory Leak）

分配 128 个 64 KiB 块，逐块写入（触发真实驻留），然后**丢弃全部指针**。
这就是泄漏的定义：进程还活着，但再也无法释放这些对象。

程序只做观察，不做断言：

- `VmSize` 通常在分配后上升；
- `VmRSS` 在 `memset` 触碰后上升；
- 泄漏后 RSS 不保证下降（glibc 会保留 arena / 空闲 chunk）。

## 运行

```bash
cmake --build build --target example_04_memory_leak
./build/examples/example_04_memory_leak
```

## 观察点

1. `VmSize` vs `VmRSS`：前者是虚拟地址空间，后者是实际驻留；
2. `VmData`：与堆/数据段相关的虚拟内存；
3. 一次快照不能证明泄漏，必须看**长期趋势**；
4. 进程退出后内核回收一切，所以“退出时 RSS 归零”不代表没有泄漏。

## 检测

```bash
# Valgrind
valgrind --leak-check=full --show-leak-kinds=all \
    ./build/examples/example_04_memory_leak

# ASan/LSan（本示例故意泄漏，若开启 LSan 会报告）
cmake -S . -B build-asan -DLAB_ENABLE_ASAN=ON
cmake --build build-asan --target example_04_memory_leak
LSAN_OPTIONS=exitcode=0 ./build-asan/examples/example_04_memory_leak
```

## 延伸

- 相关源码：[`src/memory_growth.c`](../../src/memory_growth.c)
- 概念文档：[`docs/malloc-internals.md`](../../docs/malloc-internals.md)、
  [`docs/debugging-guide.md`](../../docs/debugging-guide.md)
