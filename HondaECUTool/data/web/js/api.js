// ============================================================
// api.js - REST API Client + WebSocket Manager
// ============================================================

const API = (() => {
  const BASE = '';   // same-origin (ESP32 IP)
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
      xhr.open('POST', '/api/ota');
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
    a.href = `/download?file=${encodeURIComponent(filename)}`;
    a.download = filename;
    a.click();
  }

  // ---- WebSocket ----
  function wsConnect() {
    const url = `ws://${location.host}/ws`;
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
    get isWsConnected() { return _wsConnected; }
  };
})();
