// ============================================================
// dashboard.js - Dashboard + global app shell
// ============================================================

const App = (() => {
  let _currentPage = 'dashboard';
  let _theme = localStorage.getItem('theme') || 'dark';
  let _statusPollTimer = null;

  // ---- Toast ----
  function toast(type, title, msg, duration = 3500) {
    const icons = { success:'✅', error:'❌', warning:'⚠️', info:'ℹ️' };
    const c = document.getElementById('toast-container');
    if (!c) return;
    const el = document.createElement('div');
    el.className = `toast ${type}`;
    el.innerHTML = `
      <div class="toast-icon">${icons[type] || '●'}</div>
      <div class="toast-msg">
        <div class="toast-title">${title}</div>
        ${msg ? `<div>${msg}</div>` : ''}
      </div>`;
    c.appendChild(el);
    setTimeout(() => el.remove(), duration);
  }

  // ---- Theme ----
  function applyTheme(t) {
    _theme = t;
    document.body.classList.toggle('light-mode', t === 'light');
    localStorage.setItem('theme', t);
  }

  // ---- Page navigation ----
  function navigate(page) {
    _currentPage = page;
    document.querySelectorAll('.nav-item').forEach(n => {
      n.classList.toggle('active', n.dataset.page === page);
    });
    document.querySelectorAll('.page-section').forEach(s => {
      s.classList.toggle('active', s.id === `page-${page}`);
    });
    document.querySelector('.topbar-title').textContent =
      document.querySelector(`.nav-item[data-page="${page}"]`)?.textContent.trim() || page;
  }

  // ---- Status bar update ----
  function updateStatusBar(status) {
    const ecuDot  = document.getElementById('sb-ecu-dot');
    const ecuTxt  = document.getElementById('sb-ecu-txt');
    const wifiTxt = document.getElementById('sb-wifi');
    const vbatTxt = document.getElementById('sb-vbat');
    const memTxt  = document.getElementById('sb-mem');
    const cpuTxt  = document.getElementById('sb-cpu');

    if (ecuDot) ecuDot.className = 'status-dot ' + (status.ecuConnected ? 'green' : 'red');
    if (ecuTxt) ecuTxt.textContent = status.ecuConnected ? 'ECU: Connected' : 'ECU: Offline';
    if (wifiTxt) wifiTxt.textContent = `WiFi: ${status.clients || 0} client(s)`;
    if (vbatTxt) vbatTxt.textContent = `Vbat: ${(status.battVoltage || 0).toFixed(2)}V`;
    if (memTxt) {
      const pct = ((status.fsUsed || 0) / (status.fsTotal || 1) * 100).toFixed(0);
      memTxt.textContent = `Heap: ${((status.freeHeap || 0)/1024).toFixed(1)}KB`;
    }
    if (cpuTxt) cpuTxt.textContent = `CPU: ${(status.cpuTemp || 0).toFixed(1)}°C`;
  }

  // ---- Dashboard cards update ----
  function updateDashboard(status) {
    const set = (id, val) => {
      const el = document.getElementById(id);
      if (el) el.textContent = val;
    };
    set('dash-uptime', formatUptime(status.uptime || 0));
    set('dash-version', status.version || '-');
    set('dash-heap', ((status.freeHeap || 0)/1024).toFixed(1) + ' KB');
    set('dash-vbat', (status.battVoltage || 0).toFixed(2) + ' V');
    set('dash-cpu', (status.cpuTemp || 0).toFixed(1) + ' °C');
    set('dash-ecu-state', ecuStateLabel(status.ecuState));

    // FS usage bar
    const pct = status.fsTotal
      ? Math.round(status.fsUsed / status.fsTotal * 100) : 0;
    const bar = document.getElementById('dash-fs-bar');
    if (bar) bar.style.width = pct + '%';
    set('dash-fs-pct', `${pct}% used (${formatBytes(status.fsUsed)} / ${formatBytes(status.fsTotal)})`);
  }

  // ---- ECU connect button ----
  async function handleConnectBtn() {
    const btn = document.getElementById('btn-connect');
    if (!btn) return;
    btn.disabled = true;
    btn.textContent = 'Connecting…';
    try {
      await API.connect();
      toast('success', 'ECU Connected', 'Session started');
      refreshStatus();
    } catch (e) {
      toast('error', 'Connect Failed', e.message);
    } finally {
      btn.disabled = false;
      btn.textContent = 'Connect ECU';
    }
  }

  async function handleDisconnectBtn() {
    try {
      await API.disconnect();
      toast('info', 'ECU Disconnected', '');
      refreshStatus();
    } catch (e) {
      toast('error', 'Error', e.message);
    }
  }

  async function refreshStatus() {
    try {
      const s = await API.status();
      updateStatusBar(s);
      updateDashboard(s);
    } catch {}
  }

  // ---- Helpers ----
  function formatUptime(ms) {
    const s = Math.floor(ms / 1000);
    const m = Math.floor(s / 60);
    const h = Math.floor(m / 60);
    return `${h}h ${m % 60}m ${s % 60}s`;
  }

  function formatBytes(b) {
    if (!b) return '0 B';
    if (b < 1024) return b + ' B';
    if (b < 1024 * 1024) return (b / 1024).toFixed(1) + ' KB';
    return (b / 1024 / 1024).toFixed(2) + ' MB';
  }

  function ecuStateLabel(n) {
    return ['Disconnected', 'Connecting', 'Connected', 'Error'][n] || 'Unknown';
  }

  // ---- WebSocket handlers ----
  function setupWS() {
    API.wsConnect();

    API.onWS('live', (msg) => {
      if (msg.data) {
        Live.updateCards(msg.data);
        if (typeof MapEditor !== 'undefined' && MapEditor.updateLiveCursor) {
          MapEditor.updateLiveCursor(msg.data.rpm || 0, msg.data.tps || 0);
        }
      }
      // update vbat in statusbar
      const vbatEl = document.getElementById('sb-vbat');
      if (vbatEl && msg.vbat) vbatEl.textContent = `Vbat: ${msg.vbat.toFixed(2)}V`;
    });

    API.onWS('status', (msg) => {
      refreshStatus();
    });

    API.onWS('flash_progress', (msg) => {
      if (typeof FlashUI !== 'undefined') FlashUI.handleWSEvent(msg);
    });

    API.onWS('open', () => {
      const dot = document.getElementById('sb-ws-dot');
      if (dot) dot.className = 'status-dot green';
    });

    API.onWS('close', () => {
      const dot = document.getElementById('sb-ws-dot');
      if (dot) dot.className = 'status-dot red';
    });
  }

  // ---- Init ----
  function init() {
    applyTheme(_theme);

    // Nav click
    document.querySelectorAll('.nav-item').forEach(n => {
      n.addEventListener('click', () => navigate(n.dataset.page));
    });

    // Theme toggle
    const themeBtn = document.getElementById('btn-theme');
    if (themeBtn) {
      themeBtn.addEventListener('click', () =>
        applyTheme(_theme === 'dark' ? 'light' : 'dark'));
    }

    // Connect btn
    const connBtn = document.getElementById('btn-connect');
    if (connBtn) connBtn.addEventListener('click', handleConnectBtn);

    const discBtn = document.getElementById('btn-disconnect');
    if (discBtn) discBtn.addEventListener('click', handleDisconnectBtn);

    // Mobile sidebar
    const toggle = document.getElementById('menu-toggle');
    const sidebar = document.querySelector('.sidebar');
    const overlay = document.getElementById('sidebar-overlay');
    if (toggle) {
      toggle.addEventListener('click', () => {
        sidebar.classList.toggle('open');
        overlay.classList.toggle('show');
      });
    }
    if (overlay) {
      overlay.addEventListener('click', () => {
        sidebar.classList.remove('open');
        overlay.classList.remove('show');
      });
    }

    // Tabs
    document.querySelectorAll('.tab-item').forEach(tab => {
      tab.addEventListener('click', () => {
        const group = tab.closest('.tabs').dataset.group;
        document.querySelectorAll(`.tabs[data-group="${group}"] .tab-item`)
          .forEach(t => t.classList.remove('active'));
        document.querySelectorAll(`.tab-content[data-group="${group}"]`)
          .forEach(c => c.classList.remove('active'));
        tab.classList.add('active');
        const target = document.getElementById(tab.dataset.tab);
        if (target) target.classList.add('active');
      });
    });

    setupWS();
    refreshStatus();
    _statusPollTimer = setInterval(refreshStatus, 5000);

    navigate('dashboard');
    Live.init();
    Diag.init();
    BackupUI.init();
    Terminal.init();
    SettingsUI.init();
    if(typeof MapEditor !== 'undefined') MapEditor.init();
    if(typeof FlashUI !== 'undefined') FlashUI.init();
    if(typeof FileManager !== 'undefined') FileManager.init();
  }

  return { init, toast, navigate, applyTheme, refreshStatus, formatBytes, formatUptime };
})();

document.addEventListener('DOMContentLoaded', () => App.init());
