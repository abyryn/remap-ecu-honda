// ============================================================
// api.js - REST API Client + WebSocket Manager
// APK Version: auto-detect ESP32 IP (192.168.4.1)
// ============================================================

const API = (() => {
  // ---- Detect BASE URL ----
  // Saat diakses dari browser biasa (di ESP32 WiFi): location.hostname = '192.168.4.1'
  // Saat diakses dari APK (Capacitor/WebView lokal): location.hostname = 'localhost' atau kosong
  // Kita set BASE ke http://192.168.4.1 saat bukan di ESP32
  const isRunningInESP32 = (
    location.hostname === '192.168.4.1' ||
    location.hostname === '192.168.1.1' ||
    (location.hostname !== 'localhost' && location.hostname !== '' && !location.hostname.startsWith('localhost'))
  );

  // IP ESP32 bisa dikustomisasi oleh user
  let _espIP = localStorage.getItem('esp32_ip') || '192.168.4.1';

  // BASE URL: kosong jika di ESP32 langsung, atau http://ip jika di APK
  let BASE = isRunningInESP32 ? '' : `http://${_espIP}`;

  console.log(`[API] Running ${isRunningInESP32 ? 'on ESP32 WebServer' : 'as APK/external'}, BASE="${BASE}"`);

  // ---- IP Configuration (untuk APK) ----
  function setESP32IP(ip) {
    _espIP = ip;
    BASE = isRunningInESP32 ? '' : `http://${ip}`;
    localStorage.setItem('esp32_ip', ip);
    console.log(`[API] ESP32 IP changed to: ${ip}, BASE="${BASE}"`);
  }

  function getESP32IP() { return _espIP; }
  function getBase() { return BASE; }

  // ---- WebSocket URL ----
  function _getWSUrl() {
    if (isRunningInESP32) {
      return `ws://${location.host}/ws`;
    }
    return `ws://${_espIP}/ws`;
  }

  let _ws = null;
  let _wsHandlers = {};
  let _wsReconnectTimer = null;
  let _wsConnected = false;

  // ---- HTTP helpers ----
  async function _req(method, path, body = null) {
    const opts = {
      method,
      headers: { 'Content-Type': 'application/json' },
    };
    if (body) opts.body = JSON.stringify(body);
    try {
      const r = await fetch(BASE + path, opts);
      const json = await r.json();
      if (!r.ok) throw new Error(json.error || `HTTP ${r.status}`);
      return json;
    } catch (e) {
      console.error(`[API] ${method} ${path}:`, e.message);
      throw e;
    }
  }

  const get  = (path)        => _req('GET',    path);
  const post = (path, body)  => _req('POST',   path, body);
  const del  = (path)        => _req('DELETE', path);

  // ---- GET ----
  const status   = () => get('/api/status');
  const info     = () => get('/api/info');
  const live     = () => get('/api/live');
  const dtc      = () => get('/api/dtc');
  const log      = (n=50) => get(`/api/log?count=${n}`);
  const files    = (path='/backup') => get(`/api/files?path=${encodeURIComponent(path)}`);
  const settings = () => get('/api/settings');

  // ---- POST ----
  const connect      = ()         => post('/api/connect');
  const disconnect   = ()         => post('/api/disconnect');
  const readId       = ()         => post('/api/read-id');
  const readDTC      = ()         => post('/api/read-dtc');
  const clearDTC     = ()         => post('/api/clear-dtc');
  const startLog     = ()         => post('/api/start-log');
  const stopLog      = ()         => post('/api/stop-log');
  const backup       = (filename) => post('/api/backup', { filename });
  const restore      = (filename) => post('/api/restore', { filename });
  const reboot       = ()         => post('/api/reboot');
  const saveSettings = (data)     => post('/api/settings', data);
  const setModel     = (id)       => post('/api/set-model', { model: id });
  const klineSend    = (hex)      => post('/api/kline-send', { hex });

  // ---- DELETE ----
  const deleteBackup = (filename) =>
    del(`/api/backup?filename=${encodeURIComponent(filename)}`);

  // ---- OTA Upload ----
  async function otaUpload(file, onProgress) {
    return new Promise((resolve, reject) => {
      const xhr = new XMLHttpRequest();
      xhr.open('POST', BASE + '/api/ota');
      xhr.upload.onprogress = (e) => {
        if (e.lengthComputable && onProgress)
          onProgress(Math.round(e.loaded / e.total * 100));
      };
      xhr.onload = () => {
        try { resolve(JSON.parse(xhr.responseText)); }
        catch { resolve({}); }
      };
      xhr.onerror = () => reject(new Error('OTA upload failed'));
      const fd = new FormData();
      fd.append('firmware', file, file.name);
      xhr.send(fd);
    });
  }

  // ---- Download backup file ----
  function downloadBackup(filename) {
    const a = document.createElement('a');
    a.href = `${BASE}/download?file=${encodeURIComponent(filename)}`;
    a.download = filename;
    a.click();
  }

  // ---- WebSocket ----
  function wsConnect() {
    const url = _getWSUrl();
    console.log('[WS] Connecting to:', url);
    _ws = new WebSocket(url);

    _ws.onopen = () => {
      _wsConnected = true;
      console.log('[WS] Connected');
      clearTimeout(_wsReconnectTimer);
      if (_wsHandlers.open) _wsHandlers.open();
    };

    _ws.onmessage = (e) => {
      try {
        const msg = JSON.parse(e.data);
        if (_wsHandlers[msg.type]) _wsHandlers[msg.type](msg);
        if (_wsHandlers.message)   _wsHandlers.message(msg);
      } catch {}
    };

    _ws.onclose = () => {
      _wsConnected = false;
      console.log('[WS] Disconnected — reconnecting in 3s');
      if (_wsHandlers.close) _wsHandlers.close();
      _wsReconnectTimer = setTimeout(wsConnect, 3000);
    };

    _ws.onerror = (e) => {
      console.warn('[WS] Error', e);
    };
  }

  function wsSend(cmd, data = {}) {
    if (_ws && _ws.readyState === WebSocket.OPEN) {
      _ws.send(JSON.stringify({ cmd, ...data }));
    }
  }

  function onWS(type, handler) {
    _wsHandlers[type] = handler;
  }

  function wsClose() {
    clearTimeout(_wsReconnectTimer);
    if (_ws) _ws.close();
  }

  return {
    // GET
    status, info, live, dtc, log, files, settings,
    // POST
    connect, disconnect, readId, readDTC, clearDTC,
    startLog, stopLog, backup, restore, reboot,
    saveSettings, setModel, klineSend,
    // Generic Request
    request: _req,
    // DELETE
    deleteBackup,
    // File
    otaUpload, downloadBackup,
    // WS
    wsConnect, wsSend, onWS, wsClose,
    // Config (APK only)
    setESP32IP, getESP32IP, getBase,
    get isWsConnected() { return _wsConnected; }
  };
})();
