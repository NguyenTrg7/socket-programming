#define _CRT_SECURE_NO_WARNINGS
#include "CommandRouter.h"
#include <thread>
#include <cstring>
#include <ctime>
#include <vector>
#include <mutex>
#include <algorithm>
#include <iomanip>

#define TCP_PORT 21

// Danh sách quản lý các Session đang hoạt động & Mutex đồng bộ đa luồng
std::vector<ServerSession*> active_sessions;
std::mutex registry_mutex;

// Hàm định dạng và in Bảng các Client đang kết nối
void print_client_table() {
    std::lock_guard<std::mutex> lock(registry_mutex);
    std::cout << "\n=================================== CLIENT SERVER TABLE ===================================\n";
    std::cout << std::left
        << std::setw(10) << "Socket FD"
        << std::setw(18) << "IP Address"
        << std::setw(10) << "TCP Port"
        << std::setw(16) << "User"
        << std::setw(10) << "Mode"
        << std::setw(20) << "Connect Time" << "\n";
    std::cout << "-------------------------------------------------------------------------------------------\n";

    if (active_sessions.empty()) {
        std::cout << "                          [ No active clients connected ]                          \n";
    }
    else {
        for (const auto* s : active_sessions) {
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(s->client_addr.sin_addr), ip_str, INET_ADDRSTRLEN);
            int port = ntohs(s->client_addr.sin_port);

            char time_buf[20];
            std::tm* tm_info = std::localtime(&(s->connect_time));
            std::strftime(time_buf, sizeof(time_buf), "%H:%M:%S %Y-%m-%d", tm_info);

            std::cout << std::left
                << std::setw(10) << s->tcp_sock
                << std::setw(18) << ip_str
                << std::setw(10) << port
                << std::setw(16) << (s->is_logged_in ? s->username : "Not Logged In")
                << std::setw(10) << (s->is_pasv_mode ? "PASV" : "PORT")
                << std::setw(20) << time_buf << "\n";
        }
    }
    std::cout << "===========================================================================================\n\n";
}

void handle_client(socket_t client_tcp, sockaddr_in client_addr) {
    // Cấp phát con trỏ Session để dễ dàng đưa vào danh sách theo dõi toàn cục
    ServerSession* session = new ServerSession();
    session->tcp_sock = client_tcp;
    session->client_addr = client_addr;
    session->current_dir = std::filesystem::current_path();
    session->connect_time = std::time(nullptr);

    // Đăng ký Session mới vào danh sách
    {
        std::lock_guard<std::mutex> lock(registry_mutex);
        active_sessions.push_back(session);
    }

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
    std::cout << "[SERVER] Client " << client_ip << " da ket noi." << std::endl;

    // In bảng ngay khi có client mới kết nối
    print_client_table();

    send_tcp_reply(client_tcp, 220, "Hybrid FTP Server Ready (Port 21).", &session->tcp_mutex);

    char recv_buf[1024];
    while (!session->quit_requested) {
        memset(recv_buf, 0, sizeof(recv_buf));
        int bytes = recv(client_tcp, recv_buf, sizeof(recv_buf) - 1, 0);
        if (bytes <= 0) break;

        std::cout << "[Client " << client_ip << " sent]: " << recv_buf;
        auto args = parse_command_tokens(recv_buf);
        routeCommand(args, *session);
    }

    // Gỡ Session khỏi danh sách khi Client ngắt kết nối
    {
        std::lock_guard<std::mutex> lock(registry_mutex);
        active_sessions.erase(
            std::remove(active_sessions.begin(), active_sessions.end(), session),
            active_sessions.end()
        );
    }

    std::cout << "[SERVER] Client " << client_ip << " da ngat ket noi." << std::endl;

    // In lại bảng sau khi Client ngắt kết nối
    print_client_table();

    CLOSE_SOCKET(client_tcp);
    delete session; // Giải phóng bộ nhớ
}

int main() {
    srand(static_cast<unsigned int>(time(NULL)));
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;
#endif

    socket_t server_tcp = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr = { AF_INET, htons(TCP_PORT), INADDR_ANY };

    if (bind(server_tcp, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "Loi Bind! Hay dam bao chay voi quyen Administrator/Root de mo Port 21." << std::endl;
        return 1;
    }
    listen(server_tcp, SOMAXCONN);
    std::cout << "Server FTP da san sang tai Port " << TCP_PORT << " (Full RFC 959 Support)..." << std::endl;

    while (true) {
        sockaddr_in c_addr; socklen_t len = sizeof(c_addr);
        socket_t client = accept(server_tcp, (sockaddr*)&c_addr, &len);
        if (IS_VALID_SOCKET(client)) {
            std::thread(handle_client, client, c_addr).detach();
        }
    }
    return 0;
}