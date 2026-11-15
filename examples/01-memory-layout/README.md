# 01 - 内存布局（Memory Layout）

把一个 C 程序里不同类型的对象放到同一条输出里：

- 全局 `.data` / `.bss` / `.rodata`；
- 函数 `.text`；
- 局部变量（栈）；
- `malloc`（堆或匿名 mmap）；
- `mmap`（匿名映射）。

每个地址都由 `lab_log_addr_map()` 归类到 `/proc/self/maps` 中真实的 VMA。

## 运行

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target example_01_memory_layout
./build/examples/example_01_memory_layout
```

## 观察点

1. `.text` / `.rodata` 是只读映射（`r-xp` / `r--p`），全局变量是 `rw-p`；
2. 栈地址最高，堆通常较低，共享库在中间；
3. PIE + ASLR 会改变绝对地址，但相对区域与权限稳定；
4. `malloc(64 KiB)` 可能直接产生匿名映射，而不在 `[heap]` 里。

## 延伸

- 相关源码：[`src/addr_layout.c`](../../src/addr_layout.c)
- 概念文档：[`docs/virtual-address-space.md`](../../docs/virtual-address-space.md)、
  [`docs/elf-memory-layout.md`](../../docs/elf-memory-layout.md)
- CI 日志：`logs/<runner>/addr_layout.log`
