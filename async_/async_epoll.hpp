#ifndef __ASYNC_EPOLL_CPP__
#define __ASYNC_EPOLL_CPP__
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>

#include <memory>
#include <unordered_map>
#include <deque>
#include <string_view>
#include <chrono>
#include <limits>
#include <mutex>

#include "./task.hpp"

struct async_event {
    int fd{-1};
    std::coroutine_handle<> in_handle{nullptr};
    std::coroutine_handle<> out_handle{nullptr};
};

int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, old_option | O_NONBLOCK);
    return old_option;
}

struct epoll_manager
{
    int _fd;
    int MAX_EPOLL_TABLE = 10000; // epoll内核表大小
    int MAX_EVENT_NUMBER = 4096; // 监听事件个数
    std::mutex mtx;
    std::unordered_map<int, async_event *> event_map;

public:
    epoll_manager() {
        _fd = epoll_create(MAX_EPOLL_TABLE);
    }

    ~epoll_manager() {
        for (auto& [fd, ptr] : event_map) {
            if (ptr) {
                epoll_ctl(_fd, EPOLL_CTL_DEL, fd, nullptr);
                if (fd != -1)
                        close(fd);
                delete ptr;
                ptr = nullptr;
            }
        }
        if (_fd != -1) {
            close(_fd);
        }
    }

    int fd() { return _fd; }

    void add_event(async_event&& ev, bool oneshot = true)
    {
        int fd = ev.fd;
        int EVENT = 0;
        if (ev.in_handle)
            EVENT |= EPOLLIN;
        if (ev.out_handle)
            EVENT |= EPOLLOUT;
        std::unique_lock<std::mutex> lock(mtx);
        if (auto it = event_map.find(fd); it != event_map.end()) {
            *(it->second) = std::move(ev);
            auto p = it->second;
            lock.unlock();
            mod_event(p, EVENT, oneshot);
            return;
        }
        lock.unlock();
        epoll_event event;
        event.data.ptr = static_cast<void *>(new async_event{ev});
        event.events = EVENT | EPOLLET;
        if (oneshot)
            event.events |= EPOLLONESHOT;
        lock.lock();
        epoll_ctl(_fd, EPOLL_CTL_ADD, fd, &event);
        setnonblocking(fd);
        event_map.insert({fd, static_cast<async_event *>(event.data.ptr)});
    }

    void mod_event(async_event *ev, int EVENT, bool oneshot = true)
    {
        epoll_event event;
        event.data.ptr = static_cast<void *>(ev);
        event.events = EVENT | EPOLLET;
        if (oneshot)
            event.events |= EPOLLONESHOT;
        epoll_ctl(_fd, EPOLL_CTL_MOD, ev->fd, &event);
    }

    void del_event(int fd) {
        std::unique_lock<std::mutex> lock(mtx);
        epoll_ctl(_fd, EPOLL_CTL_DEL, fd, nullptr);
        if (auto it = event_map.find(fd); it != event_map.end()) {
            if (fd != -1)
                close(fd);
            if (it->second) {
                delete it->second;
                it->second = nullptr;
            }
            event_map.erase(it);
        }
    }

    std::deque<std::coroutine_handle<>> run(std::chrono::steady_clock::time_point deadline)
    {
        auto now = std::chrono::steady_clock::now();
        if (deadline <= now) {
            return run(false);
        }
        for (; deadline > now; now = std::chrono::steady_clock::now())
        {
            auto remaining = duration_cast<std::chrono::milliseconds>(deadline - now).count();
            int waittime = (remaining > std::numeric_limits<int>::max() ? std::numeric_limits<int>::max() : remaining); // 限制在 int 范围内
            epoll_event events[MAX_EVENT_NUMBER];
            int nums = epoll_wait(_fd, events, MAX_EVENT_NUMBER, waittime);
            if (nums > 0)
            {
                std::deque<std::coroutine_handle<>> ready_deq{};
                for (int i{0}; i < nums; ++i)
                {
                    auto ptr = static_cast<async_event *>(events[i].data.ptr);
                    auto event = events[i].events;
                    if (event & EPOLLIN)
                    {
                        ready_deq.push_back(ptr->in_handle);
                    }
                    if (event & EPOLLOUT)
                    {
                        ready_deq.push_back(ptr->out_handle);
                    }
                }
                return std::move(ready_deq);
            }
            if (errno == EINTR)
                continue;
        }
        return {};
    }

    std::deque<std::coroutine_handle<>> run(bool is_wait = true) {
        while (is_wait) {
            epoll_event events[MAX_EVENT_NUMBER];
            int nums = epoll_wait(_fd, events, MAX_EVENT_NUMBER, (is_wait ? -1 : 0));
            if (nums >= 0) {
                std::deque<std::coroutine_handle<>> ready_deq{};
                for (int i{0}; i < nums; ++i) {
                    auto ptr = static_cast<async_event *>(events[i].data.ptr);
                    auto event = events[i].events;
                    if (event & EPOLLIN) {
                        ready_deq.push_back(ptr->in_handle);
                    }
                    if (event & EPOLLOUT) {
                        ready_deq.push_back(ptr->out_handle);
                    }
                }
                return std::move(ready_deq);
            } else {
                if (errno == EINTR)
                    continue;
                else
                    break;
            }
        }
        return {};
    }
};


#endif
