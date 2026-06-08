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
        std::lock_guard<std::mutex> lock(m_promise_mutex);
        auto it = m_pending_requests.find(req_id);
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
    // 1. 双检锁初始化整个连接池
    if (!m_is_pool_inited) {  
        std::lock_guard<std::mutex> lock(m_conn_mutex);
        if (!m_is_pool_inited) {
            const google::protobuf::ServiceDescriptor *sd = method->service();
            service_name = sd->name();  
            method_name = method->name();  

            ZkClient zkCli;
            zkCli.Start();  
            std::string host_data = QueryServiceHost(&zkCli, service_name, method_name, m_idx);  
            m_ip = host_data.substr(0, m_idx);  
            m_port = atoi(host_data.substr(m_idx + 1, host_data.size() - m_idx).c_str());  

            // 为池中的 4 条连接全部建连
            for (int i = 0; i < m_pool_size; ++i) {
                if (!connect_node(m_conn_pool[i].get(), m_ip.c_str(), m_port)) {
                    LOG(ERROR) << "init connection pool failed for node " << i;
                }
            }
            m_is_pool_inited = true; // 标记池初始化完成
        }
    }

    // 2. 轮询（Round-Robin）选择一条连接
    uint32_t idx = m_pool_idx.fetch_add(1) % m_pool_size;
    ConnectionContext* ctx = m_conn_pool[idx].get();

    // 容错机制：如果选中的连接断开了，尝试重连
    if (!ctx->is_connected) {
        std::lock_guard<std::mutex> lock(ctx->send_mutex);
        if (!ctx->is_connected) {
            if (!connect_node(ctx, m_ip.c_str(), m_port)) {
                controller->SetFailed("selected connection is offline and reconnect failed");
                return;
            }
        }
    }

    // 3. 序列化请求头与包体
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

    // 4. 在全局表中注册 promise
    std::promise<std::string> prom;
    std::future<std::string> fut = prom.get_future();
    {
        std::lock_guard<std::mutex> lock(m_promise_mutex);
        m_pending_requests[current_id] = std::move(prom);
    }

    // 5. 发送数据：使用专属连接的 send_mutex 和 fd！并发度大幅提升
    {
        std::lock_guard<std::mutex> lock(ctx->send_mutex);
        if (-1 == send(ctx->fd, send_rpc_str.c_str(), send_rpc_str.size(), 0)) {
            close(ctx->fd);
            ctx->fd = -1;
            ctx->is_connected = false; // 标记断开，下次用到会重连
            controller->SetFailed("send error");
            std::lock_guard<std::mutex> plock(m_promise_mutex);
            m_pending_requests.erase(current_id);
            return;
        }
    }

    // 6. 等待各自专属的 ReadTask 唤醒
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


// 构造函数，支持延迟连接,
KrpcChannel::KrpcChannel(bool connectNow) : m_idx(0) {
    for (int i = 0; i < m_pool_size; ++i) {
        m_conn_pool.push_back(std::unique_ptr<ConnectionContext>(new ConnectionContext()));
    }
}

// 析构函数：安全释放连接池中的所有 fd
KrpcChannel::~KrpcChannel() {
    for (auto& ctx : m_conn_pool) {
        if (ctx->fd >= 0) {
            close(ctx->fd);
            ctx->fd = -1;
        }
    }
}