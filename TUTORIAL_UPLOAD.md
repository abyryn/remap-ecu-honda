# Tutorial Upload ke ESP32 — Arduino IDE
## Honda ECU K-Line Diagnostic Tool

---

## Daftar Isi

1. [Persyaratan](#1-persyaratan)
2. [Install Arduino IDE](#2-install-arduino-ide)
3. [Tambahkan Board ESP32](#3-tambahkan-board-esp32)
4. [Install Library yang Diperlukan](#4-install-library-yang-diperlukan)
5. [Install Plugin LittleFS Uploader](#5-install-plugin-littlefs-uploader)
6. [Buka Project](#6-buka-project)
7. [Konfigurasi Board Settings](#7-konfigurasi-board-settings)
8. [Upload Firmware](#8-upload-firmware)
9. [Upload File Web (LittleFS)](#9-upload-file-web-littlefs)
10. [Verifikasi](#10-verifikasi)
11. [Troubleshooting](#11-troubleshooting)
12. [Upload OTA (Wireless)](#12-upload-ota-wireless)
13. [Diagram Koneksi Hardware](#13-diagram-koneksi-hardware)

---

## 1. Persyaratan

### Hardware
| Komponen | Keterangan |
|---|---|
| ESP32 DOIT DevKit V1 | Board utama |
| Kabel USB Micro | Upload firmware |
| Optocoupler 4N25 | Interface K-Line |
| NPN Transistor (BC547/2N2222) | Driver K-Line TX |
| Resistor 1kΩ, 10kΩ, 4.7kΩ | Pull-up, pembagi tegangan |
| LED + Resistor 330Ω | Indikator status |
| Buck Converter 12V→5V | Sumber daya dari OBD |

### Software
| Software | Versi | Link |
|---|---|---|
| Arduino IDE | **2.3.x** (direkomendasikan) | https://www.arduino.cc/en/software |
| ESP32 Arduino Core | **3.x** | via Board Manager |
| ESPAsyncWebServer | Latest | GitHub |
| AsyncTCP | Latest | GitHub |
| ArduinoJson | **7.x** | Library Manager |
| LittleFS Uploader Plugin | 3.x | GitHub |

---

## 2. Install Arduino IDE

1. Buka https://www.arduino.cc/en/software
2. Download **Arduino IDE 2.3.x** sesuai OS kamu (Windows)
3. Jalankan installer, ikuti langkah default
4. Setelah selesai, buka Arduino IDE

---

## 3. Tambahkan Board ESP32

### Langkah 3.1 — Tambahkan URL Board Manager

1. Buka **File → Preferences** (atau `Ctrl + ,`)
2. Cari kolom **"Additional boards manager URLs"**
3. Klik ikon di sebelah kanan kolom
4. Tambahkan URL berikut (satu baris):

```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```

5. Klik **OK**

### Langkah 3.2 — Install ESP32 Core

1. Buka **Tools → Board → Boards Manager** (atau `Ctrl + Shift + B`)
2. Ketik **`esp32`** di kolom pencarian
3. Pilih **"esp32 by Espressif Systems"**
4. Pilih versi terbaru (minimal **3.0.x**)
5. Klik **Install** — tunggu proses selesai (±100–200 MB)

> ⏳ Proses install membutuhkan koneksi internet dan memakan waktu 5–15 menit.

---

## 4. Install Library yang Diperlukan

### Library 1: ArduinoJson

1. Buka **Sketch → Include Library → Manage Libraries** (`Ctrl + Shift + I`)
2. Ketik `ArduinoJson`
3. Pilih **"ArduinoJson by Benoit Blanchon"**
4. Pilih versi **7.x.x**
5. Klik **Install**

### Library 2: ESPAsyncWebServer + AsyncTCP

Library ini tidak tersedia di Library Manager, harus install manual via ZIP:

**Download:**
- AsyncTCP: https://github.com/me-no-dev/AsyncTCP/archive/refs/heads/master.zip
- ESPAsyncWebServer: https://github.com/me-no-dev/ESPAsyncWebServer/archive/refs/heads/master.zip

**Install:**
1. Buka **Sketch → Include Library → Add .ZIP Library...**
2. Pilih file `AsyncTCP-master.zip`
3. Ulangi untuk `ESPAsyncWebServer-master.zip`

> ✅ Setelah install, cek di **Sketch → Include Library** — harus ada `AsyncTCP` dan `ESPAsyncWebServer`.

---

## 5. Install Plugin LittleFS Uploader

Plugin ini digunakan untuk mengupload file web (HTML/CSS/JS) ke memori ESP32.

### Untuk Arduino IDE 2.x

1. Download plugin dari:
   https://github.com/lorol/arduino-esp32littlefs-plugin/releases
   
   https://github.com/earlephilhower/arduino-littlefs-upload/releases
   
   Download file: `esp32littlefs.vsix` (Arduino IDE 2.x) atau `esp32_littlefs.jar` (IDE 1.8.x)

2. **Untuk Arduino IDE 2.x:**
   - Buka Arduino IDE
   - Buka menu **Help → About Arduino IDE** untuk pastikan versi 2.x
   - Buka Terminal/CMD, jalankan:
     ```
     arduino-ide --install-plugin esp32littlefs.vsix
     ```
   - Atau drag-drop file `.vsix` ke window Arduino IDE

3. **Untuk Arduino IDE 1.8.x:**
   - Temukan folder `tools` di Arduino sketchbook:
     ```
     C:\Users\[USERNAME]\Documents\Arduino\tools\
     ```
   - Buat folder baru: `ESP32LittleFS\tool\`
   - Copy file `.jar` ke dalam folder tersebut
   - Restart Arduino IDE
   - Akan muncul menu baru: **Tools → ESP32 LittleFS Data Upload**

---

## 6. Buka Project

### Struktur Folder yang Diperlukan Arduino IDE

Arduino IDE mengharuskan file `.ino` ada di folder dengan nama yang sama. Struktur yang sudah disiapkan:

```
HondaECUTool/
├── HondaECUTool.ino     ← Buka file ini di Arduino IDE
└── data/                ← Folder untuk LittleFS (web files)
    ├── web/
    │   ├── index.html
    │   ├── css/
    │   └── js/
    ├── backup/
    ├── log/
    ├── config/
    └── temp/
```

### Langkah Copy File

1. Buka Windows Explorer
2. Navigasi ke: `C:\Users\A D M I N\Downloads\coding\remap\`
3. **Copy semua file `.cpp` dan folder `include`** dari `firmware/` ke dalam `HondaECUTool/`:
   ```
   firmware/main.cpp        → HondaECUTool/main.cpp
   firmware/kline.cpp       → HondaECUTool/kline.cpp
   firmware/ecu.cpp         → HondaECUTool/ecu.cpp
   firmware/wifi.cpp        → HondaECUTool/wifi.cpp
   firmware/logger.cpp      → HondaECUTool/logger.cpp
   firmware/backup.cpp      → HondaECUTool/backup.cpp
   firmware/filesystem.cpp  → HondaECUTool/filesystem.cpp
   firmware/ota.cpp         → HondaECUTool/ota.cpp
   firmware/api.cpp         → HondaECUTool/api.cpp
   firmware/webserver.cpp   → HondaECUTool/webserver.cpp
   firmware/settings.cpp    → HondaECUTool/settings.cpp
   firmware/utils.cpp       → HondaECUTool/utils.cpp
   firmware/config.cpp      → HondaECUTool/config.cpp
   firmware/include/        → HondaECUTool/include/
   ```

4. **Copy web files ke folder data:**
   ```
   web/  →  HondaECUTool/data/web/
   ```
   Sehingga struktur menjadi:
   ```
   HondaECUTool/data/web/index.html
   HondaECUTool/data/web/css/main.css
   HondaECUTool/data/web/js/api.js
   ... dst
   ```

5. Buka Arduino IDE → **File → Open** → pilih `HondaECUTool.ino`

---

## 7. Konfigurasi Board Settings

Setelah project terbuka, konfigurasi board via menu **Tools**:

| Setting | Nilai |
|---|---|
| **Board** | `ESP32 Dev Module` atau `DOIT ESP32 DEVKIT V1` |
| **Upload Speed** | `921600` |
| **CPU Frequency** | `240MHz (WiFi/BT)` |
| **Flash Frequency** | `80MHz` |
| **Flash Mode** | `QIO` |
| **Flash Size** | `4MB (32Mb)` |
| **Partition Scheme** | `Default 4MB with spiffs` atau `Huge APP (3MB No OTA/1MB SPIFFS)` |
| **Core Debug Level** | `Info` |
| **PSRAM** | `Disabled` |
| **Port** | `COMx` (lihat langkah di bawah) |

### Cara Menemukan COM Port

1. Sambungkan ESP32 ke komputer via USB
2. Buka **Device Manager** (klik kanan Start → Device Manager)
3. Expand **Ports (COM & LPT)**
4. Cari **"Silicon Labs CP210x USB to UART Bridge"** atau **"CH340"**
5. Catat nomor COM, contoh: `COM3`
6. Pilih di **Tools → Port → COM3**

> 💡 Jika port tidak muncul, install driver:
> - CP2102: https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers
> - CH340: https://www.wch-ic.com/downloads/CH341SER_EXE.html

---

## 8. Upload Firmware

### Langkah-langkah:

1. Pastikan kabel USB tersambung ke ESP32
2. Pilih port yang benar di **Tools → Port**
3. Klik tombol **Upload** (→ panah ke kanan) atau tekan `Ctrl + U`
4. Arduino IDE akan mulai **Compiling...**

### Proses yang akan terlihat di console:

```
Compiling sketch...
...
Linking everything together...
...
esptool.py v4.x.x
Found 1 serial port
Serial port COM3
Connecting...........
Chip is ESP32-D0WD-V3 (revision v3.1)
Features: WiFi, BT, Dual Core, 240MHz, VRef calibration in efuse
...
Writing at 0x00001000... (2 %)
Writing at 0x00008000... (5 %)
...
Writing at 0x000f4000... (100 %)
Wrote 1234567 bytes...
Hash of data verified.
Leaving...
Hard resetting via RTS pin...
Done uploading.
```

> ✅ Jika muncul **"Done uploading"** berarti sukses.

### Jika Gagal Upload (tidak terdeteksi):

Beberapa ESP32 perlu ditekan tombol **BOOT** saat proses connecting:
1. Klik Upload di Arduino IDE
2. Tunggu sampai muncul teks `"Connecting........___...."`
3. **Tahan tombol BOOT** di board ESP32
4. Lepas setelah proses connecting berhasil

---

## 9. Upload File Web (LittleFS)

File HTML, CSS, dan JS perlu diupload terpisah ke partisi LittleFS.

### Pastikan folder `data` sudah benar:

```
HondaECUTool/
└── data/
    └── web/
        ├── index.html
        ├── css/
        │   └── main.css
        └── js/
            ├── api.js
            ├── dashboard.js
            ├── live.js
            ├── dtc.js
            ├── backup.js
            ├── terminal.js
            └── settings.js
```

### Upload via Plugin:

1. Tutup **Serial Monitor** jika sedang terbuka
2. Pilih **Tools → ESP32 LittleFS Data Upload**
3. Tunggu proses upload selesai:

```
[LittleFS] Uploading files...
[LittleFS] /web/index.html -> OK
[LittleFS] /web/css/main.css -> OK
[LittleFS] /web/js/api.js -> OK
...
[LittleFS] Upload done!
```

> ⚠️ Jika menu tidak muncul, pastikan plugin sudah terinstall dengan benar (lihat langkah 5).

### Alternatif: Upload via esptool.py (command line)

Jika plugin tidak berfungsi, bisa menggunakan esptool manual:

```bash
# Install mklittlefs
pip install esptool

# Build LittleFS image
mklittlefs -c data/ -s 0x100000 littlefs.bin

# Upload (ganti COM3 dengan port kamu)
esptool.py --chip esp32 --port COM3 --baud 921600 \
  write_flash 0x290000 littlefs.bin
```

> Offset `0x290000` berlaku untuk partisi default 4MB. Sesuaikan dengan `partition scheme` yang dipilih.

---

## 10. Verifikasi

### Cek via Serial Monitor:

1. Buka **Tools → Serial Monitor** (`Ctrl + Shift + M`)
2. Set baud rate ke **115200**
3. Reset ESP32 (tekan tombol EN/RESET)

Output yang benar:

```
=== Honda ECU Tool v1.0.0 ===
[Logger] Started
[FS] LittleFS mounted OK
[Settings] No config file, using defaults
[WiFi] AP started: SSID=Honda ECU Tool IP=192.168.4.1
[WiFi] mDNS: http://honda-ecu.local
[WebSrv] Server started on port 80
[Main] Boot complete. IP: 192.168.4.1
[Main] Free heap: 256000 bytes
[Main] SSID: Honda ECU Tool  Pass: 12345678
```

### Koneksi ke Web Interface:

1. Buka **WiFi settings** di HP atau laptop
2. Sambungkan ke WiFi: **`Honda ECU Tool`**
3. Password: **`12345678`**
4. Buka browser, ketik: **`http://192.168.4.1`**
5. Dashboard akan muncul ✅

Atau bisa juga via mDNS:
```
http://honda-ecu.local
```

---

## 11. Troubleshooting

### ❌ Error: "esptool.py: error: argument --chip: invalid choice"
**Solusi:** Update ESP32 Arduino Core ke versi terbaru (3.x)

---

### ❌ Error: "fatal error: ESPAsyncWebServer.h: No such file or directory"
**Solusi:** Install library ESPAsyncWebServer dan AsyncTCP (lihat langkah 4)

---

### ❌ Error: "Multiple libraries were found for..."
**Solusi:** Pastikan tidak ada duplikat library. Hapus satu versi via:
`Sketch → Include Library → Manage Libraries`

---

### ❌ Error saat compile: "undefined reference to..."
**Solusi:** Pastikan semua file `.cpp` sudah ada di folder `HondaECUTool/`

---

### ❌ LittleFS tidak mount / web tidak bisa diakses
**Solusi:**
1. Pastikan folder `data/` ada di dalam folder `HondaECUTool/`
2. Upload ulang via **Tools → ESP32 LittleFS Data Upload**
3. Cek Partition Scheme — gunakan yang ada spiffs/littlefs

---

### ❌ Port COM tidak terdeteksi
**Solusi:**
1. Ganti kabel USB (beberapa kabel hanya charging, tidak data)
2. Install driver CP2102 atau CH340 sesuai chip USB ESP32 kamu
3. Coba port USB berbeda

---

### ❌ Upload gagal, stuck di "Connecting...."
**Solusi:**
1. Tahan tombol BOOT saat `"Connecting..."` muncul
2. Atau tambahkan kapasitor 10µF antara EN dan GND
3. Pastikan baud upload tidak terlalu tinggi — coba set ke `460800`

---

### ❌ Halaman web tidak load (ERR_CONNECTION_REFUSED)
**Solusi:**
1. Cek Serial Monitor — pastikan WebServer started
2. Pastikan sudah konek ke WiFi `Honda ECU Tool`
3. Pastikan LittleFS upload berhasil (file `index.html` harus ada)
4. Coba akses `http://192.168.4.1/api/status` — harus return JSON

---

## 12. Upload OTA (Wireless)

Setelah firmware pertama kali diupload via USB, update berikutnya bisa dilakukan via WiFi (OTA).

### Via Web Interface:
1. Sambungkan ke WiFi `Honda ECU Tool`
2. Buka `http://192.168.4.1`
3. Navigasi ke **Settings → Firmware OTA Update**
4. Klik **Choose File** → pilih file `.bin` firmware baru
5. Klik **Upload Firmware**
6. Tunggu proses upload dan reboot otomatis

### Cara Generate File .bin:
Di Arduino IDE:
1. **Sketch → Export Compiled Binary** (`Ctrl + Alt + S`)
2. File `.bin` akan tersimpan di folder `HondaECUTool/build/`

### Via Arduino IDE (OTA langsung):
1. Pastikan ESP32 sudah konek ke WiFi dan OTA aktif
2. Di **Tools → Port** — akan muncul port network seperti `honda-ecu.local at 192.168.4.1`
3. Pilih port tersebut
4. Upload seperti biasa

---

## 13. Diagram Koneksi Hardware

### ESP32 Pin Assignment

```
ESP32 DOIT V1
┌──────────────────────────────┐
│  GPIO17 (TX) ──────────────────→ 4N25 Pin 1 (Anode LED)
│  GPIO16 (RX) ←──────────────── 4N25 Pin 5 (Collector)
│  GPIO34 (ADC) ←─── Voltage Divider ─── OBD +12V
│  GPIO2  (LED) ──── 330Ω ──── LED ──── GND
│  3.3V  ──────────────────────── Pull-up 4.7kΩ pada K-Line RX
│  GND   ──────────────────────── GND
└──────────────────────────────┘
```

### Skema 4N25 Interface

```
ESP32 TX (GPIO17)
        │
       1kΩ
        │
     ┌──┴──┐
     │4N25 │  Pin 1 (+) Anode
     │     │  Pin 2 (-) Cathode ── GND
     │     │
     │     │  Pin 4 Emitter ── GND
     │     │  Pin 5 Collector ─── Pull-up 4.7kΩ → +5V
     └─────┘           │
                       └──── K-Line ──── ECU OBD Pin
                       │
                       └──── ESP32 RX (GPIO16) via Voltage Divider
                             (Divider: 10kΩ / 20kΩ, 5V → 3.3V)
```

### Voltage Divider untuk RX (5V → 3.3V)

```
K-Line ─── 10kΩ ─── ESP32 GPIO16 (RX)
                │
               20kΩ
                │
               GND
```

> ⚠️ Penting: ESP32 tidak toleran terhadap 5V pada pin GPIO.
> Gunakan voltage divider atau level shifter untuk jalur RX.

### Voltage Monitor

```
OBD +12V ─── 33kΩ ─┬─── 10kΩ ─── GND
                   │
              ESP32 GPIO34 (ADC)
```

Rasio divider: 10/(33+10) = 0.233 → maks 14.3V masuk, ADC baca ≤ 3.3V

---

## Ringkasan Checklist Upload

```
[ ] Arduino IDE 2.3.x terinstall
[ ] ESP32 Core 3.x terinstall via Boards Manager
[ ] Library ArduinoJson 7.x terinstall
[ ] Library ESPAsyncWebServer + AsyncTCP terinstall (ZIP)
[ ] Plugin LittleFS Uploader terinstall
[ ] Semua .cpp files ada di folder HondaECUTool/
[ ] Folder data/web/ berisi index.html, css/, js/
[ ] Board: ESP32 Dev Module dipilih
[ ] Port: COMx dipilih dengan benar
[ ] Upload Firmware: Ctrl+U → Done uploading ✓
[ ] Upload LittleFS: Tools → ESP32 LittleFS Data Upload ✓
[ ] Serial Monitor menampilkan "Boot complete"
[ ] Konek WiFi "Honda ECU Tool", buka 192.168.4.1 ✓
```

---

*Honda ECU K-Line Diagnostic Tool — ESP32 DOIT V1*
*Versi 1.0.0*


Copy semua .cpp + folder include/ dari firmware/ → HondaECUTool/
Copy web/ → HondaECUTool/data/web/
Buka HondaECUTool.ino di Arduino IDE
Install semua library (lihat tutorial)
Set board: ESP32 Dev Module, port COMx
Ctrl+U — upload firmware
Tools → ESP32 LittleFS Data Upload — upload web files
Konek WiFi Honda ECU Tool → buka http://192.168.4.1