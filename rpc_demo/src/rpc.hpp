#ifndef __RPC_HPP__
#define __RPC_HPP__
#include <string>
#include <tuple>
#include <unordered_map>

#include "../../async_/async_.hpp"
#include "./msg/arg.hpp"

/*
template <typename = void>
struct func_args_list;

template <typename Ret, typename... Args>
struct func_args_list<Ret(Args...)> {
    using ret = Ret;
    static auto get(const ArgumentList &args) {
        return unpack<Args...>(args);
    }
};
*/


class call_bast {
public:
    call_bast() {}
    virtual ~call_bast() {}
    virtual task<void> call(int fd, const ArgumentList &) = 0;
};

std::unordered_map<std::string, call_bast *> mp;

template <typename T>
task<void> send_return(int fd, T&& ret) {
    Request req = buildRequest("echo", std::forward<T>(ret));
    std::string ans;
    req.SerializeToString(&ans);
    uint32_t size = ans.size();
    co_await async_write(fd, (char*)(&size), sizeof(size));
    co_await async_write(fd, ans.c_str(), ans.size());
}

template <typename Ret, typename... Args>
task<void> _call(int fd, Ret fun(Args...), const ArgumentList &args)
{
    if constexpr (std::is_void_v<Ret>) {
        std::apply(fun, unpack<Args...>(args));
    } else {
        auto ret = std::apply(fun, unpack<Args...>(args));
        co_await send_return(fd, ret);
    }
    co_return;
}

#define DEF(name)                                                          \
    class rpc_##name : public call_bast                                    \
    {                                                                      \
        rpc_##name() { mp[#name] = this; }                                 \
        static rpc_##name const *ptr;                                      \
                                                                           \
    public:                                                                \
        static rpc_##name const *make() { return ptr; }                    \
        virtual ~rpc_##name() {}                                           \
        virtual task<void> call(int fd, const ArgumentList &args) override \
        {                                                                  \
            co_await _call(fd, name, args);                                \
        }                                                                  \
    };                                                                     \
    rpc_##name const *rpc_##name::ptr = new rpc_##name();


task<void> ProcessArgs(int fd, const std::string &data)
{
    Request req;
    if (!req.ParseFromString(data)) {
        throw "err[main.cpp 39]";
        co_return;
    }
    if (auto it = mp.find(req.url()); it != mp.end()) {
        co_await it->second->call(fd, req.argslist(0));
    }
}

task<void> rpc_task(int fd, std::string serialized_data) {
    try {
        //Request req;
        //req.set_url("echo");
        //*req.add_argslist() = std::move(BuildArgs<std::string, int>("echo", 2));
        //Request req = buildRequest("echo", std::string("echo"), 2);
        //std::string serialized_data;
        /*
        if (!req.SerializeToString(&serialized_data)) {
            std::cout << "SerializeToString err" << std::endl;
            return -1;
        }
        */
        co_await ProcessArgs(fd, serialized_data);
    }
    catch (const char *e)
    {
        std::cout << e;
    }
    co_return;
}

#endif