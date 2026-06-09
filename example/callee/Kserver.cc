#include <iostream>
#include <string>
#include <unordered_map>
#include <shared_mutex> // C++17 读写锁
#include "../user.pb.h"
#include "Krpcapplication.h"
#include "Krpcprovider.h"

class UserService : public Kuser::UserServiceRpc 
{
private:
    std::unordered_map<std::string, std::string> m_user_db; 
    std::shared_mutex m_db_mutex; // 读写分离锁

public:
    // 1. 本地注册方法 (排他写)
    bool Register(std::string name, std::string pwd, std::string& err_msg) {
        std::unique_lock<std::shared_mutex> lock(m_db_mutex);
        if (m_user_db.find(name) != m_user_db.end()) {
            err_msg = "Register Failed: already exists!";
            return false;
        }
        m_user_db[name] = pwd;
        return true;
    }

    // 2. 本地登录方法 (无界并发读) —— 性能起飞的核心！
    bool Login(std::string name, std::string pwd, std::string& err_msg) {
        std::shared_lock<std::shared_mutex> lock(m_db_mutex);
        auto it = m_user_db.find(name);
        if (it == m_user_db.end()) {
            err_msg = "Login Failed: not found!";
            return false;
        }
        if (it->second != pwd) {
            err_msg = "Login Failed: wrong pwd!";
            return false;
        }
        return true;  
    }

    // 重写 Register RPC 方法
    void Register(::google::protobuf::RpcController* controller,
                  const ::Kuser::RegisterRequest* request,
                  ::Kuser::RegisterResponse* response,
                  ::google::protobuf::Closure* done) override {
        std::string err_msg;
        bool register_result = Register(request->name(), request->pwd(), err_msg); 

        Kuser::ResultCode *code = response->mutable_result();
        code->set_errcode(register_result ? 0 : 1);
        code->set_errmsg(err_msg);
        response->set_success(register_result);
        done->Run();
    }

    // 重写 Login RPC 方法
    void Login(::google::protobuf::RpcController* controller,
               const ::Kuser::LoginRequest* request,
               ::Kuser::LoginResponse* response,
               ::google::protobuf::Closure* done) override {
        std::string err_msg;
        bool login_result = Login(request->name(), request->pwd(), err_msg); 

        Kuser::ResultCode *code = response->mutable_result();
        code->set_errcode(login_result ? 0 : 1);
        code->set_errmsg(err_msg);
        response->set_success(login_result);
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