cmake_minimum_required(VERSION 3.16)
project(AsyncRPC CXX)

# 使用 C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(OUTPUT ${PROJECT_SOURCE_DIR}/output)
set(EXECUTABLE_OUTPUT_PATH ${OUTPUT})

# 查找 Protobuf 库
find_package(Protobuf REQUIRED)

# -------------------------------------------------------
# 1. 如果有 .proto 文件，让 CMake 自动编译生成 .pb.cc / .pb.h
# -------------------------------------------------------
set(PROTO_FILES
    src/msg/args.proto          # 列出 proto 文件（可多个）
)

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})

# -------------------------------------------------------
# 2. 收集所有源文件（包含生成的 protobuf 文件）
# -------------------------------------------------------
file(GLOB_RECURSE ALL_SRCS
    src/*.cpp
    src/msg/*.cpp
)

# -------------------------------------------------------
# 3. 可执行文件
# -------------------------------------------------------
add_executable(app ${ALL_SRCS} ${PROTO_SRCS})

# 让编译器能找到生成的 protobuf 头文件
target_include_directories(app PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(app PRIVATE ${PROTOBUF_INCLUDE_DIRS}
                                       ${PROJECT_SOURCE_DIR}/src)

add_subdirectory(../async_ async_lib)
target_link_libraries(app PRIVATE async_lib)

# 链接 Protobuf 库
target_link_libraries(app PRIVATE ${PROTOBUF_LIBRARIES})

# 如果需要协程（GCC 10+ 默认已支持，但显式打开也没坏处）
target_compile_options(app PRIVATE -fcoroutines)