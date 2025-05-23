#include "HBMMemoryManager.h"
#include <memkind.h>
#include <dlfcn.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

namespace {

// ─────────────────── 基本类型 & 全局状态 ────────────────────
using malloc_fn = void *(*)(size_t);
using free_fn   = void  (*)(void *);
using calloc_fn = void *(*)(size_t, size_t);
using realloc_fn = void *(*)(void *, size_t);
using posix_memalign_fn = int (*)(void **, size_t, size_t);

// 原始 libc 函数指针
struct LibcFuncs {
    malloc_fn malloc = nullptr;
    free_fn   free   = nullptr;
    calloc_fn calloc = nullptr;
    realloc_fn realloc = nullptr;
    posix_memalign_fn posix_memalign = nullptr;
} real;

std::atomic<bool> g_ready{false};         // 静态区是否已完成构造
std::atomic<bool> g_debug{false};         // 调试开关
std::atomic<bool> g_fallback_enabled{true}; // 是否启用 DRAM 回退
std::atomic<bool> g_allow_zero_allocs{false}; // 是否允许零字节分配

// 内存类型和元数据
enum class MemType : uint8_t { 
    STANDARD,      // 标准内存
    HBM_DIRECT,    // 直接 HBM 分配
    HBM_FALLBACK   // HBM 回退到 DRAM
};

struct AllocInfo {
    MemType type;
    size_t size;
    memkind_t kind;  // 记录具体的 memkind，避免 free 时查找
};

// 元数据表 – 读多写少
std::unordered_map<void *, AllocInfo> g_ptr_map;
std::shared_mutex                     g_map_mtx;

// 统计信息
struct Stats {
    std::atomic<size_t> hbm_allocs{0};
    std::atomic<size_t> hbm_bytes{0};
    std::atomic<size_t> dram_allocs{0};
    std::atomic<size_t> dram_bytes{0};
    std::atomic<size_t> fallback_count{0};
} g_stats;

// 线程本地递归检测（防止死锁）
thread_local bool tls_in_hook = false;

inline const char *to_string(MemType t)
{
    switch (t) {
        case MemType::STANDARD:     return "STANDARD";
        case MemType::HBM_DIRECT:   return "HBM_DIRECT";
        case MemType::HBM_FALLBACK: return "HBM_FALLBACK";
    }
    return "UNKNOWN";
}

// 安全的 dlsym 包装
template<typename Func>
Func safe_dlsym(const char* name) {
    void* sym = dlsym(RTLD_NEXT, name);
    if (!sym) {
        std::fprintf(stderr, "HBMMemoryManager: cannot resolve %s\n", name);
        std::abort();
    }
    return reinterpret_cast<Func>(sym);
}

} // namespace

// ─────────────────── 运行时初始化 / 反初始化 ───────────────────
__attribute__((constructor))
static void init_runtime()
{
    // 解析所有 libc 函数
    real.malloc = safe_dlsym<malloc_fn>("malloc");
    real.free   = safe_dlsym<free_fn>("free");
    real.calloc = safe_dlsym<calloc_fn>("calloc");
    real.realloc = safe_dlsym<realloc_fn>("realloc");
    real.posix_memalign = safe_dlsym<posix_memalign_fn>("posix_memalign");
    
    // 检查 HBM 可用性
    int ret = memkind_check_available(MEMKIND_HBW);
    if (ret != MEMKIND_SUCCESS) {
        char errmsg[MEMKIND_ERROR_MESSAGE_SIZE];
        memkind_error_message(ret, errmsg, sizeof(errmsg));
        std::fprintf(stderr, "HBMMemoryManager: HBM not available: %s\n", errmsg);
    }
    
    // 设置环境变量（如果需要）
    const char* debug_env = std::getenv("HBM_DEBUG");
    if (debug_env && std::strcmp(debug_env, "1") == 0) {
        g_debug.store(true);
    }
    
    g_ready.store(true, std::memory_order_release);
    
    if (g_debug.load()) {
        std::cout << "[HBM] Runtime initialized\n";
    }
}

__attribute__((destructor))
static void fini_runtime()
{
    g_ready.store(false, std::memory_order_release);
    
    // 检查内存泄漏
    {
        std::shared_lock lk(g_map_mtx);
        if (!g_ptr_map.empty()) {
            std::fprintf(stderr, "[HBM] Warning: %zu allocations not freed\n", 
                        g_ptr_map.size());
            if (g_debug.load()) {
                for (const auto& [ptr, info] : g_ptr_map) {
                    std::fprintf(stderr, "  - %p: %zu bytes (%s)\n", 
                                ptr, info.size, to_string(info.type));
                }
            }
        }
    }
    
    if (g_debug.load()) {
        hbm_print_stats();
    }
}

// ─────────────────── 公共辅助接口 ────────────────────────────
void hbm_set_debug(bool enable) { 
    g_debug.store(enable); 
}

void hbm_set_fallback_enabled(bool enable) {
    g_fallback_enabled.store(enable);
}

void hbm_set_zero_allocs_allowed(bool allow) {
    g_allow_zero_allocs.store(allow);
    // 同时设置 memkind 的行为
    memkind_set_allow_zero_allocs(MEMKIND_HBW, allow);
    if (g_fallback_enabled.load()) {
        memkind_set_allow_zero_allocs(MEMKIND_HBW_PREFERRED, allow);
    }
}

void hbm_memory_cleanup()
{
    std::unique_lock lk(g_map_mtx);
    g_ptr_map.clear();
}

void hbm_print_stats()
{
    std::cout << "\n[HBM] Memory Statistics:\n"
              << "  HBM allocations:  " << g_stats.hbm_allocs.load() << "\n"
              << "  HBM bytes:        " << g_stats.hbm_bytes.load() << "\n"
              << "  DRAM allocations: " << g_stats.dram_allocs.load() << "\n"
              << "  DRAM bytes:       " << g_stats.dram_bytes.load() << "\n"
              << "  Fallback count:   " << g_stats.fallback_count.load() << "\n\n";
}

bool hbm_check_availability()
{
    return memkind_check_available(MEMKIND_HBW) == MEMKIND_SUCCESS;
}

// ─────────────────── 判断指针归属 ──────────────────────────
extern "C"
bool is_hbm_ptr(void *ptr)
{
    if (!ptr || !g_ready.load(std::memory_order_acquire)) return false;

    // 防止递归
    if (tls_in_hook) return false;
    
    // 尝试获取共享锁；失败直接视为标准内存
    std::shared_lock<std::shared_mutex> lk(g_map_mtx, std::try_to_lock);
    if (!lk.owns_lock()) return false;

    auto it = g_ptr_map.find(ptr);
    return (it != g_ptr_map.end()) && (it->second.type != MemType::STANDARD);
}

// ─────────────────── hbm_malloc ───────────────────────────────
extern "C"
void *hbm_malloc(std::size_t bytes)
{
    // 处理零字节分配
    if (bytes == 0) {
        if (!g_allow_zero_allocs.load()) {
            return nullptr;
        }
        bytes = 1;  // 分配最小单位
    }

    void      *ptr  = nullptr;
    MemType   type = MemType::STANDARD;
    memkind_t kind = MEMKIND_DEFAULT;

    // 1) 尝试直接 HBM 分配
    ptr = memkind_malloc(MEMKIND_HBW, bytes);
    if (ptr) {
        type = MemType::HBM_DIRECT;
        kind = MEMKIND_HBW;
        g_stats.hbm_allocs.fetch_add(1);
        g_stats.hbm_bytes.fetch_add(bytes);
    } else if (g_fallback_enabled.load()) {
        // 2) 尝试 HBM_PREFERRED（自动回退）
        ptr = memkind_malloc(MEMKIND_HBW_PREFERRED, bytes);
        if (ptr) {
            type = MemType::HBM_FALLBACK;
            kind = MEMKIND_HBW_PREFERRED;
            g_stats.fallback_count.fetch_add(1);
            g_stats.dram_allocs.fetch_add(1);
            g_stats.dram_bytes.fetch_add(bytes);
        }
    }

    // 3) 最终回退至系统 malloc
    if (!ptr) {
        ptr  = real.malloc(bytes);
        type = MemType::STANDARD;
        kind = MEMKIND_DEFAULT;
        g_stats.dram_allocs.fetch_add(1);
        g_stats.dram_bytes.fetch_add(bytes);
    }

    if (ptr) {
        std::unique_lock lk(g_map_mtx);
        g_ptr_map.emplace(ptr, AllocInfo{type, bytes, kind});
    }

    if (g_debug.load()) {
        std::cout << "[HBM] malloc(" << bytes << ") → " << ptr
                  << " (" << to_string(type) << ")\n";
    }
    
    return ptr;
}

// ─────────────────── hbm_free ────────────────────────────────
extern "C"
void hbm_free(void *ptr)
{
    if (!ptr) return;

    AllocInfo info{MemType::STANDARD, 0, MEMKIND_DEFAULT};
    bool found = false;

    {
        std::unique_lock lk(g_map_mtx);
        auto it = g_ptr_map.find(ptr);
        if (it != g_ptr_map.end()) {
            info = it->second;
            found = true;
            g_ptr_map.erase(it);
            
            // 更新统计
            if (info.type == MemType::HBM_DIRECT) {
                g_stats.hbm_bytes.fetch_sub(info.size);
            } else {
                g_stats.dram_bytes.fetch_sub(info.size);
            }
        }
    }

    // 根据类型选择释放方式
    if (info.type == MemType::STANDARD || !found) {
        real.free(ptr);
    } else {
        // 使用记录的 kind，避免内部查找
        memkind_free(info.kind, ptr);
    }

    if (g_debug.load()) {
        std::cout << "[HBM] free(" << ptr << ")"
                  << " (" << to_string(info.type) << ")\n";
    }
}

// ─────────────────── 扩展接口实现 ─────────────────────────────
extern "C"
int hbm_posix_memalign(void **memptr, std::size_t alignment, std::size_t size)
{
    if (!memptr || !alignment || (alignment & (alignment - 1)) != 0) {
        return EINVAL;
    }
    
    if (size == 0) {
        if (!g_allow_zero_allocs.load()) {
            *memptr = nullptr;
            return 0;
        }
        size = 1;
    }

    int ret = MEMKIND_ERROR_UNAVAILABLE;
    MemType type = MemType::STANDARD;
    memkind_t kind = MEMKIND_DEFAULT;

    // 1) 尝试 HBM
    ret = memkind_posix_memalign(MEMKIND_HBW, memptr, alignment, size);
    if (ret == 0 && *memptr) {
        type = MemType::HBM_DIRECT;
        kind = MEMKIND_HBW;
    } else if (g_fallback_enabled.load()) {
        // 2) 尝试 HBM_PREFERRED
        ret = memkind_posix_memalign(MEMKIND_HBW_PREFERRED, memptr, alignment, size);
        if (ret == 0 && *memptr) {
            type = MemType::HBM_FALLBACK;
            kind = MEMKIND_HBW_PREFERRED;
        }
    }

    // 3) 回退到标准内存
    if (ret != 0 || !*memptr) {
        ret = real.posix_memalign(memptr, alignment, size);
        type = MemType::STANDARD;
        kind = MEMKIND_DEFAULT;
    }

    if (ret == 0 && *memptr) {
        std::unique_lock lk(g_map_mtx);
        g_ptr_map.emplace(*memptr, AllocInfo{type, size, kind});
    }

    return ret;
}

extern "C"
void *hbm_calloc(std::size_t num, std::size_t size)
{
    if (num == 0 || size == 0) {
        if (!g_allow_zero_allocs.load()) {
            return nullptr;
        }
        num = 1;
        size = 1;
    }

    void *ptr = nullptr;
    MemType type = MemType::STANDARD;
    memkind_t kind = MEMKIND_DEFAULT;
    size_t total_size = num * size;

    // 检查溢出
    if (num != 0 && total_size / num != size) {
        errno = ENOMEM;
        return nullptr;
    }

    // 1) 尝试 HBM
    ptr = memkind_calloc(MEMKIND_HBW, num, size);
    if (ptr) {
        type = MemType::HBM_DIRECT;
        kind = MEMKIND_HBW;
    } else if (g_fallback_enabled.load()) {
        // 2) 尝试 HBM_PREFERRED
        ptr = memkind_calloc(MEMKIND_HBW_PREFERRED, num, size);
        if (ptr) {
            type = MemType::HBM_FALLBACK;
            kind = MEMKIND_HBW_PREFERRED;
        }
    }

    // 3) 回退到标准内存
    if (!ptr) {
        ptr = real.calloc(num, size);
        type = MemType::STANDARD;
        kind = MEMKIND_DEFAULT;
    }

    if (ptr) {
        std::unique_lock lk(g_map_mtx);
        g_ptr_map.emplace(ptr, AllocInfo{type, total_size, kind});
    }

    return ptr;
}

extern "C"
void *hbm_realloc(void *ptr, std::size_t size)
{
    // 特殊情况处理
    if (!ptr) return hbm_malloc(size);
    if (size == 0) {
        hbm_free(ptr);
        return nullptr;
    }

    // 查找原指针信息
    AllocInfo old_info{MemType::STANDARD, 0, MEMKIND_DEFAULT};
    {
        std::shared_lock lk(g_map_mtx);
        auto it = g_ptr_map.find(ptr);
        if (it != g_ptr_map.end()) {
            old_info = it->second;
        }
    }

    void *new_ptr = nullptr;
    
    // 根据原类型进行 realloc
    if (old_info.type == MemType::STANDARD) {
        new_ptr = real.realloc(ptr, size);
    } else {
        // memkind_realloc 支持 kind=NULL 但有性能损失
        new_ptr = memkind_realloc(old_info.kind, ptr, size);
    }

    if (new_ptr && new_ptr != ptr) {
        // 更新元数据
        std::unique_lock lk(g_map_mtx);
        g_ptr_map.erase(ptr);
        g_ptr_map.emplace(new_ptr, AllocInfo{old_info.type, size, old_info.kind});
    }

    return new_ptr;
}

extern "C"
std::size_t hbm_malloc_usable_size(void *ptr)
{
    if (!ptr) return 0;

    // 查找类型
    memkind_t kind = MEMKIND_DEFAULT;
    {
        std::shared_lock lk(g_map_mtx);
        auto it = g_ptr_map.find(ptr);
        if (it != g_ptr_map.end()) {
            kind = it->second.kind;
        }
    }

    return memkind_malloc_usable_size(kind, ptr);
}

// ─────────────────── free() 钩子 ──────────────────────────────
extern "C" __attribute__((visibility("default")))
void free(void *ptr)
{
    // ① 空指针直接返回
    if (!ptr) return;
    
    // ② 防止递归调用
    if (tls_in_hook) {
        real.free(ptr);
        return;
    }
    
    // ③ 早期 / 析构期：仅调用 libc free
    if (!g_ready.load(std::memory_order_acquire)) {
        if (!real.free) {
            real.free = reinterpret_cast<free_fn>(dlsym(RTLD_NEXT, "free"));
            if (!real.free) _Exit(127);
        }
        real.free(ptr);
        return;
    }

    // ④ 设置递归保护
    tls_in_hook = true;
    
    // ⑤ 正常运行期：依据内部表分流
    if (is_hbm_ptr(ptr)) {
        hbm_free(ptr);
    } else {
        real.free(ptr);
    }
    
    tls_in_hook = false;
}