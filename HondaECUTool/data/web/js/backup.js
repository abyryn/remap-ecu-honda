// ============================================================
// backup.js - Backup & Restore UI
// ============================================================

const BackupUI = (() => {
  async function _loadBackupList() {
    const tbody = document.getElementById('backup-tbody');
    if (!tbody) return;
    try {
      const res = await API.files('/backup');
      const files = (res.files || []).filter(f => f.name.endsWith('.bin'));
      if (files.length === 0) {
        tbody.innerHTML = `<tr><td colspan="5" style="text-align:center;color:var(--text-muted);padding:20px">No backups found</td></tr>`;
        return;
      }
      tbody.innerHTML = files.map(f => `
        <tr>
          <td>${f.name}</td>
          <td>${f.timestamp || '-'}</td>
          <td>${f.model || '-'}</td>
          <td>${App.formatBytes(f.size)}</td>
          <td>
            <button class="btn btn-sm btn-secondary" onclick="BackupUI.download('${f.name}')">⬇ Download</button>
            <button class="btn btn-sm btn-danger"    onclick="BackupUI.deleteFile('${f.name}')">🗑</button>
          </td>
        </tr>`).join('');
    } catch (e) {
      tbody.innerHTML = `<tr><td colspan="5" style="color:var(--danger)">Error: ${e.message}</td></tr>`;
    }
  }

  function _setProgress(pct, msg) {
    const bar   = document.getElementById('backup-progress-bar');
    const label = document.getElementById('backup-progress-label');
    const wrap  = document.getElementById('backup-progress-wrap');
    if (wrap) wrap.style.display = 'block';
    if (bar)   bar.style.width = pct + '%';
    if (label) label.textContent = `${pct}% — ${msg}`;
  }

  async function startBackup() {
    const filename = `eeprom_${Date.now()}.bin`;
    const btn = document.getElementById('btn-start-backup');
    if (btn) { btn.disabled = true; btn.textContent = 'Backing up…'; }
    _setProgress(0, 'Starting…');
    try {
      // Listen for progress via WS
      API.onWS('backup_progress', (msg) => _setProgress(msg.progress, msg.message));
      const res = await API.backup(filename);
      _setProgress(100, 'Complete');
      App.toast('success', 'Backup Complete', res.filename);
      await _loadBackupList();
    } catch (e) {
      _setProgress(0, 'Failed');
      App.toast('error', 'Backup Failed', e.message);
    } finally {
      if (btn) { btn.disabled = false; btn.textContent = '💾 Start Backup'; }
    }
  }

  function _setRestoreProgress(pct, msg) {
    const bar   = document.getElementById('restore-progress-bar');
    const label = document.getElementById('restore-progress-label');
    const wrap  = document.getElementById('restore-progress-wrap');
    if (wrap)  wrap.style.display = 'block';
    if (bar)   bar.style.width = pct + '%';
    if (label) label.textContent = `${pct}% — ${msg}`;
  }

  async function startRestore() {
    const sel = document.getElementById('restore-file-select');
    if (!sel || !sel.value) { App.toast('warning', 'Select a file first', ''); return; }
    if (!confirm(`Simulate restore from ${sel.value}? This is a read-only comparison.`)) return;

    const btn = document.getElementById('btn-start-restore');
    if (btn) { btn.disabled = true; btn.textContent = 'Comparing…'; }
    _setRestoreProgress(0, 'Starting comparison…');
    try {
      API.onWS('restore_progress', (msg) => _setRestoreProgress(msg.progress, msg.message));
      await API.restore(sel.value);
      _setRestoreProgress(100, 'Simulation done');
      App.toast('info', 'Restore Simulation', 'Check logs for comparison result');
    } catch (e) {
      _setRestoreProgress(0, 'Failed');
      App.toast('error', 'Restore Failed', e.message);
    } finally {
      if (btn) { btn.disabled = false; btn.textContent = '🔄 Simulate Restore'; }
    }
  }

  function download(filename) {
    API.downloadBackup(filename);
  }

  async function deleteFile(filename) {
    if (!confirm(`Delete backup: ${filename}?`)) return;
    try {
      await API.deleteBackup(filename);
      App.toast('success', 'Deleted', filename);
      _loadBackupList();
    } catch (e) {
      App.toast('error', 'Delete Failed', e.message);
    }
  }

  async function _populateRestoreSelect() {
    const sel = document.getElementById('restore-file-select');
    if (!sel) return;
    try {
      const res = await API.files('/backup');
      const files = (res.files || []).filter(f => f.name.endsWith('.bin'));
      sel.innerHTML = `<option value="">-- Select BIN --</option>` +
        files.map(f => `<option value="${f.name}">${f.name}</option>`).join('');
    } catch {}
  }

  function init() {
    const backupBtn  = document.getElementById('btn-start-backup');
    const restoreBtn = document.getElementById('btn-start-restore');
    const refreshBtn = document.getElementById('btn-refresh-backups');

    if (backupBtn)  backupBtn.addEventListener('click', startBackup);
    if (restoreBtn) restoreBtn.addEventListener('click', startRestore);
    if (refreshBtn) refreshBtn.addEventListener('click', () => {
      _loadBackupList();
      _populateRestoreSelect();
    });

    _loadBackupList();
    _populateRestoreSelect();
  }

  return { init, download, deleteFile };
})();
