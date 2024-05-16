// freedom_pool.cpp v1.22 (C)2023-2024 Dmitry Bodlyrev
//
// This is the most efficient block-pool memory management system you can find. I tried many before writing my own:
// rpmalloc, tlsf, many tlsf clones.
//
// I tested it live in a multimedia rich project I am working on that has OpenGL, and audio DSP, it works great no
// weird crashes. Solid.
//
// C++ wrappers for C++ new/new[]/delete/delete[]

#include <malloc/malloc.h>
#include <signal.h>

#include "freedom_pool.h"

real_malloc_ptr FreedomPool::real_malloc = NULL;
real_calloc_ptr FreedomPool::real_calloc = NULL;
real_free_ptr FreedomPool::real_free = NULL;
real_realloc_ptr FreedomPool::real_realloc = NULL;
real_malloc_size_ptr FreedomPool::real_malloc_size = NULL;
real_malloc_usable_size_ptr FreedomPool::real_malloc_usable_size = NULL;

static int64_t total_alloc = 0L;
static int64_t total_max_alloc = 0L;

//#define FREEDOM_DEBUG

// #define BREAK_ON_THRESH

#define MBYTE               1048576L

#define THRESH_DEBUG_BREAK  19L * MBYTE
#define THRESH_DEBUG_PRINT  1L * MBYTE

#define DEBUGGER_BREAK      raise(SIGINT);

void reset_freedom_counters() {
    total_alloc = 0;
    total_max_alloc = 0;
}
#if !defined(DISABLE_MALLOC_FREE_OVERRIDE)

void *_Nullable malloc(size_t nb_bytes)
{
    if (!FreedomPool::real_malloc)
        FreedomPool::initialize_overrides();
#ifdef FREEDOM_DEBUG
   // DEBUG_PRINTF(stderr, "malloc( %ld )\n", nb_bytes);
    total_alloc += nb_bytes;
 
    if (total_alloc > total_max_alloc) {
        if (nb_bytes >= THRESH_DEBUG_PRINT)
            DEBUG_PRINTF(stderr, "malloc( %2ld MB ) max: %3lld MB\n", nb_bytes/MBYTE, total_max_alloc/MBYTE);
        total_max_alloc = total_alloc;
    }
#endif
    return bigpool.malloc(nb_bytes);
}

void free(void *_Nullable p)
{
    int64_t space = 0;

    if (!FreedomPool::real_free)
        FreedomPool::initialize_overrides();
    
    // DEBUG_PRINTF(stderr, "free( %ld )\n", nb_bytes);
#ifdef FREEDOM_DEBUG
    if (p) { space = bigpool.malloc_size(p); bigpool.free(p); }
    if (space > 0) {
        total_alloc -= space;
    }
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
    if (!FreedomPool::real_malloc_size)
        FreedomPool::initialize_overrides();
    
   //  DEBUG_PRINTF(stderr, "malloc_size( %ld )\n", (long)ptr);

    return bigpool.malloc_size(ptr);
}

void *_Nullable realloc(void *_Nullable p, size_t new_size)
{
    if (!FreedomPool::real_realloc)
        FreedomPool::initialize_overrides();
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
    if (total_alloc > total_max_alloc)
    {
        if (new_size >= THRESH_DEBUG_PRINT)
            DEBUG_PRINTF(stderr, "   new( %2ld MB ) max: %3lld MB\n", (long)(new_size/MBYTE), (long)(total_max_alloc/MBYTE));
#ifdef BREAK_ON_THRESH
        if (new_size >= THRESH_DEBUG_BREAK)
            DEBUGGER_BREAK
#endif
            total_max_alloc = total_alloc;
    }
    return ret;
#endif
    return bigpool.realloc(p, new_size);
}

void *_Nullable calloc(size_t count, size_t size)
{
    if (!FreedomPool::real_calloc)
        FreedomPool::initialize_overrides();
#ifdef FREEDOM_DEBUG
    //  DEBUG_PRINTF(stderr, "calloc( %ld, %ld )\n", (long)count, size);
    size_t tot = size * count;

    total_alloc += tot;
    if (total_alloc > total_max_alloc) 
    {
        if (tot >= THRESH_DEBUG_PRINT)
            DEBUG_PRINTF(stderr, "calloc( %2ld MB ) max: %3lld MB\n", (long)(tot/MBYTE), (long)(total_max_alloc/MBYTE));
#ifdef BREAK_ON_THRESH
        if (tot >= THRESH_DEBUG_BREAK)
            DEBUGGER_BREAK
#endif
        total_max_alloc = total_alloc;
    }
#endif
    return bigpool.calloc(count, size);
}
#endif

#if !defined(DISABLE_NEWDELETE_OVERRIDE)

void * operator new(std::size_t n) 
{
    if (!FreedomPool::real_malloc)
        FreedomPool::initialize_overrides();
#ifdef FREEDOM_DEBUG
    total_alloc += n;
    if (total_alloc > total_max_alloc) {
        if (n >= THRESH_DEBUG_PRINT)
            DEBUG_PRINTF(stderr, "   new( %2ld MB ) max: %3lld MB\n", (long)(n/MBYTE), (size_t)(total_max_alloc/MBYTE));
#ifdef BREAK_ON_THRESH
        if (n >= THRESH_DEBUG_BREAK)
            DEBUGGER_BREAK
#endif
            total_max_alloc = total_alloc;
    }
#endif
    
    return bigpool.malloc(n);
}

void operator delete(void *_Nullable p) throw()
{
    if (!FreedomPool::real_free)
        FreedomPool::initialize_overrides();
#ifdef FREEDOM_DEBUG
    int64_t space = 0;
    if (p) { space = bigpool.malloc_size(p); }
    //DEBUG_PRINTF(stderr, "delete ( %x )\n", p);
    if (space > 0)
        total_alloc -= space;
#endif
    bigpool.free(p);
}

void *operator new[](std::size_t n) 
{
    if (!FreedomPool::real_malloc)
        FreedomPool::initialize_overrides();
    
#ifdef FREEDOM_DEBUG
    total_alloc += n;
    if (total_alloc > total_max_alloc) {
       if (n >= THRESH_DEBUG_PRINT)
            DEBUG_PRINTF(stderr, " new[]( %2ld MB ) max: %3lld MB\n", (long)(n/MBYTE), (long)(total_max_alloc/MBYTE));
#ifdef BREAK_ON_THRESH
        if (n >= THRESH_DEBUG_BREAK)
            DEBUGGER_BREAK
#endif
        total_max_alloc = total_alloc;
    }
#endif
    
    return bigpool.malloc(n);
}

void operator delete[](void *p) throw()
{
    if (!FreedomPool::real_free)
        FreedomPool::initialize_overrides();
#ifdef FREEDOM_DEBUG
    int64_t space = 0;
    if (p) { space = bigpool.malloc_size(p); }
  //  DEBUG_PRINTF(stderr, "delete[] ( %x )\n", p);
   
    if (space > 0)
        total_alloc -= space;
#endif
    bigpool.free(p);
}

#endif
