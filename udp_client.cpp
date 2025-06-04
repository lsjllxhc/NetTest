#include <iostream>
#include <cstring>
#include <string>
#include <vector>
#include <chrono>
#include <arpa/inet.h>
#include <unistd.h>

#define DEFAULT_SIZE   (50*1024*1024) // 50MB
#define DEFAULT_NUMBER 1
#define DEFAULT_PORT   2345
#define MAX_TOTAL_SIZE (1LL*1024*1024*1024) // 1GB

void print_usage(const std::vector<std::string>& argv, int err_pos = -1, const std::string& msg = "") {
    if (!msg.empty()) std::cout << "参数错误: " << msg << std::endl;
    std::cout << "用法: ./udp_client [ip] -size [包大小] -number [包数量] -port [端口] -up|-down" << std::endl;
    if (err_pos >= 0) {
        std::string cmdline;
        for (const auto& s : argv) cmdline += s + " ";
        std::cout << "  " << cmdline << std::endl << "  ";
        int pos = 0;
        for (int i = 0; i < err_pos; ++i) pos += argv[i].size() + 1;
        for (int i = 0; i < pos; ++i) std::cout << " ";
        for (size_t i = 0; i < argv[err_pos].size(); ++i) std::cout << "^";
        std::cout << std::endl;
    }
}

bool is_number(const std::string& s) {
    for (char c : s) if (c < '0' || c > '9') return false;
    return !s.empty();
}

int main(int argc, char* argv[]) {
    std::vector<std::string> args(argv, argv + argc);
    if (argc < 3) { print_usage(args, -1, "参数数量不足"); return 1; }
    std::string ip = argv[1];
    int size = DEFAULT_SIZE, number = DEFAULT_NUMBER, port = DEFAULT_PORT;
    bool up = false, down = false;

    for (int i = 2; i < argc; ++i) {
        if (args[i] == "-size") {
            if (i+1 >= argc || !is_number(args[i+1])) {
                print_usage(args, i, "无效的包大小参数");
                return 1;
            }
            size = atoi(args[++i].c_str());
        } else if (args[i] == "-number") {
            if (i+1 >= argc || !is_number(args[i+1])) {
                print_usage(args, i, "无效的包数量参数");
                return 1;
            }
            number = atoi(args[++i].c_str());
        } else if (args[i] == "-port") {
            if (i+1 >= argc || !is_number(args[i+1])) {
                print_usage(args, i, "无效的端口参数");
                return 1;
            }
            port = atoi(args[++i].c_str());
        } else if (args[i] == "-up") {
            up = true;
        } else if (args[i] == "-down") {
            down = true;
        } else {
            print_usage(args, i, "无效参数 " + args[i]);
            return 1;
        }
    }

    if (up == down) {
        print_usage(args, -1, "必须且只能指定-up或-down");
        return 1;
    }
    if (size <= 0 || number <= 0) {
        print_usage(args, -1, "包大小和数量必须大于0");
        return 1;
    }
    long long total_size = 1LL * size * number;
    if (total_size > MAX_TOTAL_SIZE) {
        int err_pos = -1;
        for (int i = 2; i < argc; ++i)
            if (args[i] == "-size" || args[i] == "-number") { err_pos = i+1; break; }
        print_usage(args, err_pos, "包总大小过大");
        return 1;
    }

    std::cout << "[客户端] 启动参数: ip=" << ip << ", size=" << size << ", number=" << number << ", port=" << port << ", mode=" << (up ? "up" : "down") << std::endl;

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) { perror("socket"); return 2; }

    sockaddr_in servaddr{};
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &servaddr.sin_addr) != 1) {
        std::cerr << "IP地址无效: " << ip << std::endl;
        return 1;
    }

    char info[128];
    snprintf(info, sizeof(info), "TEST size=%d number=%d mode=%s", size, number, up ? "up" : "down");
    sendto(sockfd, info, strlen(info), 0, (sockaddr*)&servaddr, sizeof(servaddr));
    std::cout << "[客户端] 已发送测试信息: " << info << std::endl;

    // 等待服务器READY
    fd_set fds;
    FD_ZERO(&fds); FD_SET(sockfd, &fds);
    timeval tv; tv.tv_sec = 10; tv.tv_usec = 0;
    char buf[128];
    socklen_t slen = sizeof(servaddr);

    int ret = select(sockfd+1, &fds, NULL, NULL, &tv);
    if (ret > 0) {
        ssize_t n = recvfrom(sockfd, buf, sizeof(buf)-1, 0, (sockaddr*)&servaddr, &slen);
        buf[n] = 0;
        if (strncmp(buf, "READY", 5) == 0) {
            std::cout << "[客户端] 服务器已准备好，开始测试..." << std::endl;
            if (up) {
                std::vector<char> pkt(size, 0);
                std::vector<double> each_speed;
                auto total_start = std::chrono::high_resolution_clock::now();
                for (int i = 0; i < number; ++i) {
                    auto start = std::chrono::high_resolution_clock::now();
                    sendto(sockfd, pkt.data(), size, 0, (sockaddr*)&servaddr, sizeof(servaddr));
                    auto end = std::chrono::high_resolution_clock::now();
                    double ms = std::chrono::duration<double, std::milli>(end - start).count();
                    double speed = size * 8.0 / ms / 1000.0; // Mbps
                    each_speed.push_back(speed);
                    std::cout << "[上传] 包" << (i+1) << ": " << ms << "ms, 速度=" << speed << " Mbps" << std::endl;
                }
                auto total_end = std::chrono::high_resolution_clock::now();
                double total_ms = std::chrono::duration<double, std::milli>(total_end - total_start).count();
                double avg_speed = (size * number * 8.0) / total_ms / 1000.0;
                std::cout << "[上传] 总用时: " << total_ms << " ms, 平均速度: " << avg_speed << " Mbps" << std::endl;
            } else if (down) {
                std::vector<double> recv_speeds;
                int recv_cnt = 0, total_bytes = 0;
                auto total_start = std::chrono::high_resolution_clock::now();
                for (int i = 0; i < number; ++i) {
                    auto start = std::chrono::high_resolution_clock::now();
                    ssize_t n = recvfrom(sockfd, buf, sizeof(buf), 0, (sockaddr*)&servaddr, &slen);
                    auto end = std::chrono::high_resolution_clock::now();
                    if (n > 0) {
                        ++recv_cnt;
                        total_bytes += n;
                        double ms = std::chrono::duration<double, std::milli>(end - start).count();
                        double speed = n * 8.0 / ms / 1000.0; // Mbps
                        recv_speeds.push_back(speed);
                        std::cout << "[下载] 包" << recv_cnt << ": " << ms << "ms, 速度=" << speed << " Mbps" << std::endl;
                    }
                }
                auto total_end = std::chrono::high_resolution_clock::now();
                double total_ms = std::chrono::duration<double, std::milli>(total_end - total_start).count();
                double avg_speed = (total_bytes * 8.0) / total_ms / 1000.0;
                std::cout << "[下载] 总用时: " << total_ms << " ms, 平均速度: " << avg_speed << " Mbps" << std::endl;
            }
        } else {
            std::cout << "[客户端] 收到未知回复: " << buf << std::endl;
        }
    } else {
        std::cout << "[客户端] 10秒内未收到服务器响应，连接失败。\n";
    }
    close(sockfd);
    return 0;
}