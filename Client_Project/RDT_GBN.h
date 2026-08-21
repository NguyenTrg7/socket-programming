#ifndef RDT_GBN_H
#define RDT_GBN_H

#include "SocketIO.h"
#include <atomic>
#include <string>
// from reliable transfer to unreliable transfer
#define FLAG_DATA 0x01
#define FLAG_ACK  0x02
#define MAX_PAYLOAD_SIZE 1024
#define WINDOW_SIZE 8
#define TIMEOUT_MS 1000
#define MAX_RETRANSMIT 10

#pragma pack(push, 1)
struct RDTHeader {
    uint32_t seq_num;
    uint32_t ack_num;
    uint16_t length;
    uint16_t checksum;
    uint8_t  flags;
};
struct RDTPacket {
    RDTHeader header;
    char data[MAX_PAYLOAD_SIZE];
};
#pragma pack(pop)

struct GbnSlot { 
    bool active = false; 
    uint16_t length = 0; 
    char data[MAX_PAYLOAD_SIZE]; 
};

uint16_t calculate_checksum(const void* buffer, size_t length);
void set_socket_timeout(socket_t sock, int timeout_ms);
void rdt_raw_send(socket_t sock, const sockaddr_in& dest_addr, uint32_t seq_num, uint32_t ack_num, uint8_t flags, const char* payload, uint16_t length);

bool rdt_send_file_gbn(socket_t sock, const sockaddr_in& dest_addr, const std::string& filename, std::atomic<bool>* abortRequested);
bool rdt_recv_file_gbn(socket_t sock, const std::string& filename, bool append, std::atomic<bool>* abortRequested);

#endif