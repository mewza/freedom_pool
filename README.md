FreedomPool v1.21
-----------------

NEW: I updated today to v1.21, 64-byte alignment isn't hooked up yet but the provisions are there, 
I really need to just fuse it inside of block management itself, instead of creating another intermediate layer.
I had 64-byte alignment working, but temporary disabled because for some reason it made not much performance difference.
I added TOKEN_ID verification as a simple sanity check, and added a way to debug alloc/free for both C/C++ so you can 
identify how much memory your app is using, and pre-grow the freedom_pool memory zone usage accordingly. You can also 
debug your app's memory allocations and if you uncomment BREAK_ON_THRESH in freedom_pool.cpp then you can have it break 
into Debugger everytime alloc is over certain threshold that you can specify by adjusting THRESH_DEBUG_BREAK.  
And if you want to enable print messages set in freedom_pool.h:

#define DEBUG_PRINTF fprintf

and now also you can override C malloc/free/etc and C++ new, delete, new[], delete[] separately or together,
by uncommenting these:

//#define DISABLE_NEWDELETE_OVERRIDE
//#define DISABLE_MALLOC_FREE_OVERRIDE

I've tested this in my huge project that has hundreds of files, and is a multimedia project and I get no crashes,
streams audio, video, uses SSL internal calls, no problems! 

If you have some modifications and improvements, and appear to be stable send it over I will check it out! 
Peace. D.

-----------------------------------------------------------------------------------------------------------------

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

It will speed up your app by about 30%, not kidding... Enjoy. D.
