#pragma once
// ============================================================
// security.h - Security Manager
// PIN protection, AES encryption, CRC32, device binding
// ============================================================
#include <Arduino.h>
#include <vector>

// --- CRC32 ---
class CRC32 {
public:
    static uint32_t calculate(const uint8_t* data, size_t len);
    static uint32_t calculate(const std::vector<uint8_t>& data);
    static bool     verify(const uint8_t* data, size_t len, uint32_t expected);
};

// --- AES-128 encryption (simple XOR-based for ESP32 constraints) ---
class AESCipher {
public:
    static bool encrypt(const uint8_t* key, const uint8_t* input, uint8_t* output, size_t len);
    static bool decrypt(const uint8_t* key, const uint8_t* input, uint8_t* output, size_t len);
};

// --- Security Manager ---
enum SecurityResult {
    SEC_OK = 0,
    SEC_PIN_REQUIRED,
    SEC_PIN_INVALID,
    SEC_DEVICE_NOT_BOUND,
    SEC_AUTH_FAILED,
    SEC_ENCRYPTION_ERROR
};

class SecurityManager {
public:
    SecurityManager();

    /**
     * @brief Initialize security with stored settings
     */
    void begin();

    /**
     * @brief Verify flash PIN before write operations
     */
    SecurityResult verifyPIN(const String& pin);

    /**
     * @brief Set new flash PIN
     */
    bool setPIN(const String& oldPin, const String& newPin);

    /**
     * @brief Check if device MAC is in allowed list
     */
    bool isDeviceBound();

    /**
     * @brief Bind current device MAC
     */
    void bindDevice();

    /**
     * @brief Encrypt a backup file buffer
     */
    bool encryptBackup(const uint8_t* input, uint8_t* output, size_t len);

    /**
     * @brief Decrypt a backup file buffer
     */
    bool decryptBackup(const uint8_t* input, uint8_t* output, size_t len);

    /**
     * @brief Calculate CRC32 of data
     */
    uint32_t crc32(const uint8_t* data, size_t len);

    /**
     * @brief Calculate checksum (sum mod 256) for K-Line frames
     */
    static uint8_t checksumKLine(const uint8_t* data, size_t len);

    /**
     * @brief Get device MAC as string
     */
    String getDeviceMAC();

    // Status
    bool isPINEnabled()  const { return _pinEnabled; }
    bool isPINVerified() const { return _pinVerified; }

private:
    String  _pin;
    bool    _pinEnabled;
    bool    _pinVerified;
    String  _boundMAC;
    uint8_t _aesKey[16];

    void _loadSettings();
    void _saveSettings();
    void _generateKey();
};

extern SecurityManager Security;
