#ifndef PSEUDONIX_DEFER_H
#define PSEUDONIX_DEFER_H

#include <functional>
//==========================================================================
// a useful code snippit I found on twitter, can be used to call when a
// scope exits, eg:
//
// void myfunc()
// {
//     int x = 3;
//     // this will be called no matter when the function returns
//     bl_defer {
//         std::cout << "Hello from the bl_deferred block: " << x;
//     };
//
//     if(...)
//     {
//         x = 5
//         return;
//     }
//     if(...)
//     {
//         x = 42;
//         return;
//     }
// }
namespace PseudoNix
{


struct Def
{
    std::function<void(void)> on_destroy;

    ~Def()
    {
        on_destroy();
    }

    struct tmp
    {
    };
};

template<typename T>
Def operator << (Def::tmp, T && v)
{
    return Def{v};
}

#define _bl_defer(id) _bl_defer_##id
#define bl_defer auto _bl_defer(__COUNTER__) = PseudoNix::Def::tmp() << [&]()
#define PSEUDONIX_TRAP bl_defer

}
//==========================================================================


#endif
