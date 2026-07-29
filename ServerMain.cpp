#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <ctime>
#include <filesystem> 
#include "rdt_packet.h"
#include "rdt_engine.h"
#include "file_utils.h"
#include "tcp_control.h"

#define TCP_PORT 2121
namespace fs = std::filesystem;

void handle_client(socket_t client_tcp, sockaddr_in client_addr) {
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
    std::cout << "[SERVER] Client " << client_ip << " da ket noi." << std::endl;

    send_tcp_reply(client_tcp, 220, "Hybrid FTP Server Ready.");

    char recv_buf[1024];
    int data_port = 2222;

    bool is_logged_in = false;
    fs::path current_dir = fs::current_path();

    while (true) {
        memset(recv_buf, 0, sizeof(recv_buf));
        int bytes = recv(client_tcp, recv_buf, sizeof(recv_buf) - 1, 0);
        if (bytes <= 0) break;

        std::string cmd, arg;
        parse_ftp_command(recv_buf, cmd, arg);

        // --- NHOM LENH XAC THUC ---
        if (cmd == "USER") {
            send_tcp_reply(client_tcp, 331, "User name okay, need password.");
        }
        else if (cmd == "PASS") {
            if (arg == "admin") {
                is_logged_in = true;
                send_tcp_reply(client_tcp, 230, "User logged in, proceed.");
            }
            else {
                send_tcp_reply(client_tcp, 530, "Not logged in. Incorrect password.");
            }
        }
        else if (!is_logged_in && cmd != "QUIT") {
            send_tcp_reply(client_tcp, 530, "Not logged in. Please authenticate with USER and PASS.");
        }
        // --- NHOM LENH DIEU HUONG & HASH ---
        else if (cmd == "PWD") {
            send_tcp_reply(client_tcp, 257, "\"" + current_dir.string() + "\" is current directory.");
        }
        else if (cmd == "CWD") {
            fs::path new_dir = current_dir / arg;
            if (fs::exists(new_dir) && fs::is_directory(new_dir)) {
                current_dir = fs::canonical(new_dir);
                send_tcp_reply(client_tcp, 250, "Directory successfully changed.");
            }
            else {
                send_tcp_reply(client_tcp, 550, "Failed to change directory.");
            }
        }
        else if (cmd == "LIST") {
            std::string file_list = "Directory listing:\r\n";
            for (const auto& entry : fs::directory_iterator(current_dir)) {
                file_list += entry.path().filename().string();
                if (entry.is_directory()) file_list += " [DIR]\r\n";
                else file_list += " (" + std::to_string(fs::file_size(entry)) + " bytes)\r\n";
            }
            send_tcp_reply(client_tcp, 250, file_list);
        }
        else if (cmd == "HASH") {
            std::string full_path = (current_dir / arg).string();
            std::string hash_val = calculate_file_hash(full_path);
            if (hash_val == "FILE_NOT_FOUND") {
                send_tcp_reply(client_tcp, 550, "File not found.");
            }
            else {
                send_tcp_reply(client_tcp, 213, "Hash value: " + hash_val);
            }
        }
        // --- NHOM LENH DU LIEU (UDP) ---
        else if (cmd == "PASV") {
            data_port = 20000 + (rand() % 10000);
            std::string reply = "Entering Passive Mode (127,0,0,1," +
                std::to_string(data_port / 256) + "," +
                std::to_string(data_port % 256) + ").";
            send_tcp_reply(client_tcp, 227, reply);
        }
        else if (cmd == "STOR") {
            send_tcp_reply(client_tcp, 150, "Opening UDP data connection for STOR.");
            socket_t server_udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            sockaddr_in udp_addr = { AF_INET, htons(data_port), INADDR_ANY };

            if (bind(server_udp, (sockaddr*)&udp_addr, sizeof(udp_addr)) == SOCKET_ERROR) {
                send_tcp_reply(client_tcp, 451, "Data port busy.");
            }
            else {
                std::string full_path = (current_dir / arg).string();
                bool ok = rdt_recv_file(server_udp, full_path);
                if (ok) send_tcp_reply(client_tcp, 226, "Transfer complete.");
                else send_tcp_reply(client_tcp, 451, "Error processing file.");
            }
            CLOSE_SOCKET(server_udp);
        }
        else if (cmd == "RETR") {
            std::string full_path = (current_dir / arg).string();
            if (!fs::exists(full_path) || !fs::is_regular_file(full_path)) {
                send_tcp_reply(client_tcp, 550, "File not found.");
                continue;
            }

            send_tcp_reply(client_tcp, 150, "Opening UDP data connection for RETR.");
            socket_t server_udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            sockaddr_in udp_addr = { AF_INET, htons(data_port), INADDR_ANY };

            if (bind(server_udp, (sockaddr*)&udp_addr, sizeof(udp_addr)) == SOCKET_ERROR) {
                send_tcp_reply(client_tcp, 451, "Data port busy.");
            }
            else {
                char dummy[10];
                sockaddr_in client_udp_addr;
                socklen_t client_len = sizeof(client_udp_addr);
                set_socket_timeout(server_udp, 5000);

                if (recvfrom(server_udp, dummy, sizeof(dummy), 0, (sockaddr*)&client_udp_addr, &client_len) > 0) {
                    bool ok = rdt_send_file(server_udp, client_udp_addr, full_path);
                    if (ok) send_tcp_reply(client_tcp, 226, "Transfer complete.");
                    else send_tcp_reply(client_tcp, 451, "Error transmitting file.");
                }
                else {
                    send_tcp_reply(client_tcp, 425, "Can't open data connection (Client UDP timeout).");
                }
            }
            CLOSE_SOCKET(server_udp);
        }
        else if (cmd == "QUIT") {
            send_tcp_reply(client_tcp, 221, "Goodbye.");
            break;
        }
        else {
            send_tcp_reply(client_tcp, 502, "Command not implemented.");
        }
    }
    CLOSE_SOCKET(client_tcp);
}

int main() {
    srand(static_cast<unsigned int>(time(NULL)));
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup failed!" << std::endl;
        return 1;
    }
#endif
    socket_t server_tcp = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr = { AF_INET, htons(TCP_PORT), INADDR_ANY };

    if (bind(server_tcp, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "Loi Bind! Port 2121 dang bi dung." << std::endl;
        return 1;
    }
    listen(server_tcp, SOMAXCONN);
    std::cout << "Server da san sang tai cong " << TCP_PORT << "..." << std::endl;

    while (true) {
        sockaddr_in c_addr; socklen_t len = sizeof(c_addr);
        socket_t client = accept(server_tcp, (sockaddr*)&c_addr, &len);
        if (client != INVALID_SOCKET) std::thread(handle_client, client, c_addr).detach();
    }
    return 0;
}