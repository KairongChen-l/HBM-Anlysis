#include "HBMMemoryManager.h"
#include <memkind.h>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <unordered_map>
#include <atomic>

// Forward declarations for real malloc and free
extern "C" {
    void *__real_malloc(size_t size);
    void __real_free(void *ptr);
}

// Enum to track memory types
enum class MemoryType {
    STANDARD,
    HBM_DIRECT,     // Direct HBM allocation
    HBM_PREFERRED,  // HBM preferred allocation
    UNKNOWN
};

// Global mutex to protect our pointer tracking data structure
static std::mutex g_ptr_mutex;

// Map to track allocated pointers and their memory type
static std::unordered_map<void*, MemoryType> g_ptr_map;

// Debug flag
static std::atomic<bool> g_debug_output{false};

// Memory initialization
void hbm_memory_init() {
    // Initialize memkind library if needed
    // Currently empty as memkind auto-initializes
}

// Memory cleanup
void hbm_memory_cleanup() {
    std::lock_guard<std::mutex> lock(g_ptr_mutex);
    g_ptr_map.clear();
}

// Enable/disable debug output
void hbm_set_debug(bool enable) {
    g_debug_output.store(enable);
}

// Function to check if a pointer is from HBM
extern "C" bool is_hbm_ptr(void *ptr) {
    if (!ptr) return false;
    
    // First check our tracking map for fast lookups
    {
        std::lock_guard<std::mutex> lock(g_ptr_mutex);
        auto it = g_ptr_map.find(ptr);
        if (it != g_ptr_map.end()) {
            return (it->second == MemoryType::HBM_DIRECT || 
                    it->second == MemoryType::HBM_PREFERRED);
        }
    }
    
    // If not in our map, we try to be cautious and return false
    // This avoids potentially crashing with memkind_detect_kind
    return false;
}

// Helper function to safely detect memory kind
static memkind_t safe_detect_kind(void* ptr) {
    // Assume DEFAULT if we can't detect
    if (!ptr) return MEMKIND_DEFAULT;
    
    memkind_t kind = MEMKIND_DEFAULT;
    try {
        // This can sometimes fail with an assertion
        kind = memkind_detect_kind(ptr);
        if (kind < 0) kind = MEMKIND_DEFAULT;
    } catch (...) {
        // If detection fails, assume default memory
        if (g_debug_output.load()) {
            std::cerr << "Warning: Exception in memkind_detect_kind for ptr " << ptr << std::endl;
        }
        kind = MEMKIND_DEFAULT;
    }
    
    return kind;
}

// Add wrapping for malloc
extern "C" void *__wrap_malloc(size_t size) {
    // For regular code, hbm_malloc will decide whether to use HBM or not
    return hbm_malloc(size);
}

// Global free replacement function
extern "C" void __wrap_free(void *ptr) {
    if (!ptr) return; // Handle NULL pointer
    
    // Use our is_hbm_ptr function to determine the correct free function
    if (is_hbm_ptr(ptr)) {
        // Call HBM-specific free
        hbm_free(ptr);
    } else {
        // Remove from our tracking map if present
        {
            std::lock_guard<std::mutex> lock(g_ptr_mutex);
            g_ptr_map.erase(ptr);
        }
        
        // Call original free implementation
        __real_free(ptr);
    }
}

// HBM memory allocation function
extern "C" void *hbm_malloc(size_t size) {
    if (size == 0) {
        return nullptr;
    }

    void *ptr = nullptr;
    MemoryType memType = MemoryType::STANDARD;
    
    // Try to use high bandwidth memory
    try {
        ptr = memkind_malloc(MEMKIND_HBW, size);
        if (ptr) {
            memType = MemoryType::HBM_DIRECT;
        }
    } catch (...) {
        if (g_debug_output.load()) {
            std::cerr << "Exception in memkind_malloc(MEMKIND_HBW)" << std::endl;
        }
        ptr = nullptr;
    }

    // If HBM allocation failed, try with PREFERRED strategy
    if (!ptr) {
        try {
            ptr = memkind_malloc(MEMKIND_HBW_PREFERRED, size);
            if (ptr) {
                memType = MemoryType::HBM_PREFERRED;
                if (g_debug_output.load()) {
                    std::cerr << "HBW allocation failed, using HBW_PREFERRED successfully" << std::endl;
                }
            }
        } catch (...) {
            if (g_debug_output.load()) {
                std::cerr << "Exception in memkind_malloc(MEMKIND_HBW_PREFERRED)" << std::endl;
            }
            ptr = nullptr;
        }
    }
    
    // If HBM allocation still failed, fall back to regular memory
    if (!ptr) {
        if (g_debug_output.load()) {
            std::cerr << "HBM allocation failed, falling back to regular memory" << std::endl;
        }
        
        ptr = __real_malloc(size);
        memType = MemoryType::STANDARD;
        
        if (!ptr && g_debug_output.load()) {
            std::cerr << "ERROR: Memory allocation failed completely for size " 
                      << size << " bytes" << std::endl;
        }
    }

    // Track the pointer if allocation succeeded
    if (ptr) {
        std::lock_guard<std::mutex> lock(g_ptr_mutex);
        g_ptr_map[ptr] = memType;
    }
    
    if (g_debug_output.load()) {
        const char* type_str = "UNKNOWN";
        switch (memType) {
            case MemoryType::STANDARD: type_str = "STANDARD"; break;
            case MemoryType::HBM_DIRECT: type_str = "HBM_DIRECT"; break;
            case MemoryType::HBM_PREFERRED: type_str = "HBM_PREFERRED"; break;
            default: break;
        }
        std::cout << "HBM malloc: " << ptr << " (" << size 
                  << " bytes) using " << type_str << std::endl;
    }
    
    return ptr;
}

// HBM memory release function
extern "C" void hbm_free(void *ptr) {
    if (!ptr) return;

    MemoryType memType = MemoryType::UNKNOWN;
    
    // Get and remove from our tracking map
    {
        std::lock_guard<std::mutex> lock(g_ptr_mutex);
        auto it = g_ptr_map.find(ptr);
        if (it != g_ptr_map.end()) {
            memType = it->second;
            g_ptr_map.erase(it);
        }
    }
    
    if (memType == MemoryType::UNKNOWN) {
        // If not found in our map, try to detect kind
        memkind_t kind = safe_detect_kind(ptr);
        
        if (kind == MEMKIND_HBW || 
            kind == MEMKIND_HBW_PREFERRED || 
            kind == MEMKIND_HBW_HUGETLB || 
            kind == MEMKIND_HBW_PREFERRED_HUGETLB || 
            kind == MEMKIND_HBW_ALL || 
            kind == MEMKIND_HBW_ALL_HUGETLB || 
            kind == MEMKIND_HBW_INTERLEAVE) {
            memType = MemoryType::HBM_DIRECT;
        } else {
            memType = MemoryType::STANDARD;
        }
    }

    if (g_debug_output.load()) {
        const char* type_str = "UNKNOWN";
        switch (memType) {
            case MemoryType::STANDARD: type_str = "STANDARD"; break;
            case MemoryType::HBM_DIRECT: type_str = "HBM_DIRECT"; break;
            case MemoryType::HBM_PREFERRED: type_str = "HBM_PREFERRED"; break;
            default: break;
        }
        std::cout << "HBM free: " << ptr << " of type " << type_str << std::endl;
    }

    // Free the memory based on its type
    if (memType == MemoryType::HBM_DIRECT || memType == MemoryType::HBM_PREFERRED) {
        try {
            // For HBM memory, use memkind_free with the appropriate kind
            memkind_t kind = safe_detect_kind(ptr);
            
            // If detection failed, try with a generic HBM kind
            if (kind == MEMKIND_DEFAULT) {
                kind = MEMKIND_HBW;
            }
            
            memkind_free(kind, ptr);
            
            if (g_debug_output.load()) {
                std::cout << "Called memkind_free successfully" << std::endl;
            }
        } catch (...) {
            if (g_debug_output.load()) {
                std::cerr << "ERROR: Exception in memkind_free, trying standard free" << std::endl;
            }
            
            // Fallback to standard free if memkind_free fails
            try {
                __real_free(ptr);
            } catch (...) {
                if (g_debug_output.load()) {
                    std::cerr << "ERROR: Standard free also failed!" << std::endl;
                }
            }
        }
    } else {
        // For standard memory, use the regular free
        try {
            __real_free(ptr);
            
            if (g_debug_output.load()) {
                std::cout << "Called standard free successfully" << std::endl;
            }
        } catch (...) {
            if (g_debug_output.load()) {
                std::cerr << "ERROR: Exception during standard free" << std::endl;
            }
        }
    }
}

// Optional: C++ memory management overrides
// Note: These should be conditionally compiled if needed

/*
void* operator new(size_t size) {
    return hbm_malloc(size);
}

void* operator new[](size_t size) {
    return hbm_malloc(size);
}

void operator delete(void* ptr) noexcept {
    __wrap_free(ptr);
}

void operator delete[](void* ptr) noexcept {
    __wrap_free(ptr);
}

// C++14 sized delete overloads
void operator delete(void* ptr, size_t) noexcept {
    __wrap_free(ptr);
}

void operator delete[](void* ptr, size_t) noexcept {
    __wrap_free(ptr);
}
*/