#include "UiStyle.h"

#include "UiFont.h"

#include <algorithm>
#include <string>
#include <utility>

namespace Ui
{
    namespace
    {
        // raylib-ийн бөөрөнхийлөлт нь богино талын хувиар өгөгддөг: пикселийн радиусыг түүнд шилжүүлнэ.
        float Roundness(Rectangle area, float radius)
        {
            const float shortSide = std::min(area.width, area.height);
            return shortSide > 0.0f ? std::clamp(2.0f * radius / shortSide, 0.0f, 1.0f) : 0.0f;
        }
    }

    void Panel(Rectangle area, Color fill, float radius)
    {
        const float r = Roundness(area, radius);
        DrawRectangleRounded(area, r, 8, fill);
        DrawRectangleRoundedLinesEx(area, r, 8, 1.0f, kBorder);
    }

    void PanelAccent(Rectangle area, Color accent, Color fill, float radius)
    {
        Panel(area, fill, radius);
        const Rectangle stripe = { area.x + 5.0f, area.y + 8.0f, 3.0f, area.height - 16.0f };
        DrawRectangleRounded(stripe, 1.0f, 4, accent);
    }

    void Outline(Rectangle area, Color color, float thickness, float radius)
    {
        DrawRectangleRoundedLinesEx(area, Roundness(area, radius), 8, thickness, color);
    }

    void Bar(Rectangle area, float ratio, Color fill, Color back)
    {
        ratio = std::clamp(ratio, 0.0f, 1.0f);
        DrawRectangleRounded(area, 1.0f, 8, back);
        if (ratio > 0.0f)
        {
            Rectangle filled = area;
            filled.width = std::max(area.height, area.width * ratio); // бөөрөнхий үзүүр хэвээр
            DrawRectangleRounded(filled, 1.0f, 8, fill);
            // Дээд талын гэрэлтэлт
            DrawRectangleRounded({ filled.x + 2.0f, filled.y + 1.0f, filled.width - 4.0f, filled.height * 0.35f }, 1.0f, 6,
                                 Fade(WHITE, 0.18f));
        }
    }

    int KeyBadge(const char* key, int x, int y, int size, Color color)
    {
        const int textWidth = MeasureBold(key, size);
        const int width = std::max(textWidth + 12, size + 8);
        const Rectangle box = { (float)x, (float)y, (float)width, (float)size + 8.0f };
        DrawRectangleRounded(box, 0.35f, 6, { 255, 255, 255, 36 });
        DrawRectangleRoundedLinesEx(box, 0.35f, 6, 1.0f, Fade(color, 0.55f));
        TextBold(key, x + (width - textWidth) / 2, y + 4, size, color);
        return width;
    }

    void Text(const char* text, int x, int y, int size, Color color)
    {
        UiText(text, x, y, size, color);
    }

    void TextBold(const char* text, int x, int y, int size, Color color)
    {
        UiTextBold(text, x, y, size, color);
    }

    void TextShadow(const char* text, int x, int y, int size, Color color, bool bold)
    {
        const Color shade = Fade(BLACK, 0.7f * (float)color.a / 255.0f);
        if (bold)
        {
            UiTextBold(text, x + 1, y + 2, size, shade);
            UiTextBold(text, x, y, size, color);
        }
        else
        {
            UiText(text, x + 1, y + 2, size, shade);
            UiText(text, x, y, size, color);
        }
    }

    int Measure(const char* text, int size)
    {
        return UiMeasure(text, size);
    }

    int MeasureBold(const char* text, int size)
    {
        return UiMeasureBold(text, size);
    }

    int Chip(const char* text, int x, int y, Color textColor, Color dot, bool right)
    {
        const int size = 18;
        const int dotSpace = dot.a > 0 ? 16 : 0;
        const int width = MeasureBold(text, size) + 24 + dotSpace;
        if (right) x -= width;
        Panel({ (float)x, (float)y, (float)width, 32.0f }, kPanel, 16.0f);
        if (dot.a > 0)
        {
            DrawCircle(x + 17, y + 16, 5.0f, dot);
        }
        TextBold(text, x + 12 + dotSpace, y + 6, size, textColor);
        return width;
    }

    // --- Зурган иконууд ---
    namespace
    {
        const char* kIconFiles[(int)Icon::Count] = {
            "icon_time_day", "icon_time_sunset", "icon_time_night", "icon_compass", "icon_kill_count", "icon_currency", "icon_team", "icon_timer",
            "icon_zombie", "icon_zombie_elite", "icon_speed_boost", "icon_location", "icon_mission", "icon_mission_complete", "icon_warning", "icon_reward",
            "icon_health", "icon_armor", "icon_ammo", "icon_ammo_large", "icon_ammo_clip", "icon_weapon_automatic", "icon_weapon_pistol",
            "icon_weapon_sniper", "icon_weapon_melee", "icon_grenade",
            "icon_settings", "icon_sound", "icon_fullscreen", "icon_pause", "icon_play", "icon_close", "icon_back", "icon_menu",
            "minimap_player", "minimap_zombie", "minimap_elite", "minimap_ally", "minimap_mission", "minimap_base", "minimap_vehicle",
            "minimap_horse", "minimap_supply", "minimap_tower",
            "upgrade_strength", "upgrade_defense", "upgrade_health_capacity", "upgrade_inventory", "upgrade_speed",
        };

        Texture2D textures[(int)Icon::Count] = {};
        Rectangle bounds[(int)Icon::Count] = {}; // зургийн ил тод бус хэсэг (пикселээр)

        const Texture2D& IconTexture(Icon icon)
        {
            static bool tried = false;
            if (!tried)
            {
                tried = true;
#if defined(__ANDROID__)
                const std::string folder = "huds/"; // APK-ийн assets дотор
#else
                std::string folder = "assets/huds/";
                const std::string application = GetApplicationDirectory();
                for (const std::string& candidate : { std::string("assets/huds/"), application + "assets/huds/", application + "../assets/huds/" })
                {
                    if (DirectoryExists(candidate.c_str())) { folder = candidate; break; }
                }
#endif
                for (int i = 0; i < (int)Icon::Count; ++i)
                {
                    const std::string path = folder + kIconFiles[i] + ".png";
#if !defined(__ANDROID__)
                    if (!FileExists(path.c_str())) continue;
#endif
                    Image image = LoadImage(path.c_str());
                    if (image.data == nullptr) continue;
                    bounds[i] = GetImageAlphaBorder(image, 0.1f);
                    if (bounds[i].width <= 0.0f) bounds[i] = { 0.0f, 0.0f, (float)image.width, (float)image.height };
                    textures[i] = LoadTextureFromImage(image);
                    UnloadImage(image);
                    if (textures[i].id == 0) continue;
                    GenTextureMipmaps(&textures[i]); // 128 -> ~20 px: ирмэг шаржигнахгүй
                    SetTextureFilter(textures[i], TEXTURE_FILTER_TRILINEAR);
                }
            }
            return textures[(int)icon];
        }
    }

    bool HasIcon(Icon icon)
    {
        return IconTexture(icon).id != 0;
    }

    bool DrawIcon(Icon icon, Rectangle box, Color tint, float rotation)
    {
        const Texture2D& texture = IconTexture(icon);
        if (texture.id == 0) return false;
        // Зургийн дүрстэй хэсгийг (хоосон захгүй) төвд нь дөрвөлжин болгож хайрцгийг дүүргэнэ
        const Rectangle b = bounds[(int)icon];
        const float side = std::min((float)texture.width, std::max(b.width, b.height) + 4.0f);
        const float sx = std::clamp(b.x + b.width * 0.5f - side * 0.5f, 0.0f, (float)texture.width - side);
        const float sy = std::clamp(b.y + b.height * 0.5f - side * 0.5f, 0.0f, (float)texture.height - side);
        const float size = std::min(box.width, box.height);
        const Rectangle dest = { box.x + box.width * 0.5f, box.y + box.height * 0.5f, size, size };
        DrawTexturePro(texture, { sx, sy, side, side }, dest, { size * 0.5f, size * 0.5f }, rotation, tint);
        return true;
    }

    int IconChip(Icon icon, const char* text, int x, int y, Color textColor, Color dot, bool right)
    {
        if (!HasIcon(icon)) return Chip(text, x, y, textColor, dot, right);
        const int size = 18;
        const int width = MeasureBold(text, size) + 24 + 22;
        if (right) x -= width;
        Panel({ (float)x, (float)y, (float)width, 32.0f }, kPanel, 16.0f);
        DrawIcon(icon, { (float)x + 8.0f, (float)y + 6.0f, 20.0f, 20.0f });
        TextBold(text, x + 12 + 22, y + 6, size, textColor);
        return width;
    }

    namespace
    {
        // Дүрсний орон зай: 100 x 40, баруун тийш харсан. Хайрцагт харьцаагаа хадгалан төвлөрнө.
        struct IconSpace
        {
            float ox, oy, k;
            Vector2 P(float x, float y) const { return { ox + x * k, oy + y * k }; }
        };

        void Tri(const IconSpace& s, Vector2 a, Vector2 b, Vector2 c, Color color)
        {
            Vector2 pa = s.P(a.x, a.y), pb = s.P(b.x, b.y), pc = s.P(c.x, c.y);
            // raylib-ийн DrawTriangle нь цагийн зүүний эсрэг дараалал шаардана (дэлгэцийн Y доошоо).
            if ((pb.x - pa.x) * (pc.y - pa.y) - (pb.y - pa.y) * (pc.x - pa.x) > 0.0f) std::swap(pb, pc);
            DrawTriangle(pa, pb, pc, color);
        }
        void Quad4(const IconSpace& s, Vector2 a, Vector2 b, Vector2 c, Vector2 d, Color color)
        {
            Tri(s, a, b, c, color);
            Tri(s, a, c, d, color);
        }
        void Rect(const IconSpace& s, float x, float y, float w, float h, Color color)
        {
            Quad4(s, { x, y }, { x + w, y }, { x + w, y + h }, { x, y + h }, color);
        }
    }

    void WeaponIcon(int type, Rectangle box, Color color)
    {
        // Зурган икон байвал түүгээр (хутга, гар буу, автомат, гранат, дурантай буу); бусад нь доорх кодоор.
        const Icon pictures[7] = { Icon::WeaponMelee, Icon::WeaponPistol, Icon::WeaponAutomatic, Icon::Count, Icon::Grenade,
                                   Icon::WeaponSniper, Icon::Count };
        if (type >= 0 && type < 7 && pictures[type] != Icon::Count && HasIcon(pictures[type]))
        {
            const Texture2D& texture = IconTexture(pictures[type]);
            // Зургийн зөвхөн дүрстэй хэсгийг харьцаагаар нь хайрцагт багтаана
            const Rectangle source = bounds[(int)pictures[type]];
            const float fit = std::min(box.width / source.width, box.height / source.height);
            const Rectangle dest = { box.x + (box.width - source.width * fit) * 0.5f, box.y + (box.height - source.height * fit) * 0.5f,
                                     source.width * fit, source.height * fit };
            // Цайвар зураг: өнгийн тодролоор (сонгогдсон = цагаан, бусад = бүдэг) гэрэлтүүлнэ
            const int level = std::min(255, (int)std::max({ color.r, color.g, color.b }) + 20);
            const Color tint = { (unsigned char)level, (unsigned char)level, (unsigned char)level, color.a };
            DrawTexturePro(texture, source, dest, { 0.0f, 0.0f }, 0.0f, tint);
            return;
        }
        const float k = std::min(box.width / 100.0f, box.height / 40.0f);
        const IconSpace s = { box.x + (box.width - 100.0f * k) * 0.5f, box.y + (box.height - 40.0f * k) * 0.5f, k };
        const Color dim = Fade(color, 0.55f);

        switch (type)
        {
        case 0: // хутга
            Quad4(s, { 4, 18 }, { 32, 17 }, { 32, 25 }, { 6, 25 }, dim);              // иш
            for (int i = 0; i < 4; ++i) Rect(s, 9.0f + 6.0f * (float)i, 17, 1.5f, 8, color);
            Rect(s, 32, 11, 4, 20, color);                                           // хамгаалалт
            Quad4(s, { 36, 17 }, { 84, 17 }, { 84, 24 }, { 36, 25 }, color);          // ир
            Tri(s, { 84, 17 }, { 98, 21 }, { 84, 24 }, color);                       // үзүүр
            Rect(s, 40, 19.5f, 40, 1.2f, dim);                                       // ховил
            break;
        case 1: // гар буу
            Rect(s, 26, 9, 50, 10, color);                                           // замаг
            for (int i = 0; i < 4; ++i) Rect(s, 30.0f + 3.5f * (float)i, 10, 1.4f, 8, dim);
            Rect(s, 76, 11, 4, 5, color);                                            // амсар
            Quad4(s, { 30, 19 }, { 46, 19 }, { 40, 37 }, { 26, 37 }, color);          // бариул
            Quad4(s, { 31, 22 }, { 43, 22 }, { 39, 34 }, { 29, 34 }, dim);
            Quad4(s, { 46, 19 }, { 56, 19 }, { 54, 27 }, { 46, 27 }, dim);            // гох хамгаалалт
            Rect(s, 72, 6, 2.5f, 3, color);                                          // хараа
            break;
        case 2: // автомат
            Quad4(s, { 2, 14 }, { 24, 15 }, { 24, 23 }, { 4, 30 }, color);            // бөгс
            Rect(s, 24, 13, 32, 10, color);                                          // их бие
            Rect(s, 26, 10, 26, 3.5f, dim);                                          // тоосны таг
            Quad4(s, { 30, 23 }, { 36, 23 }, { 32, 34 }, { 26, 33 }, color);          // бариул
            Quad4(s, { 42, 23 }, { 51, 23 }, { 58, 37 }, { 49, 39 }, color);          // муруй сумны хайрцаг
            Rect(s, 56, 15, 16, 8, dim);                                             // гар хамгаалалт
            Rect(s, 56, 11, 20, 3.5f, color);                                        // хийн хоолой
            Rect(s, 72, 16, 22, 2.5f, color);                                        // гол төмөр
            Rect(s, 93, 15, 5, 4.5f, color);                                         // амны тоормос
            Rect(s, 86, 8, 2, 8, color);                                             // урд хараа
            break;
        case 3: // үрлэн буу
            Quad4(s, { 2, 14 }, { 24, 15 }, { 24, 23 }, { 4, 29 }, dim);              // бөгс
            Rect(s, 24, 12, 22, 11, color);                                          // их бие
            Quad4(s, { 28, 23 }, { 34, 23 }, { 30, 32 }, { 25, 31 }, color);          // гох
            Rect(s, 46, 12, 52, 3.5f, color);                                        // гол төмөр
            Rect(s, 46, 17, 40, 3, color);                                           // сумны хоолой
            Rect(s, 58, 15, 16, 8, dim);                                             // гүйдэг гар
            for (int i = 0; i < 4; ++i) Rect(s, 60.0f + 3.6f * (float)i, 15, 1.4f, 8, color);
            break;
        case 4: // гранат
        {
            const Vector2 c = s.P(50, 23);
            DrawCircleV(c, 13.0f * k, color);
            for (int i = 0; i < 3; ++i) Rect(s, 37, 17.0f + 5.0f * (float)i, 26, 1.2f, Fade(BLACK, 0.45f));
            Rect(s, 37.5f, 21, 1.2f, 6, Fade(BLACK, 0.45f));
            Rect(s, 61.5f, 21, 1.2f, 6, Fade(BLACK, 0.45f));
            Rect(s, 46, 6, 8, 5, color);                                             // гал хамгаалагч
            Quad4(s, { 52, 7 }, { 62, 9 }, { 64, 22 }, { 61, 22 }, dim);              // хөшүүрэг
            DrawRing(s.P(41, 7), 3.0f * k, 4.5f * k, 0.0f, 360.0f, 16, color);       // цагираг
            break;
        }
        case 5: // дурантай буу
            Quad4(s, { 2, 16 }, { 22, 16 }, { 22, 22 }, { 2, 30 }, dim);              // бөгс
            Quad4(s, { 8, 20 }, { 16, 20 }, { 16, 24 }, { 10, 26 }, Fade(BLACK, 0.0f));
            Rect(s, 22, 16, 30, 8, color);                                           // их бие
            Rect(s, 36, 24, 6, 8, color);                                            // сумны хайрцаг
            Rect(s, 52, 17, 20, 6, dim);                                             // гар хамгаалалт
            Rect(s, 72, 18.5f, 24, 2.2f, color);                                     // гол төмөр
            Rect(s, 95, 17.5f, 4, 4, color);
            Rect(s, 30, 6, 26, 5, color);                                            // дуран
            Quad4(s, { 56, 6 }, { 62, 4 }, { 62, 13 }, { 56, 11 }, color);
            Quad4(s, { 24, 4.5f }, { 30, 6 }, { 30, 11 }, { 24, 12.5f }, color);
            Rect(s, 36, 11, 2.5f, 5, dim);                                           // суурь
            Rect(s, 48, 11, 2.5f, 5, dim);
            break;
        case 6: // пуужин харвагч
            Quad4(s, { 0, 13 }, { 10, 16 }, { 10, 23 }, { 0, 26 }, color);            // арын юүлүүр
            Rect(s, 10, 16, 62, 7, color);                                           // хоолой
            Rect(s, 28, 15, 18, 9, dim);                                             // дулаан хамгаалалт
            Rect(s, 40, 23, 4, 10, color);                                           // бариул
            Rect(s, 54, 23, 4, 10, color);
            Quad4(s, { 72, 17 }, { 76, 13 }, { 76, 26 }, { 72, 22 }, color);          // пуужингийн хүзүү
            Rect(s, 76, 12, 10, 15, color);                                          // толгой
            Tri(s, { 86, 12 }, { 99, 19.5f }, { 86, 27 }, color);                    // хошуу
            break;
        default:
            break;
        }
    }

    void Coin(Vector2 center, float radius, float alpha)
    {
        auto a = [alpha](Color color) { return Fade(color, alpha * (float)color.a / 255.0f); };
        // Сүүдэр, зузаан ирмэг (доош нь бага зэрэг шилжсэн бараан тойрог)
        DrawCircleV({ center.x + radius * 0.08f, center.y + radius * 0.16f }, radius * 1.02f, a({ 0, 0, 0, 90 }));
        DrawCircleV({ center.x, center.y + radius * 0.1f }, radius, a({ 150, 96, 18, 255 }));
        // Гадна хүрээ ба гүдгэр гадаргуу: дээрээс цайвар, доош гүн алтан
        DrawCircleV(center, radius, a({ 214, 150, 30, 255 }));
        DrawCircleGradient((int)center.x, (int)(center.y - radius * 0.15f), radius * 0.86f, a({ 255, 232, 130, 255 }),
                           a({ 236, 170, 40, 255 }));
        DrawRing(center, radius * 0.66f, radius * 0.74f, 0.0f, 360.0f, 32, a({ 190, 124, 20, 220 }));
        // ₮ тэмдэг (товгор: бараан сүүдэр, цайвар нүүр)
        const int size = (int)(radius * 1.15f);
        const char* sign = "\xE2\x82\xAE";
        const int width = MeasureBold(sign, size);
        TextBold(sign, (int)(center.x - (float)width * 0.5f) + 1, (int)(center.y - (float)size * 0.52f) + 1, size,
                 a({ 150, 96, 18, 255 }));
        TextBold(sign, (int)(center.x - (float)width * 0.5f), (int)(center.y - (float)size * 0.52f), size,
                 a({ 255, 244, 200, 255 }));
        // Гялбаа: зүүн дээд хэсэгт цагаан нум
        DrawRing(center, radius * 0.78f, radius * 0.88f, 200.0f, 250.0f, 12, a({ 255, 255, 255, 170 }));
    }
}
