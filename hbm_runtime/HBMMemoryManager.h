#pragma once

#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

// 核心接口
void *hbm_malloc(size_t bytes);
void  hbm_free(void *ptr);

// 扩展接口
void *hbm_calloc(size_t num, size_t size);
void *hbm_realloc(void *ptr, size_t size);
int   hbm_posix_memalign(void **memptr, size_t alignment, size_t size);

// 辅助接口
void hbm_set_debug(int enable);

#ifdef __cplusplus
}
#endif