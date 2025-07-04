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

#define PN_CONCAT_IMPL( x, y ) x##y
#define PN_MACRO_CONCAT( x, y ) PN_CONCAT_IMPL( x, y )
#define PSEUDONIX_TRAP auto PN_MACRO_CONCAT( deferVar, __COUNTER__ ) = PseudoNix::Def::tmp() << [&]()
#define PN_TRAP auto PN_MACRO_CONCAT( deferVar, __COUNTER__ ) = PseudoNix::Def::tmp() << [&]()

}
//==========================================================================


#endif
