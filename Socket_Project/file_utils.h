#pragma once
#include <string>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
// Tự động link thư viện mã hóa của Windows (Không cần cấu hình tay)
#pragma comment(lib, "advapi32.lib")
#else
// Trên Linux/macOS dùng SHA-256 của OpenSSL (cần link -lssl -lcrypto khi biên dịch,
// ví dụ: g++ -std=c++17 -Wall ServerMain.cpp -o server -lssl -lcrypto)
#include <openssl/sha.h>
#endif

// Tính hash SHA-256 của 1 file. Trả về chuỗi rỗng ("") nếu có lỗi
// (không mở được file, không khởi tạo được thư viện mã hóa, v.v.)
// Nơi gọi hàm này PHẢI kiểm tra .empty() để phát hiện lỗi, KHÔNG so sánh
// với 1 chuỗi lỗi cố định vì hàm không trả về chuỗi đó.
inline std::string calculate_file_hash(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Loi: Khong the mo file - " << filepath << "\n";
        return "";
    }

    constexpr std::streamsize chunk_size = 4096;
    char buffer[chunk_size];

#ifdef _WIN32
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;

    // Khoi tao Context va doi tuong Hash SHA-256
    if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        std::cerr << "Loi: Khong the khoi tao CryptoAPI.\n";
        return "";
    }
    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
        CryptReleaseContext(hProv, 0);
        return "";
    }

    // Doc file theo chunk 4KB va bam du lieu
    while (file.read(buffer, chunk_size)) {
        CryptHashData(hHash, reinterpret_cast<BYTE*>(buffer), static_cast<DWORD>(file.gcount()), 0);
    }
    if (file.gcount() > 0) {
        CryptHashData(hHash, reinterpret_cast<BYTE*>(buffer), static_cast<DWORD>(file.gcount()), 0);
    }

    // Lay kich thuoc ma bam
    DWORD cbHashSize = 0;
    DWORD dwCount = sizeof(DWORD);
    CryptGetHashParam(hHash, HP_HASHSIZE, reinterpret_cast<BYTE*>(&cbHashSize), &dwCount, 0);

    // Lay gia tri ma bam
    BYTE* rgbHash = new BYTE[cbHashSize];
    CryptGetHashParam(hHash, HP_HASHVAL, rgbHash, &cbHashSize, 0);

    // Chuyen mang byte sang chuoi Hex
    std::stringstream ss;
    for (DWORD i = 0; i < cbHashSize; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(rgbHash[i]);
    }

    // Don dep bo nho
    delete[] rgbHash;
    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);

    return ss.str();
#else
    SHA256_CTX ctx;
    if (!SHA256_Init(&ctx)) {
        std::cerr << "Loi: Khong the khoi tao SHA256_CTX.\n";
        return "";
    }

    // Doc file theo chunk 4KB va bam du lieu
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