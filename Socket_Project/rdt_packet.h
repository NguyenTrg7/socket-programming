#pragma once
#include <iostream>
#include <cstdint>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib") 
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#define FLAG_DATA 0x01
#define FLAG_ACK  0x02
#define FLAG_SYN  0x04
#define FLAG_FIN  0x08

#define MAX_PAYLOAD_SIZE 1024

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

inline uint16_t calculate_checksum(const void* buffer, size_t length) {
    const uint16_t* ptr = static_cast<const uint16_t*>(buffer);
    uint32_t sum = 0;

    while (length > 1) {
        sum += *ptr++;
        length -= 2;
    }

    if (length > 0) {
        sum += *reinterpret_cast<const uint8_t*>(ptr);
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return static_cast<uint16_t>(~sum);
}