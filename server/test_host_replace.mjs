// Туршилт: relay-ийн хостын байр (тасалбартай өрөө). node test_host_replace.mjs
import { spawn } from 'node:child_process';
import crypto from 'node:crypto';
import WebSocket from 'ws';

const SECRET = 'test-secret', PORT = 18090;
const relay = spawn(process.execPath, ['relay.js'], {
  env: { ...process.env, PORT: String(PORT), TCP_PORT: '18091', RELAY_SECRET: SECRET }, stdio: 'ignore',
});
const wait = (ms) => new Promise((r) => setTimeout(r, ms));

function ticket(room, player, role, exp) {
  const body = Buffer.from(JSON.stringify({ r: room, p: player, role, exp })).toString('base64url');
  return body + '.' + crypto.createHmac('sha256', SECRET).update(body).digest('hex');
}

function connect(query) {
  const ws = new WebSocket(`ws://127.0.0.1:${PORT}/${query}`);
  ws.log = [];
  ws.closed = false;
  ws.on('message', (data, binary) => { if (!binary) ws.log.push(JSON.parse(data.toString()).type + ':' + (JSON.parse(data.toString()).reason || '')); });
  ws.on('close', () => { ws.closed = true; });
  return ws;
}

let failed = 0;
const check = (name, ok) => { console.log((ok ? 'OK   ' : 'FAIL ') + name); if (!ok) failed++; };

await wait(800);
const now = Math.floor(Date.now() / 1000);
const loser = connect('?host&ticket=' + ticket(7, 1, 'host', now + 3600));
await wait(300);
const winner = connect('?host&ticket=' + ticket(7, 2, 'host', now + 3700)); // шинэ тасалбар (ялагч)
await wait(300);
check('winner takes the room', !winner.closed && winner.log.includes('room:'));
check('loser connection replaced', loser.closed);
const again = connect('?host&ticket=' + ticket(7, 1, 'host', now + 3600)); // ялагдагч хуучин тасалбараар буцаж оров
await wait(300);
check('loser with an older ticket is refused', again.closed && again.log.includes('error:replaced'));
check('winner keeps the room', !winner.closed);
const resume = connect('?host&ticket=' + ticket(7, 2, 'host', now + 3700)); // ялагч өөрөө дахин холбогдов
await wait(300);
check('same host reconnects', !resume.closed && resume.log.includes('room:'));
const guest = connect('?ticket=' + ticket(7, 9, 'guest', now + 3700));
await wait(300);
check('a new guest pairs with the winner', guest.log.includes('paired:') && resume.log.includes('paired:'));

for (const ws of [loser, winner, again, resume, guest]) ws.close();
relay.kill();
process.exit(failed ? 1 : 0);
