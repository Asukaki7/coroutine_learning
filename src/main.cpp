
#include <chrono>
#include <coroutine>
#include <thread>
#include <format>
#include <variant>
#include <iostream>
#include <deque>
#include <queue>
#include <memory>
#include <optional>
#include "debug.hpp"

using std::cout;
using namespace std::chrono_literals;

struct RepeatAwaiter { // awaiter(原始指针)  / awaitbale(operator ->)
	bool await_ready() const noexcept { return false; }

	std::coroutine_handle<> await_suspend(std::coroutine_handle<> coroutine) const noexcept {
		if (coroutine.done()) {
			return std::noop_coroutine();
		} else {
			return coroutine;
		}
	}

	void await_resume() const noexcept {}
};

struct RepeatAwaitable { // awaitable 
	RepeatAwaiter operator co_await() {
		return RepeatAwaiter();
	}
};


struct PreviousAwaiter {
	std::coroutine_handle<> mPrevious;

	bool await_ready() const noexcept { return false; }

	std::coroutine_handle<> await_suspend(std::coroutine_handle<> coroutine) const noexcept {
		if (mPrevious) {
			return mPrevious;
		} else {
			return std::noop_coroutine();
		}
	}

	void await_resume() const noexcept {}
};


template <class T>
struct Promise {
    auto initial_suspend() noexcept {
        return std::suspend_always();
    }

    auto final_suspend() noexcept {
        return PreviousAwaiter(mPrevious);
    }

    void unhandled_exception() noexcept {
        mException = std::current_exception();
    }

    auto yield_value(T ret) noexcept {
        new (&mResult) T(std::move(ret));
        return std::suspend_always();
    }

    void return_value(T ret) noexcept {
        new (&mResult) T(std::move(ret));
    }

    T result() {
        if (mException) [[unlikely]] {
            std::rethrow_exception(mException);
        }
        T ret = std::move(mResult);
        mResult.~T();
        return ret;
    }

    std::coroutine_handle<Promise> get_return_object() {
        return std::coroutine_handle<Promise>::from_promise(*this);
    }

    std::coroutine_handle<> mPrevious{};
    std::exception_ptr mException{};
    union {
        T mResult;
    };

    Promise() noexcept {}
    Promise(Promise &&) = delete;
    ~Promise() {}
};


template <>
struct Promise<void> {
    auto initial_suspend() noexcept {
        return std::suspend_always();
    }

    auto final_suspend() noexcept {
        return PreviousAwaiter(mPrevious);
    }

    void unhandled_exception() noexcept {
        mException = std::current_exception();
    }

    void return_void() {

	}

    void result() {
        if (mException) [[unlikely]] {
            std::rethrow_exception(mException);
        }
    }

    std::coroutine_handle<Promise> get_return_object() {
        return std::coroutine_handle<Promise>::from_promise(*this);
    }

    std::coroutine_handle<> mPrevious{};
    std::exception_ptr mException{};


    Promise() noexcept {}
    Promise(Promise &&) = delete;
    ~Promise() {}
};

template <class T>
struct Task {
    using promise_type = Promise<T>;

    Task(std::coroutine_handle<promise_type> coroutine) noexcept
        : mCoroutine(coroutine) {}

    Task(Task &&) = delete;

    ~Task() {
        mCoroutine.destroy();
    }

    struct Awaiter {
        bool await_ready() const noexcept { return false; }

        std::coroutine_handle<promise_type> await_suspend(std::coroutine_handle<> coroutine) const noexcept {
            mCoroutine.promise().mPrevious = coroutine;
            return mCoroutine;
        }

        T await_resume() const {
            return mCoroutine.promise().result();
        }

        std::coroutine_handle<promise_type> mCoroutine;
    };

    auto operator co_await() const noexcept {
        return Awaiter(mCoroutine);
    }

    std::coroutine_handle<promise_type> mCoroutine;
};


/* template <class T>
struct WorldTask {
	using promise_type = Promise;

	WorldTask(std::coroutine_handle<Promise> coroutine): mCoroutine(coroutine) {
	}


	WorldTask(WorldTask &&) = delete;

	~WorldTask() {
		mCoroutine.destroy();
	}

	struct WorldAwaiter {
		bool await_ready() const noexcept { return false; }

		std::coroutine_handle<> await_suspend(std::coroutine_handle<> coroutine) const noexcept {
			mCoroutine.promise().mPrevious = coroutine;
			return mCoroutine;
		}

		auto await_resume() const noexcept { return mCoroutine.promise().mRetValue; }

		std::coroutine_handle<promise_type> mCoroutine;
	};
	
	auto operator co_await() {
		return WorldAwaiter(mCoroutine);
	}



	std::coroutine_handle<Promise<T>> mCoroutine;
}; */


Task<std::string> baby() {
    debug(), "baby";
    co_return "aaa\n";
}

Task<double> world() {
    debug(), "world";
    co_return 3.14;
}

Task<int> hello() {
    auto ret = co_await baby();
    debug(), ret;
    int i = (int)co_await world();
    debug(), "hello得到world结果为", i;
    co_return i + 1;
}



int main() {
	debug(), "main 即将调用hello";
	Task t = hello();
	debug(), "main 调用结束hello";
	while (!t.mCoroutine.done()) {
        t.mCoroutine.resume();
        debug(), "main得到hello结果为",
            t.mCoroutine.promise().result();
    }
	
}