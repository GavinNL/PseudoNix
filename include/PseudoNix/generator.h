#ifndef PSEUDONIX_GENERATOR_H
#define PSEUDONIX_GENERATOR_H

#include <optional>
#include <coroutine>

namespace PseudoNix
{

template<typename T>
struct Generator {
    using value_type = T;

    struct promise_type {
        std::optional<T> current_value;

        Generator get_return_object() {
            return Generator{ std::coroutine_handle<promise_type>::from_promise(*this) };
        }

        std::suspend_always initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }

        std::suspend_always yield_value(T value) {
            current_value = std::move(value);
            return {};
        }

        void return_void() {}
        void unhandled_exception() { std::terminate(); }
    };

    std::coroutine_handle<promise_type> handle;

    explicit Generator(std::coroutine_handle<promise_type> h) : handle(h) {}
    Generator(const Generator&) = delete;
    Generator& operator=(const Generator&) = delete;
    Generator(Generator&& other) noexcept : handle(other.handle) { other.handle = nullptr; }
    ~Generator() { if (handle) handle.destroy(); }

    // Iterator support
    struct iterator {
        std::coroutine_handle<promise_type> handle;
        bool done = false;

        iterator& operator++() {
            handle.resume();
            done = handle.done();
            return *this;
        }

        T operator*() const {
            return *handle.promise().current_value;
        }

        bool operator==(std::default_sentinel_t) const { return done; }
    };

    iterator begin() {
        if (handle) {
            handle.resume();
        }
        return iterator{ handle, handle.done() };
    }

    std::default_sentinel_t end() { return {}; }
};

}

#endif

