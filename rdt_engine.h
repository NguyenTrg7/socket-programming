#pragma once
#include "rdt_packet.h"
#include <iostream>
#include <fstream> // Thêm thư viện đọc/ghi file
#include <chrono>

// --- CROSS-PLATFORM SOCKET DEFINITIONS ---
#ifdef _WIN32
typedef SOCKET socket_t;
#define CLOSE_SOCKET closesocket
#else
typedef int socket_t;
#define CLOSE_SOCKET close
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#endif

#define MAX_RETRANSMIT 10 // Số lần thử gửi lại tối đa trước khi hủy kết nối
#define TIMEOUT_MS 1000   // Thời gian chờ ACK (1000ms = 1 giây)

// --- HÀM CÀI ĐẶT TIMEOUT CHO SOCKET ---
inline void set_socket_timeout(socket_t sock, int timeout_ms) {
#ifdef _WIN32
    DWORD tv = timeout_ms; // Windows nhận thời gian dạng miligiây (DWORD)
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#else
    struct timeval tv;     // Linux/macOS dùng struct timeval (giây + microgiây)
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#endif
}

// ============================================================================
// 1. HÀM GỬI DỮ LIỆU TIN CẬY (SENDER SIDE - CHUNK LEVEL)
// ============================================================================
inline bool rdt_send_chunk(socket_t sock, const sockaddr_in* dest_addr, socklen_t addr_len,
    const char* buffer, uint16_t length, uint32_t seq_num)
{
    RDTPacket send_pkt;
    memset(&send_pkt, 0, sizeof(RDTPacket));

    // Đóng gói Header & Payload
    send_pkt.header.seq_num = htonl(seq_num);
    send_pkt.header.ack_num = htonl(0); 
    send_pkt.header.length = htons(length);
    send_pkt.header.flags = FLAG_DATA;
    memcpy(send_pkt.data, buffer, length);

    // Tính Checksum cho toàn bộ gói tin (Header + Data)
    send_pkt.header.checksum = 0;
    send_pkt.header.checksum = calculate_checksum(&send_pkt, sizeof(RDTHeader) + length);

    int retransmit_count = 0;
    set_socket_timeout(sock, TIMEOUT_MS);

    while (retransmit_count < MAX_RETRANSMIT) {
        // Gửi gói tin dữ liệu qua UDP socket
        int sent_bytes = sendto(sock, (const char*)&send_pkt, sizeof(RDTHeader) + length, 0,
            (const sockaddr*)dest_addr, addr_len);

        if (sent_bytes == SOCKET_ERROR) {
            std::cerr << "[SENDER] Lỗi sendto!" << std::endl;
            return false;
        }

        std::cout << "[SENDER] Da gui Seq: " << seq_num
            << " (" << length << " bytes). Dang cho ACK..." << std::endl;

        // Chờ nhận phản hồi ACK từ Receiver
        RDTPacket ack_pkt;
        sockaddr_in src_addr;
        socklen_t src_len = sizeof(src_addr);

        int recv_bytes = recvfrom(sock, (char*)&ack_pkt, sizeof(RDTPacket), 0,
            (sockaddr*)&src_addr, &src_len);

        // Trường hợp 1: Timeout (recvfrom trả về <= 0)
        if (recv_bytes <= 0) {
            retransmit_count++;
            std::cout << "[SENDER] Timeout! Gui lai lan " << retransmit_count << "..." << std::endl;
            continue;
        }

        // Trường hợp 2: Kiểm tra tính toàn vẹn của gói ACK
        uint16_t recv_checksum = ack_pkt.header.checksum;
        ack_pkt.header.checksum = 0;
        uint16_t calc_checksum = calculate_checksum(&ack_pkt, sizeof(RDTHeader) + ntohs(ack_pkt.header.length));

        if (recv_checksum != calc_checksum) {
            std::cout << "[SENDER] Goi ACK bi hong Checksum! Gui lai..." << std::endl;
            retransmit_count++;
            continue;
        }

        // Giải mã thông tin từ gói ACK
        uint32_t ack_num = ntohl(ack_pkt.header.ack_num);
        uint8_t flags = ack_pkt.header.flags;

        // Trường hợp 3: Nhận đúng ACK mong đợi
        if ((flags & FLAG_ACK) && ack_num == seq_num) {
            std::cout << "[SENDER] Nhan thanh cong ACK: " << ack_num << std::endl;
            return true; // Gửi thành công!
        }
        else {
            std::cout << "[SENDER] ACK khong hop le (Nhan ACK " << ack_num
                << ", mong doi " << seq_num << "). Gui lai..." << std::endl;
            retransmit_count++;
        }
    }

    std::cerr << "[SENDER] Vuot qua so lan thu gui lai. Truyen thất bại!" << std::endl;
    return false;
}

// ============================================================================
// 2. HÀM NHẬN DỮ LIỆU TIN CẬY (RECEIVER SIDE - CHUNK LEVEL)
// ============================================================================
inline bool rdt_recv_chunk(socket_t sock, sockaddr_in* src_addr, socklen_t* addr_len,
    char* out_buffer, uint16_t& out_length, uint32_t expected_seq)
{
    while (true) {
        RDTPacket recv_pkt;
        int recv_bytes = recvfrom(sock, (char*)&recv_pkt, sizeof(RDTPacket), 0,
            (sockaddr*)src_addr, addr_len);

        if (recv_bytes <= 0) {
            return false; // Thường xảy ra khi timeout phiên làm việc
        }

        uint16_t payload_len = ntohs(recv_pkt.header.length);

        // 1. Kiểm tra Checksum gói tin nhận được
        uint16_t recv_checksum = recv_pkt.header.checksum;
        recv_pkt.header.checksum = 0;
        uint16_t calc_checksum = calculate_checksum(&recv_pkt, sizeof(RDTHeader) + payload_len);

        if (recv_checksum != calc_checksum) {
            std::cout << "[RECEIVER] Goi tin bi loi Checksum! Bo qua..." << std::endl;
            continue; // Bỏ qua gói hỏng, không gửi ACK để bên gửi timeout gửi lại
        }

        uint32_t seq_num = ntohl(recv_pkt.header.seq_num);

        // 2. Chuẩn bị gói tin ACK trả lời
        RDTPacket ack_pkt;
        memset(&ack_pkt, 0, sizeof(RDTPacket));
        ack_pkt.header.seq_num = htonl(0);
        ack_pkt.header.ack_num = htonl(seq_num); // Trả về ACK với số Seq vừa nhận
        ack_pkt.header.length = htons(0);
        ack_pkt.header.flags = FLAG_ACK;

        ack_pkt.header.checksum = 0;
        ack_pkt.header.checksum = calculate_checksum(&ack_pkt, sizeof(RDTHeader));

        // Gửi ACK về cho Sender
        sendto(sock, (const char*)&ack_pkt, sizeof(RDTHeader), 0, (sockaddr*)src_addr, *addr_len);

        // 3. Kiểm tra Sequence Number
        if (seq_num == expected_seq) {
            // Đúng gói mong đợi -> Chép dữ liệu ra buffer ngoài và kết thúc
            memcpy(out_buffer, recv_pkt.data, payload_len);
            out_length = payload_len;
            std::cout << "[RECEIVER] Nhan dung goi Seq: " << seq_num << " -> Gui ACK " << seq_num << std::endl;
            return true;
        }
        else {
            // Gói tin bị trùng lặp (Duplicate Packet) -> Đã gửi ACK ở trên, không chép vào buffer nữa
            std::cout << "[RECEIVER] Nhan goi trung lap Seq: " << seq_num
                << " (Mong doi " << expected_seq << ") -> Gui lai ACK cũ" << std::endl;
        }
    }
}

// ============================================================================
// 3. HÀM TRUYỀN FILE HOÀN CHỈNH (DÙNG CHO CLIENT STOR)
// ============================================================================
inline bool rdt_send_file(socket_t sock, const sockaddr_in& dest_addr, const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[RDT ERROR] Khong the mo file de gui: " << filename << std::endl;
        return false;
    }

    char buffer[1024];
    uint32_t seq_num = 0;
    socklen_t addr_len = sizeof(dest_addr);

    std::cout << "[RDT FILE] Bat dau gui file '" << filename << "'..." << std::endl;

    // Đọc từng mảng dữ liệu và gọi rdt_send_chunk
    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
        uint16_t bytes_read = static_cast<uint16_t>(file.gcount());
        if (!rdt_send_chunk(sock, &dest_addr, addr_len, buffer, bytes_read, seq_num)) {
            std::cerr << "[RDT ERROR] Gui chunk that bai o Seq: " << seq_num << std::endl;
            file.close();
            return false;
        }
        seq_num++;
    }

    // Gửi gói tin EOF (Độ dài 0 byte) báo kết thúc file
    rdt_send_chunk(sock, &dest_addr, addr_len, "", 0, seq_num);

    file.close();
    std::cout << "[RDT FILE] Da gui xong file '" << filename << "'!" << std::endl;
    return true;
}

// ============================================================================
// 4. HÀM NHẬN FILE HOÀN CHỈNH (DÙNG CHO SERVER STOR)
// ============================================================================
inline bool rdt_recv_file(socket_t sock, const std::string& filename) {
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[RDT ERROR] Khong the tao file de ghi: " << filename << std::endl;
        return false;
    }

    char buffer[2048];
    uint16_t out_length = 0;
    uint32_t expected_seq = 0;
    sockaddr_in src_addr{};
    socklen_t addr_len = sizeof(src_addr);

    // Timeout nhận file 5 giây
    set_socket_timeout(sock, 5000);

    std::cout << "[RDT FILE] Dang nhan file '" << filename << "'..." << std::endl;

    while (true) {
        if (!rdt_recv_chunk(sock, &src_addr, &addr_len, buffer, out_length, expected_seq)) {
            std::cerr << "[RDT ERROR] Timeout hoac ngat ket noi khi nhan file!" << std::endl;
            break;
        }

        // Nếu nhận gói tin 0 byte -> Tín hiệu EOF
        if (out_length == 0) {
            std::cout << "[RDT FILE] Nhan tin hieu EOF. Ket thuc luu file!" << std::endl;
            file.close();
            return true;
        }

        // Ghi dữ liệu vào đĩa
        file.write(buffer, out_length);
        expected_seq++;
    }

    file.close();
    return false;
}