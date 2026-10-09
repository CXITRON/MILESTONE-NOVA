#pragma once
#include <cstdlib>
constexpr int MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2;
inline void *heap_caps_malloc(size_t size, int) { return malloc(size); }

constexpr int MALLOC_CAP_INTERNAL=4, MALLOC_CAP_DMA=8;
inline void *heap_caps_aligned_alloc(size_t align,size_t bytes,int) { void *p=nullptr; return posix_memalign(&p,align,bytes) ? nullptr : p; }
