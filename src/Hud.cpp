#include "Hud.h"

#include "Minimap.h"
#include "UiFont.h"
#include "raylib.h"

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
        DrawText(text, x + 1, y + 1, fontSize, Fade(BLACK, 0.75f));
        DrawText(text, x, y, fontSize, color);
    }

    void LabelRight(const char* text, int right, int y, int fontSize, Color color)
    {
        Label(text, right - MeasureText(text, fontSize), y, fontSize, color);
    }

    void LabelCentered(const char* text, int y, int fontSize, Color color)
    {
        Label(text, (GetScreenWidth() - MeasureText(text, fontSize)) / 2, y, fontSize, color);
    }

    void DrawCrosshair()
    {
        const int cx = GetScreenWidth() / 2;
        const int cy = GetScreenHeight() / 2;
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

void DrawHud(const GameConfig& config, const Player& player, const HudInfo& info, bool showDebug)
{
    const int width = GetScreenWidth();
    const int height = GetScreenHeight();

    // Цохиулсан үед дэлгэцийн ирмэг улайна.
    if (player.TimeSinceHit() < 0.3f)
    {
        const float alpha = 0.35f * (1.0f - player.TimeSinceHit() / 0.3f);
        DrawRectangleLinesEx({ 0.0f, 0.0f, (float)width, (float)height }, 24.0f, Fade(RED, alpha));
    }

    DrawCrosshair();

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

    // --- Баруун дээд: зоос, устгасан тоо, баг ---
    LabelRight(TextFormat("Coins: %d", info.coins), width - 20, 14, 24, kAccent);
    LabelRight(TextFormat("Kills: %d", info.missionKills), width - 20, 44, 20, kText);
    if (info.squadTotal > 0)
    {
        LabelRight(TextFormat("Squad: %d / %d", info.squadAlive, info.squadTotal), width - 20, 68, 20, kGood);
    }

    // --- Зүүн доод: амь ---
    const Rectangle bar = { 20.0f, (float)height - 44.0f, 260.0f, 24.0f };
    const float ratio = player.Health() / player.MaxHealth();
    DrawRectangleRec(bar, Fade(BLACK, 0.55f));
    DrawRectangleRec({ bar.x, bar.y, bar.width * ratio, bar.height },
                     ratio > 0.35f ? Color{ 70, 140, 70, 255 } : Color{ 190, 50, 40, 255 });
    DrawRectangleLinesEx(bar, 2.0f, Fade(BLACK, 0.8f));
    Label(TextFormat("HP %d / %d", (int)player.Health(), (int)player.MaxHealth()), (int)bar.x + 8, (int)bar.y + 4, 18,
          WHITE);

    // --- Баруун доод: сум (жижиг газрын зургийн дээр) ---
    const int ammoY = height - kMinimapSize - kMinimapMargin - 42;
    if (info.reloading)
    {
        LabelRight("RELOADING", width - 20, ammoY, 32, kAccent);
    }
    else if (info.usesAmmo)
    {
        const char* ammo = info.showMagazine ? TextFormat("%d / %d", info.ammo, info.magazine)
                                             : TextFormat("x %d", info.ammo);
        LabelRight(ammo, width - 20, ammoY, 32, info.ammo == 0 ? kDanger : kText);
    }

    // --- Доод голд: зэвсгийн 1..5 нүд, сонгогдсон нь тодорно ---
    const char* slots[] = { "1 KNIFE", "2 PISTOL", "3 RIFLE", "4 SHOTGUN", "5 GRENADE" };
    const int slotWidth = 112, slotHeight = 26, gap = 6;
    const int total = 5 * slotWidth + 4 * gap;
    int slotX = (width - total) / 2;
    const int slotY = height - 40;
    for (int i = 0; i < 5; ++i)
    {
        const bool selected = i == info.weaponSlot;
        DrawRectangle(slotX, slotY, slotWidth, slotHeight, Fade(BLACK, selected ? 0.75f : 0.4f));
        if (selected)
        {
            DrawRectangleLinesEx({ (float)slotX, (float)slotY, (float)slotWidth, (float)slotHeight }, 2.0f, kAccent);
        }
        const int textX = slotX + (slotWidth - MeasureText(slots[i], 16)) / 2;
        DrawText(slots[i], textX, slotY + 5, 16, selected ? kAccent : kTextDim);
        slotX += slotWidth + gap;
    }

    // --- Удирдлага (F1-ээр нуух) ---
    if (info.showHelp)
    {
        int y = 102;
        const char* help[] = {
            "WASD move   Shift sprint   Space jump   LMB attack   RMB aim   R reload",
            "1 knife   2 pistol   3 rifle   4 shotgun   5 grenade",
            "Wheel zoom   [ ] sensitivity   - = speed   I invert Y   F3 debug",
            "Q mission journal   E use objective   M full map",
            "F get in / out, open door (drive: W S A D, Space brake)",
            "TAB retreat   F1 hide help   ESC quit",
        };
        for (const char* text : help)
        {
            Label(text, 20, y, 16, kTextDim);
            y += 20;
        }
        Label(TextFormat("Sensitivity: %.4f   Speed: %.1f m/s   Invert Y: %s", config.mouseSensitivity,
                         config.walkSpeed, config.invertY ? "on" : "off"),
              20, y + 4, 16, kTextDim);
    }

    if (showDebug)
    {
        const Vector3 p = player.Position();
        Label(TextFormat("Pos: %.1f  %.1f  %.1f   Speed: %.1f   Grounded: %s", p.x, p.y, p.z, player.HorizontalSpeed(),
                         player.IsGrounded() ? "yes" : "no"),
              20, height - 72, 16, kAccent);
    }

    // Унааны мэдээлэл: зэвсгийн нүднүүдийн дээр
    if (info.rideText)
    {
        const int textWidth = MeasureText(info.rideText, 20);
        DrawRectangle((width - textWidth) / 2 - 10, height - 76, textWidth + 20, 28, Fade(BLACK, 0.5f));
        LabelCentered(info.rideText, height - 72, 20, kAccent);
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
