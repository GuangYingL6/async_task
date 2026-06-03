#include <iostream>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "./msg/arg.hpp"

int echo(std::string str, int n);

template <typename Func, typename... Args>
auto _rpc_call(const std::string& func, Args... args) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        throw "err rec_call(): socket";
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8081);                     // 服务器端口
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr); // 服务器地址

    if (connect(sock, (sockaddr *)&addr, sizeof(addr)) < 0) {
        throw "err rec_call(): socket";
    }

    std::string msg;
    Request req = buildRequest(func, args...);
    req.SerializeToString(&msg);
    uint32_t size = msg.size();
    send(sock, &size, sizeof(size), 0);
    send(sock, msg.c_str(), msg.size(), 0); // 发送

    size = 0;
    recv(sock, (char *)(&size), sizeof(size), 0);
    std::string buf(1024, '\0');
    for (int n{0}; n < size; ) {
        int ret = recv(sock, buf.data(), buf.size(), 0);
        std::cout << "rn: " << ret << std::endl;
        if (ret > 0) {
            n += ret;
        }
    }
    Request rreq;
    rreq.ParseFromString(buf);
    using Ret = std::invoke_result_t<Func, Args...>;
    auto ans = std::get<0>(unpack<Ret>(rreq.argslist(0)));
    std::cout << "req.url: " << rreq.url() << " req.ans: " << ans << std::endl;

    close(sock);
    return ans;
}

int main()
{
    _rpc_call<decltype(echo)>("echo", std::string("echo"), 2);
    return 0;
}

#include "./msg/args.pb.cc"