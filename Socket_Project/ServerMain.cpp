#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <ctime>
#include <sstream>
#include <filesystem> 
#include <chrono>
#include "rdt_packet.h"
#include "rdt_engine.h"
#include "file_utils.h"
#include "tcp_control.h"

#define TCP_PORT 21
namespace fs = std::filesystem;

void handle_client(socket_t client_tcp, sockaddr_in client_addr) {
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
    std::cout << "[SERVER] Client " << client_ip << " da ket noi." << std::endl;

    send_tcp_reply(client_tcp, 220, "Hybrid FTP Server Ready (Port 21).");

    char recv_buf[1024];

    // Biến quản lý chế độ kết nối Data
    bool is_pasv_mode = true;
    int server_pasv_port = 2222;
    std::string client_active_ip = client_ip;
    int client_active_port = 0;

    // Trạng thái phiên làm việc
    bool is_logged_in = false;
    std::string rnfr_filename = ""; // Lưu tạm đường dẫn cho cặp lệnh RNFR -> RNTO
    fs::path current_dir = fs::current_path();

    // Cờ abort RIÊNG cho session này (không dùng biến toàn cục dùng chung giữa
    // các client). Lưu ý: vì vòng lặp lệnh của session xử lý tuần tự (đọc lệnh
    // -> xử lý xong -> đọc lệnh tiếp), lệnh ABOR chỉ có thể được nhận SAU khi
    // transfer hiện tại kết thúc trên cùng 1 thread. Để ABOR ngắt được transfer
    // đang chạy dở, cần chạy rdt_send_file/rdt_recv_file trên 1 thread con riêng
    // trong khi thread chính vẫn recv() lệnh điều khiển - đây là điểm có thể
    // nâng cấp thêm nếu muốn ABOR hoạt động thời gian thực.
    std::atomic<bool> transfer_abort{ false };

    while (true) {
        memset(recv_buf, 0, sizeof(recv_buf));
        int bytes = recv(client_tcp, recv_buf, sizeof(recv_buf) - 1, 0);
        if (bytes <= 0) break;

        std::string cmd, arg;
        parse_ftp_command(recv_buf, cmd, arg);

        // ====================================================================
        // 1. NHÓM XÁC THỰC & HỆ THỐNG (USER, PASS, QUIT, NOOP, HELP, ABOR)
        // ====================================================================
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
        else if (cmd == "NOOP") {
            send_tcp_reply(client_tcp, 200, "NOOP ok.");
        }
        else if (cmd == "QUIT") {
            send_tcp_reply(client_tcp, 221, "Goodbye.");
            break;
        }
        else if (cmd == "HELP") {
            if (arg.empty()) {
                send_tcp_reply(client_tcp, 214, "Supported: USER PASS QUIT NOOP PWD CWD CDUP MKD RMD LIST NLST STAT SIZE MDTM TYPE MODE PORT PASV RETR STOR STOU APPE DELE RNFR RNTO HASH ABOR HELP");
            }
            else {
                send_tcp_reply(client_tcp, 214, "Help for " + arg + ": Refer to RFC 959 specification.");
            }
        }
        else if (cmd == "ABOR") {
            transfer_abort.store(true);
            send_tcp_reply(client_tcp, 225, "ABOR command successful. Data connection reset.");
        }
        else if (!is_logged_in) {
            send_tcp_reply(client_tcp, 530, "Not logged in. Please authenticate with USER and PASS first.");
        }

        // ====================================================================
        // 2. NHÓM ĐIỀU HƯỚNG THƯ MỤC & THỦ THUẬT (PWD, CWD, CDUP, MKD, RMD)
        // ====================================================================
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
        else if (cmd == "CDUP") {
            current_dir = current_dir.parent_path();
            send_tcp_reply(client_tcp, 200, "Command okay. Directory changed to " + current_dir.string());
        }
        else if (cmd == "MKD") {
            if (arg.empty()) send_tcp_reply(client_tcp, 501, "Syntax error in parameters.");
            else {
                fs::path new_dir = current_dir / arg;
                std::error_code ec;
                if (fs::create_directory(new_dir, ec)) {
                    send_tcp_reply(client_tcp, 257, "\"" + new_dir.string() + "\" created.");
                }
                else {
                    send_tcp_reply(client_tcp, 550, "Failed to create directory.");
                }
            }
        }
        else if (cmd == "RMD") {
            if (arg.empty()) send_tcp_reply(client_tcp, 501, "Syntax error in parameters.");
            else {
                fs::path target_dir = current_dir / arg;
                std::error_code ec;
                if (fs::is_directory(target_dir) && fs::is_empty(target_dir) && fs::remove(target_dir, ec)) {
                    send_tcp_reply(client_tcp, 250, "Directory removed successfully.");
                }
                else {
                    send_tcp_reply(client_tcp, 550, "Failed to remove directory (directory not empty or not found).");
                }
            }
        }

        // ====================================================================
        // 3. NHÓM THÔNG TIN & METADATA FILE (LIST, NLST, STAT, SIZE, MDTM, HASH)
        // ====================================================================
        else if (cmd == "LIST") {
            std::string file_list = "Directory listing:\r\n";
            fs::path target = arg.empty() ? current_dir : (current_dir / arg);
            if (fs::exists(target) && fs::is_directory(target)) {
                for (const auto& entry : fs::directory_iterator(target)) {
                    file_list += entry.path().filename().string();
                    if (entry.is_directory()) file_list += " [DIR]\r\n";
                    else file_list += " (" + std::to_string(fs::file_size(entry)) + " bytes)\r\n";
                }
                send_tcp_reply(client_tcp, 250, file_list);
            }
            else {
                send_tcp_reply(client_tcp, 550, "Directory not found.");
            }
        }
        else if (cmd == "NLST") {
            fs::path target = arg.empty() ? current_dir : (current_dir / arg);
            if (fs::exists(target) && fs::is_directory(target)) {
                std::string nlst_res = "";
                for (const auto& entry : fs::directory_iterator(target)) {
                    nlst_res += entry.path().filename().string() + "\r\n";
                }
                send_tcp_reply(client_tcp, 226, "Name list:\r\n" + nlst_res);
            }
            else {
                send_tcp_reply(client_tcp, 550, "Directory not found.");
            }
        }
        else if (cmd == "STAT") {
            if (arg.empty()) {
                send_tcp_reply(client_tcp, 211, "Server status: OK. Hybrid FTP Server (UDP RDT Data Channel).");
            }
            else {
                fs::path target = current_dir / arg;
                if (fs::exists(target)) {
                    std::string info = fs::is_directory(target) ? "Directory" : ("File size: " + std::to_string(fs::file_size(target)) + " bytes");
                    send_tcp_reply(client_tcp, 213, "Status of " + arg + ": " + info);
                }
                else {
                    send_tcp_reply(client_tcp, 550, "File/Directory not found.");
                }
            }
        }
        else if (cmd == "SIZE") {
            fs::path target = current_dir / arg;
            if (fs::exists(target) && fs::is_regular_file(target)) {
                send_tcp_reply(client_tcp, 213, std::to_string(fs::file_size(target)));
            }
            else {
                send_tcp_reply(client_tcp, 550, "Could not get file size.");
            }
        }
        else if (cmd == "MDTM") {
            fs::path target = current_dir / arg;
            if (fs::exists(target) && fs::is_regular_file(target)) {
                auto ftime = fs::last_write_time(target);
                auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
                std::time_t tt = std::chrono::system_clock::to_time_t(sctp);
                std::tm* gmt = std::gmtime(&tt);
                char time_buf[20];
                std::strftime(time_buf, sizeof(time_buf), "%Y%m%d%H%M%S", gmt);
                send_tcp_reply(client_tcp, 213, std::string(time_buf));
            }
            else {
                send_tcp_reply(client_tcp, 550, "Could not get modification time.");
            }
        }
        else if (cmd == "HASH") {
            std::string full_path = (current_dir / arg).string();
            std::string hash_val = calculate_file_hash(full_path);
            if (hash_val.empty()) send_tcp_reply(client_tcp, 550, "File not found or could not be hashed.");
            else send_tcp_reply(client_tcp, 213, "Hash value: " + hash_val);
        }

        // ====================================================================
        // 4. NHÓM BẢO TRÌ FILE (DELE, RNFR, RNTO)
        // ====================================================================
        else if (cmd == "DELE") {
            fs::path target = current_dir / arg;
            std::error_code ec;
            if (fs::exists(target) && fs::remove(target, ec)) {
                send_tcp_reply(client_tcp, 250, "File deleted successfully.");
            }
            else {
                send_tcp_reply(client_tcp, 550, "Failed to delete file.");
            }
        }
        else if (cmd == "RNFR") {
            fs::path target = current_dir / arg;
            if (fs::exists(target)) {
                rnfr_filename = target.string();
                send_tcp_reply(client_tcp, 350, "Requested file action pending further information.");
            }
            else {
                send_tcp_reply(client_tcp, 550, "File not found.");
            }
        }
        else if (cmd == "RNTO") {
            if (rnfr_filename.empty()) {
                send_tcp_reply(client_tcp, 503, "Bad sequence of commands. Send RNFR first.");
            }
            else {
                fs::path target_to = current_dir / arg;
                std::error_code ec;
                fs::rename(rnfr_filename, target_to, ec);
                if (!ec) {
                    send_tcp_reply(client_tcp, 250, "File renamed successfully.");
                }
                else {
                    send_tcp_reply(client_tcp, 550, "Rename failed.");
                }
                rnfr_filename = "";
            }
        }

        // ====================================================================
        // 5. NHÓM CẤU HÌNH TRUYỀN TẢI (TYPE, MODE, PASV, PORT)
        // ====================================================================
        else if (cmd == "TYPE") {
            if (arg == "A" || arg == "a") send_tcp_reply(client_tcp, 200, "Type set to ASCII.");
            else if (arg == "I" || arg == "i") send_tcp_reply(client_tcp, 200, "Type set to Binary (Image).");
            else send_tcp_reply(client_tcp, 504, "Command not implemented for that parameter.");
        }
        else if (cmd == "MODE") {
            if (arg == "S" || arg == "s") send_tcp_reply(client_tcp, 200, "Mode set to Stream.");
            else send_tcp_reply(client_tcp, 504, "Only Stream mode (S) supported.");
        }
        else if (cmd == "PASV") {
            is_pasv_mode = true;
            server_pasv_port = 20000 + (rand() % 10000);
            std::string reply = "Entering Passive Mode (127,0,0,1," +
                std::to_string(server_pasv_port / 256) + "," +
                std::to_string(server_pasv_port % 256) + ").";
            send_tcp_reply(client_tcp, 227, reply);
        }
        else if (cmd == "PORT") {
            int h1, h2, h3, h4, p1, p2;
            char comma;
            std::stringstream ss(arg);
            if (ss >> h1 >> comma >> h2 >> comma >> h3 >> comma >> h4 >> comma >> p1 >> comma >> p2) {
                client_active_ip = std::to_string(h1) + "." + std::to_string(h2) + "." + std::to_string(h3) + "." + std::to_string(h4);
                client_active_port = p1 * 256 + p2;
                is_pasv_mode = false;
                send_tcp_reply(client_tcp, 200, "PORT command successful.");
            }
            else {
                send_tcp_reply(client_tcp, 501, "Syntax error in parameters or arguments.");
            }
        }

        // ====================================================================
        // 6. NHÓM TRUYỀN DỮ LIỆU DÙNG UDP RDT (RETR, STOR, STOU, APPE)
        // ====================================================================
        else if (cmd == "STOR" || cmd == "APPE" || cmd == "STOU") {
            bool is_append = (cmd == "APPE");
            std::string filename = arg;

            if (cmd == "STOU") {
                // Thêm số đếm tăng dần bên cạnh timestamp để tránh trùng tên khi
                // có nhiều lệnh STOU trong cùng 1 giây (time() chỉ có độ phân giải giây).
                static std::atomic<long long> stou_counter{ 0 };
                filename = "unique_" + std::to_string(time(NULL)) + "_" +
                    std::to_string(stou_counter.fetch_add(1)) + ".dat";
            }

            transfer_abort.store(false); // reset cờ abort cho lượt truyền mới
            send_tcp_reply(client_tcp, 150, "Opening UDP data connection.");
            socket_t server_udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            std::string full_path = (current_dir / filename).string();
            bool ok = false;

            if (is_pasv_mode) {
                sockaddr_in udp_addr = { AF_INET, htons(server_pasv_port), INADDR_ANY };
                if (bind(server_udp, (sockaddr*)&udp_addr, sizeof(udp_addr)) != SOCKET_ERROR) {
                    ok = rdt_recv_file(server_udp, full_path, is_append, &transfer_abort);
                }
            }
            else {
                sockaddr_in udp_addr = { AF_INET, htons(20), INADDR_ANY };
                bind(server_udp, (sockaddr*)&udp_addr, sizeof(udp_addr));
                ok = rdt_recv_file(server_udp, full_path, is_append, &transfer_abort);
            }

            if (ok) {
                if (cmd == "STOU") send_tcp_reply(client_tcp, 226, "FILE: " + filename);
                else send_tcp_reply(client_tcp, 226, "Transfer complete.");
            }
            else {
                send_tcp_reply(client_tcp, 451, "Error processing file.");
            }
            CLOSE_SOCKET(server_udp);
        }
        else if (cmd == "RETR") {
            std::string full_path = (current_dir / arg).string();
            if (!fs::exists(full_path) || !fs::is_regular_file(full_path)) {
                send_tcp_reply(client_tcp, 550, "File not found.");
                continue;
            }

            transfer_abort.store(false); // reset cờ abort cho lượt truyền mới
            send_tcp_reply(client_tcp, 150, "Opening UDP data connection for RETR.");
            socket_t server_udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            bool ok = false;

            if (is_pasv_mode) {
                sockaddr_in udp_addr = { AF_INET, htons(server_pasv_port), INADDR_ANY };
                if (bind(server_udp, (sockaddr*)&udp_addr, sizeof(udp_addr)) != SOCKET_ERROR) {
                    char dummy[10];
                    sockaddr_in client_udp_addr;
                    socklen_t client_len = sizeof(client_udp_addr);
                    set_socket_timeout(server_udp, 5000);
                    if (recvfrom(server_udp, dummy, sizeof(dummy), 0, (sockaddr*)&client_udp_addr, &client_len) > 0) {
                        ok = rdt_send_file(server_udp, client_udp_addr, full_path, &transfer_abort);
                    }
                }
            }
            else {
                sockaddr_in client_udp_addr{};
                client_udp_addr.sin_family = AF_INET;
                client_udp_addr.sin_port = htons(client_active_port);
                inet_pton(AF_INET, client_active_ip.c_str(), &client_udp_addr.sin_addr);

                ok = rdt_send_file(server_udp, client_udp_addr, full_path, &transfer_abort);
            }

            if (ok) send_tcp_reply(client_tcp, 226, "Transfer complete.");
            else send_tcp_reply(client_tcp, 451, "Error transmitting file.");
            CLOSE_SOCKET(server_udp);
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
        if (client != INVALID_SOCKET) {
            std::thread(handle_client, client, c_addr).detach();
        }
    }
    return 0;
}