// Zombie Mongolia — хамтарсан тоглолтын relay сервер (хөтөч, компьютер, Android хоорондоо).
//
// Тоглоомын тооцоолол хийхгүй: хоёр тоглогчийн мессежийг бие бие рүү нь дамжуулна (src/Net.cpp).
// Хоёр төрлийн холболт хүлээн авна; нэг өрөөнд аль ч төрлийнх нь орж болно:
//
//   WebSocket (хөтөч, PORT, анхдагч 8090; nginx-ийн /relay-ээр wss://):
//     Хост:  ws://<сервер>/?host        -> {"type":"room","code":4821}
//     Зочин: ws://<сервер>/?join=4821    -> хоёуланд нь {"type":"paired"}
//     Дараа нь binary мессеж бүр нөгөө тал руу дамжина.
//
//   TCP (Windows exe, Android апп, TCP_PORT, анхдагч 8091): мессеж бүр [u32 урт LE][u8 төрөл][өгөгдөл],
//     урт = 1 + өгөгдлийн байт. Төрөл: 1 удирдлага (сервер -> клиент, JSON), 2 тоглоомын өгөгдөл,
//     3 амьд байгааг илтгэх (клиент -> сервер, хоосон), 4 эхний мессеж ("host" эсвэл "join=4821").
//
//   Удирдлагын мессежүүд хоёуланд ижил: room, paired, left ({"type":"left"} — нөгөө тал гарсан),
//   error ({"type":"error","reason":"notfound"|"full"|"bad"}, дараа нь холболт хаагдана).
//
//   Нээлттэй өрөө (Gold-той тоглолт): backend (backend/game/logic.py room_ticket) гарын үсэгтэй тасалбар өгнө.
//     Хост:  ?host&ticket=<тасалбар>   Зочин: ?ticket=<тасалбар>
//   Тасалбар = base64url(JSON {r: өрөө, p: тоглогч, role: "host"|"guest", exp}) + "." + hex(HMAC-SHA256(RELAY_SECRET)).
//   Өрөөг backend сонгосон тул зочин код оруулахгүй; тасалбаргүй хүн тэр өрөөнд орж чадахгүй.
//
// Ажиллуулах:  cd server && npm install && RELAY_SECRET=<backend-ийнхтэй ижил> npm start
const crypto = require('crypto');
const http = require('http');
const net = require('net');
const { WebSocketServer } = require('ws');

const PORT = Number(process.env.PORT) || 8090;
const TCP_PORT = Number(process.env.TCP_PORT) || 8091;
const MAX_ROOMS = 2000;
const MAX_MESSAGE = 1 << 20; // 1 MB: snapshot хэдэн арван KB байдаг

const RELAY_SECRET = process.env.RELAY_SECRET || '';

const rooms = new Map(); // code (кодоор) эсвэл "room:<id>" (тасалбараар) -> { host: Peer, guest: Peer|null }

// Тасалбарыг шалгаж агуулгыг буцаана; буруу, хугацаа нь дууссан бол null.
function readTicket(ticket) {
  if (!RELAY_SECRET || typeof ticket !== 'string') return null;
  const dot = ticket.indexOf('.');
  if (dot <= 0) return null;
  const body = ticket.slice(0, dot), signature = ticket.slice(dot + 1);
  const expected = crypto.createHmac('sha256', RELAY_SECRET).update(body).digest('hex');
  if (signature.length !== expected.length || !crypto.timingSafeEqual(Buffer.from(signature), Buffer.from(expected))) return null;
  try {
    const data = JSON.parse(Buffer.from(body, 'base64url').toString('utf8'));
    if (!Number.isInteger(data.r) || (data.role !== 'host' && data.role !== 'guest') || !(data.exp * 1000 > Date.now())) return null;
    return data;
  } catch (e) {
    return null;
  }
}

function newCode() {
  for (let i = 0; i < 100; i++) {
    const code = 1000 + Math.floor(Math.random() * 9000);
    if (!rooms.has(code)) return code;
  }
  return 0;
}

// Нэг холболт (WebSocket эсвэл TCP): ижил интерфэйс.
//   peer.control(obj), peer.send(buffer), peer.close(), peer.onData = (buffer) => {}, peer.onClose = () => {}
function attach(peer, hello) {
  const params = new URLSearchParams(hello);
  const ticket = params.has('ticket') ? readTicket(params.get('ticket')) : null;
  if (params.has('ticket') && !ticket) { peer.control({ type: 'error', reason: 'bad' }); peer.close(); return; }
  if (ticket && ticket.role === 'guest') {
    joinRoom(peer, `room:${ticket.r}`);
    return;
  }
  if (params.has('host')) {
    // Тасалбартай хост: өрөөний дугаар нь backend-ийнх; дахин холбогдвол хуучин холболтыг орлоно
    const code = ticket ? `room:${ticket.r}` : rooms.size < MAX_ROOMS ? newCode() : 0;
    if (!code) { peer.control({ type: 'error', reason: 'full' }); peer.close(); return; }
    const previous = rooms.get(code);
    if (previous && ticket) { rooms.delete(code); previous.host.close(); }
    const room = { host: peer, guest: null };
    rooms.set(code, room);
    peer.control({ type: 'room', code: ticket ? ticket.r : code });
    peer.onData = (data) => { if (room.guest) room.guest.send(data); };
    peer.onClose = () => {
      if (rooms.get(code) === room) rooms.delete(code);
      if (room.guest) { room.guest.control({ type: 'left' }); room.guest.close(); }
    };
    return;
  }

  const code = Number(params.get('join'));
  if (!code) { peer.control({ type: 'error', reason: 'bad' }); peer.close(); return; }
  joinRoom(peer, code);
}

function joinRoom(peer, code) {
  const room = rooms.get(code);
  if (!room) { peer.control({ type: 'error', reason: 'notfound' }); peer.close(); return; }
  if (room.guest) { peer.control({ type: 'error', reason: 'full' }); peer.close(); return; }
  room.guest = peer;
  room.host.control({ type: 'paired' });
  peer.control({ type: 'paired' });
  peer.onData = (data) => room.host.send(data);
  peer.onClose = () => {
    if (room.guest !== peer) return;
    room.guest = null; // хост өрөөндөө үлдэж, дараагийн зочныг хүлээнэ
    room.host.control({ type: 'left' });
  };
}

// ---------------------------------------------------------------- WebSocket (хөтөч)
const server = http.createServer((req, res) => {
  // Хостингийн health check: сервер ажиллаж байгааг шалгана
  res.writeHead(200, { 'Content-Type': 'text/plain; charset=utf-8' });
  res.end(`Zombie Mongolia relay: ${rooms.size} rooms\n`);
});
const wss = new WebSocketServer({ server, maxPayload: MAX_MESSAGE });

wss.on('connection', (ws, req) => {
  ws.isAlive = true;
  ws.on('pong', () => { ws.isAlive = true; });
  const peer = {
    onData: () => {},
    onClose: () => {},
    control: (obj) => { if (ws.readyState === ws.OPEN) ws.send(JSON.stringify(obj)); },
    send: (data) => { if (ws.readyState === ws.OPEN) ws.send(data, { binary: true }); },
    close: () => ws.close(),
  };
  ws.on('message', (data, isBinary) => { if (isBinary) peer.onData(data); });
  ws.on('close', () => peer.onClose());
  attach(peer, new URL(req.url, 'http://relay').search);
});

// Тасарсан (хариу өгөхгүй болсон) холболтуудыг 30 секунд тутам цэвэрлэнэ.
setInterval(() => {
  for (const ws of wss.clients) {
    if (!ws.isAlive) { ws.terminate(); continue; }
    ws.isAlive = false;
    ws.ping();
  }
}, 30000);

// ---------------------------------------------------------------- TCP (Windows exe, Android апп)
const KIND_CONTROL = 1, KIND_DATA = 2, KIND_KEEPALIVE = 3, KIND_HELLO = 4;

function frame(kind, payload) {
  const out = Buffer.alloc(5 + payload.length);
  out.writeUInt32LE(1 + payload.length, 0);
  out[4] = kind;
  payload.copy(out, 5);
  return out;
}

const tcp = net.createServer((socket) => {
  socket.setNoDelay(true);
  socket.setKeepAlive(true, 30000);
  socket.setTimeout(120000); // клиент 20 сек тутам амьд байгаагаа мэдэгдэнэ
  let buffer = Buffer.alloc(0);
  let attached = false, closed = false;
  const peer = {
    onData: () => {},
    onClose: () => {},
    control: (obj) => { if (!closed && socket.writable) socket.write(frame(KIND_CONTROL, Buffer.from(JSON.stringify(obj)))); },
    send: (data) => { if (!closed && socket.writable) socket.write(frame(KIND_DATA, data)); },
    close: () => { if (!closed) socket.end(); },
  };
  socket.on('data', (chunk) => {
    buffer = buffer.length ? Buffer.concat([buffer, chunk]) : chunk;
    while (buffer.length >= 4) {
      const length = buffer.readUInt32LE(0);
      if (length < 1 || length > MAX_MESSAGE) { socket.destroy(); return; }
      if (buffer.length < 4 + length) break;
      const kind = buffer[4];
      const payload = buffer.subarray(5, 4 + length);
      buffer = buffer.subarray(4 + length);
      if (!attached) {
        if (kind !== KIND_HELLO) { socket.destroy(); return; }
        attached = true;
        attach(peer, '?' + payload.toString('utf8'));
      } else if (kind === KIND_DATA) {
        peer.onData(Buffer.from(payload));
      }
      // KIND_KEEPALIVE: юу ч хийхгүй
    }
  });
  socket.on('timeout', () => socket.destroy());
  socket.on('error', () => {});
  socket.on('close', () => { closed = true; if (attached) peer.onClose(); });
});

server.listen(PORT, () => console.log(`relay websocket on :${PORT}`));
tcp.listen(TCP_PORT, () => console.log(`relay tcp on :${TCP_PORT}`));
