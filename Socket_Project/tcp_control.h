#pragma once
#include <iostream>
#include <string>
#include <sstream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#endif

inline bool send_tcp_reply(socket_t tcp_sock, int code, const std::string& message) {
    std::string response = std::to_string(code) + " " + message + "\r\n";
    int bytes_sent = send(tcp_sock, response.c_str(), static_cast<int>(response.length()), 0);
    if (bytes_sent <= 0) {
        std::cerr << "[TCP CONTROL] Loi gui phan hoi cho Client!" << std::endl;
        return false;
    }
    std::cout << "[TCP SENT] " << response;
    return true;
}

inline void parse_ftp_command(const std::string& raw_input, std::string& cmd, std::string& arg) {
    cmd = "";
    arg = "";
    std::stringstream ss(raw_input);
    ss >> cmd;

    for (char& c : cmd) {
        c = static_cast<char>(toupper(c));
    }

    std::getline(ss, arg);
    size_t start = arg.find_first_not_of(" \t\r\n");
    size_t end = arg.find_last_not_of(" \t\r\n");

    if (start != std::string::npos && end != std::string::npos) {
        arg = arg.substr(start, end - start + 1);
    }
    else {
        arg = "";
    }
}