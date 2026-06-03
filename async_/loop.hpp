#ifndef __LOOP_CPP__
#define __LOOP_CPP__

#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <strings.h>

#include <iostream>
#include <stop_token>
#include <coroutine>
#include <deque>
#include <memory>
#include <queue>
#include <chrono>
#include <thread>
#include <mutex>

#include "./async_epoll.hpp"

struct _loop
{
private:
    _loop() = default;

public:
    ~_loop() = default;
    _loop(const _loop &) = delete;
    _loop &operator=(const _loop &) = delete;

    std::deque<std::coroutine_handle<>> tasks;

    static std::weak_ptr<_loop> make() {
        static std::shared_ptr<_loop> ptr(new _loop{});
        return ptr;
    }

    _loop &add_task(std::coroutine_handle<> handle)
    {
        tasks.push_back(handle);
        return *this;
    }

    void run()
    {
        while (!tasks.empty())
        {
            auto handle = tasks.front();
            tasks.pop_front();
            if (handle && !handle.done())
            {
                handle.resume();
            }
        }
    }
};


struct timer_loop
{
    int THREAD_NUM = 4;
    
public:
    using time_handle_pair = std::pair<std::chrono::steady_clock::time_point, std::coroutine_handle<>>;
    using timer_tasks_type = std::priority_queue<time_handle_pair, std::vector<time_handle_pair>, std::greater<time_handle_pair>>;
    using ready_tasks_type = std::deque<std::coroutine_handle<>>;

    
    struct thread_tasks_struct {
        struct {
            std::mutex mtx;
            timer_tasks_type tasks;
        } timer_tasks;
        struct {
            std::mutex mtx;
            ready_tasks_type tasks;
        } ready_tasks;
    };
    std::unordered_map<std::thread::id, thread_tasks_struct> thread_tasks;
    // decltype(timer_tasks) &tasks = timer_tasks;

    std::atomic<bool> use_epoll{false};
    epoll_manager epollfd{};

    std::deque<std::thread> thread_dq;

private:
    timer_loop() : epollfd() {
        for (int i{0}; i < THREAD_NUM; ++i) {
            thread_dq.emplace_back(&timer_loop::_run, this);
            thread_tasks.try_emplace(thread_dq.back().get_id());
        }
    };

public:
    ~timer_loop() {};
    timer_loop(const timer_loop &) = delete;
    timer_loop &operator=(const timer_loop &) = delete;

    static std::weak_ptr<timer_loop> make() {
        static std::shared_ptr<timer_loop> ptr(new timer_loop{});
        return ptr;
    }

    timer_loop &add_task(std::coroutine_handle<> handle)
    {
        if (auto it = thread_tasks.find(std::this_thread::get_id()); it != thread_tasks.end()) {
            auto &[mtx, ready_tasks] = it->second.ready_tasks;
            std::unique_lock<std::mutex> lock(mtx);
            ready_tasks.emplace_back(handle);
        } else {
            auto &[mtx, ready_tasks] = thread_tasks.begin()->second.ready_tasks;
            std::unique_lock<std::mutex> lock(mtx);
            ready_tasks.emplace_back(handle);
        }
        return *this;
    }

    timer_loop &add_task(std::coroutine_handle<> handle, std::chrono::milliseconds delay)
    {
        auto deadline = std::chrono::steady_clock::now() + delay;
        auto &[mtx, timer_tasks] = thread_tasks[std::this_thread::get_id()].timer_tasks;
        std::unique_lock<std::mutex> lock(mtx);
        timer_tasks.emplace(deadline, handle);
        return *this;
    }

    void run_thread() {
        for (auto& t : thread_dq) {
            t.join();
        }
    }

    void _run()
    {
        auto &[timer_tasks, ready_tasks] = thread_tasks[std::this_thread::get_id()];
        while (true)
        {
            {
                std::unique_lock<std::mutex> lock(ready_tasks.mtx);
                if (!ready_tasks.tasks.empty()) {
                    while (!ready_tasks.tasks.empty()) {
                        auto& handle {ready_tasks.tasks.front()};
                        ready_tasks.tasks.pop_front();
                        lock.unlock();
                        if (handle && !handle.done()) {
                            std::cout << "run handle " << handle.address() << std::endl;
                            handle.resume();
                        }
                        lock.lock();
                    }
                    lock.unlock();
                } else {
                    lock.unlock();
                    // 窃取任务
                    for (auto &[id, str] : thread_tasks) {
                        if (id == std::this_thread::get_id())
                            continue;
                        std::unique_lock<std::mutex> lock(str.ready_tasks.mtx);
                        if (str.ready_tasks.tasks.size() >= 2) {
                            auto handle{std::move(str.ready_tasks.tasks.back())};
                            str.ready_tasks.tasks.pop_back();
                            lock.unlock();
                            std::cout << "task [" << id << "] -> [" << std::this_thread::get_id() << "]" << std::endl;
                            if (handle && !handle.done()) {
                                std::cout << "run handle " << handle.address() << std::endl;
                                handle.resume();
                            }
                            break;
                        }
                    }
                }
            }
            {
                //std::unique_lock<std::mutex> lock(timer_tasks.mtx);
                if (!timer_tasks.tasks.empty()) {
                    while (true) {
                        auto &deadline{timer_tasks.tasks.top().first};
                        //lock.unlock();
                        if (deadline <= std::chrono::steady_clock::now()) {
                            //lock.lock();
                            auto handle{timer_tasks.tasks.top().second};
                            timer_tasks.tasks.pop();
                            //lock.unlock();
                            add_task(handle);
                        } else {
                            bool is_use{false};
                            use_epoll.compare_exchange_strong(is_use, true);
                            if (!is_use) {
                                auto &&ready_deq = epollfd.run(deadline);
                                use_epoll.store(false);
                                if (!ready_deq.empty()) {
                                    for (auto &handle : ready_deq) {
                                        std::cout << "epoll back, add new task" << std::endl;
                                        add_task(handle);
                                    }
                                    break;
                                }
                            }
                        }
                    }
                } else {
                    //lock.unlock();
                    bool is_use{false};
                    use_epoll.compare_exchange_strong(is_use, true);
                    if (!is_use) {
                        auto &&ready_deq = epollfd.run();
                        use_epoll.store(false);
                        if (!ready_deq.empty()) {
                            for (auto &handle : ready_deq) {
                                std::cout << "epoll back, add new task" << std::endl;
                                add_task(handle);
                            }
                        }
                    }
                }
            }
            
        }
    }
};

using loop = timer_loop;

struct jump_loop
{
    bool await_ready() { return false; }
    auto await_suspend(std::coroutine_handle<> handle)
    {
        loop::make().lock()->add_task(handle); // 存储调用者句柄 用于执行后恢复
        return std::noop_coroutine();
    }
    void await_resume() { return; }
};
using async_wait = jump_loop;


struct to_epoll_in
{
    bool await_ready() { return false; }
    auto await_suspend(std::coroutine_handle<> handle)
    {
        timer_loop::make().lock()->epollfd.add_event({.fd = _fd, .in_handle = handle});
        return std::noop_coroutine();
    }
    void await_resume() { return; }
    to_epoll_in(int fd) : _fd(fd) {}
    int _fd;
};

struct to_epoll_out
{
    bool await_ready() { return false; }
    auto await_suspend(std::coroutine_handle<> handle)
    {
        timer_loop::make().lock()->epollfd.add_event({.fd = _fd, .out_handle = handle});
        return std::noop_coroutine();
    }
    void await_resume() { return; }
    to_epoll_out(int fd) : _fd(fd) {}
    int _fd;
};

task<ssize_t> async_read(int fd, char *data, ssize_t size) {
    // 先直接读取 读取失败再注册epoll
    ssize_t count = 0;
    bool wait = false;
    while (size > count) {
        std::cout << "async_read" << std::endl;
        if (wait)
            co_await to_epoll_in{fd};
        ssize_t n = recv(fd, data + count, size - count, MSG_NOSIGNAL);
        if (n == 0) {
            count += n;
            co_return count;
        } else if (n < 0) {
            if (errno == EAGAIN) {
                if (count > 0)
                    co_return count;
                else 
                    wait = true;
            } else if (errno == EINTR) {
                wait = true;
            } else {
                std::cout << "errno read: " << errno << std::endl;
                co_return -1;
            }
        } else {
            count += n;
            wait = false;
        }
    }
    co_return count;
}


task<ssize_t> async_write(int fd, const char *data, ssize_t size) {
    // 先直接读取 读取失败再注册epoll
    ssize_t count = 0;
    bool wait = false;
    while (size > count)
    {
        // 当前无数据可读，退出循环，等待下次 EPOLLIN
        std::cout << "async_write" << std::endl;
        if (wait)
            co_await to_epoll_out{fd};
        ssize_t n = send(fd, data + count, size - count, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EAGAIN) {
                if (count < size)
                    wait = true;
                else
                    co_return count;
            } else if (errno == EINTR) {
                wait = true;
            } else {
                co_return -1;
            }
        } else {
            count += n;
            wait = false;
        }
    }
    co_return count;
}

struct async_listen {
    int listenfd;
    int LISTEN_QUE_NUM = 200;

    std::stop_source source;
    std::mutex mtx;
    std::deque<std::coroutine_handle<>> accept_wait_que;
    std::deque<std::coroutine_handle<>> accept_que;
    std::unique_ptr<task<void>> send_accept_task_ptr;

public:
    async_listen() {}
    ~async_listen() {
        source.request_stop();
        send_accept_task_ptr.get()->release();
    }
    int bind_listen(const std::string_view &ip, int port)
    {
        listenfd = socket(PF_INET, SOCK_STREAM, 0);
        int opt = 1;
        if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
            ;
        // std::cout << "err: setsockopt();" << std::endl;

        struct sockaddr_in addr;
        bzero(&addr, sizeof(addr));
        addr.sin_family = AF_INET;
        if (ip.empty())
            addr.sin_addr.s_addr = INADDR_ANY;
        else
            inet_pton(AF_INET, ip.data(), &addr.sin_addr);
        addr.sin_port = htons(port);

        bind(listenfd, (struct sockaddr *)&addr, sizeof(addr));

        listen(listenfd, LISTEN_QUE_NUM);
        send_accept_task_ptr = std::make_unique<task<void>>(std::move(this->send_acceptfd(source.get_token())));
        loop::make().lock()->epollfd.add_event({.fd = listenfd, .in_handle = send_accept_task_ptr.get()->handle}, false);
        return listenfd;
    }

    struct out_loop {
        bool await_ready() { return false; }
        auto await_suspend(std::coroutine_handle<>) {
            return std::noop_coroutine();
        }
        void await_resume() { return; }
    };

    task<void> send_acceptfd(std::stop_token token)
    {
        while (!token.stop_requested())
        {
            bool f = false;
            {
                std::unique_lock<std::mutex> lock(mtx);
                if (!accept_wait_que.empty()) {
                    swap(accept_wait_que, accept_que);
                    f = true;
                }
            }
            if (f) {
                while (!accept_que.empty()) {
                    auto handle = accept_que.front();
                    accept_que.pop_front();
                    loop::make().lock()->add_task(handle);
                }
            }
            //co_await back_accept_handle{handle};
            co_await out_loop{}; // 直接返回->事件循环
        }
    }

    struct wait_accept {
        bool await_ready() { return false; }
        auto await_suspend(std::coroutine_handle<> handle) {
            {
                std::unique_lock<std::mutex> lock(_con.mtx);
                _con.accept_wait_que.push_back(handle); // 等待listen监听获取新连接
                lock.unlock();
            }
            return std::noop_coroutine(); // 返回事件循环
        }
        void await_resume() { return; }

        wait_accept(async_listen &con) : _con(con) {}
        async_listen &_con;
    };

    task<int> async_accept(struct sockaddr_in &client)
    {

        socklen_t clientL = sizeof(client);
        std::cout << "async_accept" << std::endl;
        int connfd = accept(listenfd, (struct sockaddr *)&client, &clientL);
        while (connfd < 0)
        {
            std::cout << "async_accept" << std::endl;
            co_await wait_accept{*this};
            connfd = accept(listenfd, (struct sockaddr *)&client, &clientL);
            if (connfd > 0) {
                break;
            }
            else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            } else {
                co_return -1;
            }
        }
        int flags = fcntl(connfd, F_GETFL, 0);
        fcntl(connfd, F_SETFL, flags | O_NONBLOCK);
        co_return connfd;
    }
};

#endif