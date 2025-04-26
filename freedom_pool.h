//  freedom_pool.h v1.5 (c)2023-2025 Dmitry Boldyrev
//
//  This is the most efficient block-pool memory management system you can find.
//  I tried many before writing my own: rpmalloc, tlsf, etc.
//  This code is partially based off this block allocator concept:
//  https://www.codeproject.com/Articles/1180070/Simple-Variable-Size-Memory-Block-Allocator
//
//  NEW (v1.5): Implemented IsValidPointer for safety, modernized volatile m_Internal with std::atomic

#pragma once

#ifdef __cplusplus

#include <assert.h>
#include <signal.h>
#include <dlfcn.h>
#include <dispatch/dispatch.h>
#include <dispatch/queue.h>
#include <malloc/malloc.h>
#include <assert.h>
#include "atomic.h"

#include <map>
#include <iostream>


// fprintf
#define DEBUG_PRINTF

//#define DISABLE_MALLOC_FREE_OVERRIDE
//#define DISABLE_NEWDELETE_OVERRIDE

// allocate on the stack (otherwise comment out)
#define FREEDOM_STACK_ALLOC

//#define FREEDOM_DEBUG
//#define BREAK_ON_THRESH

static const size_t KBYTE               = 1024;
static const size_t MBYTE               = KBYTE * KBYTE;

static const size_t THRESH_DEBUG_BREAK  = 1000 * MBYTE;
static const size_t THRESH_DEBUG_PRINT  = 20 * MBYTE;

static const size_t DEFAULT_GROW        = 1000 * MBYTE;  // 1.5 GB
static const size_t GROW_INCREMENT      = 50 * MBYTE;    // 50 MB increment growth

static const uint64_t TOKEN_ID          = UINT64_C(0x422E465245452100); // 'BE.FREE!'

// moved to mem.h #define MALLOC_ALIGN               64

#define DEBUGGER do {   \
    raise(SIGINT); \
} while (0);

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

void reset_freedom_counters(void);

extern "C" {
    size_t malloc_size(const void *_Nullable ptr);
    size_t malloc_usable_size(void *_Nullable ptr);
}

// Memory alignment settings

#define MEMORY_ALIGNMENT                128  // 128-byte alignment (cache line size)

#define ALIGN_UP(size, alignment)       (((size) + ((alignment) - 1)) & ~((alignment) - 1))
#define ALIGN_DOWN(size, alignment)     ((size) & ~((alignment) - 1))
#define IS_ALIGNED(size, alignment)     (((size) & ((alignment) - 1)) == 0)


// Structure to track allocation metadata
struct BlockHeader {
    size_t      size;       // Size of the allocation (excluding header)
    size_t      offset;     // Offset in the pool
    uint64_t    token;      // Verification token
};

// Size-class binning for faster allocation - power of 2 binning

#define SIZE_CLASS_COUNT        32  // Support up to 4GB objects (with 64-byte alignment)
#define GET_SIZE_CLASS(size)    (size == 0 ? 0 : __builtin_clz(((uint32_t)(size - 1) >> 6)) ^ 31)

template <size_t poolsize = DEFAULT_GROW>
class FreedomPool
{
public:
    FreedomPool():
        m_Internal(true),
        m_MaxSize(0),
        m_FreeSize(0),
        m_AllocCount(0),
        m_FreeCount(0)
    {
        initialize_overrides();
        m_Lock.init();
        
        // Initialize size classes
        for (int i = 0; i < SIZE_CLASS_COUNT; i++) {
            m_SizeClasses[i].clear();
        }
        
#ifndef FREEDOM_STACK_ALLOC
        m_Data = NULL;
#endif
        ExtendPool(poolsize);
    }
    
    ~FreedomPool()
    {
#ifndef FREEDOM_STACK_ALLOC
        if (m_Data)
            real_free(m_Data);
        m_Data = NULL;
#endif
    }
    
    // Pool status queries
    __inline bool IsFull() const { return m_FreeSize == 0; }
    __inline bool IsEmpty() const { return m_FreeSize == m_MaxSize; }
    __inline size_t GetMaxSize() const { return m_MaxSize; }
    __inline size_t GetFreeSize() const { return m_FreeSize; }
    __inline size_t GetUsedSize() const { return m_MaxSize - m_FreeSize; }
    
    // Initialize function pointers to the real memory functions
    __inline static void initialize_overrides()
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
    __inline bool IsValidPointer(const void *_Nullable p) const
    {
        if (!p) return false;
        
        // Get the supposed header address
        uintptr_t header_addr = (uintptr_t)p - sizeof(BlockHeader);
        uintptr_t data_start = (uintptr_t)m_Data;
        uintptr_t data_end = data_start + m_MaxSize;
        
        // Check if both pointer and header are within valid range
        return (header_addr >= data_start &&
                (uintptr_t)p < data_end &&
                IS_ALIGNED(header_addr - data_start, MEMORY_ALIGNMENT));
    }
    // Aligned memory allocation
    __inline void *_Nullable malloc(size_t nb_bytes)
    {
        if (!real_malloc) initialize_overrides();
        
        if (m_Internal.load(std::memory_order_relaxed) || !m_MaxSize)
            return real_malloc(nb_bytes);
        
        // Calculate aligned size with space for header
        size_t aligned_size = ALIGN_UP(nb_bytes, MEMORY_ALIGNMENT);
        size_t total_size = aligned_size + sizeof(BlockHeader);
        
        // Ensure alignment of the header itself
        total_size = ALIGN_UP(total_size, MEMORY_ALIGNMENT);
        
        if (GetFreeSize() < total_size) {
#ifdef FREEDOM_STACK_ALLOC
            DEBUG_PRINTF(stderr, "FreedomPool::malloc() Ran out of space allocating %lld MB used %lld of %lld MB. Static model, returning NULL\n", nb_bytes/MBYTE, GetUsedSize()/MBYTE, GetMaxSize()/MBYTE, (GetUsedSize() + nb_bytes + 3 * sizeof(void*) + GROW_INCREMENT) / MBYTE);
            return NULL;
#else
            DEBUG_PRINTF(stderr, "FreedomPool::malloc() Ran out of space allocating %lld MB used %lld of %lld MB, expanding to %lld MB\n", nb_bytes/MBYTE, GetUsedSize()/MBYTE, GetMaxSize()/MBYTE, (GetUsedSize() + nb_bytes + 3 * sizeof(void*) + GROW_INCREMENT) / MBYTE);
            dispatch_async( dispatch_get_main_queue(), ^{
                // grow pool by GROW_INCREMENT MB + requested size
                ExtendPool( GetUsedSize() + nb_bytes + 3 * sizeof(void*) + GROW_INCREMENT * MBYTE);
            });
#endif
        }
        
        return Malloc(aligned_size);
    }
    
    __inline void *_Nullable calloc(size_t count, size_t size)
    {
        if (!real_calloc) initialize_overrides();
        
        if (m_Internal.load(std::memory_order_relaxed) || !m_MaxSize)
            return real_calloc(count, size);
        
        size_t total_size = count * size;
        void* ptr = Malloc(total_size);
        
        if (ptr) {
            // Clear the memory
            memset(ptr, 0, total_size);
        }
        
        return ptr;
    }
    
    __inline void free(void *_Nullable p)
    {
        if (!real_free) initialize_overrides();
        
        if (m_Internal.load(std::memory_order_relaxed) || !m_MaxSize || !p) {
            real_free(p);
            return;
        }
        
        // First check if pointer is potentially within our pool
        if (IsValidPointer(p)) {
            // Only access the header if the pointer is valid
            BlockHeader* header = (BlockHeader*)((char*)p - sizeof(BlockHeader));
            if (header->token == TOKEN_ID)
            {
                Free(p);
                return;
            }
        }
        // Not our pointer or not valid, use system free
        real_free(p);
    }
    
    __inline void *_Nullable realloc(void *_Nullable p, size_t new_size)
    {
        if (!real_realloc) initialize_overrides();
        
        if (m_Internal.load(std::memory_order_relaxed) || !m_MaxSize)
            return real_realloc(p, new_size);
        
        if (!p)
            return malloc(new_size);
        
        if (IsValidPointer(p)) {
            // Only access the header if the pointer is valid
            BlockHeader* header = (BlockHeader*)((char*)p - sizeof(BlockHeader));
            if (header->token == TOKEN_ID) {
                size_t old_size = header->size;
                
                // If the new size is smaller, we can just update the size
                if (new_size <= old_size) {
                    header->size = ALIGN_UP(new_size, MEMORY_ALIGNMENT);
                    return p;
                }
                
                // Allocate new block
                void* new_p = malloc(new_size);
                if (!new_p)
                    return NULL;
                
                // Copy data and free old block
                memcpy(new_p, p, old_size);
                Free(p);
                return new_p;
            }
        }
        return real_realloc(p, new_size);
    }
    
    __inline size_t malloc_size(const void *_Nullable p)
    {
        if (!real_malloc_size) initialize_overrides();
        
        if (m_Internal.load(std::memory_order_relaxed) || !m_MaxSize || !p)
            return real_malloc_size(p);
        
        if (IsValidPointer(p)) {
            // Only access the header if the pointer is valid
            BlockHeader* header = (BlockHeader*)((char*)p - sizeof(BlockHeader));
            if (header->token == TOKEN_ID) {
                return header->size;
            }
        }
        return real_malloc_size(p);
    }
    
    __inline size_t malloc_usable_size(const void *_Nullable p)
    {
        if (!real_malloc_usable_size) initialize_overrides();
        
        if (m_Internal.load(std::memory_order_relaxed) || !m_MaxSize || !p)
            return real_malloc_usable_size(p);
        
        if (IsValidPointer(p)) {
            // Only access the header if the pointer is valid
            BlockHeader* header = (BlockHeader*)((char*)p - sizeof(BlockHeader));
            if (header->token == TOKEN_ID) {
                return header->size;
            }
        }
        return real_malloc_usable_size(p);
    }
    
    // Extend the memory pool
    size_t ExtendPool(size_t ExtraSize)
    {
#ifdef FREEDOM_STACK_ALLOC
        if (m_MaxSize) {
            fprintf(stderr, "FreedomPool isn't allowed to extend past initial size in static allocation. Set to %zu MB\n", ExtraSize/MBYTE);
            return m_MaxSize;
        }
#endif
        m_Lock.lock();
        
        // Ensure extra size is aligned
        ExtraSize = ALIGN_UP(ExtraSize, MEMORY_ALIGNMENT);
        
        size_t NewBlockOffset = m_MaxSize;
        size_t NewBlockSize = ExtraSize;
        
        fprintf(stderr, "Expanding FreedomPool internal size to: %zu MB\n", (m_MaxSize + ExtraSize)/MBYTE);
        
        // Add the block directly to the free list
        AddFreeBlock(NewBlockOffset, NewBlockSize);
        
        m_MaxSize += ExtraSize;
        m_FreeSize += ExtraSize;
        
#ifndef FREEDOM_STACK_ALLOC
        if (!m_Data)
            m_Data = (int8_t*)real_malloc(m_MaxSize);
        else
            m_Data = (int8_t*)real_realloc(m_Data, m_MaxSize);
#endif
        
        m_Lock.unlock();
        return m_MaxSize;
    }
    
protected:
    // Add a free block to the appropriate size class
     void AddFreeBlock(size_t offset, size_t size)
     {
         // Ensure offset and size are aligned
         offset = ALIGN_UP(offset, MEMORY_ALIGNMENT);
         size = ALIGN_DOWN(size, MEMORY_ALIGNMENT);
         
         if (size == 0)
             return;
         
         // Store block by address for coalescing
         auto it = m_FreeBlocksByOffset.lower_bound(offset);
         
         // Check for coalescence with previous block
         if (it != m_FreeBlocksByOffset.begin()) {
             auto prev = std::prev(it);
             if (prev->first + prev->second == offset) {
                 // Coalesce with previous block
                 size_t prevSize = prev->second;
                 size_t prevOffset = prev->first;
                 
                 // Remove previous block from its size class
                 RemoveFromSizeClass(prevSize, prevOffset);
                 
                 // Extend size
                 size += prevSize;
                 offset = prevOffset;
                 
                 // Remove previous from address map
                 m_FreeBlocksByOffset.erase(prev);
             }
         }
         
         // Check for coalescence with next block
         if (it != m_FreeBlocksByOffset.end() && offset + size == it->first) {
             // Coalesce with next block
             size_t nextSize = it->second;
             size_t nextOffset = it->first;
             
             // Remove next block from its size class
             RemoveFromSizeClass(nextSize, nextOffset);
             
             // Extend size
             size += nextSize;
             
             // Remove next from address map
             m_FreeBlocksByOffset.erase(it);
         }
         
         // Add the block to the address map
         m_FreeBlocksByOffset[offset] = size;
         
         // Add to appropriate size class
         AddToSizeClass(size, offset);
     }
    
    // Remove a block from its size class
      void RemoveFromSizeClass(size_t size, size_t offset)
      {
          int sizeClass = GET_SIZE_CLASS(size);
          auto& blocks = m_SizeClasses[sizeClass];
          
          for (auto it = blocks.begin(); it != blocks.end(); ++it) {
              if (it->first == offset) {
                  blocks.erase(it);
                  return;
              }
          }
      }
    // Add a block to its size class
       void AddToSizeClass(size_t size, size_t offset)
       {
           int sizeClass = GET_SIZE_CLASS(size);
           m_SizeClasses[sizeClass].push_back(std::make_pair(offset, size));
       }
       
    
    // Find the best fit block in the size classes
       bool FindBestFit(size_t size, size_t& offset, size_t& blockSize)
       {
           // Start with the size class that would fit this size
           int sizeClass = GET_SIZE_CLASS(size);
           
           // Look in this size class and larger ones
           for (int sc = sizeClass; sc < SIZE_CLASS_COUNT; sc++) {
               auto& blocks = m_SizeClasses[sc];
               
               // Find the best fit in this size class
               size_t bestFitSize = SIZE_MAX;
               size_t bestFitOffset = 0;
               size_t bestFitIndex = SIZE_MAX;
               
               for (size_t i = 0; i < blocks.size(); i++) {
                   if (blocks[i].second >= size && blocks[i].second < bestFitSize) {
                       bestFitSize = blocks[i].second;
                       bestFitOffset = blocks[i].first;
                       bestFitIndex = i;
                   }
               }
               
               if (bestFitIndex != SIZE_MAX) {
                   // Found a block
                   offset = bestFitOffset;
                   blockSize = bestFitSize;
                   
                   // Remove from size class
                   blocks.erase(blocks.begin() + bestFitIndex);
                   
                   // Remove from address map
                   m_FreeBlocksByOffset.erase(offset);
                   
                   return true;
               }
           }
           
           return false;
       }
    
    // Allocate memory from the pool
    void *_Nullable Malloc(size_t requestedSize)
    {
        m_Lock.trylock();
        m_Internal.store(true, std::memory_order_relaxed);
        
        // Add space for the header and ensure alignment
        size_t totalSize = requestedSize + sizeof(BlockHeader);
        totalSize = ALIGN_UP(totalSize, MEMORY_ALIGNMENT);
        
        if (m_FreeSize < totalSize) {
            m_Internal.store(false, std::memory_order_relaxed);
            m_Lock.unlock();
            return NULL;
        }
        
        // Find the best fit block
        size_t offset, blockSize;
        if (!FindBestFit(totalSize, offset, blockSize)) {
            m_Internal.store(false, std::memory_order_relaxed);
            m_Lock.unlock();
            return NULL;
        }
        
        // If the remainder is worth keeping, split the block
        if (blockSize - totalSize >= MEMORY_ALIGNMENT * 2) {
            // Split the block and add the remainder back to the free list
            AddFreeBlock(offset + totalSize, blockSize - totalSize);
            blockSize = totalSize;
        }
        
        // Set up the block header
        BlockHeader* header = (BlockHeader*)&m_Data[offset];
        header->size = requestedSize;
        header->offset = offset;
        header->token = TOKEN_ID;
        
        m_FreeSize -= blockSize;
        m_AllocCount++;
        
        m_Internal.store(false, std::memory_order_relaxed);
        m_Lock.unlock();
        
        // Return pointer to the usable memory (after the header)
        return &m_Data[offset + sizeof(BlockHeader)];
    }
    
    // Free memory back to the pool
    void Free(void *_Nullable ptr)
    {
        if (!ptr)
            return;
            
        m_Lock.lock();
        m_Internal.store(true, std::memory_order_relaxed);

        // Get the block header
        BlockHeader* header = (BlockHeader*)((char*)ptr - sizeof(BlockHeader));
        if (header->token != TOKEN_ID) {
            fprintf(stderr, "WARNING: Trying to free non-native pointer, incorrect tokenID\n");
            m_Internal.store(false, std::memory_order_relaxed);
            m_Lock.unlock();
            return;
        }
        
        size_t offset = header->offset;
        size_t size = ALIGN_UP(header->size + sizeof(BlockHeader), MEMORY_ALIGNMENT);
        
        // Add the block back to the free list
        AddFreeBlock(offset, size);
        
        m_FreeSize += size;
        m_FreeCount++;
        
        m_Internal.store(false, std::memory_order_relaxed);
        m_Lock.unlock();
    }
    
private:
#ifdef FREEDOM_STACK_ALLOC
    int8_t m_Data[poolsize];
#else
    int8_t* m_Data;
#endif

    size_t m_MaxSize;                           // Total pool size
    size_t m_FreeSize;                          // Available free space
    
    // Fast size-class based allocation system
    std::vector<std::pair<size_t, size_t>> m_SizeClasses[SIZE_CLASS_COUNT];
    
    // Address-ordered map for block coalescing
    std::map<size_t, size_t> m_FreeBlocksByOffset;
    
    size_t m_AllocCount;                        // Number of allocations
    size_t m_FreeCount;                         // Number of frees
    
    AtomicLock m_Lock;                          // Thread synchronization
    std::atomic<bool> m_Internal;               // Flag for internal operations
};

#if !defined(DISABLE_NEWDELETE_OVERRIDE)

void *_Nullable operator new(std::size_t n);
void operator delete(void *_Nullable p) throw();
void *_Nullable operator new[](std::size_t n);
void operator delete[](void *_Nullable p) throw();

extern FreedomPool<DEFAULT_GROW> bigpool;

size_t malloc_size(const void *_Nullable ptr);
size_t malloc_usable_size(void *_Nullable ptr);

#endif // DISABLE_NEWDELETE_OVERRIDE

#endif // __cplusplus

