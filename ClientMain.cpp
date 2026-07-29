#include <iostream>
#include <string>
#include <regex>
#include "rdt_packet.h"
#include "rdt_engine.h"
#include "file_utils.h"
#include "tcp_control.h"

int parse_pasv_port(const std::string& reply) {
    std::regex rx(R"(\((\d+),(\d+),(\d+),(\d+),(\d+),(\d+)\))");
    std::smatch m;
    if (std::regex_search(reply, m, rx)) return std::stoi(m[5].str()) * 256 + std::stoi(m[6].str());
    return 2222;
}

int main() {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup failed!" << std::endl;
        return 1;
    }
#endif
    socket_t client_tcp = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in srv = { AF_INET, htons(2121) };
    inet_pton(AF_INET, "127.0.0.1", &srv.sin_addr);

    if (connect(client_tcp, (sockaddr*)&srv, sizeof(srv)) == SOCKET_ERROR) {
        std::cerr << "Ket noi that bai!" << std::endl;
        return 1;
    }

    // Mo rong buffer de doc duoc LIST dai
    char buf[4096] = { 0 };
    int bytes = recv(client_tcp, buf, 4095, 0);
    if (bytes > 0) std::cout << "[SERVER] " << buf;

    std::string input;
    while (true) {
        std::cout << "FTP> ";
        std::getline(std::cin, input);
        if (input.empty()) continue;

        std::string cmd, arg;
        parse_ftp_command(input, cmd, arg);

        if (cmd == "STOR" || cmd == "RETR") {
            // 1. Goi PASV truoc khi truyen file
            send(client_tcp, "PASV\r\n", 6, 0);
            memset(buf, 0, sizeof(buf));
            recv(client_tcp, buf, 4095, 0);
            std::cout << "[SERVER] " << buf;
            int port = parse_pasv_port(buf);

            // 2. Gui lenh STOR hoac RETR
            send(client_tcp, (input + "\r\n").c_str(), static_cast<int>(input.length() + 2), 0);
            memset(buf, 0, sizeof(buf));
            recv(client_tcp, buf, 4095, 0);
            std::cout << "[SERVER] " << buf;

            if (std::string(buf).substr(0, 3) == "150") {
                socket_t udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
                sockaddr_in udp_addr = { AF_INET, htons(port) };
                inet_pton(AF_INET, "127.0.0.1", &udp_addr.sin_addr);

                if (cmd == "STOR") {
                    rdt_send_file(udp, udp_addr, arg);
                }
                else if (cmd == "RETR") {
                    sendto(udp, "READY", 5, 0, (sockaddr*)&udp_addr, sizeof(udp_addr));
                    rdt_recv_file(udp, arg);
                }
                CLOSE_SOCKET(udp);

                memset(buf, 0, sizeof(buf));
                recv(client_tcp, buf, 4095, 0);
                std::cout << "[SERVER] " << buf;
            }
        }
        else {
            // Cac lenh don gian khac (USER, LIST, HASH...)
            send(client_tcp, (input + "\r\n").c_str(), static_cast<int>(input.length() + 2), 0);
            memset(buf, 0, sizeof(buf));
            recv(client_tcp, buf, 4095, 0);
            std::cout << "[SERVER] " << buf;
            if (cmd == "QUIT") break;
        }
    }
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}