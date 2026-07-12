#!/usr/bin/env pwsh
# ============================================================
# build-apk.ps1 — Honda ECU Tool APK Builder (Windows)
# Otomatis install semua dependensi dan build APK
# ============================================================

param(
    [switch]$SkipJDK,
    [switch]$SkipSDK,
    [switch]$Release,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

function Write-Header($text) {
    Write-Host ""
    Write-Host "══════════════════════════════════════════" -ForegroundColor Cyan
    Write-Host " $text" -ForegroundColor White
    Write-Host "══════════════════════════════════════════" -ForegroundColor Cyan
}

function Write-Step($text) {
    Write-Host "[*] $text" -ForegroundColor Yellow
}

function Write-OK($text) {
    Write-Host "[✓] $text" -ForegroundColor Green
}

function Write-Err($text) {
    Write-Host "[✗] $text" -ForegroundColor Red
}

$ScriptDir   = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectDir  = $ScriptDir
$AndroidDir  = Join-Path $ProjectDir "android"
$JDK_DIR     = "C:\Program Files\Eclipse Adoptium\jdk-17"
$SDK_DIR     = "$env:LOCALAPPDATA\Android\Sdk"
$TOOLS_DIR   = "$SDK_DIR\cmdline-tools\latest\bin"

Write-Header "Honda ECU Tool — APK Builder"
Write-Host " Versi  : 1.0.0" -ForegroundColor Gray
Write-Host " Mode   : $(if ($Release) { 'RELEASE' } else { 'DEBUG' })" -ForegroundColor Gray
Write-Host ""

# ---- Step 1: Check Node.js ----
Write-Header "Step 1 — Node.js"
try {
    $nodeVer = node --version
    Write-OK "Node.js ditemukan: $nodeVer"
} catch {
    Write-Err "Node.js tidak ditemukan. Install dari https://nodejs.org"
    exit 1
}

# ---- Step 2: Install JDK 17 ----
Write-Header "Step 2 — Java JDK 17"

if ($SkipJDK) {
    Write-Step "Skip JDK install (--SkipJDK)"
} elseif (Test-Path "$JDK_DIR\bin\java.exe") {
    Write-OK "JDK 17 sudah ada di $JDK_DIR"
} else {
    # Coba cari java di PATH
    $javaPath = Get-Command java -ErrorAction SilentlyContinue
    if ($javaPath) {
        $javaVer = java -version 2>&1 | Select-String "version" | Head -1
        Write-OK "Java ditemukan: $javaVer"
        $JDK_DIR = Split-Path (Split-Path $javaPath.Source)
    } else {
        Write-Step "Mengunduh dan menginstall JDK 17 via winget..."
        try {
            winget install --id EclipseAdoptium.Temurin.17.JDK -e --silent --accept-package-agreements --accept-source-agreements
            Write-OK "JDK 17 berhasil diinstall"
        } catch {
            Write-Host ""
            Write-Err "Install JDK gagal. Install manual dari:"
            Write-Host "  https://adoptium.net/temurin/releases/?version=17" -ForegroundColor Cyan
            Write-Host "  Pilih: Windows x64 .msi"
            Write-Host ""
            Write-Host "Setelah install, jalankan script ini lagi." -ForegroundColor Yellow
            exit 1
        }
    }
}

# Set JAVA_HOME
if (Test-Path "$JDK_DIR\bin\java.exe") {
    $env:JAVA_HOME = $JDK_DIR
    $env:PATH = "$JDK_DIR\bin;$env:PATH"
    Write-OK "JAVA_HOME = $JDK_DIR"
} else {
    # Cari JDK di lokasi umum
    $possibleJDKs = @(
        "C:\Program Files\Eclipse Adoptium",
        "C:\Program Files\Java",
        "C:\Program Files\Microsoft",
        "C:\Program Files\BellSoft"
    )
    foreach ($dir in $possibleJDKs) {
        if (Test-Path $dir) {
            $jdkFound = Get-ChildItem $dir -Directory | Where-Object { $_.Name -like "jdk*17*" -or $_.Name -like "jdk-17*" } | Select-Object -First 1
            if ($jdkFound) {
                $env:JAVA_HOME = $jdkFound.FullName
                $env:PATH = "$($jdkFound.FullName)\bin;$env:PATH"
                Write-OK "JAVA_HOME = $($jdkFound.FullName)"
                break
            }
        }
    }
}

# ---- Step 3: Android SDK ----
Write-Header "Step 3 — Android SDK"

if ($SkipSDK) {
    Write-Step "Skip SDK setup (--SkipSDK)"
} elseif (Test-Path "$TOOLS_DIR\sdkmanager.bat") {
    Write-OK "Android SDK sudah ada di $SDK_DIR"
} else {
    Write-Step "Download Android Command Line Tools..."
    $clToolsUrl = "https://dl.google.com/android/repository/commandlinetools-win-11076708_latest.zip"
    $clToolsZip = "$env:TEMP\android-cmdtools.zip"

    try {
        Invoke-WebRequest -Uri $clToolsUrl -OutFile $clToolsZip -UseBasicParsing
        Write-OK "Download selesai"

        # Extract
        New-Item -ItemType Directory -Path "$SDK_DIR\cmdline-tools\latest" -Force | Out-Null
        Expand-Archive -Path $clToolsZip -DestinationPath "$env:TEMP\android-tools" -Force
        # cmdline-tools ada di subfolder "cmdline-tools"
        $extractedTools = "$env:TEMP\android-tools\cmdline-tools"
        if (Test-Path $extractedTools) {
            Copy-Item "$extractedTools\*" "$SDK_DIR\cmdline-tools\latest" -Recurse -Force
        }
        Write-OK "Android Command Line Tools diekstrak"
    } catch {
        Write-Err "Download Android SDK gagal: $_"
        Write-Host "Install Android Studio manual dari:" -ForegroundColor Yellow
        Write-Host "  https://developer.android.com/studio" -ForegroundColor Cyan
        exit 1
    }
}

# Set ANDROID_HOME
$env:ANDROID_HOME = $SDK_DIR
$env:ANDROID_SDK_ROOT = $SDK_DIR
$env:PATH = "$TOOLS_DIR;$SDK_DIR\platform-tools;$SDK_DIR\build-tools\34.0.0;$env:PATH"

# Install SDK components
if (Test-Path "$TOOLS_DIR\sdkmanager.bat") {
    Write-Step "Menginstall Android platform dan build tools..."
    $acceptStr = "y`n" * 10  # auto accept licenses
    echo $acceptStr | & "$TOOLS_DIR\sdkmanager.bat" --sdk_root="$SDK_DIR" "platform-tools" "platforms;android-34" "build-tools;34.0.0" 2>&1 | Out-Null
    Write-OK "Android platform-34 dan build-tools siap"
}

# ---- Step 4: Install npm dependencies ----
Write-Header "Step 4 — NPM Dependencies (Capacitor)"

Set-Location $ProjectDir
if (-not (Test-Path "node_modules")) {
    Write-Step "npm install..."
    npm install --silent
    Write-OK "Dependencies terinstall"
} else {
    Write-OK "node_modules sudah ada, skip install"
}

# ---- Step 5: Capacitor sync ----
Write-Header "Step 5 — Capacitor Sync"
Write-Step "npx cap sync android..."
try {
    npx cap sync android 2>&1
    Write-OK "Capacitor sync selesai"
} catch {
    Write-Step "Capacitor sync warning: $_"
}

# ---- Step 6: Build APK ----
Write-Header "Step 6 — Build APK"

Set-Location $AndroidDir

if ($Clean) {
    Write-Step "Cleaning build..."
    & ".\gradlew.bat" clean 2>&1 | Out-Null
}

if ($Release) {
    Write-Step "Building RELEASE APK..."
    $buildTarget = "assembleRelease"
    $outputPath = "app\build\outputs\apk\release\app-release-unsigned.apk"
} else {
    Write-Step "Building DEBUG APK..."
    $buildTarget = "assembleDebug"
    $outputPath = "app\build\outputs\apk\debug\app-debug.apk"
}

& ".\gradlew.bat" $buildTarget --no-daemon 2>&1

if (Test-Path $outputPath) {
    $apkSizeMB = [math]::Round((Get-Item $outputPath).Length / 1MB, 2)
    Write-Host ""
    Write-OK "═══════════════════════════════════"
    Write-OK " APK BERHASIL DIBUILD!"
    Write-OK "═══════════════════════════════════"
    Write-Host ""
    Write-Host " File: $outputPath" -ForegroundColor Cyan
    Write-Host " Size: $apkSizeMB MB" -ForegroundColor Cyan
    Write-Host ""

    # Copy ke folder utama
    $destApk = Join-Path $ProjectDir "HondaECUTool-v1.0.0-$(if($Release){'release'}else{'debug'}).apk"
    Copy-Item $outputPath $destApk -Force
    Write-OK "APK disalin ke: $destApk"
    Write-Host ""
    Write-Host " Cara install di HP Android:" -ForegroundColor Yellow
    Write-Host " 1. Transfer file APK ke HP via USB atau WhatsApp" -ForegroundColor White
    Write-Host " 2. Buka file manager di HP" -ForegroundColor White
    Write-Host " 3. Tap file APK → Install" -ForegroundColor White
    Write-Host " 4. Jika ada 'Unknown Sources', aktifkan di Settings → Security" -ForegroundColor White
    Write-Host ""
} else {
    Write-Err "Build gagal! APK tidak ditemukan di $outputPath"
    Write-Host "Cek error di atas atau buka project di Android Studio" -ForegroundColor Yellow
    Set-Location $ProjectDir
    exit 1
}

Set-Location $ProjectDir
Write-Header "Selesai!"
