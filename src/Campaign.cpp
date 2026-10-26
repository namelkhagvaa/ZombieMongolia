#include "Campaign.h"

#include "Account.h"

#include "Input.h"
#include "ProvinceMap.h"
#include "UiFont.h"
#include "UiStyle.h"

#include "CampaignData.h"
#include "Shop.h"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace
{
    // Газрын зургийн дэлгэц дээрх байрлал (1280x720 цонхонд тааруулсан).
    const Rectangle kMapRect = { 30.0f, 90.0f, 900.0f, 460.0f };
    const float kLonMin = 87.0f, kLonMax = 120.5f;
    const float kLatMin = 41.2f, kLatMax = 52.5f;

    const int kPanelX = 950;
    const int kPanelWidth = 300;
    // Аймгийн картын доорх довтлох товч (Update ба Draw хоёулаа ашиглана)
    const Rectangle kAttackButton = { (float)kPanelX, 246.0f, (float)kPanelWidth, 46.0f };

    const Color kLiberated = { 52, 150, 48, 255 };
    const Color kAvailable = { 238, 176, 28, 255 };
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
        Shape shape;                    // дэлгэцийн координатаар (товшилт шалгахад): хамгийн том хэсэг
        std::vector<Polygon> extra;     // бусад хэсэг (Улаанбаатарын Багануур, Багахангай)
        std::vector<Vector2> triangles; // дүүргэлт
        Vector2 label;                  // нэр бичих цэг
    };

    const std::vector<ProvinceArt>& Art()
    {
        static std::vector<ProvinceArt> art;
        if (art.empty())
        {
            // Бодит хил (ProvinceMap.cpp); даалгаврын талбарын бүдүүвч (Regions.cpp) биш
            auto toScreen = [](const Polygon& ring) {
                Polygon out;
                for (const Vector2& p : ring) out.push_back(ToScreen(p));
                return out;
            };
            for (const ProvinceMapShape& province : ProvinceMapShapes())
            {
                ProvinceArt item;
                for (size_t k = 0; k < province.parts.size(); ++k)
                {
                    const Polygon ring = toScreen(province.parts[k]);
                    if (k == 0) item.shape.outline = ring;
                    else item.extra.push_back(ring);
                    const std::vector<Vector2> triangles = Triangulate(ring);
                    item.triangles.insert(item.triangles.end(), triangles.begin(), triangles.end());
                }
                for (const Polygon& hole : province.holes) item.shape.holes.push_back(toScreen(hole));
                item.label = item.shape.InteriorPoint();
                art.push_back(item);
            }
        }
        return art;
    }

    Color StatusColor(const Progress& progress, int index)
    {
        if (progress.liberated[index]) return kLiberated;
        // Данстай үед: эрх нээгдээгүй аймаг саарал (сонговол эрх нээх, төлөх цонх гарна)
        return IsProvinceUnlocked(progress, index) && Account::Unlocked(index) ? kAvailable : kLocked;
    }

    // Цэг бүрийн өнгө: дээрээсээ (гэрэлтэй) доошоо (бараан) шугаман шилжинэ. Шугаман тул гурвалжны оройн
    // өнгийг холиход яг таарна (бодит хилийн урт нарийн гурвалжнууд туяа шиг харагдахгүй).
    Color ShadeAt(Vector2 p, Color fill)
    {
        const float t = std::clamp((p.y - kMapRect.y) / kMapRect.height, 0.0f, 1.0f);
        const float k = 1.08f - 0.3f * t;
        auto channel = [k](unsigned char c) { return (unsigned char)std::clamp((float)c * k, 0.0f, 255.0f); };
        return { channel(fill.r), channel(fill.g), channel(fill.b), fill.a };
    }

    void ShadedTriangle(Vector2 a, Vector2 b, Vector2 c, Color fill)
    {
        if ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x) > 0.0f) std::swap(b, c);
        const Color ca = ShadeAt(a, fill), cb = ShadeAt(b, fill), cc = ShadeAt(c, fill);
        rlBegin(RL_TRIANGLES);
        rlColor4ub(ca.r, ca.g, ca.b, ca.a); rlVertex2f(a.x, a.y);
        rlColor4ub(cb.r, cb.g, cb.b, cb.a); rlVertex2f(b.x, b.y);
        rlColor4ub(cc.r, cc.g, cc.b, cc.a); rlVertex2f(c.x, c.y);
        rlEnd();
    }

    void DrawProvince(const ProvinceArt& art, Color fill, bool)
    {
        for (size_t i = 0; i + 2 < art.triangles.size(); i += 3)
        {
            ShadedTriangle(art.triangles[i], art.triangles[i + 1], art.triangles[i + 2], fill);
        }

    }

    void DrawRing(const Polygon& ring, float width, Color color)
    {
        for (size_t i = 0; i < ring.size(); ++i) DrawLineEx(ring[i], ring[(i + 1) % ring.size()], width, color);
    }

    void DrawEdges(const ProvinceArt& art, float width, Color color)
    {
        DrawRing(art.shape.outline, width, color);
        for (const Polygon& part : art.extra) DrawRing(part, width, color);
    }

    // Газрын зургийн сүүдэр: бүх аймгийг бага зэрэг доош шилжүүлж бараанаар
    void DrawMapShadow(const std::vector<ProvinceArt>& art)
    {
        for (const ProvinceArt& item : art)
        {
            for (size_t i = 0; i + 2 < item.triangles.size(); i += 3)
            {
                const Vector2 offset = { 5.0f, 8.0f };
                DrawTriangleAnyWinding(Vector2Add(item.triangles[i], offset), Vector2Add(item.triangles[i + 1], offset),
                                       Vector2Add(item.triangles[i + 2], offset), { 0, 0, 0, 120 });
            }
        }
    }

    // Хулганы дор байгаа аймаг (мэдрэгч дэлгэц дээр -1)
    int HoveredProvince(const std::vector<ProvinceArt>& art)
    {
        if (InTouchMode()) return -1;
        const Vector2 mouse = InPointer();
        if (!CheckCollisionPointRec(mouse, kMapRect)) return -1;
        for (int i = 0; i < (int)art.size(); ++i)
        {
            if (art[i].shape.Contains(mouse)) return i;
        }
        return -1;
    }

    // Дэвсгэр зураг (assets/home_back.png): анх зурахад нэг удаа ачаална. Байхгүй бол id = 0.
    const Texture2D& Backdrop()
    {
        static Texture2D texture = {};
        static bool tried = false;
        if (tried) return texture;
        tried = true;
#if defined(__ANDROID__)
        // APK-ийн assets-ийн үндсэнд байрлана (FileExists нь assets-ийг хардаггүй).
        texture = LoadTexture("home_back.png");
#else
        const std::string application = GetApplicationDirectory();
        for (const std::string& folder : { std::string("assets/"), application + "assets/", application + "../assets/" })
        {
            const std::string path = folder + "home_back.png";
            if (!FileExists(path.c_str())) continue;
            texture = LoadTexture(path.c_str());
            break;
        }
#endif
        if (texture.id != 0)
        {
            GenTextureMipmaps(&texture);
            SetTextureFilter(texture, TEXTURE_FILTER_TRILINEAR);
        }
        return texture;
    }

    // Зургийг дэлгэцийг бүрэн дүүргэхээр (cover) масштаблаж, бараан давхаргаар бүрхэнэ.
    void DrawBackdrop(int width, int height)
    {
        const Texture2D& texture = Backdrop();
        if (texture.id != 0)
        {
            const float scale = std::max((float)width / (float)texture.width, (float)height / (float)texture.height);
            const float w = (float)texture.width * scale, h = (float)texture.height * scale;
            DrawTexturePro(texture, { 0.0f, 0.0f, (float)texture.width, (float)texture.height },
                           { ((float)width - w) * 0.5f, ((float)height - h) * 0.5f, w, h }, { 0.0f, 0.0f }, 0.0f, WHITE);
            DrawRectangle(0, 0, width, height, { 6, 8, 12, 92 });
        }
        else
        {
            DrawRectangleGradientV(0, 0, width, height, { 24, 32, 46, 255 }, { 10, 12, 18, 255 });
        }
        // Дээд, доод ирмэгийг бараан болгож бичвэрийг уншигдахуйц болгоно
        DrawRectangleGradientV(0, 0, width, 110, { 4, 6, 10, 210 }, { 4, 6, 10, 0 });
        DrawRectangleGradientV(0, height - 170, width, 170, { 4, 6, 10, 0 }, { 4, 6, 10, 225 });
    }

    // Газрын зургийн дэлгэцийн иконууд (assets/icons): анх зурахад нэг удаа ачаална. Байхгүй бол id = 0.
    enum class MapIcon { Attack, AttackHover, Strength, Speed, Inventory, Health, Defense, Count };

    const Texture2D& Icon(MapIcon which)
    {
        static Texture2D icons[(int)MapIcon::Count] = {};
        static bool tried = false;
        if (!tried)
        {
            tried = true;
            const char* names[(int)MapIcon::Count] = { "attack_icon_1.png", "attack_icon_2.png", "upgrade_1_strength.png",
                                                       "upgrade_5_speed.png", "upgrade_4_inventory.png", "upgrade_3_health.png",
                                                       "upgrade_2_defense.png" };
#if !defined(__ANDROID__)
            const std::string application = GetApplicationDirectory();
            std::string folder = "assets/icons/";
            for (const std::string& candidate : { std::string("assets/icons/"), application + "assets/icons/", application + "../assets/icons/" })
            {
                if (DirectoryExists(candidate.c_str())) { folder = candidate; break; }
            }
#else
            const std::string folder = "icons/"; // APK-ийн assets дотор
#endif
            for (int i = 0; i < (int)MapIcon::Count; ++i)
            {
                const std::string path = folder + names[i];
#if !defined(__ANDROID__)
                if (!FileExists(path.c_str())) continue;
#endif
                icons[i] = LoadTexture(path.c_str());
                if (icons[i].id == 0) continue;
                GenTextureMipmaps(&icons[i]); // 512 -> ~36 пиксел: ирмэг нь шаржигнахгүй
                SetTextureFilter(icons[i], TEXTURE_FILTER_TRILINEAR);
            }
        }
        return icons[(int)which];
    }

    // Иконы гол хэсгийг (зургийн төв хэсэг; ирмэг нь хоосон) box-д багтааж зурна. Зураг байхгүй бол false.
    bool DrawIcon(MapIcon which, Rectangle box, float crop, Color tint = WHITE)
    {
        const Texture2D& texture = Icon(which);
        if (texture.id == 0) return false;
        const float size = (float)texture.width * crop;
        const float offset = ((float)texture.width - size) * 0.5f;
        DrawTexturePro(texture, { offset, offset, size, size }, box, { 0.0f, 0.0f }, 0.0f, tint);
        return true;
    }

    MapIcon ShopIconFor(ShopItem item)
    {
        switch (item)
        {
        case ShopItem::Damage: return MapIcon::Strength;
        case ShopItem::FireRate: return MapIcon::Speed;
        case ShopItem::Magazine: return MapIcon::Inventory;
        case ShopItem::Health: return MapIcon::Health;
        default: return MapIcon::Defense;
        }
    }

    Ui::Icon ShopHudIconFor(ShopItem item)
    {
        switch (item)
        {
        case ShopItem::Damage: return Ui::Icon::UpgradeStrength;
        case ShopItem::FireRate: return Ui::Icon::UpgradeSpeed;
        case ShopItem::Magazine: return Ui::Icon::UpgradeInventory;
        case ShopItem::Health: return Ui::Icon::UpgradeHealth;
        default: return Ui::Icon::UpgradeDefense;
        }
    }

    // Аймгийн жижиг зураг: дэвсгэр зургийн аймаг бүрт өөр хэсэг.
    void DrawThumbnail(Rectangle box, int province)
    {
        DrawRectangleRounded(box, 0.12f, 6, { 30, 26, 24, 255 });
        const Texture2D& texture = Backdrop();
        if (texture.id != 0)
        {
            const float sh = (float)texture.height * 0.42f;
            const float sw = sh * box.width / box.height;
            const float span = (float)texture.width - sw;
            const float sx = std::fmod((float)province * 0.37f * span, span);
            const float sy = (float)texture.height * 0.36f;
            DrawTexturePro(texture, { sx, sy, sw, sh }, box, { 0.0f, 0.0f }, 0.0f, WHITE);
        }
        DrawRectangleRoundedLinesEx(box, 0.12f, 6, 1.5f, { 255, 255, 255, 60 });
    }

    // Хүндрэлийн гавлын яс
    void DrawSkull(Vector2 c, float r, Color color)
    {
        DrawCircleV({ c.x, c.y - r * 0.15f }, r, color);
        DrawRectangleRounded({ c.x - r * 0.62f, c.y + r * 0.3f, r * 1.24f, r * 0.75f }, 0.4f, 4, color);
        const Color hole = { 14, 16, 20, 255 };
        DrawCircleV({ c.x - r * 0.4f, c.y - r * 0.1f }, r * 0.3f, hole);
        DrawCircleV({ c.x + r * 0.4f, c.y - r * 0.1f }, r * 0.3f, hole);
        DrawTriangle({ c.x, c.y + r * 0.2f }, { c.x - r * 0.14f, c.y + r * 0.45f }, { c.x + r * 0.14f, c.y + r * 0.45f }, hole);
        for (float k : { -0.3f, 0.0f, 0.3f }) DrawLineEx({ c.x + r * k, c.y + r * 0.72f }, { c.x + r * k, c.y + r * 1.05f }, 1.0f, hole);
    }

    // Хөндлөн тавьсан буу (довтлох товчны дүрс)
    void DrawRifle(Vector2 c, float angle, float length, Color color)
    {
        const Vector2 d = { std::cos(angle), std::sin(angle) };
        const Vector2 n = { -d.y, d.x };
        auto at = [&](float t, float side) { return Vector2{ c.x + d.x * length * t + n.x * side, c.y + d.y * length * t + n.y * side }; };
        DrawLineEx(at(-0.5f, 0.0f), at(0.5f, 0.0f), 2.5f, color);     // гол
        DrawLineEx(at(-0.5f, 1.0f), at(-0.18f, 0.0f), 6.0f, color);   // ишний хэсэг
        DrawLineEx(at(0.02f, 1.5f), at(0.08f, 6.0f), 3.0f, color);    // сэлбэг (магазин)
    }

    // Эх ба эзлэгдсэн аймгийн байршлын тэмдэг (pin)
    void DrawPin(Vector2 tip, Color color)
    {
        const Vector2 head = { tip.x, tip.y - 20.0f };
        DrawCircleV({ tip.x + 1.0f, tip.y + 1.0f }, 3.0f, Fade(BLACK, 0.35f));
        DrawTriangle({ head.x - 7.5f, head.y + 3.0f }, tip, { head.x + 7.5f, head.y + 3.0f }, color);
        DrawCircleV(head, 9.0f, color);
        DrawCircleLinesV(head, 9.0f, Fade(BLACK, 0.4f));
        DrawCircleV(head, 3.5f, RAYWHITE);
    }

    void DrawStar(Vector2 c, float r, Color color)
    {
        Vector2 points[10];
        for (int i = 0; i < 10; ++i)
        {
            const float a = -PI / 2.0f + (float)i * PI / 5.0f;
            const float radius = i % 2 == 0 ? r : r * 0.45f;
            points[i] = { c.x + std::cos(a) * radius, c.y + std::sin(a) * radius };
        }
        for (int i = 0; i < 10; ++i) DrawTriangleAnyWinding(c, points[i], points[(i + 1) % 10], color);
    }

    // Луужин: хойд зүг улаан
    void DrawCompass(Vector2 c, float r)
    {
        DrawCircleV(c, r + 6.0f, { 10, 14, 20, 170 });
        DrawRing(c, r + 2.0f, r + 4.0f, 0.0f, 360.0f, 36, { 255, 255, 255, 70 });
        for (int i = 0; i < 4; ++i)
        {
            const float a = -PI / 2.0f + (float)i * PI / 2.0f;
            const Vector2 d = { std::cos(a), std::sin(a) }, n = { -d.y, d.x };
            const Vector2 tip = { c.x + d.x * r, c.y + d.y * r };
            const Vector2 left = { c.x + n.x * r * 0.22f, c.y + n.y * r * 0.22f };
            const Vector2 right = { c.x - n.x * r * 0.22f, c.y - n.y * r * 0.22f };
            DrawTriangleAnyWinding(tip, left, c, i == 0 ? Ui::kDanger : RAYWHITE);
            DrawTriangleAnyWinding(tip, c, right, i == 0 ? Color{ 170, 50, 40, 255 } : Color{ 150, 156, 164, 255 });
        }
        Ui::TextBold("N", (int)c.x - Ui::MeasureBold("N", 13) / 2, (int)(c.y - r - 22.0f), 13, Ui::kText);
    }

    // Сайжруулалт бүрийн дүрс (24x24 хайрцагт)
    void DrawShopIcon(ShopItem item, Rectangle box, Color color)
    {
        const Vector2 c = { box.x + box.width * 0.5f, box.y + box.height * 0.5f };
        switch (item)
        {
        case ShopItem::Damage: Ui::WeaponIcon(2, box, color); break;
        case ShopItem::FireRate: // аянга
            DrawTriangleAnyWinding({ c.x + 3.0f, c.y - 11.0f }, { c.x - 7.0f, c.y + 2.0f }, { c.x + 1.0f, c.y + 1.0f }, color);
            DrawTriangleAnyWinding({ c.x - 3.0f, c.y + 11.0f }, { c.x + 7.0f, c.y - 2.0f }, { c.x - 1.0f, c.y - 1.0f }, color);
            break;
        case ShopItem::Magazine: // гурван сум
            for (int i = 0; i < 3; ++i)
            {
                const float x = c.x - 7.0f + 7.0f * (float)i;
                DrawRectangleRec({ x - 2.5f, c.y - 4.0f, 5.0f, 13.0f }, color);
                DrawCircleV({ x, c.y - 4.0f }, 2.5f, color);
                DrawTriangle({ x - 2.5f, c.y - 4.0f }, { x + 2.5f, c.y - 4.0f }, { x, c.y - 10.0f }, color);
            }
            break;
        case ShopItem::Health: // зүрх
            DrawCircleV({ c.x - 4.5f, c.y - 3.0f }, 5.5f, color);
            DrawCircleV({ c.x + 4.5f, c.y - 3.0f }, 5.5f, color);
            DrawTriangle({ c.x - 9.8f, c.y - 1.0f }, { c.x, c.y + 10.0f }, { c.x + 9.8f, c.y - 1.0f }, color);
            break;
        default: // цэрэг
            DrawCircleV({ c.x, c.y - 6.0f }, 4.5f, color);
            DrawRectangleRounded({ c.x - 7.0f, c.y, 14.0f, 10.0f }, 0.6f, 6, color);
            break;
        }
    }

    // Хулганы дүрс (зүүн товч тодорсон)
    int DrawMouseIcon(int x, int y)
    {
        const Rectangle body = { (float)x + 6.0f, (float)y + 1.0f, 14.0f, 21.0f };
        DrawRectangleRounded({ (float)x, (float)y - 2.0f, 26.0f, 28.0f }, 0.3f, 6, Ui::kPanel);
        DrawRectangleRoundedLinesEx(body, 0.9f, 8, 1.5f, Ui::kText);
        DrawRectangleRec({ body.x + 2.0f, body.y + 2.0f, 4.5f, 6.0f }, Ui::kAccent);
        DrawLineEx({ body.x + 7.0f, body.y + 1.0f }, { body.x + 7.0f, body.y + 8.0f }, 1.0f, Ui::kText);
        return 26;
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
    // Аль ч аймгийг хүссэн дарааллаар чөлөөлж болно (хүндрэл нь аймгаас хамаарна: MissionFor).
    (void)progress;
    (void)provinceIndex;
    return true;
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

    // Сайжруулалт авах уу? цонх: АВАХ / Болих (цонхны гадна товшвол хаагдана). Бусад оролт хаагдана.
    if (confirmItem >= 0)
    {
        const ShopItem item = (ShopItem)confirmItem;
        const bool maxed = ShopLevel(progress, item) >= ShopMaxLevel(item);
        const bool affordable = !maxed && ShopWallet(progress) >= ShopCost(progress, item);
        bool yes = InKeyPressed(KEY_ENTER) || InKeyPressed(KEY_KP_ENTER);
        bool no = false;
        if (InMousePressed(MOUSE_BUTTON_LEFT))
        {
            const Vector2 mouse = InPointer();
            yes = CheckCollisionPointRec(mouse, confirmYes);
            no = CheckCollisionPointRec(mouse, confirmNo) || !CheckCollisionPointRec(mouse, confirmPanel);
        }
        if (yes)
        {
            confirmItem = -1;
            if (affordable)
            {
                if (ShopBuy(progress, item)) progressChanged = true;
            }
            else if (!maxed && Account::ServerShop())
            {
                Account::OpenGoldShop(); // Gold хүрэлцэхгүй: Gold авах цонх
            }
        }
        else if (no)
        {
            confirmItem = -1;
        }
        return Result::None;
    }

    // Сонголт: сум товч (аяны дарааллаар) эсвэл аймаг дээр товших
    if (InKeyPressed(KEY_RIGHT) || InKeyPressed(KEY_DOWN)) selected = (selected + 1) % count;
    if (InKeyPressed(KEY_LEFT) || InKeyPressed(KEY_UP)) selected = (selected + count - 1) % count;

    bool doubleClicked = false;
    bool attackPressed = false;
    if (InMousePressed(MOUSE_BUTTON_LEFT))
    {
        const Vector2 mouse = InPointer();
        attackPressed = CheckCollisionPointRec(mouse, kAttackButton);
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

    // Дэлгүүр: мөрийг товших эсвэл 1..5 товч -> баталгаажуулах цонх
    for (int i = 0; i < (int)ShopItem::Count; ++i)
    {
        const bool tapped = InMousePressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(InPointer(), shopCards[i]);
        if (InKeyPressed(KEY_ONE + i) || tapped) confirmItem = i;
    }
    if (confirmItem >= 0) return Result::None;

    // Аймаг эсвэл довтлох товч дээр хулганы заагч "гар" хэлбэртэй
    bool overCard = false;
    for (int i = 0; i < (int)ShopItem::Count; ++i) overCard = overCard || CheckCollisionPointRec(InPointer(), shopCards[i]);
    const bool pointing = !InTouchMode() && (HoveredProvince(art) >= 0 || overCard || CheckCollisionPointRec(InPointer(), kAttackButton));
    SetMouseCursor(pointing ? MOUSE_CURSOR_POINTING_HAND : MOUSE_CURSOR_DEFAULT);

    if ((doubleClicked || attackPressed || InKeyPressed(KEY_ENTER)) && IsProvinceUnlocked(progress, selected))
    {
        SetMouseCursor(MOUSE_CURSOR_DEFAULT);
        return Result::StartMission;
    }
    return Result::None;
}

void MapScreen::DrawConfirm(const Progress& progress) const
{
    confirmYes = confirmNo = confirmPanel = {};
    if (confirmItem < 0) return;
    const ShopItem item = (ShopItem)confirmItem;
    const int level = ShopLevel(progress, item), maxLevel = ShopMaxLevel(item), cost = ShopCost(progress, item);
    const int wallet = ShopWallet(progress);
    const bool maxed = level >= maxLevel, affordable = !maxed && wallet >= cost;
    const int screenWidth = ViewWidth(), screenHeight = ViewHeight();
    DrawRectangle(0, 0, screenWidth, screenHeight, { 0, 0, 0, 160 });

    const float width = std::min(480.0f, (float)screenWidth - 24.0f), height = 250.0f;
    const Rectangle panel = { ((float)screenWidth - width) * 0.5f, ((float)screenHeight - height) * 0.5f, width, height };
    confirmPanel = panel;
    Ui::Panel(panel, { 12, 16, 22, 245 }, 14.0f);
    Ui::Outline(panel, { 255, 200, 90, 140 }, 1.5f, 14.0f);
    const float x = panel.x + 26.0f, inner = width - 52.0f;
    if (!Ui::DrawIcon(ShopHudIconFor(item), { x, panel.y + 22.0f, 54.0f, 54.0f }))
    {
        DrawShopIcon(item, { x + 6.0f, panel.y + 28.0f, 42.0f, 42.0f }, Ui::kAccent);
    }
    Ui::TextBold(ShopItemName(item), (int)x + 68, (int)panel.y + 24, 22, Ui::kText);
    const char* detail = maxed ? "Хамгийн дээд түвшинд хүрсэн"
                       : item == ShopItem::Soldier ? TextFormat("+1 цэрэг  (одоо %d / %d)", level, maxLevel)
                                                   : TextFormat("Түвшин %d  ->  %d  (дээд %d)", level, level + 1, maxLevel);
    Ui::Text(detail, (int)x + 68, (int)panel.y + 54, 16, Ui::kTextDim);
    // Түвшний цэгүүд: дараагийнх нь лугшина
    const float pulse = 0.5f + 0.5f * std::sin((float)GetTime() * 5.0f);
    for (int k = 0; k < maxLevel; ++k)
    {
        const Color color = k < level ? Ui::kAccent : k == level && !maxed ? Fade(Ui::kAccent, 0.35f + 0.5f * pulse) : Color{ 255, 255, 255, 40 };
        DrawRectangleRounded({ x + (float)k * 22.0f, panel.y + 96.0f, 16.0f, 7.0f }, 1.0f, 4, color);
    }
    if (!maxed)
    {
        const char* price = TextFormat("%d Gold", cost);
        Ui::Coin({ x + 10.0f, panel.y + 136.0f }, 9.0f);
        Ui::TextBold(price, (int)x + 26, (int)panel.y + 124, 22, affordable ? Ui::kAccent : Ui::kDanger);
        const char* have = TextFormat("Танд %d Gold", wallet);
        Ui::Text(have, (int)(x + inner) - Ui::Measure(have, 16), (int)panel.y + 129, 16, Ui::kTextDim);
    }
    const float by = panel.y + height - 74.0f, half = inner * 0.5f - 6.0f;
    const char* yesLabel = maxed ? "ОК" : affordable ? "АВАХ" : Account::ServerShop() ? "+ GOLD АВАХ" : "ЗООС ХҮРЭЛЦЭХГҮЙ";
    confirmYes = { x, by, maxed ? inner : half, 52.0f };
    confirmNo = maxed ? Rectangle{} : Rectangle{ x + half + 12.0f, by, half, 52.0f };
    const bool yesHover = !InTouchMode() && CheckCollisionPointRec(InPointer(), confirmYes);
    DrawRectangleRounded(confirmYes, 0.35f, 8, yesHover ? Color{ 255, 218, 110, 255 } : Color{ 242, 178, 48, 255 });
    Ui::TextBold(yesLabel, (int)(confirmYes.x + (confirmYes.width - (float)Ui::MeasureBold(yesLabel, 19)) * 0.5f), (int)by + 15, 19,
                 { 34, 24, 6, 255 });
    if (!maxed)
    {
        const bool noHover = !InTouchMode() && CheckCollisionPointRec(InPointer(), confirmNo);
        DrawRectangleRounded(confirmNo, 0.35f, 8, { 255, 255, 255, (unsigned char)(noHover ? 44 : 22) });
        DrawRectangleRoundedLinesEx(confirmNo, 0.35f, 8, 1.0f, { 255, 255, 255, 70 });
        Ui::TextBold("Болих", (int)(confirmNo.x + (confirmNo.width - (float)Ui::MeasureBold("Болих", 18)) * 0.5f), (int)by + 16, 18, Ui::kText);
    }
}

void MapScreen::Preload() const
{
    Art();
    Backdrop();
    Icon(MapIcon::Attack);
}

void MapScreen::Draw(const Progress& progress) const
{
    const std::vector<Province>& provinces = Provinces();
    const std::vector<ProvinceArt>& art = Art();
    const int count = (int)provinces.size();
    const int liberated = progress.LiberatedCount();
    const int screenWidth = ViewWidth(), screenHeight = ViewHeight();
    const float pulse = 0.5f + 0.5f * std::sin((float)GetTime() * 4.0f);
    const Color kGold = Ui::kAccent;
    const Color kFrame = { 255, 200, 90, 90 };

    // Дэвсгэр: үйл явдлын зураг (assets/home_back.png) + бараан давхарга
    ClearBackground({ 14, 18, 26, 255 });
    DrawBackdrop(screenWidth, screenHeight);

    // --- Гарчиг ба аяны явц ---
    Ui::TextShadow("ZOMBIE", 30, 12, 38, Ui::kText, true);
    Ui::TextShadow("MONGOLIA", 30 + Ui::MeasureBold("ZOMBIE ", 38), 12, 38, kGold, true);
    const char* progressText = TextFormat("Чөлөөлсөн аймаг  %d / %d", liberated, count);
    Ui::TextShadow(progressText, 32, 56, 16, Ui::kText);
    Ui::Bar({ 42.0f + (float)Ui::Measure(progressText, 16), 62.0f, 200.0f, 7.0f }, (float)liberated / (float)count, kLiberated,
            { 0, 0, 0, 140 });
    // Данс: нэр, үнэгүй үлдсэн аймаг, худалдаж авсан эрх, "Гарах" товч
    Account::DrawBadge(screenWidth - 28, 20, true); // баруун дээд буланд
    if (liberated == count)
    {
        Ui::Chip("МОНГОЛ УЛС ЧӨЛӨӨЛӨГДЛӨӨ!", 420, 22, Ui::kAccent, kLiberated);
    }

    // --- Газрын зургийн хүрээ: бараан шил самбар, алтан хүрээ, булангийн хаалт ---
    const Rectangle frame = { kMapRect.x - 8.0f, kMapRect.y - 8.0f, kMapRect.width + 16.0f, kMapRect.height + 16.0f };
    Ui::Panel(frame, { 10, 13, 19, 96 }, 12.0f);
    Ui::Outline(frame, kFrame, 1.5f, 12.0f);
    {
        const float l = 26.0f, t = 3.0f;
        const float x0 = frame.x - 2.0f, y0 = frame.y - 2.0f, x1 = frame.x + frame.width + 2.0f, y1 = frame.y + frame.height + 2.0f;
        for (const Vector2 corner : { Vector2{ x0, y0 }, Vector2{ x1, y0 }, Vector2{ x0, y1 }, Vector2{ x1, y1 } })
        {
            const float sx = corner.x == x0 ? 1.0f : -1.0f, sy = corner.y == y0 ? 1.0f : -1.0f;
            DrawLineEx(corner, { corner.x + sx * l, corner.y }, t, kGold);
            DrawLineEx(corner, { corner.x, corner.y + sy * l }, t, kGold);
        }
    }

    // Аймгууд: эхлээд дотроо өөр нэгж агуулсан аймгууд, дараа нь бусад нь (дээр нь давхарлана)
    DrawMapShadow(art);
    for (int pass = 0; pass < 2; ++pass)
    {
        for (int i = 0; i < count; ++i)
        {
            const bool hasEnclave = !art[i].shape.holes.empty();
            if ((pass == 0) != hasEnclave) continue;
            DrawProvince(art[i], StatusColor(progress, i), false);
        }
    }
    // Хилийн дагуух бараан сүүдэр (товгор харагдуулна), дараа нь нимгэн хил
    for (int i = 0; i < count; ++i) DrawEdges(art[i], 7.0f, { 70, 36, 0, 30 });
    for (int i = 0; i < count; ++i) DrawEdges(art[i], 2.0f, kBorderLine);

    // Хулганы дор байгаа аймаг: гэрэлтүүлж, цагаан хил
    const int hovered = HoveredProvince(art);
    if (hovered >= 0 && hovered != selected)
    {
        const ProvinceArt& hoverArt = art[hovered];
        for (size_t i = 0; i + 2 < hoverArt.triangles.size(); i += 3)
        {
            DrawTriangleAnyWinding(hoverArt.triangles[i], hoverArt.triangles[i + 1], hoverArt.triangles[i + 2], Fade(WHITE, 0.2f));
        }
        DrawEdges(hoverArt, 2.0f, Fade(WHITE, 0.85f));
    }
    // Сонгосон аймаг: гэрэлтэх хил (хэд хэдэн өргөн бүдэг шугам) + цагаан хил
    {
        const ProvinceArt& chosenArt = art[selected];
        for (size_t i = 0; i + 2 < chosenArt.triangles.size(); i += 3)
        {
            DrawTriangleAnyWinding(chosenArt.triangles[i], chosenArt.triangles[i + 1], chosenArt.triangles[i + 2],
                                   Fade(WHITE, 0.08f + 0.08f * pulse));
        }
        const Polygon& chosen = chosenArt.shape.outline;
        const float widths[] = { 16.0f, 10.0f, 6.0f };
        const float alphas[] = { 0.10f, 0.18f, 0.35f };
        for (int k = 0; k < 3; ++k)
        {
            const Color glow = Fade(kGold, alphas[k] * (0.6f + 0.4f * pulse));
            for (size_t i = 0; i < chosen.size(); ++i) DrawLineEx(chosen[i], chosen[(i + 1) % chosen.size()], widths[k], glow);
        }
        for (size_t i = 0; i < chosen.size(); ++i)
        {
            DrawLineEx(chosen[i], chosen[(i + 1) % chosen.size()], 2.5f, RAYWHITE);
            DrawCircleV(chosen[i], 1.25f, RAYWHITE);
        }
    }

    // Нэрс: аяны дугаартай; Улаанбаатарын өмнө од. Давхцвал жижиг аймгийн (Орхон, Дархан-Уул г.м.) нэр доош.
    std::vector<Rectangle> labels((size_t)count);
    for (int i = 0; i < count; ++i)
    {
        const int size = i == selected ? 18 : 14;
        const float width = (float)Ui::MeasureBold(TextFormat("%d %s", i + 1, provinces[i].name), size) + 16.0f;
        labels[(size_t)i] = { art[i].label.x - width * 0.5f, art[i].label.y - (float)size * 0.5f, width, (float)size };
    }
    auto area = [&](int i) {
        const Polygon& ring = art[i].shape.outline;
        float sum = 0.0f;
        for (size_t k = 0; k < ring.size(); ++k)
        {
            const Vector2 p = ring[k], q = ring[(k + 1) % ring.size()];
            sum += p.x * q.y - q.x * p.y;
        }
        return std::fabs(sum);
    };
    for (int pass = 0; pass < 3; ++pass)
    {
        for (int i = 0; i < count; ++i)
        {
            for (int j = 0; j < count; ++j)
            {
                if (i == j || i == selected || !CheckCollisionRecs(labels[(size_t)i], labels[(size_t)j]) || area(i) > area(j)) continue;
                const Rectangle& other = labels[(size_t)j];
                labels[(size_t)i].y = labels[(size_t)i].y < other.y ? other.y - labels[(size_t)i].height - 2.0f
                                                                    : other.y + other.height + 2.0f;
            }
        }
    }
    for (int i = 0; i < count; ++i)
    {
        const char* text = TextFormat("%d %s", i + 1, provinces[i].name);
        const int size = i == selected ? 18 : 14;
        const int width = Ui::MeasureBold(text, size);
        const int x = (int)(labels[(size_t)i].x + labels[(size_t)i].width * 0.5f) - width / 2;
        const int y = (int)labels[(size_t)i].y;
        if (std::strcmp(provinces[i].name, "Улаанбаатар") == 0)
        {
            DrawStar({ (float)x - 9.0f, (float)y + (float)size * 0.5f + 1.0f }, 8.0f, Fade(BLACK, 0.6f));
            DrawStar({ (float)x - 10.0f, (float)y + (float)size * 0.5f }, 7.0f, Ui::kDanger);
        }
        Ui::TextBold(text, x + 1, y + 1, size, Fade(BLACK, 0.75f));
        Ui::TextBold(text, x, y, size, RAYWHITE);
    }
    // Сонгосон аймгийн байршлын тэмдэг (бага зэрэг дээш доош хөвнө)
    DrawPin({ art[selected].label.x, art[selected].label.y - 10.0f - 3.0f * pulse }, Ui::kDanger);

    // Хулганы дор байгаа аймгийн нэр, төлөв (хулганы хажууд)
    if (hovered >= 0)
    {
        const Vector2 mouse = InPointer();
        const char* name = TextFormat("%d %s", hovered + 1, provinces[hovered].name);
        const char* state = progress.liberated[hovered] ? "чөлөөлсөн" : "зомбид эзлэгдсэн";
        const int width = std::max(Ui::MeasureBold(name, 15), Ui::Measure(state, 13) + 14) + 20;
        float tx = mouse.x + 18.0f, ty = mouse.y + 16.0f;
        if (tx + (float)width > kMapRect.x + kMapRect.width) tx = mouse.x - 12.0f - (float)width;
        if (ty + 48.0f > kMapRect.y + kMapRect.height) ty = mouse.y - 56.0f;
        Ui::Panel({ tx, ty, (float)width, 46.0f }, { 10, 13, 19, 230 }, 8.0f);
        Ui::TextBold(name, (int)tx + 10, (int)ty + 5, 15, Ui::kText);
        DrawCircle((int)tx + 14, (int)ty + 33, 4.0f, StatusColor(progress, hovered));
        Ui::Text(state, (int)tx + 24, (int)ty + 25, 13, Ui::kTextDim);
    }

    // Луужин
    DrawCompass({ kMapRect.x + kMapRect.width - 34.0f, kMapRect.y + 46.0f }, 18.0f);

    // Тайлбар
    {
        const int legendY = (int)(kMapRect.y + kMapRect.height) - 34;
        int legendX = (int)kMapRect.x + 8;
        const struct { Color color; const char* text; } legend[] = {
            { kLiberated, "чөлөөлсөн" }, { kAvailable, "зомбид эзлэгдсэн" }, { kLocked, "түгжээтэй" } };
        const int entries = Account::Enabled() ? 3 : 2;
        for (int i = 0; i < entries; ++i)
        {
            legendX += Ui::Chip(legend[i].text, legendX, legendY, Ui::kText, legend[i].color) + 8;
        }
    }

    // --- Сонгосон аймгийн карт ---
    const bool unlocked = IsProvinceUnlocked(progress, selected);
    const bool owned = Account::Unlocked(selected); // данстай үед: эрх нээгдсэн эсэх
    const bool free = progress.liberated[selected];
    const int objectives = (int)StageFor(selected).objectives.size();
    const unsigned doneMask = progress.stages[(size_t)selected].done;
    int objectivesDone = 0;
    for (int i = 0; i < objectives; ++i) objectivesDone += (doneMask >> i) & 1u;
    if (free) objectivesDone = objectives;

    const int panelX = kPanelX, panelWidth = kPanelWidth;
    int y = 82;
    {
        const Rectangle card = { (float)panelX, (float)y, (float)panelWidth, 152.0f };
        Ui::PanelAccent(card, StatusColor(progress, selected), { 10, 13, 19, 210 });
        Ui::Outline(card, kFrame, 1.0f, 10.0f);
        Ui::Text("АЙМАГ", panelX + 18, y + 12, 13, Ui::kTextDim);
        Ui::TextBold(TextFormat("%02d / %02d", selected + 1, count), panelX + 18 + Ui::Measure("АЙМАГ ", 13), y + 12, 13, kGold);
        Ui::TextBold(provinces[selected].name, panelX + 18, y + 28, 26, Ui::kText);
        Ui::Text(free ? "Чөлөөлөгдсөн" : unlocked && owned ? "Зомбид эзлэгдсэн" : "Түгжээтэй", panelX + 18, y + 62, 15,
                 StatusColor(progress, selected));
        DrawThumbnail({ (float)panelX + (float)panelWidth - 122.0f, (float)y + 12.0f, 108.0f, 64.0f }, selected);

        Ui::Text(TextFormat("Даалгавар  %d / %d", objectivesDone, objectives), panelX + 18, y + 88, 15, Ui::kTextDim);
        Ui::Bar({ (float)panelX + 18.0f, (float)y + 110.0f, (float)panelWidth - 36.0f, 6.0f },
                objectives > 0 ? (float)objectivesDone / (float)objectives : 0.0f, kLiberated);
        // Gold хүрэлцвэл (сервер шийднэ) заавал нээлттэй өрөө: өөр тоглогч урилгагүй орж ирнэ
        if (Account::ServerShop() && Account::GoldAvailable() >= Account::RoomEntry())
        {
            Ui::Text("Нээлттэй өрөө", panelX + 18, y + 125, 15, kGold);
        }
        else
        {
            Ui::Text("Ганцаарчилсан", panelX + 18, y + 125, 15, Ui::kTextDim);
        }
        // Хүндрэл: аймгийн дугаараас (зомбины тоо, амь, хурд нэмэгддэг) 1-5 гавлын яс
        const int skulls = 1 + selected * 5 / count;
        const char* label = "Хүндрэл";
        const int labelX = panelX + panelWidth - 18 - 5 * 16 - Ui::Measure(label, 13) - 6;
        Ui::Text(label, labelX, y + 126, 13, Ui::kTextDim);
        for (int i = 0; i < 5; ++i)
        {
            DrawSkull({ (float)(panelX + panelWidth - 18 - (4 - i) * 16 - 6), (float)y + 133.0f }, 5.5f,
                      i < skulls ? Color{ 236, 232, 220, 255 } : Color{ 255, 255, 255, 40 });
        }
        y += 164;
    }

    // Довтлох товч (дарах / 2 товших / ENTER): шар градиент, хөндлөн буу, сум
    if (unlocked)
    {
        const Rectangle button = kAttackButton;
        const bool hover = CheckCollisionPointRec(InPointer(), button);
        const Color top = hover ? Color{ 255, 226, 120, 255 } : Color{ 255, 206, 84, 255 };
        const Color bottom = hover ? Color{ 236, 170, 40, 255 } : Color{ 214, 146, 28, 255 };
        DrawRectangleRounded({ button.x - 3.0f, button.y - 3.0f, button.width + 6.0f, button.height + 6.0f }, 0.3f, 8,
                             Fade(kGold, 0.15f + 0.2f * pulse));
        DrawRectangleRounded(button, 0.3f, 8, bottom);
        DrawRectangleRounded({ button.x, button.y, button.width, button.height * 0.55f }, 0.4f, 8, top);
        DrawRectangleRoundedLinesEx(button, 0.3f, 8, 2.0f, { 255, 240, 190, 200 });
        const Color ink = { 34, 24, 6, 255 };
        const Vector2 icon = { button.x + 34.0f, button.y + button.height * 0.5f };
        // Шар товч дээр икон уусахгүйн тулд ард нь бараан дугуй тэмдэг
        if (Icon(MapIcon::Attack).id != 0)
        {
            DrawCircleV(icon, 21.0f, { 34, 22, 8, 235 });
            DrawRing(icon, 20.0f, 22.0f, 0.0f, 360.0f, 32, { 255, 236, 170, 200 });
        }
        if (!DrawIcon(hover ? MapIcon::AttackHover : MapIcon::Attack, { icon.x - 22.0f, icon.y - 22.0f, 44.0f, 44.0f }, 0.8f))
        {
            DrawRifle(icon, 0.65f, 34.0f, ink);
            DrawRifle(icon, PI - 0.65f, 34.0f, ink);
        }
        const char* label = owned ? "ДОВТЛОХ" : "НЭЭХ"; // түгжээтэй: эрх нээх, төлөх цонх
        Ui::TextBold(label, panelX + (panelWidth - Ui::MeasureBold(label, 22)) / 2, (int)button.y + 11, 22, ink);
        const float ax = button.x + button.width - 30.0f + 3.0f * pulse, ay = button.y + button.height * 0.5f;
        DrawLineEx({ ax - 5.0f, ay - 9.0f }, { ax + 4.0f, ay }, 3.5f, ink);
        DrawLineEx({ ax + 4.0f, ay }, { ax - 5.0f, ay + 9.0f }, 3.5f, ink);
        y += 60;
    }
    else
    {
        Ui::Panel({ (float)panelX, (float)y, (float)panelWidth, 48.0f }, Ui::kPanelSoft, 10.0f);
        const char* label = TextFormat("Эхлээд %s-г чөлөөл", provinces[selected - 1].name);
        Ui::Text(label, panelX + (panelWidth - Ui::Measure(label, 15)) / 2, y + 14, 15, Ui::kTextDim);
        y += 60;
    }

    // --- Дэлгүүр ---
    {
        const int shopHeight = 40 + 5 * 46 + 26;
        const Rectangle shop = { (float)panelX, (float)y, (float)panelWidth, (float)shopHeight };
        Ui::Panel(shop, { 10, 13, 19, 196 }, 10.0f);
        Ui::Outline(shop, kFrame, 1.0f, 10.0f);
    }
    Ui::TextBold("САЙЖРУУЛАЛТ", panelX + 14, y + 12, 15, Ui::kText);
    {
        const char* coinText = TextFormat("%d Gold", ShopWallet(progress));
        const int coinWidth = Ui::MeasureBold(coinText, 17);
        Ui::Coin({ (float)(panelX + panelWidth - 24 - coinWidth), (float)y + 21.0f }, 8.0f);
        Ui::TextBold(coinText, panelX + panelWidth - 12 - coinWidth, y + 12, 17, kGold);
    }
    y += 40;
    for (int i = 0; i < (int)ShopItem::Count; ++i)
    {
        const ShopItem item = (ShopItem)i;
        const int level = ShopLevel(progress, item);
        const int maxLevel = ShopMaxLevel(item);
        const int cost = ShopCost(progress, item);
        const bool maxed = level >= maxLevel;
        const bool affordable = !maxed && ShopWallet(progress) >= cost;

        const Rectangle card = { (float)panelX + 8.0f, (float)y, (float)panelWidth - 16.0f, 40.0f };
        shopCards[i] = card;
        const bool hover = !InTouchMode() && confirmItem < 0 && CheckCollisionPointRec(InPointer(), card);
        DrawRectangleRounded(card, 0.25f, 6, hover ? Color{ 255, 205, 90, 34 } : Color{ 255, 255, 255, 12 });
        if (hover) DrawRectangleRoundedLinesEx(card, 0.25f, 6, 1.0f, Fade(kGold, 0.6f));
        Ui::KeyBadge(TextFormat("%d", i + 1), panelX + 16, y + 8, 13, affordable ? kGold : Ui::kTextDim);
        // assets/huds-ийн сайжруулалтын икон; байхгүй бол assets/icons-ийнх, тэр нь ч байхгүй бол зурсан дүрс
        const Color iconTint = maxed ? Color{ 255, 255, 255, 170 } : WHITE;
        if (!Ui::DrawIcon(ShopHudIconFor(item), { (float)panelX + 41.0f, (float)y + 2.0f, 36.0f, 36.0f }, iconTint) &&
            !DrawIcon(ShopIconFor(item), { (float)panelX + 40.0f, (float)y + 1.0f, 38.0f, 38.0f }, 0.62f, iconTint))
        {
            DrawRectangleRounded({ (float)panelX + 44.0f, (float)y + 6.0f, 28.0f, 28.0f }, 0.3f, 6, { 0, 0, 0, 90 });
            DrawShopIcon(item, { (float)panelX + 46.0f, (float)y + 8.0f, 24.0f, 24.0f }, maxed ? Ui::kTextDim : kGold);
        }
        Ui::TextBold(ShopItemName(item), panelX + 82, y + 4, 15, maxed ? Ui::kTextDim : Ui::kText);
        // Түвшний цэгүүд
        for (int k = 0; k < maxLevel; ++k)
        {
            DrawRectangleRounded({ (float)panelX + 82.0f + (float)k * 14.0f, (float)y + 26.0f, 10.0f, 5.0f }, 1.0f, 4,
                                 k < level ? kGold : Color{ 255, 255, 255, 40 });
        }
        const char* price = maxed ? "MAX" : TextFormat("%d", cost);
        Ui::TextBold(price, panelX + panelWidth - 20 - Ui::MeasureBold(price, 16), y + 11, 16,
                     maxed ? kLiberated : affordable ? kGold : Ui::kDanger);
        y += 46;
    }
    Ui::Text(TextFormat("Нийт устгасан зомби: %d", progress.kills), panelX + 14, y + 2, 14, Ui::kTextDim);

    // --- Удирдлага: товчны тэмдэгтэй ---
    if (!InTouchMode())
    {
        int x = 30;
        const int hy = (int)(kMapRect.y + kMapRect.height) + 20;
        x += DrawMouseIcon(x, hy) + 8;
        Ui::TextShadow("аймаг сонгох", x, hy + 3, 15, Ui::kText);
        x += Ui::Measure("аймаг сонгох", 15) + 22;
        const struct { const char* key; const char* text; } hints[] = {
            { "2× / ENTER", "довтлох" }, { "1–5 / товш", "сайжруулалт" }, { "ESC", "цэс" } };
        for (const auto& hint : hints)
        {
            x += Ui::KeyBadge(hint.key, x, hy, 14, Ui::kText) + 8;
            Ui::TextShadow(hint.text, x, hy + 3, 15, Ui::kText);
            x += Ui::Measure(hint.text, 15) + 22;
        }
    }
}
