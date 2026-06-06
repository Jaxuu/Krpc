# KRpc

> 一个基于 C++11 的轻量级高性能 RPC 框架，集成 Protobuf 序列化、muduo 网络库和 ZooKeeper 服务注册发现。

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-11-blue.svg)](https://en.cppreference.com/w/cpp/11)
[![Build System](https://img.shields.io/badge/build-CMake-brightgreen.svg)](https://cmake.org/)

---

## 核心功能特性

- **RPC 远程调用** — 基于 Protobuf Service 定义，客户端像调用本地方法一样调用远程服务，透明处理网络通信与序列化
- **ZooKeeper 服务注册与发现** — 服务端启动时自动注册服务到 ZooKeeper，客户端调用前从 ZooKeeper 查询服务地址，支持动态上下线
- **自定义高效二进制协议** — 在 TCP 之上设计了紧凑的私有协议（长度头 + Protobuf Header + Body），解决 TCP 粘包/拆包问题
- **高性能多线程并发** — 基于 muduo 网络库的 Reactor 模型和线程池，客户端支持多线程并发压测，具备较高的 QPS 吞吐
- **RAII 日志封装** — 基于 glog 提供开箱即用的日志系统，支持彩色终端输出和分级日志（INFO / WARNING / ERROR / FATAL）

## 技术栈

| 类别 | 技术 |
|------|------|
| 编程语言 | C++11 |
| 序列化 | [Protocol Buffers](https://developers.google.com/protocol-buffers) (protobuf) |
| 网络库 | [muduo](https://github.com/chenshuo/muduo) (基于 Reactor 模式的多线程网络库) |
| 服务注册发现 | [Apache ZooKeeper](https://zookeeper.apache.org/) (C 客户端) |
| 日志 | [Google glog](https://github.com/google/glog) |
| 线程库 | pthread |
| 构建系统 | CMake (≥ 3.0) |
| 许可证 | GPL v3 |

## 项目目录结构

```
KRpc-Cpp/
├── CMakeLists.txt                  # 根 CMake 构建脚本
├── LICENSE                         # GPL v3 许可证
├── README.md                       # 项目说明（本文件）
│
├── src/                            # 核心框架源码
│   ├── CMakeLists.txt              # 构建 krpc_core 静态库
│   ├── Krpcapplication.cc          # 框架初始化、命令行解析、单例管理
│   ├── Krpcchannel.cc              # RPC 客户端通道：连接、序列化、发送、接收
│   ├── Krpcconfig.cc               # 配置文件解析（key=value 格式）
│   ├── Krpccontroller.cc           # RPC 控制器：调用状态跟踪与错误信息
│   ├── Krpcprovider.cc             # RPC 服务提供者：注册服务、监听请求、分发调用
│   ├── zookeeperutil.cc            # ZooKeeper 客户端封装（连接、创建节点、获取数据）
│   ├── Krpcheader.proto            # RPC 协议头定义
│   └── include/                    # 头文件目录
│       ├── Krpcapplication.h
│       ├── Krpcchannel.h
│       ├── Krpcconfig.h
│       ├── Krpccontroller.h
│       ├── Krpcheader.pb.h
│       ├── KrpcLogger.h            # 日志封装（RAII 模式）
│       ├── Krpcprovider.h
│       └── zookeeperutil.h
│
├── example/                        # 示例应用
│   ├── CMakeLists.txt
│   ├── user.proto                  # UserService Protobuf 定义
│   ├── callee/                     # 服务端（被调用方）
│   │   ├── CMakeLists.txt
│   │   └── Kserver.cc              # 服务实现与启动
│   └── caller/                     # 客户端（调用方）
│       ├── CMakeLists.txt
│       └── Kclient.cc              # 多线程并发调用与 QPS 压测
│
├── bin/                            # 编译输出目录
│   └── test.conf                   # 示例配置文件
│
└── scripts/                        # 工具脚本
    └── collect_krpc_source.py      # 源码收集/归档脚本
```

## 快速开始

### 环境要求

- **操作系统**：Linux（依赖 POSIX 套接字 API 和 muduo 网络库）
- **编译器**：支持 C++11 的 GCC 或 Clang
- **CMake**：≥ 3.0

### 安装依赖

在 Debian/Ubuntu 系统上：

```bash
# Protobuf
sudo apt install libprotobuf-dev protobuf-compiler

# muduo 网络库（需从源码编译或使用包管理器）
# 见: https://github.com/chenshuo/muduo

# ZooKeeper C 客户端
sudo apt install libzookeeper-mt-dev

# glog
sudo apt install libgoogle-glog-dev
```

### 编译项目

```bash
# 进入项目根目录
cd KRpc-Cpp

# 创建构建目录
mkdir build && cd build

# 配置并编译
cmake ..
make -j$(nproc)

# 编译产物
# - lib/libkrpc_core.a    (核心框架静态库)
# - bin/server            (示例服务端)
# - bin/client            (示例客户端)
```

### 准备 protobuf 生成文件

```bash
# 为 src 目录生成协议头文件
protoc --cpp_out=src src/Krpcheader.proto

# 为示例生成服务代码
protoc --cpp_out=example example/user.proto
```

### 启动 ZooKeeper

```bash
# 确保 ZooKeeper 服务运行在 127.0.0.1:2181（或修改 test.conf 配置）
zkServer.sh start
```

### 运行示例

**1. 启动服务端（callee）：**

```bash
./bin/server -i ../bin/test.conf
```

**2. 启动客户端（caller）：**

```bash
./bin/client -i ../bin/test.conf
```

客户端会启动 100 个线程，每个线程发送 5000 次 RPC 请求，最终输出 QPS 统计结果。

## 核心模块/接口说明

### 1. `KrpcApplication` — 框架入口

单例模式的全局应用类，负责框架初始化：

```cpp
// 初始化框架，解析命令行参数 -i <配置文件>
KrpcApplication::Init(argc, argv);

// 获取配置对象
Krpcconfig& config = KrpcApplication::GetInstance().GetConfig();
```

### 2. `KrpcProvider` — 服务提供者

管理服务注册和 RPC 请求处理：

```cpp
KrpcProvider provider;
provider.NotifyService(new UserService());  // 注册服务对象
provider.Run();                             // 启动事件循环（阻塞）
```

- `NotifyService(Service*)` — 注册一个 Protobuf Service 实现
- `Run()` — 启动 muduo TCP 服务器，注册服务到 ZooKeeper，进入事件循环

### 3. `KrpcChannel` — RPC 通道（客户端）

实现 `google::protobuf::RpcChannel` 接口，供 `Stub` 调用：

```cpp
// 创建 Stub，传入 KrpcChannel
Kuser::UserServiceRpc_Stub stub(new KrpcChannel(false));

// 发起 RPC 调用
stub.Login(&controller, &request, &response, nullptr);
```

工作流程：
1. 首次调用时从 ZooKeeper 查询服务地址
2. 建立 TCP 连接
3. 序列化请求参数
4. 构造协议头并发送
5. 接收响应并反序列化

### 4. `Krpccontroller` — 调用控制器

跟踪和管理 RPC 调用状态：

```cpp
Krpccontroller controller;
stub.Login(&controller, &request, &response, nullptr);

if (controller.Failed()) {
    std::cout << controller.ErrorText() << std::endl;
}
```

### 5. `Krpcconfig` — 配置管理

解析 key=value 格式的配置文件：

```ini
# test.conf 示例
rpcserverip=127.0.0.1
rpcserverport=8001
zookeeperip=127.0.0.1
zookeeperport=2181
```

### 6. `KrpcLogger` — 日志模块

RAII 模式的 glog 封装，支持彩色日志和分级输出：

```cpp
KrpcLogger logger("MyRPC");
LOG(INFO) << "Service started";
LOG(ERROR) << "Connection failed";
```

### 7. 自定义通信协议

```
请求格式:
┌──────────────┬──────────────┬────────────────┬──────────────┐
│ Total Length │ Header Length│ RpcHeader      │ Args (Body)  │
│   (4 bytes)  │   (4 bytes)  │ (PB 序列化)    │ (PB 序列化)  │
└──────────────┴──────────────┴────────────────┴──────────────┘

响应格式:
┌──────────────┬──────────────────┐
│ Total Length │ Response (Body)  │
│   (4 bytes)  │   (PB 序列化)    │
└──────────────┴──────────────────┘
```

`RpcHeader` 包含 `service_name`、`method_name` 和 `args_size` 三个字段。

### 8. ZooKeeper 服务注册

```
ZooKeeper 节点结构:
/<service_name>              (永久节点，如 /UserServiceRpc)
/<service_name>/<method>     (临时节点，如 /UserServiceRpc/Login)
    └── 节点数据: "ip:port"  (如 "192.168.1.10:8001")
```

服务端启动时将方法节点注册为临时节点（`ZOO_EPHEMERAL`），断开连接后自动清除，天然支持服务健康检测。

## 注意事项

1. **平台限制**：框架使用了 Linux 特有的 socket API（`send`、`recv`、`inet_addr` 等）和 muduo 网络库，**仅支持 Linux 环境**，无法在 Windows 上直接编译运行。

2. **依赖顺序**：启动前需确保 ZooKeeper 服务已运行，否则服务端无法注册服务、客户端无法发现服务地址。

3. **连接复用**：`KrpcChannel` 在首次 `CallMethod` 时建立 TCP 连接并缓存 socket fd，后续调用复用同一连接。若连接断开需要重建 `KrpcChannel` 对象。

4. **长连接模式**：当前实现为长连接，服务端使用 muduo 的事件循环处理所有请求，未实现连接池或负载均衡策略，生产环境需进一步扩展。

5. **protobuf 生成文件**：`src/Krpcheader.pb.cc` 和 `example/user.pb.cc` 需在编译前由 `protoc` 工具从 `.proto` 文件生成。仓库中已包含预生成文件，但若修改 `.proto` 文件后需重新生成。

6. **线程安全**：`KrpcApplication` 单例使用 `std::mutex` 保证线程安全；ZooKeeper 数据查询有独立的全局互斥锁保护。

7. **CMake 构建**：`CMakeLists.txt` 中通过 `find_package(Protobuf REQUIRED)` 查找 protobuf，需要确保 protobuf 已正确安装并可被 CMake 发现。
