// atomic.h (c) 2023-2025 Dmitry Bodlyrev
// --------------------------------------
// implements: AtomicLock, AtomicSema

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

// 5 sec timeout, 0.005 sec running
#define SYNCHRONIZE_LOCK(alock) \
    uint64_t deadline = mach_absolute_time() + seconds_to_abs(5); \
    while (alock.islocked() && mach_absolute_time() < deadline) { \
        [NSThread sleepForTimeInterval:0.005f]; \
    }

class AtomicLock {
public:
    AtomicLock() { init(); }
    
    AtomicLock(const AtomicLock& other) : _atomic(other._atomic.load()) {}

    inline AtomicLock& operator=(const AtomicLock& other) {
        if (this != &other) {
            _atomic.store(other._atomic.load());
        }
        return *this;
    }
    inline void init() {
        _atomic.store(0, std::memory_order_relaxed);
    }
    inline void lock() {
        int expected = 0;
        int attempts = 0;
        
        while (!_atomic.compare_exchange_weak(expected, 1,
               std::memory_order_acquire,
               std::memory_order_relaxed)) {
            expected = 0;
            
            // Exponential backoff with jitter
            if (++attempts < 5) {
                // Quick CPU yield - minimal overhead
                sched_yield();
            } else if (attempts < 20) {
                // Short Mach wait - more precise timing
                mach_wait_until(mach_absolute_time() + (5 * NSEC_PER_USEC));
            } else {
                // Adaptive wait with exponential backoff
                uint64_t wait_time = std::min((uint64_t)5 * attempts, 200ULL);
                mach_wait_until(mach_absolute_time() + (wait_time * NSEC_PER_USEC));
            }
        }
    }

    inline unsigned trylock() {
        int expected = 0;
        return _atomic.compare_exchange_strong(expected, 1, std::memory_order_acquire) ? 0 : EBUSY;
    }
    inline bool islocked() {
        return _atomic.load(std::memory_order_relaxed) != 0;
    }
    inline void wait_until_unlocked() {
        int attempts = 0;
        while (islocked()) {
            if (++attempts < 5) {
                sched_yield();
            } else if (attempts < 20) {
                mach_wait_until(mach_absolute_time() + (5 * NSEC_PER_USEC));
            } else {
                uint64_t wait_time = std::min((uint64_t)5 * attempts, 200ULL);
                mach_wait_until(mach_absolute_time() + (wait_time * NSEC_PER_USEC));
            }
        }
    }
    inline void unlock() {
        _atomic.store(0, std::memory_order_release);
    }
    inline std::atomic<int> *P() {
        return &_atomic;
    }
    
protected:
    
    std::atomic<int> _atomic;
    double timer;
};

class StAtomicLock
{
public:
    StAtomicLock(AtomicLock& lock) : _lock(&lock) {
        if (_lock) _lock->lock();
    }
    
    ~StAtomicLock() {
        if (_lock) _lock->unlock();
    }
    
    bool wasLocked() {
        return (_lock) ? _lock->islocked() : false;
    }
protected:
    AtomicLock *_lock;
};

class DiagnosticAtomicLock : public AtomicLock {
private:
    std::atomic<uint64_t> total_lock_time{0};
    std::atomic<uint64_t> lock_count{0};

public:
    DiagnosticAtomicLock() { mach_timebase_info(&timebaseInfo); }
   
    void lock() {
        auto start = mach_absolute_time();
        AtomicLock::lock();
        
        auto lock_duration = mach_absolute_time() - start;
        total_lock_time.fetch_add(lock_duration);
        lock_count.fetch_add(1);
    }

    void printLockStats() {
        uint64_t count = lock_count.load();
        if (count > 0) {
            double avg_lock_time =
                abs_to_seconds(total_lock_time.load()) / count;
            
            fprintf(stderr, "Lock Statistics: "
                "Total Locks=%llu, "
                "Avg Lock Time=%.6f seconds\n",
                count, avg_lock_time);
        }
    }
    
protected:
    mach_timebase_info_data_t timebaseInfo;
    
    inline double abs_to_seconds(uint64_t abs_time) {
        // Convert Mach absolute time units back to nanoseconds
        uint64_t nanos = abs_time * timebaseInfo.numer / timebaseInfo.denom;
        
        // Convert nanoseconds to seconds
        return (double)nanos / 1e9;
    }
};

class AdvancedAtomicLock : public AtomicLock {
private:
    std::atomic<uint64_t> contention_counter{0};
    std::atomic<bool> performance_mode{false};

public:
    void lock() {
        // Enhanced lock with performance tracking
        auto start = std::chrono::steady_clock::now();
        
        AtomicLock::lock();  // Call parent lock
        
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start
        );
        
        // Track contention
        if (duration.count() > 100) {  // Long wait threshold
            contention_counter.fetch_add(1);
            
            // Adaptive mode switching
            if (contention_counter.load() > 10) {
                performance_mode.store(true);
                fprintf(stderr, "High lock contention detected.\n");
            }
        }
    }

    void unlock() {
        // Reset contention if unlock happens quickly
        if (performance_mode.load()) {
            contention_counter.store(0);
            performance_mode.store(false);
        }
        
        AtomicLock::unlock();
    }
};

class AtomicSema {
public:
    AtomicSema() : _count(0) {
        init(0, 0);
    }
    AtomicSema(const AtomicSema& other) : _count(other._count.load()) {}
    
    AtomicSema& operator=(const AtomicSema& other) {
        if (this != &other) {
            _count.store(other._count.load());
        }
        return *this;
    }
    inline int init(int s, int v) {
        _count.store(v, std::memory_order_relaxed);
        return 0;
    }
    
    inline int signal() {
        int prev = _count.fetch_add(1, std::memory_order_release);
        if (prev == 0) {
            std::lock_guard<std::mutex> lock(_mutex);
            _cond.notify_one();
        }
        return 0;
    }
    inline int wait(uint64_t timeout_ns) {
        // Fast path - check if semaphore is already signaled
        int expected = 1;
        if (_count.compare_exchange_strong(expected, 0, std::memory_order_acquire)) {
            return 0;
        }
        
        // For very short timeouts in audio processing, use spinning instead
        if (timeout_ns < 500 * NSEC_PER_USEC) { // Less than 500μs
            uint64_t deadline = mach_absolute_time() + timeout_ns;
            
            do {
                expected = 1;
                if (_count.compare_exchange_strong(expected, 0, std::memory_order_acquire)) {
                    return 0;
                }
                
                // Short yield to prevent CPU thrashing
                pthread_yield_np();
            } while (mach_absolute_time() < deadline);
            
            return -1; // Timeout
        }
        
        // Slow path - use condition variable for longer waits
        std::unique_lock<std::mutex> lock(_mutex);
        bool success = _cond.wait_for(lock, std::chrono::nanoseconds(timeout_ns),
                                      [this]() { return _count.load(std::memory_order_acquire) > 0; });
        
        if (!success) {
            return -1; // Timeout
        }
        
        _count.fetch_sub(1, std::memory_order_release);
        return 0;
    }
    
    inline int wait() {
        // Fast path check
        int expected = 1;
        if (_count.compare_exchange_strong(expected, 0, std::memory_order_acquire)) {
            return 0;
        }
        
        // Slow path
        std::unique_lock<std::mutex> lock(_mutex);
        _cond.wait(lock, [this]() { return _count.load(std::memory_order_acquire) > 0; });
        _count.fetch_sub(1, std::memory_order_release);
        return 0;
    }
    
    inline int trywait() {
        int expected = 1;
        return _count.compare_exchange_strong(expected, 0, std::memory_order_acquire) ? 0 : -1;
    }
    
protected:
    std::mutex              _mutex;
    std::atomic<int>        _count;
    std::condition_variable _cond;
};
 
#endif // __cplusplus

#endif // H_ATOMIC
