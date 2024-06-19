// freedom_pool.h v1.3 (C)2023-2024 Dmitry Bodlyrev
//
// This is the most efficient block-pool memory management system you can find. I tried many before writing my own:
// rpmalloc, tlsf, etc.
// This code is partially based off this block allocator concept:
// https://www.codeproject.com/Articles/1180070/Simple-Variable-Size-Memory-Block-Allocator
//
// NEW (v1.3): Added a bunch of stuff as you can tell, like the static memory allocation model is very handy,
//             but you cannot grow it dynamically, only initially. Enjoy

#pragma once

#include <stdlib.h>
#include <assert.h>
#include <pthread/pthread.h>
#include <dlfcn.h>

#ifdef __cplusplus
#include <map>
#include <iostream>
#endif

#include <dispatch/dispatch.h>
#include <dispatch/queue.h>
#include <assert.h>
#include <malloc/malloc.h>

// fprintf
#define DEBUG_PRINTF fprintf

//#define DISABLE_NEWDELETE_OVERRIDE
//#define DISABLE_MALLOC_FREE_OVERRIDE

// allocate on the stack (otherwise comment out)
#define FREEDOM_STACK_ALLOC

static const int DEFAULT_GROW = 1000 * 1048576L; // 60 MB

#define MALLOC_V4SF_ALIGNMENT   64
#define TOKEN_ID                (int64_t)'FREE'

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


class AtomicLock {
public:
    AtomicLock() {
        init();
    }
    void init() {
        pthread_mutex_init( &_mutex, NULL);
    }
    void exit() {
        pthread_mutex_destroy(&_mutex);
    }
    __inline void lock() {
        pthread_mutex_lock(&_mutex);
    }
    __inline unsigned trylock() {
        return pthread_mutex_trylock(&_mutex);
    }
    __inline void unlock() {
        pthread_mutex_unlock(&_mutex);
    }
    __inline pthread_mutex_t *P() {
        return &_mutex;
    }
    ~AtomicLock() {
        exit();
    }
protected:
    pthread_mutex_t _mutex;
};
template <int64_t poolsize = DEFAULT_GROW>
class FreedomPool
{
    enum {
        InvalidOffset = -1
    };
    
public:
    static const int64_t MBYTE = 1048576;

    FreedomPool()
    {
        initialize_overrides();
        m_Lock.init();
        
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
        m_Lock.exit();
    }
    __inline bool IsFull() const { return m_FreeSize == 0; };
    __inline bool IsEmpty() const { return m_FreeSize == m_MaxSize; };
    __inline int64_t GetMaxSize() const { return m_MaxSize; }
    __inline int64_t GetFreeSize() const { return m_FreeSize; }
    __inline int64_t GetUsedSize() const{ return m_MaxSize - m_FreeSize; }
    
    __inline int64_t GetNumFreeBlocks() const {
        return m_FreeBlocksByOffset.size();
    }
    __inline int64_t GetMaxFreeBlockSize() const {
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
    
    void *_Nullable malloc(int64_t nb_bytes)
    {
         if ( !m_MaxSize || !m_Internal )
            return real_malloc(nb_bytes);
    
        if (GetFreeSize() < nb_bytes + 3 * sizeof(int64_t)) {
       // int64_t grow = GetMaxSize() - (GetUsedSize() + nb_bytes + 3 * sizeof(int64_t));
            DEBUG_PRINTF(stderr, "FreedomPool::malloc(): No space left trying to allocate %lld MB used %lld of %lld MB\n", nb_bytes/MBYTE, GetUsedSize()/MBYTE, GetMaxSize()/MBYTE);
            return NULL;
        //ExtendPool( GetMaxSize() - grow );
        }
        return Malloc(nb_bytes);
    }
    
    void *_Nullable calloc(int64_t count, int64_t size)
    {
        if ( !m_MaxSize || !m_Internal )
            return real_calloc(count, size);
        else
            return Malloc(count * size);
    }
    
    void free(void *_Nullable p)
    {
        if ( !m_MaxSize || !m_Internal)
            real_free(p);
        else if (p && *((int64_t*)p - 1) == TOKEN_ID)
            Free(p);
    }
    
    int64_t malloc_size(const void *_Nullable p)
    {
        return internal_malloc_size(p);
    }
    
    void *_Nullable realloc(void *_Nullable p, int64_t new_size)
    {
        void *new_p;
        int64_t old_size = 0;
        
        if ( !m_MaxSize || !m_Internal )
            return real_realloc(p, new_size);
        
        if (p && (*((int64_t*)p - 1) == TOKEN_ID))
        {
            if (p) old_size = internal_malloc_size(p);
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
    
    int64_t malloc_usable_size(const void *_Nullable p)
    {
        return internal_malloc_usable_size(p);
    }
    
    size_t ExtendPool(size_t ExtraSize)
    {
#ifndef FREEDOM_STACK_ALLOC
        if (m_MaxSize) {
            DEBUG_PRINTF(stderr, "FreedomPool isn't allowed to extend passed initial in static allocation. Initially set to %lld\n", ExtraSize/MBYTE);
            return m_MaxSize;
        }
#endif
        m_Lock.lock();
       // m_Internal = false;
        
        int64_t NewBlockOffset = m_MaxSize;
        int64_t NewBlockSize   = ExtraSize;
        
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
      //  m_Internal = true;
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
    using TFreeBlocksByOffsetMap = std::map<int64_t, FreeBlockInfo>;
    
    // Type of the map that keeps memory blocks sorted by their sizes
    using TFreeBlocksBySizeMap = std::multimap<int64_t, typename TFreeBlocksByOffsetMap::iterator>;
    
    typedef struct FreeBlockInfo
    {
        FreeBlockInfo(int64_t _Size) : Size(_Size) { }
        // Block size (no reserved space for the size of allocation)
        int64_t Size;
        // Iterator referencing this block in the multimap sorted by the block size
        TFreeBlocksBySizeMap::iterator OrderBySizeIt;
    } FreeBlockInfo;
    
    int64_t internal_malloc_size(const void *_Nullable p)
    {
        if ( !m_Internal )
            return real_malloc_size(p);
        int64_t token = *(int64_t*)((void **) p - 1) ;
        if ( token == TOKEN_ID)
            return *((int64_t *) p - 2);
        else
            return real_malloc_size(p);
    }
    
    __inline int64_t internal_malloc_usable_size(const void *_Nullable p)
    {
        if (m_Internal && *((int64_t *) p - 1) == TOKEN_ID)
            return *((int64_t *) p - 2);
        else
            return real_malloc_usable_size(p);
    }
    
    __inline void *_Nullable internal_calloc(int64_t count, int64_t size)
    {
        return Malloc(count * size);
    }
    
    void AddNewBlock(int64_t Offset, int64_t Size)
    {
        auto NewBlockIt = m_FreeBlocksByOffset.emplace(Offset, Size);
        auto OrderIt = m_FreeBlocksBySize.emplace(Size, NewBlockIt.first);
        
        NewBlockIt.first->second.OrderBySizeIt = OrderIt;
    }
    
    void *_Nullable Malloc(int64_t Size)
    {
        m_Lock.lock();
        m_Internal = false;
        
        Size += sizeof(int64_t) * 4;
        
        if (m_FreeSize < Size) {
            m_Internal = true;
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
        *(int64_t*)(&m_Data[Offset]) = Offset;
        *(int64_t*)(&m_Data[Offset + sizeof(int64_t)]) = Size - 3 * sizeof(int64_t);
        *(int64_t*)(&m_Data[Offset + 2 * sizeof(int64_t)]) = TOKEN_ID;

        m_Internal = true;
        m_Lock.unlock();
        
        return (void*)&m_Data[Offset + 3 * sizeof(int64_t)];
    }
    
    void Free(void *_Nullable ptr)
    {
        m_Lock.lock();
        m_Internal = false;
        
        int64_t Token = *((int64_t*)ptr - 1);
        
        // something is out of alignment
        
        if (Token != TOKEN_ID) {
            DEBUG_PRINTF(stderr, "WARNING: Trying to internal_free non-native pointer, incorrect tokenID\n");
            m_Internal = true;
            m_Lock.unlock();
            return;
        }
        int64_t Offset = (int64_t)((int8_t*)ptr - m_Data - 3 * sizeof(int64_t));
        int64_t Size   = *(int64_t*)((int8_t*)ptr - 2 * sizeof(int64_t)) + 3 * sizeof(int64_t);

        // Find the first element whose offset is greater than the specified offset
        auto NextBlockIt = m_FreeBlocksByOffset.upper_bound(Offset);
        auto PrevBlockIt = NextBlockIt;
        if(PrevBlockIt != m_FreeBlocksByOffset.begin())
            --PrevBlockIt;
        else
            PrevBlockIt = m_FreeBlocksByOffset.end();
        
        int64_t NewSize, NewOffset;
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
        
        m_Internal = true;
        m_Lock.unlock();
    }
    
#ifdef FREEDOM_STACK_ALLOC
    int8_t                  m_Data[poolsize];
#else
    int8_t                  *m_Data;
#endif
    int64_t                 m_MaxSize, m_FreeSize;
    TFreeBlocksByOffsetMap  m_FreeBlocksByOffset;
    TFreeBlocksBySizeMap    m_FreeBlocksBySize;
    
    std::map<int64_t, int>  m_BlockCount;
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

