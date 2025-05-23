#include <iostream>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <set>

// 声明 HBM 接口
extern "C" {
    void* hbm_malloc(size_t size);
    void hbm_free(void* ptr);
}

// 全局跟踪所有分配
std::set<void*> allocated_ptrs;

void* tracked_hbm_malloc(size_t size) {
    void* ptr = hbm_malloc(size);
    if (ptr) {
        allocated_ptrs.insert(ptr);
        std::cout << "[TRACK] hbm_malloc(" << size << ") = " << ptr << "\n";
    }
    return ptr;
}

void tracked_free(void* ptr) {
    if (!ptr) {
        std::cout << "[TRACK] free(nullptr)\n";
        return;
    }
    
    auto it = allocated_ptrs.find(ptr);
    if (it != allocated_ptrs.end()) {
        allocated_ptrs.erase(it);
        std::cout << "[TRACK] free(" << ptr << ") - valid pointer\n";
    } else {
        std::cout << "[TRACK] free(" << ptr << ") - UNKNOWN POINTER!\n";
    }
    
    free(ptr);
}

int main() {
    std::cout << "Debug verify_free behavior\n";
    std::cout << "==========================\n\n";
    
    const int iterations = 10;  // 减少迭代次数便于调试
    const size_t size = 32 * 1024;  // 32 KiB
    
    std::cout << "Loop " << iterations << " iterations (" << size/1024 << " KiB each) ...\n";
    
    for (int i = 0; i < iterations; i++) {
        std::cout << "\nIteration " << i << ":\n";
        
        // 分配内存
        void* ptr = tracked_hbm_malloc(size);
        if (!ptr) {
            std::cerr << "Allocation failed at iteration " << i << "\n";
            break;
        }
        
        // 写入一些数据
        std::memset(ptr, i & 0xFF, size);
        
        // 释放内存
        tracked_free(ptr);
    }
    
    std::cout << "\nRemaining allocations: " << allocated_ptrs.size() << "\n";
    if (!allocated_ptrs.empty()) {
        std::cout << "WARNING: Memory leaks detected!\n";
        for (auto ptr : allocated_ptrs) {
            std::cout << "  - " << ptr << "\n";
        }
    }
    
    std::cout << "\nTest completed successfully!\n";
    return 0;
}
