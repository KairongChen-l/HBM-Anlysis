#pragma once
/**
 * HBMMemoryManager
 * ────────────────────────────────────────────────────────────────
 * 为 LLVM Pass 注入的 hbm_malloc/hbm_free 提供运行时实现。
 * 仅拦截 free()，无 -Wl,--wrap 需求，可通过链接顺序或 LD_PRELOAD 生效。
 *
 *  编译示例（CMake）:
 *      add_library(HBMMemoryManager SHARED HBMMemoryManager.cpp)
 *      target_link_libraries(HBMMemoryManager PUBLIC memkind dl pthread)
 *
 *  使用示例:
 *      clang ... -o program libHBMMemoryManager.so -lmemkind -ldl -lpthread
 *      # 或
 *      LD_PRELOAD=./libHBMMemoryManager.so ./program
 */

#include <cstddef>

extern "C" {

// Pass 重写调用的接口
void *hbm_malloc(std::size_t bytes);
void  hbm_free(void *ptr);

// 判断指针是否由 hbm_malloc 获得（仅查内部表）
bool  is_hbm_ptr(void *ptr);

} // extern "C"

// 辅助接口
void hbm_set_debug(bool enable); // 打开/关闭调试输出
void hbm_memory_cleanup();       // 主动清理内部元数据
