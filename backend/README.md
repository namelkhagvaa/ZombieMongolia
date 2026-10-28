# Zombie Mongolia — backend (Django)

Тоглогчийн бүртгэл, аймгийн эрх, QPay төлбөр, лог. Бодит сервер: **https://zombie-api.apps.mn**
- API: `https://zombie-api.apps.mn/api/` (тоглоом: Windows, Android, хөтөч)
- Админ самбар (React, `admin/`): `https://zombie-api.apps.mn/admin/`

## Зарчим
- Бүртгэл: **нэр, утас, нууц үг**. Утсыг verify.mn-ээр баталгаажуулна: тоглогч өөрийн утаснаас **144773**
  дугаарт тоглоомын харуулсан 6 оронтой кодыг SMS-ээр илгээнэ (1 SMS = 150₮, тоглогч төлнө). Нэг дугаар = нэг данс.
- **Нэг данс 2 төхөөрөмж** (`max_devices`, админаас өөрчилнө) үнэгүй бүртгэнэ: шинэ төхөөрөмжөөс нэвтрэхэд сул байр
  байвал шууд бүртгэгдэнэ. Байр дүүрсэн бол төхөөрөмж солих төлбөр (хамгийн эрт бүртгэсэн нь чөлөөлөгдөнө).
  Нэг төхөөрөмж зөвхөн нэг дансанд.
- **Нэг зэрэг нэг л төхөөрөмж** тоглоно: өөр төхөөрөмж дээр нэвтрэхэд өмнөх нэвтрэлт хүчингүй болж, тэр төхөөрөмж
  дараагийн хүсэлтэд (тоглоом минут тутам шалгана) "өөр төхөөрөмж дээр нэвтэрсэн" гэж гарна.
- **Бүх аймаг үнэгүй.** (Аймгийн багц, Улаанбаатарын төлбөр зогссон; хуучин талбарууд хуучин нэхэмжлэхэд үлдсэн.)
- **Gold** зөвхөн серверт: шинэ тоглогч `start_gold` (анх 500). Өөрчлөлт бүр `GoldTransaction`-д, `ref` нь давтагдашгүй
  тул нэг үйл явдлыг хоёр удаа тооцохгүй. Газрын зургийн дэлгүүрийн сайжруулалтыг (хохирол, буудах хурд, сумны
  хайрцаг, амь, цэрэг) Gold-оор серверээр авна: дараагийн түвшин = үнэ × (түвшин + 1).
- Тоглолтын үеийн зоос (зомби алж олдог) төхөөрөмж дээр, тухайн тоглолтод л (сум, гранат, амь, түр цэрэг).
- Бүх үнэ, тоо админы **Тохиргоо**-оос өөрчлөгдөнө.
- Тоглогч ба админы үйлдэл бүр **Лог** (Event)-д бичигдэнэ; засагдахгүй.

## Хөгжүүлэлт (Windows)
```
cd backend
py -3.12 -m venv .venv
.venv\Scripts\pip install -r requirements.txt
.venv\Scripts\python manage.py migrate
.venv\Scripts\python manage.py test game
```
Өөрийн компьютер дээр ажиллуулах бол `.env`-д `DEBUG=1`, `DB_ENGINE=sqlite`. Туршилтын горимууд (`QPAY_MODE=mock`,
`SMS_MODE=console`) зөвхөн автомат тестэд зориулагдсан: бодит мессеж, төлбөр явахгүй. `DEBUG=0` үед сервер эдгээр
горимоор асахаас татгалзана.

## API (`/api/`)
| Арга | Зам | Тайлбар |
|---|---|---|
| POST | `register/start/` | `{name, phone, password, device_id, platform}` → `verification_id`, `sms: {shortcode, text, sms_uri, instruction}` |
| POST | `register/confirm/` | `{verification_id}` → `token`, `account`. Мессеж ирээгүй бол 409 `not_verified`: тоглоом хэдэн секунд тутам дахин асууна |
| POST | `login/` | `{phone, password, device_id, platform}` → `token`, `account` (`account.device_ok=false` бол төхөөрөмж солих) |
| POST | `logout/` | |
| POST | `password/reset/start/`, `password/reset/confirm/` | `{phone}` → заавар; `{verification_id, password}` (мессеж илгээсний дараа) |
| GET | `account/` | Gold, сайжруулалтын түвшин (`upgrades`), дэлгүүрийн үнэ (`shop`), аймгууд (бүгд), төхөөрөмж |
| POST | `shop/buy/` | `{item, level}` (item: damage, fire_rate, magazine, health, soldier; level: тоглоомын харж буй одоогийн түвшин) → `account`. Түвшин таарахгүй бол 409 `shop_changed`, Gold хүрэлцэхгүй бол 402 `not_enough_gold` |
| POST | `provinces/claim/` | `{province}` (хуучин хувилбарт: бүгд үнэгүй тул үргэлж амжилттай) |
| POST | `provinces/start/` | `{province}` аймаг эхлэхэд (шалгалт + лог) |
| POST | `invoices/` | `{kind: "pack" \| "device"}` → QPay QR, банкны апп холбоосууд |
| GET | `invoices/<id>/` | төлөв (төлөгдсөн бол эрх олгогдоно) |
| GET/POST | `qpay/callback/<id>/<key>/` | QPay төлбөр орсныг мэдэгдэнэ (QPay-ээс дахин шалгаж байж эрх олгоно) |
| POST | `events/` | `{events: [{kind, data}]}` тоглоомын үйл явдал (лог) |
| GET/PUT | `progress/` | тоглоомын хадгалалт |
| | `manage/...` | админ самбарын API (staff хэрэглэгч) |

Нэвтэрсний дараа: толгой `Authorization: Bearer <token>`. Алдаа: `{"ok": false, "error": "<код>", "message": "..."}`.
401 `session_replaced`: өөр төхөөрөмж дээр нэвтэрсэн.

## Бүртгэл ба утас баталгаажуулалт
- Бүртгэл: нэр, утас, нууц үгээр шууд (`register/`), эхлэлийн Gold олгогдоно.
- Утсаа дараа нь баталгаажуулна (`phone/verify/start/`, `phone/verify/confirm/`; 144773 руу мессеж), нэг удаа
  `verify_bonus` Gold.
- **Баталгаажаагүй тоглогч Gold зарцуулна, нэмэгдэхгүй:** үе шатны шагнал, хамт давсны шагнал, тулааны хожил, Gold
  татах олгогдохгүй (`reward_skipped` логт). Тулаанд ялагдвал барьцаа нь хасагдаж шатна (хэнд ч очихгүй); ялбал юу ч
  авахгүй, баталгаажсан ялагдагчийн барьцаа буцна. Эхлэлийн Gold, худалдан авалт, буцаалт, админы Gold хэвээр.
- Дугаарыг баталгаажуулалгүй эзэлсэн бол жинхэнэ эзэн SMS-ээр бүртгүүлэхэд (`register/start/`) эсвэл нууц үг
  сэргээхэд дугаар түүнийх болно; эзэлсэн данс дугааргүй болно.
- Шинэчлэлтээс өмнө бүртгүүлсэн бүх тоглогч баталгаажсан гэж тооцогдоно (migration 0016).

## QPay ба verify.mn
- QPay: `QPAY_MODE=production`, `QPAY_USERNAME`, `QPAY_PASSWORD`, `QPAY_INVOICE_CODE`,
  `PUBLIC_BASE_URL=https://zombie-api.apps.mn` (QPay төлбөр орсныг энэ хаяг руу мэдэгдэнэ).
  v2: `/v2/auth/token`, `/v2/invoice`, `/v2/payment/check`. И-баримтын тохиргоог QPay-ээс лавлана.
- verify.mn: `SMS_MODE=verifymn`, `VERIFYMN_API_KEY` (`vrf_...`). `POST /sessions` (код үүсгэнэ) ба
  `GET /sessions/<id>` (`sessionStatus` = `VERIFIED`); сесс 5 минут.
- Нууц мэдээллийг зөвхөн серверийн `backend/.env`-д. Git, чат, зурагт гаргахгүй.

## Серверт байршуулах (Ubuntu, nginx, MySQL) — zombie-api.apps.mn
0. DNS: `zombie-api.apps.mn` → серверийн IP (A бичлэг).
1. MySQL өгөгдлийн сан (utf8mb4: монгол үсэг):
   ```sql
   CREATE DATABASE zombie CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
   CREATE USER 'zombie'@'localhost' IDENTIFIED BY '<урт нууц үг>';
   GRANT ALL ON zombie.* TO 'zombie'@'localhost';
   ```
2. Код: `backend/` хавтсыг `/opt/zombie/backend` руу хуулна (`.venv`, `db.sqlite3`, `.env`-ийг хуулахгүй):
   ```
   sudo apt install python3-venv python3-dev default-libmysqlclient-dev build-essential pkg-config
   cd /opt/zombie/backend
   python3 -m venv .venv && .venv/bin/pip install -r requirements.txt mysqlclient gunicorn
   ```
3. `.env`: `.env.example`-г хуулж бөглөнө (`DEBUG=0`, шинэ `SECRET_KEY`, MySQL, QPay, verify.mn).
   `sudo chown root:www-data .env && sudo chmod 640 .env`
4. ```
   .venv/bin/python manage.py migrate
   .venv/bin/python manage.py collectstatic --noinput
   .venv/bin/python manage.py createsuperuser      # урт, хүчтэй нууц үгтэй
   .venv/bin/python manage.py check --deploy
   ```
5. systemd (`/etc/systemd/system/zombie-api.service`):
   ```
   [Unit]
   Description=Zombie Mongolia API
   After=network.target mysql.service

   [Service]
   WorkingDirectory=/opt/zombie/backend
   ExecStart=/opt/zombie/backend/.venv/bin/gunicorn zmserver.wsgi -b 127.0.0.1:8001 -w 3 --timeout 30
   Restart=always
   User=www-data

   [Install]
   WantedBy=multi-user.target
   ```
   `sudo systemctl daemon-reload && sudo systemctl enable --now zombie-api`
6. Админ самбар: компьютер дээрээ `cd admin && npm install && npm run build`, гарсан `admin/dist`-ийг серверийн
   `/opt/zombie/admin/dist` руу хуулна.
7. nginx (`/etc/nginx/sites-available/zombie-api`, `sites-enabled` руу холбоно):
   ```
   server {
       listen 80;
       server_name zombie-api.apps.mn;
       client_max_body_size 1m;

       location = / { return 302 /admin/; }
       location /admin/ {
           alias /opt/zombie/admin/dist/;
           try_files $uri $uri/ /admin/index.html;
       }
       location /api/static/ { alias /opt/zombie/backend/static/; }
       location /api/ {
           proxy_pass http://127.0.0.1:8001;
           proxy_set_header Host $host;
           proxy_set_header X-Forwarded-For $remote_addr;
           proxy_set_header X-Forwarded-Proto $scheme;
       }
   }
   ```
   `sudo nginx -t && sudo systemctl reload nginx`, дараа нь HTTPS: `sudo certbot --nginx -d zombie-api.apps.mn`
   (HTTP-ийг HTTPS руу шилжүүлэхийг сонгоно).
8. Шалгах: `curl https://zombie-api.apps.mn/api/account/` → `{"ok": false, "error": "auth", ...}` (401) бол API ажиллаж байна.
   `https://zombie-api.apps.mn/admin/` нээгдэж, superuser-ээр нэвтэрнэ.
9. Өгөгдлийн санг өдөр бүр нөөцлөнө (`mysqldump`), ялангуяа төлбөрийн мэдээлэл.

Шинэчлэх: кодоо хуулаад `migrate`, `collectstatic`, `sudo systemctl restart zombie-api`.
