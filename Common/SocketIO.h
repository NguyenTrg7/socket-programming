#ifndef SOCKET_IO_H
#define SOCKET_IO_H

#include <iostream>
#include <string>
#include <vector>
#include <mutex>
#include <chrono>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <wincrypt.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "advapi32.lib")
typedef SOCKET socket_t;
#define CLOSE_SOCKET closesocket
#define IS_VALID_SOCKET(s) ((s) != INVALID_SOCKET)
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <openssl/sha.h>
typedef int socket_t;
#define CLOSE_SOCKET close
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define IS_VALID_SOCKET(s) ((s) >= 0)
#endif

bool send_tcp_reply(socket_t tcp_sock, int code, const std::string& message, std::mutex* tcp_mutex = nullptr);
std::vector<std::string> parse_command_tokens(const std::string& raw_input);
std::string calculate_file_hash(const std::string& filepath);

#endif