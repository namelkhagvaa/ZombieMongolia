#include "Hud.h"

#include "Input.h"

#include "Minimap.h"
#include "UiFont.h"
#include "raylib.h"

#include <cmath>
#include <cstdio>

namespace
{
    const Color kText = { 240, 236, 224, 255 };
    const Color kTextDim = { 214, 208, 192, 255 };
    const Color kAccent = { 240, 196, 80, 255 };
    const Color kDanger = { 236, 96, 80, 255 };
    const Color kGood = { 150, 210, 120, 255 };

    // Ямар ч дэвсгэр дээр уншигдахын тулд бүх бичвэрийг бараан сүүдэртэй зурна.
    void Label(const char* text, int x, int y, int fontSize, Color color)
    {
        UiText(text, x + 1, y + 1, fontSize, Fade(BLACK, 0.75f));
        UiText(text, x, y, fontSize, color);
    }

    void LabelRight(const char* text, int right, int y, int fontSize, Color color)
    {
        Label(text, right - UiMeasure(text, fontSize), y, fontSize, color);
    }

    void LabelCentered(const char* text, int y, int fontSize, Color color)
    {
        Label(text, (ViewWidth() - UiMeasure(text, fontSize)) / 2, y, fontSize, color);
    }

    void DrawCrosshair()
    {
        const int cx = ViewWidth() / 2;
        const int cy = ViewHeight() / 2;
        const Color shade = Fade(BLACK, 0.6f);
        DrawRectangle(cx - 11, cy - 2, 9, 4, shade);
        DrawRectangle(cx + 3, cy - 2, 9, 4, shade);
        DrawRectangle(cx - 2, cy - 11, 4, 9, shade);
        DrawRectangle(cx - 2, cy + 3, 4, 9, shade);
        DrawRectangle(cx - 10, cy - 1, 7, 2, WHITE);
        DrawRectangle(cx + 4, cy - 1, 7, 2, WHITE);
        DrawRectangle(cx - 1, cy - 10, 2, 7, WHITE);
        DrawRectangle(cx - 1, cy + 4, 2, 7, WHITE);
    }
}

namespace
{
    // Мэргэн буудагчийн дуран хараа: дугуй цонхны гадуур хар, дотор нь нарийн загалмай ба зайн тэмдэг.
    void DrawScope()
    {
        const int width = ViewWidth();
        const int height = ViewHeight();
        const Vector2 center = { (float)width * 0.5f, (float)height * 0.5f };
        const float radius = (float)height * 0.46f;
        const float outer = std::sqrt((float)(width * width + height * height));

        DrawRing(center, radius, outer, 0.0f, 360.0f, 96, BLACK);
        DrawRing(center, radius - 5.0f, radius, 0.0f, 360.0f, 96, Fade(BLACK, 0.55f));

        const Color line = { 12, 12, 12, 235 };
        const float gap = 9.0f;
        // Захаас төв рүү бүдүүн, төвийн ойролцоо нарийн шугам
        DrawRectangleRec({ center.x - radius, center.y - 2.0f, radius * 0.62f, 4.0f }, line);
        DrawRectangleRec({ center.x + radius * 0.38f, center.y - 2.0f, radius * 0.62f, 4.0f }, line);
        DrawRectangleRec({ center.x - 2.0f, center.y + radius * 0.38f, 4.0f, radius * 0.62f }, line);
        DrawRectangleRec({ center.x - radius * 0.38f, center.y - 0.5f, radius * 0.38f - gap, 1.0f }, line);
        DrawRectangleRec({ center.x + gap, center.y - 0.5f, radius * 0.38f - gap, 1.0f }, line);
        DrawRectangleRec({ center.x - 0.5f, center.y - radius, 1.0f, radius - gap }, line);
        DrawRectangleRec({ center.x - 0.5f, center.y + gap, 1.0f, radius * 0.38f - gap }, line);
        for (int i = 1; i <= 4; ++i)
        {
            const float offset = (float)i * radius * 0.075f;
            DrawRectangleRec({ center.x - 4.0f, center.y + gap + offset, 8.0f, 1.0f }, line); // зайн тэмдэг
            DrawRectangleRec({ center.x - gap - offset - 0.5f, center.y - 4.0f, 1.0f, 8.0f }, line);
            DrawRectangleRec({ center.x + gap + offset - 0.5f, center.y - 4.0f, 1.0f, 8.0f }, line);
        }
        DrawCircleV(center, 1.6f, { 200, 30, 30, 255 });
    }
}

void DrawHud(const GameConfig& config, const Player& player, const HudInfo& info, bool showDebug)
{
    const int width = ViewWidth();
    const int height = ViewHeight();

    // Цохиулсан үед дэлгэцийн ирмэг улайна.
    if (player.TimeSinceHit() < 0.3f)
    {
        const float alpha = 0.35f * (1.0f - player.TimeSinceHit() / 0.3f);
        DrawRectangleLinesEx({ 0.0f, 0.0f, (float)width, (float)height }, 24.0f, Fade(RED, alpha));
    }

    if (info.scoped) DrawScope();
    else DrawCrosshair();

    // --- Зүүн дээд: FPS, аймаг, давалгаа ---
    Label(TextFormat("%d FPS", GetFPS()), 20, 14, 20, GetFPS() >= 50 ? kGood : kDanger);
    Label(info.provinceName, 20, 40, 24, kText);

    // Одоо хийх даалгавар (Q дарж бүтэн жагсаалтыг харна)
    if (info.objective)
    {
        UiTextShadow(info.objective, 20, 66, 22, kAccent);
        if (info.objectiveDetail)
        {
            UiTextShadow(info.objectiveDetail, 22 + UiMeasure(info.objective, 22) + 14, 68, 20, kText);
        }
    }

    // Үеийн зомбины нөөц: хэдийг устгасан / нийт
    if (info.hordeText)
    {
        UiTextShadow(info.hordeText, 20, info.touch ? 150 : 94, 22, kDanger);
    }

    // --- Баруун дээд: зоос, устгасан тоо, баг ---
    if (info.touch)
    {
        // Мэдрэгчтэй дэлгэц: баруун дээд буланд газрын зураг, баг байрладаг тул зоосыг дээд голд харуулна.
        LabelCentered(TextFormat("Зоос: %d", info.coins), 14, 22, kAccent);
    }
    else
    {
        LabelRight(TextFormat("Зоос: %d", info.coins), width - 20, 14, 24, kAccent);
        LabelRight(TextFormat("Устгасан: %d", info.missionKills), width - 20, 44, 20, kText);
    }

    // Баг: F1, F2, F3 товчоор шилжих гурван гишүүн; удирдаж буй нь тодорно.
    int teamY = 72;
    for (int i = 0; i < 3 && !info.touch; ++i)
    {
        const HudInfo::TeamEntry& entry = info.team[i];
        char line[96];
        if (entry.alive) std::snprintf(line, sizeof(line), "F%d  %s   %d", i + 1, entry.name, entry.health);
        else std::snprintf(line, sizeof(line), "F%d  %s   —", i + 1, entry.name);

        const int textWidth = UiMeasure(line, 20);
        const int x = width - 20 - textWidth;
        if (entry.active)
        {
            DrawRectangle(x - 8, teamY - 2, textWidth + 16, 26, Fade(BLACK, 0.6f));
            DrawRectangleLinesEx({ (float)x - 8.0f, (float)teamY - 2.0f, (float)textWidth + 16.0f, 26.0f }, 2.0f, kAccent);
        }
        const Color color = !entry.alive ? kDanger : entry.active ? kAccent : entry.health < 35 ? kDanger : kText;
        UiTextShadow(line, x, teamY, 20, color);
        teamY += 28;
    }
    if (info.squadTotal > 0 && !info.touch)
    {
        LabelRight(TextFormat("Цэрэг: %d / %d", info.squadAlive, info.squadTotal), width - 20, teamY + 2, 20, kGood);
    }

    // --- Зүүн доод: амь ---
    const Rectangle bar = { 20.0f, (float)height - 44.0f, 260.0f, 24.0f };
    const float ratio = player.Health() / player.MaxHealth();
    DrawRectangleRec(bar, Fade(BLACK, 0.55f));
    DrawRectangleRec({ bar.x, bar.y, bar.width * ratio, bar.height },
                     ratio > 0.35f ? Color{ 70, 140, 70, 255 } : Color{ 190, 50, 40, 255 });
    DrawRectangleLinesEx(bar, 2.0f, Fade(BLACK, 0.8f));
    Label(TextFormat("Амь %d / %d", (int)player.Health(), (int)player.MaxHealth()), (int)bar.x + 8, (int)bar.y + 4, 18,
          WHITE);
    if (info.stance)
    {
        Label(info.stance, (int)(bar.x + bar.width) + 14, (int)bar.y + 3, 20, kAccent);
    }

    // --- Баруун доод: сум (жижиг газрын зургийн дээр) ---
    // (Мэдрэгчтэй дэлгэцэд: доод голд, зэвсгийн товчлууруудын дээр.)
    const int ammoY = info.touch ? height - 108 : height - kMinimapSize - kMinimapMargin - 42;
    const char* ammoText = nullptr;
    Color ammoColor = kText;
    if (info.reloading)
    {
        ammoText = "ЦЭНЭГЛЭЖ БАЙНА";
        ammoColor = kAccent;
    }
    else if (info.usesAmmo)
    {
        ammoText = info.showMagazine ? TextFormat("%d / %d", info.ammo, info.magazine) : TextFormat("x %d", info.ammo);
        ammoColor = info.ammo == 0 ? kDanger : kText;
    }
    if (ammoText && info.touch) LabelCentered(ammoText, ammoY, 30, ammoColor);
    else if (ammoText) LabelRight(ammoText, width - 20, ammoY, 32, ammoColor);

    // --- Доод голд: зэвсгийн 1..5 нүд, сонгогдсон нь тодорно ---
    const char* const* slots = info.slotNames;
    const int slotWidth = 112, slotHeight = 26, gap = 6;
    const int total = info.slotCount * slotWidth + (info.slotCount - 1) * gap;
    int slotX = (width - total) / 2;
    const int slotY = height - 40;
    for (int i = 0; i < info.slotCount && !info.touch; ++i)
    {
        const bool selected = i == info.weaponSlot;
        DrawRectangle(slotX, slotY, slotWidth, slotHeight, Fade(BLACK, selected ? 0.75f : 0.4f));
        if (selected)
        {
            DrawRectangleLinesEx({ (float)slotX, (float)slotY, (float)slotWidth, (float)slotHeight }, 2.0f, kAccent);
        }
        const int textX = slotX + (slotWidth - UiMeasure(slots[i], 16)) / 2;
        UiText(slots[i], textX, slotY + 5, 16, selected ? kAccent : kTextDim);
        slotX += slotWidth + gap;
    }

    // --- Удирдлага (F1-ээр нуух) ---
    if (info.showHelp && !info.touch)
    {
        int y = 102;
        const char* help[] = {
            "WASD — явах   Shift — гүйх   Space — үсрэх / босох   C — суух   Z — хэвтэх",
            "Хулганы зүүн товч — довтлох   баруун товч — онилох (мэргэн буудагч: дуран хараа)   R — цэнэглэх",
            "F1 F2 F3 — багийн гишүүн солих (F3 — мэргэн буудагч)   1–5 — зэвсэг сонгох",
            "Хулганы дугуй — камерын зай   [ ] — хулганы мэдрэмж   - = — алхах хурд   I — босоо тэнхлэг урвуулах   F4 — шалгах горим",
            "Q — даалгаврын журнал   E — үйлдэл хийх   M — газрын зураг",
            "F — унаанд суух / буух, хаалга нээх / хаах   (жолоодох: W S A D, Space — тоормос)",
            "TAB — ухрах   H — тусламжийг нуух   ESC — гарах",
        };
        for (const char* text : help)
        {
            Label(text, 20, y, 16, kTextDim);
            y += 20;
        }
        Label(TextFormat("Хулганы мэдрэмж: %.4f   Алхах хурд: %.1f м/с   Босоо тэнхлэг урвуу: %s", config.mouseSensitivity,
                         config.walkSpeed, config.invertY ? "тийм" : "үгүй"),
              20, y + 4, 16, kTextDim);
    }

    if (showDebug)
    {
        const Vector3 p = player.Position();
        Label(TextFormat("Байрлал: %.1f  %.1f  %.1f   Хурд: %.1f   Газарт: %s", p.x, p.y, p.z, player.HorizontalSpeed(),
                         player.IsGrounded() ? "тийм" : "үгүй"),
              20, height - 72, 16, kAccent);
    }

    // Унааны мэдээлэл: зэвсгийн нүднүүдийн дээр
    if (info.rideText)
    {
        const int textWidth = UiMeasure(info.rideText, 20);
        const int rideY = info.touch ? height - 146 : height - 76;
        DrawRectangle((width - textWidth) / 2 - 10, rideY, textWidth + 20, 28, Fade(BLACK, 0.5f));
        LabelCentered(info.rideText, rideY + 4, 20, kAccent);
    }

    // Харилцах заавар: онилгооны доор ("E — Генератор асаах")
    if (info.prompt)
    {
        const int textWidth = UiMeasure(info.prompt, 26);
        const int x = (width - textWidth) / 2, y = height / 2 + 70;
        DrawRectangle(x - 14, y - 6, textWidth + 28, 40, Fade(BLACK, 0.6f));
        DrawRectangleLinesEx({ (float)x - 14.0f, (float)y - 6.0f, (float)textWidth + 28.0f, 40.0f }, 2.0f, kAccent);
        UiText(info.prompt, x, y, 26, WHITE);
    }

    // Богино мэдэгдэл: дэлгэцийн дээд голд ("ДААЛГАВАР БИЕЛЛЭЭ: ...")
    if (info.notice)
    {
        const int textWidth = UiMeasure(info.notice, 28);
        const int x = (width - textWidth) / 2, y = 150;
        DrawRectangle(x - 18, y - 8, textWidth + 36, 46, Fade({ 14, 50, 24, 255 }, 0.85f));
        UiText(info.notice, x, y, 28, kGood);
    }

    if (info.banner)
    {
        LabelCentered(info.banner, height / 2 - 110, 48, RAYWHITE);
    }
    if (info.subBanner)
    {
        LabelCentered(info.subBanner, height / 2 - 54, 22, RAYWHITE);
    }
}
