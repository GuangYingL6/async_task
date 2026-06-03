#ifndef __ASYNC_MUTEX_CPP__
#define __ASYNC_MUTEX_CPP__
//#include <iostream>
#include <optional>
#include <coroutine>
#include <mutex>
#include <deque>

#include "./task.hpp"
#include "./loop.hpp"

struct async_mutex
{
private:
    std::mutex mtx;

    std::mutex que_mtx;
    std::deque<std::coroutine_handle<>> lock_que;

public:
    async_mutex() {}
    ~async_mutex() {}

    void push(std::coroutine_handle<> handle) {
        std::unique_lock<std::mutex> lock(que_mtx);
        lock_que.push_back(handle);
    }

    std::coroutine_handle<> pop() {
        std::unique_lock<std::mutex> lock(que_mtx);
        if (!lock_que.empty()) {
            auto handle = lock_que.front();
            lock_que.pop_front();
            return handle;
        }
        return {};
    }

    std::mutex &get_mtx() { return mtx; }
};

struct async_lock
{
    async_mutex &_mtx;
    bool _is_unlock; // 是否释放过锁
    std::unique_lock<std::mutex> lk;

    struct lock_wait
    {
        bool await_ready() { return false; }
        auto await_suspend(std::coroutine_handle<> handle)
        {
            _mtx.push(handle); // 存入锁等待队列
            return std::noop_coroutine();
        }
        void await_resume() { return; }

        lock_wait(async_mutex& mtx) : _mtx(mtx) {};

        async_mutex &_mtx;
    };

public:
    async_lock(async_mutex &mtx) : _mtx(mtx), _is_unlock(true), lk(_mtx.get_mtx(), std::defer_lock) {}
    ~async_lock()
    {
        if (!_is_unlock) {
            lk.unlock();
            auto handle = _mtx.pop();
            if (handle)
                loop::make().lock()->add_task(handle);
        }
    }
    task<void> lock()
    {
        if (!_is_unlock) {
            co_return; // 已持有锁未释放
        } else {
            while (!lk.try_lock()) { // 尝试获取锁
                co_await lock_wait{_mtx};
            }
        }
        _is_unlock = false;
        //std::cout << "get lock" << std::endl;
        co_return;
    }

    void unlock()
    {
        if (!_is_unlock) {
            lk.unlock();
            auto handle = _mtx.pop();
            if (handle)
                loop::make().lock()->add_task(handle);
            _is_unlock = true;
        }
        //std::cout << "unlock" << std::endl;
    }
};

struct async_conditionan_variable
{
    std::mutex que_mtx;
    std::deque<std::coroutine_handle<>> wait_que;

    void push(std::coroutine_handle<> handle) {
        std::unique_lock<std::mutex> lock(que_mtx);
        wait_que.push_back(handle);
    }
    std::coroutine_handle<> pop() {
        std::unique_lock<std::mutex> lock(que_mtx);
        if (!wait_que.empty()) {
            auto handle = wait_que.front();
            wait_que.pop_front();
            return handle;
        }
        return {};
    }

public:
    async_conditionan_variable() {}

    struct add_wait_que
    {
        bool await_ready() { return false; }
        auto await_suspend(std::coroutine_handle<> handle) {
            _cv.push(handle); // 存储调用者句柄
            return std::noop_coroutine();
        }
        void await_resume() { return; }

        add_wait_que(async_conditionan_variable &cv) : _cv(cv) {}
        async_conditionan_variable &_cv;
    };

    template <typename CallFunc>
    task<void> wait(async_lock &lock, CallFunc &&callfunc) {
        while (!callfunc()) {
            lock.unlock();
            co_await add_wait_que{*this}; // 解锁 等待
            co_await lock.lock();
        } // 条件满足 加锁
    }

    void notify_one() {
        auto handle = pop();
        if (handle)
            loop::make().lock()->add_task(handle);
    }

    void notify_all() {
        while (true) {
            auto handle = pop();
            if (handle)
                loop::make().lock()->add_task(handle);
            else
                break;
        }
    }
};




#endif