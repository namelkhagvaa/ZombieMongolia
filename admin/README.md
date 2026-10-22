# Zombie Mongolia — админ самбар

React + TypeScript + Ant Design. Django-ийн `/api/manage/` API-тай ажиллана (backend/game/manage_api.py).

## Хөгжүүлэлт

```
cd backend && .venv\Scripts\python.exe manage.py runserver 127.0.0.1:8010
cd admin && npm install && npm run dev
```

http://localhost:5173/admin/ — Django-ийн staff хэрэглэгчээр нэвтэрнэ (`manage.py createsuperuser`).
`/api/` хүсэлтийг Vite 127.0.0.1:8010 руу дамжуулна.

## Серверт

`npm run build` → `admin/dist`-г серверт хуулж nginx-ээр `/admin/` дээр үйлчилнэ (backend/README.md, 6-р алхам).

## Хуудсууд

- Хянах самбар: тоглогч, идэвх, орлого (30 хоног), платформ, аймгуудын статистик
- Тоглогчид: хайх, шүүх; тоглогчийн дэлгэрэнгүй — эрх засах, аймаг нээх/хаах, төхөөрөмж суллах, хаах, нууц үг тавих
- Төлбөр: QPay нэхэмжлэх, QPay-ээс шалгах, гараар төлсөн гэж тэмдэглэх
- Үйлдлийн лог: тоглогч ба админы бүх үйлдэл
- Тохиргоо: үнэ, үнэгүй эрхийн тоо (зөвхөн superuser)
