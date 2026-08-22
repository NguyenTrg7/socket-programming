#include "Session.h"
#include "RDT_GBN.h"
#include <thread>
#include <regex>
#include <cstring>
#include <atomic>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <sstream>

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

std::string get_local_ip(socket_t sock) {
    sockaddr_in local_addr{};
    socklen_t addr_len = sizeof(local_addr);
    if (getsockname(sock, (sockaddr*)&local_addr, &addr_len) == 0) {
        char ip_str[INET_ADDRSTRLEN] = { 0 };
        inet_ntop(AF_INET, &(local_addr.sin_addr), ip_str, INET_ADDRSTRLEN);
        std::string res(ip_str);
        if (res != "0.0.0.0") return res;
    }
    return "127.0.0.1";
}

bool handle_port_command(socket_t client_tcp, const std::string& input, std::string& full_cmd_out, int& out_port) {
    auto args = parse_command_tokens(input);
    if (args.empty() || args[0] != "PORT") return false;

    std::string local_ip = get_local_ip(client_tcp);
    int port = 20000;

    if (args.size() == 2) {
        std::string arg = args[1];
        if (std::count(arg.begin(), arg.end(), ',') == 5) {
            full_cmd_out = input + "\r\n";
            std::stringstream ss(arg);
            std::string item;
            std::vector<int> parts;
            while (std::getline(ss, item, ',')) { parts.push_back(std::stoi(item)); }
            if (parts.size() == 6) { out_port = parts[4] * 256 + parts[5]; }
            return true;
        }
        try { port = std::stoi(arg); }
        catch (...) { return false; }
    }
    else if (args.size() >= 3) {
        local_ip = args[1];
        try { port = std::stoi(args[2]); }
        catch (...) { return false; }
    }

    std::string formatted_ip = local_ip;
    std::replace(formatted_ip.begin(), formatted_ip.end(), '.', ',');

    int p1 = port / 256;
    int p2 = port % 256;

    full_cmd_out = "PORT " + formatted_ip + "," + std::to_string(p1) + "," + std::to_string(p2) + "\r\n";
    out_port = port;
    return true;
}

int main() {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "Loi khoi tao Winsock!" << std::endl;
        return 1;
    }

    // Bật hỗ trợ ANSI Escape Codes trên Terminal Windows
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif

    socket_t client_tcp = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in srv = { AF_INET, htons(21), 0 };

    std::string server_ip_input;
    std::cout << "Nhap IP cua Server FTP: ";
    std::cin >> server_ip_input;
    std::cin.ignore();

    inet_pton(AF_INET, server_ip_input.c_str(), &srv.sin_addr);

    if (connect(client_tcp, (sockaddr*)&srv, sizeof(srv)) == SOCKET_ERROR) {
        std::cerr << "Loi: Khong the ket noi den Server!" << std::endl;
        return 1;
    }

    char buf[4096] = { 0 };
    recv(client_tcp, buf, 4095, 0);
    std::cout << "[SERVER] " << buf;

    std::string input, server_ip = server_ip_input;
    int data_port = 2222;
    int client_active_port = 20000;
    bool is_pasv = false;
    std::string current_mode = "S";

    std::atomic<bool> abortRequested{ false };

    while (true) {
        std::cout << "FTP> ";
        if (!std::getline(std::cin, input) || input.empty()) continue;

        auto args = parse_command_tokens(input);
        if (args.empty()) continue;
        std::string cmd = args[0];

        if (cmd == "STOR" || cmd == "APPE" || cmd == "STOU") {
            std::string filename = (args.size() > 1) ? args[1] : "default.dat";
            std::ifstream file_check(filename, std::ios::binary);
            if (!file_check.is_open()) {
                std::cout << "Error: Local file '" << filename << "' is not exist or unreadable !" << std::endl;
                continue;
            }
            file_check.close();
        }

        if (cmd == "ABOR") {
            abortRequested.store(true);
            std::string full_cmd = input + "\r\n";
            send(client_tcp, full_cmd.c_str(), static_cast<int>(full_cmd.length()), 0);

            memset(buf, 0, sizeof(buf));
            int b = recv(client_tcp, buf, 4095, 0);
            if (b > 0) {
                std::cout << "[SERVER] " << buf;
            }
            continue;
        }

        std::string full_cmd = input + "\r\n";

        if (cmd == "PORT") {
            is_pasv = false;
            std::string formatted_cmd;
            if (handle_port_command(client_tcp, input, formatted_cmd, client_active_port)) {
                full_cmd = formatted_cmd;
                std::cout << "[CLIENT] Da tu dong chuyen doi sang chuan RFC: " << full_cmd;
            }
            else {
                std::cout << "[CLIENT] Loi cu phap PORT!\n";
                continue;
            }
        }

        if (cmd == "MODE") {
            if (args.size() > 1) {
                std::string m = args[1];
                std::transform(m.begin(), m.end(), m.begin(), ::toupper);
                if (m == "S" || m == "B" || m == "C") {
                    current_mode = m;
                    std::cout << "[CLIENT] Switch mode request: " << current_mode << "\n";
                }
            }
        }

        send(client_tcp, full_cmd.c_str(), static_cast<int>(full_cmd.length()), 0);

        memset(buf, 0, sizeof(buf));
        int b = recv(client_tcp, buf, 4095, 0);
        if (b <= 0) break;
        std::cout << "[SERVER] " << buf;

        if (cmd == "PASV") {
            is_pasv = true;
            parse_pasv_reply(buf, server_ip, data_port);
        }
        else if (cmd == "QUIT") {
            break;
        }
        else if (strncmp(buf, "150", 3) == 0) {
            std::string filename = (args.size() > 1) ? args[1] : "default.dat";
            abortRequested.store(false);

            std::thread([=, &abortRequested]() {
                socket_t udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
                sockaddr_in udp_dest_addr{};
                udp_dest_addr.sin_family = AF_INET;

                if (cmd == "STOR" || cmd == "APPE" || cmd == "STOU") {
                    if (is_pasv) {
                        udp_dest_addr.sin_port = htons(data_port);
                        inet_pton(AF_INET, server_ip.c_str(), &udp_dest_addr.sin_addr);
                    }
                    else {
                        udp_dest_addr.sin_port = htons(20);
                        inet_pton(AF_INET, server_ip.c_str(), &udp_dest_addr.sin_addr);
                    }
                    rdt_send_file_gbn(udp, udp_dest_addr, filename, &abortRequested);
                }
                else if (cmd == "RETR") {
                    if (is_pasv) {
                        udp_dest_addr.sin_port = htons(data_port);
                        inet_pton(AF_INET, server_ip.c_str(), &udp_dest_addr.sin_addr);
                        char dummy[] = "PING";
                        sendto(udp, dummy, sizeof(dummy), 0, (sockaddr*)&udp_dest_addr, sizeof(udp_dest_addr));
                    }
                    else {
                        sockaddr_in local_addr{};
                        local_addr.sin_family = AF_INET;
                        local_addr.sin_port = htons(client_active_port);
                        local_addr.sin_addr.s_addr = INADDR_ANY;

                        int opt = 1;
                        setsockopt(udp, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
                        bind(udp, (sockaddr*)&local_addr, sizeof(local_addr));
                    }
                    rdt_recv_file_gbn(udp, filename, false, &abortRequested);
                }

                if (!abortRequested.load()) {
                    char end_buf[1024] = { 0 };
                    recv(client_tcp, end_buf, 1023, 0);
                    std::cout << "\n[SERVER] " << end_buf << "FTP> " << std::flush;
                }

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