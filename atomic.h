// atomic.h (c) 2023-2025 Dmitry Bodlyrev
// -------------------------------------------------------------------------
// implements: AtomicLock, AtomicSema, HashClass, AtomicThread

#ifndef H_ATOMIC
#define H_ATOMIC

#ifdef __cplusplus

#include <unistd.h>

#include <atomic>
#include <mutex>
#include <condition_variable>
#include <type_traits>

template<typename T>
bool IsPointerAnObject(T* ptr) {
    if (!ptr) return false;
    return !std::is_fundamental<T>::value;
}

class AtomicLock {
public:
    AtomicLock() {
        init();
    }
    
    // Prevent copying and moving
    AtomicLock(const AtomicLock&) = delete;
    AtomicLock& operator=(const AtomicLock&) = delete;
    AtomicLock(AtomicLock&&) = delete;
    AtomicLock& operator=(AtomicLock&&) = delete;
    
    void init() {
        _atomic.store(0, std::memory_order_relaxed);
    }
    
    __inline void lock() {
        int expected = 0;
        // Don't reset expected inside the loop - let compare_exchange update it
        while (!_atomic.compare_exchange_weak(expected, 1, std::memory_order_acquire)) {
            // Short delay before retrying to reduce CPU usage
               // sched_yield();
            usleep(10000);
           // pthread_yield();
            expected = 0; // Reset only after yielding
        }
    }
    
    __inline unsigned trylock() {
        int expected = 0;
        return _atomic.compare_exchange_strong(expected, 1, std::memory_order_acquire) ? 0 : EBUSY;
    }
    
    __inline void unlock() {
        _atomic.store(0, std::memory_order_release);
    }
    
    __inline std::atomic<int> *P() {
        return &_atomic;
    }
    
protected:
    std::atomic<int> _atomic;
};

#endif // __cplusplus

#endif // H_ATOMIC
