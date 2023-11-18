FreedomPool v1.1
----------------

NEW: Added 64-byte alignment for internal pool's blocks. I got a significant improvement on performance since I 
added this, but let me know if you experience any weird crashes.

This is the most efficient block-pool memory management system you can find. I tried many before writing my own:
rpmalloc, tlsf, many tlsf clones.

I tested it live in a multimedia rich project I am working on which heavily uses OpenGL, and audio 
DSP, it works great no strange crashes. Solid.

This code is partially based off this block allocator concept:
https://www.codeproject.com/Articles/1180070/Simple-Variable-Size-Memory-Block-Allocator

This is how you would use it in your app, in main.mm/.cpp you make a static allocation of class and iniitializae it
to the max size of your app's memory usage.

FreedomPool bigpool;
int main(int argc, char *argv[])
{
    bigpool.ExtendPool( 1024LL * 1024LL * 8LL ); // 8 MB
}

This type of allocation is cross-thread safe, what does it mean? it means you can bigpool.malloc() in one thread,
and bigpool.free() in another without a problem. I also tried to make it self-expanding based on memory usage,
but I still recommend that you measure your app's memory usage and pre-grow it for entire app's usage, because
ExtendPool() uses realloc() and it isn't thread-safe, so you can't do cross-thread stuff.

LICENSE: Freeware- use it as you please. As is. Because I didn't come up with original concept, 
although I thought of it before I discovered someone already done it, decided to releasse my wrapper.

It will speed up your app by about 30%, not kidding... Enjoy

Dmitry
