#ifndef HBM_MEMORY_MANAGER_H
#define HBM_MEMORY_MANAGER_H

#include <cstddef>

#ifdef __cplusplus
extern "C"
{
#endif

    // HBM内存分配函数 - 替代malloc
    void *hbm_malloc(size_t size);

    // HBM内存释放函数 - 替代free
    void hbm_free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif // HBM_MEMORY_MANAGER_H