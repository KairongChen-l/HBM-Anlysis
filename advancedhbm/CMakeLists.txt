# 添加包含目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# 收集所有源文件
file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

# 检查是否找到了源文件
if(NOT SOURCES)
    message(FATAL_ERROR "No source files found in ${CMAKE_CURRENT_SOURCE_DIR}")
endif()

# 添加选项以支持LTO分析
option(MYHBM_LTO_SUPPORT "Enable LTO support for cross-module analysis" ON)
if(MYHBM_LTO_SUPPORT)
    add_definitions(-DMYHBM_LTO_SUPPORT)
endif()

# 创建LLVM插件
add_llvm_pass_plugin(AdvancedHBMPlugin
    ${SOURCES}
)
# 链接memkind库
target_link_libraries(AdvancedHBMPlugin PRIVATE memkind)

# 添加包含线程安全所需的库
target_link_libraries(AdvancedHBMPlugin PRIVATE pthread)

# 收集HBMMemoryManager源文件
file(GLOB HBM_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/../hbm_runtime/*.cpp")

# 检查是否找到了源文件
if(NOT HBM_SOURCES)
    message(FATAL_ERROR "No source files found in ${CMAKE_CURRENT_SOURCE_DIR}/../hbm_runtime")
endif()

# 添加hbm_runtime静态库
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
    add_library(HBMMemoryManager SHARED ${HBM_SOURCES})
else()
    add_library(HBMMemoryManager SHARED ${HBM_SOURCES})
endif()

# 链接memkind库
target_link_libraries(HBMMemoryManager PRIVATE memkind pthread)
target_link_options(HBMMemoryManager PRIVATE -Wl,--wrap=malloc -Wl,--wrap=free)

# 给外部暴露hbm_runtime的头文件
target_include_directories(HBMMemoryManager PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../hbm_runtime)

# 安装 HBMMemoryManager 库和头文件，方便被分析的程序使用
# install(TARGETS HBMMemoryManager
#         ARCHIVE DESTINATION lib
#         LIBRARY DESTINATION lib)
# install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../hbm_runtime/
#         DESTINATION include/hbm_runtime
#         FILES_MATCHING PATTERN "*.h")

# 安装插件到 LLVM 的 lib/Transforms 目录
install(TARGETS AdvancedHBMPlugin
        LIBRARY DESTINATION ${LLVM_LIBRARY_DIR}/Transforms)

message(STATUS "Built HBMMemoryManager static library")

