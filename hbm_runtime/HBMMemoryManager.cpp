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

// 原始 libc 函数指针 - 使用 volatile 防止编译器优化
volatile malloc_fn real_malloc = nullptr;
volatile free_fn   real_free   = nullptr;
volatile calloc_fn real_calloc = nullptr;
volatile realloc_fn real_realloc = nullptr;
volatile posix_memalign_fn real_posix_memalign = nullptr;

std::atomic<bool> g_initialized{false};   // 运行时是否已初始化
std::atomic<bool> g_in_init{false};       // 是否正在初始化
std::atomic<bool> g_debug{false};         // 调试开关
std::atomic<bool> g_fallback_enabled{true}; // 是否启用 DRAM 回退

// 内存类型
enum class MemType : uint8_t { 
    STANDARD = 0,      // 标准内存
    HBM_DIRECT = 1,    // 直接 HBM 分配
    HBM_FALLBACK = 2   // HBM 回退到 DRAM
};

// 简化的元数据结构
struct AllocInfo {
    MemType type;
    memkind_t kind;
};

// 元数据表 - 使用普通 mutex 避免 shared_mutex 在某些架构上的问题
std::mutex g_map_mtx;
std::unordered_map<void *, AllocInfo> g_ptr_map;

// 使用原子变量替代 thread_local（避免 TLS 问题）
std::atomic<int> g_recursion_count{0};

inline const char *to_string(MemType t)
{
    switch (t) {
        case MemType::STANDARD:     return "STANDARD";
        case MemType::HBM_DIRECT:   return "HBM_DIRECT";
        case MemType::HBM_FALLBACK: return "HBM_FALLBACK";
    }
    return "UNKNOWN";
}

// 递归保护
class RecursionGuard {
    bool should_guard;
public:
    RecursionGuard() : should_guard(true) {
        g_recursion_count.fetch_add(1, std::memory_order_relaxed);
    }
    ~RecursionGuard() {
        if (should_guard) {
            g_recursion_count.fetch_sub(1, std::memory_order_relaxed);
        }
    }
    bool is_recursive() const { 
        return g_recursion_count.load(std::memory_order_relaxed) > 1; 
    }
    void disable() { 
        if (should_guard) {
            g_recursion_count.fetch_sub(1, std::memory_order_relaxed);
            should_guard = false;
        }
    }
};

// 初始化原始函数指针
static void init_libc_functions()
{
    if (real_malloc && real_free) return;
    
    // 使用 RTLD_NEXT 获取下一个符号
    real_malloc = (malloc_fn)dlsym(RTLD_NEXT, "malloc");
    real_free = (free_fn)dlsym(RTLD_NEXT, "free");
    real_calloc = (calloc_fn)dlsym(RTLD_NEXT, "calloc");
    real_realloc = (realloc_fn)dlsym(RTLD_NEXT, "realloc");
    real_posix_memalign = (posix_memalign_fn)dlsym(RTLD_NEXT, "posix_memalign");
    
    // 确保获取到有效的函数指针
    if (!real_malloc || !real_free) {
        // 尝试直接从 libc 获取
        void *libc_handle = dlopen("libc.so.6", RTLD_LAZY | RTLD_NOLOAD);
        if (!libc_handle) {
            libc_handle = dlopen("libc.so", RTLD_LAZY | RTLD_NOLOAD);
        }
        
        if (libc_handle) {
            if (!real_malloc) real_malloc = (malloc_fn)dlsym(libc_handle, "malloc");
            if (!real_free) real_free = (free_fn)dlsym(libc_handle, "free");
            if (!real_calloc) real_calloc = (calloc_fn)dlsym(libc_handle, "calloc");
            if (!real_realloc) real_realloc = (realloc_fn)dlsym(libc_handle, "realloc");
            if (!real_posix_memalign) real_posix_memalign = (posix_memalign_fn)dlsym(libc_handle, "posix_memalign");
        }
    }
    
    if (!real_malloc || !real_free) {
        std::fprintf(stderr, "HBMMemoryManager: FATAL - cannot resolve malloc/free\n");
        std::abort();
    }
}

} // namespace

// ─────────────────── 运行时初始化 ───────────────────────────
static void do_init()
{
    // 防止多线程同时初始化
    bool expected = false;
    if (!g_in_init.compare_exchange_strong(expected, true)) {
        // 其他线程正在初始化，等待
        while (!g_initialized.load(std::memory_order_acquire)) {
            // 忙等待
        }
        return;
    }
    
    // 初始化 libc 函数
    init_libc_functions();
    
    // 检查环境变量
    const char* debug_env = std::getenv("HBM_DEBUG");
    if (debug_env && (std::strcmp(debug_env, "1") == 0 || std::strcmp(debug_env, "true") == 0)) {
        g_debug.store(true);
    }
    
    // 检查 memkind 版本
    if (g_debug.load()) {
        int version = memkind_get_version();
        int major = version / 1000000;
        int minor = (version % 1000000) / 1000;
        int patch = version % 1000;
        std::fprintf(stderr, "[HBM] memkind version: %d.%d.%d\n", major, minor, patch);
    }
    
    // 检查 HBM 可用性
    int ret = memkind_check_available(MEMKIND_HBW);
    if (ret != MEMKIND_SUCCESS && g_debug.load()) {
        char errmsg[MEMKIND_ERROR_MESSAGE_SIZE];
        memkind_error_message(ret, errmsg, sizeof(errmsg));
        std::fprintf(stderr, "[HBM] Warning: HBM not available: %s\n", errmsg);
    }
    
    // 标记初始化完成
    g_initialized.store(true, std::memory_order_release);
    g_in_init.store(false);
    
    if (g_debug.load()) {
        std::fprintf(stderr, "[HBM] Runtime initialized\n");
    }
}

// 确保初始化的内联函数
inline static void ensure_init()
{
    if (!g_initialized.load(std::memory_order_acquire)) {
        do_init();
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
    // 在应用层处理
}

void hbm_memory_cleanup()
{
    std::lock_guard<std::mutex> lk(g_map_mtx);
    g_ptr_map.clear();
}

void hbm_print_stats()
{
    std::lock_guard<std::mutex> lk(g_map_mtx);
    size_t hbm_count = 0, standard_count = 0, fallback_count = 0;
    
    for (const auto& [ptr, info] : g_ptr_map) {
        switch (info.type) {
            case MemType::HBM_DIRECT: hbm_count++; break;
            case MemType::HBM_FALLBACK: fallback_count++; break;
            case MemType::STANDARD: standard_count++; break;
        }
    }
    
    std::fprintf(stderr, "\n[HBM] Memory Statistics:\n");
    std::fprintf(stderr, "  Total allocations: %zu\n", g_ptr_map.size());
    std::fprintf(stderr, "  HBM direct:        %zu\n", hbm_count);
    std::fprintf(stderr, "  HBM fallback:      %zu\n", fallback_count);
    std::fprintf(stderr, "  Standard:          %zu\n\n", standard_count);
}

bool hbm_check_availability()
{
    ensure_init();
    return memkind_check_available(MEMKIND_HBW) == MEMKIND_SUCCESS;
}

// ─────────────────── 核心内存管理接口 ─────────────────────────
extern "C"
bool is_hbm_ptr(void *ptr)
{
    if (!ptr || !g_initialized.load(std::memory_order_acquire)) return false;
    
    std::lock_guard<std::mutex> lk(g_map_mtx);
    auto it = g_ptr_map.find(ptr);
    return (it != g_ptr_map.end()) && (it->second.type != MemType::STANDARD);
}

extern "C"
void *hbm_malloc(std::size_t bytes)
{
    ensure_init();
    
    if (bytes == 0) return nullptr;
    
    void      *ptr  = nullptr;
    MemType   type = MemType::STANDARD;
    memkind_t kind = MEMKIND_DEFAULT;
    
    // 防止递归
    RecursionGuard guard;
    if (guard.is_recursive()) {
        return real_malloc(bytes);
    }

    // 1) 尝试 HBM 分配
    ptr = memkind_malloc(MEMKIND_HBW, bytes);
    if (ptr) {
        type = MemType::HBM_DIRECT;
        kind = MEMKIND_HBW;
    } else if (g_fallback_enabled.load()) {
        // 2) 尝试 HBM_PREFERRED
        ptr = memkind_malloc(MEMKIND_HBW_PREFERRED, bytes);
        if (ptr) {
            type = MemType::HBM_FALLBACK;
            kind = MEMKIND_HBW_PREFERRED;
        }
    }

    // 3) 回退到标准 malloc
    if (!ptr) {
        guard.disable();  // 避免 real_malloc 再次进入
        ptr = real_malloc(bytes);
        type = MemType::STANDARD;
        kind = MEMKIND_DEFAULT;
    }

    if (ptr) {
        std::lock_guard<std::mutex> lk(g_map_mtx);
        g_ptr_map[ptr] = AllocInfo{type, kind};
        
        if (g_debug.load()) {
            std::fprintf(stderr, "[HBM] malloc(%zu) → %p (%s)\n", 
                        bytes, ptr, to_string(type));
        }
    }
    
    return ptr;
}

extern "C"
void hbm_free(void *ptr)
{
    if (!ptr) return;
    
    AllocInfo info{MemType::STANDARD, MEMKIND_DEFAULT};
    bool found = false;

    // 查找并删除元数据
    {
        std::lock_guard<std::mutex> lk(g_map_mtx);
        auto it = g_ptr_map.find(ptr);
        if (it != g_ptr_map.end()) {
            info = it->second;
            found = true;
            g_ptr_map.erase(it);
        }
    }

    // 防止递归
    RecursionGuard guard;
    
    // 执行释放
    if (!found || info.type == MemType::STANDARD) {
        guard.disable();
        real_free(ptr);
    } else {
        memkind_free(info.kind, ptr);
    }

    if (g_debug.load() && found) {
        std::fprintf(stderr, "[HBM] free(%p) (%s)\n", ptr, to_string(info.type));
    }
}

// ─────────────────── 扩展接口 ────────────────────────────────
extern "C"
int hbm_posix_memalign(void **memptr, std::size_t alignment, std::size_t size)
{
    ensure_init();
    
    if (!memptr || !alignment || (alignment & (alignment - 1)) != 0) {
        return EINVAL;
    }
    
    *memptr = nullptr;
    if (size == 0) return 0;

    int ret = MEMKIND_ERROR_UNAVAILABLE;
    MemType type = MemType::STANDARD;
    memkind_t kind = MEMKIND_DEFAULT;
    
    RecursionGuard guard;
    if (guard.is_recursive()) {
        return real_posix_memalign(memptr, alignment, size);
    }

    // 尝试 HBM
    ret = memkind_posix_memalign(MEMKIND_HBW, memptr, alignment, size);
    if (ret == 0 && *memptr) {
        type = MemType::HBM_DIRECT;
        kind = MEMKIND_HBW;
    } else if (g_fallback_enabled.load()) {
        ret = memkind_posix_memalign(MEMKIND_HBW_PREFERRED, memptr, alignment, size);
        if (ret == 0 && *memptr) {
            type = MemType::HBM_FALLBACK;
            kind = MEMKIND_HBW_PREFERRED;
        }
    }

    // 回退到标准内存
    if (ret != 0 || !*memptr) {
        guard.disable();
        ret = real_posix_memalign(memptr, alignment, size);
        type = MemType::STANDARD;
        kind = MEMKIND_DEFAULT;
    }

    if (ret == 0 && *memptr) {
        std::lock_guard<std::mutex> lk(g_map_mtx);
        g_ptr_map[*memptr] = AllocInfo{type, kind};
    }

    return ret;
}

extern "C"
void *hbm_calloc(std::size_t num, std::size_t size)
{
    ensure_init();
    
    if (num == 0 || size == 0) return nullptr;
    
    // 检查溢出
    size_t total = num * size;
    if (num != 0 && total / num != size) {
        errno = ENOMEM;
        return nullptr;
    }

    void *ptr = nullptr;
    MemType type = MemType::STANDARD;
    memkind_t kind = MEMKIND_DEFAULT;
    
    RecursionGuard guard;
    if (guard.is_recursive()) {
        return real_calloc(num, size);
    }

    // 尝试 HBM
    ptr = memkind_calloc(MEMKIND_HBW, num, size);
    if (ptr) {
        type = MemType::HBM_DIRECT;
        kind = MEMKIND_HBW;
    } else if (g_fallback_enabled.load()) {
        ptr = memkind_calloc(MEMKIND_HBW_PREFERRED, num, size);
        if (ptr) {
            type = MemType::HBM_FALLBACK;
            kind = MEMKIND_HBW_PREFERRED;
        }
    }

    // 回退到标准内存
    if (!ptr) {
        guard.disable();
        ptr = real_calloc(num, size);
        type = MemType::STANDARD;
        kind = MEMKIND_DEFAULT;
    }

    if (ptr) {
        std::lock_guard<std::mutex> lk(g_map_mtx);
        g_ptr_map[ptr] = AllocInfo{type, kind};
    }

    return ptr;
}

extern "C"
void *hbm_realloc(void *ptr, std::size_t size)
{
    ensure_init();
    
    if (!ptr) return hbm_malloc(size);
    if (size == 0) {
        hbm_free(ptr);
        return nullptr;
    }

    // 查找原指针信息
    AllocInfo old_info{MemType::STANDARD, MEMKIND_DEFAULT};
    {
        std::lock_guard<std::mutex> lk(g_map_mtx);
        auto it = g_ptr_map.find(ptr);
        if (it != g_ptr_map.end()) {
            old_info = it->second;
        }
    }

    RecursionGuard guard;
    void *new_ptr = nullptr;
    
    if (old_info.type == MemType::STANDARD || guard.is_recursive()) {
        guard.disable();
        new_ptr = real_realloc(ptr, size);
    } else {
        new_ptr = memkind_realloc(old_info.kind, ptr, size);
    }

    if (new_ptr && new_ptr != ptr) {
        std::lock_guard<std::mutex> lk(g_map_mtx);
        g_ptr_map.erase(ptr);
        g_ptr_map[new_ptr] = old_info;
    }

    return new_ptr;
}

extern "C"
std::size_t hbm_malloc_usable_size(void *ptr)
{
    if (!ptr) return 0;
    
    std::lock_guard<std::mutex> lk(g_map_mtx);
    auto it = g_ptr_map.find(ptr);
    if (it != g_ptr_map.end()) {
        return memkind_malloc_usable_size(it->second.kind, ptr);
    }
    
    // 调用原始函数
    typedef size_t (*malloc_usable_size_fn)(void *);
    static malloc_usable_size_fn real_malloc_usable_size = nullptr;
    if (!real_malloc_usable_size) {
        real_malloc_usable_size = (malloc_usable_size_fn)dlsym(RTLD_NEXT, "malloc_usable_size");
    }
    
    return real_malloc_usable_size ? real_malloc_usable_size(ptr) : 0;
}

// ─────────────────── free() 钩子 ──────────────────────────────
extern "C" __attribute__((visibility("default")))
void free(void *ptr)
{
    // 空指针直接返回
    if (!ptr) return;
    
    // 未初始化或正在初始化时，使用原始 free
    if (!g_initialized.load(std::memory_order_acquire) || g_in_init.load()) {
        if (!real_free) {
            init_libc_functions();
        }
        real_free(ptr);
        return;
    }
    
    // 防止递归
    RecursionGuard guard;
    if (guard.is_recursive()) {
        real_free(ptr);
        return;
    }
    
    // 查找并释放
    AllocInfo info{MemType::STANDARD, MEMKIND_DEFAULT};
    bool found = false;
    
    {
        std::lock_guard<std::mutex> lk(g_map_mtx);
        auto it = g_ptr_map.find(ptr);
        if (it != g_ptr_map.end()) {
            info = it->second;
            found = true;
            g_ptr_map.erase(it);
        }
    }
    
    // 执行释放
    if (!found || info.type == MemType::STANDARD) {
        guard.disable();  // 防止 real_free 内部再次调用
        real_free(ptr);
    } else {
        memkind_free(info.kind, ptr);
    }
    
    if (g_debug.load() && found && info.type != MemType::STANDARD) {
        std::fprintf(stderr, "[HBM] free(%p) via hook (%s)\n", ptr, to_string(info.type));
    }
}

// ─────────────────── 构造/析构函数 ────────────────────────────
__attribute__((constructor))
static void hbm_constructor()
{
    // 构造函数中进行基本初始化
    init_libc_functions();
}

__attribute__((destructor))
static void hbm_destructor()
{
    // 输出统计信息
    if (g_debug.load() && g_initialized.load()) {
        hbm_print_stats();
    }
}