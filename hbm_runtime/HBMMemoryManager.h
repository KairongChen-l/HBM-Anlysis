#pragma once
/**
 * HBMMemoryManager
 * ────────────────────────────────────────────────────────────────
 * 为 LLVM Pass 注入的 hbm_malloc/hbm_free 提供运行时实现。
 * 仅拦截 free()，无 -Wl,--wrap 需求，可通过链接顺序或 LD_PRELOAD 生效。
 *
 *  编译示例（CMake）:
 *      add_library(HBMMemoryManager SHARED HBMMemoryManager.cpp)
 *      target_link_libraries(HBMMemoryManager PUBLIC memkind numa dl pthread)
 *
 *  使用示例:
 *      clang ... -o program libHBMMemoryManager.so -lmemkind -lnuma -ldl -lpthread
 *      # 或
 *      LD_PRELOAD=./libHBMMemoryManager.so ./program
 */

#include <cstddef>
#include <cstdint>

extern "C" {

// Pass 重写调用的接口
void *hbm_malloc(std::size_t bytes);
void  hbm_free(void *ptr);

// 扩展接口：支持对齐分配
int   hbm_posix_memalign(void **memptr, std::size_t alignment, std::size_t size);
void *hbm_calloc(std::size_t num, std::size_t size);
void *hbm_realloc(void *ptr, std::size_t size);

// 判断指针是否由 hbm_malloc 获得（仅查内部表）
bool  is_hbm_ptr(void *ptr);

// 获取可用内存大小
std::size_t hbm_malloc_usable_size(void *ptr);

} // extern "C"

// 辅助接口
void hbm_set_debug(bool enable);      // 打开/关闭调试输出
void hbm_memory_cleanup();            // 主动清理内部元数据
void hbm_print_stats();               // 打印内存使用统计
bool hbm_check_availability();        // 检查 HBM 是否可用

// 配置接口
void hbm_set_fallback_enabled(bool enable);  // 是否启用 DRAM 回退
void hbm_set_zero_allocs_allowed(bool allow); // 是否允许零字节分配