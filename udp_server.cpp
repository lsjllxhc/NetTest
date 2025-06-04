#include <iostream>
#include <fstream>
#include <ctime>
#include <cstring>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>
#include <arpa/inet.h>
#include <unistd.h>

#define DEFAULT_PORT 2345
#define BUF_SIZE 65536
#define MAX_TOTAL_SIZE (1LL102410241024) // 1GB

std::mutex log_mutex;

// 读取端口号
int read_port() {
 return DEFAULT_PORT;
}

// 写日志
void write_log(const std::string& content) {
 std::lock_guard<std::mutex> lock(log_mutex);
 std::ofstream fout("udp.log", std::ios::app);
 time_t now = time(NULL);
 char tstr[64];
 strftime(tstr, sizeof(tstr), "%Y-%m-%d %H:%M:%S", localtime(&now));
 fout << "[" << tstr << "] " << content << std::endl;
}

// 解析测试信息
bool parse_test_info(const char buf, int& size, int& number, std::string& mode) {
 // 格式: TEST size=xxx number=yyy mode=up/down
 std::string s(buf);
 size_t p1 = s.find("size="), p2 = s.find("number="), p3 = s.find("mode=");
 if (p1 == std::string::npos || p2 == std::string::npos || p3 == std::string::npos) return false;
 try {
 size = std::stoi(s.substr(p1+5, s.find(' ', p1+5)-(p1+5)));
 number = std::stoi(s.substr(p2+7, s.find(' ', p2+7)-(p2+7)));
 mode = s.substr(p3+5);
 size_t sp = mode.find(' ');
 if (sp != std::string::npos) mode = mode.substr(0, sp);
 if (mode != "up" && mode != "down") return false;
 } catch (...) {
 return false;
 }
 return true;
}

// 处理每个客户端请求（独立线程）
void client_worker(sockaddr_in cliaddr, int size, int number, std::string mode, int sockfd) {
 char ipstr[INET_ADDRSTRLEN];
 inet_ntop(AF_INET, &cliaddr.sin_addr, ipstr, sizeof(ipstr));
 uint16_t cliport = ntohs(cliaddr.sin_port);
 std::string cip = std::string(ipstr) + ":" + std::to_string(cliport);
// === UPLOAD（客户端上传）===
if (mode == "up") {
    std::vector<double> recv_speeds;
    char buf[BUF_SIZE];
    socklen_t len = sizeof(cliaddr);
    int received = 0;
    auto total_start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < number; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        ssize_t n = recvfrom(sockfd, buf, sizeof(buf), 0, (sockaddr*)&cliaddr, &len);
        auto end = std::chrono::high_resolution_clock::now();
        if (n > 0) {
            ++received;
            double ms = std::chrono::duration<double, std::milli>(end - start).count();
            double speed = n * 8.0 / ms / 1000.0; // Mbps
            recv_speeds.push_back(speed);
            std::cout << "[上传] " << cip << " 包" << received << ": " << ms << "ms, 速度=" << speed << " Mbps" << std::endl;
        }
    }
    auto total_end = std::chrono::high_resolution_clock::now();
    double total_ms = std::chrono::duration<double, std::milli>(total_end - total_start).count();
    double avg_speed = (size * number * 8.0) / total_ms / 1000.0;
    std::cout << "[上传] " << cip << " 总用时: " << total_ms << " ms, 平均速度: " << avg_speed << " Mbps" << std::endl;
    std::string logline = "客户端 " + cip + " 上传测试完成，包大小=" + std::to_string(size) + "，包数=" + std::to_string(number) + 
        "，总用时=" + std::to_string(total_ms) + "ms，平均速度=" + std::to_string(avg_speed) + "Mbps";
    write_log(logline);
}
// === DOWNLOAD（服务端发包）===
else if (mode == "down") {
    std::vector<char> pkt(size, 0);
    std::vector<double> send_speeds;
    auto total_start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < number; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        sendto(sockfd, pkt.data(), size, 0, (sockaddr*)&cliaddr, sizeof(cliaddr));
        auto end = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(end - start).count();
        double speed = size * 8.0 / ms / 1000.0; // Mbps
        send_speeds.push_back(speed);
        std::cout << "[下载] " << cip << " 包" << (i+1) << ": " << ms << "ms, 速度=" << speed << " Mbps" << std::endl;
    }
    auto total_end = std::chrono::high_resolution_clock::now();
    double total_ms = std::chrono::duration<double, std::milli>(total_end - total_start).count();
    double avg_speed = (size * number * 8.0) / total_ms / 1000.0;
    std::cout << "[下载] " << cip << " 总用时: " << total_ms << " ms, 平均速度: " << avg_speed << " Mbps" << std::endl;
    std::string logline = "客户端 " + cip + " 下载测试完成，包大小=" + std::to_string(size) + "，包数=" + std::to_string(number) +
        "，总用时=" + std::to_string(total_ms) + "ms，平均速度=" + std::to_string(avg_speed) + "Mbps";
    write_log(logline);
}
}

int main() {
 int port = read_port();
 std::cout << "[服务端] 监听端口: " << port << std::endl;
int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
if (sockfd < 0) { perror("socket"); return 1; }

sockaddr_in servaddr{}, cliaddr{};
servaddr.sin_family = AF_INET;
servaddr.sin_addr.s_addr = INADDR_ANY;
servaddr.sin_port = htons(port);

if (bind(sockfd, (sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
    perror("bind"); return 2;
}

char buf[BUF_SIZE];
socklen_t len = sizeof(cliaddr);

while (true) {
    ssize_t n = recvfrom(sockfd, buf, BUF_SIZE-1, 0, (sockaddr*)&cliaddr, &len);
    if (n <= 0) continue;
    buf[n] = 0;

    // 识别测试信息
    if (strncmp(buf, "TEST", 4) == 0) {
        int size = 0, number = 0;
        std::string mode;
        if (!parse_test_info(buf, size, number, mode)) {
            std::cerr << "[服务端] 测试信息格式错误: " << buf << std::endl;
            continue;
        }
        char ipstr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &cliaddr.sin_addr, ipstr, sizeof(ipstr));
        uint16_t cliport = ntohs(cliaddr.sin_port);

        std::string logline = "收到测试请求 from " + std::string(ipstr) + ":" + std::to_string(cliport) +
                              " 条件: 包大小=" + std::to_string(size) + "，包数=" + std::to_string(number) + "，模式=" + mode;
        std::cout << "[服务端] " << logline << std::endl;
        write_log(logline);

        // 回复READY
        sendto(sockfd, "READY", 5, 0, (sockaddr*)&cliaddr, len);

        // 启动线程处理
        std::thread(client_worker, cliaddr, size, number, mode, sockfd).detach();
    }
    // 其它包忽略（接收/发送线程自己处理）
}
close(sockfd);
return 0;
}