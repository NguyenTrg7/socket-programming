#include "SocketIO.h"
#include <sstream>
#include <fstream>
#include <iomanip>

bool send_tcp_reply(socket_t tcp_sock, int code, const std::string& message, std::mutex* tcp_mutex) {
    std::string response = std::to_string(code) + " " + message + "\r\n";
    
    if (tcp_mutex) tcp_mutex->lock();
    int bytes_sent = send(tcp_sock, response.c_str(), static_cast<int>(response.length()), 0);
    if (tcp_mutex) tcp_mutex->unlock();

    if (bytes_sent <= 0) {
        std::cerr << "[TCP CONTROL] Loi gui phan hoi cho Client!\n";
        return false;
    }
    std::cout << "[TCP SENT] " << response;
    return true;
}

std::vector<std::string> parse_command_tokens(const std::string& raw_input) {
    std::vector<std::string> tokens;
    std::stringstream ss(raw_input);
    std::string token;
    
    if (ss >> token) {
        for (char& c : token) c = static_cast<char>(toupper(c));
        tokens.push_back(token);
    }
    
    std::string arg;
    std::getline(ss, arg);
    size_t start = arg.find_first_not_of(" \t\r\n");
    size_t end = arg.find_last_not_of(" \t\r\n");
    if (start != std::string::npos && end != std::string::npos) {
        tokens.push_back(arg.substr(start, end - start + 1));
    }
    return tokens;
}

std::string calculate_file_hash(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) return "";

    constexpr std::streamsize chunk_size = 4096;
    char buffer[chunk_size];

#ifdef _WIN32
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) return "";
    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) { CryptReleaseContext(hProv, 0); return ""; }

    while (file.read(buffer, chunk_size)) {
        CryptHashData(hHash, reinterpret_cast<BYTE*>(buffer), static_cast<DWORD>(file.gcount()), 0);
    }
    if (file.gcount() > 0) {
        CryptHashData(hHash, reinterpret_cast<BYTE*>(buffer), static_cast<DWORD>(file.gcount()), 0);
    }

    DWORD cbHashSize = 0, dwCount = sizeof(DWORD);
    CryptGetHashParam(hHash, HP_HASHSIZE, reinterpret_cast<BYTE*>(&cbHashSize), &dwCount, 0);
    BYTE* rgbHash = new BYTE[cbHashSize];
    CryptGetHashParam(hHash, HP_HASHVAL, rgbHash, &cbHashSize, 0);

    std::stringstream ss;
    for (DWORD i = 0; i < cbHashSize; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(rgbHash[i]);
    }
    
    delete[] rgbHash;
    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
    return ss.str();
#else
    SHA256_CTX ctx;
    if (!SHA256_Init(&ctx)) return "";
    while (file.read(buffer, chunk_size)) {
        SHA256_Update(&ctx, buffer, static_cast<size_t>(file.gcount()));
    }
    if (file.gcount() > 0) {
        SHA256_Update(&ctx, buffer, static_cast<size_t>(file.gcount()));
    }

    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256_Final(digest, &ctx);
    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(digest[i]);
    }
    return ss.str();
#endif
}