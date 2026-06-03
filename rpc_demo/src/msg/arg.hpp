#ifndef _ARG_HPP__
#define _ARG_HPP__
#include "args.pb.h"
#include <google/protobuf/wrappers.pb.h>
#include <google/protobuf/any.pb.h>

// 辅助：将任意 Protobuf 消息包装成 Argument
template <typename T>
Argument PackArgument(const T &msg) {
    Argument arg;
    arg.mutable_value()->PackFrom(msg);
    return arg;
}

template <typename type>
void build_arg(ArgumentList& list, type&& it) {
    if constexpr (std::is_same_v<type, int>) {
        google::protobuf::Int32Value int_val;
        int_val.set_value(it);
        *list.add_args() = PackArgument(int_val);
    } else if constexpr (std::is_same_v<type, std::string>) {
        google::protobuf::StringValue str_val;
        str_val.set_value(it);
        *list.add_args() = PackArgument(str_val);
    }
    else if constexpr (std::is_same_v<type, double>) {
        google::protobuf::DoubleValue dou_val;
        dou_val.set_value(it);
        *list.add_args() = PackArgument(dou_val);
    }
    else if constexpr (std::is_same_v<type, bool>) {
        google::protobuf::BoolValue bool_val;
        bool_val.set_value(it);
        *list.add_args() = PackArgument(bool_val);
    }
    else {
        throw "args type build not find";
    }
}

template <typename... Args>
ArgumentList BuildArgs(Args&&... args) {
    ArgumentList list;
    (build_arg(list, std::forward<Args>(args)), ...);
    return list;
}

template <typename... Args>
Request buildRequest(const std::string& url, Args... args) {
    Request req;
    req.set_url(url);
    *req.add_argslist() = std::move(BuildArgs<Args...>(std::forward<Args>(args)...));
    return std::move(req);
}

// 序列化为字符串（字节流）
std::string SerializeArgs(const ArgumentList &list) {
    std::string out;
    list.SerializeToString(&out);
    return out;
}

template <typename Ret>
Ret any_to_type(const google::protobuf::Any &any)
{
    if constexpr (std::is_same_v<Ret, int>)
    {
        if (!any.Is<google::protobuf::Int32Value>())
            throw "any type != int";
        google::protobuf::Int32Value val;
        any.UnpackTo(&val);
        return std::move(val.value());
    }
    else if constexpr (std::is_same_v<Ret, std::string>)
    {
        if (!any.Is<google::protobuf::StringValue>())
            throw "any type != string";
        google::protobuf::StringValue val;
        any.UnpackTo(&val);
        return std::move(val.value());
    }
    else if constexpr (std::is_same_v<Ret, double>) {
        if (!any.Is<google::protobuf::DoubleValue>())
            throw "any type != double";
        google::protobuf::DoubleValue val;
        any.UnpackTo(&val);
        return std::move(val.value());
    }
    else if constexpr (std::is_same_v<Ret, bool>) {
        if (!any.Is<google::protobuf::BoolValue>())
            throw "any type != bool";
        google::protobuf::BoolValue val;
        any.UnpackTo(&val);
        return std::move(val.value());
    }
    else {
        throw "any type is null";
        return {};
    }
}

template <typename Tuple, size_t... Idx>
Tuple _unpack(const ArgumentList &args, std::index_sequence<Idx...>) {
    return std::make_tuple((any_to_type<typename std::tuple_element<Idx, Tuple>::type>(args.args()[Idx].value()))...);
}

template <typename... ArgsType>
std::tuple<ArgsType...> unpack(const ArgumentList& args) {
    using Tuple = std::tuple<ArgsType...>;
    constexpr size_t N = std::tuple_size_v<Tuple>;
    return std::move(_unpack<Tuple>(args, std::make_index_sequence<N>{}));
}
#endif