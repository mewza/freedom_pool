FreedomPool v1.5
-----------------

v1.5: Implemented IsValidPointer for safety, modernized volatile m_Internal with std::atomic
v1.4: Optimized (removed multimap bottlebneck), proper implementation of configurable memory alignment,
      default: 64, stable as a rock I actively use it in my huge multimedia project, with no troubles.

This is probably the most efficient static or dynamic allocated block-pool memory management system you 
can find. I've gone over at least 5 or 10, tried many before coming up with my own in the end, becuase of
bugs and failures of other people's implementations of block-pool solutions, which gave my app random weird
crashes. You should expect STABLE and BUG FREE performance from FreedomPool, and nothing short of! I don't 
have time to waste on bullshit. If I do things I do things to the best of my ability, and I don't do things
simply because someone just pays me to do something. My inspiration is moving humanity forward towards
some sort of better future where things simply work for the benefit of all and everyone does things
out of good intentions, never to harm or cause evil to other people.

I am using freedom_pool in my multimedia rich project (which has hundreds of .cpp and .h files in the project) 
I am working on which makes use of OpenGL, does intense audio DSP on iOS and OS X - I have MacCatalyst build 
which works solid with no strange crashes with FreedomPool.

This code is conceptually based off the block allocator concept below, and despite independently coming up 
with the idea  6 months later, this author gets the credits for being first: 

       https://www.codeproject.com/Articles/1180070/Simple-Variable-Size-Memory-Block-Allocator

To enable debugging and messaging define DEBUG_PRINTF in freedom_pool.h like shown here:

       #define DEBUG_PRINTF fprintf    // (or)
       #define DEBUG_PRINTF            // skip logging 

you can override C++/C malloc/free/new/delete/etc separately or together with those #defines:

       // #define DISABLE_NEWDELETE_OVERRIDE
       // #define DISABLE_MALLOC_FREE_OVERRIDE

       (commented out means they are disabled)

If you have some ideas for improvements or have made modification that are stable send over to me for evaluation, 
and I will integrate them if it benefits this project. 

This is how you would use it in your app, in main.mm/.cpp you make a static allocation of class and iniitializae it
to the max size of your app's memory usage, or if you have DISABLE_NEWDELETE_OVERRIDE and DISABLE_MALLOC_FREE_OVERRIDE
commented out in freedom_pool.h (by default FreedomPool is enabled, i.e. defines are commented out), so if you include 
freedom_pool.cpp into your project, and just recompile the project, all malloc/free/new/delete/new[]/delete[] will be 
automaticlaly routed through FreedomPool, so no need to do anything beyond that. 

Manual use of FreedomPool with DISABLE_NEWDELETE_OVERRIDE and DISABLE_MALLOC_FREE_OVERRIDE uncommented looks like this:

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

This type of allocation is cross-thread safe, easy to use, and transparent, what does it mean? it means you can bigpool.malloc() 
in one thread, and safely bigpool.free() in another. I also tried to make FreedomPool dynamically self-expanding based on memory usage, 
but then decided to backtrack because of realloc that can happen in the wrong time, can cause problems. if you want
to enable ExtendPool mechanism, you can do so by adding this: dispatch_async( dispatch_get_main_queue(), ^{ ExtendPool( EXPANSION ); }; 
inside of bigpool.malloc(). I still recommend the approach of measurig your app's memory usage (which is easy to do with FREEDOM_DEBUG 
and pre-grow it statically or dynamically, whichever.

LICENSE: LOVE FREEWARE- use it as you please. Provided AS-IS. Would appreciate a "Thank you" in the credits of the application, and a reference to this
page on Github. 

The one and only - original inventor of "MacAmp" and "Winamp" who was saved by GOD from death in "mysterious" circumstances *cough* *cough*, and no -resurrection from the dead is never fun, and in many ways I understood what Jesus has been through with crucifiction and resurrection as a result from a very direct experience of it, and no, no doctors were involved - just GOD. And yes, GOD is very real. 

P.S. Russian pepole are not your enemy, they are your friend, in fact, dear western ignoramuses - don't trust your oligarchs or your mainstream media, who have fooled you into believing that. Together, we can raise above the power of their control sytem over us with money.

Dmitry Boldyrev <subband@protonmail.com>


