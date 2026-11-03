// Туршилт: нэг өрөөнд олон зочин (протокол 2). node test_multi_guest.mjs
import { spawn } from 'node:child_process';
import WebSocket from 'ws';

const PORT = 18190;
const relay = spawn(process.execPath, ['relay.js'], {
  env: { ...process.env, PORT: String(PORT), TCP_PORT: '18191' }, stdio: 'ignore',
});
const wait = (ms) => new Promise((r) => setTimeout(r, ms));

function connect(query) {
  const ws = new WebSocket(`ws://127.0.0.1:${PORT}/${query}`);
  ws.binaryType = 'nodebuffer';
  ws.control = [];
  ws.data = [];
  ws.closed = false;
  ws.on('message', (data, binary) => {
    if (binary) ws.data.push(Buffer.from(data));
    else ws.control.push(JSON.parse(data.toString()));
  });
  ws.on('close', () => { ws.closed = true; });
  return ws;
}

let failed = 0;
const check = (name, ok) => { console.log((ok ? 'OK   ' : 'FAIL ') + name); if (!ok) failed++; };

await wait(800);
const host = connect('?host');
await wait(300);
const code = host.control.find((m) => m.type === 'room').code;
check('host gets a room code', code > 0);

const guests = [];
for (let i = 0; i < 5; i++) { guests.push(connect(`?join=${code}`)); await wait(150); }
const slots = host.control.filter((m) => m.type === 'paired').map((m) => m.slot);
check('five guests get slots 0..4', JSON.stringify(slots) === '[0,1,2,3,4]');
check('every guest is paired', guests.every((g) => g.control.some((m) => m.type === 'paired')));

const sixth = connect(`?join=${code}`);
await wait(300);
check('sixth guest is refused (room full)', sixth.closed && sixth.control.some((m) => m.reason === 'full'));

// Зочин -> хост: эхний байт нь илгээгчийн дугаар
guests[3].send(Buffer.from([7, 8, 9]));
await wait(200);
const fromGuest = host.data.at(-1);
check('host receives guest data with its slot', fromGuest && fromGuest[0] === 3 && fromGuest.subarray(1).equals(Buffer.from([7, 8, 9])));

// Хост -> нэг зочин
host.send(Buffer.from([2, 42]));
await wait(200);
check('host message reaches only slot 2', guests[2].data.at(-1)?.equals(Buffer.from([42])) && guests[1].data.length === 0);

// Хост -> бүгд (0xFE)
host.send(Buffer.from([0xfe, 5, 6]));
await wait(200);
check('broadcast reaches all guests', guests.every((g) => g.data.at(-1)?.equals(Buffer.from([5, 6]))));

// Зочин гарахад хост тухайн дугаарыг мэднэ; шинэ зочин тэр дугаарыг авна
guests[1].close();
await wait(300);
check('host is told which slot left', host.control.some((m) => m.type === 'left' && m.slot === 1));
const late = connect(`?join=${code}`);
await wait(300);
check('a new guest takes the freed slot', host.control.filter((m) => m.type === 'paired').at(-1).slot === 1 && !late.closed);

// Хост гарвал бүх зочид мэднэ
host.close();
await wait(300);
check('guests are told the host left', [guests[0], guests[2], guests[3], guests[4], late].every((g) => g.control.some((m) => m.type === 'left')));

relay.kill();
console.log(failed ? `${failed} FAILED` : 'all passed');
process.exit(failed ? 1 : 0);
