// freedom_pool.cpp v1.3 (C)2023-2024 Dmitry Bodlyrev
//
// This is the most efficient block-pool memory management system you can find. I tried many before writing my own:
// rpmalloc, tlsf, many tlsf clones
//
// C and C++ wrappers for malloc/free/new/delete etc
//  NEW: v1.3 has been thouroughly tested and it appears to be actually working the way it should!

#include <malloc/malloc.h>
#include <signal.h>

#include "freedom_pool.h"

real_malloc_ptr _Nullable real_malloc = NULL;
real_free_ptr _Nullable real_free = NULL;
real_calloc_ptr _Nullable real_calloc = NULL;
real_realloc_ptr _Nullable real_realloc = NULL;
real_malloc_size_ptr _Nullable real_malloc_size = NULL;
real_malloc_usable_size_ptr _Nullable real_malloc_usable_size = NULL;

static int64_t total_alloc = 0L;
static int64_t total_max_alloc = 0L;

#define FREEDOM_DEBUG
#define BREAK_ON_THRESH

static const int64_t MBYTE = 1048576;
static const int64_t THRESH_DEBUG_BREAK = 18 * MBYTE;
static const int64_t THRESH_DEBUG_PRINT = 2 * MBYTE;

#define DEBUGGER      raise(SIGINT);

void reset_freedom_counters() {
    total_alloc = 0;
    total_max_alloc = 0;
}

#if !defined(DISABLE_MALLOC_FREE_OVERRIDE)

void *_Nullable malloc(size_t nb_bytes)
{
    if (!real_malloc)
        FreedomPool<DEFAULT_GROW>::initialize_overrides();
#ifdef FREEDOM_DEBUG
    // DEBUG_PRINTF(stderr, "malloc( %ld )\n", nb_bytes);
    total_alloc += nb_bytes;
    total_max_alloc = std::max((int64_t)total_max_alloc, (int64_t)nb_bytes);
    
    if (nb_bytes >= THRESH_DEBUG_PRINT)
        DEBUG_PRINTF(stderr, "malloc( %3lld MB ) : spc %3lld MB\n", (int64_t)(nb_bytes/MBYTE), (int64_t)(total_alloc/MBYTE));
#endif
    return bigpool.malloc(nb_bytes);
}

void free(void *_Nullable p)
{
    int64_t space = 0;
    
    if (!real_free)
        FreedomPool<DEFAULT_GROW>::initialize_overrides();
    
#ifdef FREEDOM_DEBUG
    if (p) { space = bigpool.malloc_size(p); }
    if (space > 0) {
        total_alloc -= space;
    }
    if (space >= THRESH_DEBUG_PRINT)
        DEBUG_PRINTF(stderr, "free( %3lld ) : spc %3lld\n", (int64_t)(space/MBYTE), (int64_t)(total_alloc/MBYTE));
#endif
    bigpool.free(p);
    
}

/*
 size_t malloc_usable_size(const void *_Nullable ptr)
 {
 DEBUG_PRINTF(stderr, "malloc_usable_size( %ld )\n", (long)ptr);
 
 if (!FreedomPool::real_malloc_usable_size)
 FreedomPool::initialize_overrides();
 
 return bigpool.malloc_usable_size(ptr);
 }*/


size_t malloc_size(const void *_Nullable ptr)
{
    if (!real_malloc_size)
        FreedomPool<DEFAULT_GROW>::initialize_overrides();
    
    //  DEBUG_PRINTF(stderr, "malloc_size( %ld )\n", (long)ptr);
    
    return bigpool.malloc_size(ptr);
}

void *_Nullable realloc(void *_Nullable p, size_t new_size)
{
    if (!real_realloc)
        FreedomPool<DEFAULT_GROW>::initialize_overrides();
#ifdef FREEDOM_DEBUG
    // DEBUG_PRINTF(stderr, "realloc( %ld, %ld )\n", (long)p, new_size);
    int64_t space = 0;
    if (p) { space = bigpool.malloc_size(p); }
    
    void *_Nullable ret = NULL;
    ret = bigpool.realloc(p, new_size);
    
    // don't count freedompool extend
    if (ret) {
        total_alloc += new_size - space;
    }
#ifdef BREAK_ON_THRESH
    if (new_size >= THRESH_DEBUG_BREAK)
        DEBUGGER
#endif
        total_max_alloc = std::max(total_max_alloc, total_alloc);
    if (new_size >= THRESH_DEBUG_PRINT)
        DEBUG_PRINTF(stderr, "new( %3lld MB ) : spc %3lld MB\n", (int64_t)(new_size/MBYTE), (int64_t)(total_max_alloc/MBYTE));
    return ret;
#endif
    return bigpool.realloc(p, new_size);
}

void *_Nullable calloc(size_t count, size_t size)
{
    if (!real_calloc)
        FreedomPool<DEFAULT_GROW>::initialize_overrides();
#ifdef FREEDOM_DEBUG
    //  DEBUG_PRINTF(stderr, "calloc( %ld, %ld )\n", (long)count, size);
    size_t tot = size * count;
    total_alloc += tot;
    total_max_alloc = std::max(total_max_alloc, total_alloc);
#ifdef BREAK_ON_THRESH
    if (tot >= THRESH_DEBUG_BREAK)
        DEBUGGER
#endif
        if (tot >= THRESH_DEBUG_PRINT)
            DEBUG_PRINTF(stderr, "calloc( %3lld MB ) : spc %3lld MB\n",  (int64_t)(tot/MBYTE), (int64_t)(total_max_alloc/MBYTE));
#endif
    return bigpool.calloc(count, size);
}

#endif // DISABLE_MALLOC_FREE_OVERRIDE

#if !defined(DISABLE_NEWDELETE_OVERRIDE)

#ifdef __cplusplus

void * operator new(std::size_t n)
{
    if (!real_malloc)
        FreedomPool<DEFAULT_GROW>::initialize_overrides();
#ifdef FREEDOM_DEBUG
    total_alloc += n;
#ifdef BREAK_ON_THRESH
    if (n >= THRESH_DEBUG_BREAK)
        DEBUGGER
#endif
        total_max_alloc = std::max(total_max_alloc, total_alloc);
    
    if (n >= THRESH_DEBUG_PRINT)
        DEBUG_PRINTF(stderr, "new( %3lld MB ) : spc %3lld MB\n", (int64_t)(n/MBYTE), (int64_t)(total_alloc/MBYTE));
#endif
    
    return bigpool.malloc(n);
}

void operator delete(void *_Nullable p) throw()
{
    if (!real_free)
        FreedomPool<DEFAULT_GROW>::initialize_overrides();
#ifdef FREEDOM_DEBUG
    int64_t space = 0;
    if (p) { space = bigpool.malloc_size(p); }
    if (space > 0)
        total_alloc -= space;
    if (space >= THRESH_DEBUG_BREAK)
        DEBUG_PRINTF(stderr, "delete ( %3lld ) : spc %3lld\n", space/MBYTE, total_alloc/MBYTE);
#endif
    bigpool.free(p);
}

void *operator new[](std::size_t n)
{
    if (!real_malloc)
        FreedomPool<DEFAULT_GROW>::initialize_overrides();
    
#ifdef FREEDOM_DEBUG
    total_alloc += n;
    total_max_alloc = std::max(total_max_alloc, total_alloc);
    if (n >= THRESH_DEBUG_PRINT)
        DEBUG_PRINTF(stderr, " new[]( %3lld MB ) : spc %3lld MB\n", (int64_t)(n/MBYTE), (int64_t)(total_alloc/MBYTE));
#ifdef BREAK_ON_THRESH
    if (n >= THRESH_DEBUG_BREAK)
        DEBUGGER
#endif
#endif
        
        return bigpool.malloc(n);
}

void operator delete[](void *p) throw()
{
    if (!real_free)
        FreedomPool<DEFAULT_GROW>::initialize_overrides();
#ifdef FREEDOM_DEBUG
    int64_t space = 0;
    if (p) { space = bigpool.malloc_size(p); }
    if (space > 0)
        total_alloc -= space;
    if (space >= THRESH_DEBUG_BREAK)
        DEBUG_PRINTF(stderr, "delete[] ( %3lld ) : spc %3lld\n", space, total_alloc);
#endif
    bigpool.free(p); //: real_free(p);
}

#endif

#endif

