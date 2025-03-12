//
// Created by EmTVt on 2025/3/12.
//

#pragma once

#include <chrono>
#include <iostream>
#include <coroutine>
#include <optional>
#include <exception>
#include <thread>
#include <variant>
#include <span>
using std::cout;
using std::endl;
using namespace std::chrono_literals;

namespace co_async_cliiii {
//防止tuple或者variant中有void, 所以用一层类型结构体封装一下, 防止出现tuple<void>(这是未定义行为)
template <class T = void>
struct NonVoidHelper {
    using Type = T;
};
template <>
struct NonVoidHelper<void> {
    using Type = NonVoidHelper;

    explicit NonVoidHelper() = default;
};

//Uninitialized的作用: 主要是进行延迟构造, 防止tuple<void>出现无法构造的情况.
//所以套一层结构体, 这样tuple只会自动初始化Uninitialized
template<class T>
struct Uninitialized {
    union {
        T mValue;
    };
    Uninitialized() {}
    ~Uninitialized() {}
    Uninitialized(Uninitialized&&) = delete;

    T moveValue() {
        T ret(std::move(mValue));
        mValue.~T();
        return ret;
    }

    template<class... Args>
    void putValue(Args&&... args) {
        new (&mValue) T(std::forward<Args>(args)...);
    }
};
template <>
struct Uninitialized<void> {
    auto moveValue() {
        return NonVoidHelper<>{};
    }

    void putValue(NonVoidHelper<>) {}
};
template <class T>
struct Uninitialized<T const> : Uninitialized<T> {};
template <class T>
struct Uninitialized<T &> : Uninitialized<std::reference_wrapper<T>> {};
template <class T>
struct Uninitialized<T &&> : Uninitialized<T> {};

template <class T>
concept Awaiter = requires(T a, std::coroutine_handle<> h) {
    { a.await_ready() };
    { a.await_suspend(h) };
    { a.await_resume() };
};

template <class T>
concept Awaitable = Awaiter<T> || requires(T a) {
    { a.operator co_await() } -> Awaiter;
};

//提取Awaitable的返回值类型, 由于whenAllImpl中只有Args...这个参数, 所以通过这个结构体能够知道协程的返回值从而创建tuple
template <class T>
struct AwaitableTraits;

template <Awaiter T>
struct AwaitableTraits<T> {
    using RetType = decltype(std::declval<T>().await_resume());
    using NonVoidRetType = NonVoidHelper<RetType>::Type;
};

template <class T>
    requires(!Awaiter<T> && Awaitable<T>)
struct AwaitableTraits<T> : AwaitableTraits<decltype(std::declval<T>().operator co_await())> {};


struct Suspend {
    bool await_ready() noexcept{ return false; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) noexcept{
        if (h_ && !h_.done()) return h_;
        return std::noop_coroutine();
    }
    void await_resume() noexcept{}

    std::coroutine_handle<> h_;
};

template<class T = void>
struct Promise {
    std::suspend_always initial_suspend() {return {};}
    Suspend final_suspend() noexcept{return {h_};}
    std::coroutine_handle<Promise> get_return_object() {return std::coroutine_handle<Promise>::from_promise(*this);}
    void unhandled_exception() {
        mException = std::current_exception();
    }
    void return_value(T&& x) {
        val.putValue(std::move(x));
    }
    void return_value(const T& x) {
        val.putValue(x);
    }
    T result() {
        if (mException) std::rethrow_exception(mException);
        return val.moveValue();
    }
    Promise& operator=(Promise&&) = delete;

    std::exception_ptr mException = nullptr;
    Uninitialized<T> val;
    std::coroutine_handle<> h_;
};

template<>
struct Promise<void> {
    std::suspend_always initial_suspend() {return {};}
    Suspend final_suspend() noexcept{return {h_};}
    std::coroutine_handle<Promise> get_return_object() {return std::coroutine_handle<Promise>::from_promise(*this);}
    void unhandled_exception() {
        mException = std::current_exception();
    }
    void return_void() {}
    void result() {
        if (mException) std::rethrow_exception(mException);
    }
    std::exception_ptr mException = nullptr;
    std::coroutine_handle<> h_;
};

template<class T>
struct Task {
    using promise_type = Promise<T>;
    struct Awaitable {
        bool await_ready() {return false;}
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
            h_.promise().h_ = h;
            return h_;
        }
        T await_resume() {
            return h_.promise().result();
        }

        std::coroutine_handle<promise_type> h_;
    };

    Awaitable operator co_await() const{
        return Awaitable{h_};
    }

    Task(std::coroutine_handle<promise_type> h) : h_(h) {}
    Task(Task &&) = delete;

    operator std::coroutine_handle<>() const{
        return h_;
    }

    std::coroutine_handle<promise_type> h_;
};

struct Loop {
    struct TimerEntity {
        std::chrono::system_clock::time_point expireTime;
        std::coroutine_handle<> h_;
        bool operator<(const TimerEntity& other) const {
            return expireTime > other.expireTime;
        }
    };

    std::priority_queue<TimerEntity> mTimerHeap;
    std::deque<std::coroutine_handle<>> mReadyQueue;

    void addTask(std::coroutine_handle<> h) {
        mReadyQueue.push_front(h);
    }

    void addTimer(std::chrono::system_clock::time_point expireTime, std::coroutine_handle<> h) {
        mTimerHeap.push({expireTime, h});
    }

    void runAll() {
        while (!mTimerHeap.empty() || !mReadyQueue.empty()) {
            while (!mReadyQueue.empty()) {
                auto h = mReadyQueue.front();
                mReadyQueue.pop_front();
                h.resume();
            }
            if (!mTimerHeap.empty()) {
                auto nowTime = std::chrono::system_clock::now();
                auto timer = std::move(mTimerHeap.top());
                if (timer.expireTime < nowTime) {
                    mTimerHeap.pop();
                    timer.h_.resume();
                } else {
                    std::this_thread::sleep_until(timer.expireTime);
                }
            }
        }
    }

    void clear() {
        while (!mTimerHeap.empty()) {mTimerHeap.pop();}
    }

    Loop& operator=(Loop&&) = delete;
};

Loop& getLoop() {
    static Loop loop;
    return loop;
}

struct SleepAwaiter {
    bool await_ready() {
        return std::chrono::system_clock::now() >= mExpireTime;
    }
    void await_suspend(std::coroutine_handle<> h) {
        getLoop().addTimer(mExpireTime, h);
    }
    void await_resume() {}

    std::chrono::system_clock::time_point mExpireTime;
};

Task<void> sleep_for(std::chrono::system_clock::duration d) {
    co_await SleepAwaiter(std::chrono::system_clock::now() + d);
    co_return;
}

struct whenAllBlock {
    std::size_t  mCount;
    std::coroutine_handle<> h_;
};

struct ReturnPreviousPromise {
    std::suspend_always initial_suspend() { return {}; }
    Suspend final_suspend() noexcept { return {mPrevious}; }
    void unhandled_exception() {}
    std::coroutine_handle<> get_return_object() {
        return std::coroutine_handle<ReturnPreviousPromise>::from_promise(*this);
    }
    void return_value(std::coroutine_handle<> h) {
        mPrevious = h;
    }
    std::coroutine_handle<> mPrevious;
};

struct ReturnPreviousTask {
    using promise_type = ReturnPreviousPromise;
    ReturnPreviousTask() {}
    ReturnPreviousTask(std::coroutine_handle<> h) : h_(h) {}
    std::coroutine_handle<> h_;
};

struct WhenAllAwaiter {
    bool await_ready() { return false; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
        if (mTasks.empty()) return h;
        block.h_ = h;
        for (const auto& t : mTasks.subspan(1)) {
            getLoop().addTask(t.h_);
        }
        return mTasks.front().h_;
    }
    void await_resume() {}
    whenAllBlock& block;
    std::span<const ReturnPreviousTask> mTasks;
};

//由WhenAllAwaiter中只能返回void, 将执行权交给main函数, 所以这里需要重新套一层协程任务, 这个协程里面多加一层逻辑判断,
//当最后一个任务完成时, 将执行权返回到when_all中.
//whenAllHelper要在结束的时候将执行权交给block中存储的h_.
//但是上面定义的Promise和Task, 只有co_await另外的协程任务时, 才能在结束的时候返回到原协程中.
//所以这里要重新创建一个promise => ReturnPreviousPromise
template<class T>
ReturnPreviousTask whenAllHelper(const Task<T>& t, whenAllBlock& block, Uninitialized<T>& result) {
    if constexpr (std::is_void_v<T>) {
        co_await t;
        result.putValue(NonVoidHelper<void>{});
    } else {
        result.putValue(co_await t);  //接收hello的返回值, 用它来初始化result.
    }
    block.mCount --;
    std::coroutine_handle<> h = nullptr;
    if (block.mCount == 0) {
        h = block.h_;
    }
    co_return h;
}

template<std::size_t... Is, class... Args>
Task<std::tuple<typename AwaitableTraits<Args>::NonVoidRetType...>>
whenAllImpl(std::index_sequence<Is...>, Args&&... args) {
    whenAllBlock block{sizeof...(Is)};

    //创建一个tuple来装下每一个协程的返回值
    std::tuple<Uninitialized<typename AwaitableTraits<Args>::RetType>...> result;

    ReturnPreviousTask taskArray[]{whenAllHelper(args, block, std::get<Is>(result))...};
    co_await WhenAllAwaiter(block, taskArray);
    co_return std::tuple<typename AwaitableTraits<Args>::NonVoidRetType...>{
        std::get<Is>(result).moveValue()...
    };
}

template<Awaitable... Args>
    requires(sizeof...(Args) != 0)
auto when_all(Args&&... args) {
    return whenAllImpl(std::make_index_sequence<sizeof...(Args)>{}, std::forward<Args>(args)...);
}


//===========

struct WhenAnyBlock {
    static constexpr std::size_t kNullIndex = std::size_t(-1);

    std::size_t mIndex{kNullIndex};
    std::coroutine_handle<> mPrevious{};
    std::exception_ptr mException{};
};

struct WhenAnyAwaiter {
    bool await_ready() const noexcept {
        return false;
    }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> coroutine) const {
        if (mTasks.empty()) return coroutine;
        mControl.mPrevious = coroutine;
        for (const auto& t: mTasks.subspan(1))
            getLoop().addTask(t.h_);
        return mTasks.front().h_;
    }

    void await_resume() {}

    WhenAnyBlock &mControl;
    std::span<ReturnPreviousTask const> mTasks;
};

template <class T>
ReturnPreviousTask whenAnyHelper(const auto& t, WhenAnyBlock& control,
                                 Uninitialized<T>& result, std::size_t index) {
    if constexpr (std::is_void_v<T>) {
        co_await t;
        result.putValue(NonVoidHelper<T>{});
    } else {
        result.putValue(co_await t);
    }
    control.mIndex = index;
    co_return control.mPrevious;
}

template <std::size_t... Is, class... Ts>
Task<std::variant<typename AwaitableTraits<Ts>::NonVoidRetType...>>
whenAnyImpl(std::index_sequence<Is...>, Ts &&...ts) {
    WhenAnyBlock control{};
    std::tuple<Uninitialized<typename AwaitableTraits<Ts>::RetType>...> result;
    ReturnPreviousTask taskArray[]{whenAnyHelper(ts, control, std::get<Is>(result), Is)...};
    co_await WhenAnyAwaiter(control, taskArray);
    Uninitialized<std::variant<typename AwaitableTraits<Ts>::NonVoidRetType...>> varResult;
    ((control.mIndex == Is && (varResult.putValue(
        std::in_place_index<Is>, std::get<Is>(result).moveValue()), 0)), ...);
    getLoop().clear();
    co_return varResult.moveValue();
}

template <Awaitable... Ts>
    requires(sizeof...(Ts) != 0)
auto when_any(Ts &&...ts) {
    return whenAnyImpl(std::make_index_sequence<sizeof...(Ts)>{},
                       std::forward<Ts>(ts)...);
}

}