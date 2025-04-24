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