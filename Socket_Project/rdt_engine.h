#pragma once
#include "rdt_packet.h"
#include <iostream>
#include <fstream>
#include <chrono>
#include <atomic>
#include <thread>
#include <vector>
#include <cstring>

#ifdef _WIN32
typedef SOCKET socket_t;
#define CLOSE_SOCKET closesocket
#else
typedef int socket_t;
#define CLOSE_SOCKET close
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#endif

// ============================================================================
// RDT - Go-Back-N Sliding Window
// ----------------------------------------------------------------------------
// WINDOW_SIZE : so goi toi da duoc phep "bay" (da gui nhung chua duoc ACK)
// TIMEOUT_MS  : thoi gian cho ACK cua goi co seq = base truoc khi coi la mat
//               va phat lai TOAN BO cua so hien tai (dac trung cua Go-Back-N,
//               khac voi Selective Repeat chi phat lai goi bi mat)
// MAX_RETRANSMIT : so lan phat lai CA CUA SO toi da truoc khi bao loi
// ============================================================================
#define WINDOW_SIZE 8
#define TIMEOUT_MS 1000
#define MAX_RETRANSMIT 10

inline void set_socket_timeout(socket_t sock, int timeout_ms) {
#ifdef _WIN32
    DWORD tv = timeout_ms;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#else
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#endif
}

// Goi 1 goi RDT thuan tuy (khong cho ACK) - dung lam building block cho ca
// sender (goi DATA) lan receiver (goi ACK).
inline void rdt_raw_send(socket_t sock, const sockaddr_in& dest_addr, socklen_t addr_len,
    uint32_t seq_num, uint32_t ack_num, uint8_t flags, const char* payload, uint16_t length)
{
    RDTPacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.header.seq_num = htonl(seq_num);
    pkt.header.ack_num = htonl(ack_num);
    pkt.header.length = htons(length);
    pkt.header.flags = flags;
    if (length > 0 && payload) memcpy(pkt.data, payload, length);

    pkt.header.checksum = 0;
    pkt.header.checksum = calculate_checksum(&pkt, sizeof(RDTHeader) + length);

    sendto(sock, (const char*)&pkt, sizeof(RDTHeader) + length, 0,
        (const sockaddr*)&dest_addr, addr_len);
}

inline void rdt_send_ack(socket_t sock, const sockaddr_in& dest_addr, socklen_t addr_len, uint32_t ack_num) {
    rdt_raw_send(sock, dest_addr, addr_len, 0, ack_num, FLAG_ACK, nullptr, 0);
}

// ============================================================================
// SENDER - Go-Back-N
// ============================================================================
struct GbnSlot {
    bool active = false;
    uint16_t length = 0;
    char data[MAX_PAYLOAD_SIZE];
};

inline bool rdt_send_file(socket_t sock, const sockaddr_in& dest_addr, const std::string& filename,
    std::atomic<bool>* abort_flag = nullptr)
{
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) return false;

    socklen_t addr_len = sizeof(dest_addr);
    // Timeout ngan de vong lap vua co the gui goi moi trong cua so, vua kiem
    // tra timer tong the cua goi base, vua kiem tra abort_flag thuong xuyen.
    set_socket_timeout(sock, 100);

    std::vector<GbnSlot> window(WINDOW_SIZE);
    uint32_t base = 0;       // seq nho nhat chua duoc ACK
    uint32_t next_seq = 0;   // seq se gan cho goi tiep theo duoc gui
    bool eof_reached = false;
    bool fin_queued = false; // da dua goi ket thuc (length = 0) vao cua so chua
    bool timer_running = false;
    int retransmit_rounds = 0;
    auto base_timer_start = std::chrono::steady_clock::now();
    char read_buf[MAX_PAYLOAD_SIZE];

    while (true) {
        if (abort_flag && abort_flag->load()) {
            std::cout << "[RDT] ABOR received. Stop sending file." << std::endl;
            file.close();
            return false;
        }

        // 1. Do day cua so: gui them goi moi neu con cho trong window
        while (!fin_queued && (next_seq - base) < WINDOW_SIZE) {
            uint16_t len = 0;
            if (!eof_reached) {
                file.read(read_buf, sizeof(read_buf));
                len = static_cast<uint16_t>(file.gcount());
                if (len == 0) eof_reached = true;
            }
            if (eof_reached) {
                len = 0;          // goi rong danh dau ket thuc luong du lieu
                fin_queued = true; // sau goi nay khong dua them goi moi vao cua so nua
            }

            GbnSlot& slot = window[next_seq % WINDOW_SIZE];
            slot.active = true;
            slot.length = len;
            if (len > 0) memcpy(slot.data, read_buf, len);

            rdt_raw_send(sock, dest_addr, addr_len, next_seq, 0, FLAG_DATA,
                len > 0 ? slot.data : nullptr, len);
            next_seq++;

            if (!timer_running) {
                base_timer_start = std::chrono::steady_clock::now();
                timer_running = true;
            }
        }

        // 2. Da gui het du lieu (ke ca goi FIN) va toan bo da duoc ACK -> xong
        if (fin_queued && base == next_seq) {
            file.close();
            return true;
        }

        // 3. Cho ACK (co timeout ngan)
        RDTPacket ack_pkt;
        sockaddr_in src_addr; socklen_t src_len = sizeof(src_addr);
        int recv_bytes = recvfrom(sock, (char*)&ack_pkt, sizeof(RDTPacket), 0,
            (sockaddr*)&src_addr, &src_len);

        if (recv_bytes > 0) {
            uint16_t recv_checksum = ack_pkt.header.checksum;
            ack_pkt.header.checksum = 0;
            uint16_t calc_checksum = calculate_checksum(&ack_pkt, sizeof(RDTHeader) + ntohs(ack_pkt.header.length));

            if (recv_checksum == calc_checksum && (ack_pkt.header.flags & FLAG_ACK)) {
                uint32_t ack_num = ntohl(ack_pkt.header.ack_num); // ACK TICH LUY: "da nhan dung thu tu den goi ack_num"
                if (ack_num + 1 > base && ack_num < next_seq) {
                    for (uint32_t s = base; s <= ack_num; s++) window[s % WINDOW_SIZE].active = false;
                    base = ack_num + 1;
                    retransmit_rounds = 0;
                    if (base == next_seq) timer_running = false;
                    else { base_timer_start = std::chrono::steady_clock::now(); timer_running = true; }
                }
            }
        }

        // 4. Timeout cua goi base -> phat lai TOAN BO cua so (dac trung Go-Back-N)
        if (timer_running) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - base_timer_start).count();
            if (elapsed >= TIMEOUT_MS) {
                retransmit_rounds++;
                if (retransmit_rounds > MAX_RETRANSMIT) {
                    file.close();
                    return false;
                }
                for (uint32_t s = base; s < next_seq; s++) {
                    GbnSlot& slot = window[s % WINDOW_SIZE];
                    rdt_raw_send(sock, dest_addr, addr_len, s, 0, FLAG_DATA,
                        slot.length > 0 ? slot.data : nullptr, slot.length);
                }
                base_timer_start = std::chrono::steady_clock::now();
            }
        }
    }
}

// ============================================================================
// RECEIVER - Go-Back-N (khong dem goi den som/sai thu tu, chi nhan dung
// expected_seq, ACK tich luy cho goi cuoi cung da nhan dung thu tu)
// DUNG CHUNG CHO STOR, STOU, APPE, RETR
// ============================================================================
inline bool rdt_recv_file(socket_t sock, const std::string& filename, bool append = false,
    std::atomic<bool>* abort_flag = nullptr)
{
    std::ofstream file(filename, append ? (std::ios::binary | std::ios::app) : std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[RDT ERROR] Khong the tao/mo file: " << filename << std::endl;
        return false;
    }

    uint32_t expected_seq = 0;
    sockaddr_in src_addr{};
    socklen_t addr_len = sizeof(src_addr);

    set_socket_timeout(sock, 5000);

    while (true) {
        if (abort_flag && abort_flag->load()) {
            std::cout << "[RDT] ABOR received. Stop receiving file." << std::endl;
            file.close();
            return false;
        }

        RDTPacket recv_pkt;
        int recv_bytes = recvfrom(sock, (char*)&recv_pkt, sizeof(RDTPacket), 0,
            (sockaddr*)&src_addr, &addr_len);
        if (recv_bytes <= 0) {
            file.close(); // het thoi gian cho / loi socket
            return false;
        }

        uint16_t payload_len = ntohs(recv_pkt.header.length);
        uint16_t recv_checksum = recv_pkt.header.checksum;
        recv_pkt.header.checksum = 0;
        uint16_t calc_checksum = calculate_checksum(&recv_pkt, sizeof(RDTHeader) + payload_len);
        if (recv_checksum != calc_checksum) continue; // goi loi -> bo qua, khong ACK

        uint32_t seq_num = ntohl(recv_pkt.header.seq_num);

        if (seq_num == expected_seq) {
            if (payload_len > 0) file.write(recv_pkt.data, payload_len);
            rdt_send_ack(sock, src_addr, addr_len, expected_seq);

            if (payload_len == 0) {
                // Goi ket thuc luong du lieu. "Linger" mot chut de phong truong hop
                // ACK cuoi bi mat tren duong ve, khien sender Go-Back-N phat lai
                // ca cua so (goi FIN) - neu vay ta ACK lai roi moi ket thuc that su.
                set_socket_timeout(sock, 500);
                for (int i = 0; i < 5; i++) {
                    RDTPacket dup;
                    sockaddr_in tmp_addr; socklen_t tmp_len = sizeof(tmp_addr);
                    int rb = recvfrom(sock, (char*)&dup, sizeof(dup), 0, (sockaddr*)&tmp_addr, &tmp_len);
                    if (rb <= 0) break; // khong con goi lap -> sender coi nhu da nhan ACK
                    uint16_t dup_len = ntohs(dup.header.length);
                    uint16_t dup_cs = dup.header.checksum; dup.header.checksum = 0;
                    if (dup_cs == calculate_checksum(&dup, sizeof(RDTHeader) + dup_len)) {
                        rdt_send_ack(sock, tmp_addr, tmp_len, expected_seq);
                    }
                }
                file.close();
                return true;
            }
            expected_seq++;
        }
        else if (seq_num < expected_seq) {
            // Goi cu do sender Go-Back-N phat lai ca cua so -> ACK lai goi cuoi
            // da nhan dung thu tu de sender advance base, khong ghi lai vao file.
            if (expected_seq > 0) rdt_send_ack(sock, src_addr, addr_len, expected_seq - 1);
        }
        else {
            // Goi den som / sai thu tu (GBN receiver khong dem) -> bo qua, ACK lai
            // goi cuoi cung da nhan dung thu tu de bao sender phat lai tu cho dung.
            if (expected_seq > 0) rdt_send_ack(sock, src_addr, addr_len, expected_seq - 1);
        }
    }
}