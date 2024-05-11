// freedom_pool.h v1.2 (C)2023-2024 Dmitry Bodlyrev
//
// This is the most efficient block-pool memory management system you can find. I tried many before writing my own:
// rpmalloc, tlsf, etc.
//
// I tested it live in a multimedia rich project I am working on that has OpenGL, and audio DSP, it works great no
// weird crashes. Solid.
//
// This code is partially based off this block allocator concept:
// https://www.codeproject.com/Articles/1180070/Simple-Variable-Size-Memory-Block-Allocator

#ifndef H_FREEDOM_POOL
#define H_FREEDOM_POOL

#include <stdlib.h>
#include <assert.h>

#include <dlfcn.h>

#ifdef __cplusplus
#include <map>
#include <iostream>
#endif

#include <dispatch/dispatch.h>
#include <dispatch/queue.h>
#include <assert.h>
#include <malloc/malloc.h>
#include "atomic_lock.h"

// fprintf
#define DEBUG_PRINTF

//#define DISABLE_NEWDELETE_OVERRIDE
//#define DISABLE_MALLOC_FREE_OVERRIDE

#define MALLOC_V4SF_ALIGNMENT   64
#define TOKEN_ID                ((void*)'MAGI')

typedef void *_Nullable (*real_malloc_ptr)(size_t);
typedef void *_Nullable (*real_calloc_ptr)(size_t count, size_t size);
typedef void (*real_free_ptr)(void *_Nonnull p);
typedef void *_Nullable (*real_realloc_ptr)(void *_Nonnull p, size_t new_size);
typedef size_t (*real_malloc_size_ptr)(const void *_Nonnull ptr);
typedef size_t (*real_malloc_usable_size_ptr)(const void *_Nonnull ptr);

extern "C" {
    size_t malloc_size(const void *_Nonnull ptr);
    size_t malloc_usable_size(void *_Nonnull ptr);
}

class FreedomPool
{
    enum {
        InvalidOffset = -1
    };
public:
    
    static real_malloc_ptr _Nullable real_malloc;
    static real_free_ptr _Nullable real_free;
    static real_calloc_ptr _Nullable real_calloc;
    static real_realloc_ptr _Nullable real_realloc;
    static real_malloc_size_ptr _Nullable real_malloc_size;
    static real_malloc_usable_size_ptr _Nullable real_malloc_usable_size;

    FreedomPool()
    {
        m_Internal = true;
        m_FreeBlocksByOffset.empty();
        m_FreeBlocksBySize.empty();
        
        initialize_overrides();
        
        m_MaxSize = 0;
        m_FreeSize = m_MaxSize;
        m_Lock.init();
        
        m_Data = (int8_t*) real_malloc( m_MaxSize );
        ExtendPool( 1024L * 1024L); // 1MB
    }
    
    ~FreedomPool()
    {
        if (m_Data)
            internal_free(m_Data);
        m_Data = nullptr;
        m_Lock.exit();
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

    void *_Nullable calloc(size_t count, size_t size)
    {
        if (m_Internal || !m_MaxSize)
            return aligned_calloc(count, size);
      
        return internal_malloc(count * size);
    }
    
    void *_Nullable malloc(size_t nb_bytes)
    {
        if (m_Internal || !m_MaxSize)
            return aligned_malloc(nb_bytes);
        
       if (nb_bytes <= 0) {
           DEBUG_PRINTF(stderr, "FreedomPool: nb_bytes <= 0\n");
           assert( nb_bytes <= 0 );
       }
        size_t grow = GetMaxSize() - (GetUsedSize() + nb_bytes + sizeof(size_t));
        if (  grow < 0 ) {
            DEBUG_PRINTF(stderr, "FreedomPool(): trying to grow beyond boundaries: %lld Current: %lld\n", -grow, GetMaxSize());
            ExtendPool( GetMaxSize() - grow );
        }
        return internal_malloc(nb_bytes);
    }
    
    void *_Nullable realloc(void *_Nonnull p, size_t new_size)
    {
        void *new_p;
        size_t old_size;
        
        if (m_Internal || !m_MaxSize)
            return aligned_realloc(p, new_size);
        
        if (!p) return nullptr;
        old_size = internal_malloc_size(p);
        if (old_size <= 0)
            return nullptr;
        if (!(new_p = internal_malloc(new_size)))
            return nullptr;
        memcpy(new_p, p, old_size);
        
        internal_free(p);
        
        return new_p;
    }

    size_t malloc_size(const void *_Nonnull p)
    {
        return internal_malloc_size(p);
    }
    
    size_t malloc_usable_size(const void *_Nonnull p)
    {
        return internal_malloc_usable_size(p);
    }
    
    void free(void *_Nonnull p)
    {
        if (m_Internal || !m_MaxSize)
            aligned_free(p);
        else
           internal_free(p);
    }
    
     void ExtendPool(size_t ExtraSize)
     {
         m_Lock.lock();
         size_t NewBlockOffset = m_MaxSize;
         size_t NewBlockSize   = ExtraSize;

         DEBUG_PRINTF(stderr, "WARNING! Extending pool to: %lld MB\n", ExtraSize/1024L/1024L);
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

         m_Data = (int8_t*)aligned_realloc(m_Data, m_MaxSize);
         assert(m_Data != nullptr);
         
 #ifdef DILIGENT_DEBUG
         VERIFY_EXPR(m_FreeBlocksByOffset.size() == m_FreeBlocksBySize.size());
         if (!m_DbgDisableDebugValidation)
             DbgVerifyList();
 #endif
         m_Lock.unlock();
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
    using TFreeBlocksBySizeMap = std::multimap<size_t, TFreeBlocksByOffsetMap::iterator>;
     
    typedef struct FreeBlockInfo
    {
        FreeBlockInfo(size_t _Size) : Size(_Size) { }
        // Block size (no reserved space for the size of allocation)
        size_t Size;
        // Iterator referencing this block in the multimap sorted by the block size
        TFreeBlocksBySizeMap::iterator OrderBySizeIt;
    } FreeBlockInfo;

    
    __inline size_t aligned_malloc_size(const void *_Nonnull p)
    {
        if (*((void **) p - 1) == TOKEN_ID)
            return real_malloc_size(*((void **) p - 2));
        else
            return real_malloc_size(p);
    }
    
    __inline size_t aligned_malloc_usable_size(const void *_Nonnull p)
    {
        if (*((void **) p - 1) == TOKEN_ID)
            return real_malloc_usable_size(*((void **) p - 2));
        else
            return real_malloc_usable_size(p);
    }
    
    __inline void *_Nullable aligned_malloc(size_t nb_bytes)
    {
        void *p, *p0 = real_malloc(nb_bytes + MALLOC_V4SF_ALIGNMENT);
        if (!p0) return nullptr;
        
        p = (void *) (((size_t) p0 + MALLOC_V4SF_ALIGNMENT) & (~((size_t) (MALLOC_V4SF_ALIGNMENT-1))));
        *((void **) p - 2) = p0;
        *((void **) p - 1) = TOKEN_ID;
        
        return p;
    }

    __inline void *_Nullable aligned_realloc(void *_Nonnull p, size_t newSize)
    {
        if (!p) return real_realloc(p, newSize);
        if (*((void **) p - 1) == TOKEN_ID)
            return real_realloc(*((void **) p - 2), newSize);
        else return real_realloc(p, newSize);
    }
    
    __inline void aligned_free(void *_Nonnull p)
    {
        if (p) {
            if (*((void **) p - 1) == TOKEN_ID)
                real_free(*((void **) p - 2));
            else
                real_free(p);
        }
    }
    
    __inline void *_Nullable aligned_calloc(size_t count, size_t size)
    {
        return aligned_malloc(count * size);
    }
    
    void AddNewBlock(size_t Offset, size_t Size)
    {
        auto NewBlockIt = m_FreeBlocksByOffset.emplace(Offset, Size);
        auto OrderIt = m_FreeBlocksBySize.emplace(Size, NewBlockIt.first);
       
        NewBlockIt.first->second.OrderBySizeIt = OrderIt;
    }
    
    void *_Nullable internal_malloc(size_t Size)
    {
        m_Lock.lock();
        m_Internal = true;
        
        Size += sizeof(size_t);
       
        if (m_FreeSize < Size) {
            m_Internal = false;
            m_Lock.unlock();
            return nullptr;
        }
     
        // Get the first block that is large enough to encompass Size bytes
        auto SmallestBlockItIt = m_FreeBlocksBySize.lower_bound(Size);
        if(SmallestBlockItIt == m_FreeBlocksBySize.end()) {
            m_Internal = false;
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
        *(size_t*)&m_Data[Offset] = Size - sizeof(size_t);
 
        m_Internal = false;
        m_Lock.unlock();
 
        return (void*)&m_Data[Offset + sizeof(size_t)];
    }

    size_t internal_malloc_size(const void *_Nonnull ptr)
    {
      //  return real_malloc_size(ptr);
        size_t Size = *((size_t*)ptr - 1);
        return Size;
    }
    
    size_t internal_malloc_usable_size(const void *_Nonnull ptr)
    {
       // return real_malloc_usable_size(ptr);
        size_t Size = *((size_t*)ptr - 1);
        return Size;
    }
    void internal_free(void *_Nullable ptr)
    {
        m_Lock.lock();
        m_Internal = true;
        
        size_t Offset = (size_t)((int8_t*)ptr - m_Data - sizeof(size_t));
        size_t Size = *(size_t*)((int8_t*)ptr - sizeof(size_t)) + sizeof(size_t);
        
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

   
    int8_t       *_Nullable m_Data;
    size_t                  m_MaxSize, m_FreeSize;
    TFreeBlocksByOffsetMap  m_FreeBlocksByOffset;
    TFreeBlocksBySizeMap    m_FreeBlocksBySize;
    
    std::map<size_t, int>   m_BlockCount;
    AtomicLock              m_Lock;
    volatile bool           m_Internal;
};

extern FreedomPool bigpool;

#if !defined(DISABLE_NEWDELETE_OVERRIDE)

#ifdef __cplusplus

void *_Nullable operator new(std::size_t n);
void operator delete(void *_Nullable p) throw();
void *_Nullable operator new[](std::size_t n);
void operator delete[](void *_Nullable p) throw();

#endif // __cplusplus

#endif // DISABLE_NEWDELETE_OVERRIDE

#endif // H_DYN_ALLOC
