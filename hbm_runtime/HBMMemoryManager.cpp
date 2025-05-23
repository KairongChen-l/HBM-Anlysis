#include "HBMMemoryManager.h"
#include <memkind.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>

// 使用 C 风格实现，确保最大兼容性

// ─────────────────── 全局状态 ────────────────────────────────
static void* (*real_malloc)(size_t) = NULL;
static void  (*real_free)(void*) = NULL;
static void* (*real_calloc)(size_t, size_t) = NULL;
static void* (*real_realloc)(void*, size_t) = NULL;
static int   (*real_posix_memalign)(void**, size_t, size_t) = NULL;

static int g_initialized = 0;
static int g_debug = 0;
static pthread_mutex_t g_init_mutex = PTHREAD_MUTEX_INITIALIZER;

// 简单的内存类型标记
#define HBM_MAGIC_HEADER 0x48424D4D  // "HBMM"
#define STANDARD_MAGIC   0x5354444D  // "STDM"

typedef struct {
    unsigned int magic;
    memkind_t kind;
    size_t size;
} alloc_header_t;

// ─────────────────── 初始化函数 ──────────────────────────────
static void init_functions(void)
{
    if (real_malloc) return;
    
    real_malloc = (void*(*)(size_t))dlsym(RTLD_NEXT, "malloc");
    real_free = (void(*)(void*))dlsym(RTLD_NEXT, "free");
    real_calloc = (void*(*)(size_t,size_t))dlsym(RTLD_NEXT, "calloc");
    real_realloc = (void*(*)(void*,size_t))dlsym(RTLD_NEXT, "realloc");
    real_posix_memalign = (int(*)(void**,size_t,size_t))dlsym(RTLD_NEXT, "posix_memalign");
    
    if (!real_malloc || !real_free) {
        fprintf(stderr, "HBMMemoryManager: Cannot resolve malloc/free\n");
        abort();
    }
}

static void do_init(void)
{
    pthread_mutex_lock(&g_init_mutex);
    if (g_initialized) {
        pthread_mutex_unlock(&g_init_mutex);
        return;
    }
    
    init_functions();
    
    // 检查环境变量
    const char* debug_env = getenv("HBM_DEBUG");
    if (debug_env && (strcmp(debug_env, "1") == 0)) {
        g_debug = 1;
    }
    
    // 检查 HBM 可用性
    if (g_debug) {
        int ret = memkind_check_available(MEMKIND_HBW);
        if (ret != MEMKIND_SUCCESS) {
            fprintf(stderr, "[HBM] Warning: HBW memory not available\n");
        } else {
            fprintf(stderr, "[HBM] HBW memory available\n");
        }
    }
    
    g_initialized = 1;
    pthread_mutex_unlock(&g_init_mutex);
}

// ─────────────────── 构造函数 ────────────────────────────────
__attribute__((constructor))
static void hbm_init(void)
{
    init_functions();
}

// ─────────────────── 辅助函数 ────────────────────────────────
void hbm_set_debug(int enable)
{
    g_debug = enable;
}

// ─────────────────── 内存分配接口 ─────────────────────────────
void *hbm_malloc(size_t bytes)
{
    if (!g_initialized) do_init();
    if (bytes == 0) return NULL;
    
    // 分配额外空间存储头部信息
    size_t total_size = bytes + sizeof(alloc_header_t);
    void *ptr = NULL;
    memkind_t kind = MEMKIND_DEFAULT;
    
    // 尝试 HBM 分配
    ptr = memkind_malloc(MEMKIND_HBW, total_size);
    if (ptr) {
        kind = MEMKIND_HBW;
    } else {
        // 尝试 HBW_PREFERRED
        ptr = memkind_malloc(MEMKIND_HBW_PREFERRED, total_size);
        if (ptr) {
            kind = MEMKIND_HBW_PREFERRED;
        } else {
            // 回退到标准内存
            ptr = real_malloc(total_size);
            kind = MEMKIND_DEFAULT;
        }
    }
    
    if (!ptr) return NULL;
    
    // 设置头部信息
    alloc_header_t *header = (alloc_header_t*)ptr;
    header->magic = HBM_MAGIC_HEADER;
    header->kind = kind;
    header->size = bytes;
    
    void *user_ptr = (char*)ptr + sizeof(alloc_header_t);
    
    if (g_debug) {
        fprintf(stderr, "[HBM] malloc(%zu) = %p (kind=%d)\n", bytes, user_ptr, kind);
    }
    
    return user_ptr;
}

void hbm_free(void *ptr)
{
    if (!ptr) return;
    
    // 获取头部信息
    alloc_header_t *header = (alloc_header_t*)((char*)ptr - sizeof(alloc_header_t));
    
    // 检查魔数
    if (header->magic != HBM_MAGIC_HEADER) {
        // 不是我们分配的，使用标准 free
        if (g_debug) {
            fprintf(stderr, "[HBM] free(%p) - not HBM allocated\n", ptr);
        }
        real_free(ptr);
        return;
    }
    
    void *real_ptr = header;
    memkind_t kind = header->kind;
    
    if (g_debug) {
        fprintf(stderr, "[HBM] free(%p) (kind=%d)\n", ptr, kind);
    }
    
    // 清除魔数，防止重复释放
    header->magic = 0;
    
    if (kind == MEMKIND_DEFAULT) {
        real_free(real_ptr);
    } else {
        memkind_free(kind, real_ptr);
    }
}

void *hbm_calloc(size_t num, size_t size)
{
    if (!g_initialized) do_init();
    if (num == 0 || size == 0) return NULL;
    
    size_t bytes = num * size;
    // 检查溢出
    if (num != 0 && bytes / num != size) {
        errno = ENOMEM;
        return NULL;
    }
    
    size_t total_size = bytes + sizeof(alloc_header_t);
    void *ptr = NULL;
    memkind_t kind = MEMKIND_DEFAULT;
    
    // 尝试 HBM 分配
    ptr = memkind_calloc(MEMKIND_HBW, 1, total_size);
    if (ptr) {
        kind = MEMKIND_HBW;
    } else {
        ptr = memkind_calloc(MEMKIND_HBW_PREFERRED, 1, total_size);
        if (ptr) {
            kind = MEMKIND_HBW_PREFERRED;
        } else {
            ptr = real_calloc(1, total_size);
            kind = MEMKIND_DEFAULT;
        }
    }
    
    if (!ptr) return NULL;
    
    alloc_header_t *header = (alloc_header_t*)ptr;
    header->magic = HBM_MAGIC_HEADER;
    header->kind = kind;
    header->size = bytes;
    
    return (char*)ptr + sizeof(alloc_header_t);
}

void *hbm_realloc(void *ptr, size_t size)
{
    if (!ptr) return hbm_malloc(size);
    if (size == 0) {
        hbm_free(ptr);
        return NULL;
    }
    
    // 获取原始信息
    alloc_header_t *old_header = (alloc_header_t*)((char*)ptr - sizeof(alloc_header_t));
    if (old_header->magic != HBM_MAGIC_HEADER) {
        // 不是我们分配的，使用标准 realloc
        return real_realloc(ptr, size);
    }
    
    // 分配新内存
    void *new_ptr = hbm_malloc(size);
    if (!new_ptr) return NULL;
    
    // 复制数据
    size_t copy_size = (old_header->size < size) ? old_header->size : size;
    memcpy(new_ptr, ptr, copy_size);
    
    // 释放旧内存
    hbm_free(ptr);
    
    return new_ptr;
}

int hbm_posix_memalign(void **memptr, size_t alignment, size_t size)
{
    if (!memptr || !alignment || (alignment & (alignment - 1)) != 0) {
        return EINVAL;
    }
    
    if (!g_initialized) do_init();
    
    *memptr = NULL;
    if (size == 0) return 0;
    
    // 确保对齐大小足够存放头部
    size_t actual_alignment = alignment;
    if (actual_alignment < sizeof(alloc_header_t)) {
        actual_alignment = sizeof(alloc_header_t);
    }
    
    size_t total_size = size + actual_alignment;
    void *ptr = NULL;
    memkind_t kind = MEMKIND_DEFAULT;
    int ret;
    
    // 尝试 HBM 分配
    ret = memkind_posix_memalign(MEMKIND_HBW, &ptr, actual_alignment, total_size);
    if (ret == 0 && ptr) {
        kind = MEMKIND_HBW;
    } else {
        ret = memkind_posix_memalign(MEMKIND_HBW_PREFERRED, &ptr, actual_alignment, total_size);
        if (ret == 0 && ptr) {
            kind = MEMKIND_HBW_PREFERRED;
        } else {
            ret = real_posix_memalign(&ptr, actual_alignment, total_size);
            kind = MEMKIND_DEFAULT;
        }
    }
    
    if (ret != 0 || !ptr) return ret;
    
    // 设置头部信息
    alloc_header_t *header = (alloc_header_t*)ptr;
    header->magic = HBM_MAGIC_HEADER;
    header->kind = kind;
    header->size = size;
    
    *memptr = (char*)ptr + actual_alignment;
    return 0;
}

// ─────────────────── free 钩子 ───────────────────────────────
void free(void *ptr)
{
    if (!ptr) return;
    
    // 如果未初始化，直接使用原始 free
    if (!g_initialized || !real_free) {
        if (!real_free) init_functions();
        real_free(ptr);
        return;
    }
    
    // 检查是否是 HBM 分配的内存
    alloc_header_t *header = (alloc_header_t*)((char*)ptr - sizeof(alloc_header_t));
    
    // 简单的边界检查，避免访问无效内存
    if ((uintptr_t)header < 4096) {
        // 地址太小，不可能是有效的头部
        real_free(ptr);
        return;
    }
    
    // 检查魔数
    if (header->magic == HBM_MAGIC_HEADER) {
        hbm_free(ptr);
    } else {
        real_free(ptr);
    }
}