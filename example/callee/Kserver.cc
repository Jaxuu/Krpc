#include <iostream>
#include <string>
#include <unordered_map>
#include <mutex>
#include "../user.pb.h"
#include "Krpcapplication.h"
#include "Krpcprovider.h"

/*
UserService 提供了真实的本地业务逻辑。
现在加入了 std::unordered_map 模拟数据库，并加入互斥锁保证高并发安全。
*/
class UserService : public Kuser::UserServiceRpc 
{
private:
    std::unordered_map<std::string, std::string> m_user_db; // 模拟数据库，存储用户名和密码
    std::mutex m_db_mutex; // 保护数据库的互斥锁

public:
    // ================= 本地业务逻辑 =================

    // 1. 本地注册方法
    bool Register(std::string name, std::string pwd, std::string& err_msg) {
        std::lock_guard<std::mutex> lock(m_db_mutex);
        // 检测如果注册过，就返回失败
        if (m_user_db.find(name) != m_user_db.end()) {
            err_msg = "Register Failed: User '" + name + "' already exists!";
            std::cout << err_msg << std::endl;
            return false;
        }
        // 没有注册过，添加入库
        m_user_db[name] = pwd;
        std::cout << "doing local service: Register Success! User: " << name << std::endl;
        return true;
    }

    // 2. 本地登录方法
    bool Login(std::string name, std::string pwd, std::string& err_msg) {
        std::lock_guard<std::mutex> lock(m_db_mutex);
        auto it = m_user_db.find(name);
        // 检测如果没有注册，提示先注册
        if (it == m_user_db.end()) {
            err_msg = "Login Failed: User '" + name + "' not found, please register first!";
            std::cout << err_msg << std::endl;
            return false;
        }
        // 检测密码是否正确
        if (it->second != pwd) {
            err_msg = "Login Failed: Incorrect password for user '" + name + "'!";
            std::cout << err_msg << std::endl;
            return false;
        }
        std::cout << "doing local service: Login Success! User: " << name << std::endl;  
        return true;  
    }

    // ================= RPC 接口实现 =================

    // 重写 Register RPC 方法
    void Register(::google::protobuf::RpcController* controller,
                  const ::Kuser::RegisterRequest* request,
                  ::Kuser::RegisterResponse* response,
                  ::google::protobuf::Closure* done) override {
        std::string name = request->name();
        std::string pwd = request->pwd();
        std::string err_msg;

        // 调用本地业务逻辑
        bool register_result = Register(name, pwd, err_msg); 

        // 组装响应
        Kuser::ResultCode *code = response->mutable_result();
        if (register_result) {
            code->set_errcode(0);
            code->set_errmsg("");
        } else {
            code->set_errcode(1);  // 自定义错误码 1 表示业务失败
            code->set_errmsg(err_msg);
        }
        response->set_success(register_result);

        // 回调，通知框架发包
        done->Run();
    }

    // 重写 Login RPC 方法
    void Login(::google::protobuf::RpcController* controller,
               const ::Kuser::LoginRequest* request,
               ::Kuser::LoginResponse* response,
               ::google::protobuf::Closure* done) override {
        std::string name = request->name();
        std::string pwd = request->pwd();
        std::string err_msg;

        // 调用本地业务逻辑
        bool login_result = Login(name, pwd, err_msg); 

        // 组装响应
        Kuser::ResultCode *code = response->mutable_result();
        if (login_result) {
            code->set_errcode(0);
            code->set_errmsg("");
        } else {
            code->set_errcode(1);
            code->set_errmsg(err_msg);
        }
        response->set_success(login_result);

        // 回调，通知框架发包
        done->Run();
    }
};

int main(int argc, char **argv) {
    KrpcApplication::Init(argc, argv);

    KrpcProvider provider;

    UserService my_service;
    provider.NotifyService(&my_service);

    provider.Run();

    return 0;
}