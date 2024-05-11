// freedom_pool.cpp v1.2 (C)2023-2024 Dmitry Bodlyrev
//
// This is the most efficient block-pool memory management system you can find. I tried many before writing my own:
// rpmalloc, tlsf, many tlsf clones.
//
// I tested it live in a multimedia rich project I am working on that has OpenGL, and audio DSP, it works great no
// weird crashes. Solid.
//
// C++ wrappers for C++ new/new[]/delete/delete[]

#include <malloc/malloc.h>
#include "freedom_pool.h"

real_malloc_ptr FreedomPool::real_malloc = NULL;
real_calloc_ptr FreedomPool::real_calloc = NULL;
real_free_ptr FreedomPool::real_free = NULL;
real_realloc_ptr FreedomPool::real_realloc = NULL;
real_malloc_size_ptr FreedomPool::real_malloc_size = NULL;
real_malloc_usable_size_ptr FreedomPool::real_malloc_usable_size = NULL;

static uint64_t total_alloc = 0;

#define Debugger() { assert(false); }

#if !defined(DISABLE_MALLOC_FREE_OVERRIDE)

void *_Nullable malloc(int64_t nb_bytes)
{
    DEBUG_PRINTF(stderr, "malloc( %ld )\n", nb_bytes);

    if (!FreedomPool::real_malloc)
        FreedomPool::initialize_overrides();
    
    return bigpool.malloc(nb_bytes);
}

void free(void *_Nonnull p) {
    DEBUG_PRINTF(stderr, "free( %ld )\n", (long)p);

    if (!FreedomPool::real_free)
        FreedomPool::initialize_overrides();
    
   // DEBUG_PRINTF(stderr, "free( %ld )\n", nb_bytes);
    bigpool.free(p);
}

/*
size_t malloc_usable_size(const void *_Nonnull ptr)
{
    DEBUG_PRINTF(stderr, "malloc_usable_size( %ld )\n", (long)ptr);

    if (!FreedomPool::real_malloc_usable_size)
        FreedomPool::initialize_overrides();
    
    return bigpool.malloc_usable_size(ptr);
}*/


size_t malloc_size(const void *_Nonnull ptr)
{
    DEBUG_PRINTF(stderr, "malloc_size( %ld )\n", (long)ptr);

    if (!FreedomPool::real_malloc_size)
        FreedomPool::initialize_overrides();
    
    return bigpool.malloc_size(ptr); 
}

void *_Nullable realloc(void *_Nonnull p, size_t new_size)
{
    DEBUG_PRINTF(stderr, "realloc( %ld, %ld )\n", (long)p, new_size);
    if (!FreedomPool::real_realloc)
        FreedomPool::initialize_overrides();
    return bigpool.realloc(p, new_size);
}

void *_Nullable calloc(size_t count, size_t size)
{
    DEBUG_PRINTF(stderr, "calloc( %ld, %ld )\n", (long)count, size);
    if (!FreedomPool::real_calloc)
        FreedomPool::initialize_overrides();
    return bigpool.calloc(count, size);
}
#endif

#if !defined(DISABLE_NEWDELETE_OVERRIDE)
void * operator new(std::size_t n) {
    total_alloc += n;
    if (n > 1024L*1024L*5L) {
        //Debugger();
        DEBUG_PRINTF(stderr, "new( %ld MB ) tot: %ld MB\n", n/1024L/1024L, total_alloc/1024L/1024L);
    }
    if (!FreedomPool::real_malloc)
        FreedomPool::initialize_overrides();
    return bigpool.malloc(n);
}

void operator delete(void * p) throw() {
    size_t space = 0;
    DEBUG_PRINTF(stderr, "delete ( %ld )\n", (long)p);
    if (!FreedomPool::real_free)
        FreedomPool::initialize_overrides();
    if (p) { space = bigpool.malloc_size(p); bigpool.free(p); }
    if (space > 0)
        total_alloc -= space;
}

void *operator new[](std::size_t n) {
    total_alloc += n;
    if (n > 1024L*1024L*5L) {
       // Debugger();
        DEBUG_PRINTF(stderr, "new[] ( %ld MB ) tot: %ld MB\n", (long)n/1024L/1024L, total_alloc/1024L/1024L);
    }
    if (!FreedomPool::real_malloc)
        FreedomPool::initialize_overrides();
    return bigpool.malloc(n);
}
void operator delete[](void *p) throw() {
    size_t space = 0;
    DEBUG_PRINTF(stderr, "delete[] ( %ld )\n", (long)p);
    if (!FreedomPool::real_free)
        FreedomPool::initialize_overrides();
    if (p) {  space = bigpool.malloc_size(p); bigpool.free(p); }
    if (space > 0)
        total_alloc -= space;
}

#endif
