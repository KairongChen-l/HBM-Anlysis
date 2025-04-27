#ifndef HBM_MEMORY_MANAGER_H
#define HBM_MEMORY_MANAGER_H

#include <cstddef>

// Forward declarations of memory functions
extern "C" {
    // HBM allocation function
    void* hbm_malloc(size_t size);
    
    // HBM free function
    void hbm_free(void* ptr);
    
    // Function to check if a pointer is in HBM
    bool is_hbm_ptr(void* ptr);
    
    // Wrapped malloc and free (for link-time interception)
    void* __wrap_malloc(size_t size);
    void __wrap_free(void* ptr);
    
    // Original malloc and free
    void* __real_malloc(size_t size);
    void __real_free(void* ptr);
}

// Initialization and cleanup functions
void hbm_memory_init();
void hbm_memory_cleanup();

// Enable/disable debug output
void hbm_set_debug(bool enable);

#endif // HBM_MEMORY_MANAGER_H