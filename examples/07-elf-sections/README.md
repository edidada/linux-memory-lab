# 07 - ELF 段与节

运行时用 `dl_iterate_phdr()` 遍历主程序的 program header，打印每个
`PT_LOAD` 的：

- 文件偏移 `p_offset`；
- 链接时虚拟地址 `p_vaddr`；
- 文件大小 `p_filesz` 与内存大小 `p_memsz`（差值通常是 `.bss`）；
- 权限 `R/W/X`；
- 加载后的真实起止地址（`dlpi_addr + p_vaddr`）。

然后把起止地址交给 `lab_log_addr_map()`，与 `/proc/self/maps` 对照。

## 运行

```bash
cmake --build build --target example_07_elf_sections
./build/examples/example_07_elf_sections
```

## 与 readelf 对照

```bash
readelf -h build/examples/example_07_elf_sections | grep Type
readelf -l build/examples/example_07_elf_sections
readelf -S build/examples/example_07_elf_sections
```

如果 `Type` 是 `DYN`（PIE），`readelf` 里的 `VirtAddr` 需要加上运行基址，
才是进程里的绝对地址；`EXEC` 则通常与链接地址一致。

## 关键结论

- **section 是链接器概念，segment 是加载器概念**；
- 内核按 `PT_LOAD` 建立 VMA，并把多个 section 合并进同一段；
- `.bss` 只存在于内存（`p_memsz - p_filesz`），不占文件空间；
- 权限由 segment 决定，写只读段会触发 `SIGSEGV`。

## 延伸

- 概念文档：[`docs/elf-memory-layout.md`](../../docs/elf-memory-layout.md)
- 相关源码：[`src/addr_layout.c`](../../src/addr_layout.c)
