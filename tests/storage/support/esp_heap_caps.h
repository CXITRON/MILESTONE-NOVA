#pragma once
#include <cstdlib>
inline constexpr int MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2;
inline void *heap_caps_malloc(size_t bytes, int) { return malloc(bytes); }

inline void *heap_caps_aligned_alloc(size_t alignment, size_t size, int caps) { (void)caps; void *p=nullptr; return posix_memalign(&p, alignment, size) ? nullptr : p; }
