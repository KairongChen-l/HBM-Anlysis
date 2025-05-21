cmake_minimum_required(VERSION 3.12)
project(MyAdvancedHBM)

# LLVM uses C++17
set(CMAKE_CXX_STANDARD 17)
add_compile_options(-fexceptions)

# Load LLVMConfig.cmake
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# 寻找 memkind 库
find_package(PkgConfig REQUIRED)
pkg_check_modules(MEMKIND REQUIRED memkind)
if(NOT MEMKIND_FOUND)
    message(FATAL_ERROR "memkind library not found")
endif()
message(STATUS "Found memkind: ${MEMKIND_LIBRARIES}")
#include_directories(/usr/include)
#link_directories(/usr/lib64)

#
# 检查是否支持 LTO
include(CheckIPOSupported)
check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_ERROR)
if(IPO_SUPPORTED)
    message(STATUS "LTO/IPO supported by compiler")
else()
    message(WARNING "LTO/IPO not supported by compiler: ${IPO_ERROR}")
endif()

# Include LLVM's CMake utilities
include(AddLLVM)

# Use LLVM's preprocessor definitions, include directories, and library search paths
add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})
include_directories(${MEMKIND_INCLUDE_DIRS})
link_directories(${MEMKIND_LIBRARY_DIRS})

# Our pass lives in this subdirectory
add_subdirectory(advancedhbm)


# 创建包含脚本，帮助用户设置环境变量
# configure_file(
#     ${CMAKE_CURRENT_SOURCE_DIR}/scripts/setup_hbm_env.sh.in
#     ${CMAKE_CURRENT_BINARY_DIR}/setup_hbm_env.sh
#     @ONLY
# )

# # 安装脚本
# install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/setup_hbm_env.sh
#         DESTINATION bin)

# # 打印配置总结
# message(STATUS "======= Advanced HBM Plugin Configuration Summary =======")
# message(STATUS "Build Type:              ${CMAKE_BUILD_TYPE}")
# message(STATUS "LLVM Version:            ${LLVM_PACKAGE_VERSION}")
# message(STATUS "Install Prefix:          ${CMAKE_INSTALL_PREFIX}")
# message(STATUS "Documentation:           ${BUILD_DOCUMENTATION}")
# message(STATUS "Testing:                 ${BUILD_TESTING}")
# message(STATUS "Tools:                   ${INSTALL_TOOLS}")
# message(STATUS "LTO/IPO Support:         ${IPO_SUPPORTED}")
# message(STATUS "====================================================")