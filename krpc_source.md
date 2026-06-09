# KRPC Project Source Collection

- Generated at: 2026-06-09 18:49:52
- Project root: `/home/ysh/projects/Krpc-main`
- File count: 22
- Excluded: `*.pb.cc`, `*.pb.h`, build/bin/.git/third_party/vendor/log directories

## File Tree

- `CMakeLists.txt`
- `example/CMakeLists.txt`
- `example/callee/CMakeLists.txt`
- `example/callee/Kserver.cc`
- `example/caller/CMakeLists.txt`
- `example/caller/Kclient.cc`
- `example/user.proto`
- `src/CMakeLists.txt`
- `src/Krpcapplication.cc`
- `src/Krpcchannel.cc`
- `src/Krpcconfig.cc`
- `src/Krpccontroller.cc`
- `src/Krpcheader.proto`
- `src/Krpcprovider.cc`
- `src/include/KrpcLogger.h`
- `src/include/Krpcapplication.h`
- `src/include/Krpcchannel.h`
- `src/include/Krpcconfig.h`
- `src/include/Krpccontroller.h`
- `src/include/Krpcprovider.h`
- `src/include/zookeeperutil.h`
- `src/zookeeperutil.cc`

---

## File: `CMakeLists.txt`

```cmake
#设置最低版本和项目名称
cmake_minimum_required(VERSION 3.0)
project(Krpc)

#设置全局的C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)#CMAKE_CXX_STANDARD_REQUIRED表示强制使用指定的标准

#设置头文件目录，供所有子项目使用
#include_directories(${CMAKE_SOURCE_DIR}/src/include) #其中CMAKE_SOURCE_DIR表示当前目录

#查找porotbuf包
find_package(Protobuf REQUIRED)
include_directories(${Protobuf_INCLUDE_DIRS})#Protobuf_INCLUDE_DIRS表示protobuf头文件目录

#设置全局链接库
set(LIBS
    protobuf
    pthread
    zookeeper_mt
    muduo_net
    muduo_base
    glog
)
#添加子目录
add_subdirectory(src)
add_subdirectory(example)
```

---

## File: `example/CMakeLists.txt`

```cmake
add_subdirectory(callee)
add_subdirectory(caller)
```

---

## File: `example/callee/CMakeLists.txt`

```cmake
#获取服务端的源文件
file(GLOB SERVER_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

#获取protobuf生成的.cc
file(GLOB PROTO_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/../*.pb.cc)

#创建服务端可执行文件
add_executable(server ${SERVER_SRCS} ${PROTO_SRCS})

#链接必要的库，尤其是example中生成的静态库
target_link_libraries(server krpc_core ${LIBS})

#设置编译选项
target_compile_options(server PRIVATE -std=c++11 -Wall)

# 设置 server 可执行文件输出目录
set_target_properties(server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)
```

---

## File: `example/callee/Kserver.cc`

```cpp
#include <iostream>
#include <string>
#include <unordered_map>
#include <shared_mutex> // C++17 读写锁
#include "../user.pb.h"
#include "Krpcapplication.h"
#include "Krpcprovider.h"

class UserService : public Kuser::UserServiceRpc 
{
private:
    std::unordered_map<std::string, std::string> m_user_db; 
    std::shared_mutex m_db_mutex; // 读写分离锁

public:
    // 1. 本地注册方法 (排他写)
    bool Register(std::string name, std::string pwd, std::string& err_msg) {
        std::unique_lock<std::shared_mutex> lock(m_db_mutex);
        if (m_user_db.find(name) != m_user_db.end()) {
            err_msg = "Register Failed: already exists!";
            return false;
        }
        m_user_db[name] = pwd;
        return true;
    }

    // 2. 本地登录方法 (无界并发读) —— 性能起飞的核心！
    bool Login(std::string name, std::string pwd, std::string& err_msg) {
        std::shared_lock<std::shared_mutex> lock(m_db_mutex);
        auto it = m_user_db.find(name);
        if (it == m_user_db.end()) {
            err_msg = "Login Failed: not found!";
            return false;
        }
        if (it->second != pwd) {
            err_msg = "Login Failed: wrong pwd!";
            return false;
        }
        return true;  
    }

    // 重写 Register RPC 方法
    void Register(::google::protobuf::RpcController* controller,
                  const ::Kuser::RegisterRequest* request,
                  ::Kuser::RegisterResponse* response,
                  ::google::protobuf::Closure* done) override {
        std::string err_msg;
        bool register_result = Register(request->name(), request->pwd(), err_msg); 

        Kuser::ResultCode *code = response->mutable_result();
        code->set_errcode(register_result ? 0 : 1);
        code->set_errmsg(err_msg);
        response->set_success(register_result);
        done->Run();
    }

    // 重写 Login RPC 方法
    void Login(::google::protobuf::RpcController* controller,
               const ::Kuser::LoginRequest* request,
               ::Kuser::LoginResponse* response,
               ::google::protobuf::Closure* done) override {
        std::string err_msg;
        bool login_result = Login(request->name(), request->pwd(), err_msg); 

        Kuser::ResultCode *code = response->mutable_result();
        code->set_errcode(login_result ? 0 : 1);
        code->set_errmsg(err_msg);
        response->set_success(login_result);
        done->Run();
    }
};

int main(int argc, char **argv) {
    KrpcApplication::Init(argc, argv);
    KrpcProvider provider;
    UserService my_service;
    provider.NotifyService(&my_service);
    provider.Run();
    return 0;
}
```

---

## File: `example/caller/CMakeLists.txt`

```cmake
#获取服务端的源文件
file(GLOB Client_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

#获取protobuf生成的.cc
file(GLOB PROTO_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/../*.pb.cc)

#创建服务端可执行文件
add_executable(client ${Client_SRCS} ${PROTO_SRCS})

#链接必要的库，尤其是example中生成的静态库
target_link_libraries(client krpc_core ${LIBS})

# 设置编译选项
target_compile_options(client PRIVATE -std=c++11 -Wall)

# 设置 client 可执行文件输出目录
set_target_properties(client PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)
```

---

## File: `example/caller/Kclient.cc`

```cpp
#include "Krpcapplication.h"
#include "../user.pb.h"
#include "Krpccontroller.h"
#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>
#include <vector>
#include <algorithm>
#include "KrpcLogger.h"

// 压测函数：交替测试 Login 和 Register
void send_request(Kuser::UserServiceRpc_Stub* stub, int requests_per_thread, std::vector<double>& local_latencies) {
    // 1. 构造 Login 请求
    Kuser::LoginRequest login_request;
    login_request.set_name("benchmark_user");  
    login_request.set_pwd("123456");    
    Kuser::LoginResponse login_response;

    // 2. 构造 Register 请求 
    // (如果你的 RegisterRequest 有不同的字段，请在这里按需修改)
    Kuser::RegisterRequest register_request;
    register_request.set_name("benchmark_new_user");
    register_request.set_pwd("654321");
    // register_request.set_id(1001); 
    Kuser::RegisterResponse register_response;

    for (int i = 0; i < requests_per_thread; ++i) {
        Krpccontroller controller;
        auto req_start = std::chrono::high_resolution_clock::now();
        
        // 核心测试点：单数发 Login，双数发 Register
        bool is_login = (i % 2 == 0);
        
        if (is_login) {
            stub->Login(&controller, &login_request, &login_response, nullptr);
        } else {
            stub->Register(&controller, &register_request, &register_response, nullptr);
        }

        // 失败拦截
        if (controller.Failed()) {
            // 打印前几次错误，方便定位是哪个方法出错了
            if (i < 2) { 
                std::string method_str = is_login ? "Login" : "Register";
                LOG(ERROR) << "RPC Call Failed! Method: " << method_str << ", Reason: " << controller.ErrorText();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue; 
        }

        auto req_end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> req_elapsed = req_end - req_start;
        local_latencies.push_back(req_elapsed.count()); 
    }
}

int main(int argc, char **argv) {
    KrpcApplication::Init(argc, argv);

    // 压测参数
    const int thread_count = 100;      
    const int requests_per_thread = 5000; 

    KrpcChannel* channel = new KrpcChannel(false);
    Kuser::UserServiceRpc_Stub stub(channel);

    std::vector<std::thread> threads;  
    std::vector<std::vector<double>> all_latencies(thread_count);
    for(int i=0; i<thread_count; ++i) {
        all_latencies[i].reserve(requests_per_thread);
    }

    LOG(INFO) << "Starting Benchmark (Testing Multiple Methods)...";
    auto start_time = std::chrono::high_resolution_clock::now();  

    for (int i = 0; i < thread_count; i++) {
        threads.emplace_back([&stub, requests_per_thread, &all_latencies, i]() {  
            send_request(&stub, requests_per_thread, all_latencies[i]);  
        });
    }

    for (auto &t : threads) { t.join(); }
    
    LOG(INFO) << "All threads finished! Calculating results...";
    
    auto end_time = std::chrono::high_resolution_clock::now();  
    std::chrono::duration<double> total_elapsed = end_time - start_time;  

    // 合并并计算 P99
    std::vector<double> merged_latencies;
    merged_latencies.reserve(thread_count * requests_per_thread);
    for (const auto& local_lat : all_latencies) {
        merged_latencies.insert(merged_latencies.end(), local_lat.begin(), local_lat.end());
    }
    std::sort(merged_latencies.begin(), merged_latencies.end());
    
    double p99_latency = merged_latencies[merged_latencies.size() * 0.99];
    double qps = (thread_count * requests_per_thread) / total_elapsed.count();

    LOG(INFO) << "========= Benchmark Results =========";
    LOG(INFO) << "Total Requests : " << thread_count * requests_per_thread;
    LOG(INFO) << "QPS            : " << qps << " req/sec";
    LOG(INFO) << "P99 Latency    : " << p99_latency << " ms";
    LOG(INFO) << "=====================================";

    return 0;
}
```

---

## File: `example/user.proto`

```protobuf
syntax="proto3";

package Kuser;

option cc_generic_services=true;

message ResultCode{
    int32 errcode=1;
    bytes errmsg=2;
}
message LoginRequest{
    bytes name=1;
    bytes pwd=2;
}
message LoginResponse{
    ResultCode result=1;
    bool success=2;
}
message RegisterRequest{
    uint32 id=1;
    bytes name=2;
    bytes pwd=3;
}
message RegisterResponse{
    ResultCode result=1;
    bool success=2;
}
service UserServiceRpc{
    rpc Login(LoginRequest) returns(LoginResponse);
    rpc Register(RegisterRequest) returns(RegisterResponse);
}
```

---

## File: `src/CMakeLists.txt`

```cmake
# 除了可以直接使用CMAKE_CURRENT_SOURCE_DIR的相对路径，也可以使用get_filename_component来获取当前目录的绝对路径
#这样就可以保证CMake 的某些操作可能需要使用绝对路径，尤其是跨目录的构建场景。绝对路径可以避免相对路径带来的混淆和潜在问题。
#动态调整目录结构：如果项目目录层次较深，或者使用子模块或外部依赖，绝对路径可以方便地定位正确的文件，而不用担心路径拼接或依赖 CMake 的当前工作目录。

#示例：
#get_filename_component(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR} ABSOLUTE)
#file(GLOB_RECURSE SRC_FILES ${SRC_DIR}/*.cc)
# 获取当前目录下的所有源文件
file(GLOB SRC_FILES ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

# 获取 protobuf 的生成文件
file(GLOB PROTO_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/*.pb.cc)

# 创建静态库或共享库
add_library(krpc_core STATIC ${SRC_FILES} ${PROTO_SRCS})

# 添加所有需要的库
target_link_libraries(krpc_core PUBLIC 
    protobuf
    muduo_net
    muduo_base
    pthread
    zookeeper_mt
    glog
)

# 设置头文件的路径
target_include_directories(krpc_core PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 添加编译选项
target_compile_options(krpc_core PRIVATE -std=c++11 -Wall)
```

---

## File: `src/Krpcapplication.cc`

```cpp
#include "Krpcapplication.h"
#include<cstdlib>
#include<unistd.h>

Krpcconfig KrpcApplication::m_config;  // 全局配置对象
std::mutex KrpcApplication::m_mutex;  // 用于线程安全的互斥锁
KrpcApplication* KrpcApplication::m_application = nullptr;  // 单例对象指针，初始为空

// 初始化函数，用于解析命令行参数并加载配置文件
void KrpcApplication::Init(int argc, char **argv) {
    if (argc < 2) {  // 如果命令行参数少于2个，说明没有指定配置文件
        std::cout << "格式: command -i <配置文件路径>" << std::endl;
        exit(EXIT_FAILURE);  // 退出程序
    }

    int o;
    std::string config_file;
    // 使用getopt解析命令行参数，-i表示指定配置文件
    while (-1 != (o = getopt(argc, argv, "i:"))) {
        switch (o) {
            case 'i':  // 如果参数是-i，后面的值就是配置文件的路径
                config_file = optarg;  // 将配置文件路径保存到config_file
                break;
            case '?':  // 如果出现未知参数（不是-i），提示正确格式并退出
                std::cout << "格式: command -i <配置文件路径>" << std::endl;
                exit(EXIT_FAILURE);
                break;
            case ':':  // 如果-i后面没有跟参数，提示正确格式并退出
                std::cout << "格式: command -i <配置文件路径>" << std::endl;
                exit(EXIT_FAILURE);
                break;
            default:
                break;
        }
    }

    // 加载配置文件
    m_config.LoadConfigFile(config_file.c_str());
}

// 获取单例对象的引用，保证全局只有一个实例
KrpcApplication &KrpcApplication::GetInstance() {
    static KrpcApplication instance; 
    return instance;
}

// 程序退出时自动调用的函数，用于销毁单例对象
void KrpcApplication::deleteInstance() {
    if (m_application) {  // 如果单例对象存在
        delete m_application;  // 销毁单例对象
    }
}

// 获取全局配置对象的引用
Krpcconfig& KrpcApplication::GetConfig() {
    return m_config;
}
```

---

## File: `src/Krpcchannel.cc`

```cpp
#include "Krpcchannel.h"
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <thread>
#include <sys/uio.h> // 提供 iovec 和 sendmsg 等高阶网络 I/O 接口
#include "zookeeperutil.h"
#include "Krpcheader.pb.h"
#include "Krpcapplication.h"
#include "Krpccontroller.h"
#include "KrpcLogger.h"


// 新增后台独立接收线程，解析响应并精准唤醒对应的业务线程
void KrpcChannel::ReadTask(int fd) {
    while (true) {
        uint32_t total_len = 0;
        if (recv_exact(fd, (char*)&total_len, 4) != 4) break; // 这里使用传入的 fd
        total_len = ntohl(total_len);

        uint32_t header_len = 0;
        if (recv_exact(fd, (char*)&header_len, 4) != 4) break;
        header_len = ntohl(header_len);

        std::vector<char> header_buf(header_len);
        if (recv_exact(fd, header_buf.data(), header_len) != header_len) break;
        std::string rpc_header_str(header_buf.data(), header_len);
        Krpc::RpcHeader krpcheader;
        krpcheader.ParseFromString(rpc_header_str);

        uint32_t body_len = total_len - 4 - header_len;
        std::vector<char> body_buf(body_len);
        if (recv_exact(fd, body_buf.data(), body_len) != body_len) break;
        std::string response_str(body_buf.data(), body_len);

        uint64_t req_id = krpcheader.request_id();

        // 同样通过 req_id 取模，找到这个请求对应的桶
        size_t bucket_index = req_id % BUCKET_NUM;
        auto& bucket = m_promise_buckets[bucket_index];

        {
            // 只锁住这个特定的桶，绝不会阻塞正在写入其他桶的线程！
            std::lock_guard<std::mutex> lock(bucket->mutex);
            auto it = bucket->pending_requests.find(req_id);
            if (it != bucket->pending_requests.end()) {
                it->second.set_value(response_str); 
                bucket->pending_requests.erase(it);       
            }
        }
    }
}

// 辅助函数：循环读取直到读够 size 字节
ssize_t KrpcChannel::recv_exact(int fd, char* buf, size_t size) {
    size_t total_read = 0;
    while (total_read < size) {
        ssize_t ret = recv(fd, buf + total_read, size - total_read, 0);
        if (ret == 0) return 0; // 对端关闭
        if (ret == -1) {
            if (errno == EINTR) continue; // 中断信号，继续读
            return -1; // 错误
        }
        total_read += ret;
    }
    return total_read;
}

// RPC调用的核心方法，负责将客户端的请求序列化并发送到服务端，同时接收服务端的响应
void KrpcChannel::CallMethod(const ::google::protobuf::MethodDescriptor *method,
                             ::google::protobuf::RpcController *controller,
                             const ::google::protobuf::Message *request,
                             ::google::protobuf::Message *response,
                             ::google::protobuf::Closure *done)
{
    // 提早生成唯一的路由 Key！
    uint64_t current_id = m_request_id_generator.fetch_add(1);

    // 拼装出本次请求的目标服务路径
    std::string service_name = method->service()->name();  
    std::string method_name = method->name();
    std::string method_path = "/" + service_name + "/" + method_name;

    // 1. ZK 客户端全局只启动一次
    if (!m_is_zk_started) {
        std::lock_guard<std::mutex> lock(m_init_mutex); 
        if (!m_is_zk_started) {
            m_zkCli.Start(); 
            m_is_zk_started = true; 
        }
    }

    // 2. 从专属路由表中获取连接
    std::shared_ptr<ConnectionContext> ctx;
    std::shared_ptr<RouteTable> target_route_table; // 用来接住当前路由表的快照
    {
        std::lock_guard<std::mutex> lock(m_route_mutex);
        auto route_it = m_service_routers.find(method_path);
        if (route_it != m_service_routers.end()) {
            target_route_table = route_it->second; // 拿到快照，引用计数+1
        }
    }

    // 3. 如果没找到，说明是该服务第一次被调用，按需建环！
    if (!target_route_table) {
        std::lock_guard<std::mutex> lock(m_init_mutex); // 防止多个线程同时去建环
        
        // 再次检查是否被其他线程建好了 (Double-Checked Locking)
        {
            std::lock_guard<std::mutex> r_lock(m_route_mutex);
            auto route_it = m_service_routers.find(method_path);
            if (route_it != m_service_routers.end()) {
                target_route_table = route_it->second;
            }
        }
        
        // 真的没建好，去 ZK 拉取
        if (!target_route_table) {
            RefreshConnections(method_path);
            // 建好之后拿出来
            std::lock_guard<std::mutex> r_lock(m_route_mutex);
            target_route_table = m_service_routers[method_path];
        }
    }

    if (!target_route_table || target_route_table->hash_ring.empty()) {
        controller->SetFailed("Fatal Error: Cluster is down for service: " + method_path);
        return;
    }

    // 4. 完全无锁化并行执行区
    // 🚀 性能与分布兼顾的整数哈希 (Integer Avalanche Hash)
    // 只有 3 条极快的位运算指令，不仅避免了 std::string 拷贝，还能将 1,2,3 彻底打散到 2^32 整个圆环上！
    uint32_t hash_val = current_id;
    hash_val ^= hash_val >> 16;
    hash_val *= 0x85ebca6b;
    hash_val ^= hash_val >> 13;
    hash_val *= 0xc2b2ae35;
    hash_val ^= hash_val >> 16;

    auto it = target_route_table->hash_ring.upper_bound(hash_val);
    if (it == target_route_table->hash_ring.end()) {
        it = target_route_table->hash_ring.begin();
    }
    ctx = it->second;

    // 断线重连兜底
    if (!ctx->is_connected) {
        std::lock_guard<std::mutex> lock(ctx->send_mutex);
        if (!ctx->is_connected) {
            if (!connect_node(ctx.get(), ctx->ip.c_str(), ctx->port)) {
                controller->SetFailed("Selected connection is offline");
                return;
            }
        }
    }

    // 5. 序列化请求头与包体
    std::string args_str;
    if (!request->SerializeToString(&args_str)) {
        controller->SetFailed("serialize request fail");
        return;
    }

    Krpc::RpcHeader krpcheader;
    krpcheader.set_service_name(service_name);
    krpcheader.set_method_name(method_name);
    krpcheader.set_args_size(args_str.size());
    krpcheader.set_request_id(current_id); 

    std::string rpc_header_str;
    if (!krpcheader.SerializeToString(&rpc_header_str)) {
        controller->SetFailed("serialize rpc header error!");
        return;
    }
    
    uint32_t header_size = rpc_header_str.size();
    uint32_t total_len = 4 + header_size + args_str.size(); 
    uint32_t net_total_len = htonl(total_len);
    uint32_t net_header_len = htonl(header_size);

    // 6. 注册 promise(先注册，准备收包)
    std::promise<std::string> prom;
    std::future<std::string> fut = prom.get_future();

    // 通过 request_id 取模，计算这个请求该去哪个桶排队
    size_t bucket_index = current_id % BUCKET_NUM;
    auto& bucket = m_promise_buckets[bucket_index];
    {
        // 只锁这 1/16 的区域！
        std::lock_guard<std::mutex> bucket_lock(bucket->mutex);
        bucket->pending_requests[current_id] = std::move(prom);
    }

    // 7. 自适应双擎发包策略
    {
        std::lock_guard<std::mutex> lock(ctx->send_mutex);
        int send_result = -1;

        if (total_len < 256) {
            // 针对小包：极速缓存拼接，利用 CPU L1 缓存极速暴力拷贝，避免内核机制开销
            std::string send_rpc_str;
            send_rpc_str.reserve(4 + 4 + header_size + args_str.size());
            send_rpc_str.append((char*)&net_total_len, 4);
            send_rpc_str.append((char*)&net_header_len, 4);
            send_rpc_str.append(rpc_header_str);
            send_rpc_str.append(args_str);

            send_result = send(ctx->fd, send_rpc_str.c_str(), send_rpc_str.size(), MSG_NOSIGNAL);
        } else {
            // 针对大包（如海量数据拉取）：DMA 聚集写，告别内存拼接，由操作系统网卡驱动进行底层零拷贝
            struct iovec iov[4];
            iov[0].iov_base = &net_total_len;
            iov[0].iov_len  = 4;
            iov[1].iov_base = &net_header_len;
            iov[1].iov_len  = 4;
            // string 的 data() 返回 const char*，iovec 需要 void*，进行强转
            iov[2].iov_base = (void*)rpc_header_str.data(); 
            iov[2].iov_len  = rpc_header_str.size();
            iov[3].iov_base = (void*)args_str.data();
            iov[3].iov_len  = args_str.size();

            struct msghdr msg;
            memset(&msg, 0, sizeof(msg));
            msg.msg_iov = iov;
            msg.msg_iovlen = 4; // 告诉内核，有 4 块碎片内存需要合并发送

            send_result = sendmsg(ctx->fd, &msg, MSG_NOSIGNAL);
        }

        // 统一处理发送失败的容灾兜底逻辑
        if (-1 == send_result) {
            close(ctx->fd);
            ctx->fd = -1;
            ctx->is_connected = false; // 标记断开，触发下一轮自动重连
            controller->SetFailed("network send error");

            // 发送失败，必须把注册的 promise 擦除掉，防止内存泄漏
            std::lock_guard<std::mutex> bucket_lock(bucket->mutex);
            bucket->pending_requests.erase(current_id);
            return;
        }
    }

    // 8. 等待各自专属的 ReadTask 唤醒
    if (fut.wait_for(std::chrono::seconds(3)) == std::future_status::timeout) {
        controller->SetFailed("rpc call timeout");
        std::lock_guard<std::mutex> bucket_lock(bucket->mutex);
        bucket->pending_requests.erase(current_id);
        return;
    }

    std::string response_str = fut.get();

    if (!response->ParseFromString(response_str)) {
        controller->SetFailed("parse response error");
        return;
    }
}

// 创建并初始化特定上下文的 socket 连接
bool KrpcChannel::connect_node(ConnectionContext* ctx, const char *ip, uint16_t port) {
    int clientfd = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == clientfd) {
        LOG(ERROR) << "socket error";  
        return false;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;  
    server_addr.sin_port = htons(port);  
    server_addr.sin_addr.s_addr = inet_addr(ip);  

    if (-1 == connect(clientfd, (struct sockaddr *)&server_addr, sizeof(server_addr))) {
        close(clientfd);  
        LOG(ERROR) << "connect server error";  
        return false;
    }

    ctx->fd = clientfd;
    ctx->is_connected = true;

    // 建连成功后，启动专属收包线程，并传入该连接的 fd
    std::thread read_thread(&KrpcChannel::ReadTask, this, clientfd);
    read_thread.detach(); 

    return true;
}

// 当 ZooKeeper 发现服务器宕机或新上线时，会自动触发这个回调
void KrpcChannel::OnNodeChange(zhandle_t *zh, int type, int status, const char *path, void *watcherCtx) {
    if (type == ZOO_CHILD_EVENT) { // 如果是子节点（IP目录）发生变动
        KrpcChannel* channel = static_cast<KrpcChannel*>(watcherCtx);
        LOG(WARNING) << "🚨 [容灾警报] ZooKeeper 探测到节点上下线，触发动态路由表重建: " << path;

        // 直接将变化的服务路径传给刷新函数，只重建这一个服务的哈希环！
        channel->RefreshConnections(path);
    }
}

// 核心的“无锁化”路由表热替换逻辑
void KrpcChannel::RefreshConnections(const std::string& method_path) {
    // 1. 拉取特定服务的最新列表，并且重新挂载 Watcher，因为 ZK 的 Watcher 是一次性的！
    std::vector<std::string> hosts = m_zkCli.GetChildrenWithWatch(method_path.c_str(), &KrpcChannel::OnNodeChange, this);

    // 创建一个全新的 shared_ptr 路由表
    auto new_route_table = std::make_shared<RouteTable>();
    std::hash<std::string> hash_fn; // C++11 标准字符串哈希函数
    
    for (const auto& host : hosts) {
        int idx = host.find(":");
        if (idx != -1) {
            std::string node_ip = host.substr(0, idx);
            uint16_t node_port = atoi(host.substr(idx + 1).c_str());
            
            for (int i = 0; i < 4; ++i) { 
                auto ctx = std::make_shared<ConnectionContext>();
                ctx->ip = node_ip;
                ctx->port = node_port;
                if (connect_node(ctx.get(), node_ip.c_str(), node_port)) {
                    new_route_table->pool_size++;

                    for (int v = 0; v < VIRTUAL_NODES; ++v) {
                        std::string vnode_name = node_ip + ":" + std::to_string(node_port) 
                                                 + "_conn" + std::to_string(i) 
                                                 + "_VN" + std::to_string(v);
                        uint32_t hash_val = hash_fn(vnode_name);
                        // 挂载到临时路由表中
                        new_route_table->hash_ring[hash_val] = ctx; 
                    }
                }
            }
        }
    }

    // 2. 瞬间原子化替换特定哈希环！
    {
        std::lock_guard<std::mutex> lock(m_route_mutex);
        m_service_routers[method_path] = new_route_table;
    }

    LOG(INFO) << "✅ 服务 [" << method_path << "] 路由表重建完成！活跃连接: " 
              << new_route_table->pool_size << "，虚拟节点: " << new_route_table->hash_ring.size();
}

KrpcChannel::KrpcChannel(bool connectNow) {// 预先分配好 16 个带有独立锁的哈希表桶
    for (size_t i = 0; i < BUCKET_NUM; ++i) {
        m_promise_buckets.push_back(std::unique_ptr<PromiseBucket>(new PromiseBucket()));
    }
}

KrpcChannel::~KrpcChannel() {}
```

---

## File: `src/Krpcconfig.cc`

```cpp
#include "Krpcconfig.h"
#include <iostream>
#include <fstream>
#include <memory>

// 加载配置文件，解析配置文件中的键值对
void Krpcconfig::LoadConfigFile(const char *config_file) {
    FILE *pf = fopen(config_file, "r");
    if (pf == nullptr) {
        // 不要兜底！直接报 Fatal 错误并退出进程！
        std::cerr << "[FATAL ERROR] 找不到或无法打开配置文件: " 
                  << (config_file ? config_file : "null") 
                  << " \n请检查启动参数路径是否正确！(例如: ./server -i ../conf/test.conf)" << std::endl;
        exit(EXIT_FAILURE); 
    }

    // 使用智能指针管理文件指针，确保文件在退出时自动fclose
    std::unique_ptr<FILE, decltype(&fclose)> pf_ptr(pf, &fclose);

    char buf[1024];  // 用于存储从文件中读取的每一行内容
    
    // 使用pf.get()方法获取原始指针，逐行读取文件内容
    while (fgets(buf, 1024, pf_ptr.get()) != nullptr) {
        std::string read_buf(buf);  // 将读取的内容转换为字符串
        Trim(read_buf);  // 去掉字符串前后的空格

        // 忽略注释行（以#开头）和空行
        if (read_buf[0] == '#' || read_buf.empty()) continue;

        // 查找键值对的分隔符'='
        int index = read_buf.find('=');
        if (index == -1) continue;  // 如果没有找到'='，跳过该行

        // 提取键（key）
        std::string key = read_buf.substr(0, index);
        Trim(key);  // 去掉key前后的空格

        std::string value = read_buf.substr(index + 1); 
        Trim(value);

        // 将键值对存入配置map中
        config_map.insert({key, value});
    }
}

// 根据key查找对应的value
std::string Krpcconfig::Load(const std::string &key) {
    // 优先读取环境变量 (比如 RPC_SERVER_IP)
    char* env_val = std::getenv(key.c_str());
    if (env_val != nullptr) return std::string(env_val);

    // 再读配置文件
    auto it = config_map.find(key);
    return (it != config_map.end()) ? it->second : "";
}

// 去掉字符串前后所有的空白字符（包括空格、制表符、\r、\n）
void Krpcconfig::Trim(std::string &read_buf) {
    // 找到第一个不是空白字符的位置
    int start = read_buf.find_first_not_of(" \t\r\n");
    if (start == -1) {
        read_buf = ""; // 全是空白字符
        return;
    }
    
    // 找到最后一个不是空白字符的位置
    int end = read_buf.find_last_not_of(" \t\r\n");
    
    // 精准截取有效内容
    read_buf = read_buf.substr(start, end - start + 1);
}
```

---

## File: `src/Krpccontroller.cc`

```cpp
#include "Krpccontroller.h"

// 构造函数，初始化控制器状态
Krpccontroller::Krpccontroller() {
    m_failed = false;  // 初始状态为未失败
    m_errText = "";    // 错误信息初始为空
}

// 重置控制器状态，将失败标志和错误信息清空
void Krpccontroller::Reset() {
    m_failed = false;  // 重置失败标志
    m_errText = "";    // 清空错误信息
}

// 判断当前RPC调用是否失败
bool Krpccontroller::Failed() const {
    return m_failed;  // 返回失败标志
}

// 获取错误信息
std::string Krpccontroller::ErrorText() const {
    return m_errText;  // 返回错误信息
}

// 设置RPC调用失败，并记录失败原因
void Krpccontroller::SetFailed(const std::string &reason) {
    m_failed = true;   // 设置失败标志
    m_errText = reason; // 记录失败原因
}

// 以下功能未实现，是RPC服务端提供的取消功能
// 开始取消RPC调用（未实现）
void Krpccontroller::StartCancel() {
    // 目前为空，未实现具体功能
}

// 判断RPC调用是否被取消（未实现）
bool Krpccontroller::IsCanceled() const {
    return false;  // 默认返回false，表示未被取消
}

// 注册取消回调函数（未实现）
void Krpccontroller::NotifyOnCancel(google::protobuf::Closure* callback) {
    // 目前为空，未实现具体功能
}
```

---

## File: `src/Krpcheader.proto`

```protobuf
syntax="proto3";
package Krpc;

message RpcHeader{
    bytes service_name=1;
    bytes method_name=2;
    uint32 args_size=3;
    uint64 request_id=4;
}
```

---

## File: `src/Krpcprovider.cc`

```cpp
#include "Krpcprovider.h"
#include "Krpcapplication.h"
#include "Krpcheader.pb.h"
#include "KrpcLogger.h"
#include <iostream>

// 自定义的 Closure 类,完美绕过 Protobuf 原生 NewCallback 只能绑定 2 个参数的限制！
// 结合 C++11 的 Lambda，实现任意数量参数的捕获和延期执行。
namespace {
    class RpcClosure : public google::protobuf::Closure {
    public:
        using CallBack = std::function<void()>;
        RpcClosure(CallBack cb) : cb_(cb) {}
        void Run() override {
            cb_();          
            delete this;    
        }
    private:
        CallBack cb_;
    };
} // end anonymous namespace

// 注册服务对象及其方法，以便服务端能够处理客户端的RPC请求
void KrpcProvider::NotifyService(google::protobuf::Service *service) {
    // 服务端需要知道客户端想要调用的服务对象和方法，这些信息会保存在一个数据结构（如 ServiceInfo）中。
    ServiceInfo service_info;

    // 参数类型设置为 google::protobuf::Service，是因为所有由 protobuf 生成的服务类
    // 都继承自 google::protobuf::Service，这样我们可以通过基类指针指向子类对象，实现动态多态。
    // 通过动态多态调用 service->GetDescriptor()，会返回 protobuf 生成的服务类的描述信息（ServiceDescriptor）。
    const google::protobuf::ServiceDescriptor *psd = service->GetDescriptor();

    // 通过 ServiceDescriptor，我们可以获取该服务类中定义的方法列表，并进行相应的注册和管理。
    std::string service_name = psd->name();
    int method_count = psd->method_count();

    // 打印服务名
    std::cout << "service_name=" << service_name << std::endl;

    // 遍历服务中的所有方法，并注册到服务信息中
    for (int i = 0; i < method_count; ++i) {
        // 获取服务中的方法描述
        const google::protobuf::MethodDescriptor *pmd = psd->method(i);
        std::string method_name = pmd->name();
        std::cout << "method_name=" << method_name << std::endl;
        service_info.method_map.emplace(method_name, pmd);  // 将方法名和方法描述符存入map
    }
    service_info.service = service;  // 保存服务对象
    service_map.emplace(service_name, service_info);  // 将服务信息存入服务map
}

// 启动RPC服务节点，开始提供远程网络调用服务
void KrpcProvider::Run() {
    // 读取配置文件中的RPC服务器IP和端口
    std::string ip = KrpcApplication::GetInstance().GetConfig().Load("rpcserverip");
    int port = atoi(KrpcApplication::GetInstance().GetConfig().Load("rpcserverport").c_str());

    // 使用muduo网络库，创建地址对象, 端口传 0，告诉操作系统自动分配一个空闲端口
    muduo::net::InetAddress address(ip, port);

    // 创建TcpServer对象
    std::shared_ptr<muduo::net::TcpServer> server = std::make_shared<muduo::net::TcpServer>(&event_loop, address, "KrpcProvider");

    // 绑定连接回调和消息回调，分离网络连接业务和消息处理业务
    server->setConnectionCallback(std::bind(&KrpcProvider::OnConnection, this, std::placeholders::_1));
    server->setMessageCallback(std::bind(&KrpcProvider::OnMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    server->setThreadNum(4);

    server->start(); // 先启动服务，muduo 内部会进行 bind 和 listen
    std::cout << "RpcProvider start service at ip:" << ip << " port:" << port << std::endl;
    
    // 将当前RPC节点上要发布的服务全部注册到ZooKeeper上，让RPC客户端可以在ZooKeeper上发现服务
    ZkClient zkclient;
    zkclient.Start();  // 连接ZooKeeper服务器

    // service_name、method_name都是永久节点
    for (auto &sp : service_map) {
        // service_name 在ZooKeeper中的目录是"/"+service_name
        std::string service_path = "/" + sp.first;
        zkclient.Create(service_path.c_str(), nullptr, 0);  // 创建服务节点

        for (auto &mp : sp.second.method_map) {
            // 改动1：方法节点现在必须也是持久化节点（相当于一个文件夹）
            std::string method_path = service_path + "/" + mp.first;
            zkclient.Create(method_path.c_str(), nullptr, 0);
            
            // 改动2：将 IP:Port 拼接成具体的子节点路径
            char method_path_data[128] = {0};
            sprintf(method_path_data, "%s/%s:%d", method_path.c_str(), ip.c_str(), port);   // 将IP和端口信息存入节点数据
            
            // 改动3：创建这台机器专属的临时子节点！
            // 路径如：/UserServiceRpc/Login/127.0.0.1:8000
            // 当这台机器宕机，这个特定的子节点就会自动消失！
            zkclient.Create(method_path_data, nullptr, 0, ZOO_EPHEMERAL);
        }
    }

    event_loop.loop();  // 进入事件循环
}

// 连接回调函数，处理客户端连接事件
void KrpcProvider::OnConnection(const muduo::net::TcpConnectionPtr &conn) {
    if (!conn->connected()) {
        conn->shutdown();
    }
}

// 消息回调函数，处理客户端发送的RPC请求
void KrpcProvider::OnMessage(const muduo::net::TcpConnectionPtr &conn, muduo::net::Buffer *buffer, muduo::Timestamp receive_time) {
     // 循环处理缓冲区，解决粘包问题
    while (buffer->readableBytes() >= 4) {
        // 1. 预读取前4个字节（Total Length）
        // peek() 不会移动 buffer 的读指针
        uint32_t total_len = 0;
        std::memcpy(&total_len, buffer->peek(), 4);
        total_len = ntohl(total_len); // 网络字节序转主机字节序

        // 2. 检查数据是否完整（拆包处理）
        // 如果缓冲区剩余数据 小于 4字节长度头 + 内容长度，说明包没收全，退出等待下一次
        if (buffer->readableBytes() < 4 + total_len) {
            break; 
        }

        // --- 数据包完整，开始解包 ---
        // 3. 真正读取数据
        buffer->retrieve(4); // 消耗掉前4个字节的长度头

        // 读取 4字节的 Header Length
        uint32_t header_len = 0;
        const char* data_ptr = buffer->peek();
        std::memcpy(&header_len, data_ptr, 4);
        header_len = ntohl(header_len);
        
        buffer->retrieve(4); // 消耗掉 header length

        // 读取 Header 数据
        std::string rpc_header_str(buffer->peek(), header_len);
        Krpc::RpcHeader krpcHeader;
        buffer->retrieve(header_len); // 消耗掉 header data

        // 读取 Body 数据 (args)
        uint32_t args_size = total_len - 4 - header_len; // 总长度 - header长度字段(4) - header内容
        std::string args_str(buffer->peek(), args_size);
        buffer->retrieve(args_size); // 消耗掉 body data

        // 4. 业务逻辑处理
        if (!krpcHeader.ParseFromString(rpc_header_str)) {
            std::cout << "header parse error" << std::endl;
            return;
        }
        
        std::string service_name = krpcHeader.service_name();
        std::string method_name = krpcHeader.method_name();

        auto it = service_map.find(service_name);
        if (it == service_map.end()) {
            std::cout << service_name << " is not exist!" << std::endl;
            return;
        }
        auto mit = it->second.method_map.find(method_name);
        if (mit == it->second.method_map.end()) {
            std::cout << service_name << "." << method_name << " is not exist!" << std::endl;
            return;
        }

        google::protobuf::Service *service = it->second.service;
        const google::protobuf::MethodDescriptor *method = mit->second;

        google::protobuf::Message *request = service->GetRequestPrototype(method).New();
        if (!request->ParseFromString(args_str)) {
            std::cout << "request parse error" << std::endl;
            return;
        }
        google::protobuf::Message *response = service->GetResponsePrototype(method).New();

        uint64_t req_id = krpcHeader.request_id();
        // 废弃 NewCallback，直接用 Lambda 捕获 3 个参数！优雅又现代。
        google::protobuf::Closure *done = new RpcClosure([this, conn, response, req_id]() {
            this->SendRpcResponse(conn, response, req_id);
        });
        service->CallMethod(method, nullptr, request, response, done);
    }
}

// 发送RPC响应给客户端
void KrpcProvider::SendRpcResponse(const muduo::net::TcpConnectionPtr &conn, google::protobuf::Message *response, uint64_t request_id) {
    std::string response_str;
    if (response->SerializeToString(&response_str)) {
        
        Krpc::RpcHeader krpcheader;
        krpcheader.set_request_id(request_id); 
        
        std::string rpc_header_str;
        krpcheader.SerializeToString(&rpc_header_str);

        uint32_t header_size = rpc_header_str.size();
        uint32_t total_len = 4 + header_size + response_str.size(); 
        
        uint32_t net_total_len = htonl(total_len);
        uint32_t net_header_len = htonl(header_size);

        std::string send_buf;
        send_buf.reserve(4 + 4 + header_size + response_str.size());
        
        send_buf.append((char*)&net_total_len, 4);
        send_buf.append((char*)&net_header_len, 4);
        send_buf.append(rpc_header_str);
        send_buf.append(response_str);

        conn->send(send_buf);
    } else {
        LOG(ERROR) << "serialize response error!";
    }
}

// 析构函数，退出事件循环
KrpcProvider::~KrpcProvider() {
    event_loop.quit();
}
```

---

## File: `src/include/KrpcLogger.h`

```cpp
#ifndef KRPC_LOG_H
#define KRPC_LOG_H
#include<glog/logging.h>
#include<string>
//采用RAII的思想
class KrpcLogger
{
public:
      //构造函数，自动初始化glog
      explicit KrpcLogger(const char *argv0)
      {
        google::InitGoogleLogging(argv0);
        FLAGS_colorlogtostderr=true;//启用彩色日志
        FLAGS_logtostderr=true;//默认输出标准错误
      }
      ~KrpcLogger(){
        google::ShutdownGoogleLogging();
      }
      //提供静态日志方法
      static void Info(const std::string &message)
      {
        LOG(INFO)<<message;
      }
      static void Warning(const std::string &message){
        LOG(WARNING)<<message;
      }
      static void ERROR(const std::string &message){
        LOG(ERROR)<<message;
      }
          static void Fatal(const std::string& message) {
        LOG(FATAL) << message;
    }
//禁用拷贝构造函数和重载赋值函数
private:
    KrpcLogger(const KrpcLogger&)=delete;
    KrpcLogger& operator=(const KrpcLogger&)=delete;
};

#endif
```

---

## File: `src/include/Krpcapplication.h`

```cpp
#ifndef _Krpcapplication_H
#define _Krpcapplication_H
#include "Krpcconfig.h"
#include "Krpcchannel.h" 
#include  "Krpccontroller.h"
#include<mutex>
//Krpc基础类，负责框架的一些初始化操作
class KrpcApplication
{
    public:
    static void Init(int argc,char **argv);
    static KrpcApplication & GetInstance();
    static void deleteInstance();
    static Krpcconfig& GetConfig();
    private:
    static Krpcconfig m_config;
    static KrpcApplication * m_application;//全局唯一单例访问对象
    static std::mutex m_mutex;
    KrpcApplication(){}
    ~KrpcApplication(){}
    KrpcApplication(const KrpcApplication&)=delete;
    KrpcApplication(KrpcApplication&&)=delete;
};
#endif 
```

---

## File: `src/include/Krpcchannel.h`

```cpp
#ifndef _Krpcchannel_h_
#define _Krpcchannel_h_
#include <google/protobuf/service.h>
#include <string>
#include <cstdint>
#include <future>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <vector>
#include <memory>
#include <sys/types.h>
#include <map>
#include "zookeeperutil.h"

// 每条TCP连接的独立上下文
struct ConnectionContext {
    int fd = -1;
    std::mutex send_mutex; // 每条连接专属的发送锁！
    std::atomic<bool> is_connected{false}; // 标记当前连接是否存活
    // 记录当前这个连接到底连的是谁
    std::string ip;
    uint16_t port = 0;
    // 利用 RAII 机制，当这个上下文被销毁时，自动切断底层的网线
    ~ConnectionContext() {
        if (fd >= 0) {
            close(fd); 
            // 只要这里 close 了 fd，后台正在阻塞 recv 的 ReadTask 就会瞬间收到 0，退出循环并安全结束线程！
            fd = -1;
        }
    }
};

// 定义小哈希表（桶）结构
struct PromiseBucket {
    std::mutex mutex; // 这个桶专属的独立锁
    std::unordered_map<uint64_t, std::promise<std::string>> pending_requests;
};

// 定义针对【单个服务】的独立路由表
struct RouteTable {
    size_t pool_size = 0; // 该服务当前存活的物理连接数
    std::map<uint32_t, std::shared_ptr<ConnectionContext>> hash_ring; // 专属哈希环
};

class KrpcChannel : public google::protobuf::RpcChannel
{
public:
    KrpcChannel(bool connectNow);
    virtual ~KrpcChannel();
    void CallMethod(const ::google::protobuf::MethodDescriptor *method,
                    ::google::protobuf::RpcController *controller,
                    const ::google::protobuf::Message *request,
                    ::google::protobuf::Message *response,
                    ::google::protobuf::Closure *done) override; // override可以验证是否是虚函数
                    
private:
    // 集群容灾核心组件
    ZkClient m_zkCli;                   // 持久化 ZK 客户端，保持与注册中心的 Session
    std::mutex m_route_mutex;           // 保护路由表，防止读写冲突
    std::mutex m_init_mutex;            // 专门保护双检锁初始化的锁！

    // 全量微服务路由网关核心组件
    std::atomic<bool> m_is_zk_started{false}; // ZK 客户端是否已启动（全局只需启一次）
    
    // 核心大招：二维路由表 Key 是 method_path (如 /UserServiceRpc/Login)，并将Value 改为 shared_ptr，开启无锁化读！
    std::unordered_map<std::string, std::shared_ptr<RouteTable>> m_service_routers;
    static const int VIRTUAL_NODES = 150; 

    // 多路复用、段锁核心组件
    std::atomic<uint64_t> m_request_id_generator{0}; 
    static const size_t BUCKET_NUM = 16;   // 替代原来的全局锁和单表，定义 16 个桶
    std::vector<std::unique_ptr<PromiseBucket>> m_promise_buckets;
    
    
    bool connect_node(ConnectionContext* ctx, const char *ip, uint16_t port);
    ssize_t recv_exact(int fd, char* buf, size_t size);

    // 动态刷新函数
    void RefreshConnections(const std::string& method_path); // 执行路由表重建
    static void OnNodeChange(zhandle_t *zh, int type, int status, const char *path, void *watcherCtx); // ZK 回调函数

    void ReadTask(int fd); // 后台接收线程
};
#endif
```

---

## File: `src/include/Krpcconfig.h`

```cpp
#ifndef _Krpcconfig_h
#define _Krpcconfig_h
#include <unordered_map>
#include <string>

class Krpcconfig{
    public:
    void LoadConfigFile(const char *config_file);//加载配置文件
    std::string Load(const std::string &key);//查找key对应的value
    private:
    std::unordered_map<std::string, std::string> config_map;
    void Trim(std::string &read_buf);//去掉字符串前后的空格
};
#endif
```

---

## File: `src/include/Krpccontroller.h`

```cpp
#ifndef _Krpccontroller_H
#define _Krpccontroller_H

#include<google/protobuf/service.h>
#include<string>
//用于描述RPC调用的控制器
//其主要作用是跟踪RPC方法调用的状态、错误信息并提供控制功能(如取消调用)。
class Krpccontroller:public google::protobuf::RpcController
{
public:
 Krpccontroller();
 void Reset();
 bool Failed() const;
std::string ErrorText() const;
void SetFailed(const std::string &reason);

//目前未实现具体的功能
void StartCancel();
bool IsCanceled() const;
void NotifyOnCancel(google::protobuf::Closure* callback);
private:
 bool m_failed;//RPC方法执行过程中的状态
 std::string m_errText;//RPC方法执行过程中的错误信息
};

#endif
```

---

## File: `src/include/Krpcprovider.h`

```cpp
#ifndef _Krpcprovider_H__
#define _Krpcprovider_H__
#include "google/protobuf/service.h"
#include "zookeeperutil.h"
#include<muduo/net/TcpServer.h>
#include<muduo/net/EventLoop.h>
#include<muduo/net/InetAddress.h>
#include<muduo/net/TcpConnection.h>
#include<google/protobuf/descriptor.h>
#include<functional>
#include<string>
#include<unordered_map>

class KrpcProvider
{
public:
    //这里是提供给外部使用的，可以发布rpc方法的函数接口。
    void NotifyService(google::protobuf::Service* service);
      ~KrpcProvider();
    //启动rpc服务节点，开始提供rpc远程网络调用服务
    void Run();
private:
    muduo::net::EventLoop event_loop;
    struct ServiceInfo
    {
        google::protobuf::Service* service;
        std::unordered_map<std::string, const google::protobuf::MethodDescriptor*> method_map;
    };
    std::unordered_map<std::string, ServiceInfo>service_map;//保存服务对象和rpc方法
    
    void OnConnection(const muduo::net::TcpConnectionPtr& conn);
    void OnMessage(const muduo::net::TcpConnectionPtr& conn, muduo::net::Buffer* buffer, muduo::Timestamp receive_time);
    void SendRpcResponse(const muduo::net::TcpConnectionPtr& conn, google::protobuf::Message* response, uint64_t request_id);
};
#endif 


```

---

## File: `src/include/zookeeperutil.h`

```cpp
#ifndef _zookeeperutil_h_
#define _zookeeperutil_h_

#include <zookeeper/zookeeper.h>
#include <string>
#include <mutex>
#include <condition_variable>
#include <semaphore.h>
#include <vector>

//封装的zk客户端
class ZkClient
{
public:
    ZkClient();
    ~ZkClient();
    //zkclient启动连接zkserver
    void Start();
    //在zkserver中创建一个节点，根据指定的path
    void Create(const char* path, const char* data, int datalen, int state=0);
    //根据参数指定的znode节点路径，或者znode节点值
    std::string GetData(const char* path);
    //提供给 watcher 回调使用的唤醒接口
    void NotifyConnected();

    // 获取指定节点下的所有子节点列表
    std::vector<std::string> GetChildren(const char* path);

    // 获取指定节点下的所有子节点列表，并注册专属监听器
    std::vector<std::string> GetChildrenWithWatch(const char* path, watcher_fn watcher, void* watcherCtx);

private:
    //Zk的客户端句柄
    zhandle_t* m_zhandle;
    
    std::mutex m_mutex;             // 全局锁，用于保护共享变量的线程安全
    std::condition_variable m_cv;   // 条件变量，用于线程间通信
    bool m_connected;               // 标记ZooKeeper客户端是否连接成功
};
#endif

```

---

## File: `src/zookeeperutil.cc`

```cpp
#include "zookeeperutil.h"
#include "Krpcapplication.h"
#include "KrpcLogger.h"

// 全局的watcher观察器，用于接收ZooKeeper服务器的通知
void global_watcher(zhandle_t *zh, int type, int status, const char *path, void *watcherCtx) {
    if (type == ZOO_SESSION_EVENT) {  
        if (status == ZOO_CONNECTED_STATE) {  
            // 将 void* 强转回 ZkClient* 指针
            ZkClient *client = static_cast<ZkClient*>(watcherCtx);
            if (client != nullptr) {
                client->NotifyConnected(); // 精准唤醒触发回调的那个对象
            }
        }
    }
}

ZkClient::ZkClient() : m_zhandle(nullptr), m_connected(false) {}

ZkClient::~ZkClient() {
    if (m_zhandle != nullptr) {
        zookeeper_close(m_zhandle);  // 关闭ZooKeeper连接
    }
}

// 唤醒当前对象
void ZkClient::NotifyConnected() {
    std::lock_guard<std::mutex> lock(m_mutex);  
    m_connected = true;  
    m_cv.notify_all();  
}

// 启动ZooKeeper客户端，连接ZooKeeper服务器
void ZkClient::Start() {
    // 从配置文件中读取ZooKeeper服务器的IP和端口
    std::string host = KrpcApplication::GetInstance().GetConfig().Load("zookeeperip");
    std::string port = KrpcApplication::GetInstance().GetConfig().Load("zookeeperport");
    std::string connstr = host + ":" + port;  // 拼接连接字符串

    /*
    zookeeper_mt：多线程版本
    ZooKeeper的API客户端程序提供了三个线程：
    1. API调用线程
    2. 网络I/O线程（使用pthread_create和poll）
    3. watcher回调线程（使用pthread_create）
    */

    // 使用zookeeper_init初始化一个ZooKeeper客户端对象，异步建立与服务器的连接
    m_zhandle = zookeeper_init(connstr.c_str(), global_watcher, 6000, nullptr, this, 0);
    if (nullptr == m_zhandle) {  // 初始化失败
        LOG(ERROR) << "zookeeper_init error";
        exit(EXIT_FAILURE);  // 退出程序
    }

    // 等待连接成功
    std::unique_lock<std::mutex> lock(m_mutex);
    // 【规范】设置 10 秒超时。如果 10 秒连不上，直接报错退出，避免进程僵死
    if (!m_cv.wait_for(lock, std::chrono::seconds(10), [this] { return m_connected; })) {
        LOG(FATAL) << "ZooKeeper 连接超时 (10秒)! 请检查 IP 和端口是否正确。";
    }
    LOG(INFO) << "zookeeper_init success";
}

// 创建ZooKeeper节点
void ZkClient::Create(const char *path, const char *data, int datalen, int state) {
    char path_buffer[128];  // 用于存储创建的节点路径
    int bufferlen = sizeof(path_buffer);

    // 检查节点是否已经存在
    int flag = zoo_exists(m_zhandle, path, 0, nullptr);
    if (flag == ZNONODE) {  // 如果节点不存在
        // 创建指定的ZooKeeper节点
        flag = zoo_create(m_zhandle, path, data, datalen, &ZOO_OPEN_ACL_UNSAFE, state, path_buffer, bufferlen);
        if (flag == ZOK) {  // 创建成功
            LOG(INFO) << "znode create success... path:" << path;
        } else {  // 创建失败
            LOG(ERROR) << "znode create failed... path:" << path;
            exit(EXIT_FAILURE);  // 退出程序
        }
    }
}

// 获取ZooKeeper节点的数据
std::string ZkClient::GetData(const char *path) {
    char buf[64];  // 用于存储节点数据
    int bufferlen = sizeof(buf);

    // 获取指定节点的数据
    int flag = zoo_get(m_zhandle, path, 0, buf, &bufferlen, nullptr);
    if (flag != ZOK) {  // 获取失败
        LOG(ERROR) << "zoo_get error";
        return "";  // 返回空字符串
    } else {  // 获取成功
        return buf;  // 返回节点数据
    }
    return "";  // 默认返回空字符串
}

std::vector<std::string> ZkClient::GetChildren(const char* path) {
    struct String_vector strings;
    // 调用原生的 zoo_get_children 获取所有子节点
    int flag = zoo_get_children(m_zhandle, path, 0, &strings);
    
    std::vector<std::string> children;
    if (flag == ZOK) {
        for (int i = 0; i < strings.count; ++i) {
            children.push_back(strings.data[i]);
        }
        // 释放 C 语言结构体的内存，防止内存泄漏！
        deallocate_String_vector(&strings);
    } else {
        LOG(ERROR) << "zoo_get_children error, path: " << path;
    }
    return children;
}

std::vector<std::string> ZkClient::GetChildrenWithWatch(const char* path, watcher_fn watcher, void* watcherCtx) {
    struct String_vector strings;
    // 使用 zoo_wget_children 绑定我们自定义的 watcher 回调
    int flag = zoo_wget_children(m_zhandle, path, watcher, watcherCtx, &strings);
    
    std::vector<std::string> children;
    if (flag == ZOK) {
        for (int i = 0; i < strings.count; ++i) {
            children.push_back(strings.data[i]);
        }
        deallocate_String_vector(&strings); // 防止内存泄漏
    } else {
        LOG(ERROR) << "zoo_wget_children error, path: " << path;
    }
    return children;
}
```

---

