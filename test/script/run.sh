#!/usr/bin/env bash
# build_with_hbm.sh —— 一键编译 + 运行 Pass + 链接
set -euo pipefail

PLUGIN=$(realpath ../build/advancedhbm/AdvancedHBMPlugin.so)

# 把所有 .c/.cpp 编译成 LLVM bitcode（.bc）
find src -type f \( -name '*.c' -o -name '*.cpp' \) | while read f; do
    clang-18 -O2 -g -c -emit-llvm "$f" -o "${f%.*}.bc"
done

# 将所有 TU bitcode 链接成一个大模块
llvm-link $(find src -name '*.bc') -o whole.bc

# 在合并后的模块上运行你的 Pass
opt-18 -load-pass-plugin="$PLUGIN" \
       -passes=hbm-transform \
       -hbm-report-file=report.json \
       -S whole.bc -o whole_opt.ll

# 把 IR 再编译回可执行文件
clang++-18 whole_opt.ll -o my_program \
        ../build/advancedhbm/libHBMMemoryManager.a \
        -lmemkind -lpthread -Wl,--wrap=malloc -Wl,--wrap=free
