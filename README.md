# KRpc

> 基于 C++11 的轻量级高性能 RPC 框架 — 集成 Protobuf 序列化、muduo 网络库、ZooKeeper 服务发现，QPS 突破 2.1 万。

[![C++ Standard](https://img.shields.io/badge/C%2B%2B-11-blue)](https://en.cppreference.com/w/cpp/11)
[![Build](https://img.shields.io/badge/build-CMake-green)](https://cmake.org/)
[![License](https://img.shields.io/badge/License-GPL%20v3-blue.svg)](LICENSE)

---

## 核心功能特性

- **RPC 远程调用** — 基于 Protobuf Service，客户端像调用本地方法一样调用远程服务，透明处理网络通信与序列化/反序列化
- **ZooKeeper 服务注册与发现** — 服务端启动时自动注册到 ZooKeeper（临时节点），客户端调用前从 ZooKeeper 动态查询服务地址，天然支持服务健康检测与无感容灾
- **自定义高效二进制协议** — TCP 之上设计紧凑私有协议（长度头 + Header + Body），配合循环读取与长度校验解决粘包/拆包问题
- **高并发与自适应零拷贝** — 基于 muduo Reactor 多线程模型 + 连接复用，C++17 读写分离架构优化，100 线程 × 5000 请求压测 QPS 突破 2.1 万
- **二维一致性哈希负载均衡** — 微服务网关层实现二维一致性哈希，支持全量微服务的分布式负载均衡与动态路由
- **RAII 日志系统** — 基于 glog 封装，开箱即用的彩色分级日志（INFO / WARNING / ERROR / FATAL）

## 技术栈

| 类别 | 方案 |
|------|------|
| 语言标准 | C++11（部分 C++17 特性） |
| 序列化 | [Protocol Buffers](https://protobuf.dev/) |
| 网络库 | [muduo](https://github.com/chenshuo/muduo) — Reactor 多线程网络库 |
| 服务发现 | [Apache ZooKeeper](https://zookeeper.apache.org/) C 客户端 (`zookeeper_mt`) |
| 日志 | [Google glog](https://github.com/google/glog) |
| 线程 | POSIX Threads (`pthread`) |
| 构建 | CMake ≥ 3.0 |
| 协议 | GPL v3 |

## 项目目录结构

```
KRpc-Cpp/
├── CMakeLists.txt                       # 根 CMake（项目配置、依赖查找、子目录）
├── LICENSE                              # GPL v3
├── README.md
│
├── src/                                 # === 核心框架源码 ===
│   ├── CMakeLists.txt                   # 构建静态库 libkrpc_core.a
│   ├── Krpcheader.proto                 # RPC 协议头 Protobuf 定义
│   ├── Krpcapplication.cc               # 框架入口：命令行解析、单例、配置加载
│   ├── Krpcchannel.cc                   # 客户端通道：TCP 连接、序列化、收发
│   ├── Krpcconfig.cc                    # 配置文件解析（key=value 格式）
│   ├── Krpccontroller.cc                # 调用控制器：状态跟踪与错误信息
│   ├── Krpcprovider.cc                  # 服务提供者：注册、监听、请求分发
│   ├── zookeeperutil.cc                 # ZooKeeper 客户端封装
│   └── include/
│       ├── Krpcapplication.h
│       ├── Krpcchannel.h
│       ├── Krpcconfig.h
│       ├── Krpccontroller.h
│       ├── Krpcprovider.h
│       ├── KrpcLogger.h                 # RAII 日志封装
│       └── zookeeperutil.h
│
├── example/                             # === 示例应用 ===
│   ├── CMakeLists.txt
│   ├── user.proto                       # UserService 定义（Login / Register）
│   ├── callee/
│   │   ├── CMakeLists.txt               # 构建 server 可执行文件
│   │   └── Kserver.cc                   # 服务端：实现服务 + 启动 RPC 节点
│   └── caller/
│       ├── CMakeLists.txt               # 构建 client 可执行文件
│       └── Kclient.cc                   # 客户端：多线程压测 + QPS 统计
│
├── bin/                                 # 编译产物输出目录
│   └── test.conf                        # 示例配置文件
│
└── scripts/
    └── collect_krpc_source.py           # 源码归档工具
```

## 快速开始

### 前置依赖

- **操作系统**: Linux（依赖 POSIX socket API 和 muduo）
- **编译器**: GCC/Clang 支持 C++11
- **CMake**: ≥ 3.0

### 安装系统依赖（Debian/Ubuntu）

```bash
# Protocol Buffers
sudo apt install libprotobuf-dev protobuf-compiler

# ZooKeeper C 客户端（多线程版本）
sudo apt install libzookeeper-mt-dev

# glog
sudo apt install libgoogle-glog-dev

# muduo 网络库 — 需手动编译安装
# git clone https://github.com/chenshuo/muduo.git
# cd muduo && ./build.sh && sudo ./build.sh install
```

### 编译

```bash
cd KRpc-Cpp

# 生成 Protobuf 代码（若 .pb.cc/.pb.h 不存在）
protoc --cpp_out=src src/Krpcheader.proto
protoc --cpp_out=example example/user.proto

# CMake 构建
mkdir build && cd build
cmake ..
make -j$(nproc)
```

编译产物:
- `lib/libkrpc_core.a` — 核心框架静态库
- `bin/server` — 示例服务端
- `bin/client` — 示例客户端

### 运行

1. **启动 ZooKeeper**

```bash
sudo systemctl start zookeeper
```

2. **编辑配置文件** `conf/test.conf`：

```ini
rpcserverip=127.0.0.1
rpcserverport=8000
zookeeperip=127.0.0.1
zookeeperport=2181
```

3. **启动服务端**：(可以通过编辑多个配置文件启动多个服务端)

```bash
./bin/server -i ../conf/test.conf
```

4. **启动客户端**（并发压测）：

```bash
./bin/client -i ../conf/test.conf
```

客户端运行 100 线程 × 5000 请求，终端输出：
```
Total requests: 500000
Success count: 500000
Fail count: 0
Elapsed time: 23.5 seconds
QPS: 21276.6
```

## 核心模块说明

### ① `KrpcApplication` — 框架入口（单例）

```cpp
// 解析命令行参数 -i <配置文件>，加载配置
KrpcApplication::Init(argc, argv);

// 获取全局配置
Krpcconfig& cfg = KrpcApplication::GetInstance().GetConfig();
std::string ip = cfg.Load("rpcserverip");
```

### ② `KrpcProvider` — 服务端

```cpp
KrpcProvider provider;
provider.NotifyService(new UserService());  // 注册 Protobuf Service 实现
provider.Run();                             // 启动事件循环（阻塞等待请求）

// Run() 内部流程:
// 1. 创建 muduo::TcpServer，绑定 OnConnection / OnMessage 回调
// 2. 遍历已注册的 service 和 method，写入 ZooKeeper 临时节点
// 3. server->start() + event_loop.loop()
```

### ③ `KrpcChannel` — 客户端通道

继承 `google::protobuf::RpcChannel`，供生成的 `Stub` 类直接使用：

```cpp
Kuser::UserServiceRpc_Stub stub(new KrpcChannel(false));

Krpccontroller controller;
stub.Login(&controller, &request, &response, nullptr);

if (controller.Failed()) {
    std::cerr << controller.ErrorText() << std::endl;
}
```

`CallMethod` 执行流程：
1. 首次调用 → ZooKeeper 查询 `/<service>/<method>` 获取 ip:port
2. TCP `connect()` 建立连接（最多重试 3 次）
3. 序列化 request → args_str
4. 构造协议头 `RpcHeader { service_name, method_name, args_size }` → 序列化
5. 发送 `[TotalLen(4B)][HeaderLen(4B)][Header][Body]`
6. 接收 `[TotalLen(4B)][ResponseBody]` → 反序列化 response

### ④ `Krpccontroller` — 调用控制器

```cpp
Krpccontroller ctrl;
// 调用过程中框架可通过 ctrl.SetFailed("reason") 标记失败
if (ctrl.Failed()) { /* 处理错误 */ }
ctrl.Reset();  // 重置状态
```

### ⑤ `Krpcconfig` — 配置管理

解析 `key=value` 格式配置文件，支持 `#` 注释和空行跳过：

```cpp
config.LoadConfigFile("test.conf");
std::string val = config.Load("rpcserverport");  // → "8001"
```

### ⑥ `KrpcLogger` — 日志模块（RAII）

```cpp
KrpcLogger logger("MyRPC");  // 构造时 InitGoogleLogging
LOG(INFO) << "Server started on port " << port;
LOG(ERROR) << "Connection failed: " << reason;
// 析构时自动 ShutdownGoogleLogging
```

### ⑦ 通信协议格式

```
请求帧:
┌─────────────┬─────────────┬──────────────────┬─────────────┐
│ Total Length │Header Length│ RpcHeader (PB)   │ Args (PB)   │
│   (4 bytes)  │  (4 bytes)  │ {service,method, │ {request}   │
│              │             │  args_size}      │             │
└─────────────┴─────────────┴──────────────────┴─────────────┘

响应帧:
┌─────────────┬───────────────────┐
│ Total Length │ Response (PB)    │
│   (4 bytes)  │ {response data}  │
└─────────────┴───────────────────┘

所有整数采用网络字节序（htonl / ntohl）
```

### ⑧ ZooKeeper 节点结构

```
ZooKeeper 路径树:
/<ServiceName>/             ← 永久节点（如 /UserServiceRpc）
/<ServiceName>/Login        ← 临时节点，数据 "192.168.1.10:8001"
/<ServiceName>/Register     ← 临时节点，数据 "192.168.1.10:8001"
```

临时节点在服务端断开后自动清除，天然支持服务下线检测。

## 注意事项

1. **仅支持 Linux** — 使用了 POSIX socket API（`send`/`recv`/`inet_addr`/`getopt`）和 muduo 网络库，无法在 Windows 上编译运行。
2. **ZooKeeper 前置** — 必须在运行 RPC 服务端/客户端前启动 ZooKeeper，否则服务注册和发现均会失败。
3. **长连接模式** — `KrpcChannel` 首次 `CallMethod` 建立 TCP 连接并缓存 fd，后续调用复用。连接断开后需重建 Channel 对象。
4. **Protobuf 生成文件** — 修改 `.proto` 后需重新运行 `protoc` 生成 `.pb.cc`/`.pb.h`。仓库中已包含预生成文件（如 `Krpcheader.pb.cc`）。
5. **线程安全** — `KrpcApplication` 单例使用 `std::mutex` 保护；ZooKeeper 查询有独立全局锁；`KrpcChannel::CallMethod` 本身非线程安全，建议每个线程使用独立的 Channel/Stub。
6. **配置文件位置** — 通过 `-i` 命令行参数指定，必须为程序第一个参数。
7. **muduo 版本** — 依赖 muduo 的 `net` 和 `base` 两个库，推荐使用陈硕官方版本编译安装。
