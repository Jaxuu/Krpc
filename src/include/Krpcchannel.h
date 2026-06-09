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

// 每条TCP连接的独立上下文
struct ConnectionContext {
    int fd = -1;
    std::mutex send_mutex; // 每条连接专属的发送锁！
    std::thread read_thread;
    std::atomic<bool> is_connected{false}; // 标记当前连接是否存活
    // 新增：记录当前这个连接到底连的是谁
    std::string ip;
    uint16_t port = 0;
};

class ZkClient;

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
    std::string m_ip;
    uint16_t m_port;
    std::string method_name;
    int m_idx; // 用来划分服务器ip和port的下标

    bool connect_node(ConnectionContext* ctx, const char *ip, uint16_t port);
    std::string QueryServiceHost(ZkClient *zkclient, std::string service_name, std::string method_name, int &idx);
    ssize_t recv_exact(int fd, char* buf, size_t size);

    // 连接池核心组件
    size_t m_pool_size = 0; //线程池大小，改为运行时动态计算
    std::vector<std::unique_ptr<ConnectionContext>> m_conn_pool;
    std::atomic<uint32_t> m_pool_idx{0}; // 用于 Round-Robin 轮询的计数器

    std::mutex m_conn_mutex; // 保护建连过程
    std::atomic<bool> m_is_pool_inited{false}; // 标记连接池是否已完成初始化

    // 多路复用核心组件
    std::atomic<uint64_t> m_request_id_generator{0};  
    std::mutex m_promise_mutex;                       
    std::unordered_map<uint64_t, std::promise<std::string>> m_pending_requests; 
    
    void ReadTask(int fd); // 后台接收线程
};
#endif
