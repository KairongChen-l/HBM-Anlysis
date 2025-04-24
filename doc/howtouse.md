# 使用 MyAdvancedHBM 插件

### 使用 opt 工具加载插件

```bash
opt -load-pass-plugin=./build/advancedhbm/libMyAdvancedHBMPlugin.so -passes="my-hbm-transform" input.ll -o output.ll
```

这个命令做了几件事情：
- 加载插件
- 使用 `my-hbm-transform` pass（你在 HBMPlugin.cpp 中注册的 pass 名称）
- 处理输入的 LLVM IR 文件 (`input.ll`)
- 生成优化后的输出文件 (`output.ll`)

### 使用 clang 直接编译并应用插件

```bash
clang -fpass-plugin=./build/advancedhbm/libMyAdvancedHBMPlugin.so -Xclang -fpass=my-hbm-transform input.c -o output
```

## 命令行选项
### 阈值和评分相关参数

```bash
opt -load-pass-plugin=./build/advancedhbm/libMyAdvancedHBMPlugin.so \
    -hbm-threshold=60.0 \                # 调整 HBM 使用的得分阈值 (默认: 50.0)
    -hbm-parallel-bonus=25.0 \           # 调整并行代码的得分奖励 (默认: 20.0)
    -hbm-stream-bonus=15.0 \             # 调整流式访问的得分奖励 (默认: 10.0)
    -hbm-vector-bonus=8.0 \              # 调整向量化代码的得分奖励 (默认: 5.0)
    -hbm-access-base-read=6.0 \          # 调整基础读访问得分 (默认: 5.0)
    -hbm-access-base-write=10.0 \        # 调整基础写访问得分 (默认: 8.0)
    -hbm-bandwidth-scale=1.5 \           # 调整带宽评分缩放因子 (默认: 1.0)
    -passes="my-hbm-transform" input.ll -o output.ll
```

### 报告和性能剖析相关参数

```bash
opt -load-pass-plugin=./build/advancedhbm/libMyAdvancedHBMPlugin.so \
    -hbm-report-file=report.json \       # 指定分析报告输出文件
    -hbm-profile-file=profile.json \     # 指定外部性能剖析文件
    -passes="my-hbm-transform" input.ll -o output.ll
```

### 插桩 Pass 使用方法

如果你想使用插桩 Pass 来收集性能数据：

```bash
opt -load-pass-plugin=./build/advancedhbm/libMyAdvancedHBMPlugin.so \
    -passes="my-instrument" input.ll -o instrumented.ll
```

然后使用 clang 编译插桩后的代码：

```bash
clang instrumented.ll -o instrumented_program
```

运行插桩程序会生成性能数据，之后可以用于 HBM 优化。

## 使用示例

### 按照这个例子进行使用

1. **编译原始程序获取 LLVM IR**：
   ```bash
   clang -S -emit-llvm source.c -o source.ll
   ```

2. **使用 HBM 分析工具优化**：
   ```bash
   opt -load-pass-plugin=./build/advancedhbm/libMyAdvancedHBMPlugin.so \
       -hbm-threshold=45.0 \
       -hbm-report-file=report.json \
       -passes="my-hbm-transform" source.ll -o optimized.ll
   ```

3. **编译优化后的代码**：
   ```bash
   clang optimized.ll -o optimized_program
   ```

### 针对具体使用场景的参数调整

- **对于带宽密集型应用**：
  ```bash
  -hbm-stream-bonus=20.0 -hbm-vector-bonus=15.0 -hbm-threshold=40.0
  ```

- **对于多线程应用**：
  ```bash
  -hbm-parallel-bonus=30.0 -hbm-threshold=45.0
  ```

- **对于内存使用量较大的应用**：
  ```bash
  -hbm-threshold=60.0  # 提高阈值，减少移动到 HBM 的内存量
  ```

## 分析报告

使用 `-hbm-report-file` 参数时，插件会生成一个 JSON 格式的分析报告，包含每个内存分配点的详细分析结果，包括：
- 位置信息（文件名和行号）
- 分配大小
- 得分细节（流式访问、向量化、并行等）
- 内存访问模式分析
- 扣分项（MemorySSA 复杂度、访问混乱度、冲突）
- 多维度评分（带宽、延迟、利用率、大小效率）
- 跨函数分析结果
- 数据流生命周期分析
- 竞争分析


# MyAdvancedHBM Pass 与 Clang 集成使用指南

使用 Clang 直接应用 LLVM 插件 Pass 进行编译是一种更直接的方式。以下是如何通过 Clang 直接使用 MyAdvancedHBM Pass 的方法：

## 基本用法

### 使用 Clang 分析和转换代码

```bash
clang -fpass-plugin=/path/to/libMyAdvancedHBMPlugin.so -Xclang -fexperimental-new-pass-manager \
      -mllvm -passes=hbm-transform \
      your_program.c -o your_program
```

这个命令会在编译过程中应用 HBM 分析和转换，生成可执行文件。

### 仅分析不转换

```bash
clang -fpass-plugin=/path/to/libMyAdvancedHBMPlugin.so -Xclang -fexperimental-new-pass-manager \
      -mllvm -passes=hbm-transform \
      -mllvm -hbm-analysis-only \
      your_program.c -o your_program
```

这会执行分析但不会修改代码。

### 生成分析报告

```bash
clang -fpass-plugin=/path/to/libMyAdvancedHBMPlugin.so -Xclang -fexperimental-new-pass-manager \
      -mllvm -passes=hbm-transform \
      -mllvm -hbm-report-file=report.json \
      your_program.c -o your_program
```

## 调整参数

所有的命令行选项都需要通过 `-mllvm` 传递给 LLVM：

```bash
clang -fpass-plugin=/path/to/libMyAdvancedHBMPlugin.so -Xclang -fexperimental-new-pass-manager \
      -mllvm -passes=hbm-transform \
      -mllvm -hbm-threshold=70.0 \
      -mllvm -hbm-parallel-bonus=25.0 \
      -mllvm -hbm-stream-bonus=15.0 \
      -mllvm -hbm-report-file=report.json \
      your_program.c -o your_program
```

## 编译多个文件

对于多文件项目，您可以在每个文件上应用 Pass：

```bash
clang -fpass-plugin=/path/to/libMyAdvancedHBMPlugin.so -Xclang -fexperimental-new-pass-manager \
      -mllvm -passes=hbm-transform \
      -mllvm -hbm-report-file=report.json \
      file1.c file2.c file3.c -o program
```

## 在 Makefile 中使用

在 Makefile 中，您可以这样设置：

```makefile
CXX = clang++
CXXFLAGS = -fpass-plugin=/path/to/libMyAdvancedHBMPlugin.so -Xclang -fexperimental-new-pass-manager
LLVM_ARGS = -mllvm -passes=hbm-transform -mllvm -hbm-report-file=report.json

all: your_program

your_program: your_program.c
	$(CXX) $(CXXFLAGS) $(LLVM_ARGS) $< -o $@
```

## 在 CMake 中使用

在 CMake 项目中，您可以这样配置：

```cmake
set(HBM_PLUGIN_PATH "/path/to/libMyAdvancedHBMPlugin.so")

add_executable(your_program your_program.c)
target_compile_options(your_program PRIVATE
    -fpass-plugin=${HBM_PLUGIN_PATH}
    -Xclang -fexperimental-new-pass-manager
    -mllvm -passes=hbm-transform
    -mllvm -hbm-report-file=report.json
)
```

## 与其他编译选项结合

该 Pass 可以与其他 Clang 编译选项结合使用：

```bash
clang -O2 -g -fpass-plugin=/path/to/libMyAdvancedHBMPlugin.so -Xclang -fexperimental-new-pass-manager \
      -mllvm -passes=hbm-transform \
      -Wall -Wextra \
      your_program.c -o your_program
```

## 链接运行时库

如果您的代码被转换为使用 `hbm_malloc` 和 `hbm_free`，您需要链接相应的运行时库：

```bash
clang -fpass-plugin=/path/to/libMyAdvancedHBMPlugin.so -Xclang -fexperimental-new-pass-manager \
      -mllvm -passes=hbm-transform \
      your_program.c -o your_program \
      -L/path/to/hbm/lib -lhbm_runtime
```

## 调试插件

要调试插件的运行，您可以启用更多的日志信息：

```bash
clang -fpass-plugin=/path/to/libMyAdvancedHBMPlugin.so -Xclang -fexperimental-new-pass-manager \
      -mllvm -passes=hbm-transform \
      -mllvm -debug-only=hbm-transform \
      your_program.c -o your_program
```

通过这些方法，您可以直接在 Clang 编译流程中无缝集成 MyAdvancedHBM Pass，简化开发和测试过程。