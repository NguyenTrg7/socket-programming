#include "CommandRouter.h"
#include <thread>
#include <cstring>
#include <ctime>
#include <atomic>

#define TCP_PORT 21

std::atomic<int> active_sessions{0};

void handle_client(socket_t client_tcp, sockaddr_in client_addr) {
    ServerSession session;
    session.tcp_sock = client_tcp;
    session.client_addr = client_addr;
    session.current_dir = std::filesystem::current_path();

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);

    active_sessions++;
    std::cout << "[SERVER] Client " << client_ip << " da ket noi." << std::endl;
    std::cout << ">>> Bảng phiên hoạt động: Hien tai co " << active_sessions.load() << " client dang ket noi.\n";

    send_tcp_reply(client_tcp, 220, "Hybrid FTP Server Ready (Port 21).", &session.tcp_mutex);

    char recv_buf[1024];
    while (!session.quit_requested) {
        memset(recv_buf, 0, sizeof(recv_buf));
        int bytes = recv(client_tcp, recv_buf, sizeof(recv_buf) - 1, 0);
        if (bytes <= 0) break;
        std::cout << "[Client " << client_ip << " sent]: " << recv_buf;
        auto args = parse_command_tokens(recv_buf);
        routeCommand(args, session);
    }

    active_sessions--;
    std::cout << "[SERVER] Client " << client_ip << " ngat ket noi. Con lai " << active_sessions.load() << " client.\n";
    
    CLOSE_SOCKET(client_tcp);
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