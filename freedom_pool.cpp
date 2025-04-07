//  freedom_pool.h v1.4 (c)2023-2025 Dmitry Boldyrev
//
//  This is the most efficient block-pool memory management system you can find.
//  I tried many before writing my own: rpmalloc, tlsf, etc.
//  This code is partially based off this block allocator concept:
//  https://www.codeproject.com/Articles/1180070/Simple-Variable-Size-Memory-Block-Allocator
//
//  NEW (v1.4): Optimizatoins and configurable memory alignment. Got rid of multimap bottleneck.

#include "freedom_pool.h"

#define BIGPOOL_MALLOC bigpool.malloc
#define BIGPOOL_USABLE_SIZE bigpool.malloc_usable_size
#define BIGPOOL_FREE bigpool.free
#define BIGPOOL_SIZE bigpool.malloc_size
#define BIGPOOL_CALLOC bigpool.calloc
#define BIGPOOL_REALLOC bigpool.realloc

#ifdef __cplusplus

#define PRINT_V(x) (((x)/MBYTE) > 0) ? (x)/MBYTE : (x)/KBYTE, (((x)/MBYTE) > 0) ? "MB" : "kb"

real_malloc_ptr _Nullable real_malloc = NULL;
real_free_ptr _Nullable real_free = NULL;
real_calloc_ptr _Nullable real_calloc = NULL;
real_realloc_ptr _Nullable real_realloc = NULL;
real_malloc_size_ptr _Nullable real_malloc_size = NULL;
real_malloc_usable_size_ptr _Nullable real_malloc_usable_size = NULL;

static size_t heap_alloc = 0L;
static size_t heap_max_alloc = 0L;

void reset_freedom_counters(void)
{
    heap_alloc = 0;
    heap_max_alloc = 0;
}

FreedomPool<DEFAULT_GROW> bigpool;

#ifndef DISABLE_MALLOC_FREE_OVERRIDE

void *_Nullable malloc(size_t nb_bytes)
{
#ifdef FREEDOM_DEBUG
    heap_alloc += nb_bytes;
    heap_max_alloc = F_MAX(heap_max_alloc, (size_t)nb_bytes);
    
    if (nb_bytes >= THRESH_DEBUG_PRINT)
        DEBUG_PRINTF(stderr, "malloc( %3ld %s ) heap: %3ld %s\n", PRINT_V(nb_bytes), PRINT_V(heap_alloc));
#endif
    return BIGPOOL_MALLOC(nb_bytes);
}

void free(void *_Nullable ptr)
{
#ifdef FREEDOM_DEBUG
    size_t space = 0;
    if (ptr) { space = BIGPOOL_SIZE(ptr); }
    if (space > 0) {
        heap_alloc -= space;
    }
    if (space >= THRESH_DEBUG_PRINT)
        DEBUG_PRINTF(stderr, "  free( %3ld %s ) heap: %3ld %s\n", PRINT_V(space), PRINT_V(heap_alloc));
#endif
    BIGPOOL_FREE(ptr);
}

 size_t malloc_usable_size(const void *_Nullable ptr)
 {
     DEBUG_PRINTF(stderr, "malloc_usable_size( %ld )\n", (long)ptr);
     return BIGPOOL_USABLE_SIZE(ptr);
 }


size_t malloc_size(const void *_Nullable ptr)
{
    DEBUG_PRINTF(stderr, "malloc_size( %ld )\n", (long)ptr);
    
    return BIGPOOL_SIZE(ptr);
}

void *_Nullable realloc(void *_Nullable ptr, size_t nb_bytes)
{
#ifdef FREEDOM_DEBUG
    // DEBUG_PRINTF(stderr, "realloc( %ld, %ld )\n", (long)p, nb_bytes);
    size_t space = 0;
    if (ptr) { space = BIGPOOL_SIZE(ptr); }
    
    void *_Nullable ret = NULL;
    ret = BIGPOOL_REALLOC(ptr, nb_bytes);
    
    // don't count freedompool extend
    if (ret) {
        heap_alloc += nb_bytes - space;
    }
#ifdef BREAK_ON_THRESH
    if (nb_bytes >= THRESH_DEBUG_BREAK)
        DEBUGGER
#endif
    heap_max_alloc = std::max(heap_max_alloc, heap_alloc);
    if (nb_bytes >= THRESH_DEBUG_PRINT)
        DEBUG_PRINTF(stderr, "realloc( %3ld %s ) heap: %3ld %s\n", PRINT_V(nb_bytes), PRINT_V(heap_alloc));
    return ret;
#endif
    return BIGPOOL_REALLOC(ptr, nb_bytes);
}

void *_Nullable calloc(size_t count, size_t size)
{
#ifdef FREEDOM_DEBUG
    //  DEBUG_PRINTF(stderr, "calloc( %ld, %ld )\n", (long)count, size);
    size_t nb_bytes = size * count;
    heap_alloc += nb_bytes;
    heap_max_alloc = std::max(heap_max_alloc, heap_alloc);
#ifdef BREAK_ON_THRESH
    if (nb_bytes >= THRESH_DEBUG_BREAK)
        DEBUGGER
#endif
    if (nb_bytes >= THRESH_DEBUG_PRINT)
        DEBUG_PRINTF(stderr, "calloc( %3ld %s ) heap: %3ld %s\n", PRINT_V(nb_bytes), PRINT_V(heap_alloc));
#endif
    return BIGPOOL_CALLOC(count, size);
}
#endif // DISABLE_MALLOC_FREE_OVERRIDE

#ifndef DISABLE_NEWDELETE_OVERRIDE

void * operator new(std::size_t nb_bytes)
{
    void *ptr = BIGPOOL_MALLOC(nb_bytes);
#ifdef FREEDOM_DEBUG
    heap_alloc += nb_bytes;
#ifdef BREAK_ON_THRESH
    if (nb_bytes >= THRESH_DEBUG_BREAK)
        DEBUGGER
#endif
    heap_max_alloc = std::max(heap_max_alloc, heap_alloc);
    if (nb_bytes >= THRESH_DEBUG_PRINT)
        DEBUG_PRINTF(stderr, "   new( %3lld %s %7x ) heap: %3ld %s\n", PRINT_V(nb_bytes), ptr,  PRINT_V(heap_alloc));
#endif
    
    return ptr;
}

void operator delete(void *_Nullable ptr) throw()
{
#ifdef FREEDOM_DEBUG
    size_t space = 0;
    if (ptr) { space = BIGPOOL_SIZE(ptr); }
    if (space > 0)
        heap_alloc -= space;
    if (space >= THRESH_DEBUG_BREAK)
        DEBUG_PRINTF(stderr, "delete( %3lld %s %7x) heap: %3ld %s\n", PRINT_V(space), ptr, PRINT_V(heap_alloc));
#endif
     BIGPOOL_FREE(ptr);
}

void *operator new[](std::size_t nb_bytes)
{
    void *ptr = BIGPOOL_MALLOC(nb_bytes);
#ifdef FREEDOM_DEBUG
    heap_alloc += nb_bytes;
    heap_max_alloc = std::max(heap_max_alloc, heap_alloc);
    if (nb_bytes >= THRESH_DEBUG_PRINT)
            DEBUG_PRINTF(stderr, " new[]( %3ld %s %7x ) heap: %3ld %s\n", PRINT_V(nb_bytes), ptr,  PRINT_V(heap_alloc));
#ifdef BREAK_ON_THRESH
    if (nb_bytes >= THRESH_DEBUG_BREAK)
        DEBUGGER
#endif
#endif
        
    return ptr;
}

void operator delete[](void *ptr) throw()
{
#ifdef FREEDOM_DEBUG
    size_t space = 0;
    if (ptr) { space = BIGPOOL_SIZE(ptr); }
    if (space > 0)
        heap_alloc -= space;
    if (space >= THRESH_DEBUG_BREAK)
        DEBUG_PRINTF(stderr, " del[]( %3ld %s %7x ) heap: %3ld %s\n", PRINT_V(space), ptr, PRINT_V(heap_alloc));
#endif
    BIGPOOL_FREE(ptr);
}

#endif // DISABLE_NEWDELETE_OVERRIDE

#endif // __cplusplus
