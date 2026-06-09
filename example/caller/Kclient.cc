#include "Krpcapplication.h"
#include "../user.pb.h"
#include "Krpccontroller.h"
#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>
#include <vector>
#include <algorithm>
#include "KrpcLogger.h"

// 压测函数：交替测试 Login 和 Register
void send_request(Kuser::UserServiceRpc_Stub* stub, int requests_per_thread, std::vector<double>& local_latencies) {
    // 1. 构造 Login 请求
    Kuser::LoginRequest login_request;
    login_request.set_name("benchmark_user");  
    login_request.set_pwd("123456");    
    Kuser::LoginResponse login_response;

    // 2. 构造 Register 请求 
    // (如果你的 RegisterRequest 有不同的字段，请在这里按需修改)
    Kuser::RegisterRequest register_request;
    register_request.set_name("benchmark_new_user");
    register_request.set_pwd("654321");
    // register_request.set_id(1001); 
    Kuser::RegisterResponse register_response;

    for (int i = 0; i < requests_per_thread; ++i) {
        Krpccontroller controller;
        auto req_start = std::chrono::high_resolution_clock::now();
        
        // 核心测试点：单数发 Login，双数发 Register
        bool is_login = (i % 2 == 0);
        
        if (is_login) {
            stub->Login(&controller, &login_request, &login_response, nullptr);
        } else {
            stub->Register(&controller, &register_request, &register_response, nullptr);
        }

        // 失败拦截
        if (controller.Failed()) {
            // 打印前几次错误，方便定位是哪个方法出错了
            if (i < 2) { 
                std::string method_str = is_login ? "Login" : "Register";
                LOG(ERROR) << "RPC Call Failed! Method: " << method_str << ", Reason: " << controller.ErrorText();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue; 
        }

        auto req_end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> req_elapsed = req_end - req_start;
        local_latencies.push_back(req_elapsed.count()); 
    }
}

int main(int argc, char **argv) {
    KrpcApplication::Init(argc, argv);

    // 压测参数
    const int thread_count = 100;      
    const int requests_per_thread = 5000; 

    KrpcChannel* channel = new KrpcChannel(false);
    Kuser::UserServiceRpc_Stub stub(channel);

    std::vector<std::thread> threads;  
    std::vector<std::vector<double>> all_latencies(thread_count);
    for(int i=0; i<thread_count; ++i) {
        all_latencies[i].reserve(requests_per_thread);
    }

    LOG(INFO) << "Starting Benchmark (Testing Multiple Methods)...";
    auto start_time = std::chrono::high_resolution_clock::now();  

    for (int i = 0; i < thread_count; i++) {
        threads.emplace_back([&stub, requests_per_thread, &all_latencies, i]() {  
            send_request(&stub, requests_per_thread, all_latencies[i]);  
        });
    }

    for (auto &t : threads) { t.join(); }
    
    LOG(INFO) << "All threads finished! Calculating results...";
    
    auto end_time = std::chrono::high_resolution_clock::now();  
    std::chrono::duration<double> total_elapsed = end_time - start_time;  

    // 合并并计算 P99
    std::vector<double> merged_latencies;
    merged_latencies.reserve(thread_count * requests_per_thread);
    for (const auto& local_lat : all_latencies) {
        merged_latencies.insert(merged_latencies.end(), local_lat.begin(), local_lat.end());
    }
    std::sort(merged_latencies.begin(), merged_latencies.end());
    
    double p99_latency = merged_latencies[merged_latencies.size() * 0.99];
    double qps = (thread_count * requests_per_thread) / total_elapsed.count();

    LOG(INFO) << "========= Benchmark Results =========";
    LOG(INFO) << "Total Requests : " << thread_count * requests_per_thread;
    LOG(INFO) << "QPS            : " << qps << " req/sec";
    LOG(INFO) << "P99 Latency    : " << p99_latency << " ms";
    LOG(INFO) << "=====================================";

    return 0;
}