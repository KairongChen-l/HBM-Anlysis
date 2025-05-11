#!/bin/bash

# 设置路径 - 使用绝对路径
BUILD_DIR="$HOME/space/HBM-Anlysis/build"  # 修改为你的实际构建目录路径
PASS_PATH="${BUILD_DIR}/advancedhbm/AdvancedHBMPlugin.so"
HBM_LIB_PATH="${BUILD_DIR}/advancedhbm/libHBMMemoryManager.a"
REPORT_PATH="hpl_hbm_report.json"

# 检查插件是否存在
if [ ! -f "$PASS_PATH" ]; then
    echo "错误: 找不到插件文件 $PASS_PATH"
    echo "请确保构建路径正确，并且已成功编译插件"
    exit 1
fi

# 设置编译器
export CC=clang
export CXX=clang++

# 设置CFLAGS和CXXFLAGS
export CFLAGS="-flto=thin -O2 -g"
export CXXFLAGS="-flto=thin -O2 -g"

# 设置Linker选项，加载Pass插件
export LDFLAGS="-Wl,-plugin-opt=load=${PASS_PATH} -Wl,-plugin-opt=opt-arg=-hbm-report-file=${REPORT_PATH}"

# 检查是否只进行分析而不修改
if [ "$1" = "--analysis-only" ]; then
    export LDFLAGS="${LDFLAGS} -Wl,-plugin-opt=opt-arg=-hbm-analysis-only"
    echo "分析模式已启用（不进行代码转换）"
else
    echo "转换模式已启用（将分配移至HBM）"
    
    # 如果不是只分析模式，需要链接HBM运行时库
    export LDFLAGS="${LDFLAGS} ${HBM_LIB_PATH} -lmemkind -lpthread"
fi

echo "HBM分析环境已配置"
echo "CFLAGS: ${CFLAGS}"
echo "LDFLAGS: ${LDFLAGS}"
echo "报告将生成在: ${REPORT_PATH}"