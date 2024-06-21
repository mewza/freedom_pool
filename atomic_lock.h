// atomic_lock.h
//
// pthreads based C++ mutex/signal wrapper class
// (C) 2023 DEMOS

#ifndef H_ATOMICLOCK
#define H_ATOMICLOCK

#ifndef _PTHREAD_H
#include <pthread.h>
#endif

#ifdef __cplusplus
class AtomicLock {
public:
    AtomicLock() : _mutex(PTHREAD_MUTEX_INITIALIZER)  {
    }
    void init() {
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


class AtomicSignal {
public:
    AtomicSignal() : _mutex(PTHREAD_MUTEX_INITIALIZER), _cond(PTHREAD_COND_INITIALIZER) {
    }
    ~AtomicSignal() {
        exit();
    }
    void init() {
        pthread_mutex_init(&_mutex, NULL);
        pthread_cond_init(&_cond, NULL);
    }
    void exit() {
        pthread_mutex_destroy(&_mutex);
        pthread_cond_destroy(&_cond);
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
    __inline void signal() {
        lock();
        pthread_cond_signal(&_cond);
        unlock();
    }
    __inline void wait() {
        lock();
        pthread_cond_wait(&_cond, &_mutex);
        unlock();
    }
   
protected:
    pthread_cond_t _cond;
    pthread_mutex_t _mutex;
};
#endif


#endif // H_ATOMICLOCK
