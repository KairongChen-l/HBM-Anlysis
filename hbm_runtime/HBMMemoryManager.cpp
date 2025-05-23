#include "HBMMemoryManager.h"
#include <memkind.h>
#include <dlfcn.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

// 兼容性定义
#ifndef MEMKIND_ERROR_MESSAGE_SIZE
#define MEMKIND_ERROR_MESSAGE_SIZE 128
#endif

namespace {

// ─────────────────── 基本类型 & 全局状态 ────────────────────
using malloc_fn = void *(*)(size_t);
using free_fn   = void  (*)(void *);
using calloc_fn = void *(*)(size_t, size_t);
using realloc_fn = void *(*)(void *, size_t);
using posix_memalign_fn = int (*)(void **, size_t, size_t);

// 原始 libc 函数指针
malloc_fn real_malloc = nullptr;
free_fn   real_free   = nullptr;
calloc_fn real_calloc = nullptr;
realloc_fn real_realloc = nullptr;
posix_memalign_fn real_posix_memalign = nullptr;

std::atomic<bool> g_ready{false};         // 静态区是否已完成构造
std::atomic<bool> g_debug{false};         // 调试开关
std::atomic<bool> g_fallback_enabled{true}; // 是否启用 DRAM 回退
std::atomic<bool> g_allow_zero_allocs{false}; // 是否允许零字节分配
std::atomic<bool> g_trace_all_frees{false}; // 跟踪所有 free 调用

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

// 线程本地递归检测（防止死锁）
thread_local int tls_recursion_depth = 0;

// 递归保护 RAII 类
class RecursionGuard {
public:
    RecursionGuard() { tls_recursion_depth++; }
    ~RecursionGuard() { tls_recursion_depth--; }
    bool is_recursive() const { return tls_recursion_depth > 1; }
};

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
    // 使用 RTLD_NEXT 避免无限递归
    void* sym = dlsym(RTLD_NEXT, name);
    if (!sym) {
        // 如果 RTLD_NEXT 失败，尝试直接从 libc 加载
        void* libc = dlopen("libc.so.6", RTLD_LAZY);
        if (libc) {
            sym = dlsym(libc, name);
            dlclose(libc);
        }
    }
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
    // 防止重复初始化
    if (g_ready.load()) return;
    
    // 解析所有 libc 函数
    real_malloc = safe_dlsym<malloc_fn>("malloc");
    real_free   = safe_dlsym<free_fn>("free");
    real_calloc = safe_dlsym<calloc_fn>("calloc");
    real_realloc = safe_dlsym<realloc_fn>("realloc");
    real_posix_memalign = safe_dlsym<posix_memalign_fn>("posix_memalign");
    
    // 检查 memkind 版本
    int version = memkind_get_version();
    int major = version / 1000000;
    int minor = (version % 1000000) / 1000;
    int patch = version % 1000;
    
    // 设置环境变量（如果需要）
    const char* debug_env = std::getenv("HBM_DEBUG");
    if (debug_env && std::strcmp(debug_env, "1") == 0) {
        g_debug.store(true);
    }
    
    const char* trace_env = std::getenv("HBM_TRACE_FREE");
    if (trace_env && std::strcmp(trace_env, "1") == 0) {
        g_trace_all_frees.store(true);
    }
    
    if (g_debug.load()) {
        std::cout << "[HBM] memkind version: " << major << "." << minor << "." << patch << "\n";
    }
    
    // 检查 HBM 可用性
    int ret = memkind_check_available(MEMKIND_HBW);
    if (ret != MEMKIND_SUCCESS) {
        char errmsg[MEMKIND_ERROR_MESSAGE_SIZE];
        memkind_error_message(ret, errmsg, sizeof(errmsg));
        if (g_debug.load()) {
            std::fprintf(stderr, "HBMMemoryManager: HBM not available: %s\n", errmsg);
        }
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
        if (!g_ptr_map.empty() && g_debug.load()) {
            std::fprintf(stderr, "[HBM] Warning: %zu allocations not freed\n", 
                        g_ptr_map.size());
            for (const auto& [ptr, info] : g_ptr_map) {
                std::fprintf(stderr, "  - %p: %zu bytes (%s)\n", 
                            ptr, info.size, to_string(info.type));
            }
        }
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
}

void hbm_memory_cleanup()
{
    std::unique_lock lk(g_map_mtx);
    g_ptr_map.clear();
}

void hbm_print_stats()
{
    std::shared_lock lk(g_map_mtx);
    size_t hbm_count = 0, hbm_bytes = 0;
    size_t dram_count = 0, dram_bytes = 0;
    
    for (const auto& [ptr, info] : g_ptr_map) {
        if (info.type == MemType::HBM_DIRECT) {
            hbm_count++;
            hbm_bytes += info.size;
        } else if (info.type != MemType::STANDARD) {
            dram_count++;
            dram_bytes += info.size;
        }
    }
    
    std::cout << "\n[HBM] Memory Statistics:\n"
              << "  Active allocations: " << g_ptr_map.size() << "\n"
              << "  HBM allocations:    " << hbm_count << " (" << hbm_bytes << " bytes)\n"
              << "  DRAM allocations:   " << dram_count << " (" << dram_bytes << " bytes)\n\n";
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
    
    RecursionGuard guard;
    if (guard.is_recursive()) return false;
    
    // 尝试获取共享锁
    std::shared_lock<std::shared_mutex> lk(g_map_mtx, std::try_to_lock);
    if (!lk.owns_lock()) return false;

    auto it = g_ptr_map.find(ptr);
    return (it != g_ptr_map.end()) && (it->second.type != MemType::STANDARD);
}

// ─────────────────── hbm_malloc ───────────────────────────────
extern "C"
void *hbm_malloc(std::size_t bytes)
{
    // 确保运行时已初始化
    if (!g_ready.load()) {
        init_runtime();
    }
    
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
    } else if (g_fallback_enabled.load()) {
        // 2) 尝试 HBM_PREFERRED（自动回退）
        ptr = memkind_malloc(MEMKIND_HBW_PREFERRED, bytes);
        if (ptr) {
            type = MemType::HBM_FALLBACK;
            kind = MEMKIND_HBW_PREFERRED;
        }
    }

    // 3) 最终回退至系统 malloc
    if (!ptr) {
        ptr  = real_malloc(bytes);
        type = MemType::STANDARD;
        kind = MEMKIND_DEFAULT;
    }

    if (ptr) {
        RecursionGuard guard;
        std::unique_lock lk(g_map_mtx);
        g_ptr_map[ptr] = AllocInfo{type, bytes, kind};
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
        RecursionGuard guard;
        std::unique_lock lk(g_map_mtx);
        auto it = g_ptr_map.find(ptr);
        if (it != g_ptr_map.end()) {
            info = it->second;
            found = true;
            g_ptr_map.erase(it);
        }
    }

    // 根据类型选择释放方式
    if (info.type == MemType::STANDARD || !found) {
        real_free(ptr);
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
    
    if (!g_ready.load()) {
        init_runtime();
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
        ret = real_posix_memalign(memptr, alignment, size);
        type = MemType::STANDARD;
        kind = MEMKIND_DEFAULT;
    }

    if (ret == 0 && *memptr) {
        RecursionGuard guard;
        std::unique_lock lk(g_map_mtx);
        g_ptr_map[*memptr] = AllocInfo{type, size, kind};
    }

    return ret;
}

extern "C"
void *hbm_calloc(std::size_t num, std::size_t size)
{
    if (!g_ready.load()) {
        init_runtime();
    }
    
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
        ptr = real_calloc(num, size);
        type = MemType::STANDARD;
        kind = MEMKIND_DEFAULT;
    }

    if (ptr) {
        RecursionGuard guard;
        std::unique_lock lk(g_map_mtx);
        g_ptr_map[ptr] = AllocInfo{type, total_size, kind};
    }

    return ptr;
}

extern "C"
void *hbm_realloc(void *ptr, std::size_t size)
{
    if (!g_ready.load()) {
        init_runtime();
    }
    
    // 特殊情况处理
    if (!ptr) return hbm_malloc(size);
    if (size == 0) {
        hbm_free(ptr);
        return nullptr;
    }

    // 查找原指针信息
    AllocInfo old_info{MemType::STANDARD, 0, MEMKIND_DEFAULT};
    {
        RecursionGuard guard;
        std::shared_lock lk(g_map_mtx);
        auto it = g_ptr_map.find(ptr);
        if (it != g_ptr_map.end()) {
            old_info = it->second;
        }
    }

    void *new_ptr = nullptr;
    
    // 根据原类型进行 realloc
    if (old_info.type == MemType::STANDARD) {
        new_ptr = real_realloc(ptr, size);
    } else {
        // memkind_realloc 支持 kind=NULL 但有性能损失
        new_ptr = memkind_realloc(old_info.kind, ptr, size);
    }

    if (new_ptr && new_ptr != ptr) {
        // 更新元数据
        RecursionGuard guard;
        std::unique_lock lk(g_map_mtx);
        g_ptr_map.erase(ptr);
        g_ptr_map[new_ptr] = AllocInfo{old_info.type, size, old_info.kind};
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
        RecursionGuard guard;
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
    
    // ② 早期 / 析构期：仅调用 libc free
    if (!g_ready.load(std::memory_order_acquire)) {
        if (!real_free) {
            real_free = reinterpret_cast<free_fn>(dlsym(RTLD_NEXT, "free"));
            if (!real_free) {
                // 最后尝试
                void* libc = dlopen("libc.so.6", RTLD_LAZY);
                if (libc) {
                    real_free = reinterpret_cast<free_fn>(dlsym(libc, "free"));
                    dlclose(libc);
                }
                if (!real_free) _Exit(127);
            }
        }
        real_free(ptr);
        return;
    }
    
    // ③ 检查递归调用
    RecursionGuard guard;
    if (guard.is_recursive()) {
        real_free(ptr);
        return;
    }
    
    // ④ 查找并释放
    AllocInfo info{MemType::STANDARD, 0, MEMKIND_DEFAULT};
    bool found = false;
    bool lock_acquired = false;
    
    // 尝试获取锁，但不要阻塞太久
    {
        std::unique_lock lk(g_map_mtx, std::try_to_lock);
        if (lk.owns_lock()) {
            lock_acquired = true;
            auto it = g_ptr_map.find(ptr);
            if (it != g_ptr_map.end()) {
                info = it->second;
                found = true;
                g_ptr_map.erase(it);
            }
        }
    }
    
    // ⑤ 执行释放
    try {
        if (found && info.type != MemType::STANDARD) {
            // HBM 内存使用 memkind_free
            memkind_free(info.kind, ptr);
            
            if (g_debug.load()) {
                std::cout << "[HBM] free(" << ptr << ") via hook"
                          << " (" << to_string(info.type) << ")\n";
            }
        } else {
            // 标准内存或未找到的指针
            // 重要：即使没有获取到锁，也要尝试释放
            // 这可能是标准内存或者是在锁竞争时分配的内存
            real_free(ptr);
            
            if (g_debug.load() && !found && lock_acquired) {
                std::cout << "[HBM] free(" << ptr << ") via hook"
                          << " (STANDARD/unknown)\n";
            }
            
            // 额外的跟踪
            if (g_trace_all_frees.load()) {
                std::cout << "[TRACE] free(" << ptr << ") -> real_free"
                          << " (found=" << found << ", lock=" << lock_acquired << ")\n";
            }
        }
    } catch (...) {
        // 如果发生任何异常，尝试使用标准 free
        if (g_debug.load()) {
            std::cerr << "[HBM] Exception in free hook, falling back to real_free\n";
        }
        real_free(ptr);
    }
}