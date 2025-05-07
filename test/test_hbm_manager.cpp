#include "HBMMemoryManager.h"
#include <iostream>
#include <cstring>  // for memset
#include <vector>
#include <cstdlib>  // for rand

int main() {
    // Initialize the HBM memory system
    hbm_memory_init();
    
    // Enable debug output
    hbm_set_debug(true);
    
    std::cout << "==== Start HBM Memory Manager Full Test ====" << std::endl;

    // --- 单块测试 ---
    std::cout << "\n[1] Testing hbm_malloc + hbm_free..." << std::endl;
    size_t allocSize = 1024 * 1024; // 1MB
    void* p1 = hbm_malloc(allocSize);
    if (!p1) {
        std::cerr << "hbm_malloc failed!" << std::endl;
        return 1;
    }
    std::cout << "hbm_malloc returned: " << p1 << std::endl;
    
    if (is_hbm_ptr(p1)) {
        std::cout << "Confirmed: Pointer is recognized as HBM memory." << std::endl;
    } else {
        std::cout << "Warning: Pointer NOT recognized as HBM memory!" << std::endl;
    }

    // Try writing to the memory
    try {
        memset(p1, 0xA5, allocSize);
        std::cout << "Memory written successfully." << std::endl;
    } catch (...) {
        std::cerr << "Exception when writing to memory!" << std::endl;
    }

    // Free the memory
    try {
        hbm_free(p1);
        std::cout << "hbm_free succeeded." << std::endl;
    } catch (...) {
        std::cerr << "Exception when freeing memory!" << std::endl;
    }

    // --- __wrap_malloc + __wrap_free 测试 ---
    std::cout << "\n[2] Testing __wrap_malloc + __wrap_free..." << std::endl;
    void* p2 = nullptr;
    try {
        p2 = __wrap_malloc(2 * allocSize);
        if (!p2) {
            std::cerr << "__wrap_malloc failed!" << std::endl;
        } else {
            std::cout << "__wrap_malloc returned: " << p2 << std::endl;

            if (is_hbm_ptr(p2)) {
                std::cout << "Confirmed: __wrap_malloc pointer is HBM memory." << std::endl;
            } else {
                std::cout << "Info: __wrap_malloc pointer is standard memory." << std::endl;
            }

            memset(p2, 0x5A, 2 * allocSize);
            std::cout << "Memory written successfully." << std::endl;

            __wrap_free(p2);
            std::cout << "__wrap_free succeeded." << std::endl;
        }
    } catch (...) {
        std::cerr << "Exception in __wrap_malloc test!" << std::endl;
        if (p2) __wrap_free(p2);
    }

    // --- 连续分配测试 ---
    std::cout << "\n[3] Bulk allocation test..." << std::endl;
    const int N = 10;
    std::vector<void*> blocks;
    
    try {
        for (int i = 0; i < N; ++i) {
            // Vary allocation sizes
            size_t blockSize = 256 * 1024 + (rand() % 512) * 1024; // 256KB-768KB
            void* blk = hbm_malloc(blockSize);
            if (blk) {
                blocks.push_back(blk);
                memset(blk, i, blockSize); // Write some data
            }
        }
        std::cout << "Allocated " << blocks.size() << " blocks." << std::endl;

        for (void* blk : blocks) {
            hbm_free(blk);
        }
        std::cout << "All blocks freed successfully." << std::endl;
    } catch (...) {
        std::cerr << "Exception in bulk allocation test!" << std::endl;
        // Clean up any remaining blocks
        for (void* blk : blocks) {
            if (blk) hbm_free(blk);
        }
    }

    // Allocate more memory after the first round of tests
    std::cout << "\n[4] Second round allocation test..." << std::endl;
    void* p3 = hbm_malloc(1024 * 1024);
    if (p3) {
        std::cout << "Second round allocation succeeded: " << p3 << std::endl;
        memset(p3, 0, 1024 * 1024);
        hbm_free(p3);
        std::cout << "Second round free succeeded." << std::endl;
    } else {
        std::cout << "Second round allocation failed." << std::endl;
    }

    // Clean up and exit
    hbm_memory_cleanup();
    std::cout << "\n==== End of HBM Memory Manager Full Test ====" << std::endl;
    return 0;
}