#!/bin/bash
# analyze_app_revised.sh

# 设置路径
PASS_PATH="/home/dell/space/HBM-Anlysis/build/advancedhbm/AdvancedHBMPlugin.so"  # 修改为你的实际路径
REPORT_PATH="app_hbm_report.json"

# 验证插件
echo "验证 HBM 分析插件..."
if [ ! -f "$PASS_PATH" ]; then
    echo "错误: 找不到插件 $PASS_PATH"
    exit 1
fi

echo "插件文件信息:"
ls -l "$PASS_PATH"

# 清理之前的构建
echo "清理之前的构建..."
make clean

# 尝试不同的编译选项组合
echo "尝试使用lld链接器..."
make CC=clang-18 \
     CFLAGS="-Wall -Wextra -O2 -flto=thin -g" \
     LDFLAGS="-fuse-ld=lld -Wl,--load-pass-plugin=${PASS_PATH} -Wl,-mllvm,-hbm-report-file=${REPORT_PATH}"

if [ $? -ne 0 ]; then
    echo "使用lld失败，尝试另一种语法..."
    make clean
    make CC=clang-18 \
         CFLAGS="-Wall -Wextra -O2 -flto=thin -g" \
         LDFLAGS="-fuse-ld=lld -Wl,--lto-legacy-pass-manager -Wl,-plugin-opt=load=${PASS_PATH} -Wl,-plugin-opt=opt-arg=-hbm-report-file=${REPORT_PATH}"
fi

if [ $? -ne 0 ]; then
    echo "尝试更原始的方法..."
    make clean
    make CC=clang-18 \
         CFLAGS="-Wall -Wextra -O2 -flto=thin -g" \
         LDFLAGS="-Wl,-plugin=${PASS_PATH} -Wl,-plugin-opt=-hbm-report-file=${REPORT_PATH}"
fi

# 检查报告文件
if [ -f "$REPORT_PATH" ]; then
    echo "成功! HBM 分析报告已生成: $REPORT_PATH"
    echo "报告大小: $(du -h $REPORT_PATH | cut -f1)"
else
    echo "警告: 未找到报告文件。"
fi

# 如果编译成功，尝试运行应用程序
if [ -x "mem_app" ]; then
    echo "运行应用程序..."
    ./mem_app
    
    # 再次检查报告文件
    if [ -f "$REPORT_PATH" ]; then
        echo "最终报告大小: $(du -h $REPORT_PATH | cut -f1)"
    fi
else
    echo "编译后无法找到可执行文件 mem_app"
fi

echo "分析完成。"