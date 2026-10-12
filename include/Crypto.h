#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace Crypto
{
    std::vector<unsigned char> randomBytes(size_t n);
    std::string hexEncode(const std::vector<unsigned char> &data);
    std::vector<unsigned char> hexDecode(const std::string &hex);
    std::string base64Encode(const std::vector<unsigned char> &data);
    std::vector<unsigned char> base64Decode(const std::string &text);
    std::vector<unsigned char> sha256(const std::vector<unsigned char> &data);
    std::vector<unsigned char> pbkdf2Sha256(const std::string &password, const std::vector<unsigned char> &salt, uint32_t iterations, size_t length);
    std::string encryptAesGcm(const std::string &plaintext, const std::vector<unsigned char> &key);
    std::string decryptAesGcm(const std::string &packed, const std::vector<unsigned char> &key);
    bool constantTimeEqual(const std::vector<unsigned char> &a, const std::vector<unsigned char> &b);
    std::string sha256Hex(const std::string &text);
    // Best-effort wiping (SecureZeroMemory / OPENSSL_cleanse). Cannot reach copies made elsewhere.
    void secureZero(void *p, size_t n);
    void wipe(std::string &s);
    void wipe(std::vector<unsigned char> &v);
}
