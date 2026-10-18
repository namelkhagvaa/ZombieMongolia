#pragma once

// Хөтөч (WebAssembly) дээр ажиллахад хэрэгтэй жижиг ялгаанууд.
// Хөтөч дотор үндсэн урсгал frame бүрийн дараа удирдлагыг хөтчид буцааж байж зураг дэлгэцэнд гарна
// (-sASYNCIFY). raylib-ийн WindowShouldClose нь хөтөч дээр үргэлж 16 мс унтдаг тул оронд нь эдгээрийг хэрэглэнэ.

// Цонхыг хаах хүсэлт ирсэн эсэх (хөтөч дээр хэзээ ч үгүй).
bool AppShouldClose();

// EndDrawing-ийн дараа: хөтөч дээр дараагийн requestAnimationFrame хүртэл хүлээнэ; бусад платформд юу ч хийхгүй.
void FrameYield();

// Хадгалалтын файлыг хөтчийн localStorage-тэй тааруулна (бусад платформд юу ч хийхгүй).
void WebRestoreFile(const char* path); // localStorage -> файл (уншихаас өмнө)
void WebPersistFile(const char* path); // файл -> localStorage (бичсэний дараа)
