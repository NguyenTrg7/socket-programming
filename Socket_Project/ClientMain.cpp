#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
#include <string>
#include <regex>
#include <cstring>
#include <thread>

#include "rdt_packet.h"
#include "rdt_engine.h"
#include "file_utils.h"
#include "tcp_control.h"

// Hàm trích xuất Port từ chuỗi phản hồi 227 của PASV
int parse_pasv_port(const std::string& reply) {
    std::regex rx(R"(\((\d+),(\d+),(\d+),(\d+),(\d+),(\d+)\))");
    std::smatch m;
    if (std::regex_search(reply, m, rx)) {
        return std::stoi(m[5].str()) * 256 + std::stoi(m[6].str());
    }
    return 2222; // Mặc định nếu không parse được
}

int main() {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    socket_t client_tcp = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in srv{};
    srv.sin_family = AF_INET;
    srv.sin_port = htons(21); // Kết nối Control Channel qua Port 21
    inet_pton(AF_INET, "127.0.0.1", &srv.sin_addr);

    if (connect(client_tcp, (sockaddr*)&srv, sizeof(srv)) == SOCKET_ERROR) {
        std::cerr << "Ket noi Control Channel (Port 21) that bai!" << std::endl;
        return 1;
    }

    // Đọc thông báo chào 220 từ Server
    char greeting_buf[1024] = { 0 };
    recv(client_tcp, greeting_buf, 1023, 0);
    std::cout << "[SERVER] " << greeting_buf;

    std::string input;
    int current_pasv_port = 2222;
    std::string server_ip = "127.0.0.1";

    while (true) {
        std::cout << "FTP> ";
        if (!std::getline(std::cin, input) || input.empty()) continue;

        std::string cmd, arg;
        parse_ftp_command(input, cmd, arg);

        // ====================================================================
        // 1. LỆNH PASV: Cập nhật Port UDP Data do Server cấp
        // ====================================================================
        if (cmd == "PASV") {
            std::string msg = "PASV\r\n";
            send(client_tcp, msg.c_str(), static_cast<int>(msg.length()), 0);

            char buf[1024] = { 0 };
            recv(client_tcp, buf, 1023, 0);
            std::cout << "[SERVER] " << buf;

            current_pasv_port = parse_pasv_port(buf);
        }
        // ====================================================================
        // 2. NHÓM UPLOAD DỮ LIỆU UDP: STOR, APPE, STOU
        // ====================================================================
        else if (cmd == "STOR" || cmd == "APPE" || cmd == "STOU") {
            std::string local_filename = arg;
            if (local_filename.empty()) {
                std::cout << "Nhap ten file local can upload: ";
                std::getline(std::cin, local_filename);
            }

            // Gửi lệnh Control sang TCP
            std::string full_cmd = input + "\r\n";
            send(client_tcp, full_cmd.c_str(), static_cast<int>(full_cmd.length()), 0);

            char buf[1024] = { 0 };
            int b = recv(client_tcp, buf, 1023, 0);
            if (b <= 0) continue;
            std::cout << "[SERVER] " << buf;

            // Nếu Server xác nhận mã 150 (Chấp nhận mở Data Channel)
            if (strncmp(buf, "150", 3) == 0) {
                socket_t udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
                sockaddr_in udp_addr{};
                udp_addr.sin_family = AF_INET;
                udp_addr.sin_port = htons(current_pasv_port);
                inet_pton(AF_INET, server_ip.c_str(), &udp_addr.sin_addr);

                std::cout << "[CLIENT] Dang truyen du lieu qua UDP..." << std::endl;
                rdt_send_file(udp, udp_addr, local_filename);
                CLOSE_SOCKET(udp);

                // Nhận thông báo hoàn tất (226) từ TCP
                memset(buf, 0, sizeof(buf));
                recv(client_tcp, buf, 1023, 0);
                std::cout << "[SERVER] " << buf;
            }
        }
        // ====================================================================
        // 3. NHÓM DOWNLOAD DỮ LIỆU UDP: RETR
        // ====================================================================
        else if (cmd == "RETR") {
            if (arg.empty()) {
                std::cout << "Loi: RETR can truyen ten file muon tai!" << std::endl;
                continue;
            }

            // Gửi lệnh Control sang TCP
            std::string full_cmd = input + "\r\n";
            send(client_tcp, full_cmd.c_str(), static_cast<int>(full_cmd.length()), 0);

            char buf[1024] = { 0 };
            int b = recv(client_tcp, buf, 1023, 0);
            if (b <= 0) continue;
            std::cout << "[SERVER] " << buf;

            if (strncmp(buf, "150", 3) == 0) {
                socket_t udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
                sockaddr_in udp_addr{};
                udp_addr.sin_family = AF_INET;
                udp_addr.sin_port = htons(current_pasv_port);
                inet_pton(AF_INET, server_ip.c_str(), &udp_addr.sin_addr);

                // Gửi gói UDP "PING" mồi để Server PASV biết IP/Port UDP của Client
                char dummy[] = "PING";
                sendto(udp, dummy, sizeof(dummy), 0, (sockaddr*)&udp_addr, sizeof(udp_addr));

                std::cout << "[CLIENT] Dang nhan du lieu qua UDP..." << std::endl;
                rdt_recv_file(udp, arg);
                CLOSE_SOCKET(udp);

                // Nhận thông báo hoàn tất (226) từ TCP
                memset(buf, 0, sizeof(buf));
                recv(client_tcp, buf, 1023, 0);
                std::cout << "[SERVER] " << buf;
            }
        }
        // ====================================================================
        // 4. TẤT CẢ CÁC LỆNH KÊNH CONTROL TCP (HASH, NOOP, PWD, CWD, DELE, v.v.)
        // ====================================================================
        else {
            std::string full_cmd = input + "\r\n";
            send(client_tcp, full_cmd.c_str(), static_cast<int>(full_cmd.length()), 0);

            char buf[1024] = { 0 };
            int b = recv(client_tcp, buf, 1023, 0);
            if (b > 0) {
                std::cout << "[SERVER] " << buf;
            }

            if (cmd == "QUIT") break;
        }
    }

    CLOSE_SOCKET(client_tcp);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}