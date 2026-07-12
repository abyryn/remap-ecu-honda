// ============================================================
// flashui.js - Flash ECU User Interface
// ============================================================

const FlashUI = (function() {

  // DOM Elements
  const btnReadCal = document.getElementById('btn-flash-read-cal');
  const btnReadFull = document.getElementById('btn-flash-read-full');
  const btnWriteCal = document.getElementById('btn-flash-write-cal');
  const btnWriteFull = document.getElementById('btn-flash-write-full');
  const btnRecover = document.getElementById('btn-flash-recover');
  
  const progressBox = document.getElementById('flash-progress-box');
  const progressText = document.getElementById('flash-status-text');
  const progressBar = document.getElementById('flash-progress-bar');
  const progressSpeed = document.getElementById('flash-speed');
  const progressEta = document.getElementById('flash-eta');
  const progressMsg = document.getElementById('flash-msg');
  
  const chkAutoBackup = document.getElementById('chk-auto-backup');
  
  let isFlashing = false;

  function init() {
    if(!btnReadCal) return;
    
    btnReadCal.addEventListener('click', () => startOperation('read', 'calibration'));
    btnReadFull.addEventListener('click', () => startOperation('read', 'full'));
    
    btnWriteCal.addEventListener('click', () => {
      if(confirm('WARNING: You are about to write calibration data to the ECU.\n\nMake sure the battery voltage is stable above 12V and DO NOT disconnect the cable.\n\nProceed?')) {
        startOperation('write', 'calibration');
      }
    });
    
    btnWriteFull.addEventListener('click', () => {
      if(confirm('DANGER: You are about to overwrite the FULL FLASH image of the ECU.\n\nThis will overwrite immobilizer data and maps. Ensure you have a valid Full Flash backup.\n\nProceed?')) {
        startOperation('write', 'full');
      }
    });
    
    btnRecover.addEventListener('click', () => {
      if(confirm('Recovery mode will attempt to force-flash a known good firmware. Proceed?')) {
        startOperation('recovery', 'full');
      }
    });
  }
  
  function startOperation(action, type) {
    if(isFlashing) return;
    
    isFlashing = true;
    showProgress();
    setAllButtons(false);
    
    let endpoint = '';
    let payload = { type: type };
    
    if (action === 'read') {
      endpoint = '/api/ecu/read';
    } else if (action === 'write') {
      endpoint = '/api/ecu/write';
      payload.autoBackup = chkAutoBackup ? chkAutoBackup.checked : true;
    } else if (action === 'recovery') {
      endpoint = '/api/recovery';
      payload = {};
    }
    
    API.request(endpoint, 'POST', payload)
      .then(res => {
        // Operation started successfully, we wait for WS events
        progressMsg.textContent = 'Waiting for device response...';
      })
      .catch(e => {
        isFlashing = false;
        hideProgress();
        setAllButtons(true);
        App.toast('error', 'Operation Failed', e.message);
      });
  }
  
  function handleWSEvent(data) {
    if(!data || data.type !== 'flash_progress') return;
    
    const pct = data.percent || 0;
    const msg = data.msg || '';
    const speed = data.speed || 0;
    const eta = data.eta || 0;
    const state = data.state; // IDLE, ERASING, WRITING, READING, DONE, ERROR
    
    if(!isFlashing && state !== 'IDLE' && state !== 'DONE' && state !== 'ERROR') {
      isFlashing = true;
      showProgress();
      setAllButtons(false);
    }
    
    progressText.textContent = state;
    progressBar.style.width = pct + '%';
    progressMsg.textContent = msg;
    
    if(speed > 0) {
      progressSpeed.textContent = (speed / 1024).toFixed(1) + ' KB/s';
    } else {
      progressSpeed.textContent = '-- KB/s';
    }
    
    if(eta > 0) {
      const min = Math.floor(eta / 60);
      const sec = Math.floor(eta % 60);
      progressEta.textContent = `ETA: ${min}:${sec.toString().padStart(2, '0')}`;
    } else {
      progressEta.textContent = 'ETA: --:--';
    }
    
    if(state === 'DONE' || state === 'ERROR') {
      isFlashing = false;
      setAllButtons(true);
      
      if(state === 'DONE') {
        progressText.textContent = 'Operation Complete';
        App.toast('success', 'Flash Operation', 'Completed successfully.');
        
        if(msg.includes('Read')) {
          // Enable write buttons if read was successful
          if(btnWriteCal) btnWriteCal.disabled = false;
          if(btnWriteFull) btnWriteFull.disabled = false;
        }
      } else {
        progressText.textContent = 'Operation Failed';
        progressText.style.color = 'var(--danger)';
        App.toast('error', 'Flash Error', msg);
      }
      
      setTimeout(() => {
        if(!isFlashing) hideProgress();
      }, 5000);
    }
  }
  
  function showProgress() {
    if(progressBox) {
      progressBox.style.display = 'block';
      progressText.style.color = '';
      progressBar.style.width = '0%';
      progressSpeed.textContent = '0 KB/s';
      progressEta.textContent = 'ETA: --:--';
    }
  }
  
  function hideProgress() {
    if(progressBox) progressBox.style.display = 'none';
  }
  
  function setAllButtons(enable) {
    if(btnReadCal) btnReadCal.disabled = !enable;
    if(btnReadFull) btnReadFull.disabled = !enable;
    if(btnRecover) btnRecover.disabled = !enable;
    // We only enable write buttons if there is data in buffer
    if(btnWriteCal && !enable) btnWriteCal.disabled = true;
    if(btnWriteFull && !enable) btnWriteFull.disabled = true;
  }
  
  return {
    init,
    handleWSEvent
  };

})();
