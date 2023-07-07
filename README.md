FreedomPool v1.0
----------------

This is the most efficient block-pool memory management system you can find. I tried many before writing my own:
rpmalloc, tlsf, many tlsf clones.

I tested it live in a multimedia rich project I am working on that has OpenGL, and audio 
DSP, it works great no weird crashes. Solid.

This code is partially based off this block allocator concept:
https://www.codeproject.com/Articles/1180070/Simple-Variable-Size-Memory-Block-Allocator

This is how you use it in your app, in main.mm/.cpp you make a static allocation of class and initialzie it in main.

FreedomPool bigpool;
int main(int argc, char *argv[])
{
    bigpool.ExtendPool( 1024LL * 1024LL * 8LL ); // 8 MB
}

LICENSE: Free to use to anyone for any purposes! Because I didn't come up with original concept, although
I did, but someone already done it, so I decided to just released my wrapper solution for free because
of this. Go ahead use it as you wish... It speed up my app by about 30%, not kidding...

I hope your app will run faster and this code will benefit you/your company... Enjoy.

Dmitry
