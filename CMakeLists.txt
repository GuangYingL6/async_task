cmake_minimum_required(VERSION 3.16)
project(AsyncRPC_Parent)           # 父项目名称，可任意

# 将子目录加入构建，子目录中的 CMakeLists.txt 会被正常处理
add_subdirectory(./rpc_demo async_rpc_project)