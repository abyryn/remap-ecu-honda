// ============================================================
// filemanager.js - File & Storage Manager
// ============================================================

const FileManager = (function() {

  const tbody = document.getElementById('fm-file-list');
  const btnRefresh = document.getElementById('btn-fm-refresh');
  const btnLoadBuffer = document.getElementById('btn-fm-load-buffer');
  
  let selectedFile = null;

  function init() {
    if(!tbody || !btnRefresh) return;
    
    document.querySelector('[data-page="files"]').addEventListener('click', loadFiles);
    btnRefresh.addEventListener('click', loadFiles);
    
    if(btnLoadBuffer) {
      btnLoadBuffer.addEventListener('click', () => {
        if(!selectedFile) {
          App.toast('error', 'Select File', 'Please select a file first.');
          return;
        }
        
        // Simulating load buffer by triggering restore
        App.toast('info', 'Not Implemented', 'Loading file into buffer will be available in next release.');
      });
    }
  }

  function loadFiles() {
    tbody.innerHTML = '<li style="padding:24px;text-align:center;color:var(--text-muted)"><i class="fa fa-spinner fa-spin"></i> Loading...</li>';
    
    API.request('/api/files?path=/backup')
      .then(res => {
        tbody.innerHTML = '';
        selectedFile = null;
        
        if (!res.files || res.files.length === 0) {
          tbody.innerHTML = '<li style="padding:24px;text-align:center;color:var(--text-muted)">No files found in /backup</li>';
          return;
        }
        
        res.files.forEach(f => {
          const li = document.createElement('li');
          li.className = 'file-item';
          
          const kb = (f.size / 1024).toFixed(1);
          
          li.innerHTML = `
            <div style="display:flex;align-items:center;gap:12px">
              <i class="fa fa-file-lines fa-2x" style="color:var(--primary)"></i>
              <div>
                <div style="font-weight:600">${f.name}</div>
                <div style="font-size:11px;color:var(--text-secondary)">Size: ${kb} KB</div>
              </div>
            </div>
            <div style="display:flex;gap:8px">
              <a href="/download?file=${encodeURIComponent(f.name)}" target="_blank" class="btn btn-secondary btn-sm" title="Download">
                <i class="fa fa-download"></i>
              </a>
              <button class="btn btn-danger btn-sm btn-del" data-file="${f.name}" title="Delete">
                <i class="fa fa-trash"></i>
              </button>
            </div>
          `;
          
          // Select behavior
          li.addEventListener('click', (e) => {
            if(e.target.closest('.btn') || e.target.closest('a')) return; // ignore buttons
            document.querySelectorAll('.file-item').forEach(el => el.style.background = '');
            li.style.background = 'rgba(255,255,255,0.05)';
            selectedFile = f.name;
          });
          
          // Delete behavior
          li.querySelector('.btn-del').addEventListener('click', () => {
            if (confirm(`Delete ${f.name}?`)) {
              deleteFile(f.name);
            }
          });
          
          tbody.appendChild(li);
        });
      })
      .catch(e => {
        tbody.innerHTML = `<li style="padding:24px;text-align:center;color:var(--danger)">Error: ${e.message}</li>`;
      });
  }

  function deleteFile(name) {
    API.request(`/api/backup?filename=${encodeURIComponent(name)}`, 'DELETE')
      .then(res => {
        App.toast('success', 'Deleted', `${name} removed.`);
        loadFiles();
      })
      .catch(e => {
        App.toast('error', 'Delete Failed', e.message);
      });
  }

  return { init, loadFiles };

})();
