// ============================================================
// HondaECUTool.ino
// Honda ECU K-Line Diagnostic Tool
// Board  : ESP32 DOIT DevKit V1
// Author : Honda ECU Tool Project
//
// CARA BUKA DI ARDUINO IDE:
//   File → Open → pilih file HondaECUTool.ino ini
//   Semua .cpp dan .h otomatis ter-include karena ada
//   di folder yang sama (HondaECUTool/).
//
// CATATAN:
//   File ini hanya entry point untuk Arduino IDE.
//   Semua implementasi ada di file .cpp terpisah.
//   Jika menggunakan PlatformIO, gunakan platformio.ini
//   di root folder dan arahkan src_dir = firmware/
// ============================================================

// --- Semua header di-include dari main.cpp ---
// Arduino IDE akan otomatis compile semua .cpp dalam folder ini.

// Entry points
void setup();
void loop();
