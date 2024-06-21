FreedomPool v1.32
-----------------

v1.32: Fixed more crashes and weirdness during malloc/free/new/delete overrides, smooth rolling now! If it crashes even once,
       from adding freedom_pool overrides, plz notify me and I will look into it. It doesn't crash at all for me, but 1.3 did.

You can now align to 64-byte if you need to outside of freedom_pool, like I do it:

       #define MALLOC_V4SF_ALIGNMENT 64

       static inline void *aligned_malloc(size_t nb_bytes)
       {
           void *p, *p0 = bigpool.malloc(nb_bytes + MALLOC_V4SF_ALIGNMENT);
           if (!p0) return (void *) 0;
           p = (void *) (((size_t) p0 + MALLOC_V4SF_ALIGNMENT) & (~((size_t) (MALLOC_V4SF_ALIGNMENT-1))));
           //*(uint32_t*)((void **) p - 1) = 'FREE';
           *((void **) p - 1) = p0;
           return p;
       }
       
       static inline int64_t aligned_free(void *p) {
           if (p) {
               void *ptr = *((void **) p - 1);
               int64_t sz = bigpool.malloc_size(ptr);
               bigpool.free( ptr );
             //  if ((sz/1048576L) == 0)
             //      fprintf(stderr, "Valigned_free( %3lld kb)\n", (int64_t)(sz/1024));
             //  else
             //      fprintf(stderr, "Valigned_free( %3lld MB )\n", (int64_t)(sz/1048576L));
               return sz;
           } else return -1;
       }
       
       static inline void *aligned_ptr(void *p)
       {
           return (p) ? *((void **) p - 1) : (void *) 0;
       }

and finally for logging:

       #define DEBUG_PRINTF fprintf    // (or)
       #define DEBUG_PRINTF            // skip logging 

you can override C++/C malloc/free/new/delete/etc separately or together with those #define controls
by uncommenting these:

       // #define DISABLE_NEWDELETE_OVERRIDE
       // #define DISABLE_MALLOC_FREE_OVERRIDE

I've tested this in my huge project that has hundreds of files and functions, no crashes as of 1.31!
streams audio, video, uses SSL internal calls, no problems! 

If you have some modifications and improvements that appear to be stable send over I will check them out! 

This is probably the most efficient block-pool memory management system you can find. I tried many before 
coming up with my own, which turned out to be already 6 months in existence, well brilliant minds think alike!

I don't recommend rpmalloc, tlsf, many tlsf clones. I have tried , and had too many problems and crashes.

I am in fact using freedom_pool in my multimedia rich project that I am working on which utilizes heave use of 
OpenGL, audio, DSP, it works great no strange crashes.

This code is partially based off this block allocator concept, despite independently coming up with the idea 
6 months later, this author gets the credits for being first: 

       https://www.codeproject.com/Articles/1180070/Simple-Variable-Size-Memory-Block-Allocator

This is how you would use it in your app, in main.mm/.cpp you make a static allocation of class and iniitializae it
to the max size of your app's memory usage.

       #include <stdio.h>
       #include <stdlib.h>
       
       FreedomPool bigpool;
       
       int main(int argc, char *argv[])
       {
              char *s = bigpool.malloc(12);
              // should print out 12
              printf("allocated space for s: %d\n", bigpool.malloc_size(s)); 
              bigpool.free(s);

              // or if you commented out DISABLE_MALLOC_FREE_OVERRIDE

              char *ss = malloc(12);
              // should print out 12
              printf("allocated space for ss: %d\n", bigpool.malloc_size(ss)); 
              free(ss);

              // and if you commented out also DISABLE_NEWDELETE_OVERRIDE
              
              char *sss = new char[12];
              // should print out 12
              printf("allocated space for sss: %d\n", bigpool.malloc_size(sss)); 
              delete[] sss;

              return 0;
       }

This type of allocation is cross-thread safe, what does it mean? it means you can bigpool.malloc() in one thread,
and bigpool.free() in another without a problem. I also tried to make it self-expanding based on memory usage, but then
decided to temporary remove it because of realloc that can happen in the wrong time, can cause problems, if you want
to enable ExtendPool mechanism, you can do so by adding this: dispatch_async( dispatch_get_main_queue(), ^{ ExtendPool( EXPANSION ); };  inside of bigpool.malloc(). I still recommend the approach of measurig your app's memory usage (which is easy to do with FREEDOM_DEBUG and pre-grow it statically or dynamically, whichever.

LICENSE: Freeware- use it as you please. Provided AS-IS. Because I borrowed some code from the above mentioned solution,
although I thought of it before I discovered someone already done it, decided to just make a nice iOS / OS X version, but it should
also work on Linux, and other UNIXes and Windoze as well.
