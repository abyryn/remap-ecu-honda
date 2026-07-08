#pragma once

// ============================================================
// config.h - Global Configuration
// Honda ECU K-Line Diagnostic Tool
// ============================================================

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

// --- LED ---
#define LED_PIN          2

// --- LittleFS ---
#define FS_BACKUP_DIR    "/backup"
#define FS_LOG_DIR       "/log"
#define FS_CONFIG_DIR    "/config"
#define FS_TEMP_DIR      "/temp"
#define CONFIG_FILE      "/config/settings.json"

// --- OTA ---
#define OTA_USERNAME     "admin"
#define OTA_PASSWORD     "admin123"

// --- Security ---
#define AUTH_USERNAME    "admin"
#define AUTH_PASSWORD    "admin123"
#define SESSION_TIMEOUT  1800   // seconds

// --- WebSocket ---
#define WS_LIVE_INTERVAL_MS  100

// --- Firmware ---
#define FW_VERSION       "1.0.0"
#define FW_BUILD_DATE    __DATE__
#define DEVICE_NAME      "Honda ECU Tool"
