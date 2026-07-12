# Honda ECU Tool — APK Android

> 🏍 Aplikasi Android untuk diagnosa dan remapping ECU Honda via K-Line (ESP32 WiFi)

---

## 📱 Cara Install APK

1. **Download** file `HondaECUTool-v1.0.0-debug.apk`
2. **Transfer ke HP** via USB, WhatsApp, atau Google Drive
3. **Buka file manager** di HP → tap file APK
4. Jika muncul "Unknown Sources" → **Settings → Security → Install Unknown Apps → Aktifkan**
5. Tap **Install** → selesai!

---

## 🚀 Cara Pakai

1. **Nyalakan ESP32** (device Honda ECU Tool)
2. **Konek WiFi HP** ke `Honda ECU Tool` (password default: sesuai setting)
3. **Buka aplikasi** Honda ECU Tool
4. Muncul layar **konfigurasi IP** → masukkan IP ESP32 (default: `192.168.4.1`)
5. Tap **Hubungkan ke ESP32**
6. Aplikasi siap digunakan! 🎉

---

## 🔨 Cara Build APK Sendiri

### Opsi A — Android Studio (Paling Mudah)
1. Install [Android Studio](https://developer.android.com/studio)
2. Buka terminal di folder `honda-ecu-apk/`
3. Install dependensi: `npm install`
4. Sync Capacitor: `npx cap sync android`
5. Buka di Android Studio: `npx cap open android`
6. Klik **Build → Build Bundle(s) / APK(s) → Build APK(s)**
7. APK ada di `android/app/build/outputs/apk/debug/`

### Opsi B — Script PowerShell Otomatis
```powershell
# Buka PowerShell sebagai Administrator, lalu jalankan:
cd "path\ke\honda-ecu-apk"
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
.\build-apk.ps1
```

Script otomatis akan:
- ✅ Install Java JDK 17 (via winget)
- ✅ Download Android SDK
- ✅ Install platform Android 34
- ✅ Sync web assets ke Android
- ✅ Build APK debug

**Mode release:**
```powershell
.\build-apk.ps1 -Release
```

**Jika Java sudah ada:**
```powershell
.\build-apk.ps1 -SkipJDK
```

### Opsi C — GitHub Actions (Tanpa Install Apapun)
1. Push project ini ke GitHub
2. Buka tab **Actions** di repository
3. Pilih workflow **"Build Honda ECU Tool APK"**
4. Klik **Run workflow** → pilih Debug atau Release
5. Tunggu ~3-5 menit
6. Download APK dari tab **Artifacts**

Atau push tag untuk auto-release:
```bash
git tag v1.0.0
git push origin v1.0.0
```

---

## 📁 Struktur Project

```
honda-ecu-apk/
├── www/                    ← Web assets (HTML/JS)
│   ├── index.html          ← Mobile UI (dimodifikasi dari mobile.html)
│   └── js/
│       ├── api.js          ← API client (APK-aware, auto-detect ESP32 IP)
│       ├── dashboard.js
│       ├── live.js
│       └── ...
├── android/                ← Android project
│   ├── app/
│   │   ├── build.gradle
│   │   └── src/main/
│   │       ├── AndroidManifest.xml
│   │       ├── java/...MainActivity.java
│   │       └── res/
│   ├── build.gradle
│   └── gradlew.bat
├── capacitor.config.json   ← Konfigurasi Capacitor
├── package.json
└── build-apk.ps1           ← Script build otomatis Windows
```

---

## ⚙️ Konfigurasi

### Ubah IP ESP32 di Aplikasi
- Buka aplikasi → muncul layar IP Configuration
- Masukkan IP baru → tap Hubungkan
- IP disimpan di storage lokal HP (akan diingat)

### Ubah Default IP (Permanent)
Edit `www/js/api.js` baris:
```javascript
let _espIP = localStorage.getItem('esp32_ip') || '192.168.4.1';
```
Ganti `192.168.4.1` dengan IP yang diinginkan.

---

## 📋 Requirement

| Komponen | Minimum |
|----------|---------|
| Android  | 6.0 (API 23) |
| RAM      | 1 GB |
| Storage  | 20 MB |
| WiFi     | Wajib (untuk konek ke ESP32) |

---

## 🐛 Troubleshooting

| Masalah | Solusi |
|---------|--------|
| Tidak bisa konek ke ESP32 | Pastikan konek ke WiFi "Honda ECU Tool" dulu |
| IP salah | Buka Settings ESP32 untuk lihat IP aktual |
| APK tidak bisa diinstall | Aktifkan "Install Unknown Apps" di Settings |
| Build gagal (Java not found) | Install JDK 17 dari https://adoptium.net |
| Build gagal (SDK not found) | Jalankan `.\build-apk.ps1` ulang atau install Android Studio |

---

## 📄 License

Project ini adalah tool diagnostik untuk keperluan pribadi. Gunakan dengan bijak.
