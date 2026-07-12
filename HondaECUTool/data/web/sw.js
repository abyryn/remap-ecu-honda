const CACHE_NAME = 'honda-ecu-cache-v2';
const ASSETS = [
  '/',
  '/index.html',
  '/mobile.html',
  '/css/main.css',
  '/js/api.js',
  '/js/live.js',
  '/js/dtc.js',
  '/js/backup.js',
  '/js/terminal.js',
  '/js/settings.js',
  '/js/mapeditor.js',
  '/js/flashui.js',
  '/js/filemanager.js',
  '/js/dashboard.js',
  '/manifest.json'
];

// Install Event
self.addEventListener('install', (e) => {
  e.waitUntil(
    caches.open(CACHE_NAME).then((cache) => {
      return cache.addAll(ASSETS);
    }).then(() => self.skipWaiting())
  );
});

// Activate Event
self.addEventListener('activate', (e) => {
  e.waitUntil(
    caches.keys().then((keys) => {
      return Promise.all(
        keys.map((key) => {
          if (key !== CACHE_NAME) {
            return caches.delete(key);
          }
        })
      );
    }).then(() => self.clients.claim())
  );
});

// Fetch Event (Network First, fallback to cache)
self.addEventListener('fetch', (e) => {
  // Only handle GET requests and skip WebSocket requests
  if (e.request.method !== 'GET' || e.request.url.startsWith('ws')) {
    return;
  }
  
  e.respondWith(
    fetch(e.request)
      .then((res) => {
        // Update cache dynamically with the fetched resource
        const clone = res.clone();
        caches.open(CACHE_NAME).then((cache) => {
          cache.put(e.request, clone);
        });
        return res;
      })
      .catch(() => {
        return caches.match(e.request);
      })
  );
});
