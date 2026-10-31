#pragma once

#include "CampaignData.h"
#include "raylib.h"

// Даалгаврын төрлийн тэмдэг: газрын зураг, жижиг зураг, HUD, дэлгэц дээрх чиглүүлэгч, журнал бүгд ижил дүрсээр.
// Файлгүй (кодоор зурна): төрөл бүр өөр өнгө, өөр дүрс.
enum class MarkerKind
{
    Reach,    // туг: газарт хүрэх
    Interact, // аянга: радио, генератор асаах
    Rescue,   // хүн: аврах
    Collect,  // хайрцаг: цуглуулах
    Deliver,  // доош сумтай хайрцаг: хүргэх
    Clear,    // онилгоо: бүсийг цэвэрлэх (бүх даалгавар биелсний дараах үлдсэн зомби ч)
    Defend,   // бамбай: хамгаалах
    Boss,     // гавлын яс: аварга
    Event     // анхаар тэмдэг: гэнэтийн үйл явдал (ачаа, бөөгнөрөл, аврах хүн)
};

MarkerKind KindOf(ObjectiveType type);
Color MarkerColor(MarkerKind kind);
const char* MarkerName(MarkerKind kind); // "Аврах", "Цуглуулах" ...

// Газрын зураг, дэлгэцэд харуулах даалгаврын тэмдэг
struct MapMarker
{
    Vector3 position;
    MarkerKind kind;
    int objective = -1;       // даалгаврын индекс (-1 = гэнэтийн үйл явдал)
    const char* title = "";   // даалгаврын нэр (үйл явдалд богино нэр)
    bool focus = false;       // одоо хийх ёстой (эхний идэвхтэй) даалгавар
};

// Дугуй тэмдэг: бараан дэвсгэр, төрлийн өнгөт хүрээ, дотор нь цагаан дүрс. number > 0 бол баруун дээд буланд дугаар.
void DrawMarkerBadge(MarkerKind kind, Vector2 center, float radius, float alpha = 1.0f, int number = 0);
// Зөвхөн дүрс (дэвсгэргүй): HUD-ийн мөр дотор
void DrawMarkerGlyph(MarkerKind kind, Vector2 center, float radius, Color color);
