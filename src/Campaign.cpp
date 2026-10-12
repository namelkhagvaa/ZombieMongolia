#include "Campaign.h"

#include "Input.h"
#include "UiFont.h"
#include "UiStyle.h"

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
    const Color kLocked = { 70, 78, 92, 255 };
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
    if (InKeyPressed(KEY_RIGHT) || InKeyPressed(KEY_DOWN)) selected = (selected + 1) % count;
    if (InKeyPressed(KEY_LEFT) || InKeyPressed(KEY_UP)) selected = (selected + count - 1) % count;

    bool doubleClicked = false;
    if (InMousePressed(MOUSE_BUTTON_LEFT))
    {
        const Vector2 mouse = InPointer();
        for (int i = 0; i < count; ++i)
        {
            // Contains нь нүхийг хасдаг тул Төв дотор Улаанбаатарыг товшиход Улаанбаатар сонгогдоно.
            if (art[i].shape.Contains(mouse))
            {
                // Нэг аймгийг 0.4 сек дотор хоёр товшвол шууд довтолно.
                const double now = GetTime();
                doubleClicked = (i == lastClicked && now - lastClickTime < 0.4);
                lastClicked = doubleClicked ? -1 : i;
                lastClickTime = now;
                selected = i;
                break;
            }
        }
    }

    // Дэлгүүр: 1..5 товч
    for (int i = 0; i < (int)ShopItem::Count; ++i)
    {
        if (InKeyPressed(KEY_ONE + i) && ShopBuy(progress, (ShopItem)i))
        {
            progressChanged = true;
        }
    }

    if ((doubleClicked || InKeyPressed(KEY_ENTER)) && IsProvinceUnlocked(progress, selected))
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
    const int screenWidth = ViewWidth(), screenHeight = ViewHeight();

    // Дэвсгэр: дээрээс доош бараан хөх градиент
    ClearBackground({ 14, 18, 26, 255 });
    DrawRectangleGradientV(0, 0, screenWidth, screenHeight, { 24, 32, 46, 255 }, { 10, 12, 18, 255 });

    // --- Гарчиг ба аяны явц ---
    Ui::TextBold("ZOMBIE MONGOLIA", 30, 18, 34, Ui::kText);
    DrawRectangle(30, 58, 60, 3, Ui::kAccent);
    const char* progressText = TextFormat("Чөлөөлсөн аймаг  %d / %d", liberated, count);
    Ui::Text(progressText, 104, 50, 16, Ui::kTextDim);
    Ui::Bar({ 112.0f + (float)Ui::Measure(progressText, 16), 56.0f, 180.0f, 6.0f }, (float)liberated / (float)count, kLiberated);
    if (liberated == count)
    {
        Ui::Chip("МОНГОЛ УЛС ЧӨЛӨӨЛӨГДЛӨӨ!", 380, 20, Ui::kAccent, kLiberated);
    }

    // --- Газрын зураг: эхлээд дотроо өөр нэгж агуулсан аймгууд, дараа нь бусад нь (дээр нь давхарлана) ---
    Ui::Panel({ kMapRect.x - 6.0f, kMapRect.y - 6.0f, kMapRect.width + 12.0f, kMapRect.height + 12.0f }, Ui::kPanel, 14.0f);
    for (int pass = 0; pass < 2; ++pass)
    {
        for (int i = 0; i < count; ++i)
        {
            const bool hasEnclave = !provinces[i].holes.empty();
            if ((pass == 0) != hasEnclave) continue;
            DrawProvince(art[i], StatusColor(progress, i), false);
        }
    }
    // Сонгосон аймгийн хил: лугшдаг алтан шугам
    const float pulse = 0.5f + 0.5f * std::sin((float)GetTime() * 4.0f);
    const Polygon& chosen = art[selected].shape.outline;
    for (size_t i = 0; i < chosen.size(); ++i)
    {
        DrawLineEx(chosen[i], chosen[(i + 1) % chosen.size()], 5.0f, Fade(Ui::kAccent, 0.25f + 0.25f * pulse));
        DrawLineEx(chosen[i], chosen[(i + 1) % chosen.size()], 2.5f, RAYWHITE);
    }

    // Нэрс: аяны дугаартай
    for (int i = 0; i < count; ++i)
    {
        const char* text = TextFormat("%d %s", i + 1, provinces[i].name);
        const int width = Ui::MeasureBold(text, 12);
        const int x = (int)art[i].label.x - width / 2;
        const int y = (int)art[i].label.y - 6;
        Ui::TextBold(text, x + 1, y + 1, 12, Fade(BLACK, 0.7f));
        Ui::TextBold(text, x, y, 12, i == selected ? Color{ 30, 22, 6, 255 } : RAYWHITE);
    }

    // Тайлбар
    int legendX = (int)kMapRect.x + 12;
    const int legendY = (int)(kMapRect.y + kMapRect.height) - 30;
    const struct { Color color; const char* text; } legend[] = {
        { kLiberated, "чөлөөлсөн" }, { kAvailable, "довтолж болно" }, { kLocked, "түгжээтэй" } };
    for (const auto& entry : legend)
    {
        legendX += Ui::Chip(entry.text, legendX, legendY, Ui::kTextDim, entry.color) + 8;
    }

    // --- Сонгосон аймгийн карт ---
    const MissionParams mission = MissionFor(selected);
    const bool unlocked = IsProvinceUnlocked(progress, selected);
    const bool free = progress.liberated[selected];
    const int objectives = (int)StageFor(selected).objectives.size();
    const unsigned doneMask = progress.stages[(size_t)selected].done;
    int objectivesDone = 0;
    for (int i = 0; i < objectives; ++i) objectivesDone += (doneMask >> i) & 1u;
    if (free) objectivesDone = objectives;

    const int panelX = kPanelX, panelWidth = 300;
    int y = 84;
    {
        const Rectangle card = { (float)panelX, (float)y, (float)panelWidth, 150.0f };
        Ui::PanelAccent(card, StatusColor(progress, selected));
        Ui::Text(TextFormat("ҮЕ %02d / %02d", selected + 1, count), panelX + 18, y + 12, 13, Ui::kTextDim);
        Ui::TextBold(provinces[selected].name, panelX + 18, y + 28, 26, Ui::kText);
        Ui::Text(free ? "Чөлөөлөгдсөн — дахин тоглож болно" : unlocked ? "Зомбид эзлэгдсэн" : "Түгжээтэй", panelX + 18, y + 62,
                 15, StatusColor(progress, selected));
        Ui::Text(TextFormat("Даалгавар  %d / %d", objectivesDone, objectives), panelX + 18, y + 88, 15, Ui::kTextDim);
        Ui::Bar({ (float)panelX + 18.0f, (float)y + 110.0f, (float)panelWidth - 36.0f, 6.0f },
                objectives > 0 ? (float)objectivesDone / (float)objectives : 0.0f, kLiberated);
        Ui::Text(TextFormat("Шагнал  %d зоос", free ? 0 : mission.reward), panelX + 18, y + 124, 15, Ui::kAccent);
        y += 162;
    }

    // Довтлох товч (2 товших / ENTER)
    if (unlocked)
    {
        const Rectangle button = { (float)panelX, (float)y, (float)panelWidth, 46.0f };
        DrawRectangleRounded(button, 0.5f, 8, Fade(Ui::kAccent, 0.85f + 0.15f * pulse));
        const char* label = "ДОВТЛОХ";
        Ui::TextBold(label, panelX + (panelWidth - Ui::MeasureBold(label, 20)) / 2, y + 11, 20, { 30, 22, 6, 255 });
        y += 58;
    }
    else
    {
        Ui::Panel({ (float)panelX, (float)y, (float)panelWidth, 46.0f }, Ui::kPanelSoft, 23.0f);
        const char* label = TextFormat("Эхлээд %s-г чөлөөл", provinces[selected - 1].name);
        Ui::Text(label, panelX + (panelWidth - Ui::Measure(label, 15)) / 2, y + 13, 15, Ui::kTextDim);
        y += 58;
    }

    // --- Дэлгүүр ---
    Ui::TextBold("САЙЖРУУЛАЛТ", panelX, y + 6, 14, Ui::kTextDim);
    const char* coinText = TextFormat("%d", progress.coins);
    Ui::Chip(coinText, panelX + panelWidth, y, Ui::kAccent, Ui::kAccent, true);
    y += 40;
    for (int i = 0; i < (int)ShopItem::Count; ++i)
    {
        const ShopItem item = (ShopItem)i;
        const int level = ShopLevel(progress, item);
        const int maxLevel = ShopMaxLevel(item);
        const int cost = ShopCost(progress, item);
        const bool maxed = level >= maxLevel;
        const bool affordable = !maxed && progress.coins >= cost;

        const Rectangle card = { (float)panelX, (float)y, (float)panelWidth, 44.0f };
        Ui::Panel(card, Ui::kPanelSoft, 10.0f);
        Ui::KeyBadge(TextFormat("%d", i + 1), panelX + 10, y + 10, 14, affordable ? Ui::kAccent : Ui::kTextDim);
        Ui::TextBold(ShopItemName(item), panelX + 44, y + 5, 15, maxed ? Ui::kTextDim : Ui::kText);
        // Түвшний цэгүүд
        for (int k = 0; k < maxLevel; ++k)
        {
            DrawRectangleRounded({ (float)panelX + 44.0f + (float)k * 14.0f, (float)y + 28.0f, 10.0f, 6.0f }, 1.0f, 4,
                                 k < level ? Ui::kAccent : Color{ 255, 255, 255, 40 });
        }
        const char* price = maxed ? "MAX" : TextFormat("%d", cost);
        Ui::TextBold(price, panelX + panelWidth - 14 - Ui::MeasureBold(price, 16), y + 13, 16,
                     maxed ? kLiberated : affordable ? Ui::kAccent : Ui::kDanger);
        y += 50;
    }
    Ui::Text(TextFormat("Нийт устгасан зомби: %d", progress.kills), panelX, y + 4, 14, Ui::kTextDim);

    // --- Удирдлага: товчны тэмдэгтэй ---
    if (!InTouchMode())
    {
        int x = 30;
        const int hy = (int)(kMapRect.y + kMapRect.height) + 22;
        const struct { const char* key; const char* text; } hints[] = {
            { "← →", "аймаг сонгох" }, { "2× / ENTER", "довтлох" }, { "1–5", "сайжруулалт авах" }, { "ESC", "гарах" } };
        for (const auto& hint : hints)
        {
            x += Ui::KeyBadge(hint.key, x, hy, 14, Ui::kText) + 8;
            Ui::Text(hint.text, x, hy + 3, 15, Ui::kTextDim);
            x += Ui::Measure(hint.text, 15) + 26;
        }
    }
}
