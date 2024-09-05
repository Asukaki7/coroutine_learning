#pragma once

#include <coroutine>
#include <co_async/task.hpp>

namespace co_async {

struct CurrentCoroutineAwaiter {
    bool await_ready() const noexcept {
        return false;
    }

    std::coroutine_handle<>
    await_suspend(std::coroutine_handle<> coroutine) noexcept {
        mCurrent = coroutine;
        return coroutine;
    }

    auto await_resume() const noexcept {
        return mCurrent;
    }

    std::coroutine_handle<> mCurrent;
};

inline Task<std::coroutine_handle<>> current_coroutine() {
    co_return co_await CurrentCoroutineAwaiter();
}

} // namespace co_async
