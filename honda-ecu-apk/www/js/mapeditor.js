// ============================================================
// mapeditor.js - Tuning Map Editor
// ============================================================

const MapEditor = (function() {
  
  let currentMap = null;
  let mapData = null; // { name, type, cols, rows, rowLabels, colLabels, values }
  
  // DOM Elements
  const elSelect = document.getElementById('map-select');
  const btnLoad = document.getElementById('btn-map-load');
  const btnSave = document.getElementById('btn-map-save');
  const container = document.getElementById('map-table-container');
  const infoLabel = document.getElementById('map-info-label');
  const chkLiveCursor = document.getElementById('chk-live-cursor');
  
  let history = [];
  let historyPos = -1;
  
  function init() {
    if(!elSelect) return;
    
    // Load map list when page is shown
    document.querySelector('[data-page="mapeditor"]').addEventListener('click', () => {
      fetchMapList();
    });
    
    btnLoad.addEventListener('click', () => {
      const name = elSelect.value;
      if(name) loadMap(name);
    });
    
    btnSave.addEventListener('click', saveMap);
    
    // Global keyboard listener for the editor
    document.addEventListener('keydown', handleKeyDown);
  }
  
  function fetchMapList() {
    API.request('/api/maps')
      .then(res => {
        elSelect.innerHTML = '<option value="">-- Select Map --</option>';
        if (res.maps && res.maps.length > 0) {
          res.maps.forEach(m => {
            const opt = document.createElement('option');
            opt.value = m.name;
            opt.textContent = `${m.name} (${m.type})`;
            elSelect.appendChild(opt);
          });
          btnLoad.disabled = false;
        } else {
          elSelect.innerHTML = '<option value="">No maps available for this ECU</option>';
          btnLoad.disabled = true;
        }
      })
      .catch(e => {
        console.error("Map list error", e);
      });
  }
  
  function loadMap(name) {
    container.innerHTML = '<div style="padding:100px;text-align:center"><i class="fa fa-spinner fa-spin fa-2x"></i></div>';
    
    API.request(`/api/map?name=${encodeURIComponent(name)}`)
      .then(res => {
        mapData = res;
        currentMap = name;
        history = [];
        historyPos = -1;
        saveState();
        renderTable();
        btnSave.disabled = false;
        App.toast('success', 'Map Loaded', name);
      })
      .catch(e => {
        container.innerHTML = `<div style="padding:100px;text-align:center;color:var(--danger)"><i class="fa fa-triangle-exclamation fa-3x" style="margin-bottom:12px"></i><div>${e.message}</div></div>`;
        btnSave.disabled = true;
      });
  }
  
  function renderTable() {
    if (!mapData || !mapData.values) return;
    
    const { cols, rows, rowLabels, colLabels, values } = mapData;
    
    let html = '<table class="map-table"><thead><tr><th></th>';
    
    // Col labels (e.g. RPM)
    for(let c=0; c<cols; c++) {
      html += `<th>${colLabels ? colLabels[c] : c}</th>`;
    }
    html += '</tr></thead><tbody>';
    
    // Row labels (e.g. TPS) and values
    for(let r=0; r<rows; r++) {
      html += `<tr><td>${rowLabels ? rowLabels[r] : r}</td>`;
      for(let c=0; c<cols; c++) {
        html += `<td data-r="${r}" data-c="${c}"><input type="text" class="map-cell" value="${values[r][c]}" data-r="${r}" data-c="${c}"></td>`;
      }
      html += '</tr>';
    }
    html += '</tbody></table>';
    
    container.innerHTML = html;
    infoLabel.textContent = `Map Size: ${cols}x${rows}`;
    
    // Attach events
    const cells = container.querySelectorAll('.map-cell');
    cells.forEach(cell => {
      cell.addEventListener('focus', onCellFocus);
      cell.addEventListener('change', onCellChange);
    });
  }
  
  function onCellFocus(e) {
    e.target.select();
  }
  
  function onCellChange(e) {
    const r = parseInt(e.target.getAttribute('data-r'));
    const c = parseInt(e.target.getAttribute('data-c'));
    const val = parseFloat(e.target.value);
    
    if(!isNaN(val)) {
      mapData.values[r][c] = val;
      saveState();
    } else {
      e.target.value = mapData.values[r][c]; // revert
    }
  }
  
  function handleKeyDown(e) {
    if(!document.getElementById('page-mapeditor').classList.contains('active')) return;
    
    const activeEl = document.activeElement;
    if(activeEl && activeEl.classList.contains('map-cell')) {
      const r = parseInt(activeEl.getAttribute('data-r'));
      const c = parseInt(activeEl.getAttribute('data-c'));
      
      let changed = false;
      let val = mapData.values[r][c];
      
      if(e.key === 'w' || e.key === 'W' || e.key === '+') {
        val += 1; // Increment step, could be based on map precision
        changed = true;
        e.preventDefault();
      } else if (e.key === 's' || e.key === 'S' || e.key === '-') {
        val -= 1;
        changed = true;
        e.preventDefault();
      }
      
      if(changed) {
        mapData.values[r][c] = val;
        activeEl.value = val;
        saveState();
      }
    }
  }
  
  function saveState() {
    const stateStr = JSON.stringify(mapData.values);
    if(historyPos >= 0 && history[historyPos] === stateStr) return;
    history.push(stateStr);
    historyPos++;
  }
  
  function saveMap() {
    if(!currentMap || !mapData) return;
    
    btnSave.disabled = true;
    btnSave.innerHTML = '<i class="fa fa-spinner fa-spin"></i> Saving...';
    
    API.request(`/api/map?name=${encodeURIComponent(currentMap)}`, 'POST', { values: mapData.values })
      .then(r => {
        App.toast('success', 'Map Applied', 'Changes saved to buffer.');
      })
      .catch(e => {
        App.toast('error', 'Save Failed', e.message);
      })
      .finally(() => {
        btnSave.disabled = false;
        btnSave.innerHTML = '<i class="fa fa-floppy-disk"></i> Apply to Buffer';
      });
  }
  
  // Live cursor update
  function updateLiveCursor(rpm, tps) {
    if(!chkLiveCursor || !chkLiveCursor.checked || !mapData) return;
    
    if(mapData.type === '3D') {
      let bestR = 0; let minRDiff = 999999;
      let bestC = 0; let minCDiff = 999999;
      
      for(let c=0; c<mapData.cols; c++) {
        const cVal = parseFloat(mapData.colLabels[c]);
        const diff = Math.abs(cVal - rpm);
        if(diff < minCDiff) { minCDiff = diff; bestC = c; }
      }
      
      for(let r=0; r<mapData.rows; r++) {
        const rVal = parseFloat(mapData.rowLabels[r]);
        const diff = Math.abs(rVal - tps);
        if(diff < minRDiff) { minRDiff = diff; bestR = r; }
      }
      
      const cells = container.querySelectorAll('.live-cursor');
      cells.forEach(c => c.classList.remove('live-cursor'));
      
      const cell = container.querySelector(`.map-cell[data-r="${bestR}"][data-c="${bestC}"]`);
      if(cell) {
        cell.parentElement.classList.add('live-cursor');
      }
    }
  }
  
  return {
    init,
    updateLiveCursor
  };
})();
