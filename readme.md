# async_task

基于 C++20 协程的轻量级异步框架，提供 epoll 事件驱动、work-stealing 调度、协程互斥锁等基础设施，并附带一个基于 protobuf 的简易 RPC 服务示例。

## 特性

- C++20 Coroutine — 基于 coroutine_handle 的对称转移（symmetric transfer），lazy 启动
- epoll 事件驱动 — 边缘触发（ET）+ ONESHOT，支持 I/O 就绪回调自动恢复协程
- Work-Stealing 调度 — 多线程事件循环，空闲线程从其他线程的任务队列窃取任务
- 定时器队列 — 基于 priority_queue 的 deadline 调度，到期任务自动转入就绪队列
- 协程互斥锁 — async_mutex / async_lock / async_conditional_variable，挂起而非阻塞线程
- 简易 RPC — 基于 protobuf 的函数注册与远程调用，DEF() 宏一行注册

## 项目结构
```
async_task/
├── async_/                      # 异步框架（header-only）
│   ├── CMakeLists.txt           # INTERFACE library
│   ├── async_.hpp               # 聚合头文件
│   ├── task.hpp                 # 协程 Task 类型 + promise_type
│   ├── async_epoll.hpp          # epoll 封装
│   ├── async_mutex.hpp          # 协程互斥锁 + 条件变量
│   └── loop.hpp                 # 事件循环 + work-stealing + 定时器
│
└── rpc_demo/                    # RPC 示例
    ├── CMakeLists.txt
    └── src/
        ├── server.cpp           # 服务端：accept 循环 + 连接管理
        ├── rpc.hpp              # RPC 注册表 + 调用分发
        ├── rpc_func.hpp         # 示例函数（DEF 宏注册）
        └── msg/
            ├── args.proto       # protobuf 消息定义
            ├── arg.hpp          # protobuf 辅助工具
            └── args.pb.h        # 生成的 protobuf 头文件
```
## 构建

### 依赖

- C++20 编译器（GCC 10+ / Clang 14+）
- CMake >= 3.16
- protobuf（libprotobuf-dev）

### 编译
```
cd rpc_demo
mkdir build && cd build
cmake ..
make -j$(nproc)
```
生成的可执行文件位于 rpc_demo/output/app。

### 使用

#### 启动服务端
```
./output/app
```
默认监听 127.0.0.1:8081。

#### 注册 RPC 函数

在 rpc_func.hpp 中使用 DEF 宏注册：
```
int add(int a, int b) {
    return a + b;
}
DEF(add)
```
### 框架 API

#### 协程任务
```
#include "async_/async_.hpp"

task<int> compute() {
    co_return 42;
}

task<void> hello() {
    int result = co_await compute();
}
```
#### I/O 操作
```
co_await async_read(fd, buffer, size);
co_await async_write(fd, buffer, size);
```
#### 协程互斥锁
```
async_mutex mtx;

task<void> critical_section() {
    async_lock lock(mtx);
    co_await lock.lock();
    // 临界区...
    lock.unlock();  // 析构时也会自动释放
}
```
#### 定时任务
```
loop::make().lock()->add_task(handle, std::chrono::milliseconds(500));
```
#### 事件循环
```
auto l = loop::make().lock();
l->add_task(some_coroutine());
l->run_thread();  // 阻塞，运行所有工作线程
```
### 架构设计

#### 调度模型

采用单 epoll + 多 worker 混合模型：

1. 通过 CAS 原子操作（use_epoll）确保同一时刻只有一个线程调用 epoll_wait
2. 其他线程从本线程或相邻线程的就绪队列中窃取任务执行
3. epoll 返回的就绪 handle 被分发到当前线程的就绪队列

#### 协程生命周期
```
创建 → 挂起（lazy start）→ 加入就绪队列 → 调度器 resume
  → 执行到 co_await I/O → 注册 epoll → 挂起
  → I/O 就绪 → 恢复协程 → 继续执行
  → co_return → final_suspend → 恢复调用者（或 noop_coroutine）
```
#### async_mutex 工作原理
```
lock() → try_lock 成功 → 继续执行
       → try_lock 失败 → 协程挂起，加入等待队列
       → unlock() → 从队列取出下一个协程 → add_task 唤醒
```
与 std::mutex 的区别：协程挂起时不阻塞线程，线程可以继续执行其他任务。

### 已知限制

- 客户端为同步阻塞实现，未接入异步框架
- 无优雅退出机制，timer_loop 持续运行直到进程终止
- 无单元测试

License

MIT