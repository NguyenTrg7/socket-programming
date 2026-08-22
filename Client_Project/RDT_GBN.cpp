#include "RDT_GBN.h"
#include <chrono>
#include <vector>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <filesystem>
#include <iostream>

uint16_t calculate_checksum(const void* buffer, size_t length) {
    const uint16_t* ptr = static_cast<const uint16_t*>(buffer);
    uint32_t sum = 0;
    while (length > 1) { sum += *ptr++; length -= 2; }
    if (length > 0) sum += *reinterpret_cast<const uint8_t*>(ptr);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return static_cast<uint16_t>(~sum);
}

void set_socket_timeout(socket_t sock, int timeout_ms) {
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

void rdt_raw_send(socket_t sock, const sockaddr_in& dest_addr, uint32_t seq_num, uint32_t ack_num, uint8_t flags, const char* payload, uint16_t length) {
    RDTPacket pkt; memset(&pkt, 0, sizeof(pkt));
    pkt.header.seq_num = htonl(seq_num);
    pkt.header.ack_num = htonl(ack_num);
    pkt.header.length = htons(length);
    pkt.header.flags = flags;
    if (length > 0 && payload) memcpy(pkt.data, payload, length);
    pkt.header.checksum = calculate_checksum(&pkt, sizeof(RDTHeader) + length);
    sendto(sock, (const char*)&pkt, sizeof(RDTHeader) + length, 0, (const sockaddr*)&dest_addr, sizeof(dest_addr));
}

bool rdt_send_file_gbn(socket_t sock, const sockaddr_in& dest_addr, const std::string& filename, std::atomic<bool>* abortRequested) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) return false;

    file.seekg(0, std::ios::end);
    size_t total_bytes = file.tellg();
    file.seekg(0, std::ios::beg);
    uint32_t total_packets = (total_bytes + MAX_PAYLOAD_SIZE - 1) / MAX_PAYLOAD_SIZE;
    if (total_packets == 0) total_packets = 1;

    socklen_t addr_len = sizeof(dest_addr);
    set_socket_timeout(sock, 100);
    std::vector<GbnSlot> window(WINDOW_SIZE);
    uint32_t base = 0, next_seq = 0;
    bool eof_reached = false, fin_queued = false, timer_running = false;
    int retransmit_rounds = 0;
    auto base_timer_start = std::chrono::steady_clock::now();
    char read_buf[MAX_PAYLOAD_SIZE];

    auto last_print_send = std::chrono::steady_clock::now();
    auto start_time = std::chrono::steady_clock::now();

    while (true) {
        if (abortRequested && abortRequested->load()) {
            std::cout << "\033[s\033[1A\033[2K[RDT] ABOR received. Stop sending file.\033[u" << std::flush;
            file.close(); return false;
        }

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_print_send).count() >= 300) {
            double elapsed_sec = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count() / 1000.0;
            if (elapsed_sec <= 0) elapsed_sec = 0.001;

            float percent = (float)base / total_packets * 100.0f;
            if (percent > 100.0f) percent = 100.0f;

            double current_bytes = (double)base * MAX_PAYLOAD_SIZE;
            double mb_per_sec = (current_bytes / (1024.0 * 1024.0)) / elapsed_sec;

            // Mã ANSI: Lưu con trỏ -> Lên 1 dòng -> Xóa dòng -> In tiến trình -> Khôi phục con trỏ
            std::cout << "\033[s\033[1A\033[2K[RDT Send] " << base << "/" << total_packets << " pkts ("
                << std::fixed << std::setprecision(1) << percent << "%) "
                << "| Speed: " << std::setprecision(2) << mb_per_sec << " MB/s\033[u" << std::flush;

            last_print_send = now;
        }

        while (!fin_queued && (next_seq - base) < WINDOW_SIZE) {
            uint16_t len = 0;
            if (!eof_reached) {
                file.read(read_buf, sizeof(read_buf));
                len = static_cast<uint16_t>(file.gcount());
                if (len == 0) eof_reached = true;
            }
            if (eof_reached) { len = 0; fin_queued = true; }

            GbnSlot& slot = window[next_seq % WINDOW_SIZE];
            slot.active = true; slot.length = len;
            if (len > 0) memcpy(slot.data, read_buf, len);

            rdt_raw_send(sock, dest_addr, next_seq, 0, FLAG_DATA, len > 0 ? slot.data : nullptr, len);
            next_seq++;
            if (!timer_running) { base_timer_start = std::chrono::steady_clock::now(); timer_running = true; }
        }

        if (fin_queued && base == next_seq) {
            auto end_time = std::chrono::steady_clock::now();
            double total_sec = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count() / 1000.0;
            if (total_sec <= 0.0001) total_sec = 0.001;

            double total_mb = total_bytes / (1024.0 * 1024.0);
            double avg_mb_s = total_mb / total_sec;

            std::cout << "\033[s\033[1A\033[2K[RDT Send] 100% COMPLETE! (" << total_packets << "/" << total_packets << " pkts) "
                << "| Time: " << std::fixed << std::setprecision(2) << total_sec << "s "
                << "| Avg Speed: " << avg_mb_s << " MB/s\033[u" << std::flush;
            file.close(); return true;
        }

        RDTPacket ack_pkt; sockaddr_in src_addr; socklen_t src_len = sizeof(src_addr);
        if (recvfrom(sock, (char*)&ack_pkt, sizeof(RDTPacket), 0, (sockaddr*)&src_addr, &src_len) > 0) {
            uint16_t recv_cs = ack_pkt.header.checksum; ack_pkt.header.checksum = 0;
            if (recv_cs == calculate_checksum(&ack_pkt, sizeof(RDTHeader) + ntohs(ack_pkt.header.length)) && (ack_pkt.header.flags & FLAG_ACK)) {
                uint32_t ack_num = ntohl(ack_pkt.header.ack_num);
                if (ack_num + 1 > base && ack_num < next_seq) {
                    base = ack_num + 1; retransmit_rounds = 0;
                    timer_running = (base != next_seq);
                    if (timer_running) base_timer_start = std::chrono::steady_clock::now();
                }
            }
        }

        if (timer_running && std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - base_timer_start).count() >= TIMEOUT_MS) {
            retransmit_rounds++;
            if (retransmit_rounds > MAX_RETRANSMIT) {
                std::cout << "\033[s\033[1A\033[2K[RDT] Retransmit limit reached.\033[u" << std::flush;
                file.close(); return false;
            }
            for (uint32_t s = base; s < next_seq; s++) {
                GbnSlot& slot = window[s % WINDOW_SIZE];
                rdt_raw_send(sock, dest_addr, s, 0, FLAG_DATA, slot.length > 0 ? slot.data : nullptr, slot.length);
            }
            base_timer_start = std::chrono::steady_clock::now();
        }
    }
}

bool rdt_recv_file_gbn(socket_t sock, const std::string& filename, bool append, std::atomic<bool>* abortRequested) {
    std::string actual_filename = filename;

    if (!append && std::filesystem::exists(actual_filename)) {
        std::filesystem::path p(actual_filename);
        std::string stem = p.stem().string();
        std::string ext = p.extension().string();

        int counter = 1;
        std::filesystem::path new_path;
        do {
            std::string new_name = stem + "(" + std::to_string(counter) + ")" + ext;
            if (p.parent_path().empty()) {
                new_path = new_name;
            }
            else {
                new_path = p.parent_path() / new_name;
            }
            counter++;
        } while (std::filesystem::exists(new_path));

        actual_filename = new_path.string();
        std::cout << "\033[s\033[1A\033[2K[RDT Recv] File exists. Auto-renamed to: " << actual_filename << "\033[u" << std::flush;
    }

    std::ofstream file(actual_filename, append ? (std::ios::binary | std::ios::app) : std::ios::binary);
    if (!file.is_open()) return false;

    uint32_t expected_seq = 0;
    sockaddr_in src_addr{}; socklen_t addr_len = sizeof(src_addr);

    set_socket_timeout(sock, 1000);
    int timeout_count = 0;

    auto last_print_recv = std::chrono::steady_clock::now();
    auto start_time = std::chrono::steady_clock::now();

    while (true) {
        if (abortRequested && abortRequested->load()) {
            std::cout << "\033[s\033[1A\033[2K[RDT] ABOR received. Stop receiving file.\033[u" << std::flush;
            file.close(); return false;
        }

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_print_recv).count() >= 300) {
            double elapsed_sec = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count() / 1000.0;
            if (elapsed_sec <= 0) elapsed_sec = 0.001;

            double current_bytes = (double)expected_seq * MAX_PAYLOAD_SIZE;
            double mb_per_sec = (current_bytes / (1024.0 * 1024.0)) / elapsed_sec;

            // Mã ANSI: Lưu con trỏ -> Lên 1 dòng -> Xóa dòng -> In tiến trình -> Khôi phục con trỏ
            std::cout << "\033[s\033[1A\033[2K[RDT Recv] Received: " << expected_seq << " pkts "
                << "| Speed: " << std::fixed << std::setprecision(2) << mb_per_sec << " MB/s\033[u" << std::flush;

            last_print_recv = now;
        }

        RDTPacket recv_pkt;
        int recv_bytes = recvfrom(sock, (char*)&recv_pkt, sizeof(RDTPacket), 0, (sockaddr*)&src_addr, &addr_len);

        if (recv_bytes <= 0) {
            timeout_count++;
            if (timeout_count > 10) {
                std::cout << "\033[s\033[1A\033[2K[RDT] Error: Timeout, connection interrupted.\033[u" << std::flush;
                file.close();
                return false;
            }
            continue;
        }
        timeout_count = 0;

        uint16_t payload_len = ntohs(recv_pkt.header.length);
        uint16_t recv_cs = recv_pkt.header.checksum; recv_pkt.header.checksum = 0;
        if (recv_cs != calculate_checksum(&recv_pkt, sizeof(RDTHeader) + payload_len)) continue;

        uint32_t seq_num = ntohl(recv_pkt.header.seq_num);
        if (seq_num == expected_seq) {
            if (payload_len > 0) file.write(recv_pkt.data, payload_len);
            rdt_raw_send(sock, src_addr, 0, expected_seq, FLAG_ACK, nullptr, 0);

            if (payload_len == 0) {
                auto end_time = std::chrono::steady_clock::now();
                double total_sec = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count() / 1000.0;
                if (total_sec <= 0.0001) total_sec = 0.001;

                double total_mb = ((double)expected_seq * MAX_PAYLOAD_SIZE) / (1024.0 * 1024.0);
                double avg_mb_s = total_mb / total_sec;

                std::cout << "\033[s\033[1A\033[2K[RDT Recv] COMPLETE! (" << expected_seq << " pkts) "
                    << "| Time: " << std::fixed << std::setprecision(2) << total_sec << "s "
                    << "| Avg Speed: " << avg_mb_s << " MB/s\033[u" << std::flush;

                set_socket_timeout(sock, 500);
                for (int i = 0; i < 5; i++) {
                    RDTPacket dup; sockaddr_in tmp_addr; socklen_t tmp_len = sizeof(tmp_addr);
                    if (recvfrom(sock, (char*)&dup, sizeof(dup), 0, (sockaddr*)&tmp_addr, &tmp_len) <= 0) break;
                    uint16_t dup_len = ntohs(dup.header.length);
                    uint16_t dup_cs = dup.header.checksum; dup.header.checksum = 0;
                    if (dup_cs == calculate_checksum(&dup, sizeof(RDTHeader) + dup_len)) rdt_raw_send(sock, tmp_addr, 0, expected_seq, FLAG_ACK, nullptr, 0);
                }
                file.close(); return true;
            }
            expected_seq++;
        }
        else if (expected_seq > 0) {
            rdt_raw_send(sock, src_addr, 0, expected_seq - 1, FLAG_ACK, nullptr, 0);
        }
    }
}