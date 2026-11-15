# tests

本目录放**本地可重复**的验证脚本。CI 本身直接使用 CTest（见
`.github/workflows/ci.yml`），这里的脚本用于在提交前跑一遍同样的流程。

## smoke.sh

```bash
./tests/smoke.sh                 # 使用 ./build
./tests/smoke.sh /tmp/lab-build  # 指定构建目录
```

它会：

1. 用 CMake 配置并编译 6 个演示程序 + 8 个 `examples/` 教学示例；
2. 用 CTest 运行全部测试（故障类程序在子进程中触发信号，父进程正常退出）；
3. 检查 `segfault / uaf / stack_overflow / memory_growth / mmap_sigbus / addr_layout`
   的日志都包含 `FINAL: PASS`。

要求：Linux、CMake ≥ 3.16、GCC/Clang、`/proc` 可用。

## 与 CI 的关系

- CI 的 `build-and-test` 在 x86-64 与 ARM64、多个 Ubuntu 版本上执行同样的
  `ctest`；
- 程序日志由 CI 汇总到 `logs/<runner>/`，可用 `git diff` 对比不同机器；
- 本脚本是“提交前自检”，不修改 `logs/`（日志写到构建目录下的 `lab-logs/`）。

## 单独运行某个程序

```bash
cd build
ctest -R example_02_stack_overflow -V
ctest -R addr_layout -V
```
