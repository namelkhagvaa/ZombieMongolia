// Zombie Mongolia — хамтарсан тоглолтын relay сервер (хөтөч, компьютер, Android хоорондоо).
//
// Тоглоомын тооцоолол хийхгүй: хоёр тоглогчийн мессежийг бие бие рүү нь дамжуулна (src/Net.cpp).
// Хоёр төрлийн холболт хүлээн авна; нэг өрөөнд аль ч төрлийнх нь орж болно:
//
//   WebSocket (хөтөч, PORT, анхдагч 8090; nginx-ийн /relay-ээр wss://):
//     Хост:  ws://<сервер>/?host        -> {"type":"room","code":4821}
//     Зочин: ws://<сервер>/?join=4821    -> хоёуланд нь {"type":"paired"}
//     Дараа нь binary мессеж бүр нөгөө тал руу дамжина. Эхний байт 0xFF бол "хаяж болох" мессеж (snapshot,
//     дүрийн байдал): 0xFF-ийг хасаад дамжуулна, хүлээн авагчийн дараалал дүүрсэн бол хаяна.
//
//   TCP (Windows exe, Android апп, TCP_PORT, анхдагч 8091): мессеж бүр [u32 урт LE][u8 төрөл][өгөгдөл],
//     урт = 1 + өгөгдлийн байт. Төрөл: 1 удирдлага (сервер -> клиент, JSON), 2 тоглоомын өгөгдөл,
//     3 амьд байгааг илтгэх (клиент -> сервер, хоосон), 4 эхний мессеж ("host" эсвэл "join=4821"),
//     5 хаяж болох тоглоомын өгөгдөл (хүлээн авагчид 2 болж очно).
//
//   Удирдлагын мессежүүд хоёуланд ижил: room, paired, left ({"type":"left"} — нөгөө тал гарсан эсвэл тасарсан),
//   error ({"type":"error","reason":"notfound"|"full"|"bad"}, дараа нь холболт хаагдана).
//
//   Нээлттэй өрөө (Gold-той тоглолт): backend (backend/game/logic.py room_ticket) гарын үсэгтэй тасалбар өгнө.
//     Хост:  ?host&ticket=<тасалбар>   Зочин: ?ticket=<тасалбар>
//   Тасалбар = base64url(JSON {r: өрөө, p: тоглогч, role: "host"|"guest", exp}) + "." + hex(HMAC-SHA256(RELAY_SECRET)).
//   Өрөөг backend сонгосон тул зочин код оруулахгүй; тасалбаргүй хүн тэр өрөөнд орж чадахгүй.
//
//   Дахин холбогдох (тасалбартай өрөө): хостын холболт тасарвал өрөө GRACE_MS хадгалагдаж, зочин холбогдсон хэвээр
//   ("left" авна) хүлээнэ; хост ижил тасалбараар буцаж ормогц хоёуланд "paired". Зочин тасарвал ижил тоглогчийн
//   тасалбараар буцаж орно (хуучин, хариу өгөхгүй болсон холболтыг орлоно).
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
const GRACE_MS = 45000;      // тасарсан хостыг (тасалбартай өрөө) хүлээх хугацаа
const BACKLOG_LIMIT = 65536; // хүлээн авагчийн илгээгээгүй дараалал: үүнээс их бол хаяж болох мессежийг хаяна
const DROP_MARK = 0xff;      // WebSocket: хаяж болох мессежийн эхний байт
const MAX_ROOMS_PER_IP = 8;  // нэг IP-ээс код олгох өрөө (тасалбаргүй): серверийг дүүргэхээс

const RELAY_SECRET = process.env.RELAY_SECRET || '';

const rooms = new Map(); // code (кодоор) эсвэл "room:<id>" (тасалбараар) -> Room
const roomsByIp = new Map();

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

// Хүлээн авагч руу дамжуулна. Хаяж болох мессеж (snapshot): хүлээн авагчийн сүлжээ удаан бол хаяна, эс бөгөөс
// хоцрогдол секундээр хуримтлагдана.
function deliver(target, data, droppable) {
  if (!target) return;
  if (droppable && target.backlog() > BACKLOG_LIMIT) return;
  target.send(data);
}

function countIp(ip, delta) {
  const value = (roomsByIp.get(ip) || 0) + delta;
  if (value > 0) roomsByIp.set(ip, value);
  else roomsByIp.delete(ip);
}

function deleteRoom(code, room) {
  if (rooms.get(code) !== room) return;
  rooms.delete(code);
  if (room.ip) countIp(room.ip, -1);
  clearTimeout(room.hostGone);
}

// Нэг холболт (WebSocket эсвэл TCP): ижил интерфэйс.
//   peer.control(obj), peer.send(buffer), peer.backlog(), peer.close(), peer.ip,
//   peer.onData = (buffer, droppable) => {}, peer.onClose = () => {}
function attach(peer, hello) {
  const params = new URLSearchParams(hello);
  const ticket = params.has('ticket') ? readTicket(params.get('ticket')) : null;
  if (params.has('ticket') && !ticket) { peer.control({ type: 'error', reason: 'bad' }); peer.close(); return; }
  if (ticket && ticket.role === 'guest') {
    joinRoom(peer, `room:${ticket.r}`, ticket.p);
    return;
  }
  if (params.has('host')) {
    if (ticket) { hostTicketRoom(peer, `room:${ticket.r}`, ticket.r, ticket); return; }
    // Тасалбаргүй (дотоод сүлжээ, хуучин хувилбар): санамсаргүй код
    const code = rooms.size < MAX_ROOMS && (roomsByIp.get(peer.ip) || 0) < MAX_ROOMS_PER_IP ? newCode() : 0;
    if (!code) { peer.control({ type: 'error', reason: 'full' }); peer.close(); return; }
    const room = { host: peer, guest: null, guestPlayer: null, ticket: false, ip: peer.ip, hostGone: null };
    rooms.set(code, room);
    countIp(peer.ip, 1);
    peer.control({ type: 'room', code });
    bindHost(peer, room, code);
    return;
  }

  const code = Number(params.get('join'));
  if (!code) { peer.control({ type: 'error', reason: 'bad' }); peer.close(); return; }
  joinRoom(peer, code, null);
}

// Тасалбартай хост: дахин холбогдвол хуучин холболтыг орлож, өрөө (ба холбогдсон зочин) хэвээр үргэлжилнэ.
// Өөр тоглогч (тулаанд ялж шинэ хост болсон) зөвхөн илүү шинэ тасалбараар орлоно: ялагдсан хостын хуучин (хүчинтэй хэвээр)
// тасалбар шинэ хостыг гаргахгүй (эс бөгөөс хоёулаа ээлжлэн бие биеэ гаргаж, шинэ зочин орж чадахгүй).
function hostTicketRoom(peer, code, id, ticket) {
  let room = rooms.get(code);
  if (room && room.hostPlayer !== ticket.p && ticket.exp < room.hostExp) {
    peer.control({ type: 'error', reason: 'replaced' });
    peer.close();
    return;
  }
  if (room) {
    clearTimeout(room.hostGone);
    room.hostGone = null;
    const previous = room.host;
    room.host = peer;
    if (previous) previous.close(); // (onClose нь room.host !== peer тул юу ч хийхгүй)
  } else {
    if (rooms.size >= MAX_ROOMS) { peer.control({ type: 'error', reason: 'full' }); peer.close(); return; }
    room = { host: peer, guest: null, guestPlayer: null, ticket: true, ip: null, hostGone: null };
    rooms.set(code, room);
  }
  room.hostPlayer = ticket.p;
  room.hostExp = ticket.exp;
  peer.control({ type: 'room', code: id });
  bindHost(peer, room, code);
  if (room.guest) {
    peer.control({ type: 'paired' });
    room.guest.control({ type: 'paired' });
  }
}

function bindHost(peer, room, code) {
  peer.onData = (data, droppable) => deliver(room.guest, data, droppable);
  peer.onClose = () => {
    if (room.host !== peer) return; // орлогдсон
    room.host = null;
    if (room.ticket) {
      // Хост буцаж орохыг хүлээнэ: зочин холбогдсон хэвээр ("left": тоглоом хүлээх горимд орно)
      if (room.guest) room.guest.control({ type: 'left' });
      room.hostGone = setTimeout(() => {
        if (room.host) return;
        deleteRoom(code, room);
        if (room.guest) { room.guest.control({ type: 'left' }); room.guest.close(); }
      }, GRACE_MS);
      return;
    }
    deleteRoom(code, room);
    if (room.guest) { room.guest.control({ type: 'left' }); room.guest.close(); }
  };
}

function joinRoom(peer, code, player) {
  const room = rooms.get(code);
  if (!room) { peer.control({ type: 'error', reason: 'notfound' }); peer.close(); return; }
  if (room.guest) {
    // Ижил тоглогч дахин холбогдож байна (хуучин холболт хариу өгөхгүй болсон): орлоно. Өөр хүн бол дүүрсэн.
    if (player === null || room.guestPlayer !== player) { peer.control({ type: 'error', reason: 'full' }); peer.close(); return; }
    const previous = room.guest;
    room.guest = null;
    previous.close();
  }
  room.guest = peer;
  room.guestPlayer = player;
  if (room.host) {
    room.host.control({ type: 'paired' });
    peer.control({ type: 'paired' });
  }
  peer.onData = (data, droppable) => deliver(room.host, data, droppable);
  peer.onClose = () => {
    if (room.guest !== peer) return; // орлогдсон
    room.guest = null; // хост өрөөндөө үлдэж, зочин буцаж орох эсвэл дараагийн зочныг хүлээнэ
    if (room.host) room.host.control({ type: 'left' });
  };
}

// ---------------------------------------------------------------- WebSocket (хөтөч)
const server = http.createServer((req, res) => {
  // Хостингийн health check: сервер ажиллаж байгааг шалгана
  res.writeHead(200, { 'Content-Type': 'text/plain; charset=utf-8' });
  res.end(`Zombie Mongolia relay: ${rooms.size} rooms\n`);
});
const wss = new WebSocketServer({ server, maxPayload: MAX_MESSAGE });

function clientIp(req, socket) {
  const forwarded = req && req.headers['x-forwarded-for'];
  return (forwarded ? String(forwarded).split(',')[0].trim() : '') || (socket && socket.remoteAddress) || '';
}

wss.on('connection', (ws, req) => {
  ws.isAlive = true;
  ws.on('pong', () => { ws.isAlive = true; });
  const peer = {
    ip: clientIp(req, req.socket),
    onData: () => {},
    onClose: () => {},
    control: (obj) => { if (ws.readyState === ws.OPEN) ws.send(JSON.stringify(obj)); },
    send: (data) => { if (ws.readyState === ws.OPEN) ws.send(data, { binary: true }); },
    backlog: () => ws.bufferedAmount,
    close: () => ws.close(),
  };
  ws.on('message', (data, isBinary) => {
    if (!isBinary) return;
    const buffer = Buffer.isBuffer(data) ? data : Buffer.from(data);
    if (buffer.length > 1 && buffer[0] === DROP_MARK) peer.onData(buffer.subarray(1), true);
    else peer.onData(buffer, false);
  });
  ws.on('close', () => peer.onClose());
  attach(peer, new URL(req.url, 'http://relay').search);
});

// Тасарсан (хариу өгөхгүй болсон) холболтуудыг 15 секунд тутам цэвэрлэнэ.
setInterval(() => {
  for (const ws of wss.clients) {
    if (!ws.isAlive) { ws.terminate(); continue; }
    ws.isAlive = false;
    ws.ping();
  }
}, 15000);

// ---------------------------------------------------------------- TCP (Windows exe, Android апп)
const KIND_CONTROL = 1, KIND_DATA = 2, KIND_KEEPALIVE = 3, KIND_HELLO = 4, KIND_DATA_DROPPABLE = 5;

function frame(kind, payload) {
  const out = Buffer.alloc(5 + payload.length);
  out.writeUInt32LE(1 + payload.length, 0);
  out[4] = kind;
  payload.copy(out, 5);
  return out;
}

const tcp = net.createServer((socket) => {
  socket.setNoDelay(true);
  socket.setKeepAlive(true, 15000);
  socket.setTimeout(45000); // клиент 10 сек тутам амьд байгаагаа мэдэгдэнэ
  let buffer = Buffer.alloc(0);
  let attached = false, closed = false;
  const peer = {
    ip: socket.remoteAddress || '',
    onData: () => {},
    onClose: () => {},
    control: (obj) => { if (!closed && socket.writable) socket.write(frame(KIND_CONTROL, Buffer.from(JSON.stringify(obj)))); },
    send: (data) => { if (!closed && socket.writable) socket.write(frame(KIND_DATA, data)); },
    backlog: () => socket.writableLength,
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
      } else if (kind === KIND_DATA || kind === KIND_DATA_DROPPABLE) {
        peer.onData(Buffer.from(payload), kind === KIND_DATA_DROPPABLE);
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
