// ============================================================
// live.js - Live Data + Chart
// ============================================================

const Live = (() => {
  let _chart = null;
  let _paused = false;
  let _history = { labels: [], rpm: [], tps: [], ect: [] };
  const MAX_POINTS = 60;

  const PIDS = [
    { key: 'rpm',        label: 'RPM',          unit: '',    id: 'live-rpm',       fmt: v => Math.round(v) },
    { key: 'tps',        label: 'TPS',           unit: '%',   id: 'live-tps',       fmt: v => v.toFixed(1) },
    { key: 'map',        label: 'MAP',           unit: 'kPa', id: 'live-map',       fmt: v => v.toFixed(1) },
    { key: 'iat',        label: 'IAT',           unit: '°C',  id: 'live-iat',       fmt: v => v.toFixed(1) },
    { key: 'ect',        label: 'ECT',           unit: '°C',  id: 'live-ect',       fmt: v => v.toFixed(1) },
    { key: 'battVoltage',label: 'Battery',       unit: 'V',   id: 'live-vbat',      fmt: v => v.toFixed(2) },
    { key: 'injPW',      label: 'Inj PW',        unit: 'ms',  id: 'live-inj',       fmt: v => v.toFixed(3) },
    { key: 'ignTiming',  label: 'Ign Timing',    unit: '°',   id: 'live-ign',       fmt: v => v.toFixed(1) },
    { key: 'speed',      label: 'Speed',         unit: 'km/h',id: 'live-speed',     fmt: v => Math.round(v) },
    { key: 'engineLoad', label: 'Engine Load',   unit: '%',   id: 'live-load',      fmt: v => v.toFixed(1) },
    { key: 'o2',         label: 'O2 Sensor',     unit: 'mV',  id: 'live-o2',        fmt: v => v.toFixed(1) },
    { key: 'fuelTrim',   label: 'Fuel Trim',     unit: '%',   id: 'live-ftrim',     fmt: v => v.toFixed(1) },
    { key: 'closedLoop', label: 'Loop',          unit: '',    id: 'live-loop',      fmt: v => v ? 'Closed' : 'Open' },
    { key: 'idleSwitch', label: 'Idle SW',       unit: '',    id: 'live-idle',      fmt: v => v ? 'ON' : 'OFF' },
  ];

  function updateCards(data) {
    if (_paused) return;
    PIDS.forEach(p => {
      const el = document.getElementById(p.id);
      if (el && data[p.key] !== undefined) {
        el.textContent = p.fmt(data[p.key]) + (p.unit ? ' ' + p.unit : '');
      }
    });
    _pushChart(data);
  }

  function _pushChart(data) {
    if (!_chart || _paused) return;
    const ts = new Date().toLocaleTimeString('id', { hour12: false });
    _history.labels.push(ts);
    _history.rpm.push(data.rpm || 0);
    _history.tps.push(data.tps || 0);
    _history.ect.push(data.ect || 0);

    if (_history.labels.length > MAX_POINTS) {
      _history.labels.shift();
      _history.rpm.shift();
      _history.tps.shift();
      _history.ect.shift();
    }

    _chart.data.labels                = _history.labels;
    _chart.data.datasets[0].data      = _history.rpm;
    _chart.data.datasets[1].data      = _history.tps;
    _chart.data.datasets[2].data      = _history.ect;
    _chart.update('none');
  }

  function _initChart() {
    const canvas = document.getElementById('live-chart');
    if (!canvas || typeof Chart === 'undefined') return;

    _chart = new Chart(canvas.getContext('2d'), {
      type: 'line',
      data: {
        labels: [],
        datasets: [
          {
            label: 'RPM / 10',
            data: [],
            borderColor: '#e8343a',
            backgroundColor: 'rgba(232,52,58,0.08)',
            borderWidth: 2,
            pointRadius: 0,
            tension: 0.3,
            yAxisID: 'y',
          },
          {
            label: 'TPS %',
            data: [],
            borderColor: '#3b82f6',
            backgroundColor: 'rgba(59,130,246,0.08)',
            borderWidth: 2,
            pointRadius: 0,
            tension: 0.3,
            yAxisID: 'y1',
          },
          {
            label: 'ECT °C',
            data: [],
            borderColor: '#f59e0b',
            backgroundColor: 'rgba(245,158,11,0.08)',
            borderWidth: 2,
            pointRadius: 0,
            tension: 0.3,
            yAxisID: 'y1',
          },
        ],
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        animation: false,
        interaction: { mode: 'index', intersect: false },
        plugins: {
          legend: { position: 'top', labels: { color: '#8a91a8', font: { size: 11 } } },
          tooltip: { enabled: true },
        },
        scales: {
          x: {
            ticks: { color: '#555c73', font: { size: 10 }, maxTicksLimit: 10 },
            grid: { color: 'rgba(255,255,255,0.05)' },
          },
          y: {
            type: 'linear', position: 'left',
            min: 0, max: 800,
            ticks: { color: '#e8343a', font: { size: 10 } },
            grid: { color: 'rgba(255,255,255,0.05)' },
            title: { display: true, text: 'RPM/10', color: '#e8343a', font: { size: 10 } },
          },
          y1: {
            type: 'linear', position: 'right',
            min: 0, max: 120,
            ticks: { color: '#8a91a8', font: { size: 10 } },
            grid: { drawOnChartArea: false },
          },
        },
      },
    });
  }

  function _exportCSV() {
    const rows = [['Time', 'RPM', 'TPS%', 'ECT°C']];
    for (let i = 0; i < _history.labels.length; i++) {
      rows.push([_history.labels[i], _history.rpm[i], _history.tps[i], _history.ect[i]]);
    }
    const csv  = rows.map(r => r.join(',')).join('\n');
    const blob = new Blob([csv], { type: 'text/csv' });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = `live_${Date.now()}.csv`;
    a.click();
  }

  function init() {
    _initChart();

    const pauseBtn = document.getElementById('btn-live-pause');
    if (pauseBtn) {
      pauseBtn.addEventListener('click', () => {
        _paused = !_paused;
        pauseBtn.textContent = _paused ? '▶ Resume' : '⏸ Pause';
      });
    }

    const exportBtn = document.getElementById('btn-live-export');
    if (exportBtn) exportBtn.addEventListener('click', _exportCSV);

    const clearBtn = document.getElementById('btn-live-clear');
    if (clearBtn) {
      clearBtn.addEventListener('click', () => {
        _history = { labels: [], rpm: [], tps: [], ect: [] };
        if (_chart) {
          _chart.data.labels = [];
          _chart.data.datasets.forEach(d => d.data = []);
          _chart.update();
        }
      });
    }
  }

  return { init, updateCards };
})();
