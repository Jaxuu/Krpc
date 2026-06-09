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