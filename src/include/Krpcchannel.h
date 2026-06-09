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
#include <sys/types.h> // 提供 ssize_t 定义
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
    std::string service_name;
    std::string method_name;

    // --- 集群容灾核心组件 ---
    ZkClient m_zkCli;                   // 持久化 ZK 客户端，保持与注册中心的 Session
    std::string m_method_path;          // 需要持续监听的服务路径
    std::mutex m_route_mutex;           // 保护路由表，防止读写冲突
    std::mutex m_init_mutex;            // 专门保护双检锁初始化的锁！

    // 连接池核心组件
    size_t m_pool_size = 0; //线程池大小，改为运行时动态计算
    std::vector<std::shared_ptr<ConnectionContext>> m_conn_pool;    // 改为 shared_ptr。确保正在发送请求的线程拿到副本后，即使后台清空了旧池子，旧连接也能安全存活到发包结束！
    std::atomic<uint32_t> m_pool_idx{0}; // 用于 Round-Robin 轮询的计数器
    std::atomic<bool> m_is_pool_inited{false}; // 标记连接池是否已完成初始化

    // 多路复用、段锁核心组件
    std::atomic<uint64_t> m_request_id_generator{0}; 
    static const size_t BUCKET_NUM = 16;   // 替代原来的全局锁和单表，定义 16 个桶
    std::vector<std::unique_ptr<PromiseBucket>> m_promise_buckets;
    
    
    bool connect_node(ConnectionContext* ctx, const char *ip, uint16_t port);
    ssize_t recv_exact(int fd, char* buf, size_t size);

    // 动态刷新函数
    void RefreshConnections(); // 执行路由表重建
    static void OnNodeChange(zhandle_t *zh, int type, int status, const char *path, void *watcherCtx); // ZK 回调函数

    void ReadTask(int fd); // 后台接收线程
};
#endif
