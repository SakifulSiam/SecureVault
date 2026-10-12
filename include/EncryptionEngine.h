#pragma once
#include <string>
#include <vector>

class EncryptionEngine
{
public:
    virtual ~EncryptionEngine() = default;
    virtual std::string encrypt(const std::string &data) = 0;
    virtual std::string decrypt(const std::string &data) = 0;
};

class AesGcmEncryption : public EncryptionEngine
{
    std::vector<unsigned char> key;

public:
    explicit AesGcmEncryption(std::vector<unsigned char> encryptionKey);
    ~AesGcmEncryption() override;
    std::string encrypt(const std::string &data) override;
    std::string decrypt(const std::string &data) override;
};
