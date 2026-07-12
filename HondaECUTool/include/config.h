#pragma once

// ============================================================
// config.h - Global Configuration
// Honda ECU Remapper Mobile v2.0.0
// ============================================================

// --- Firmware ---
#define FW_VERSION       "2.0.0"
#define FW_BUILD_DATE    __DATE__
#define DEVICE_NAME      "Honda ECU Remapper"

// --- WiFi ---
#define WIFI_SSID        "Honda ECU Tool"
#define WIFI_PASSWORD    "12345678"
#define WIFI_CHANNEL     1
#define WIFI_MAX_CLIENTS 4
#define AP_IP            "192.168.4.1"
#define AP_GATEWAY       "192.168.4.1"
#define AP_SUBNET        "255.255.255.0"

// --- Web Server ---
#define HTTP_PORT        80
#define WS_PATH          "/ws"

// --- K-Line UART ---
#define KLINE_TX_PIN     17
#define KLINE_RX_PIN     16
#define KLINE_UART_NUM   UART_NUM_2   // Serial2
#define KLINE_BAUD       10400
#define KLINE_TIMEOUT_MS 1000
#define KLINE_RETRY_MAX  3

// --- Voltage Monitor ---
#define VBAT_PIN         34
#define VBAT_DIVIDER     4.0f   // voltage divider ratio
#define VBAT_REF         3.3f
#define ADC_RESOLUTION   4095.0f
#define VBAT_LOW_WARN    11.0f  // warn if battery below this

// --- LED ---
#define LED_PIN          2

// --- LittleFS ---
#define FS_BACKUP_DIR    "/backup"
#define FS_LOG_DIR       "/log"
#define FS_CONFIG_DIR    "/config"
#define FS_TEMP_DIR      "/temp"
#define FS_MAPS_DIR      "/maps"
#define CONFIG_FILE      "/config/settings.json"

// --- OTA ---
#define OTA_USERNAME     "admin"
#define OTA_PASSWORD     "admin123"

// --- Security ---
#define AUTH_USERNAME    "admin"
#define AUTH_PASSWORD    "admin123"
#define SESSION_TIMEOUT  1800   // seconds
#define FLASH_PIN_DEFAULT "0000"
#define AES_KEY_SIZE     16     // AES-128

// --- WebSocket ---
#define WS_LIVE_INTERVAL_MS  100

// --- Flash Operations ---
#define FLASH_BLOCK_SIZE     64    // bytes per transfer block
#define FLASH_MAX_SIZE       262144 // 256KB max flash size
#define FLASH_EEPROM_SIZE    256   // default EEPROM size
#define FLASH_RETRY_MAX      5     // retries per block
#define FLASH_TIMEOUT_MS     5000  // timeout per block operation
#define FLASH_KEEPALIVE_MS   2000  // keep-alive interval during flash
#define FLASH_VERIFY_CRC     true  // always verify after write
#define FLASH_AUTO_BACKUP    true  // auto backup before write
#define FLASH_CHECKPOINT_INTERVAL 32 // save checkpoint every N blocks

// --- Map Editor ---
#define MAP_MAX_ROWS     32
#define MAP_MAX_COLS     32
#define MAP_MAX_TABLES   20    // max map tables per ECU
#define MAP_UNDO_STACK   50    // max undo levels

// --- Datalogger ---
#define DATALOG_MAX_CHANNELS 16
#define DATALOG_SAMPLE_RATE  20   // Hz
#define DATALOG_MAX_DURATION 3600 // seconds (1 hour)

// --- Recovery ---
#define RECOVERY_MAX_ATTEMPTS 5
#define RECOVERY_DELAY_MS     2000
