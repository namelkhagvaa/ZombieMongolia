#include "Campaign.h"

#include "CampaignData.h"
#include "Shop.h"
#include "raylib.h"

#include <cmath>

namespace
{
    // Газрын зургийн дэлгэц дээрх байрлал (1280x720 цонхонд тааруулсан).
    const Rectangle kMapRect = { 30.0f, 90.0f, 900.0f, 460.0f };
    const float kLonMin = 87.0f, kLonMax = 120.5f;
    const float kLatMin = 41.2f, kLatMax = 52.5f;

    const int kPanelX = 950;

    const Color kLiberated = { 60, 170, 80, 255 };
    const Color kAvailable = { 235, 190, 40, 255 };
    const Color kLocked = { 104, 108, 112, 255 };
    const Color kBorderLine = { 24, 28, 36, 255 };

    Vector2 ToScreen(Vector2 lonLat)
    {
        const float u = (lonLat.x - kLonMin) / (kLonMax - kLonMin);
        const float v = (kLatMax - lonLat.y) / (kLatMax - kLatMin);
        return { kMapRect.x + u * kMapRect.width, kMapRect.y + v * kMapRect.height };
    }

    // Аймаг бүрийн дэлгэц дээрх дүрс: нэг удаа тооцоод хадгална.
    struct ProvinceArt
    {
        Shape shape;                    // дэлгэцийн координатаар (товшилт шалгахад)
        std::vector<Vector2> triangles; // дүүргэлт
        Vector2 label;                  // нэр бичих цэг
    };

    const std::vector<ProvinceArt>& Art()
    {
        static std::vector<ProvinceArt> art;
        if (art.empty())
        {
            for (const Province& province : Provinces())
            {
                ProvinceArt item;
                for (const Vector2& p : province.outline) item.shape.outline.push_back(ToScreen(p));
                for (const Polygon& hole : province.holes)
                {
                    Polygon ring;
                    for (const Vector2& p : hole) ring.push_back(ToScreen(p));
                    item.shape.holes.push_back(ring);
                }
                item.triangles = Triangulate(item.shape.outline);
                item.label = item.shape.InteriorPoint();
                art.push_back(item);
            }
        }
        return art;
    }

    Color StatusColor(const Progress& progress, int index)
    {
        if (progress.liberated[index]) return kLiberated;
        return IsProvinceUnlocked(progress, index) ? kAvailable : kLocked;
    }

    void DrawProvince(const ProvinceArt& art, Color fill, bool)
    {
        for (size_t i = 0; i + 2 < art.triangles.size(); i += 3)
        {
            DrawTriangleAnyWinding(art.triangles[i], art.triangles[i + 1], art.triangles[i + 2], fill);
        }

        const Polygon& outline = art.shape.outline;
        for (size_t i = 0; i < outline.size(); ++i)
        {
            DrawLineEx(outline[i], outline[(i + 1) % outline.size()], 1.5f, kBorderLine);
        }
    }
}

MissionParams MissionFor(int provinceIndex)
{
    const int d = provinceIndex;
    MissionParams params;
    params.waves = 3 + d / 6;
    params.baseZombies = 6 + 2 * d;
    params.healthScale = 1.0f + 0.1f * (float)d;
    params.zombieDamage = 8.0f + 0.5f * (float)d;
    params.speedBonus = 0.03f * (float)d;
    params.reward = 60 + 25 * d;
    return params;
}

bool IsProvinceUnlocked(const Progress& progress, int provinceIndex)
{
    // Баруунаас зүүн тийш нэг нэгээр: өмнөх аймгийг чөлөөлсний дараа дараагийнх нээгдэнэ.
    return provinceIndex == 0 || progress.liberated[provinceIndex - 1];
}

void MapScreen::SelectNextTarget(const Progress& progress)
{
    for (int i = 0; i < (int)progress.liberated.size(); ++i)
    {
        if (!progress.liberated[i])
        {
            selected = i;
            return;
        }
    }
}

MapScreen::Result MapScreen::Update(Progress& progress, bool& progressChanged)
{
    const std::vector<ProvinceArt>& art = Art();
    const int count = (int)art.size();

    // Сонголт: сум товч (аяны дарааллаар) эсвэл аймаг дээр товших
    if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_DOWN)) selected = (selected + 1) % count;
    if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_UP)) selected = (selected + count - 1) % count;

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        const Vector2 mouse = GetMousePosition();
        for (int i = 0; i < count; ++i)
        {
            // Contains нь нүхийг хасдаг тул Төв дотор Улаанбаатарыг товшиход Улаанбаатар сонгогдоно.
            if (art[i].shape.Contains(mouse))
            {
                selected = i;
                break;
            }
        }
    }

    // Дэлгүүр: 1..5 товч
    for (int i = 0; i < (int)ShopItem::Count; ++i)
    {
        if (IsKeyPressed(KEY_ONE + i) && ShopBuy(progress, (ShopItem)i))
        {
            progressChanged = true;
        }
    }

    if (IsKeyPressed(KEY_ENTER) && IsProvinceUnlocked(progress, selected))
    {
        return Result::StartMission;
    }
    return Result::None;
}

void MapScreen::Draw(const Progress& progress) const
{
    const std::vector<Province>& provinces = Provinces();
    const std::vector<ProvinceArt>& art = Art();
    const int count = (int)provinces.size();
    const int liberated = progress.LiberatedCount();

    ClearBackground({ 28, 34, 44, 255 });
    DrawText("ZOMBIE MONGOLIA", 30, 20, 36, RAYWHITE);
    DrawText(TextFormat("Liberated: %d / %d", liberated, count), 30, 60, 20, kLiberated);
    if (liberated == count)
    {
        DrawText("MONGOLIA IS FREE!", 330, 26, 30, kAvailable);
    }

    // --- Газрын зураг: эхлээд дотроо өөр нэгж агуулсан аймгууд, дараа нь бусад нь (дээр нь давхарлана) ---
    DrawRectangleRec(kMapRect, { 38, 46, 60, 255 });
    for (int pass = 0; pass < 2; ++pass)
    {
        for (int i = 0; i < count; ++i)
        {
            const bool hasEnclave = !provinces[i].holes.empty();
            if ((pass == 0) != hasEnclave) continue;
            DrawProvince(art[i], StatusColor(progress, i), false);
        }
    }
    // Сонгосон аймгийн хилийг цагаанаар тодруулна.
    const Polygon& chosen = art[selected].shape.outline;
    for (size_t i = 0; i < chosen.size(); ++i)
    {
        DrawLineEx(chosen[i], chosen[(i + 1) % chosen.size()], 3.0f, RAYWHITE);
    }

    // Нэрс: аяны дугаартай
    for (int i = 0; i < count; ++i)
    {
        const char* text = TextFormat("%d %s", i + 1, provinces[i].name);
        const int width = MeasureText(text, 10);
        const int x = (int)art[i].label.x - width / 2;
        const int y = (int)art[i].label.y - 5;
        DrawText(text, x + 1, y + 1, 10, Fade(BLACK, 0.7f));
        DrawText(text, x, y, 10, RAYWHITE);
    }

    // Тайлбар
    int legendX = (int)kMapRect.x + 10;
    const int legendY = (int)(kMapRect.y + kMapRect.height) - 24;
    const struct { Color color; const char* text; } legend[] = {
        { kLiberated, "liberated" }, { kAvailable, "can attack" }, { kLocked, "locked" } };
    for (const auto& entry : legend)
    {
        DrawCircle(legendX + 6, legendY + 8, 6.0f, entry.color);
        DrawText(entry.text, legendX + 18, legendY, 16, LIGHTGRAY);
        legendX += 130;
    }

    // --- Сонгосон аймгийн мэдээлэл ---
    const MissionParams mission = MissionFor(selected);
    const bool unlocked = IsProvinceUnlocked(progress, selected);
    const bool free = progress.liberated[selected];

    int y = 90;
    DrawText(provinces[selected].name, kPanelX, y, 28, RAYWHITE);
    y += 36;
    DrawText(free ? "Liberated (replay for coins)" : unlocked ? "Occupied by zombies" : "Locked",
             kPanelX, y, 18, StatusColor(progress, selected));
    y += 28;
    DrawText(TextFormat("Stage: %d / %d", selected + 1, count), kPanelX, y, 18, LIGHTGRAY);
    y += 22;
    const int objectives = (int)StageFor(selected).objectives.size();
    const unsigned doneMask = progress.stages[(size_t)selected].done;
    int objectivesDone = 0;
    for (int i = 0; i < objectives; ++i) objectivesDone += (doneMask >> i) & 1u;
    DrawText(TextFormat("Objectives: %d / %d done", free ? objectives : objectivesDone, objectives), kPanelX, y, 18,
             LIGHTGRAY);
    y += 22;
    DrawText(TextFormat("Reward: %d coins", free ? 0 : mission.reward), kPanelX, y, 18, LIGHTGRAY);
    y += 40;

    // --- Дэлгүүр ---
    DrawText(TextFormat("Coins: %d", progress.coins), kPanelX, y, 24, kAvailable);
    y += 34;
    for (int i = 0; i < (int)ShopItem::Count; ++i)
    {
        const ShopItem item = (ShopItem)i;
        const int level = ShopLevel(progress, item);
        const int maxLevel = ShopMaxLevel(item);
        const int cost = ShopCost(progress, item);
        const bool maxed = level >= maxLevel;
        const Color color = maxed ? kLocked : progress.coins >= cost ? RAYWHITE : GRAY;

        DrawText(TextFormat("[%d] %s", i + 1, ShopItemName(item)), kPanelX, y, 18, color);
        y += 20;
        DrawText(maxed ? TextFormat("     level %d/%d  MAX", level, maxLevel)
                       : TextFormat("     level %d/%d  cost %d", level, maxLevel, cost),
                 kPanelX, y, 16, color);
        y += 26;
    }

    DrawText(TextFormat("Total kills: %d", progress.kills), kPanelX, y + 6, 16, LIGHTGRAY);

    // --- Удирдлага ---
    DrawText("Click / arrows: select province     ENTER: attack     1-5: buy upgrade     ESC: quit",
             30, 580, 18, LIGHTGRAY);
    if (!unlocked)
    {
        DrawText(TextFormat("Liberate %s first: provinces open one by one from west to east.",
                            provinces[selected - 1].name),
                 30, 610, 18, kAvailable);
    }
}
