# 08 - 汇编与内存（Assembly / Stack Frame）

C 语言的每次读写在机器码里都是对地址的 `LOAD` / `STORE`。本示例把
“看起来抽象”的栈帧直接打印出来：

- 形参 `arg` 的地址；
- 局部变量 `local` 的地址；
- 当前帧地址 `__builtin_frame_address(0)`；
- 返回地址 `__builtin_return_address(0)`；
- 递归时帧与帧之间的方向。

## 运行

```bash
cmake --build build --target example_08_assembly_memory
./build/examples/example_08_assembly_memory
```

## 观察点

- `frame - local` 通常为正：x86-64 与 ARM64 的栈都向低地址增长；
- `ret` 指向 `.text`（可执行映射）；
- 取地址会强制变量落栈，否则编译器可能把它一直放在寄存器；
- 递归时地址逐层下降，和 GDB 的 `bt` / `info registers` 对应。

## 对照反汇编与 GDB

```bash
objdump -d --no-show-raw-insn build/examples/example_08_assembly_memory | less

gdb build/examples/example_08_assembly_memory
(gdb) break inspect
(gdb) run
(gdb) info args
(gdb) info locals
(gdb) x/16gx $rsp        # x86-64
(gdb) x/16gx $sp         # ARM64
(gdb) disassemble /m inspect
```

## 延伸

- 概念文档：[`docs/elf-memory-layout.md`](../../docs/elf-memory-layout.md)、
  [`docs/virtual-address-space.md`](../../docs/virtual-address-space.md)
- 相关源码：[`src/addr_layout.c`](../../src/addr_layout.c)
