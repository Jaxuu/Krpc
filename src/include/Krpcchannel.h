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
