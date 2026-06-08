# KRPC Project Source Collection

- Generated at: 2026-06-08 16:47:45
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
set(CMAKE_CXX_STANDARD 11)#在这里我设置了11，代表使用c++11标准
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
#include "../user.pb.h"
#include "Krpcapplication.h"
#include "Krpcprovider.h"

/*
UserService 原本是一个本地服务，提供了两个本地方法：Login 和 GetFriendLists。
现在通过 RPC 框架，这些方法可以被远程调用。
*/
class UserService : public Kuser::UserServiceRpc // 继承自 protobuf 生成的 RPC 服务基类
{
public:
    // 本地登录方法，用于处理实际的业务逻辑
    bool Login(std::string name, std::string pwd) {
        std::cout << "doing local service: Login" << std::endl;
        std::cout << "name:" << name << " pwd:" << pwd << std::endl;  
        return true;  // 模拟登录成功
    }

    /*
    重写基类 UserServiceRpc 的虚函数，这些方法会被 RPC 框架直接调用。
    1. 调用者（caller）通过 RPC 框架发送 Login 请求。
    2. 服务提供者（callee）接收到请求后，调用下面重写的 Login 方法。
    */
    void Login(::google::protobuf::RpcController* controller,
              const ::Kuser::LoginRequest* request,
              ::Kuser::LoginResponse* response,
              ::google::protobuf::Closure* done) {
        // 从请求中获取用户名和密码
        std::string name = request->name();
        std::string pwd = request->pwd();

        // 调用本地业务逻辑处理登录
        bool login_result = Login(name, pwd); 

        // 将响应结果写入 response 对象
        Kuser::ResultCode *code = response->mutable_result();
        code->set_errcode(0);  // 设置错误码为 0，表示成功
        code->set_errmsg("");  // 设置错误信息为空
        response->set_success(login_result);  // 设置登录结果

        // 执行回调操作，框架会自动将响应序列化并发送给调用者
        done->Run();
    }
};

int main(int argc, char **argv) {
    // 调用框架的初始化操作，解析命令行参数并加载配置文件
    KrpcApplication::Init(argc, argv);

    // 创建一个 RPC 服务提供者对象
    KrpcProvider provider;

    // 将 UserService 对象发布到 RPC 节点上，使其可以被远程调用
    UserService my_service;
    provider.NotifyService(&my_service);

    // 启动 RPC 服务节点，进入阻塞状态，等待远程的 RPC 调用请求
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

// 压测函数
void send_request(Kuser::UserServiceRpc_Stub* stub, int requests_per_thread, std::vector<double>& local_latencies) {
    Kuser::LoginRequest request;
    request.set_name("benchmark_user");  
    request.set_pwd("123456");    
    Kuser::LoginResponse response;

    for (int i = 0; i < requests_per_thread; ++i) {
        Krpccontroller controller;
        auto req_start = std::chrono::high_resolution_clock::now();
        
        // 发起 RPC 调用
        stub->Login(&controller, &request, &response, nullptr);

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

    // 共享 Channel，测试多路复用
    KrpcChannel* channel = new KrpcChannel(false); 
    Kuser::UserServiceRpc_Stub stub(channel);

    std::vector<std::thread> threads;  
    std::vector<std::vector<double>> all_latencies(thread_count);
    for(int i=0; i<thread_count; ++i) {
        all_latencies[i].reserve(requests_per_thread);
    }

    LOG(INFO) << "Starting Benchmark...";
    auto start_time = std::chrono::high_resolution_clock::now();  

    for (int i = 0; i < thread_count; i++) {
        threads.emplace_back([&stub, requests_per_thread, &all_latencies, i]() {  
            send_request(&stub, requests_per_thread, all_latencies[i]);  
        });
    }

    for (auto &t : threads) { t.join(); }
    // 【添加这行】确保程序真的执行到了这里
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

    // 极限精简输出
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
#include "zookeeperutil.h"
#include "Krpcheader.pb.h"
#include "Krpcapplication.h"
#include "Krpccontroller.h"
#include "KrpcLogger.h"

// 新增后台独立接收线程，解析响应并精准唤醒对应的业务线程
void KrpcChannel::ReadTask() {
    while (true) {
        uint32_t total_len = 0;
        if (recv_exact(m_clientfd, (char*)&total_len, 4) != 4) break; 
        total_len = ntohl(total_len);

        uint32_t header_len = 0;
        if (recv_exact(m_clientfd, (char*)&header_len, 4) != 4) break;
        header_len = ntohl(header_len);

        std::vector<char> header_buf(header_len);
        if (recv_exact(m_clientfd, header_buf.data(), header_len) != header_len) break;
        std::string rpc_header_str(header_buf.data(), header_len);
        Krpc::RpcHeader krpcheader;
        krpcheader.ParseFromString(rpc_header_str);

        uint32_t body_len = total_len - 4 - header_len;
        std::vector<char> body_buf(body_len);
        if (recv_exact(m_clientfd, body_buf.data(), body_len) != body_len) break;
        std::string response_str(body_buf.data(), body_len);

        uint64_t req_id = krpcheader.request_id();
        std::lock_guard<std::mutex> lock(m_promise_mutex);
        auto it = m_pending_requests.find(req_id);
        // 唤醒promise对应的future
        if (it != m_pending_requests.end()) {
            it->second.set_value(response_str); 
            m_pending_requests.erase(it);       
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
    // 修改：加入双重检查锁，防止 100 个并发线程同时发起连接
    if (-1 == m_clientfd) {  
        std::lock_guard<std::mutex> lock(m_conn_mutex);
        if (-1 == m_clientfd) {
            const google::protobuf::ServiceDescriptor *sd = method->service();
            service_name = sd->name();  
            method_name = method->name();  

            ZkClient zkCli;
            zkCli.Start();  
            std::string host_data = QueryServiceHost(&zkCli, service_name, method_name, m_idx);  
            m_ip = host_data.substr(0, m_idx);  
            m_port = atoi(host_data.substr(m_idx + 1, host_data.size() - m_idx).c_str());  

            auto rt = newConnect(m_ip.c_str(), m_port);
            if (!rt) {
                controller->SetFailed("connect server error");
                LOG(ERROR) << "connect server error";  
                return;
            }
        }
    }

    std::string args_str;
    if (!request->SerializeToString(&args_str)) {
        controller->SetFailed("serialize request fail");
        return;
    }

    uint64_t current_id = m_request_id_generator.fetch_add(1);

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

    std::string send_rpc_str;
    send_rpc_str.reserve(4 + 4 + header_size + args_str.size());
    send_rpc_str.append((char*)&net_total_len, 4);
    send_rpc_str.append((char*)&net_header_len, 4);
    send_rpc_str.append(rpc_header_str);
    send_rpc_str.append(args_str);

    std::promise<std::string> prom;
    std::future<std::string> fut = prom.get_future();
    {
        std::lock_guard<std::mutex> lock(m_promise_mutex);
        m_pending_requests[current_id] = std::move(prom);
    }

    // 发送必须加锁，保护底层的 Socket
    {
        std::lock_guard<std::mutex> lock(m_send_mutex);
        if (-1 == send(m_clientfd, send_rpc_str.c_str(), send_rpc_str.size(), 0)) {
            // 关闭失效的 socket 描述符，防止 FD 泄漏
            close(m_clientfd);
            // 重置为 -1，触发下一个线程执行DCL(双检锁)
            m_clientfd = -1;
            controller->SetFailed("send error");
            std::lock_guard<std::mutex> plock(m_promise_mutex);
            m_pending_requests.erase(current_id);
            return;
        }
    }

    // 阻塞等待后台读线程通过 set_value 唤醒，最多等3秒
    if (fut.wait_for(std::chrono::seconds(3)) == std::future_status::timeout) {
        controller->SetFailed("rpc call timeout");
        std::lock_guard<std::mutex> lock(m_promise_mutex);
        m_pending_requests.erase(current_id);
        return;
    }

    std::string response_str = fut.get();

    if (!response->ParseFromString(response_str)) {
        controller->SetFailed("parse response error");
        return;
    }
}

// 创建新的socket连接
bool KrpcChannel::newConnect(const char *ip, uint16_t port) {
    // 创建socket
    int clientfd = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == clientfd) {
        char errtxt[512] = {0};
        std::cout << "socket error" << strerror_r(errno, errtxt, sizeof(errtxt)) << std::endl;  // 打印错误信息
        LOG(ERROR) << "socket error:" << errtxt;  // 记录错误日志
        return false;
    }

    // 设置服务器地址信息
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;  // IPv4地址族
    server_addr.sin_port = htons(port);  // 端口号
    server_addr.sin_addr.s_addr = inet_addr(ip);  // IP地址

    // 尝试连接服务器
    if (-1 == connect(clientfd, (struct sockaddr *)&server_addr, sizeof(server_addr))) {
        close(clientfd);  // 连接失败，关闭socket
        char errtxt[512] = {0};
        std::cout << "connect error" << strerror_r(errno, errtxt, sizeof(errtxt)) << std::endl;  // 打印错误信息
        LOG(ERROR) << "connect server error" << errtxt;  // 记录错误日志
        return false;
    }

    m_clientfd = clientfd;

    // 建连成功后，启动后台收包线程
    std::thread read_thread(&KrpcChannel::ReadTask, this);
    read_thread.detach(); 

    return true;
}

// 从ZooKeeper查询服务地址
std::string KrpcChannel::QueryServiceHost(ZkClient *zkclient, std::string service_name, std::string method_name, int &idx) {
    std::string method_path = "/" + service_name + "/" + method_name;  // 构造ZooKeeper路径

    std::string host_data_1 = zkclient->GetData(method_path.c_str());  // 从ZooKeeper获取数据

    if (host_data_1 == "") {  // 如果未找到服务地址
        LOG(ERROR) << method_path + " is not exist!";  // 记录错误日志
        return " ";
    }

    idx = host_data_1.find(":");  // 查找IP和端口的分隔符
    if (idx == -1) {  // 如果分隔符不存在
        LOG(ERROR) << method_path + " address is invalid!";  // 记录错误日志
        return " ";
    }

    return host_data_1;  // 返回服务地址
}


// 构造函数，支持延迟连接
KrpcChannel::KrpcChannel(bool connectNow) : m_clientfd(-1), m_idx(0) {
    if (!connectNow) return;

    // 尝试连接服务器，最多重试3次
    auto rt = newConnect(m_ip.c_str(), m_port);
    int count = 3;  // 重试次数
    while (!rt && count--) {
        rt = newConnect(m_ip.c_str(), m_port);
    }

}
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
        std::cerr << "[Warning] 找不到配置文件: " << config_file 
                  << "，将使用默认本地配置 (127.0.0.1)!" << std::endl;
        // 设置默认值
        config_map["rpcserverip"] = "127.0.0.1";
        config_map["rpcserverport"] = "8000";
        config_map["zookeeperip"] = "127.0.0.1";
        config_map["zookeeperport"] = "2181";
        return;
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

// 去掉字符串前后的空格
void Krpcconfig::Trim(std::string &read_buf) {
    // 去掉字符串前面的空格
    int index = read_buf.find_first_not_of(' ');
    if (index != -1) {  // 如果找到非空格字符
        read_buf = read_buf.substr(index, read_buf.size() - index);  // 截取字符串
    }

    // 去掉字符串后面的空格
    index = read_buf.find_last_not_of(' ');
    if (index != -1) {  // 如果找到非空格字符
        read_buf = read_buf.substr(0, index + 1);  // 截取字符串
    }
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
    // service_name为永久节点，method_name为临时节点
    for (auto &sp : service_map) {
        // service_name 在ZooKeeper中的目录是"/"+service_name
        std::string service_path = "/" + sp.first;
        zkclient.Create(service_path.c_str(), nullptr, 0);  // 创建服务节点
        for (auto &mp : sp.second.method_map) {
            std::string method_path = service_path + "/" + mp.first;
            char method_path_data[128] = {0};
            sprintf(method_path_data, "%s:%d", ip.c_str(), port);  // 将IP和端口信息存入节点数据
            // ZOO_EPHEMERAL表示这个节点是临时节点，在客户端断开连接后，ZooKeeper会自动删除这个节点
            zkclient.Create(method_path.c_str(), method_path_data, strlen(method_path_data), ZOO_EPHEMERAL);
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
#include <sys/types.h> // 提供 ssize_t 定义

class ZkClient;

class KrpcChannel : public google::protobuf::RpcChannel
{
public:
    KrpcChannel(bool connectNow);
    virtual ~KrpcChannel()
    {
        if (m_clientfd >= 0) {
            close(m_clientfd);
        }
    }
    void CallMethod(const ::google::protobuf::MethodDescriptor *method,
                    ::google::protobuf::RpcController *controller,
                    const ::google::protobuf::Message *request,
                    ::google::protobuf::Message *response,
                    ::google::protobuf::Closure *done) override; // override可以验证是否是虚函数
private:
    int m_clientfd; // 存放客户端套接字
    std::string service_name;
    std::string m_ip;
    uint16_t m_port;
    std::string method_name;
    int m_idx; // 用来划分服务器ip和port的下标

    bool newConnect(const char *ip, uint16_t port);
    std::string QueryServiceHost(ZkClient *zkclient, std::string service_name, std::string method_name, int &idx);
    ssize_t recv_exact(int fd, char* buf, size_t size);

    // 新增多路复用核心成员
    std::atomic<uint64_t> m_request_id_generator{0};  
    std::mutex m_promise_mutex;                       
    std::unordered_map<uint64_t, std::promise<std::string>> m_pending_requests; 
    
    std::mutex m_send_mutex; // 保护 Socket 发送
    std::mutex m_conn_mutex; // 保护建连过程，防止多线程并发建连

    void ReadTask(); // 后台接收线程
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

#include<zookeeper/zookeeper.h>
#include<string>
#include <mutex>
#include <condition_variable>
#include<semaphore.h>


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
    if (type == ZOO_SESSION_EVENT) {  // 回调消息类型和会话相关的事件
        if (status == ZOO_CONNECTED_STATE) {  // ZooKeeper客户端和服务器连接成功
            // 将 void* 强转回 ZkClient* 指针
            ZkClient *client = static_cast<ZkClient*>(watcherCtx);
            if (client != nullptr) {
                client->NotifyConnected(); // 精准唤醒触发回调的那个对象
            }
        }
    }
    cv.notify_all();  // 通知所有等待的线程
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
```

---

