# 添加包含目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# 收集所有源文件
file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

# 检查是否找到了源文件
if(NOT SOURCES)
    message(FATAL_ERROR "No source files found in ${CMAKE_CURRENT_SOURCE_DIR}")
endif()

# 创建LLVM插件
add_llvm_pass_plugin(AdvancedHBMPlugin
    ${SOURCES}
)
# 链接memkind库
target_link_libraries(AdvancedHBMPlugin PRIVATE memkind)
### >>> 新增：编译HBMMemoryManager为静态库 <<<

# 收集HBMMemoryManager源文件
file(GLOB HBM_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/../hbm_runtime/*.cpp")

# 检查是否找到了源文件
if(NOT HBM_SOURCES)
    message(FATAL_ERROR "No source files found in ${CMAKE_CURRENT_SOURCE_DIR}/../hbm_runtime")
endif()

# 添加hbm_runtime静态库
add_library(HBMMemoryManager STATIC ${HBM_SOURCES})

# 链接memkind库
target_link_libraries(HBMMemoryManager PRIVATE memkind pthread)

# 给外部暴露hbm_runtime的头文件
target_include_directories(HBMMemoryManager PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../hbm_runtime)

message(STATUS "Built HBMMemoryManager static library")