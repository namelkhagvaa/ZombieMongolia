#pragma once

// Тоглоомын бүх C++ файлд эхэнд нь автоматаар орно (CMakeLists.txt: -include). raylib-ийн TextFormat-ийг загварыг нь
// идэвхтэй хэл рүү орчуулдаг хувилбараар солино: TextFormat("Тулаанд %d Gold хэрэгтэй", n) англи горимд
// "You need %d Gold to battle" загвараар форматлагдана (src/LangEn.cpp-д %d, %s ижил дараалалтай байх ёстой).
// raylib.h-ийг энд оруулахгүй (windows.h-тэй файлууд): макро raylib-ийн TextFormat-ийн зарлалтыг ч LangTextFormat
// болгоно (ижил гарын үсэг).
#include "Lang.h"

#include <cstddef>

extern "C" const char* LangTextFormat(const char* format, ...); // (raylib.h-ийн зарлалт extern "C" дотор)
#define TextFormat(...) LangTextFormat(__VA_ARGS__)
// Дэлгэцийн текстийн snprintf: TextFormat шиг загвар, %s аргументыг орчуулна
extern "C" int LangSnprintf(char* out, size_t size, const char* format, ...);
