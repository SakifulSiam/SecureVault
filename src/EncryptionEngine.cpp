#include "EncryptionEngine.h"
#include "Crypto.h"
AesGcmEncryption::AesGcmEncryption(std::vector<unsigned char> encryptionKey) : key(std::move(encryptionKey)) {}
AesGcmEncryption::~AesGcmEncryption() { Crypto::wipe(key); }
std::string AesGcmEncryption::encrypt(const std::string &data) { return Crypto::encryptAesGcm(data, key); }
std::string AesGcmEncryption::decrypt(const std::string &data) { return Crypto::decryptAesGcm(data, key); }
