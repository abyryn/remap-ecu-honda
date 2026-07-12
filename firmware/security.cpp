// ============================================================
// security.cpp - Security Manager Implementation
// ============================================================
#include "include/security.h"
#include "include/config.h"
#include "include/logger.h"
#include "include/filesystem.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_system.h>

SecurityManager Security;

// ---- CRC32 lookup table (IEEE 802.3) ----
static const uint32_t crc32_table[256] = {
    0x00000000,0x77073096,0xEE0E612C,0x990951BA,0x076DC419,0x706AF48F,0xE963A535,0x9E6495A3,
    0x0EDB8832,0x79DCB8A4,0xE0D5E91B,0x97D2D988,0x09B64C2B,0x7EB17CBF,0xE7B82D09,0x90BF1D9F,
    0x1DB71064,0x6AB020F2,0xF3B97148,0x84BE41DE,0x1ADAD47D,0x6DDDE4EB,0xF4D4B551,0x83D385C7,
    0x136C9856,0x646BA8C0,0xFD62F97A,0x8A65C9EC,0x14015C4F,0x63066CD9,0xFA0F3D63,0x8D080DF5,
    0x3B6E20C8,0x4C69105E,0xD56041E4,0xA2677172,0x3C03E4D1,0x4B04D447,0xD20D85FD,0xA50AB56B,
    0x35B5A8FA,0x42B2986C,0xDBBBB9D6,0xACBCB9C0,0x32D86CE3,0x45DF5C75,0xDCD60DCF,0xABD13D59,
    0x26D930AC,0x51DE003A,0xC8D75180,0xBFD06116,0x21B4F6B5,0x56B3C423,0xCFBA9599,0xB8BDA50F,
    0x2802B89E,0x5F058808,0xC60CD9B2,0xB10BE924,0x2F6F7C87,0x58684C11,0xC1611DAB,0xB6662D3D,
    0x76DC4190,0x01DB7106,0x98D220BC,0xEFD5102A,0x71B18589,0x06B6B51F,0x9FBFE4A5,0xE8B8D433,
    0x7807C9A2,0x0F00F934,0x9609A88E,0xE10E9818,0x7F6A0D6B,0x086D3D2D,0x91646C97,0xE6635C01,
    0x6B6B51F4,0x1C6C6162,0x856530D8,0xF262004E,0x6C0695ED,0x1B01A57B,0x8208F4C1,0xF50FC457,
    0x65B0D9C6,0x12B7E950,0x8ABEE5FE,0xFDBE9468,0x63CAAB7B,0x14CDBB0D,0x8DCB879B,0xFACC4709,
    0xEE0E612C,0x990951BA,0x076DC419,0x706AF48F,0xE963A535,0x9E6495A3,0x0EDB8832,0x79DCB8A4,
    0xE0D5E91B,0x97D2D988,0x09B64C2B,0x7EB17CBF,0xE7B82D09,0x90BF1D9F,0x1DB71064,0x6AB020F2,
    0xF3B97148,0x84BE41DE,0x1ADAD47D,0x6DDDE4EB,0xF4D4B551,0x83D385C7,0x136C9856,0x646BA8C0,
    0xFD62F97A,0x8A65C9EC,0x14015C4F,0x63066CD9,0xFA0F3D63,0x8D080DF5,0x3B6E20C8,0x4C69105E,
    0xD56041E4,0xA2677172,0x3C03E4D1,0x4B04D447,0xD20D85FD,0xA50AB56B,0x35B5A8FA,0x42B2986C,
    0xDBBBB9D6,0xACBCB9C0,0x32D86CE3,0x45DF5C75,0xDCD60DCF,0xABD13D59,0x26D930AC,0x51DE003A,
    0xC8D75180,0xBFD06116,0x21B4F6B5,0x56B3C423,0xCFBA9599,0xB8BDA50F,0x2802B89E,0x5F058808,
    0xC60CD9B2,0xB10BE924,0x2F6F7C87,0x58684C11,0xC1611DAB,0xB6662D3D,0x76DC4190,0x01DB7106,
    0x98D220BC,0xEFD5102A,0x71B18589,0x06B6B51F,0x9FBFE4A5,0xE8B8D433,0x7807C9A2,0x0F00F934,
    0x9609A88E,0xE10E9818,0x7F6A0D6B,0x086D3D2D,0x91646C97,0xE6635C01,0x6B6B51F4,0x1C6C6162,
    0x856530D8,0xF262004E,0x6C0695ED,0x1B01A57B,0x8208F4C1,0xF50FC457,0x65B0D9C6,0x12B7E950,
    0x8ABEE5FE,0xFDBE9468,0x63CAAB7B,0x14CDBB0D,0x8DCB879B,0xFACC4709,0xEE0E612C,0x990951BA,
    0x076DC419,0x706AF48F,0xE963A535,0x9E6495A3,0x0EDB8832,0x79DCB8A4,0xE0D5E91B,0x97D2D988,
    0x09B64C2B,0x7EB17CBF,0xE7B82D09,0x90BF1D9F,0x1DB71064,0x6AB020F2,0xF3B97148,0x84BE41DE,
    0x1ADAD47D,0x6DDDE4EB,0xF4D4B551,0x83D385C7,0x136C9856,0x646BA8C0,0xFD62F97A,0x8A65C9EC,
    0x14015C4F,0x63066CD9,0xFA0F3D63,0x8D080DF5,0x3B6E20C8,0x4C69105E,0xD56041E4,0xA2677172,
    0x3C03E4D1,0x4B04D447,0xD20D85FD,0xA50AB56B,0x35B5A8FA,0x42B2986C,0xDBBBB9D6,0xACBCB9C0,
    0x32D86CE3,0x45DF5C75,0xDCD60DCF,0xABD13D59,0x26D930AC,0x51DE003A,0xC8D75180,0xBFD06116,
    0x21B4F6B5,0x56B3C423,0xCFBA9599,0xB8BDA50F,0x2802B89E,0x5F058808,0xC60CD9B2,0xB10BE924,
    0x2F6F7C87,0x58684C11,0xC1611DAB,0xB6662D3D,0x76DC4190,0x01DB7106,0x98D220BC,0xEFD5102A
};

// ============================================================
// CRC32
// ============================================================
uint32_t CRC32::calculate(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

uint32_t CRC32::calculate(const std::vector<uint8_t>& data) {
    return calculate(data.data(), data.size());
}

bool CRC32::verify(const uint8_t* data, size_t len, uint32_t expected) {
    return calculate(data, len) == expected;
}

// ============================================================
// AES-128 (simplified CBC-like using ESP32 mbedtls)
// ============================================================
#include <mbedtls/aes.h>

bool AESCipher::encrypt(const uint8_t* key, const uint8_t* input, uint8_t* output, size_t len) {
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);

    if (mbedtls_aes_setkey_enc(&ctx, key, 128) != 0) {
        mbedtls_aes_free(&ctx);
        return false;
    }

    // Pad to 16-byte boundary
    size_t blocks = (len + 15) / 16;
    uint8_t iv[16] = {0};  // zero IV for simplicity

    for (size_t i = 0; i < blocks; i++) {
        uint8_t block[16] = {0};
        size_t copyLen = min((size_t)16, len - i * 16);
        memcpy(block, input + i * 16, copyLen);

        // XOR with previous ciphertext (CBC)
        for (int j = 0; j < 16; j++) block[j] ^= iv[j];

        mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, block, output + i * 16);
        memcpy(iv, output + i * 16, 16);
    }

    mbedtls_aes_free(&ctx);
    return true;
}

bool AESCipher::decrypt(const uint8_t* key, const uint8_t* input, uint8_t* output, size_t len) {
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);

    if (mbedtls_aes_setkey_dec(&ctx, key, 128) != 0) {
        mbedtls_aes_free(&ctx);
        return false;
    }

    size_t blocks = (len + 15) / 16;
    uint8_t iv[16] = {0};

    for (size_t i = 0; i < blocks; i++) {
        uint8_t decrypted[16];
        mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_DECRYPT, input + i * 16, decrypted);

        // XOR with previous ciphertext (CBC)
        for (int j = 0; j < 16; j++) decrypted[j] ^= iv[j];

        memcpy(iv, input + i * 16, 16);
        size_t copyLen = min((size_t)16, len - i * 16);
        memcpy(output + i * 16, decrypted, copyLen);
    }

    mbedtls_aes_free(&ctx);
    return true;
}

// ============================================================
// SecurityManager
// ============================================================
SecurityManager::SecurityManager()
    : _pin(FLASH_PIN_DEFAULT), _pinEnabled(false),
      _pinVerified(false), _boundMAC("") {
    memset(_aesKey, 0, sizeof(_aesKey));
}

void SecurityManager::begin() {
    _loadSettings();
    _generateKey();
    Logger.log(LOG_INFO, "SEC", "Security initialized. PIN=%s Bound=%s",
               _pinEnabled ? "ON" : "OFF",
               _boundMAC.isEmpty() ? "NONE" : _boundMAC.c_str());
}

SecurityResult SecurityManager::verifyPIN(const String& pin) {
    if (!_pinEnabled) {
        _pinVerified = true;
        return SEC_OK;
    }
    if (pin == _pin) {
        _pinVerified = true;
        Logger.log(LOG_INFO, "SEC", "PIN verified OK");
        return SEC_OK;
    }
    _pinVerified = false;
    Logger.log(LOG_WARN, "SEC", "PIN verification failed");
    return SEC_PIN_INVALID;
}

bool SecurityManager::setPIN(const String& oldPin, const String& newPin) {
    if (_pinEnabled && oldPin != _pin) return false;
    _pin = newPin;
    _pinEnabled = (newPin.length() > 0 && newPin != "0000");
    _saveSettings();
    Logger.log(LOG_INFO, "SEC", "PIN %s", _pinEnabled ? "set" : "disabled");
    return true;
}

bool SecurityManager::isDeviceBound() {
    if (_boundMAC.isEmpty()) return true;  // no binding = allow all
    return getDeviceMAC() == _boundMAC;
}

void SecurityManager::bindDevice() {
    _boundMAC = getDeviceMAC();
    _saveSettings();
    Logger.log(LOG_INFO, "SEC", "Device bound: %s", _boundMAC.c_str());
}

bool SecurityManager::encryptBackup(const uint8_t* input, uint8_t* output, size_t len) {
    return AESCipher::encrypt(_aesKey, input, output, len);
}

bool SecurityManager::decryptBackup(const uint8_t* input, uint8_t* output, size_t len) {
    return AESCipher::decrypt(_aesKey, input, output, len);
}

uint32_t SecurityManager::crc32(const uint8_t* data, size_t len) {
    return CRC32::calculate(data, len);
}

uint8_t SecurityManager::checksumKLine(const uint8_t* data, size_t len) {
    uint16_t sum = 0;
    for (size_t i = 0; i < len; i++) sum += data[i];
    return (uint8_t)(sum & 0xFF);
}

String SecurityManager::getDeviceMAC() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

void SecurityManager::_loadSettings() {
    String path = String(FS_CONFIG_DIR) + "/security.json";
    if (!FS_Mgr.exists(path)) return;

    String json = FS_Mgr.readText(path);
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return;

    _pin        = doc["pin"] | FLASH_PIN_DEFAULT;
    _pinEnabled = doc["pinEnabled"] | false;
    _boundMAC   = doc["boundMAC"] | "";
}

void SecurityManager::_saveSettings() {
    StaticJsonDocument<256> doc;
    doc["pin"]        = _pin;
    doc["pinEnabled"] = _pinEnabled;
    doc["boundMAC"]   = _boundMAC;

    String json;
    serializeJson(doc, json);
    FS_Mgr.writeText(String(FS_CONFIG_DIR) + "/security.json", json);
}

void SecurityManager::_generateKey() {
    // Derive AES key from device MAC + PIN (deterministic)
    String seed = getDeviceMAC() + _pin + "HondaECU";
    uint32_t hash = CRC32::calculate((const uint8_t*)seed.c_str(), seed.length());
    for (int i = 0; i < 4; i++) {
        _aesKey[i * 4]     = (hash >> 24) & 0xFF;
        _aesKey[i * 4 + 1] = (hash >> 16) & 0xFF;
        _aesKey[i * 4 + 2] = (hash >> 8)  & 0xFF;
        _aesKey[i * 4 + 3] =  hash         & 0xFF;
        hash = hash * 2654435761UL + i;  // shuffle
    }
}
