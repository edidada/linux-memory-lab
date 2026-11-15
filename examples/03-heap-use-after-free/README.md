# 03 - 堆 Use After Free

演示一条最容易误判的规则：

> **虚拟地址有效，不代表对象生命周期有效。**

`free(p)` 通常只把 chunk 放回分配器的空闲结构（tcache/bin），
不会撤销 `[heap]` 或匿名 mmap，也不会清零用户数据。因此：

- 释放后再读 `*p`，可能仍然得到 `100`；
- 也可能读到 glibc 写入的 `fd`/`key`；
- 也可能崩溃 —— 这些都是未定义行为。

## 运行

```bash
cmake --build build --target example_03_heap_use_after_free
./build/examples/example_03_heap_use_after_free
```

子进程的结果通过退出码或信号返回，父进程只负责打印分析，始终返回 0。

## 用 ASan 得到确定性结论

```bash
cmake -S . -B build-asan -DLAB_ENABLE_ASAN=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-asan --target example_03_heap_use_after_free
ASAN_OPTIONS=detect_leaks=0 ./build-asan/examples/example_03_heap_use_after_free
```

预期看到：

```text
ERROR: AddressSanitizer: heap-use-after-free
READ of size 4 ...
```

## 延伸

- 相关源码：[`src/uaf.c`](../../src/uaf.c)
- 概念文档：[`docs/malloc-internals.md`](../../docs/malloc-internals.md)
