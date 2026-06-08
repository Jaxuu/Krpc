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

// 压测函数
void send_request(Kuser::UserServiceRpc_Stub* stub, int requests_per_thread, std::vector<double>& local_latencies) {
    Kuser::LoginRequest request;
    request.set_name("benchmark_user");  
    request.set_pwd("123456");    
    Kuser::LoginResponse response;

    for (int i = 0; i < requests_per_thread; ++i) {
        Krpccontroller controller;
        auto req_start = std::chrono::high_resolution_clock::now();
        
        // 发起 RPC 调用
        stub->Login(&controller, &request, &response, nullptr);

        // 新增验证逻辑：如果调用失败，直接退出或跳过，千万别算进成功耗时里！
        if (controller.Failed()) {
            // 只打印一次错误，防止日志刷屏卡死终端
            if (i == 0) { 
                LOG(ERROR) << "RPC Call Failed! Reason: " << controller.ErrorText();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10)); // 失败后稍微让出CPU，避免疯狂重试
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

    // 共享 Channel，测试多路复用
    KrpcChannel* channel = new KrpcChannel(false); 
    Kuser::UserServiceRpc_Stub stub(channel);

    std::vector<std::thread> threads;  
    std::vector<std::vector<double>> all_latencies(thread_count);
    for(int i=0; i<thread_count; ++i) {
        all_latencies[i].reserve(requests_per_thread);
    }

    LOG(INFO) << "Starting Benchmark...";
    auto start_time = std::chrono::high_resolution_clock::now();  

    for (int i = 0; i < thread_count; i++) {
        threads.emplace_back([&stub, requests_per_thread, &all_latencies, i]() {  
            send_request(&stub, requests_per_thread, all_latencies[i]);  
        });
    }

    for (auto &t : threads) { t.join(); }
    // 【添加这行】确保程序真的执行到了这里
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

    // 极限精简输出
    LOG(INFO) << "========= Benchmark Results =========";
    LOG(INFO) << "Total Requests : " << thread_count * requests_per_thread;
    LOG(INFO) << "QPS            : " << qps << " req/sec";
    LOG(INFO) << "P99 Latency    : " << p99_latency << " ms";
    LOG(INFO) << "=====================================";

    return 0;
}