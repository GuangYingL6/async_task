#include "../async_/async_.hpp"
#include "./rpc.hpp"
#include "./rpc_func.hpp"

async_mutex mtx{};

async_conditionan_variable cv{};

struct server
{
    std::unordered_map<int, task<void>> task_map;
    std::deque<task<void>> die_task;
    std::unique_ptr<task<void>> tsk;
    std::coroutine_handle<> operator()()
    {
        tsk = std::make_unique<task<void>>(std::move(_server()));
        return tsk.get()->handle;
    }
    task<void> _server()
    {
        async_listen listener;
        listener.bind_listen("127.0.0.1", 8081);
        std::cout << "server bind" << std::endl;

        while (true)
        {
            sockaddr_in addr{};
            std::cout << "await accept..." << std::endl;
            std::optional<int> client = co_await listener.async_accept(addr);
            async_lock lock(mtx);
            co_await lock.lock();
            while (!die_task.empty())
            {
                if (die_task.front().handle.done())
                {
                    die_task.pop_front();
                }
                else
                {
                    break;
                }
            }
            task_map.insert({client.value(), std::move([](int client, server *ptr) -> task<void>
                                                       {
                std::cout << "new echo start" << std::endl;
                uint32_t size = 0;
                co_await async_read(client, (char*)(&size), sizeof(size));
                std::string buff(1024, '\0');
                for (int n{0}; n < size; ) {
                    int ret = (co_await async_read(client, buff.data(), buff.size())).value();
                    std::cout << "rn: " << ret << std::endl;
                    if (ret > 0) {
                        n += ret;
                    }
                }

                co_await rpc_task(client, std::string(buff.c_str(), size));

                async_lock lock(mtx);
                co_await lock.lock();
                auto it = ptr->task_map.find(client);
                ptr->die_task.emplace_back(std::move(it->second));
                ptr->task_map.erase(client);
                lock.unlock();
                loop::make().lock()->epollfd.del_event(client); }(client.value(), this))});
            loop::make().lock()->add_task(task_map.find(client.value())->second.handle);
            lock.unlock();
        }
    }
};

int main()
{
    server s1;
    loop::make().lock()->add_task(s1()).run_thread();
}