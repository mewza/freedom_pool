//  freedom_pool.cpp v1.35 (C)2023-2024 DEMOS
//
//  This is the most efficient block-pool memory management system you can find. 
//   I tried many before writing my own: rpmalloc, tlsf, many tlsf clones
//
//  C and C++ wrappers for malloc/free/new/delete etc
//  NEW: v1.31 has been thouroughly tested AGAIN! and it appears to be working well, even with overrides of new/delete/malloc/free
//
//  This is the most efficient block-pool memory management system you can find. I tried many before writing my own:
//  rpmalloc, tlsf, many tlsf clones.

#include <signal.h>
#include <malloc/malloc.h>
#include "freedom_pool.h"

real_malloc_ptr _Nullable real_malloc = NULL;
real_free_ptr _Nullable real_free = NULL;
real_calloc_ptr _Nullable real_calloc = NULL;
real_realloc_ptr _Nullable real_realloc = NULL;
real_malloc_size_ptr _Nullable real_malloc_size = NULL;
real_malloc_usable_size_ptr _Nullable real_malloc_usable_size = NULL;

static size_t total_alloc = 0L;
static size_t total_max_alloc = 0L;

void reset_freedom_counters() {
    total_alloc = 0;
    total_max_alloc = 0;
}

FreedomPool<DEFAULT_GROW> bigpool;

#if !defined(DISABLE_MALLOC_FREE_OVERRIDE)

void *_Nullable malloc(size_t nb_bytes)
{
#ifdef FREEDOM_DEBUG
    total_alloc += nb_bytes;
    total_max_alloc = F_MAX(total_max_alloc, (size_t)nb_bytes);
    
    if (nb_bytes >= THRESH_DEBUG_PRINT)
        DEBUG_PRINTF(stderr, "malloc( %3ld %s ) : tot %3ld MB\n", ((nb_bytes/MBYTE) > 0) ? (size_t)(nb_bytes/MBYTE) : nb_bytes, ((nb_bytes/MBYTE) > 0) ? "MB" : "kb", (size_t)(total_alloc/MBYTE));
#endif
    return bigpool.malloc(nb_bytes);
}

void free(void *_Nullable ptr)
{
#ifdef FREEDOM_DEBUG
    size_t space = 0;
    if (ptr) { space = bigpool.malloc_size(ptr); }
    if (space > 0) {
        total_alloc -= space;
    }
    if (space >= THRESH_DEBUG_PRINT)
        DEBUG_PRINTF(stderr, "  free( %3ld MB ) : spc %3ld\n", (size_t)(space/MBYTE), (size_t)(total_alloc/MBYTE));
#endif
    bigpool.free(ptr);
}

 size_t malloc_usable_size(const void *_Nullable ptr)
 {
     DEBUG_PRINTF(stderr, "malloc_usable_size( %ld )\n", (long)ptr);
     return bigpool.malloc_usable_size(ptr);
 }


size_t malloc_size(const void *_Nullable ptr)
{
    DEBUG_PRINTF(stderr, "malloc_size( %ld )\n", (long)ptr);
    
    return bigpool.malloc_size(ptr);
}

void *_Nullable realloc(void *_Nullable ptr, size_t nb_bytes)
{
#ifdef FREEDOM_DEBUG
    // DEBUG_PRINTF(stderr, "realloc( %ld, %ld )\n", (long)p, nb_bytes);
    size_t space = 0;
    if (ptr) { space = bigpool.malloc_size(ptr); }
    
    void *_Nullable ret = NULL;
    ret = bigpool.realloc(ptr, nb_bytes);
    
    // don't count freedompool extend
    if (ret) {
        total_alloc += nb_bytes - space;
    }
#ifdef BREAK_ON_THRESH
    if (nb_bytes >= THRESH_DEBUG_BREAK)
        DEBUGGER
#endif
    total_max_alloc = std::max(total_max_alloc, total_alloc);
    if (nb_bytes >= THRESH_DEBUG_PRINT)
        DEBUG_PRINTF(stderr, "realloc( %3ld %s ) : tot %3ld MB\n", ((nb_bytes/MBYTE) > 0) ? (size_t)(nb_bytes/MBYTE) : nb_bytes, ((nb_bytes/MBYTE) > 0) ? "MB" : "kb", (size_t)(total_alloc/MBYTE));
    return ret;
#endif
    return bigpool.realloc(ptr, nb_bytes);
}

void *_Nullable calloc(size_t count, size_t size)
{
#ifdef FREEDOM_DEBUG
    //  DEBUG_PRINTF(stderr, "calloc( %ld, %ld )\n", (long)count, size);
    size_t nb_bytes = size * count;
    total_alloc += nb_bytes;
    total_max_alloc = std::max(total_max_alloc, total_alloc);
#ifdef BREAK_ON_THRESH
    if (nb_bytes >= THRESH_DEBUG_BREAK)
        DEBUGGER
#endif
    if (nb_bytes >= THRESH_DEBUG_PRINT)
        DEBUG_PRINTF(stderr, "calloc( %3ld %s ) : tot %3ld MB\n", ((nb_bytes/MBYTE) > 0) ? (size_t)(nb_bytes/MBYTE) : nb_bytes, ((nb_bytes/MBYTE) > 0) ? "MB" : "kb", (size_t)(total_alloc/MBYTE));
#endif
    return bigpool.calloc(count, size);
}

#endif // DISABLE_MALLOC_FREE_OVERRIDE

#if !defined(DISABLE_NEWDELETE_OVERRIDE)

#ifdef __cplusplus

void * operator new(std::size_t nb_bytes)
{
    void *ptr = bigpool.malloc(nb_bytes);
#ifdef FREEDOM_DEBUG
    total_alloc += nb_bytes;
#ifdef BREAK_ON_THRESH
    if (n >= THRESH_DEBUG_BREAK)
        DEBUGGER
#endif
    total_max_alloc = std::max(total_max_alloc, total_alloc);
    if (nb_bytes >= THRESH_DEBUG_PRINT)
        DEBUG_PRINTF(stderr, "new( %3lld %s %7x ) : tot %3ld MB\n", ((nb_bytes/MBYTE) > 0) ? (size_t)(nb_bytes/MBYTE) : nb_bytes, ((nb_bytes/MBYTE) > 0) ? "MB" : "kb", ptr,  (size_t)(total_alloc/MBYTE));
#endif
    
    return ptr;
}

void operator delete(void *_Nullable ptr) throw()
{
#ifdef FREEDOM_DEBUG
    size_t space = 0;
    if (ptr) { space = bigpool.malloc_size(ptr); }
    if (space > 0)
        total_alloc -= space;
    if (space >= THRESH_DEBUG_BREAK)
        DEBUG_PRINTF(stderr, "delete ( %3lld %7x) : spc %3ld\n", space/MBYTE, ptr, total_alloc/MBYTE);
#endif
     bigpool.free(ptr);
}

void *operator new[](std::size_t nb_bytes)
{
    void *ptr = bigpool.malloc(nb_bytes);
#ifdef FREEDOM_DEBUG
    total_alloc += nb_bytes;
    total_max_alloc = std::max(total_max_alloc, total_alloc);
    if (nb_bytes >= THRESH_DEBUG_PRINT)
            DEBUG_PRINTF(stderr, "new[]( %3ld %s %7x ) : tot %3ld MB\n", ((nb_bytes/MBYTE) > 0) ? (size_t)(nb_bytes/MBYTE) : nb_bytes, ((nb_bytes/MBYTE) > 0) ? "MB" : "kb", ptr,  (size_t)(total_alloc/MBYTE));
#ifdef BREAK_ON_THRESH
    if (n >= THRESH_DEBUG_BREAK)
        DEBUGGER
#endif
#endif
        
    return ptr;
}

void operator delete[](void *ptr) throw()
{
#ifdef FREEDOM_DEBUG
    size_t space = 0;
    if (ptr) { space = bigpool.malloc_size(ptr); }
    if (space > 0)
        total_alloc -= space;
    if (space >= THRESH_DEBUG_BREAK)
        DEBUG_PRINTF(stderr, "delete[] ( %3ld %7x ) : spc %3ld\n", space, ptr, total_alloc);
#endif
    bigpool.free(ptr);
}

#endif

#endif




