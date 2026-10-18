#pragma once

// Хөтөч (WebAssembly) дээр ажиллахад хэрэгтэй жижиг ялгаанууд.
// Хөтөч дотор үндсэн урсгал frame бүрийн дараа удирдлагыг хөтчид буцааж байж зураг дэлгэцэнд гарна
// (-sASYNCIFY). raylib-ийн WindowShouldClose нь хөтөч дээр үргэлж 16 мс унтдаг тул оронд нь эдгээрийг хэрэглэнэ.

// Цонхыг хаах хүсэлт ирсэн эсэх (хөтөч дээр хэзээ ч үгүй).
bool AppShouldClose();

// EndDrawing-ийн дараа: хөтөч дээр дараагийн requestAnimationFrame хүртэл хүлээнэ; бусад платформд юу ч хийхгүй.
void FrameYield();

// Мэдрэгчтэй дэлгэц (хөтөч): үндсэн оролт нь хуруу эсэх (утас, таблет). Бусад платформд false.
bool WebIsTouchDevice();

// Хөтөч: одоо дэлгэцэд хүрч буй хуруунууд (дэлгэцийн пикселээр, GetScreenWidth/Height-тэй ижил).
// raylib-ийн вэб давхарга өргөсөн хурууг жагсаалтад үлдээдэг тул хөтчийн event.touches-ийг шууд уншина.
// capacity = 0 бол зөвхөн тоог буцаана. Бусад платформд 0.
int WebReadTouches(float* xy, int* ids, int capacity);

// Хадгалалтын файлыг хөтчийн localStorage-тэй тааруулна (бусад платформд юу ч хийхгүй).
void WebRestoreFile(const char* path); // localStorage -> файл (уншихаас өмнө)
void WebPersistFile(const char* path); // файл -> localStorage (бичсэний дараа)
