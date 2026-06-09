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

