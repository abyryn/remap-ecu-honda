// ============================================================
// settings.js - Settings Page UI
// ============================================================

const SettingsUI = (() => {
  const MODELS = [
    'Honda Beat','Honda Scoopy','Honda Genio','Honda Vario',
    'Honda PCX','Honda ADV','Honda Supra','Honda Sonic',
    'Honda Verza','Honda CB150R','Honda CBR150R','Honda CRF150L',
    'Honda Stylo','Honda EM1'
  ];

  async function _load() {
    try {
      const s = await API.settings();
      const set = (id, val) => {
        const el = document.getElementById(id);
        if (!el) return;
        if (el.type === 'checkbox') el.checked = !!val;
        else el.value = val ?? '';
      };
      set('set-ssid',        s.wifiSSID);
      set('set-baud',        s.uartBaud);
      set('set-timeout',     s.timeoutMs);
      set('set-auto-reconnect', s.autoReconnect);
      set('set-dark-mode',   s.darkMode);
      set('set-language',    s.language);
      set('set-username',    s.authUsername);
    } catch (e) {
      App.toast('error', 'Load Settings Failed', e.message);
    }
  }

  async function _save() {
    const get = (id) => {
      const el = document.getElementById(id);
      if (!el) return undefined;
      return el.type === 'checkbox' ? el.checked : el.value;
    };

    const payload = {
      wifiSSID:      get('set-ssid'),
      wifiPassword:  get('set-password') || undefined,
      uartBaud:      parseInt(get('set-baud')) || 10400,
      timeoutMs:     parseInt(get('set-timeout')) || 1000,
      autoReconnect: get('set-auto-reconnect'),
      darkMode:      get('set-dark-mode'),
      language:      get('set-language'),
      authUsername:  get('set-username'),
      authPassword:  get('set-auth-password') || undefined,
    };

    // Remove undefined fields
    Object.keys(payload).forEach(k => payload[k] === undefined && delete payload[k]);

    const btn = document.getElementById('btn-save-settings');
    if (btn) { btn.disabled = true; btn.textContent = 'Saving…'; }
    try {
      await API.saveSettings(payload);
      App.toast('success', 'Settings Saved', 'Reboot may be required for WiFi changes');
      // Apply theme immediately
      App.applyTheme(payload.darkMode ? 'dark' : 'light');
    } catch (e) {
      App.toast('error', 'Save Failed', e.message);
    } finally {
      if (btn) { btn.disabled = false; btn.textContent = '💾 Save Settings'; }
    }
  }

  async function _reboot() {
    if (!confirm('Reboot ESP32 now?')) return;
    try {
      await API.reboot();
      App.toast('info', 'Rebooting…', 'Reconnect in ~5 seconds');
    } catch {}
  }

  function _populateModels() {
    const sel = document.getElementById('set-model');
    if (!sel) return;
    sel.innerHTML = MODELS.map((m, i) =>
      `<option value="${i}">${m}</option>`).join('');
    sel.addEventListener('change', () => API.setModel(parseInt(sel.value)));
  }

  async function _otaSection() {
    const input = document.getElementById('ota-file-input');
    const btn   = document.getElementById('btn-ota-upload');
    const bar   = document.getElementById('ota-progress-bar');
    const label = document.getElementById('ota-progress-label');

    if (!btn || !input) return;

    btn.addEventListener('click', async () => {
      const file = input.files[0];
      if (!file) { App.toast('warning', 'Select a .bin file first', ''); return; }
      if (!confirm(`Upload firmware: ${file.name} (${App.formatBytes(file.size)})?`)) return;

      btn.disabled = true;
      try {
        await API.otaUpload(file, (pct) => {
          if (bar)   bar.style.width = pct + '%';
          if (label) label.textContent = `Uploading… ${pct}%`;
        });
        App.toast('success', 'OTA Complete', 'Device is rebooting');
      } catch (e) {
        App.toast('error', 'OTA Failed', e.message);
        btn.disabled = false;
      }
    });
  }

  function init() {
    const saveBtn   = document.getElementById('btn-save-settings');
    const rebootBtn = document.getElementById('btn-reboot');

    if (saveBtn)   saveBtn.addEventListener('click', _save);
    if (rebootBtn) rebootBtn.addEventListener('click', _reboot);

    _populateModels();
    _load();
    _otaSection();
  }

  return { init };
})();
