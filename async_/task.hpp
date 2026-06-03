#ifndef __TASK_CPP__
#define __TASK_CPP__
#include <optional>
#include <coroutine>

template <typename Handle, typename U = std::void_t<>>
struct handle_has_val : std::false_type
{
};

// 特化版本
template <typename Handle>
struct handle_has_val<Handle, std::void_t<decltype(std::declval<Handle>().promise()._val)>> : std::true_type
{
};

template <typename Handle>
inline constexpr bool handle_has_val_v = handle_has_val<Handle>::value;

template <typename T>
struct task;

template <typename T>
struct promise_type_t
{
    struct ret_caller
    {
        bool await_ready() noexcept { return false; }
        std::coroutine_handle<> await_suspend(std::coroutine_handle<>) noexcept
        {
            if (caller_handle)
            {
                return caller_handle; // 对称协程 转到调用者协程
            }
            return std::noop_coroutine();
        }
        void await_resume() noexcept { return; }
        std::coroutine_handle<> caller_handle;
    };

    task<T> get_return_object()
    {
        return {std::coroutine_handle<promise_type_t<T>>::from_promise(*this)};
    }
    std::suspend_always initial_suspend() { return {}; }
    ret_caller final_suspend() noexcept { return {_caller_handle}; }
    void unhandled_exception() noexcept {
        _exception = std::current_exception();
    }

    void return_value(std::optional<T> value)
    {
        _val = std::move(value);
    }

    std::coroutine_handle<> _caller_handle;
    std::optional<T> _val{std::nullopt};
    std::exception_ptr _exception;
};

template <>
struct promise_type_t<void>
{
    struct ret_caller
    {
        bool await_ready() noexcept { return false; }
        std::coroutine_handle<> await_suspend(std::coroutine_handle<>) noexcept
        {
            if (caller_handle)
            {
                return caller_handle; // 对称协程 转到调用者协程
            }
            return std::noop_coroutine();
        }
        void await_resume() noexcept { return; }
        std::coroutine_handle<> caller_handle;
    };

    task<void> get_return_object();
    std::suspend_always initial_suspend() { return {}; }
    ret_caller final_suspend() noexcept { return {_caller_handle}; }
    void unhandled_exception() noexcept {
        _exception = std::current_exception();
    }

    void return_void() {}

    std::coroutine_handle<> _caller_handle;
    std::exception_ptr _exception;
};

template <typename T>
struct task
{

    using promise_type = typename std::conditional_t<std::is_void_v<T>, promise_type_t<void>, promise_type_t<T>>;

    std::coroutine_handle<promise_type> handle;

    struct save_caller
    {
        std::coroutine_handle<promise_type> _handle;
        bool await_ready() { return false; }
        auto await_suspend(std::coroutine_handle<> caller_handle)
        {
            _handle.promise()._caller_handle = caller_handle; // 存储调用者句柄 用于执行后恢复
            return _handle;
        }
        std::conditional_t<handle_has_val_v<decltype(_handle)>, std::optional<T>, void>
        await_resume()
        {
            if (_handle.promise()._exception) {
                std::rethrow_exception(_handle.promise()._exception);
            }
            if constexpr (handle_has_val_v<decltype(_handle)>) {
                return std::move(_handle.promise()._val);
            }
            else {
                return;
            }
        }
    };

    auto operator co_await() &&
    {
        return save_caller{handle};
    }

    std::coroutine_handle<promise_type> release() noexcept {
        return std::exchange(handle, nullptr);
    }

    task(std::coroutine_handle<promise_type> h) : handle(h) {}

    // 析构：仅销毁非空句柄
    ~task()
    {
        if (handle)
        {
            handle.destroy();
            handle = nullptr;
        }
    }

    // 移动构造函数：转移所有权
    task(task &&other) noexcept : handle(std::exchange(other.handle, nullptr)) {}
    task &operator=(task &&other) noexcept
    {
        if (this != &other)
        {
            if (handle)
                handle.destroy();
            handle = std::exchange(other.handle, nullptr);
        }
        return *this;
    }
    // 禁止拷贝
    task(const task &) = delete;
    task &operator=(const task &) = delete;
};

task<void> promise_type_t<void>::get_return_object()
{
    return {std::coroutine_handle<promise_type_t<void>>::from_promise(*this)};
}

#endif