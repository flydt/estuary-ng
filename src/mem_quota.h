#include <stddef.h>

#define CT_MEM_QUOTA_ENABLED 1

// size must add L to define macro as long int
// otherwise it will lead overflow
#define CT_MEM_QUOTA_SIZE (8 * 1024 * 1024 * 1024L)

void quota_mem_init(size_t quota_size);

void quota_mem_destroy();

void quota_mem_free(void *mem_ptr, size_t mem_size);

void *quota_mem_alloc(size_t alloc_size);
