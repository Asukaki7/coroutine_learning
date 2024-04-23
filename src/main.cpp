
#include <chrono>
#include <coroutine>
#include <thread>
#include <format>
#include <variant>
#include <iostream>
#include <deque>
#include <queue>
#include <memory>

using std::cout;
using namespace std::chrono_literals;

template <class T>
union Uninitialized {
	T mValue;

	Uninitialized() noexcept{}
	Uninitialized(Uninitialized &&) = delete;
	~Uninitialized() noexcept {}

	T moveValue() {
		T ret(std::move(mValue));
		mValue.~T();
		return ret;
	}

	template <class ...Ts>
	void putValue(Ts &&...args) {
		new (std::addressof(mValue)) T(std::forward<Ts>(args)...);
	}
};

struct suspendAlways
{
	bool await_ready() const noexcept { return false; }

	void await_suspend(std::coroutine_handle<>) const noexcept {}

	void await_resume() const noexcept {}

};

struct suspendNever
{
	bool await_ready() const noexcept {
		return true;
	}

	void await_suspend(std::coroutine_handle<>) const noexcept {}

	void await_resume() const noexcept {}
};


struct repeatAwaiter //awaiter(raw pointer) / awaitable(operator->)
{
	bool await_ready() const noexcept { return false; }

	std::coroutine_handle<> await_suspend(std::coroutine_handle<> coroutine) const noexcept {
		if (coroutine.done())
			return std::noop_coroutine();
		else
			return coroutine;
	}

	void await_resume() const noexcept {}
};

struct RepeatAwaitable { //awaitable
	repeatAwaiter operator co_await() {
		return repeatAwaiter();
	}
};

struct PreviousAwaiter {
	std::coroutine_handle<> mPrevious;

	bool await_ready() const noexcept { return false; }

	std::coroutine_handle<> await_suspend(std::coroutine_handle<> coroutine) const noexcept {
		if (mPrevious)
			return mPrevious;
		else
			return std::noop_coroutine();
	}

	void await_resume() const noexcept {}
};


template <class T>
struct Promise {

	auto initial_suspend() {
		return suspendAlways();
	}

	auto final_suspend() noexcept {
		return PreviousAwaiter(mPrevious);
	}

	void unhandled_exception() {
		mException = std::current_exception();
	}

	auto yield_value(T ret) noexcept {
		new (&mResult) T(std::move(ret));
		return std::suspend_always();
	}

	/* 	void return_void() {
			mreturn_value = 0;
		} */

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
	Promise(Promise&&) = delete;
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

	void return_void() noexcept {
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

	Promise() = default;
	Promise(Promise&&) = delete;
	~Promise() = default;
};


template <class T>
struct Task {
	using promise_type = Promise<T>;

	Task(std::coroutine_handle<promise_type> coroutine)
		: mCoroutine(coroutine) {}

	Task(Task&&) = delete;

	~Task() {
		mCoroutine.destroy();
	}

	struct Awaiter {
		bool await_ready() const noexcept {
			return false;
		}

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

	operator std::coroutine_handle<>() const noexcept {
		return mCoroutine;
	}

	std::coroutine_handle<promise_type> mCoroutine;
};


template<class T>
struct WorldTask {
	using promise_type = Promise<T>;

	WorldTask(std::coroutine_handle<promise_type> coroutine)
		: mCoroutine(coroutine) {}

	WorldTask(WorldTask&&) = delete;

	~WorldTask() {
		mCoroutine.destroy();
	}
	struct WorldAwaiter {
		bool await_ready() const noexcept {
			return false;
		}
		std::coroutine_handle<> await_suspend(std::coroutine_handle<> coroutine) const noexcept {
			mCoroutine.promise().mPrevious = coroutine;
			return mCoroutine;
		}

		auto await_resume() const noexcept {
			return mCoroutine.promise().result();
		}

		std::coroutine_handle<promise_type> mCoroutine;
	};

	auto operator co_await() {
		return WorldAwaiter(mCoroutine);
	}

	std::coroutine_handle<promise_type> mCoroutine;
};

WorldTask<int> world() {
	std::cout << "world" << std::endl;
	co_return 11;
}

struct Loop {
	std::deque<std::coroutine_handle<>> mReadyQueue;


	struct TimerEntry {
		std::chrono::system_clock::time_point expireTime;
		std::coroutine_handle<> coroutine;
		
		bool operator<(TimerEntry const& that) const noexcept {
			return expireTime > that.expireTime;
		}
	};

	std::priority_queue<TimerEntry> mTimerHeap;

	void addTask(std::coroutine_handle<> t) {
		mReadyQueue.push_front(t);
	}

	void addTimer(std::chrono::system_clock::time_point expireTime, std::coroutine_handle<> t) {
		mTimerHeap.push({ expireTime, t });
	}

	void runAll() {
		while (!mTimerHeap.empty() || !mReadyQueue.empty()) {
			while (!mReadyQueue.empty()) {
				auto readyTask = mReadyQueue.front();
				cout << "pop\n";
				mReadyQueue.pop_front();
				readyTask.resume();
			}
			if (!mTimerHeap.empty()) {
				auto nowTime = std::chrono::system_clock::now();
				const auto timer = std::move(mTimerHeap.top());
				if (timer.expireTime < nowTime) {
					mTimerHeap.pop();
					timer.coroutine.resume();
				} else {
					std::this_thread::sleep_until(timer.expireTime);
				}
				
			}
		}
	}

	Loop& operator=(Loop&&) = delete;
};



Loop& getLoop() {
	static Loop loop;
	return loop;
}




struct SleepAwaiter {
	bool await_ready() const {
		return std::chrono::system_clock::now() >= mExpireTime;
	}

	void await_suspend(std::coroutine_handle<> coroutine) const {
		getLoop().addTimer(mExpireTime, coroutine);
	}
	void await_resume() const noexcept {

	}

	std::chrono::system_clock::time_point mExpireTime;
};


Task<void> sleep_until(std::chrono::system_clock::time_point expireTime) {
	co_await SleepAwaiter(expireTime);
	co_return;

}

Task<void> sleep_for(std::chrono::system_clock::duration duration) {
	co_await sleep_until(std::chrono::system_clock::now() + duration);
	co_return;
}



Task<std::string> baby() {
	std::cout << "baby" << std::endl;
	co_return "aaa";
}

Task<int> hello1() {
	std::cout << "hello1 start sleeping 1s\n";
	co_await sleep_for(std::chrono::seconds(1));
	std::cout << "hello1 wake up\n";
	co_return 1;
}

Task<int> hello2() {
	std::cout << "hello2 start sleeping 2s\n";
	co_await sleep_for(std::chrono::seconds(2));
	std::cout << "hello2 wake up\n";
	co_return 2;
}

struct ReturnPreviousPromise {
    auto initial_suspend() noexcept {
        return std::suspend_always();
    }

    auto final_suspend() noexcept {
        return PreviousAwaiter(mPrevious);
    }

    void unhandled_exception() {
        throw;
    }

    void return_value(std::coroutine_handle<> previous) noexcept {
        mPrevious = previous;
    }

    auto get_return_object() {
        return std::coroutine_handle<ReturnPreviousPromise>::from_promise(
            *this);
    }

    std::coroutine_handle<> mPrevious{};

    ReturnPreviousPromise &operator=(ReturnPreviousPromise &&) = delete;
};
struct whenAllCounterBlock {
	std::size_t mCount;
	std::coroutine_handle<> mPrevious;
};

struct ReturnPreviousTask {
    using promise_type = ReturnPreviousPromise;

    ReturnPreviousTask(std::coroutine_handle<promise_type> coroutine) noexcept
        : mCoroutine(coroutine) {}

    ReturnPreviousTask(ReturnPreviousTask &&) = delete;

    ~ReturnPreviousTask() {
        mCoroutine.destroy();
    }

    std::coroutine_handle<promise_type> mCoroutine;
};

struct WhenAllAwaiter {
	bool await_ready() const noexcept {
		return false;
	}

	auto await_suspend(std::coroutine_handle<> coroutine) const {
		counter.mPrevious = coroutine;
		getLoop().addTask(t2.mCoroutine);
		return t1.mCoroutine;
	}

	void await_resume() const noexcept {

	}

	whenAllCounterBlock &counter;
	ReturnPreviousTask const &t1;
	ReturnPreviousTask const &t2;
};


template <class T>
ReturnPreviousTask whenALLHelper(Task<T> const &t, whenAllCounterBlock &counter, Uninitialized<T> &result) {
	result.putValue(co_await t);
	--counter.mCount;
	std::coroutine_handle<> previous = nullptr;
	if (counter.mCount == 0) {
		previous = counter.mPrevious;
	}
	co_return previous;
}


template <class T1, class T2>
Task<std::tuple<T1, T2>> when_all(Task<T1> const &t1, Task<T2> const &t2) {
	whenAllCounterBlock counter;
	std::tuple<Uninitialized<T1>, Uninitialized<T2>> result;
	counter.mCount = 2;
	counter.mCount = 2;
	co_await WhenAllAwaiter(counter, 
							whenALLHelper(t2, counter, std::get<0>(result)), 
							whenALLHelper(t1, counter, std::get<1>(result)));
	co_return std::tuple<T1, T2>(std::get<0>(result).moveValue(),
								 std::get<1>(result).moveValue());
}



Task<int> hello() {
	cout << "hello is waiting for 1 and 2\n";
	auto [i, j] = co_await when_all(hello1(), hello2());
	cout << "hello is waiting\n";
	co_return i + j;
}

int main() {

	auto t = hello();
	getLoop().addTask(t);

	getLoop().runAll();

	cout << "main acquire hello res: " << t.mCoroutine.promise().result() << std::endl;
	
}