#include "ObjectiveIcons.h"

#include "UiStyle.h"
#include "raymath.h"

#include <cmath>
#include <cstdio>

namespace
{
    // raylib-ийн DrawTriangle цагийн зүүний эсрэг дарааллыг шаарддаг: аль ч дарааллаар өгсөн зөв зурна
    void Tri(Vector2 a, Vector2 b, Vector2 c, Color color)
    {
        const float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        if (cross < 0.0f) DrawTriangle(a, b, c, color);
        else DrawTriangle(a, c, b, color);
    }

    // Нэгж координат (-1..1, y доош) -> дэлгэц
    struct Frame
    {
        Vector2 c;
        float u;
        Vector2 P(float x, float y) const { return { c.x + x * u, c.y + y * u }; }
    };
}

MarkerKind KindOf(ObjectiveType type)
{
    switch (type)
    {
    case ObjectiveType::ReachLocation: return MarkerKind::Reach;
    case ObjectiveType::InteractObject: return MarkerKind::Interact;
    case ObjectiveType::RescueSurvivors: return MarkerKind::Rescue;
    case ObjectiveType::CollectItems: return MarkerKind::Collect;
    case ObjectiveType::DeliverItems: return MarkerKind::Deliver;
    case ObjectiveType::ClearZone: return MarkerKind::Clear;
    case ObjectiveType::DefendLocation: return MarkerKind::Defend;
    case ObjectiveType::DefeatBoss: return MarkerKind::Boss;
    }
    return MarkerKind::Reach;
}

Color MarkerColor(MarkerKind kind)
{
    switch (kind)
    {
    case MarkerKind::Reach: return { 255, 196, 64, 255 };    // алтан шар
    case MarkerKind::Interact: return { 80, 200, 255, 255 }; // цэнхэр
    case MarkerKind::Rescue: return { 110, 220, 120, 255 };  // ногоон
    case MarkerKind::Collect: return { 255, 150, 60, 255 };  // улбар шар
    case MarkerKind::Deliver: return { 200, 150, 255, 255 }; // ягаан нил
    case MarkerKind::Clear: return { 245, 85, 70, 255 };     // улаан
    case MarkerKind::Defend: return { 90, 150, 255, 255 };   // хөх
    case MarkerKind::Boss: return { 225, 70, 210, 255 };     // нил ягаан
    case MarkerKind::Event: return { 255, 236, 140, 255 };   // цайвар шар
    case MarkerKind::Ping: return { 70, 235, 225, 255 };     // ногоовтор цэнхэр
    }
    return WHITE;
}

const char* MarkerName(MarkerKind kind)
{
    switch (kind)
    {
    case MarkerKind::Reach: return "Очих";
    case MarkerKind::Interact: return "Асаах";
    case MarkerKind::Rescue: return "Аврах";
    case MarkerKind::Collect: return "Цуглуулах";
    case MarkerKind::Deliver: return "Хүргэх";
    case MarkerKind::Clear: return "Цэвэрлэх";
    case MarkerKind::Defend: return "Хамгаалах";
    case MarkerKind::Boss: return "Аварга";
    case MarkerKind::Event: return "Үйл явдал";
    case MarkerKind::Ping: return "Тэмдэг";
    }
    return "";
}

void DrawMarkerGlyph(MarkerKind kind, Vector2 center, float radius, Color color)
{
    const Frame f = { center, radius * 0.62f };
    const float line = std::max(1.5f, f.u * 0.24f);
    const Color dark = { 20, 24, 30, color.a };
    switch (kind)
    {
    case MarkerKind::Reach:
        // Туг: шон, гурвалжин даавуу
        DrawLineEx(f.P(-0.55f, 1.0f), f.P(-0.55f, -1.0f), line, color);
        Tri(f.P(-0.55f, -1.0f), f.P(0.95f, -0.5f), f.P(-0.55f, 0.05f), color);
        break;
    case MarkerKind::Interact:
        // Аянга (цахилгаан, радио)
        Tri(f.P(0.35f, -1.05f), f.P(-0.6f, 0.15f), f.P(0.12f, 0.15f), color);
        Tri(f.P(-0.12f, -0.15f), f.P(0.6f, -0.15f), f.P(-0.35f, 1.05f), color);
        break;
    case MarkerKind::Rescue:
        // Хүн: толгой, мөр, бие
        DrawCircleV(f.P(0.0f, -0.6f), f.u * 0.36f, color);
        DrawRectangleRounded({ f.P(-0.62f, -0.12f).x, f.P(0.0f, -0.12f).y, f.u * 1.24f, f.u * 1.12f }, 0.45f, 6, color);
        break;
    case MarkerKind::Collect:
        // Хайрцаг: таг, дунд нь зурвас
        DrawRectangleRounded({ f.P(-0.85f, -0.7f).x, f.P(0.0f, -0.7f).y, f.u * 1.7f, f.u * 1.55f }, 0.18f, 4, color);
        DrawRectangleV(f.P(-0.85f, -0.25f), { f.u * 1.7f, f.u * 0.16f }, dark);
        DrawRectangleV(f.P(-0.12f, -0.7f), { f.u * 0.24f, f.u * 0.62f }, dark);
        break;
    case MarkerKind::Deliver:
        // Доош чиглэсэн сум хайрцаг руу
        DrawRectangleRounded({ f.P(-0.8f, 0.15f).x, f.P(0.0f, 0.15f).y, f.u * 1.6f, f.u * 0.85f }, 0.2f, 4, color);
        DrawRectangleV(f.P(-0.13f, -1.05f), { f.u * 0.26f, f.u * 0.7f }, color);
        Tri(f.P(-0.5f, -0.4f), f.P(0.5f, -0.4f), f.P(0.0f, 0.05f), color);
        break;
    case MarkerKind::Clear:
        // Онилгоо: цагираг, дөрвөн зураас, төвд цэг
        DrawRing(center, f.u * 0.62f, f.u * 0.62f + line, 0.0f, 360.0f, 28, color);
        DrawLineEx(f.P(0.0f, -1.1f), f.P(0.0f, -0.4f), line, color);
        DrawLineEx(f.P(0.0f, 0.4f), f.P(0.0f, 1.1f), line, color);
        DrawLineEx(f.P(-1.1f, 0.0f), f.P(-0.4f, 0.0f), line, color);
        DrawLineEx(f.P(0.4f, 0.0f), f.P(1.1f, 0.0f), line, color);
        DrawCircleV(center, f.u * 0.16f, color);
        break;
    case MarkerKind::Defend:
        // Бамбай
        DrawRectangleV(f.P(-0.78f, -0.9f), { f.u * 1.56f, f.u * 0.95f }, color);
        Tri(f.P(-0.78f, 0.04f), f.P(0.78f, 0.04f), f.P(0.0f, 1.05f), color);
        DrawRectangleV(f.P(-0.1f, -0.7f), { f.u * 0.2f, f.u * 1.2f }, dark);
        break;
    case MarkerKind::Boss:
        // Гавлын яс: толгой, эрүү, нүд
        DrawCircleV(f.P(0.0f, -0.18f), f.u * 0.82f, color);
        DrawRectangleRounded({ f.P(-0.46f, 0.3f).x, f.P(0.0f, 0.3f).y, f.u * 0.92f, f.u * 0.7f }, 0.3f, 4, color);
        DrawCircleV(f.P(-0.34f, -0.15f), f.u * 0.22f, dark);
        DrawCircleV(f.P(0.34f, -0.15f), f.u * 0.22f, dark);
        DrawRectangleV(f.P(-0.06f, 0.6f), { f.u * 0.12f, f.u * 0.4f }, dark);
        break;
    case MarkerKind::Event:
        // Анхаар (!)
        DrawRectangleRounded({ f.P(-0.15f, -1.0f).x, f.P(0.0f, -1.0f).y, f.u * 0.3f, f.u * 1.3f }, 0.5f, 4, color);
        DrawCircleV(f.P(0.0f, 0.75f), f.u * 0.18f, color);
        break;
    case MarkerKind::Ping:
        // Зүү: дугуй толгой, доош шовх
        DrawCircleV(f.P(0.0f, -0.35f), f.u * 0.62f, color);
        Tri(f.P(-0.5f, 0.0f), f.P(0.5f, 0.0f), f.P(0.0f, 1.05f), color);
        DrawCircleV(f.P(0.0f, -0.35f), f.u * 0.24f, dark);
        break;
    }
}

void DrawMarkerBadge(MarkerKind kind, Vector2 center, float radius, float alpha, int number)
{
    const Color ring = MarkerColor(kind);
    DrawCircleV({ center.x + 1.0f, center.y + 2.0f }, radius + 2.0f, Fade(BLACK, 0.35f * alpha)); // сүүдэр
    DrawCircleV(center, radius, Fade({ 18, 22, 30, 255 }, 0.92f * alpha));
    DrawRing(center, radius - std::max(2.0f, radius * 0.16f), radius, 0.0f, 360.0f, 36, Fade(ring, alpha));
    DrawMarkerGlyph(kind, center, radius * 0.95f, Fade(WHITE, alpha));
    if (number > 0)
    {
        // Даалгаврын дугаар: баруун дээд буланд жижиг дугуй
        char text[8];
        std::snprintf(text, sizeof(text), "%d", number);
        const float r = std::max(7.0f, radius * 0.42f);
        const Vector2 at = { center.x + radius * 0.74f, center.y - radius * 0.74f };
        DrawCircleV(at, r, Fade(ring, alpha));
        const int size = (int)(r * 1.45f);
        const int w = Ui::MeasureBold(text, size);
        Ui::TextBold(text, (int)(at.x - (float)w * 0.5f), (int)(at.y - (float)size * 0.55f), size, Fade({ 18, 22, 30, 255 }, alpha));
    }
}
