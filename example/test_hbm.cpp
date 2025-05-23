#include <iostream>
#include <cstdlib>
#include <vector>

// 声明 HBM 接口
extern "C" {
    void* hbm_malloc(size_t size);
    void hbm_free(void* ptr);
}

int main() {
    std::cout << "Testing HBM Memory Manager\n";
    
    // 测试1：普通 malloc/free
    std::cout << "\n1. Testing regular malloc/free:\n";
    void* p1 = malloc(1024);
    std::cout << "malloc(1024) = " << p1 << "\n";
    free(p1);
    std::cout << "free() completed\n";
    
    // 测试2：HBM malloc/free
    std::cout << "\n2. Testing HBM malloc/free:\n";
    void* p2 = hbm_malloc(2048);
    std::cout << "hbm_malloc(2048) = " << p2 << "\n";
    hbm_free(p2);
    std::cout << "hbm_free() completed\n";
    
    // 测试3：HBM malloc + regular free (通过钩子)
    std::cout << "\n3. Testing HBM malloc + regular free:\n";
    void* p3 = hbm_malloc(4096);
    std::cout << "hbm_malloc(4096) = " << p3 << "\n";
    free(p3);  // 应该通过钩子正确释放
    std::cout << "free() completed\n";
    
    // 测试4：混合使用
    std::cout << "\n4. Testing mixed allocations:\n";
    std::vector<void*> ptrs;
    
    for (int i = 0; i < 5; i++) {
        void* p;
        if (i % 2 == 0) {
            p = hbm_malloc(1024 * (i + 1));
            std::cout << "hbm_malloc(" << 1024 * (i + 1) << ") = " << p << "\n";
        } else {
            p = malloc(1024 * (i + 1));
            std::cout << "malloc(" << 1024 * (i + 1) << ") = " << p << "\n";
        }
        ptrs.push_back(p);
    }
    
    // 使用 free() 释放所有内存（包括 HBM）
    for (auto p : ptrs) {
        free(p);
    }
    std::cout << "All freed successfully\n";
    
    std::cout << "\nAll tests completed!\n";
    return 0;
}
