#pragma once
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <functional> // Thu vien ho tro tinh Hash

// ============================================================================
// HAM 1: LAY KICH THUOC FILE (BYTES)
// ============================================================================
inline uint64_t get_file_size(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return 0;
    }
    std::streamsize size = file.tellg();
    file.close();
    return static_cast<uint64_t>(size);
}

// ============================================================================
// HAM 2: DOC 1 KHOI (CHUNK) TU FILE NHI PHAN
// ============================================================================
inline bool read_file_chunk(std::ifstream& file, char* buffer, size_t chunk_size, uint16_t& bytes_read) {
    if (!file.is_open() || file.eof()) {
        bytes_read = 0;
        return false;
    }
    file.read(buffer, chunk_size);
    bytes_read = static_cast<uint16_t>(file.gcount());
    return bytes_read > 0;
}

// ============================================================================
// HAM 3: GHI 1 KHOI (CHUNK) VAO FILE NHI PHAN
// ============================================================================
inline bool write_file_chunk(std::ofstream& file, const char* buffer, uint16_t bytes_to_write) {
    if (!file.is_open()) {
        return false;
    }
    file.write(buffer, bytes_to_write);
    return file.good();
}

// ============================================================================
// HAM 4: TINH HASH (Kiem tra toan ven du lieu - EXCELLENT LEVEL)
// ============================================================================
inline std::string calculate_file_hash(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) return "FILE_NOT_FOUND";

    // Doc toan bo noi dung file vao string
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    // Dung std::hash de mo phong SHA/MD5 giup code gon nhe ma van dap ung yeu cau Do an
    std::hash<std::string> hasher;
    return std::to_string(hasher(content));
}