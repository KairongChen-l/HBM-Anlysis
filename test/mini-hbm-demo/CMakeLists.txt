cmake_minimum_required(VERSION 3.16)

# 1️⃣ 先指定 clang，再 project()
set(CMAKE_C_COMPILER  clang-18)
set(CMAKE_CXX_COMPILER clang++-18)
project(matmul LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)

# 2️⃣ 让 clang 产生 bitcode
add_compile_options(-O2 -g -emit-llvm -c)

# 3️⃣ 路径 / 工具
set(HBM_PLUGIN "${CMAKE_SOURCE_DIR}/../../build/advancedhbm/AdvancedHBMPlugin.so")
set(HBM_RT_LIB "${CMAKE_SOURCE_DIR}/../../build/advancedhbm/libHBMMemoryManager.a")
find_library(MEMKIND_LIB memkind PATHS /usr/lib /usr/local/lib REQUIRED)
find_program(LLVM_LINK  NAMES llvm-link-18 llvm-link REQUIRED)
find_program(OPT_TOOL   NAMES opt-18       opt       REQUIRED)
# 定义 CLANG_TOOL 变量
find_program(CLANG_TOOL NAMES clang++-18   clang++   REQUIRED)

# 4️⃣ 所有 TU -> .bc
add_library(matmul_objs OBJECT
  src/main.cpp
  src/matmul.cpp)

# 5️⃣ 合并 + Pass
set(WHOLE_BC    ${CMAKE_BINARY_DIR}/whole.bc)
set(WHOLE_LL    ${CMAKE_BINARY_DIR}/whole_opt.ll)
set(REPORT_JSON ${CMAKE_BINARY_DIR}/report.json)

add_custom_command(
  OUTPUT  ${WHOLE_LL}
  COMMAND ${LLVM_LINK} $<TARGET_OBJECTS:matmul_objs> -o ${WHOLE_BC}
  COMMAND ${OPT_TOOL}
          -load-pass-plugin=${HBM_PLUGIN}
          -passes=hbm-transform
          -hbm-report-file=${REPORT_JSON}
          -S -f ${WHOLE_BC} -o ${WHOLE_LL}
  DEPENDS matmul_objs
  COMMAND_EXPAND_LISTS
  COMMENT "llvm-link → opt(hbm-transform)")

add_custom_target(run_hbm_pass ALL DEPENDS ${WHOLE_LL})

# -----------------------------------------------------
# 6. 生成最终可执行
# -----------------------------------------------------
set(MATMUL_EXE ${CMAKE_BINARY_DIR}/matmul)

add_custom_command(
  OUTPUT ${MATMUL_EXE}
  COMMAND ${CLANG_TOOL}
          ${WHOLE_LL}
          ${HBM_RT_LIB}
          ${MEMKIND_LIB}
          -lpthread
          -Wl,--wrap=malloc -Wl,--wrap=free
          -o ${MATMUL_EXE}
  DEPENDS run_hbm_pass
  COMMENT "clang++ whole_opt.ll → matmul"
  COMMAND_EXPAND_LISTS)

add_custom_target(matmul ALL DEPENDS ${MATMUL_EXE})