#pragma once
#include <iostream>
#include <string>
#include <sstream>

// Khởi tạo các Include hệ thống theo Cross-platform
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#endif

// ============================================================================
// HÀM GỬI MÃ PHẢN HỒI CHUẨN FTP QUA TCP (REPLY CODE 1xx, 2xx, 3xx, 4xx, 5xx)
// ============================================================================
inline bool send_tcp_reply(socket_t tcp_sock, int code, const std::string& message) {
    // Chuỗi phản hồi FTP chuẩn bắt buộc kết thúc bằng CR-LF (\r\n)
    std::string response = std::to_string(code) + " " + message + "\r\n";

    int bytes_sent = send(tcp_sock, response.c_str(), static_cast<int>(response.length()), 0);
    if (bytes_sent <= 0) {
        std::cerr << "[TCP CONTROL] Lỗi gửi phản hồi cho Client!" << std::endl;
        return false;
    }

    std::cout << "[TCP SENT] " << response; // In log kiểm tra
    return true;
}

// ============================================================================
// HÀM BÁCH PHÂN (PARSE) LỆNH FTP TỪ CLIENT GỬI SANG
// Vi dụ: Client gửi "USER admin\r\n" -> cmd = "USER", arg = "admin"
// ============================================================================
inline void parse_ftp_command(const std::string& raw_input, std::string& cmd, std::string& arg) {
    cmd = "";
    arg = "";

    std::stringstream ss(raw_input);
    ss >> cmd; // Lấy từ đầu tiên (Tên lệnh)

    // Chuyển tên lệnh thành chữ HOA (Case-insensitive)
    for (char& c : cmd) {
        c = static_cast<char>(toupper(c));
    }

    // Lấy phần tham số phía sau (nếu có)
    std::getline(ss, arg);

    // Xóa các khoảng trắng thừa hoặc ký tự \r \n ở đầu/cuối tham số
    size_t start = arg.find_first_not_of(" \t\r\n");
    size_t end = arg.find_last_not_of(" \t\r\n");

    if (start != std::string::npos && end != std::string::npos) {
        arg = arg.substr(start, end - start + 1);
    }
    else {
        arg = "";
    }
}