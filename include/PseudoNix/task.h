#ifndef PSEDUONIX_COROUTINE_TASK_H
#define PSEDUONIX_COROUTINE_TASK_H

#include <coroutine>
#include <exception>
#include <iostream>
#include <utility>

namespace PseudoNix
{

template<typename T>
struct promise_value
{
    T result;

    void return_value(T && r )
    {
        result = std::move(r);
    }
};

struct promise_void
{
    void return_void()
    {

    }
};


template<typename T,
         typename initial_suspend_t = std::suspend_never,
         typename final_suspend_t = std::suspend_always>
struct Task_t
{
    struct promise_type : public std::conditional_t< std::is_same_v<void,T>, promise_void, promise_value<T> >
    {
        // must have a default consturctor
        promise_type() = default;

        // this is the first method to get
        // executed when a coroutine is
        // called for the first time
        // Its job is to construct the return
        // object
        Task_t<T, initial_suspend_t, final_suspend_t> get_return_object()
        {
            //std::cout << "get_return_object()\n";
            return Task_t<T, initial_suspend_t, final_suspend_t>{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        // returns an awaiter. can return
        // std::suspend_never/always
        // This is called just before the corotuine
        // starts execution. It specifies in what
        // state the coroutine should start in.
        // in this case we are not-suspending when
        // we first start the coroutine
        initial_suspend_t initial_suspend() {
            //std::cout << "initial suspend\n";
            return {};
        }

        // executes when the coroutine finishes
        // executing.
        final_suspend_t final_suspend() noexcept {
            return {};
        }

        // if there are any exceptions thrown
        // that are not handled, this function
        // will be called
        void unhandled_exception()
        {
            std::cout <<  "Unhandled Exception" << std::endl;
            exit(1);
        }
    };


    // a copy constructor that passes in a coroutine handle
    // so that we can control the exceution
    Task_t(std::coroutine_handle<promise_type> handle_) : handle(handle_)
    {
    }

    ~Task_t()
    {
        if constexpr ( std::is_same_v<final_suspend_t, std::suspend_never> )
        {
            destroy();
        }
    }
    Task_t(Task_t<T, initial_suspend_t, final_suspend_t> &&V) : handle(std::exchange(V.handle, nullptr))
    {
    }
    Task_t & operator=(Task_t<T, initial_suspend_t, final_suspend_t> && V)
    {
        if(&V != this)
        {
            handle = std::exchange(V.handle, nullptr);
        }
        return *this;
    }
    Task_t(Task_t<T, initial_suspend_t, final_suspend_t> const &handle_) = delete;
    Task_t & operator=(Task_t<T, initial_suspend_t, final_suspend_t> const & V) = delete;

    void destroy()
    {
        if(handle)
            handle.destroy();
        handle = nullptr;
    }
    void resume()
    {
        try
        {
            handle.resume();
        }
        catch (std::exception & e)
        {
            std::cout << "Exception Thrown: " << e.what() << std::endl;
            throw e;
        }

    }

    T operator()()
    {
        return std::move(handle.promise().result);
    }

    bool done() const
    {
        return handle.done();
    }

    bool valid() const
    {
        return handle != nullptr;
    }
private:
    std::coroutine_handle<promise_type> handle;
};

template<typename T>
using Task = Task_t<T>;

}
#endif
