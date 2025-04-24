cmake_minimum_required(VERSION 3.12)
project(MyAdvancedHBM)

# LLVM uses C++17
set(CMAKE_CXX_STANDARD 17)
add_compile_options(-fexceptions)

# Load LLVMConfig.cmake
find_package(LLVM REQUIRED CONFIG)


# Include LLVM's CMake utilities
include(AddLLVM)

# Use LLVM's preprocessor definitions, include directories, and library search paths
add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})


# Our pass lives in this subdirectory
add_subdirectory(advancedhbm)


