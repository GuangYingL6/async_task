cmake_minimum_required(VERSION 3.16)
project(async_lib VERSION 1.0.0 LANGUAGES CXX)

# 创建一个 INTERFACE 库（仅头文件，无需编译）
add_library(async_lib INTERFACE)

# 设置头文件所在目录，并传播给使用者
target_include_directories(async_lib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# C++20 标准
target_compile_features(async_lib INTERFACE cxx_std_20)
