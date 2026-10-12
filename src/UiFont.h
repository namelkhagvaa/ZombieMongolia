#pragma once

#include "raylib.h"

// Дэлгэцийн бичвэрийн фонт. raylib-ийн үндсэн фонт кирилл үсэггүй тул Windows-ийн системийн
// фонтыг (Segoe UI, эсвэл Arial / Tahoma) кирилл үсгүүдтэй нь ачаална. Гаднын файл нэмэхгүй.
//
// Бичвэрүүд UTF-8 кодчилолтой. Фонт олдохгүй бол үндсэн фонт руу шилжих ба кирилл үсэг "?" болно;
// энэ тохиолдолд UiHasCyrillic() false буцаана.

void LoadUiFont();   // цонх нээгдсэний дараа
void UnloadUiFont(); // цонх хаагдахаас өмнө
bool UiHasCyrillic();

void UiText(const char* text, int x, int y, int size, Color color);
// Бараан сүүдэртэй: ямар ч дэвсгэр дээр уншигдана.
void UiTextShadow(const char* text, int x, int y, int size, Color color);
int UiMeasure(const char* text, int size);
// Тод (semibold) фонт: гарчиг, тоо. Олдохгүй бол энгийн фонтоор зурна.
void UiTextBold(const char* text, int x, int y, int size, Color color);
int UiMeasureBold(const char* text, int size);
