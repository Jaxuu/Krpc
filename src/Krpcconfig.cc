#include "Krpcconfig.h"
#include <iostream>
#include <fstream>
#include <memory>

// 加载配置文件，解析配置文件中的键值对
void Krpcconfig::LoadConfigFile(const char *config_file) {
    FILE *pf = fopen(config_file, "r");
    if (pf == nullptr) {
        // 不要兜底！直接报 Fatal 错误并退出进程！
        std::cerr << "[FATAL ERROR] 找不到或无法打开配置文件: " 
                  << (config_file ? config_file : "null") 
                  << " \n请检查启动参数路径是否正确！(例如: ./server -i ../conf/test.conf)" << std::endl;
        exit(EXIT_FAILURE); 
    }

    // 使用智能指针管理文件指针，确保文件在退出时自动fclose
    std::unique_ptr<FILE, decltype(&fclose)> pf_ptr(pf, &fclose);

    char buf[1024];  // 用于存储从文件中读取的每一行内容
    
    // 使用pf.get()方法获取原始指针，逐行读取文件内容
    while (fgets(buf, 1024, pf_ptr.get()) != nullptr) {
        std::string read_buf(buf);  // 将读取的内容转换为字符串
        Trim(read_buf);  // 去掉字符串前后的空格

        // 忽略注释行（以#开头）和空行
        if (read_buf[0] == '#' || read_buf.empty()) continue;

        // 查找键值对的分隔符'='
        int index = read_buf.find('=');
        if (index == -1) continue;  // 如果没有找到'='，跳过该行

        // 提取键（key）
        std::string key = read_buf.substr(0, index);
        Trim(key);  // 去掉key前后的空格

        std::string value = read_buf.substr(index + 1); 
        Trim(value);

        // 将键值对存入配置map中
        config_map.insert({key, value});
    }
}

// 根据key查找对应的value
std::string Krpcconfig::Load(const std::string &key) {
    // 优先读取环境变量 (比如 RPC_SERVER_IP)
    char* env_val = std::getenv(key.c_str());
    if (env_val != nullptr) return std::string(env_val);

    // 再读配置文件
    auto it = config_map.find(key);
    return (it != config_map.end()) ? it->second : "";
}

// 去掉字符串前后所有的空白字符（包括空格、制表符、\r、\n）
void Krpcconfig::Trim(std::string &read_buf) {
    // 找到第一个不是空白字符的位置
    int start = read_buf.find_first_not_of(" \t\r\n");
    if (start == -1) {
        read_buf = ""; // 全是空白字符
        return;
    }
    
    // 找到最后一个不是空白字符的位置
    int end = read_buf.find_last_not_of(" \t\r\n");
    
    // 精准截取有效内容
    read_buf = read_buf.substr(start, end - start + 1);
}