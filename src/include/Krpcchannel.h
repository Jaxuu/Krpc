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
