// freedom_pool.cpp
//
// This is the most efficient memory allocator system you can find. I tried many: rpmalloc, tlsf, this one is IT
// I tested it and it works great on iOS, OS X, I got about 30% speed up in doing multi-media rich app that uses
// 3D OpenGL gfx, and audio
//
// This code is partially based off this block allocator concept:
// https://www.codeproject.com/Articles/1180070/Simple-Variable-Size-Memory-Block-Allocator
//
// (C) 2023 Dmitry Bodlyrev

#include "freedom_pool.h"

void * operator new(std::size_t n)
{
    return bigpool.malloc(n);
}
void operator delete(void * p) throw()
{
    bigpool.free(p);
}

void *operator new[](std::size_t n)
{
    return bigpool.malloc(n);
}
void operator delete[](void *p) throw()
{
    bigpool.free(p);
}

