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