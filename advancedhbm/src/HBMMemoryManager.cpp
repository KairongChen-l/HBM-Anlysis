#include "HBMMemoryManager.h"
#include <memkind.h>
#include <cstdlib>
#include <iostream>

// HBM内存分配函数
extern "C" void *hbm_malloc(size_t size)
{
    if (size == 0)
    {
        // 处理零大小分配请求
        return nullptr;
    }

    // 尝试使用高带宽内存进行分配
    void *ptr = memkind_malloc(MEMKIND_HBW, size);

    // 如果HBM分配失败，尝试使用PREFERRED策略
    if (!ptr)
    {
        // 尝试使用HBW_PREFERRED（优先使用高带宽内存）
        ptr = memkind_malloc(MEMKIND_HBW_PREFERRED, size);

        if (ptr)
        {
            std::cerr << "HBW allocation failed, using HBW_PREFERRED successfully\n";
        }
        else
        {
            // 如果仍然失败，回退到普通内存
            std::cerr << "HBM allocation failed, falling back to regular memory\n";
            ptr = malloc(size);

            if (!ptr)
            {
                // 所有分配方式都失败
                std::cerr << "ERROR: Memory allocation failed completely for size " << size << " bytes\n";
                return nullptr;
            }
        }
    }

// 记录分配信息（如果需要调试）
#ifdef DEBUG_HBM_ALLOC
    memkind_t kind = memkind_detect_kind(ptr);
    const char *kind_name = "unknown";
    if (kind == MEMKIND_HBW)
        kind_name = "MEMKIND_HBW";
    else if (kind == MEMKIND_HBW_PREFERRED)
        kind_name = "MEMKIND_HBW_PREFERRED";
    else if (kind == MEMKIND_DEFAULT)
        kind_name = "MEMKIND_DEFAULT";

    std::cerr << "HBM allocation: " << ptr << " (" << size
              << " bytes) using " << kind_name << std::endl;
#endif
    std::cout << "调用了HBM_Malloc" << std::endl;
    return ptr;
}

// HBM内存释放函数
extern "C" void hbm_free(void *ptr)
{
    // 检查指针是否为空
    if (!ptr)
    {
#ifdef DEBUG_HBM_ALLOC
        std::cerr << "hbm_free: Ignoring NULL pointer\n";
#endif
        return;
    }

    // 尝试确定内存类型（HBM vs 常规内存）
    memkind_t kind = memkind_detect_kind(ptr);

#ifdef DEBUG_HBM_ALLOC
    const char *kind_name = "unknown";
    if (kind == MEMKIND_HBW)
        kind_name = "MEMKIND_HBW";
    else if (kind == MEMKIND_HBW_PREFERRED)
        kind_name = "MEMKIND_HBW_PREFERRED";
    else if (kind == MEMKIND_HBW_HUGETLB)
        kind_name = "MEMKIND_HBW_HUGETLB";
    else if (kind == MEMKIND_HBW_PREFERRED_HUGETLB)
        kind_name = "MEMKIND_HBW_PREFERRED_HUGETLB";
    else if (kind == MEMKIND_HBW_ALL)
        kind_name = "MEMKIND_HBW_ALL";
    else if (kind == MEMKIND_HBW_ALL_HUGETLB)
        kind_name = "MEMKIND_HBW_ALL_HUGETLB";
    else if (kind == MEMKIND_HBW_INTERLEAVE)
        kind_name = "MEMKIND_HBW_INTERLEAVE";
    else if (kind == MEMKIND_DEFAULT)
        kind_name = "MEMKIND_DEFAULT";
    else
        kind_name = "STANDARD_MEMORY";

    std::cerr << "HBM free: " << ptr << " of type " << kind_name << std::endl;
#endif

    // 根据内存类型选择适当的释放函数
    if (kind == MEMKIND_HBW ||
        kind == MEMKIND_HBW_PREFERRED ||
        kind == MEMKIND_HBW_HUGETLB ||
        kind == MEMKIND_HBW_PREFERRED_HUGETLB ||
        kind == MEMKIND_HBW_ALL ||
        kind == MEMKIND_HBW_ALL_HUGETLB ||
        kind == MEMKIND_HBW_INTERLEAVE)
    {
        // 使用检测到的确切类型来释放内存
        try
        {
            memkind_free(kind, ptr);
            std::cout << "调用了HBM_Free (HBM内存)" << std::endl;
        }
        catch (...)
        {
            std::cerr << "ERROR: Exception occurred during memkind_free\n";
        }
    }
    else
    {
        // 对于标准内存或无法识别的内存类型，使用常规free
        try
        {
            free(ptr);
            std::cout << "调用了HBM_Free (标准内存)" << std::endl;
        }
        catch (...)
        {
            std::cerr << "ERROR: Exception occurred during standard free\n";
        }
    }
}