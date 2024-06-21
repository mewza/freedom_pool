//     freedom_pool.h v1.32 (C)2023-2024 Dmitry Bodlyrev
//
//      This is the most efficient block-pool memory management system you can find. I tried many before writing my own:
//      rpmalloc, tlsf, etc.
//     This code is partially based off this block allocator concept:
//     https://www.codeproject.com/Articles/1180070/Simple-Variable-Size-Memory-Block-Allocator
//
//     NEW (v1.31): Added a bunch of stuff, like the static memory allocation model which is very handy!
//             New in 1.31: I went over all the code again and fixed a ton of bugs, if it crashes even once, 
//             let me know plz. I will keep refining it until there are no more bugs!

#pragma once

#include <stdlib.h>
#include <assert.h>
#include <signal.h>

#include <dlfcn.h>

#ifdef __cplusplus
#include <map>
#include <iostream>
#endif

#include <dispatch/dispatch.h>
#include <dispatch/queue.h>
#include <assert.h>
#include <malloc/malloc.h>
#include "atomic.h"

// fprintf
#define DEBUG_PRINTF fprintf

//#define DISABLE_NEWDELETE_OVERRIDE
//#define DISABLE_MALLOC_FREE_OVERRIDE

// allocate on the stack (otherwise comment out)
#define FREEDOM_STACK_ALLOC

static const size_t DEFAULT_GROW   = 50 * 1048576; // 50 MB
static const size_t GROW_INCREMENT = 50 * 1048576; // 50 MB increment growth

#define MALLOC_V4SF_ALIGNMENT   64
#define TOKEN_ID                (uint64_t)'FREE'

typedef void *_Nullable (*real_malloc_ptr)(size_t);
typedef void *_Nullable (*real_calloc_ptr)(size_t count, size_t size);
typedef void (*real_free_ptr)(void *_Nullable p);
typedef void *_Nullable (*real_realloc_ptr)(void *_Nullable p, size_t new_size);
typedef size_t (*real_malloc_size_ptr)(const void *_Nullable ptr);
typedef size_t (*real_malloc_usable_size_ptr)(const void *_Nullable ptr);

extern real_malloc_ptr _Nullable real_malloc;
extern real_free_ptr _Nullable real_free;
extern real_calloc_ptr _Nullable real_calloc;
extern real_realloc_ptr _Nullable real_realloc;
extern real_malloc_size_ptr _Nullable real_malloc_size;
extern real_malloc_usable_size_ptr _Nullable real_malloc_usable_size;

extern "C" {
    size_t malloc_size(const void *_Nullable ptr);
    size_t malloc_usable_size(void *_Nullable ptr);
}

template <size_t poolsize = DEFAULT_GROW>
class FreedomPool
{
public:
    static const size_t MBYTE = 1048576;

    FreedomPool()
    {
        initialize_overrides();
        
        if (sizeof(size_t) != sizeof(uint64_t)) {
            fprintf(stderr, "FreedomPool(): sizeof(size_t) != sizoef(uint64_t) Terminating!\n");
            raise(SIGINT);
        }
        m_FreeBlocksByOffset.empty();
        m_FreeBlocksBySize.empty();
        
#ifndef FREEDOM_STACK_ALLOC
        m_Data = (int8_t*) real_malloc( 0 ) ;
#endif
        m_MaxSize = 0;
        m_FreeSize = m_MaxSize;

        m_Internal = true;
        ExtendPool(poolsize);
        
        printBlocks();
    }
    
    ~FreedomPool()
    {
#ifndef FREEDOM_STACK_ALLOC
        if (m_Data)
            real_free(m_Data);
        m_Data = NULL;
#endif
    }
    __inline bool IsFull() const { return m_FreeSize == 0; };
    __inline bool IsEmpty() const { return m_FreeSize == m_MaxSize; };
    __inline size_t GetMaxSize() const { return m_MaxSize; }
    __inline size_t GetFreeSize() const { return m_FreeSize; }
    __inline size_t GetUsedSize() const{ return m_MaxSize - m_FreeSize; }
    
    __inline size_t GetNumFreeBlocks() const {
        return m_FreeBlocksByOffset.size();
    }
    __inline size_t GetMaxFreeBlockSize() const {
        return !m_FreeBlocksBySize.empty() ? m_FreeBlocksBySize.rbegin()->first : 0;
    }
    
    static void initialize_overrides()
    {
        if (!real_malloc) {
            real_malloc = (real_malloc_ptr)dlsym(RTLD_NEXT, "malloc");
        }
        if (!real_free) {
            real_free = (real_free_ptr)dlsym(RTLD_NEXT, "free");
        }
        if (!real_calloc) {
            real_calloc = (real_calloc_ptr)dlsym(RTLD_NEXT, "calloc");
        }
        if (!real_realloc) {
            real_realloc = (real_realloc_ptr)dlsym(RTLD_NEXT, "realloc");
        }
        if (!real_malloc_size) {
            real_malloc_size = (real_malloc_size_ptr)dlsym(RTLD_NEXT, "malloc_size");
        }
        if (!real_malloc_usable_size) {
            real_malloc_usable_size = (real_malloc_usable_size_ptr)dlsym(RTLD_NEXT, "malloc_usable_size");
        }
    }
    
    void *_Nullable malloc(size_t nb_bytes)
    {
        if (!real_malloc) initialize_overrides();
        
         if ( !m_MaxSize || !m_Internal )
            return real_malloc(nb_bytes);

        if (GetFreeSize() < nb_bytes + 3 * sizeof(size_t)) {
#ifdef FREEDOM_STACK_ALLOC
            DEBUG_PRINTF(stderr, "FreedomPool::malloc() Ran out of space allocating %lld MB used %lld of %lld MB. Static model, returning NULL\n", nb_bytes/MBYTE, GetUsedSize()/MBYTE, GetMaxSize()/MBYTE, (GetUsedSize() + nb_bytes + 3 * sizeof(size_t) + GROW_INCREMENT) / MBYTE);
            return NULL;
#else
            DEBUG_PRINTF(stderr, "FreedomPool::malloc() Ran out of space allocating %lld MB used %lld of %lld MB, expanding to %lld MB\n", nb_bytes/MBYTE, GetUsedSize()/MBYTE, GetMaxSize()/MBYTE, (GetUsedSize() + nb_bytes + 3 * sizeof(size_t) + GROW_INCREMENT) / MBYTE);
            dispatch_async( dispatch_get_main_queue(), ^{
                // grow pool by GROW_INCREMENT MB + requested size
                ExtendPool( GetUsedSize() + nb_bytes + 3 * sizeof(size_t) + GROW_INCREMENT * MBYTE);
            });
#endif
        }
        return Malloc(nb_bytes);
    }
    
    void *_Nullable calloc(size_t count, size_t size)
    {
        if (!real_calloc) initialize_overrides();
        
        if ( !m_MaxSize || !m_Internal )
            return real_calloc(count, size);
        
        return Malloc(count * size);
    }
    
    void free(void *_Nullable p)
    {
        if (!real_free) initialize_overrides();
        if ( !m_MaxSize || !m_Internal) {
            real_free(p);
            return;
        }
        LOG("free(): m_Internal: %d m_MaxSize: %lld", m_Internal, m_MaxSize/MBYTE);
        if (p && *(uint64_t*)((int8_t*)p - sizeof(uint64_t*)) == TOKEN_ID)
            Free(p);
        else if (p) real_free(p);
    }
    
    size_t malloc_size(const void *_Nullable p)
    {
        if (!real_malloc_size) initialize_overrides();
        
        if ( !m_MaxSize || !m_Internal )
            return real_malloc_size(p);
        
        if (p && *(uint64_t*)((int8_t*)p - sizeof(uint64_t*)) == TOKEN_ID)
            return *((size_t *) p - 2);
        else
            return real_malloc_size(p);
    }
    
    void *_Nullable realloc(void *_Nullable p, size_t new_size)
    {
        void *new_p;
        size_t old_size = 0;
        
        if (!real_realloc) initialize_overrides();
        
        if ( !m_MaxSize || !m_Internal )
            return real_realloc(p, new_size);
        
        if (p && (*(uint64_t*)((int8_t*)p - sizeof(uint64_t*)) == TOKEN_ID))
        {
            old_size = *(size_t*)((void**)p - 2);
            if (old_size <= 0)
                return real_malloc(new_size);
            if (!(new_p = Malloc(new_size)))
                return nullptr;
            memcpy(new_p, p, old_size);
            Free(p);
            return new_p;
        } else
            return real_realloc(p, new_size);
    }
    
    size_t malloc_usable_size(const void *_Nullable p)
    {
        if (!real_malloc_usable_size) initialize_overrides();
        
        if ( !m_MaxSize || !m_Internal )
            return real_malloc_usable_size(p);
        
        return internal_malloc_usable_size(p);
    }
    
    size_t ExtendPool(size_t ExtraSize)
    {
#ifndef FREEDOM_STACK_ALLOC
        if (m_MaxSize) {
            DEBUG_PRINTF(stderr, "FreedomPool isn't allowed to extend passed initial in static allocation. Initially set to %ulld\n", ExtraSize/MBYTE);
            return m_MaxSize;
        }
#endif
        m_Lock.lock();
        m_Internal = true;
        
        size_t NewBlockOffset = m_MaxSize;
        size_t NewBlockSize   = ExtraSize;
        
        DEBUG_PRINTF(stderr, "Expanding FreedomPool internal size to: %lld MB\n", ExtraSize/MBYTE);
        if (!m_FreeBlocksByOffset.empty())
        {
            auto LastBlockIt = m_FreeBlocksByOffset.end();
            --LastBlockIt;
            
            const auto LastBlockOffset = LastBlockIt->first;
            const auto LastBlockSize   = LastBlockIt->second.Size;
            if (LastBlockOffset + LastBlockSize == m_MaxSize)
            {
                // Extend the last block
                NewBlockOffset = LastBlockOffset;
                NewBlockSize += LastBlockSize;
                
                //  VERIFY_EXPR(LastBlockIt->second.OrderBySizeIt->first == LastBlockSize &&
                //            LastBlockIt->second.OrderBySizeIt->second == LastBlockIt);
                m_FreeBlocksBySize.erase(LastBlockIt->second.OrderBySizeIt);
                m_FreeBlocksByOffset.erase(LastBlockIt);
            }
        }
        
        AddNewBlock(NewBlockOffset, NewBlockSize);
        
        m_MaxSize += ExtraSize;
        m_FreeSize += ExtraSize;
#ifndef FREEDOM_STACK_ALLOC
        m_Data = (int8_t*)real_realloc(m_Data, m_MaxSize);
#endif

#ifdef DILIGENT_DEBUG
        VERIFY_EXPR(m_FreeBlocksByOffset.size() == m_FreeBlocksBySize.size());
        if (!m_DbgDisableDebugValidation)
            DbgVerifyList();
#endif
        m_Internal = false;
        m_Lock.unlock();
        
        return m_MaxSize;
    }
    
    void printBlocks()
    {
        static int x = 0;
        if (!(x++%100))
            for (auto it = m_BlockCount.rbegin(); it != m_BlockCount.rend(); ++it) {
                std::cout << it->first << ": " << it->second << std::endl;
            }
    }
    
protected:
    
    struct FreeBlockInfo;
    // Type of the map that keeps memory blocks sorted by their offsets
    using TFreeBlocksByOffsetMap = std::map<size_t, FreeBlockInfo>;
    
    // Type of the map that keeps memory blocks sorted by their sizes
    using TFreeBlocksBySizeMap = std::multimap<size_t, typename TFreeBlocksByOffsetMap::iterator>;
    
    typedef struct FreeBlockInfo
    {
        FreeBlockInfo(size_t _Size) : Size(_Size) { }
        // Block size (no reserved space for the size of allocation)
        size_t Size;
        // Iterator referencing this block in the multimap sorted by the block size
        TFreeBlocksBySizeMap::iterator OrderBySizeIt;
    } FreeBlockInfo;
    
    __inline size_t internal_malloc_usable_size(const void *_Nullable p)
    {
        if (p && *(uint64_t*)((void**) p - 1) == TOKEN_ID)
            return *(size_t*)((void **) p - 2);
        else
            return real_malloc_usable_size(p);
    }
    
    __inline void *_Nullable internal_calloc(size_t count, size_t size)
    {
        return Malloc(count * size);
    }
    
    void AddNewBlock(size_t Offset, size_t Size)
    {
        auto NewBlockIt = m_FreeBlocksByOffset.emplace(Offset, Size);
        auto OrderIt = m_FreeBlocksBySize.emplace(Size, NewBlockIt.first);
        
        NewBlockIt.first->second.OrderBySizeIt = OrderIt;
    }
    
    void *_Nullable Malloc(size_t Size)
    {
        m_Lock.lock();
        m_Internal = true;
        
        Size += sizeof(uint64_t) * 3;
        
        if (m_FreeSize < Size) {
            m_Internal = false;
            m_Lock.unlock();
            return nullptr;
        }
        
        // Get the first block that is large enough to encompass Size bytes
        auto SmallestBlockItIt = m_FreeBlocksBySize.lower_bound(Size);
        if(SmallestBlockItIt == m_FreeBlocksBySize.end()) {
            m_Internal = true;
            m_Lock.unlock();
            return nullptr;
        }
        
        auto SmallestBlockIt = SmallestBlockItIt->second;
        auto Offset = SmallestBlockIt->first;
        auto NewOffset = Offset + Size;
        auto NewSize = SmallestBlockIt->second.Size - Size;
        
        m_FreeBlocksBySize.erase(SmallestBlockItIt);
        m_FreeBlocksByOffset.erase(SmallestBlockIt);
        
        if (NewSize > 0)
            AddNewBlock(NewOffset, NewSize);
        
        m_FreeSize -= Size;
        *(size_t*)(&m_Data[Offset]) = Offset;
        *(size_t*)(&m_Data[Offset + sizeof(uint64_t)]) = Size - 3 * sizeof(uint64_t);
        *(uint64_t*)(&m_Data[Offset + 2 * sizeof(uint64_t)]) = TOKEN_ID;

        m_Internal = false;
        m_Lock.unlock();
        
        return (void*)&m_Data[Offset + 3 * sizeof(uint64_t)];
    }
    
    void Free(void *_Nullable ptr)
    {
        m_Lock.lock();
        m_Internal = true;
        
        uint64_t Token = *((uint64_t*)ptr - 1);
        
        // something is out of alignment
        
        if (Token != TOKEN_ID) {
            DEBUG_PRINTF(stderr, "WARNING: Trying to internal_free non-native pointer, incorrect tokenID\n");
            m_Internal = false;
            m_Lock.unlock();
            return;
        }
        size_t Offset = (size_t)((uint64_t*)ptr - 3 * sizeof(uint64_t));
        size_t Size   = *(uint64_t*)((int8_t*)ptr - 2 * sizeof(uint64_t)) + 3 * sizeof(uint64_t);

        // Find the first element whose offset is greater than the specified offset
        auto NextBlockIt = m_FreeBlocksByOffset.upper_bound(Offset);
        auto PrevBlockIt = NextBlockIt;
        if(PrevBlockIt != m_FreeBlocksByOffset.begin())
            --PrevBlockIt;
        else
            PrevBlockIt = m_FreeBlocksByOffset.end();
        
        size_t NewSize, NewOffset;
        if(PrevBlockIt != m_FreeBlocksByOffset.end() && Offset == PrevBlockIt->first + PrevBlockIt->second.Size)
        {
            NewSize = PrevBlockIt->second.Size + Size;
            NewOffset = PrevBlockIt->first;
            
            if (NextBlockIt != m_FreeBlocksByOffset.end() && Offset + Size == NextBlockIt->first)
            {
                NewSize += NextBlockIt->second.Size;
                m_FreeBlocksBySize.erase(PrevBlockIt->second.OrderBySizeIt);
                m_FreeBlocksBySize.erase(NextBlockIt->second.OrderBySizeIt);
                // Delete the range of two blocks
                ++NextBlockIt;
                m_FreeBlocksByOffset.erase(PrevBlockIt, NextBlockIt);
            } else
            {
                m_FreeBlocksBySize.erase(PrevBlockIt->second.OrderBySizeIt);
                m_FreeBlocksByOffset.erase(PrevBlockIt);
            }
        } else if (NextBlockIt != m_FreeBlocksByOffset.end() && Offset + Size == NextBlockIt->first)
        {
            NewSize = Size + NextBlockIt->second.Size;
            NewOffset = Offset;
            m_FreeBlocksBySize.erase(NextBlockIt->second.OrderBySizeIt);
            m_FreeBlocksByOffset.erase(NextBlockIt);
        } else
        {
            NewSize = Size;
            NewOffset = Offset;
        }
        
        AddNewBlock(NewOffset, NewSize);
        m_FreeSize += Size;
        
        m_Internal = false;
        m_Lock.unlock();
    }
    
#ifdef FREEDOM_STACK_ALLOC
    int8_t                  m_Data[poolsize];
#else
    int8_t                  *m_Data;
#endif
    size_t                  m_MaxSize, m_FreeSize;
    TFreeBlocksByOffsetMap  m_FreeBlocksByOffset;
    TFreeBlocksBySizeMap    m_FreeBlocksBySize;
    
    std::map<size_t, int>  m_BlockCount;
    AtomicLock              m_Lock;
    volatile bool           m_Internal;
};

#if !defined(DISABLE_NEWDELETE_OVERRIDE)

#ifdef __cplusplus

void *_Nullable operator new(std::size_t n);
void operator delete(void *_Nullable p) throw();
void *_Nullable operator new[](std::size_t n);
void operator delete[](void *_Nullable p) throw();

#endif // __cplusplus

#endif // DISABLE_NEWDELETE_OVERRIDE

void reset_freedom_counters();

extern FreedomPool<DEFAULT_GROW> bigpool;


