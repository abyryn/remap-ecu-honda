// ============================================================
// dtc.js - Diagnostic Trouble Codes UI
// ============================================================

const Diag = (() => {
  let _dtcs = [];

  function _renderTable() {
    const tbody = document.getElementById('dtc-tbody');
    if (!tbody) return;

    if (_dtcs.length === 0) {
      tbody.innerHTML = `<tr><td colspan="4" style="text-align:center;color:var(--text-muted);padding:24px">
        No DTCs found</td></tr>`;
      const badge = document.getElementById('dtc-count-badge');
      if (badge) badge.textContent = '0';
      return;
    }

    tbody.innerHTML = _dtcs.map(d => `
      <tr>
        <td><span class="badge-pill ${d.milOn ? 'badge-danger' : 'badge-warning'}">${d.code}</span></td>
        <td>${d.description}</td>
        <td>${d.milOn
          ? '<span class="badge-pill badge-danger">MIL ON</span>'
          : '<span class="badge-pill badge-info">Pending</span>'}</td>
        <td>${d.pending ? 'Pending' : 'Confirmed'}</td>
      </tr>`).join('');

    const badge = document.getElementById('dtc-count-badge');
    if (badge) badge.textContent = _dtcs.length;
  }

  async function handleReadDTC() {
    const btn = document.getElementById('btn-read-dtc');
    if (btn) { btn.disabled = true; btn.textContent = 'Reading…'; }
    try {
      const res = await API.readDTC();
      _dtcs = res.dtcs || [];
      _renderTable();
      App.toast('success', 'DTC Read', `Found ${_dtcs.length} code(s)`);
    } catch (e) {
      App.toast('error', 'Read DTC Failed', e.message);
    } finally {
      if (btn) { btn.disabled = false; btn.textContent = '🔍 Read DTC'; }
    }
  }

  async function handleClearDTC() {
    if (!confirm('Clear all DTCs? This will erase fault history.')) return;
    const btn = document.getElementById('btn-clear-dtc');
    if (btn) { btn.disabled = true; btn.textContent = 'Clearing…'; }
    try {
      await API.clearDTC();
      _dtcs = [];
      _renderTable();
      App.toast('success', 'DTCs Cleared', 'All fault codes have been erased');
    } catch (e) {
      App.toast('error', 'Clear Failed', e.message);
    } finally {
      if (btn) { btn.disabled = false; btn.textContent = '🗑 Clear DTC'; }
    }
  }

  function init() {
    const readBtn  = document.getElementById('btn-read-dtc');
    const clearBtn = document.getElementById('btn-clear-dtc');
    if (readBtn)  readBtn.addEventListener('click',  handleReadDTC);
    if (clearBtn) clearBtn.addEventListener('click', handleClearDTC);
    _renderTable();
  }

  return { init };
})();
