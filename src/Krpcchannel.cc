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

    // 1. 双检锁初始化 ZK 客户端与第一波连接
    if (!m_is_pool_inited) {  
        std::lock_guard<std::mutex> lock(m_init_mutex); // 专门的初始化锁
        if (!m_is_pool_inited) {
            const google::protobuf::ServiceDescriptor *sd = method->service();
            service_name = sd->name();  
            method_name = method->name();  
            m_method_path = "/" + service_name + "/" + method_name;

            m_zkCli.Start(); // 启动持久化的 ZK 客户端
            RefreshConnections(); // 首次拉取并挂载监听器

            m_is_pool_inited = true; 
        }
    }

    // 2. 获取一条连接（使用 shared_ptr 拷贝，保护生命周期）
    std::shared_ptr<ConnectionContext> ctx;
    {
        std::lock_guard<std::mutex> lock(m_route_mutex);
        if (m_hash_ring.empty()) {
            controller->SetFailed("Fatal Error: Cluster is down. No active servers!");
            return;
        }

        // 把 request_id 转换成字符串作为路由 Key（在实际业务中，你可以让上层传递 userId）
        std::string routing_key = std::to_string(current_id);
        uint32_t hash_val = std::hash<std::string>{}(routing_key);

        // 算法核心：在红黑树中寻找第一个 > hash_val 的节点，相当于在环上顺时针找！
        auto it = m_hash_ring.upper_bound(hash_val);
        
        // 如果到了树的末尾，说明越过了 2^32，由于是环，我们回到起点
        if (it == m_hash_ring.end()) {
            it = m_hash_ring.begin();
        }
        
        ctx = it->second;
    }

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

    // 3. 序列化请求头与包体
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

    std::string send_rpc_str;
    send_rpc_str.reserve(4 + 4 + header_size + args_str.size());
    send_rpc_str.append((char*)&net_total_len, 4);
    send_rpc_str.append((char*)&net_header_len, 4);
    send_rpc_str.append(rpc_header_str);
    send_rpc_str.append(args_str);

    // 4. 在全局表中注册 promise
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

    // 5. 发送数据：使用专属连接的 send_mutex 和 fd！并发度大幅提升
    {
        std::lock_guard<std::mutex> lock(ctx->send_mutex);
        // 关键修改:将最后一个参数 0 改为 MSG_NOSIGNAL
        if (-1 == send(ctx->fd, send_rpc_str.c_str(), send_rpc_str.size(), MSG_NOSIGNAL)) {
            close(ctx->fd);
            ctx->fd = -1;
            ctx->is_connected = false; // 标记断开，下次用到会重连
            controller->SetFailed("send error");

            std::lock_guard<std::mutex> bucket_lock(bucket->mutex);
            bucket->pending_requests.erase(current_id);
            return;
        }
    }

    // 6. 等待各自专属的 ReadTask 唤醒
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
        channel->RefreshConnections(); // 重新拉取并建立连接池
    }
}

// 核心的“无锁化”路由表热替换逻辑
void KrpcChannel::RefreshConnections() {
    // 1. 获取最新存活的机器列表，并且重新挂载 Watcher，因为 ZK 的 Watcher 是一次性的！
    std::vector<std::string> hosts = m_zkCli.GetChildrenWithWatch(m_method_path.c_str(), &KrpcChannel::OnNodeChange, this);

    // 构建一个崭新的哈希环
    std::map<uint32_t, std::shared_ptr<ConnectionContext>> new_hash_ring;
    std::hash<std::string> hash_fn; // C++11 标准字符串哈希函数

    size_t active_physical_connections = 0;
    
    for (const auto& host : hosts) {
        int idx = host.find(":");
        if (idx != -1) {
            std::string node_ip = host.substr(0, idx);
            uint16_t node_port = atoi(host.substr(idx + 1).c_str());
            
            // 为每台存活的机器建立 4 条新连接
            for (int i = 0; i < 4; ++i) { 
                auto ctx = std::make_shared<ConnectionContext>();
                ctx->ip = node_ip;
                ctx->port = node_port;
                if (connect_node(ctx.get(), node_ip.c_str(), node_port)) {
                    active_physical_connections++;

                    // 为这一条物理连接，生成 150 个虚拟节点挂到环上！
                    for (int v = 0; v < VIRTUAL_NODES; ++v) {
                        // 构造虚拟节点的名字，例如: "127.0.0.1:8000_conn0_VN45"
                        std::string vnode_name = node_ip + ":" + std::to_string(node_port) 
                                                 + "_conn" + std::to_string(i) 
                                                 + "_VN" + std::to_string(v);
                        
                        // 计算它在环上的位置 0 ~ 2^32
                        uint32_t hash_val = hash_fn(vnode_name);
                        
                        // 将虚拟节点放入红黑树中，指向真实的 ctx
                        new_hash_ring[hash_val] = ctx; 
                    }
                }
            }
        }
    }

    // 2. 瞬间热替换全局哈希环！
    {
        std::lock_guard<std::mutex> lock(m_route_mutex);
        m_hash_ring = new_hash_ring; 
        m_pool_size = active_physical_connections;
    }

    LOG(INFO) << "✅ 路由表重建完成！当前活跃物理连接: " << m_pool_size 
              << "，哈希环虚拟节点总数: " << new_hash_ring.size();
}

KrpcChannel::KrpcChannel(bool connectNow) {// 预先分配好 16 个带有独立锁的哈希表桶
    for (size_t i = 0; i < BUCKET_NUM; ++i) {
        m_promise_buckets.push_back(std::unique_ptr<PromiseBucket>(new PromiseBucket()));
    }
}

KrpcChannel::~KrpcChannel() {}