// freedom_pool.h v1.1 ©2023 Dmitry Bodlyrev
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

#ifdef __cplusplus
#include <map>
#include <iostream>
#endif

#include <dispatch/dispatch.h>
#include <dispatch/queue.h>
#include <assert.h>
#include <malloc/malloc.h>
#include "atomic_lock.h"

//#define DEBUG fprintf
#define DEBUG_PRINTF

class FreedomPool
{
    enum {
        InvalidOffset = -1
    };
public:
    FreedomPool()
    {
        internal = true;
        
        (void)m_FreeBlocksByOffset.empty();
        (void)m_FreeBlocksBySize.empty();
        
        m_MaxSize = 0;
        m_FreeSize = m_MaxSize;
        m_Lock.init();
        
        m_Data = (uint8_t*)_malloc64( m_MaxSize );
       // ExtendPool( 1024L ); // 1MB
    }
    ~FreedomPool()
    {
        if (m_Data)
            _free64(m_Data);
        m_Data = NULL;
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
    
    void *calloc(size_t count, size_t size)
    {
        if (internal || !m_MaxSize)
            return _malloc64(count * size);
        else
            return malloc(count * size);
    }
    
    void *malloc(int64_t nb_bytes)
    {
        if (internal || !m_MaxSize) {
            return _malloc64(nb_bytes);
        }
       if (nb_bytes <= 0) {
           DEBUG_PRINTF(stderr, "FreedomPool: nb_bytes <= 0\n");
           assert( nb_bytes <= 0 );
       }
        //dispatch_sync( dispatch_get_main_queue(), (dispatch_block_t)^{
        int64_t grow = GetMaxSize() - (GetUsedSize() + nb_bytes + sizeof(int64_t));
        if (  grow < 0 ) {
            DEBUG_PRINTF(stderr, "FreedomPool(): trying to grow beyond boundaries: %lld Current: %lld\n", -grow, GetMaxSize());
            ExtendPool( GetMaxSize() - grow );
        }
        return malloc_internal(nb_bytes);
    }
    
    void *realloc(void *p, int64_t new_size)
    {
        void *new_p;
        
        if (internal || !m_MaxSize)
            return _realloc64(p, new_size);
        
        if (!p) return NULL;
        if (!(new_p = malloc(new_size)))
            return NULL;
        int64_t old_size = *(int64_t*)((uint8_t*)p - sizeof(int64_t));
        memcpy(new_p, p, old_size);
        free(p);
        
        return new_p;
    }

    void free(void *p)
    {
        if (internal || !m_MaxSize) {
            _free64(p);
            return;
        }
    //    if (p) free_internal(p);
     if (p) free_internal(*((void **) p - 1));
    }
    
     void ExtendPool(int64_t ExtraSize)
     {
         m_Lock.lock();
         int64_t NewBlockOffset = m_MaxSize;
         int64_t NewBlockSize   = ExtraSize;

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

         m_Data = (uint8_t*)_realloc64(m_Data, m_MaxSize);
         assert(m_Data != NULL);
         
         
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
            for (auto it = block_count.rbegin(); it != block_count.rend(); ++it) {
                std::cout << it->first << ": " << it->second << std::endl;
            }
    }
protected:
   
    struct FreeBlockInfo;
    // Type of the map that keeps memory blocks sorted by their offsets
    using TFreeBlocksByOffsetMap = std::map<int64_t, FreeBlockInfo>;
     
    // Type of the map that keeps memory blocks sorted by their sizes
    using TFreeBlocksBySizeMap = std::multimap<int64_t, TFreeBlocksByOffsetMap::iterator>;
     
    typedef struct FreeBlockInfo
    {
        FreeBlockInfo(int64_t _Size) : Size(_Size) { }
        // Block size (no reserved space for the size of allocation)
        int64_t Size;
        // Iterator referencing this block in the multimap sorted by the block size
        TFreeBlocksBySizeMap::iterator OrderBySizeIt;
    } FreeBlockInfo;

    void AddNewBlock(int64_t Offset, int64_t Size)
    {
        auto NewBlockIt = m_FreeBlocksByOffset.emplace(Offset, Size);
        auto OrderIt = m_FreeBlocksBySize.emplace(Size, NewBlockIt.first);
       
        NewBlockIt.first->second.OrderBySizeIt = OrderIt;
    }
    
    void *malloc_internal(int64_t Size)
    {
        m_Lock.lock();
        internal = true;
        Size += sizeof(int64_t);
       
        if (m_FreeSize < Size) {
            internal = false;
            m_Lock.unlock();
            return NULL;
        }
     
        // Get the first block that is large enough to encompass Size bytes
        auto SmallestBlockItIt = m_FreeBlocksBySize.lower_bound(Size);
        if(SmallestBlockItIt == m_FreeBlocksBySize.end()) {
            internal = false;
            m_Lock.unlock();
            return NULL;
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
        *(int64_t*)&m_Data[Offset] = Size - sizeof(int64_t);
 
        internal = false;
        m_Lock.unlock();
 
        return (void*)&m_Data[Offset+sizeof(int64_t)];
    }

    void free_internal(void *ptr)
    {
        m_Lock.lock();
        internal = true;
        
        int64_t Offset = (int64_t)((uint8_t*)ptr - m_Data - sizeof(int64_t));
        int64_t Size = *(int64_t*)((uint8_t*)ptr - sizeof(int64_t)) + sizeof(int64_t);
        
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
        internal = false;
        m_Lock.unlock();

    }
    void *_malloc64(int64_t nb_bytes)
    {
        void *p, *p0 = ::malloc(nb_bytes + 64);
        if (!p0) return (void *) 0;
        ::memset(p0, 0, nb_bytes + 64);
        p = (void *) (((size_t) p0 + 64) & (~((size_t)63)));
        *((void **) p - 1) = p0;
        return p;
    }
    
    void *_realloc64(void *p, int64_t nb_bytes)
    {
        if (!p) return NULL;
        
        void *pp = _malloc64(nb_bytes);
        size_t sz = _msize64(p);
        if (pp && sz > 0)
        {
            ::memcpy(pp, p, sz);
            _free64(p);
            return pp;
        } else
            return NULL;
    }
    size_t _msize64(void *p) {
        return malloc_size(*((void **) p - 1));
    }
    void _free64(void *p) {
        if (p) ::free(*((void **) p - 1));
    }
    
    
    uint8_t                *m_Data;
    int64_t                 m_MaxSize, m_FreeSize;
    TFreeBlocksByOffsetMap  m_FreeBlocksByOffset;
    TFreeBlocksBySizeMap    m_FreeBlocksBySize;
    
    std::map<int64_t, int>  block_count;
    AtomicLock              m_Lock;
    volatile bool           internal;
};

extern FreedomPool bigpool;

#ifdef __cplusplus

void *operator new(std::size_t n);
void operator delete(void * p) throw();
void *operator new[](std::size_t n);
void operator delete[](void *p) throw();

#endif // __cplusplus

#endif // H_FREEDOM_POOL
