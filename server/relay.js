// Zombie Mongolia — хөтөч дээрх хамтарсан тоглолтын relay сервер.
//
// Тоглоомын тооцоолол хийхгүй: хоёр тоглогчийн мессежийг бие бие рүү нь дамжуулна (src/Net.cpp-ийн вэб хэсэг).
//   Хост:  ws://<сервер>/?host        -> {"type":"room","code":4821}
//   Зочин: ws://<сервер>/?join=4821    -> хоёуланд нь {"type":"paired"}
//   Дараа нь binary мессеж бүр нөгөө тал руу дамжина. Нэг нь гарвал нөгөөд {"type":"left"}.
//   Алдаа: {"type":"error","reason":"notfound"|"full"|"bad"} (холболт хаагдана).
//
// Ажиллуулах:  cd server && npm install && npm start    (PORT орчны хувьсагч, анхдагч 8090)
const http = require('http');
const { WebSocketServer } = require('ws');

const PORT = Number(process.env.PORT) || 8090;
const MAX_ROOMS = 2000;
const MAX_MESSAGE = 1 << 20; // 1 MB: snapshot хэдэн арван KB байдаг

const rooms = new Map(); // code -> { host, guest }

const server = http.createServer((req, res) => {
  // Хостингийн health check: сервер ажиллаж байгааг шалгана
  res.writeHead(200, { 'Content-Type': 'text/plain; charset=utf-8' });
  res.end(`Zombie Mongolia relay: ${rooms.size} rooms\n`);
});
const wss = new WebSocketServer({ server, maxPayload: MAX_MESSAGE });

function control(ws, message) {
  if (ws.readyState === ws.OPEN) ws.send(JSON.stringify(message));
}

function newCode() {
  for (let i = 0; i < 100; i++) {
    const code = 1000 + Math.floor(Math.random() * 9000);
    if (!rooms.has(code)) return code;
  }
  return 0;
}

wss.on('connection', (ws, req) => {
  const params = new URL(req.url, 'http://relay').searchParams;
  ws.isAlive = true;
  ws.on('pong', () => { ws.isAlive = true; });

  if (params.has('host')) {
    const code = rooms.size < MAX_ROOMS ? newCode() : 0;
    if (!code) { control(ws, { type: 'error', reason: 'full' }); ws.close(); return; }
    const room = { host: ws, guest: null };
    rooms.set(code, room);
    control(ws, { type: 'room', code });
    ws.on('message', (data, isBinary) => {
      if (isBinary && room.guest && room.guest.readyState === ws.OPEN) room.guest.send(data, { binary: true });
    });
    ws.on('close', () => {
      rooms.delete(code);
      if (room.guest) { control(room.guest, { type: 'left' }); room.guest.close(); }
    });
    return;
  }

  const code = Number(params.get('join'));
  const room = rooms.get(code);
  if (!code) { control(ws, { type: 'error', reason: 'bad' }); ws.close(); return; }
  if (!room) { control(ws, { type: 'error', reason: 'notfound' }); ws.close(); return; }
  if (room.guest) { control(ws, { type: 'error', reason: 'full' }); ws.close(); return; }
  room.guest = ws;
  control(room.host, { type: 'paired' });
  control(ws, { type: 'paired' });
  ws.on('message', (data, isBinary) => {
    if (isBinary && room.host.readyState === ws.OPEN) room.host.send(data, { binary: true });
  });
  ws.on('close', () => {
    if (room.guest !== ws) return;
    room.guest = null; // хост өрөөндөө үлдэж, дараагийн зочныг хүлээнэ
    control(room.host, { type: 'left' });
  });
});

// Тасарсан (хариу өгөхгүй болсон) холболтуудыг 30 секунд тутам цэвэрлэнэ.
setInterval(() => {
  for (const ws of wss.clients) {
    if (!ws.isAlive) { ws.terminate(); continue; }
    ws.isAlive = false;
    ws.ping();
  }
}, 30000);

server.listen(PORT, () => console.log(`relay listening on :${PORT}`));
