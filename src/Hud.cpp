#include "Hud.h"

#include "Input.h"

#include "Minimap.h"
#include "UiFont.h"
#include "UiStyle.h"
#include "raylib.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

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

namespace
{
    // "F — цонхоор орох" маягийн бичвэрийг товч ба үйлдэл болгон хуваана (товч нь 3-аас бага тэмдэгт).
    bool SplitKey(const char* text, char* key, size_t keySize, const char** rest)
    {
        const char* dash = std::strstr(text, " — ");
        if (!dash || dash - text == 0 || dash - text > 3) return false;
        const size_t length = std::min((size_t)(dash - text), keySize - 1);
        std::memcpy(key, text, length);
        key[length] = 0;
        *rest = dash + std::strlen(" — ");
        return true;
    }

    // Товчтой заавар: [F] үйлдэл. Төвөөр нь (centerX) байрлуулна; өндрийг буцаана.
    void KeyPrompt(const char* text, int centerX, int y, int size, Color accent)
    {
        char key[8];
        const char* rest = text;
        const bool hasKey = SplitKey(text, key, sizeof(key), &rest);
        const int keyWidth = hasKey ? Ui::MeasureBold(key, size - 2) + 14 : 0;
        const int gap = hasKey ? 12 : 0;
        const int textWidth = Ui::Measure(rest, size);
        const int width = 28 + keyWidth + gap + textWidth;
        const int height = size + 22;
        const int x = centerX - width / 2;
        Ui::Panel({ (float)x, (float)y, (float)width, (float)height }, Ui::kPanel, (float)height * 0.5f);
        int cursor = x + 14;
        if (hasKey)
        {
            cursor += Ui::KeyBadge(key, cursor, y + (height - size - 6) / 2, size - 2, accent) + gap;
        }
        Ui::Text(rest, cursor, y + (height - size) / 2 - 1, size, Ui::kText);
    }

    // Орчин үеийн загалмай: төвийн цэг ба зайтай дөрвөн нарийн шугам, бараан хүрээтэй.
    void DrawCrosshair()
    {
        const float cx = (float)ViewWidth() * 0.5f;
        const float cy = (float)ViewHeight() * 0.5f;
        const Color shade = Fade(BLACK, 0.55f);
        const float gap = 6.0f, length = 8.0f;
        auto line = [&](float x, float y, float w, float h) {
            DrawRectangleRec({ x - 1.0f, y - 1.0f, w + 2.0f, h + 2.0f }, shade);
            DrawRectangleRec({ x, y, w, h }, WHITE);
        };
        line(cx - gap - length, cy - 1.0f, length, 2.0f);
        line(cx + gap, cy - 1.0f, length, 2.0f);
        line(cx - 1.0f, cy - gap - length, 2.0f, length);
        line(cx - 1.0f, cy + gap, 2.0f, length);
        DrawCircleV({ cx, cy }, 2.6f, shade);
        DrawCircleV({ cx, cy }, 1.5f, WHITE);
    }

    // Цохиулсан үед: дэлгэцийн ирмэгээс дотогш бүдгэрэх улаан туяа.
    void DrawDamage(float strength)
    {
        if (strength <= 0.0f) return;
        const int w = ViewWidth(), h = ViewHeight();
        const int band = h / 5;
        const Color edge = Fade({ 200, 20, 20, 255 }, strength);
        const Color clear = Fade({ 200, 20, 20, 255 }, 0.0f);
        DrawRectangleGradientV(0, 0, w, band, edge, clear);
        DrawRectangleGradientV(0, h - band, w, band, clear, edge);
        DrawRectangleGradientH(0, 0, band, h, edge, clear);
        DrawRectangleGradientH(w - band, 0, band, h, clear, edge);
    }
}

void DrawHud(const GameConfig& config, const Player& player, const HudInfo& info, bool showDebug)
{
    const int width = ViewWidth();
    const int height = ViewHeight();

    if (player.TimeSinceHit() < 0.45f)
    {
        DrawDamage(0.55f * (1.0f - player.TimeSinceHit() / 0.45f));
    }
    const float healthRatio = player.Health() / player.MaxHealth();
    if (healthRatio < 0.3f)
    {
        // Амь бага: зүрхний цохилт шиг лугшиж, ирмэг улайна.
        const float pulse = 0.5f + 0.5f * std::sin((float)GetTime() * 6.0f);
        DrawDamage((0.3f - healthRatio) * (0.6f + 0.6f * pulse));
    }

    if (info.scoped) DrawScope();
    else DrawCrosshair();

    // --- Зүүн дээд: даалгаврын карт (аймаг, одоогийн зорилт, зай) ---
    {
        const int x = 16, y = 16;
        const int titleSize = 15, objSize = 20, detailSize = 16;
        const char* objective = info.objective ? info.objective : "";
        const int contentWidth = std::max({ Ui::MeasureBold(info.provinceName, titleSize) + 90,
                                            Ui::MeasureBold(objective, objSize),
                                            info.objectiveDetail ? Ui::Measure(info.objectiveDetail, detailSize) : 0 });
        const int cardHeight = info.objective ? (info.objectiveDetail ? 86 : 64) : 40;
        const Rectangle card = { (float)x, (float)y, (float)contentWidth + 36.0f, (float)cardHeight };
        Ui::PanelAccent(card, Ui::kAccent);
        Ui::TextBold(info.provinceName, x + 18, y + 10, titleSize, Ui::kTextDim);
        // FPS: картын баруун дээд буланд жижиг
        const int fps = GetFPS();
        const char* fpsText = TextFormat("%d FPS", fps);
        Ui::Text(fpsText, (int)(card.x + card.width) - 12 - Ui::Measure(fpsText, 13), y + 11, 13,
                 fps >= 50 ? Fade(Ui::kGood, 0.8f) : Ui::kDanger);
        if (info.objective)
        {
            Ui::TextBold(objective, x + 18, y + 32, objSize, Ui::kText);
            if (info.objectiveDetail)
            {
                Ui::Text(info.objectiveDetail, x + 18, y + 60, detailSize, Ui::kAccent);
            }
        }
    }

    // Үеийн зомбины нөөц: хоёр чип (даалгаврын картын доор; мэдрэгчтэй дэлгэцэд товчлууруудын доор)
    if (info.hordeSmall >= 0)
    {
        const int y = info.touch ? 164 : (info.objective ? (info.objectiveDetail ? 112 : 90) : 66);
        int x = 16;
        x += Ui::Chip(TextFormat("Зомби  %d", info.hordeSmall), x, y, Ui::kText, Ui::kDanger) + 8;
        x += Ui::Chip(TextFormat("Том зомби  %d", info.hordeBig), x, y, Ui::kText, { 170, 60, 200, 255 }) + 8;
        if (info.zombieSpeed > 1.01f)
        {
            Ui::Chip(TextFormat("Хурд ×%d", (int)(info.zombieSpeed + 0.5f)), x, y, Ui::kDanger, Ui::kDanger);
        }
    }

    // --- Баруун дээд: зоос, устгасан тоо, баг ---
    if (info.touch)
    {
        // Мэдрэгчтэй дэлгэц: баруун дээд буланд газрын зураг, баг байрладаг тул зоосыг дээд голд харуулна.
        const char* coins = TextFormat("%d", info.coins);
        const int chipWidth = Ui::MeasureBold(coins, 18) + 40;
        Ui::Chip(coins, (width - chipWidth) / 2, 16, Ui::kAccent, Ui::kAccent);
    }
    else
    {
        const int right = width - 16;
        const int coinWidth = Ui::Chip(TextFormat("%d", info.coins), right, 16, Ui::kAccent, Ui::kAccent, true);
        Ui::Coin({ (float)(right - coinWidth + 17), 32.0f }, 9.0f); // чипийн цэгийн оронд зоос
        Ui::Chip(TextFormat("Устгасан  %d", info.missionKills), right - coinWidth - 8, 16, Ui::kText, Ui::kDanger, true);

        // Баг: F1, F2, F3 картууд, амины зурвастай; удирдаж буй нь алтан хүрээтэй.
        int teamY = 58;
        const int cardWidth = 230, cardHeight = 44;
        for (int i = 0; i < info.teamCount; ++i)
        {
            const HudInfo::TeamEntry& entry = info.team[i];
            const Rectangle card = { (float)(right - cardWidth), (float)teamY, (float)cardWidth, (float)cardHeight };
            Ui::Panel(card, entry.active ? Color{ 40, 32, 14, 200 } : Ui::kPanel);
            if (entry.active) Ui::Outline(card, Ui::kAccent, 2.0f);
            const int keyWidth = Ui::KeyBadge(info.teamKeys[i], (int)card.x + 10, teamY + 9, 14,
                                              entry.active ? Ui::kAccent : Ui::kTextDim);
            const int textX = (int)card.x + 18 + keyWidth;
            const Color nameColor = !entry.alive ? Ui::kDanger : entry.active ? Ui::kAccent : Ui::kText;
            Ui::TextBold(entry.name, textX, teamY + 5, 16, nameColor);
            const char* hp = entry.alive ? TextFormat("%d", entry.health) : "—";
            Ui::Text(hp, (int)(card.x + card.width) - 12 - Ui::Measure(hp, 15), teamY + 6, 15, Ui::kTextDim);
            const float ratio = entry.alive ? (float)entry.health / 100.0f : 0.0f;
            Ui::Bar({ (float)textX, (float)teamY + 29.0f, card.x + card.width - 12.0f - (float)textX, 6.0f }, ratio,
                    ratio > 0.35f ? Ui::kGood : Ui::kDanger);
            if (entry.tag)
            {
                Ui::Chip(entry.tag, (int)card.x - 8, teamY + 2, Ui::kInfo, Ui::kInfo, true);
            }
            teamY += cardHeight + 6;
        }
        if (info.squadTotal > 0)
        {
            Ui::Chip(TextFormat("Цэрэг  %d / %d", info.squadAlive, info.squadTotal), right, teamY, Ui::kText, Ui::kGood, true);
            teamY += 40;
        }
        if (info.partnerName)
        {
            Ui::Chip(TextFormat("%s%s", info.partnerName, info.partnerHealth == 0 ? "  (унасан)" : ""), right, teamY,
                     info.partnerHealth == 0 ? Ui::kDanger : Ui::kInfo, Ui::kInfo, true);
        }
    }

    // --- Зүүн доод: амь ---
    {
        const Rectangle card = { 16.0f, (float)height - 76.0f, 300.0f, 60.0f };
        Ui::Panel(card);
        const Color barColor = healthRatio > 0.6f ? Ui::kGood : healthRatio > 0.3f ? Ui::kAccent : Ui::kDanger;
        // Зүрхний тэмдэг: хоёр тойрог, гурвалжин
        const float hx = card.x + 26.0f, hy = card.y + 22.0f;
        DrawCircleV({ hx - 5.0f, hy - 2.0f }, 6.0f, barColor);
        DrawCircleV({ hx + 5.0f, hy - 2.0f }, 6.0f, barColor);
        DrawTriangle({ hx - 11.0f, hy }, { hx, hy + 12.0f }, { hx + 11.0f, hy }, barColor);
        Ui::TextBold(TextFormat("%d", (int)player.Health()), (int)card.x + 46, (int)card.y + 6, 26, Ui::kText);
        Ui::Text(TextFormat("/ %d", (int)player.MaxHealth()),
                 (int)card.x + 52 + Ui::MeasureBold(TextFormat("%d", (int)player.Health()), 26), (int)card.y + 13, 16,
                 Ui::kTextDim);
        if (info.stance)
        {
            Ui::KeyBadge(info.stance, (int)(card.x + card.width) - 14 - Ui::MeasureBold(info.stance, 14) - 12,
                         (int)card.y + 10, 14, Ui::kAccent);
        }
        Ui::Bar({ card.x + 14.0f, card.y + 42.0f, card.width - 28.0f, 8.0f }, healthRatio, barColor);
    }

    // --- Баруун доод: зэвсэг ба сум (жижиг газрын зургийн дээр; мэдрэгчтэй дэлгэцэд доод голд) ---
    if (info.slotNames && info.weaponSlot >= 0 && info.weaponSlot < info.slotCount)
    {
        const char* weapon = info.slotNames[info.weaponSlot];
        if (weapon[0] >= '0' && weapon[0] <= '9' && weapon[1] == ' ') weapon += 2; // "3 АВТОМАТ" -> "АВТОМАТ"
        char big[32] = "", small[32] = "";
        Color bigColor = Ui::kText;
        if (info.reloading)
        {
            std::snprintf(big, sizeof(big), "ЦЭНЭГЛЭЖ БАЙНА");
            bigColor = Ui::kAccent;
        }
        else if (info.usesAmmo)
        {
            std::snprintf(big, sizeof(big), "%d", info.ammo);
            if (info.showMagazine) std::snprintf(small, sizeof(small), "/ %d", info.magazine);
            bigColor = info.ammo == 0 ? Ui::kDanger : Ui::kText;
        }
        const int bigSize = info.reloading ? 20 : 34;
        const int contentWidth = std::max(Ui::MeasureBold(weapon, 14),
                                          Ui::MeasureBold(big, bigSize) + (small[0] ? Ui::Measure(small, 18) + 8 : 0));
        const int cardWidth = std::max(150, contentWidth + 32);
        const int cardHeight = big[0] ? 74 : 40;
        const int x = info.touch ? (width - cardWidth) / 2 : width - 16 - cardWidth;
        const int y = info.touch ? height - 150 : height - kMinimapSize - kMinimapMargin - cardHeight - 10;
        Ui::Panel({ (float)x, (float)y, (float)cardWidth, (float)cardHeight });
        Ui::TextBold(weapon, x + 16, y + 10, 14, Ui::kTextDim);
        Ui::WeaponIcon(info.slotWeapons[info.weaponSlot], { (float)(x + cardWidth - 70), (float)y + 8.0f, 56.0f, 20.0f },
                       Fade(Ui::kText, 0.7f));
        if (big[0])
        {
            const int by = y + (info.reloading ? 38 : 28);
            Ui::TextBold(big, x + 16, by, bigSize, bigColor);
            if (small[0])
            {
                Ui::Text(small, x + 24 + Ui::MeasureBold(big, bigSize), by + bigSize - 22, 18, Ui::kTextDim);
            }
        }
    }

    // --- Доод голд: зэвсгийн нүднүүд ---
    if (!info.touch && info.slotNames)
    {
        const int slotWidth = 108, slotHeight = 58, gap = 8;
        const int total = info.slotCount * slotWidth + (info.slotCount - 1) * gap;
        int slotX = (width - total) / 2;
        const int slotY = height - slotHeight - 16;
        for (int i = 0; i < info.slotCount; ++i)
        {
            const bool selected = i == info.weaponSlot;
            const Rectangle tile = { (float)slotX, (float)slotY, (float)slotWidth, (float)slotHeight };
            Ui::Panel(tile, selected ? Color{ 40, 32, 14, 210 } : Ui::kPanelSoft);
            if (selected) Ui::Outline(tile, Ui::kAccent, 2.0f);
            Ui::TextBold(TextFormat("%d", i + 1), slotX + 8, slotY + 4, 13, selected ? Ui::kAccent : Ui::kTextDim);
            // Зэвсгийн дүрс, доор нь жижиг нэр ("3 АВТОМАТ" -> "АВТОМАТ")
            Ui::WeaponIcon(info.slotWeapons[i], { (float)slotX + 14.0f, (float)slotY + 6.0f, (float)slotWidth - 28.0f, 30.0f },
                           selected ? Ui::kText : Fade(Ui::kTextDim, 0.85f));
            const char* name = info.slotNames[i];
            if (name[0] >= '0' && name[0] <= '9' && name[1] == ' ') name += 2;
            const int nameWidth = Ui::MeasureBold(name, 11);
            Ui::TextBold(name, slotX + (slotWidth - nameWidth) / 2, slotY + 40, 11, selected ? Ui::kAccent : Ui::kTextDim);
            slotX += slotWidth + gap;
        }
    }

    // --- Удирдлага (F10-аар нээж, нуух) ---
    if (info.showHelp && !info.touch)
    {
        const char* help[][2] = {
            { "WASD", "явах" },        { "Shift", "гүйх" },        { "Space", "үсрэх / босох" },  { "C", "суух" },
            { "Z", "хэвтэх" },         { "LMB", "буудах" },        { "RMB", "онилох" },           { "R", "цэнэглэх" },
            { "1–5", "зэвсэг" },       { "F1–F3", "гишүүн солих" }, { "F", "унах / хаалга / цонх" }, { "E", "үйлдэл" },
            { "Q", "журнал" },         { "M", "газрын зураг" },    { "TAB", "ухрах" },            { "V", "камер 1-р / 3-р" },
            { "H", "энд бай (гишүүн)" }, { "F10", "тусламж нуух" },
        };
        const int columns = 2, rowHeight = 26;
        const int rows = (int)(sizeof(help) / sizeof(help[0]) + columns - 1) / columns;
        const int colWidth = 220;
        const int top = info.hordeSmall >= 0 ? 154 : 120;
        const Rectangle card = { 16.0f, (float)top, (float)(colWidth * columns + 20), (float)(rows * rowHeight + 46) };
        Ui::Panel(card, Ui::kPanelSoft);
        Ui::TextBold("УДИРДЛАГА", (int)card.x + 14, (int)card.y + 10, 13, Ui::kTextDim);
        for (int i = 0; i < (int)(sizeof(help) / sizeof(help[0])); ++i)
        {
            const int cx = (int)card.x + 12 + (i % columns) * colWidth;
            const int cy = (int)card.y + 34 + (i / columns) * rowHeight;
            const int keyWidth = Ui::KeyBadge(help[i][0], cx, cy, 12, Ui::kText);
            Ui::Text(help[i][1], cx + keyWidth + 8, cy + 1, 15, Ui::kTextDim);
        }
        Ui::Text(TextFormat("Мэдрэмж %.4f   [ ]     Хурд %.1f м/с   - =     Босоо урвуу: %s  (I)", config.mouseSensitivity,
                            config.walkSpeed, config.invertY ? "тийм" : "үгүй"),
                 (int)card.x + 14, (int)(card.y + card.height) + 6, 13, Fade(Ui::kTextDim, 0.8f));
    }

    if (showDebug)
    {
        const Vector3 p = player.Position();
        Ui::TextShadow(TextFormat("Байрлал: %.1f  %.1f  %.1f   Хурд: %.1f   Газарт: %s", p.x, p.y, p.z,
                                  player.HorizontalSpeed(), player.IsGrounded() ? "тийм" : "үгүй"),
                       20, height - 100, 16, Ui::kAccent);
    }

    // Унааны мэдээлэл ба ойрын унаа, хаалга, цонхны санамж: зэвсгийн нүднүүдийн дээр
    if (info.rideText)
    {
        KeyPrompt(info.rideText, width / 2, info.touch ? height - 200 : height - 116, 17, Ui::kAccent);
    }

    // Харилцах заавар: онилгооны доор ("E — Генератор асаах")
    if (info.prompt)
    {
        KeyPrompt(info.prompt, width / 2, height / 2 + 60, 20, Ui::kAccent);
    }

    // Богино мэдэгдэл: дээд голд ногоон зурвастай toast
    if (info.notice)
    {
        const int textWidth = Ui::MeasureBold(info.notice, 22);
        const Rectangle toast = { (float)(width - textWidth) / 2.0f - 30.0f, info.touch ? 60.0f : 120.0f, (float)textWidth + 60.0f,
                                  52.0f };
        Ui::PanelAccent(toast, Ui::kGood, { 10, 30, 18, 215 });
        Ui::TextBold(info.notice, (int)toast.x + 30, (int)toast.y + 14, 22, Ui::kGood);
    }

    // Том мэдээ: дэлгэцийн голд бараан тууз дээр
    if (info.banner)
    {
        const int bannerY = height / 2 - 120;
        const int bandHeight = info.subBanner ? 120 : 84;
        DrawRectangleGradientH(0, bannerY, width / 2, bandHeight, Fade(BLACK, 0.0f), Fade(BLACK, 0.7f));
        DrawRectangleGradientH(width / 2, bannerY, width - width / 2, bandHeight, Fade(BLACK, 0.7f), Fade(BLACK, 0.0f));
        const int textWidth = Ui::MeasureBold(info.banner, 52);
        Ui::TextBold(info.banner, (width - textWidth) / 2, bannerY + 14, 52, Ui::kText);
        DrawRectangle(width / 2 - 40, bannerY + 76, 80, 3, Ui::kAccent);
        if (info.subBanner)
        {
            const int subWidth = Ui::Measure(info.subBanner, 18);
            Ui::Text(info.subBanner, (width - subWidth) / 2, bannerY + 88, 18, Ui::kTextDim);
        }
    }
}
