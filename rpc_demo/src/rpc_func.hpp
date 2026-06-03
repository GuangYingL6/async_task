#ifndef __RPC_FUNC_HPP__
#define __RPC_FUNC_HPP__
#include "./rpc.hpp"

int echo(std::string str, int n)
{
    for (int i{n}; i > 0; --i)
        std::cout << str << std::endl;
    return n;
}
DEF(echo)

#endif