// Zombie Mongolia: вэб апп-ын service worker (нүүр дэлгэцэд суулгах боломж, web/shell.html бүртгэнэ).
// Юуг ч кэшлэхгүй: тоглоомын файлууд (?v=BUILD_ID) хөтчийн ердийн кэшээр ачаалагдана, шинэ build шууд ирнэ.
// Зөвхөн хуудсыг нээх хүсэлтийг сүлжээгээр авч, сүлжээгүй бол "интернэтгүй" гэсэн энгийн хуудас өгнө.
self.addEventListener('install', function () { self.skipWaiting(); });
self.addEventListener('activate', function (event) { event.waitUntil(self.clients.claim()); });

var OFFLINE_PAGE =
  '<!doctype html><html lang="mn"><head><meta charset="utf-8">' +
  '<meta name="viewport" content="width=device-width, initial-scale=1">' +
  '<title>Zombie Mongolia</title></head>' +
  '<body style="margin:0;height:100vh;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:16px;' +
  'background:#0a0c12;color:#e8e4da;font-family:Segoe UI,Arial,sans-serif;text-align:center;padding:0 24px">' +
  '<b style="color:rgb(255,192,64);font-size:20px">Интернэт холболт алга</b>' +
  '<div>Zombie Mongolia тоглохын тулд интернэтэд холбогдоно уу.</div>' +
  '<button onclick="location.reload()" style="padding:10px 18px;border:0;border-radius:8px;background:rgb(255,192,64);' +
  'color:#111;font:inherit;font-weight:700">Дахин оролдох</button></body></html>';

self.addEventListener('fetch', function (event) {
  if (event.request.mode !== 'navigate') return; // .js, .wasm, .data: хөтөч өөрөө
  event.respondWith(fetch(event.request).catch(function () {
    return new Response(OFFLINE_PAGE, { headers: { 'Content-Type': 'text/html; charset=utf-8' } });
  }));
});
