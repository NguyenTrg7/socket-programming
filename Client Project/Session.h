#ifndef SESSION_H
#define SESSION_H

#include "SocketIO.h"
#include <atomic>
#include <filesystem>

struct ServerSession {
    socket_t tcp_sock;
    sockaddr_in client_addr;
    std::mutex tcp_mutex; 

    std::string username = "";
    bool is_logged_in = false;
    bool quit_requested = false;
    
    std::filesystem::path current_dir;
    std::string rnfr_filename = "";

    bool is_pasv_mode = true;
    int data_port = 2222;
    std::string client_active_ip = "";
    int client_active_port = 0;

    std::atomic<bool> isTransferring{false};
    std::atomic<bool> abortRequested{false};
};

#endif