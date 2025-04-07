FreedomPool v1.4
-----------------

v1.4: Optimized (removed multimap bottlebneck), proper implementation of configurable memory alignment,
      default: 64, stable as a rock I actively use it in my huge multimedia project, with no troubles.

This is probably the most efficient block-pool memory management system you can find. I tried many before 
coming up with my own, which turned out to be already 6 months in existence, well brilliant minds think alike!

I don't recommend rpmalloc, tlsf, many tlsf clones. I have tried , and had too many problems and crashes.

I am in fact using freedom_pool in my multimedia rich project that I am working on which utilizes heave use of 
OpenGL, audio, DSP, it works great no strange crashes.

This code is partially based off this block allocator concept, despite independently coming up with the idea 
6 months later, this author gets the credits for being first: 

       https://www.codeproject.com/Articles/1180070/Simple-Variable-Size-Memory-Block-Allocator

To enable debugging and messaging define DEBUG_PRINTF in freedom_pool.h like shown here:

       #define DEBUG_PRINTF fprintf    // (or)
       #define DEBUG_PRINTF            // skip logging 

you can override C++/C malloc/free/new/delete/etc separately or together with those #defines:

       // #define DISABLE_NEWDELETE_OVERRIDE
       // #define DISABLE_MALLOC_FREE_OVERRIDE

       (commented out means they are disabled)

If you have some modifications and improvements that appear to be stable send over I will check them out, but otherwise
enjoy, this is a great static allocation alternative to stdlib's.

This is how you would use it in your app, in main.mm/.cpp you make a static allocation of class and iniitializae it
to the max size of your app's memory usage, or if you can just uncomment DISABLE_NEWDELETE_OVERRIDE and DISABLE_MALLOC_FREE_OVERRIDE
if they are commented out in freedom_pool.h (I think by default they are enabled, which means if you include freedom_pool.cpp into
your project, and just recompile the project, all malloc/free/new/delete/new[]/delete[] will be automaticlaly routed through FreedomPool.
Manual use with those #defines disabled would look like this:

       #include <stdio.h>
       #include <stdlib.h>
       #include "freedom_pool.h"
       
       int main(int argc, char *argv[])
       {
              char *s = (char*)bigpool.malloc(12);
              
              // should print out 16
              printf("allocated space for s: %d\n", (int)bigpool.malloc_size(s)); 
              bigpool.free(s);
              
              char *s2 = new char[12];
              // should print out 16
              printf("allocated space for s2: %d\n", bigpool.malloc_size(s2)); 
              delete[] s2;

              return 0;
       }

This type of allocation is cross-thread safe, what does it mean? it means you can bigpool.malloc() in one thread,
and bigpool.free() in another without a problem. I also tried to make FreedomPool self-expanding based on memory usage, 
but then decided to temporary remove it because of realloc that can happen in the wrong time, can cause problems, if you want
to enable ExtendPool mechanism, you can do so by adding this: dispatch_async( dispatch_get_main_queue(), ^{ ExtendPool( EXPANSION ); };  inside of bigpool.malloc(). I still recommend the approach of measurig your app's memory usage (which is easy to do with FREEDOM_DEBUG and pre-grow it statically or dynamically, whichever.

LICENSE: Freeware- use it as you please. Provided AS-IS. Would appreciate a "Thank you" in the credits of the application, and reference to the GITHUB. Because I borrowed some code from the above mentioned solution, although I came up with it independently, I discovered someone already done it, 
decided to just make a nice wrapper for iOS and OS X, and it should also work on Linux, and other Unix and non-Unix based operating systems.

Dmitry Boldyrev <subband@protonmail.com>


