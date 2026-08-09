#include "../Common/SocketIO.h"
#include "../Common/RDT_GBN.h"
#include <thread>
#include <regex>
#include <cstring>
#include <atomic>

bool parse_pasv_reply(const std::string& reply, std::string& ip, int& port) {
    std::regex rx(R"(\((\d+),(\d+),(\d+),(\d+),(\d+),(\d+)\))");
    std::smatch m;
    if (std::regex_search(reply, m, rx)) {
        ip = m[1].str() + "." + m[2].str() + "." + m[3].str() + "." + m[4].str();
        port = std::stoi(m[5].str()) * 256 + std::stoi(m[6].str());
        return true;
    }
    return false;
}

int main() {
#ifdef _WIN32
    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    socket_t client_tcp = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in srv = { AF_INET, htons(21), 0 };
    inet_pton(AF_INET, "127.0.0.1", &srv.sin_addr);

    if (connect(client_tcp, (sockaddr*)&srv, sizeof(srv)) == SOCKET_ERROR) {
        std::cerr << "Loi: Khong the ket noi den Server!" << std::endl;
        return 1;
    }

    char buf[4096] = { 0 };
    recv(client_tcp, buf, 4095, 0);
    std::cout << "[SERVER] " << buf;

    std::string input, server_ip = "127.0.0.1";
    int pasv_port = 2222;

    std::atomic<bool> abortRequested{false};

    while (true) {
        std::cout << "FTP> ";
        if (!std::getline(std::cin, input) || input.empty()) continue;

        auto args = parse_command_tokens(input);
        if (args.empty()) continue;
        std::string cmd = args[0];

        // Xu ly ngat ngang ABOR
        if (cmd == "ABOR") {
            abortRequested.store(true);
            std::string full_cmd = input + "\r\n";
            send(client_tcp, full_cmd.c_str(), full_cmd.length(), 0);
            continue; 
        }

        std::string full_cmd = input + "\r\n";
        send(client_tcp, full_cmd.c_str(), full_cmd.length(), 0);
        
        memset(buf, 0, sizeof(buf));
        int b = recv(client_tcp, buf, 1023, 0);
        if (b <= 0) break;
        std::cout << "[SERVER] " << buf;

        if (cmd == "PASV") {
            parse_pasv_reply(buf, server_ip, pasv_port);
        }
        else if (cmd == "QUIT") {
            break;
        }
        else if (strncmp(buf, "150", 3) == 0) { 
            // Bat dau truyen du lieu ngam
            std::string filename = (args.size() > 1) ? args[1] : "";
            if (filename.empty() && (cmd == "STOR" || cmd == "APPE" || cmd == "STOU")) {
                std::cout << "Loi: Thieu ten file de upload!" << std::endl;
                continue;
            }

            abortRequested.store(false);

            std::thread([=, &abortRequested]() {
                socket_t udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
                sockaddr_in udp_addr = { AF_INET, htons(pasv_port), 0 };
                inet_pton(AF_INET, server_ip.c_str(), &udp_addr.sin_addr);

                if (cmd == "STOR" || cmd == "APPE" || cmd == "STOU") {
                    rdt_send_file_gbn(udp, udp_addr, filename, &abortRequested);
                } else if (cmd == "RETR") {
                    char dummy[] = "PING"; 
                    sendto(udp, dummy, sizeof(dummy), 0, (sockaddr*)&udp_addr, sizeof(udp_addr));
                    rdt_recv_file_gbn(udp, filename, false, &abortRequested);
                }

                // Nhận thông báo chốt 226/426 từ Server
                char end_buf[1024] = {0};
                recv(client_tcp, end_buf, 1023, 0);
                std::cout << "\n[SERVER] " << end_buf << "FTP> " << std::flush;
                
                CLOSE_SOCKET(udp);
            }).detach();
        }
    }

    CLOSE_SOCKET(client_tcp);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}