#pragma once

#include <string>

// Тоглоомын хэл. Монголд (төхөөрөмж, App Store-ийн бүс, цагийн бүс) үргэлж монгол; бусад улсад анхдагчаар англи,
// тохиргооноос (түр зогсоох цэс) монгол болгож болно. Текстүүд кодонд монголоор; Tr() англи хувилбарыг
// хүснэгтээс (src/LangEn.cpp) буцаана, орчуулгагүй бол монголоороо.
enum class Language { Mongolian, English };

// Эхлэхэд нэг удаа (хадгалалт уншсаны дараа). saved: -1 сонгоогүй, 0 монгол, 1 англи
void LangInit(int saved);
Language CurrentLanguage();
bool English();
// Монголоос гадна: хэл солих сонголт харагдана
bool LanguageSelectable();
void SetLanguage(Language language);
// Хадгалах утга (Progress::language): -1 = сонгоогүй (бүсээр)
int LanguageSaveValue();

// Англи орчуулга (идэвхтэй бол), эс бөгөөс өгсөн текст өөрөө. Буцаасан заагч програм ажиллаж байх хугацаанд хүчинтэй.
const char* Tr(const char* mongolian);
inline const char* Tr(const std::string& mongolian) { return Tr(mongolian.c_str()); }
// Хэлээс хамаарсан сонголт (орчуулгын хүснэгтэд ороогүй жижиг зүйлс)
inline const char* Tr2(const char* mongolian, const char* english) { return English() ? english : mongolian; }
