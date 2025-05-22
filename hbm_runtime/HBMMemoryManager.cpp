#include "HBMMemoryManager.h"
#include <memkind.h>
#include <dlfcn.h>
#include <atomic>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

namespace {

// ─────────────────── 基本类型 & 全局状态 ────────────────────
using malloc_fn = void *(*)(size_t);
using free_fn   = void  (*)(void *);

malloc_fn real_malloc = nullptr;
free_fn   real_free   = nullptr;

std::atomic<bool> g_ready{false};   // 静态区是否已完成构造
std::atomic<bool> g_debug{false};   // 调试开关

enum class MemType : uint8_t { STANDARD, HBM_DIRECT, HBM_FALLBACK };

// 元数据表 – 读多写少
std::unordered_map<void *, MemType> g_ptr_map;
std::shared_mutex                   g_map_mtx;

inline const char *to_string(MemType t)
{
    switch (t) {
        case MemType::STANDARD:     return "STANDARD";
        case MemType::HBM_DIRECT:   return "HBM_DIRECT";
        case MemType::HBM_FALLBACK: return "HBM_PREFERRED";
    }
    return "UNKNOWN";
}

} // namespace

// ─────────────────── 运行时初始化 / 反初始化 ───────────────────
__attribute__((constructor))
static void init_runtime()
{
    real_malloc = reinterpret_cast<malloc_fn>(dlsym(RTLD_NEXT, "malloc"));
    real_free   = reinterpret_cast<free_fn>(dlsym(RTLD_NEXT, "free"));
    if (!real_malloc || !real_free) {
        std::fprintf(stderr,
                     "HBMMemoryManager: cannot resolve libc malloc/free\n");
        std::abort();
    }
    g_ready.store(true, std::memory_order_release);
}

__attribute__((destructor))
static void fini_runtime()
{
    g_ready.store(false, std::memory_order_release);
}

// ─────────────────── 公共辅助接口 ────────────────────────────
void hbm_set_debug(bool enable)  { g_debug.store(enable); }

void hbm_memory_cleanup()
{
    std::unique_lock lk(g_map_mtx);
    g_ptr_map.clear();
}

// ─────────────────── 判断指针归属（无 memkind 探测） ──────────
extern "C"
bool is_hbm_ptr(void *ptr)
{
    if (!ptr) return false;

    // 尝试获取共享锁；失败直接视为标准内存，避免死锁
    std::shared_lock<std::shared_mutex> lk(g_map_mtx, std::try_to_lock);
    if (!lk.owns_lock()) return false;

    auto it = g_ptr_map.find(ptr);
    return (it != g_ptr_map.end()) && (it->second != MemType::STANDARD);
}

// ─────────────────── hbm_malloc ───────────────────────────────
extern "C"
void *hbm_malloc(std::size_t bytes)
{
    if (bytes == 0) return nullptr;

    void    *ptr  = nullptr;
    MemType  type = MemType::STANDARD;

    // 1) 直接高带宽内存
    ptr = memkind_malloc(MEMKIND_HBW, bytes);
    if (ptr) {
        type = MemType::HBM_DIRECT;
    } else {
        // 2) HBW_PREFERRED （不足时自动回退 DRAM）
        ptr = memkind_malloc(MEMKIND_HBW_PREFERRED, bytes);
        if (ptr) type = MemType::HBM_FALLBACK;
    }

    // 3) 最终回退至系统 malloc
    if (!ptr) {
        ptr  = real_malloc(bytes);
        type = MemType::STANDARD;
    }

    if (ptr) {
        std::unique_lock lk(g_map_mtx);
        g_ptr_map.emplace(ptr, type);
    }

    if (g_debug.load()) {
        std::cout << "[HBM] malloc " << bytes << " → " << ptr
                  << " (" << to_string(type) << ")\n";
    }
    return ptr;
}

// ─────────────────── hbm_free ────────────────────────────────
extern "C"
void hbm_free(void *ptr)
{
    if (!ptr) return;

    MemType type = MemType::STANDARD;

    {
        std::unique_lock lk(g_map_mtx);
        auto it = g_ptr_map.find(ptr);
        if (it != g_ptr_map.end()) {
            type = it->second;
            g_ptr_map.erase(it);
        }
    }   // 立即释放锁，防止内部 free() 再次进入锁区

    if (type == MemType::STANDARD)
        real_free(ptr);
    else
        memkind_free(nullptr, ptr);     // kind=NULL → 自动识别

    if (g_debug.load()) {
        std::cout << "[HBM] free   " << ptr
                  << " (" << to_string(type) << ")\n";
    }
}

// ─────────────────── free() 钩子 ──────────────────────────────
extern "C" __attribute__((visibility("default")))
void free(void *ptr)
{
    // ① 早期 / 析构期：仅调用 libc free
    if (!g_ready.load(std::memory_order_acquire)) {
        if (!real_free) {
            real_free = reinterpret_cast<free_fn>(dlsym(RTLD_NEXT, "free"));
            if (!real_free) _Exit(127);
        }
        real_free(ptr);
        return;
    }

    // ② 正常运行期：依据内部表分流
    if (is_hbm_ptr(ptr))
        hbm_free(ptr);
    else
        real_free(ptr);
}
