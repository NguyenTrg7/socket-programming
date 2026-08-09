#include "CommandRouter.h"
#include "../Common/RDT_GBN.h"
#include <unordered_map>
#include <functional>
#include <thread>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

void handleUser(const std::vector<std::string>& args, ServerSession& session) {
    send_tcp_reply(session.tcp_sock, 331, "User name okay, need password.", &session.tcp_mutex);
}

void handlePass(const std::vector<std::string>& args, ServerSession& session) {
    if (args.size() > 1 && args[1] == "admin") {
        session.is_logged_in = true;
        send_tcp_reply(session.tcp_sock, 230, "User logged in, proceed.", &session.tcp_mutex);
    } else {
        send_tcp_reply(session.tcp_sock, 530, "Not logged in. Incorrect password.", &session.tcp_mutex);
    }
}

void handleNoop(const std::vector<std::string>& args, ServerSession& session) {
    send_tcp_reply(session.tcp_sock, 200, "NOOP ok.", &session.tcp_mutex);
}

void handleQuit(const std::vector<std::string>& args, ServerSession& session) {
    session.quit_requested = true;
    send_tcp_reply(session.tcp_sock, 221, "Goodbye.", &session.tcp_mutex);
}

void handleHelp(const std::vector<std::string>& args, ServerSession& session) {
    if (args.size() == 1) send_tcp_reply(session.tcp_sock, 214, "Supported: USER PASS QUIT NOOP PWD CWD CDUP MKD RMD LIST NLST STAT SIZE MDTM TYPE MODE PORT PASV RETR STOR STOU APPE DELE RNFR RNTO HASH ABOR HELP", &session.tcp_mutex);
    else send_tcp_reply(session.tcp_sock, 214, "Help for " + args[1] + ": Refer to RFC 959 specification.", &session.tcp_mutex);
}

void handleAbort(const std::vector<std::string>& args, ServerSession& session) {
    session.abortRequested.store(true);
    send_tcp_reply(session.tcp_sock, 225, "ABOR command successful. Data connection reset.", &session.tcp_mutex);
}

void handlePwd(const std::vector<std::string>& args, ServerSession& session) {
    send_tcp_reply(session.tcp_sock, 257, "\"" + session.current_dir.string() + "\" is current directory.", &session.tcp_mutex);
}

void handleCwd(const std::vector<std::string>& args, ServerSession& session) {
    if (args.size() < 2) { send_tcp_reply(session.tcp_sock, 501, "Syntax error.", &session.tcp_mutex); return; }
    fs::path new_dir = session.current_dir / args[1];
    if (fs::exists(new_dir) && fs::is_directory(new_dir)) {
        session.current_dir = fs::canonical(new_dir);
        send_tcp_reply(session.tcp_sock, 250, "Directory successfully changed.", &session.tcp_mutex);
    } else {
        send_tcp_reply(session.tcp_sock, 550, "Failed to change directory.", &session.tcp_mutex);
    }
}

void handleCdup(const std::vector<std::string>& args, ServerSession& session) {
    session.current_dir = session.current_dir.parent_path();
    send_tcp_reply(session.tcp_sock, 200, "Command okay. Directory changed to " + session.current_dir.string(), &session.tcp_mutex);
}

void handleMkd(const std::vector<std::string>& args, ServerSession& session) {
    if (args.size() < 2) { send_tcp_reply(session.tcp_sock, 501, "Syntax error.", &session.tcp_mutex); return; }
    fs::path new_dir = session.current_dir / args[1];
    std::error_code ec;
    if (fs::create_directory(new_dir, ec)) send_tcp_reply(session.tcp_sock, 257, "\"" + new_dir.string() + "\" created.", &session.tcp_mutex);
    else send_tcp_reply(session.tcp_sock, 550, "Failed to create directory.", &session.tcp_mutex);
}

void handleRmd(const std::vector<std::string>& args, ServerSession& session) {
    if (args.size() < 2) { send_tcp_reply(session.tcp_sock, 501, "Syntax error.", &session.tcp_mutex); return; }
    fs::path target_dir = session.current_dir / args[1];
    std::error_code ec;
    if (fs::is_directory(target_dir) && fs::is_empty(target_dir) && fs::remove(target_dir, ec)) send_tcp_reply(session.tcp_sock, 250, "Directory removed.", &session.tcp_mutex);
    else send_tcp_reply(session.tcp_sock, 550, "Failed to remove directory.", &session.tcp_mutex);
}

void handleList(const std::vector<std::string>& args, ServerSession& session) {
    std::string file_list = "Directory listing:\r\n";
    fs::path target = (args.size() > 1) ? (session.current_dir / args[1]) : session.current_dir;
    if (fs::exists(target) && fs::is_directory(target)) {
        for (const auto& entry : fs::directory_iterator(target)) {
            file_list += entry.path().filename().string();
            if (entry.is_directory()) file_list += " [DIR]\r\n";
            else file_list += " (" + std::to_string(fs::file_size(entry)) + " bytes)\r\n";
        }
        send_tcp_reply(session.tcp_sock, 250, file_list, &session.tcp_mutex);
    } else send_tcp_reply(session.tcp_sock, 550, "Directory not found.", &session.tcp_mutex);
}

void handleNlst(const std::vector<std::string>& args, ServerSession& session) {
    fs::path target = (args.size() > 1) ? (session.current_dir / args[1]) : session.current_dir;
    if (fs::exists(target) && fs::is_directory(target)) {
        std::string res = "";
        for (const auto& entry : fs::directory_iterator(target)) res += entry.path().filename().string() + "\r\n";
        send_tcp_reply(session.tcp_sock, 226, "Name list:\r\n" + res, &session.tcp_mutex);
    } else send_tcp_reply(session.tcp_sock, 550, "Directory not found.", &session.tcp_mutex);
}

void handleStat(const std::vector<std::string>& args, ServerSession& session) {
    if (args.size() == 1) send_tcp_reply(session.tcp_sock, 211, "Server status: OK. Hybrid FTP Server.", &session.tcp_mutex);
    else {
        fs::path target = session.current_dir / args[1];
        if (fs::exists(target)) {
            std::string info = fs::is_directory(target) ? "Directory" : ("File size: " + std::to_string(fs::file_size(target)) + " bytes");
            send_tcp_reply(session.tcp_sock, 213, "Status of " + args[1] + ": " + info, &session.tcp_mutex);
        } else send_tcp_reply(session.tcp_sock, 550, "File/Directory not found.", &session.tcp_mutex);
    }
}

void handleSize(const std::vector<std::string>& args, ServerSession& session) {
    if (args.size() < 2) { send_tcp_reply(session.tcp_sock, 501, "Syntax error.", &session.tcp_mutex); return; }
    fs::path target = session.current_dir / args[1];
    if (fs::exists(target) && fs::is_regular_file(target)) send_tcp_reply(session.tcp_sock, 213, std::to_string(fs::file_size(target)), &session.tcp_mutex);
    else send_tcp_reply(session.tcp_sock, 550, "Could not get file size.", &session.tcp_mutex);
}

void handleMdtm(const std::vector<std::string>& args, ServerSession& session) {
    if (args.size() < 2) { send_tcp_reply(session.tcp_sock, 501, "Syntax error.", &session.tcp_mutex); return; }
    fs::path target = session.current_dir / args[1];
    if (fs::exists(target) && fs::is_regular_file(target)) {
        auto ftime = fs::last_write_time(target);
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
        std::time_t tt = std::chrono::system_clock::to_time_t(sctp);
        std::tm* gmt = std::gmtime(&tt);
        char buf[20]; std::strftime(buf, sizeof(buf), "%Y%m%d%H%M%S", gmt);
        send_tcp_reply(session.tcp_sock, 213, std::string(buf), &session.tcp_mutex);
    } else send_tcp_reply(session.tcp_sock, 550, "Could not get modification time.", &session.tcp_mutex);
}

void handleHash(const std::vector<std::string>& args, ServerSession& session) {
    if (args.size() < 2) { send_tcp_reply(session.tcp_sock, 501, "Syntax error.", &session.tcp_mutex); return; }
    std::string full_path = (session.current_dir / args[1]).string();
    std::string h = calculate_file_hash(full_path);
    if (!h.empty()) send_tcp_reply(session.tcp_sock, 213, "Hash value: " + h, &session.tcp_mutex);
    else send_tcp_reply(session.tcp_sock, 550, "File not found.", &session.tcp_mutex);
}

void handleDele(const std::vector<std::string>& args, ServerSession& session) {
    if (args.size() < 2) { send_tcp_reply(session.tcp_sock, 501, "Syntax error.", &session.tcp_mutex); return; }
    fs::path target = session.current_dir / args[1];
    std::error_code ec;
    if (fs::exists(target) && fs::remove(target, ec)) send_tcp_reply(session.tcp_sock, 250, "File deleted successfully.", &session.tcp_mutex);
    else send_tcp_reply(session.tcp_sock, 550, "Failed to delete file.", &session.tcp_mutex);
}

void handleRnfr(const std::vector<std::string>& args, ServerSession& session) {
    if (args.size() < 2) { send_tcp_reply(session.tcp_sock, 501, "Syntax error.", &session.tcp_mutex); return; }
    fs::path target = session.current_dir / args[1];
    if (fs::exists(target)) {
        session.rnfr_filename = target.string();
        send_tcp_reply(session.tcp_sock, 350, "Requested file action pending further information.", &session.tcp_mutex);
    } else send_tcp_reply(session.tcp_sock, 550, "File not found.", &session.tcp_mutex);
}

void handleRnto(const std::vector<std::string>& args, ServerSession& session) {
    if (args.size() < 2) { send_tcp_reply(session.tcp_sock, 501, "Syntax error.", &session.tcp_mutex); return; }
    if (session.rnfr_filename.empty()) send_tcp_reply(session.tcp_sock, 503, "Bad sequence of commands. Send RNFR first.", &session.tcp_mutex);
    else {
        fs::path target_to = session.current_dir / args[1];
        std::error_code ec; fs::rename(session.rnfr_filename, target_to, ec);
        if (!ec) send_tcp_reply(session.tcp_sock, 250, "File renamed successfully.", &session.tcp_mutex);
        else send_tcp_reply(session.tcp_sock, 550, "Rename failed.", &session.tcp_mutex);
        session.rnfr_filename = "";
    }
}

void handleType(const std::vector<std::string>& args, ServerSession& session) {
    if (args.size() < 2) { send_tcp_reply(session.tcp_sock, 501, "Syntax error.", &session.tcp_mutex); return; }
    if (args[1] == "A") send_tcp_reply(session.tcp_sock, 200, "Type set to ASCII.", &session.tcp_mutex);
    else if (args[1] == "I") send_tcp_reply(session.tcp_sock, 200, "Type set to Binary.", &session.tcp_mutex);
    else send_tcp_reply(session.tcp_sock, 504, "Command not implemented for that parameter.", &session.tcp_mutex);
}

void handleMode(const std::vector<std::string>& args, ServerSession& session) {
    if (args.size() < 2) { send_tcp_reply(session.tcp_sock, 501, "Syntax error.", &session.tcp_mutex); return; }
    if (args[1] == "S") send_tcp_reply(session.tcp_sock, 200, "Mode set to Stream.", &session.tcp_mutex);
    else send_tcp_reply(session.tcp_sock, 504, "Only Stream mode (S) supported.", &session.tcp_mutex);
}

void handlePasv(const std::vector<std::string>& args, ServerSession& session) {
    session.is_pasv_mode = true;
    session.data_port = 20000 + (rand() % 10000);
    std::string reply = "Entering Passive Mode (127,0,0,1," + std::to_string(session.data_port / 256) + "," + std::to_string(session.data_port % 256) + ").";
    send_tcp_reply(session.tcp_sock, 227, reply, &session.tcp_mutex);
}

void handlePort(const std::vector<std::string>& args, ServerSession& session) {
    if (args.size() < 2) { send_tcp_reply(session.tcp_sock, 501, "Syntax error.", &session.tcp_mutex); return; }
    int h1, h2, h3, h4, p1, p2; char comma;
    std::stringstream ss(args[1]);
    if (ss >> h1 >> comma >> h2 >> comma >> h3 >> comma >> h4 >> comma >> p1 >> comma >> p2) {
        session.client_active_ip = std::to_string(h1) + "." + std::to_string(h2) + "." + std::to_string(h3) + "." + std::to_string(h4);
        session.client_active_port = p1 * 256 + p2;
        session.is_pasv_mode = false;
        send_tcp_reply(session.tcp_sock, 200, "PORT command successful.", &session.tcp_mutex);
    } else send_tcp_reply(session.tcp_sock, 501, "Syntax error in parameters.", &session.tcp_mutex);
}

void handleRetr(const std::vector<std::string>& args, ServerSession& session) {
    if (args.size() < 2) { send_tcp_reply(session.tcp_sock, 501, "Syntax error.", &session.tcp_mutex); return; }
    std::string full_path = (session.current_dir / args[1]).string();
    if (!fs::exists(full_path) || !fs::is_regular_file(full_path)) { send_tcp_reply(session.tcp_sock, 550, "File not found.", &session.tcp_mutex); return; }

    session.isTransferring.store(true);
    session.abortRequested.store(false);
    send_tcp_reply(session.tcp_sock, 150, "Opening UDP data connection for RETR.", &session.tcp_mutex);

    std::thread([&session, full_path]() {
        socket_t server_udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        bool ok = false;
        if (session.is_pasv_mode) {
            sockaddr_in udp_addr = { AF_INET, htons(session.data_port), INADDR_ANY };
            if (bind(server_udp, (sockaddr*)&udp_addr, sizeof(udp_addr)) != SOCKET_ERROR) {
                char dummy[10]; sockaddr_in client_udp; socklen_t len = sizeof(client_udp);
                set_socket_timeout(server_udp, 5000);
                if (recvfrom(server_udp, dummy, sizeof(dummy), 0, (sockaddr*)&client_udp, &len) > 0) {
                    ok = rdt_send_file_gbn(server_udp, client_udp, full_path, &session.abortRequested);
                }
            }
        } else {
            sockaddr_in client_udp{}; client_udp.sin_family = AF_INET; client_udp.sin_port = htons(session.client_active_port);
            inet_pton(AF_INET, session.client_active_ip.c_str(), &client_udp.sin_addr);
            ok = rdt_send_file_gbn(server_udp, client_udp, full_path, &session.abortRequested);
        }

        if (ok) send_tcp_reply(session.tcp_sock, 226, "Transfer complete.", &session.tcp_mutex);
        else if (session.abortRequested.load()) send_tcp_reply(session.tcp_sock, 426, "Transfer aborted.", &session.tcp_mutex);
        else send_tcp_reply(session.tcp_sock, 451, "Error transmitting file.", &session.tcp_mutex);
        
        session.isTransferring.store(false);
        CLOSE_SOCKET(server_udp);
    }).detach();
}

void handleStorAppeStou(const std::vector<std::string>& args, ServerSession& session, const std::string& cmd) {
    bool is_append = (cmd == "APPE");
    std::string filename = (args.size() > 1) ? args[1] : "default.dat";

    if (cmd == "STOU") {
        static std::atomic<long long> stou_counter{ 0 };
        filename = "unique_" + std::to_string(time(NULL)) + "_" + std::to_string(stou_counter.fetch_add(1)) + ".dat";
    }

    session.isTransferring.store(true);
    session.abortRequested.store(false);
    send_tcp_reply(session.tcp_sock, 150, "Opening UDP data connection.", &session.tcp_mutex);

    std::string full_path = (session.current_dir / filename).string();

    std::thread([&session, full_path, is_append, cmd, filename]() {
        socket_t server_udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        bool ok = false;

        if (session.is_pasv_mode) {
            sockaddr_in udp_addr = { AF_INET, htons(session.data_port), INADDR_ANY };
            if (bind(server_udp, (sockaddr*)&udp_addr, sizeof(udp_addr)) != SOCKET_ERROR) {
                ok = rdt_recv_file_gbn(server_udp, full_path, is_append, &session.abortRequested);
            }
        } else {
            sockaddr_in udp_addr = { AF_INET, htons(20), INADDR_ANY };
            bind(server_udp, (sockaddr*)&udp_addr, sizeof(udp_addr));
            ok = rdt_recv_file_gbn(server_udp, full_path, is_append, &session.abortRequested);
        }

        if (ok) {
            if (cmd == "STOU") send_tcp_reply(session.tcp_sock, 226, "FILE: " + filename, &session.tcp_mutex);
            else send_tcp_reply(session.tcp_sock, 226, "Transfer complete.", &session.tcp_mutex);
        }
        else if (session.abortRequested.load()) send_tcp_reply(session.tcp_sock, 426, "Transfer aborted.", &session.tcp_mutex);
        else send_tcp_reply(session.tcp_sock, 451, "Error processing file.", &session.tcp_mutex);
        
        session.isTransferring.store(false);
        CLOSE_SOCKET(server_udp);
    }).detach();
}

// Router Chính
void routeCommand(const std::vector<std::string>& args, ServerSession& session) {
    if (args.empty()) return;

    std::string cmd = args[0];
    if (cmd == "USER") handleUser(args, session);
    else if (cmd == "PASS") handlePass(args, session);
    else if (cmd == "NOOP") handleNoop(args, session);
    else if (cmd == "QUIT") handleQuit(args, session);
    else if (cmd == "HELP") handleHelp(args, session);
    else if (cmd == "ABOR") handleAbort(args, session);
    
    else if (!session.is_logged_in) send_tcp_reply(session.tcp_sock, 530, "Not logged in.", &session.tcp_mutex);

    else if (cmd == "PWD") handlePwd(args, session);
    else if (cmd == "CWD") handleCwd(args, session);
    else if (cmd == "CDUP") handleCdup(args, session);
    else if (cmd == "MKD") handleMkd(args, session);
    else if (cmd == "RMD") handleRmd(args, session);
    else if (cmd == "LIST") handleList(args, session);
    else if (cmd == "NLST") handleNlst(args, session);
    else if (cmd == "STAT") handleStat(args, session);
    else if (cmd == "SIZE") handleSize(args, session);
    else if (cmd == "MDTM") handleMdtm(args, session);
    else if (cmd == "HASH") handleHash(args, session);
    else if (cmd == "DELE") handleDele(args, session);
    else if (cmd == "RNFR") handleRnfr(args, session);
    else if (cmd == "RNTO") handleRnto(args, session);
    else if (cmd == "TYPE") handleType(args, session);
    else if (cmd == "MODE") handleMode(args, session);
    else if (cmd == "PASV") handlePasv(args, session);
    else if (cmd == "PORT") handlePort(args, session);
    else if (cmd == "RETR") handleRetr(args, session);
    else if (cmd == "STOR" || cmd == "APPE" || cmd == "STOU") handleStorAppeStou(args, session, cmd);
    else send_tcp_reply(session.tcp_sock, 502, "Command not implemented.", &session.tcp_mutex);
}