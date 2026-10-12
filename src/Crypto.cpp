#include "Crypto.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <sstream>
#include <iomanip>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#else
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <openssl/crypto.h>
#endif

namespace
{
    std::string b64(const unsigned char *data, size_t len)
    {
        static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        for (size_t i = 0; i < len; i += 3)
        {
            unsigned v = data[i] << 16;
            if (i + 1 < len)
                v |= data[i + 1] << 8;
            if (i + 2 < len)
                v |= data[i + 2];
            out.push_back(table[(v >> 18) & 63]);
            out.push_back(table[(v >> 12) & 63]);
            out.push_back(i + 1 < len ? table[(v >> 6) & 63] : '=');
            out.push_back(i + 2 < len ? table[v & 63] : '=');
        }
        return out;
    }
    std::vector<unsigned char> unb64(const std::string &s)
    {
        static const std::string table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::vector<unsigned char> out;
        int val = 0, bits = -8;
        for (unsigned char c : s)
        {
            if (c == '=')
                break;
            auto p = table.find(c);
            if (p == std::string::npos)
                continue;
            val = ((val << 6) + (int)p) & 0xFFFFFF; // masked: avoids signed-overflow UB (P1-8)
            bits += 6;
            if (bits >= 0)
            {
                out.push_back((unsigned char)((val >> bits) & 0xFF));
                bits -= 8;
            }
        }
        return out;
    }
}

namespace Crypto
{
    std::vector<unsigned char> randomBytes(size_t n)
    {
        std::vector<unsigned char> out(n);
#ifdef _WIN32
        BCRYPT_ALG_HANDLE h = nullptr;
        if (BCryptOpenAlgorithmProvider(&h, BCRYPT_RNG_ALGORITHM, nullptr, 0) != 0)
            throw std::runtime_error("RNG initialization failed");
        if (BCryptGenRandom(h, out.data(), static_cast<ULONG>(n), 0) != 0)
        {
            BCryptCloseAlgorithmProvider(h, 0);
            throw std::runtime_error("Random generation failed");
        }
        BCryptCloseAlgorithmProvider(h, 0);
#else
        if (RAND_bytes(out.data(), static_cast<int>(n)) != 1)
            throw std::runtime_error("Random generation failed");
#endif
        return out;
    }
    std::string hexEncode(const std::vector<unsigned char> &data)
    {
        std::ostringstream o;
        o << std::hex << std::setfill('0');
        for (auto c : data)
            o << std::setw(2) << (int)c;
        return o.str();
    }
    std::vector<unsigned char> hexDecode(const std::string &hex)
    {
        if (hex.size() % 2)
            throw std::runtime_error("Invalid hex");
        std::vector<unsigned char> out;
        for (size_t i = 0; i < hex.size(); i += 2)
            out.push_back((unsigned char)std::stoi(hex.substr(i, 2), nullptr, 16));
        return out;
    }
    std::string base64Encode(const std::vector<unsigned char> &data) { return b64(data.data(), data.size()); }
    std::vector<unsigned char> base64Decode(const std::string &text) { return unb64(text); }
    std::vector<unsigned char> sha256(const std::vector<unsigned char> &data)
    {
        std::vector<unsigned char> out(32);
#ifdef _WIN32
        BCRYPT_ALG_HANDLE a = nullptr;
        BCRYPT_HASH_HANDLE h = nullptr;
        DWORD obj = 0, cb = 0;
        if (BCryptOpenAlgorithmProvider(&a, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
            throw std::runtime_error("SHA-256 unavailable");
        BCryptGetProperty(a, BCRYPT_OBJECT_LENGTH, (PUCHAR)&obj, sizeof(obj), &cb, 0);
        std::vector<unsigned char> ho(obj);
        BCryptCreateHash(a, &h, ho.data(), obj, nullptr, 0, 0);
        BCryptHashData(h, (PUCHAR)data.data(), (ULONG)data.size(), 0);
        BCryptFinishHash(h, out.data(), 32, 0);
        BCryptDestroyHash(h);
        BCryptCloseAlgorithmProvider(a, 0);
#else
        SHA256(data.data(), data.size(), out.data());
#endif
        return out;
    }
    std::vector<unsigned char> pbkdf2Sha256(const std::string &password, const std::vector<unsigned char> &salt, uint32_t iterations, size_t length)
    {
        std::vector<unsigned char> out(length);
#ifdef _WIN32
        BCRYPT_ALG_HANDLE a = nullptr;
        if (BCryptOpenAlgorithmProvider(&a, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) != 0)
            throw std::runtime_error("PBKDF2 unavailable");
        if (BCryptDeriveKeyPBKDF2(a, (PUCHAR)password.data(), (ULONG)password.size(), (PUCHAR)salt.data(), (ULONG)salt.size(), iterations, out.data(), (ULONG)length, 0) != 0)
        {
            BCryptCloseAlgorithmProvider(a, 0);
            throw std::runtime_error("Key derivation failed");
        }
        BCryptCloseAlgorithmProvider(a, 0);
#else
        if (PKCS5_PBKDF2_HMAC(password.data(), (int)password.size(), salt.data(), (int)salt.size(), (int)iterations, EVP_sha256(), (int)length, out.data()) != 1)
            throw std::runtime_error("Key derivation failed");
#endif
        return out;
    }
    std::string encryptAesGcm(const std::string &plaintext, const std::vector<unsigned char> &key)
    {
        if (key.size() != 32)
            throw std::runtime_error("AES-256 requires a 32-byte key");
        auto nonce = randomBytes(12);
        std::vector<unsigned char> ct(plaintext.size());
        std::vector<unsigned char> tag(16);
#ifdef _WIN32
        BCRYPT_ALG_HANDLE a = nullptr;
        BCRYPT_KEY_HANDLE k = nullptr;
        DWORD obj = 0, cb = 0;
        if (BCryptOpenAlgorithmProvider(&a, BCRYPT_AES_ALGORITHM, nullptr, 0) != 0)
            throw std::runtime_error("AES unavailable");
        BCryptSetProperty(a, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_GCM, sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
        BCryptGetProperty(a, BCRYPT_OBJECT_LENGTH, (PUCHAR)&obj, sizeof(obj), &cb, 0);
        std::vector<unsigned char> ko(obj);
        if (BCryptGenerateSymmetricKey(a, &k, (PUCHAR)ko.data(), obj, (PUCHAR)key.data(), (ULONG)key.size(), 0) != 0)
            throw std::runtime_error("AES key setup failed");
        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
        BCRYPT_INIT_AUTH_MODE_INFO(info);
        info.pbNonce = nonce.data();
        info.cbNonce = (ULONG)nonce.size();
        info.pbTag = tag.data();
        info.cbTag = (ULONG)tag.size();
        ULONG out = 0;
        if (BCryptEncrypt(k, (PUCHAR)plaintext.data(), (ULONG)plaintext.size(), &info, nullptr, 0, ct.data(), (ULONG)ct.size(), &out, 0) != 0)
        {
            BCryptDestroyKey(k);
            BCryptCloseAlgorithmProvider(a, 0);
            throw std::runtime_error("Encryption failed");
        }
        BCryptDestroyKey(k);
        BCryptCloseAlgorithmProvider(a, 0);
#else
        EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
        if (!ctx)
            throw std::runtime_error("AES context failed");
        int len = 0, outlen = 0;
        if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 || EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) != 1 || EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), nonce.data()) != 1 || EVP_EncryptUpdate(ctx, ct.data(), &len, (const unsigned char *)plaintext.data(), (int)plaintext.size()) != 1)
        {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("Encryption failed");
        }
        outlen = len;
        if (EVP_EncryptFinal_ex(ctx, ct.data() + outlen, &len) != 1)
        {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("Encryption failed");
        }
        outlen += len;
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag.data());
        EVP_CIPHER_CTX_free(ctx);
#endif
        std::vector<unsigned char> packed;
        packed.insert(packed.end(), nonce.begin(), nonce.end());
        packed.insert(packed.end(), tag.begin(), tag.end());
        packed.insert(packed.end(), ct.begin(), ct.end());
        return base64Encode(packed);
    }
    std::string decryptAesGcm(const std::string &packed, const std::vector<unsigned char> &key)
    {
        auto data = base64Decode(packed);
        if (data.size() < 28 || key.size() != 32)
            throw std::runtime_error("Invalid encrypted vault data");
        std::vector<unsigned char> nonce(data.begin(), data.begin() + 12), tag(data.begin() + 12, data.begin() + 28), ct(data.begin() + 28, data.end()), pt(ct.size());
#ifdef _WIN32
        BCRYPT_ALG_HANDLE a = nullptr;
        BCRYPT_KEY_HANDLE k = nullptr;
        DWORD obj = 0, cb = 0;
        if (BCryptOpenAlgorithmProvider(&a, BCRYPT_AES_ALGORITHM, nullptr, 0) != 0)
            throw std::runtime_error("AES unavailable");
        BCryptSetProperty(a, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_GCM, sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
        BCryptGetProperty(a, BCRYPT_OBJECT_LENGTH, (PUCHAR)&obj, sizeof(obj), &cb, 0);
        std::vector<unsigned char> ko(obj);
        if (BCryptGenerateSymmetricKey(a, &k, ko.data(), obj, (PUCHAR)key.data(), (ULONG)key.size(), 0) != 0)
            throw std::runtime_error("AES key setup failed");
        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
        BCRYPT_INIT_AUTH_MODE_INFO(info);
        info.pbNonce = nonce.data();
        info.cbNonce = (ULONG)nonce.size();
        info.pbTag = tag.data();
        info.cbTag = (ULONG)tag.size();
        ULONG out = 0;
        NTSTATUS st = BCryptDecrypt(k, ct.data(), (ULONG)ct.size(), &info, nullptr, 0, pt.data(), (ULONG)pt.size(), &out, 0);
        BCryptDestroyKey(k);
        BCryptCloseAlgorithmProvider(a, 0);
        if (st != 0)
            throw std::runtime_error("Vault authentication failed or password is incorrect");
#else
        EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
        if (!ctx)
            throw std::runtime_error("AES context failed");
        int len = 0, outlen = 0;
        if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 || EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) != 1 || EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), nonce.data()) != 1 || EVP_DecryptUpdate(ctx, pt.data(), &len, ct.data(), (int)ct.size()) != 1)
        {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("Decryption failed");
        }
        outlen = len;
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, tag.data());
        int ok = EVP_DecryptFinal_ex(ctx, pt.data() + outlen, &len);
        EVP_CIPHER_CTX_free(ctx);
        if (ok != 1)
            throw std::runtime_error("Vault authentication failed or password is incorrect");
        outlen += len;
#endif
        return std::string((char *)pt.data(), pt.size());
    }
    bool constantTimeEqual(const std::vector<unsigned char> &a, const std::vector<unsigned char> &b)
    {
        if (a.size() != b.size())
            return false;
        unsigned char d = 0;
        for (size_t i = 0; i < a.size(); ++i)
            d |= a[i] ^ b[i];
        return d == 0;
    }
    std::string sha256Hex(const std::string &text) { return hexEncode(sha256(std::vector<unsigned char>(text.begin(), text.end()))); }
    void secureZero(void *p, size_t n)
    {
        if (!p || !n)
            return;
#ifdef _WIN32
        SecureZeroMemory(p, n);
#else
        OPENSSL_cleanse(p, n);
#endif
    }
    void wipe(std::string &s)
    {
        if (!s.empty())
            secureZero(&s[0], s.size());
        s.clear();
    }
    void wipe(std::vector<unsigned char> &v)
    {
        if (!v.empty())
            secureZero(v.data(), v.size());
        v.clear();
    }
}
