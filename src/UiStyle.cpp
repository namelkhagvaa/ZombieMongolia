#include "UiStyle.h"

#include "UiFont.h"
#include "Input.h"

#include <algorithm>
#include <cmath>
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

    namespace
    {
        // Дэлгэц бүрийн өөрсдийн бичсэн бараан саарал дэвсгэрийг (жишээ нь { 12, 16, 22, 250 }) дизайн системийн
        // самбарын өнгө рүү шилжүүлнэ (тунгалаг байдлыг нь хадгална): бүх цонх нэг өнгөтэй болно.
        Color Harmonize(Color fill)
        {
            const int top = std::max({ (int)fill.r, (int)fill.g, (int)fill.b });
            const int bottom = std::min({ (int)fill.r, (int)fill.g, (int)fill.b });
            if (top < 48 && top - bottom < 20) return { kPanelSolid.r, kPanelSolid.g, kPanelSolid.b, fill.a };
            return fill;
        }

        float CapRadius(float radius) { return std::min(radius, kRadius); }
    }

    void Panel(Rectangle area, Color fill, float radius)
    {
        fill = Harmonize(fill);
        const float r = Roundness(area, CapRadius(radius));
        // Зөөлөн сүүдэр (доош бага зэрэг)
        if (area.width > 60.0f && area.height > 30.0f)
            DrawRectangleRounded({ area.x, area.y + 3.0f, area.width, area.height }, r, 6, Fade(BLACK, 0.22f * (float)fill.a / 255.0f));
        DrawRectangleRounded(area, r, 6, fill);
        // Дээд талын нимгэн гэрэлтэлт (шил): дээрээс доош бүдгэрнэ
        const float glow = std::min(area.height * 0.45f, 40.0f);
        DrawRectangleGradientV((int)area.x + 1, (int)area.y + 1, (int)area.width - 2, (int)glow, Fade(WHITE, 0.05f), Fade(WHITE, 0.0f));
        DrawRectangleRoundedLinesEx(area, r, 6, 1.0f, kBorder);
        // Том самбар: булангийн тактикийн тэмдэг
        if (area.width >= 220.0f && area.height >= 120.0f) CornerBrackets(area, Fade(WHITE, 0.16f), 9.0f, 1.5f);
    }

    void PanelAccent(Rectangle area, Color accent, Color fill, float radius)
    {
        Panel(area, fill, radius);
        // Зүүн ирмэгийн өнгөт зураас (ирмэгтээ наалдсан) ба түүний бүдэг гэрэлтэлт
        DrawRectangleGradientH((int)area.x + 3, (int)area.y + 1, 22, (int)area.height - 2, Fade(accent, 0.14f), Fade(accent, 0.0f));
        DrawRectangleRec({ area.x, area.y + 3.0f, 3.0f, area.height - 6.0f }, accent);
    }

    void Outline(Rectangle area, Color color, float thickness, float radius)
    {
        DrawRectangleRoundedLinesEx(area, Roundness(area, CapRadius(radius)), 6, thickness, color);
    }

    void Bar(Rectangle area, float ratio, Color fill, Color back)
    {
        ratio = std::clamp(ratio, 0.0f, 1.0f);
        const float r = Roundness(area, 1.5f);
        DrawRectangleRounded(area, r, 4, back);
        if (ratio > 0.0f)
        {
            Rectangle filled = area;
            filled.width = std::max(2.0f, area.width * ratio);
            // Градиент дүүргэлт: зүүнээсээ бага зэрэг бараан, үзүүртээ тод
            const Color dark = { (unsigned char)(fill.r * 0.72f), (unsigned char)(fill.g * 0.72f), (unsigned char)(fill.b * 0.72f), fill.a };
            DrawRectangleGradientH((int)filled.x, (int)filled.y, (int)std::ceil(filled.width), (int)filled.height, dark, fill);
            // Дээд талын гялбаа
            DrawRectangleRec({ filled.x, filled.y, filled.width, std::max(1.0f, filled.height * 0.32f) }, Fade(WHITE, 0.16f));
            // Үзүүрийн гэрэлтэлт
            if (ratio < 0.999f && area.height >= 4.0f)
                DrawRectangleRec({ filled.x + filled.width - 2.0f, filled.y - 1.0f, 2.0f, filled.height + 2.0f }, Fade(WHITE, 0.55f));
        }
        // 10%-ийн хуваалт (урт зурвас)
        if (area.width >= 90.0f && area.height >= 5.0f)
        {
            for (int i = 1; i < 10; ++i)
            {
                const float x = area.x + area.width * (float)i / 10.0f;
                DrawRectangleRec({ std::floor(x), area.y, 1.0f, area.height }, Fade(kBgDeep, 0.45f));
            }
        }
    }

    int KeyBadge(const char* key, int x, int y, int size, Color color)
    {
        // Гарны товч: өргөгдсөн дэвсгэр, доод ирмэг зузаан (дарагддаг товчны хэлбэр)
        const int textWidth = MeasureBold(key, size);
        const int width = std::max(textWidth + 12, size + 8);
        const Rectangle box = { (float)x, (float)y, (float)width, (float)size + 8.0f };
        const float r = Roundness(box, 3.0f);
        DrawRectangleRounded({ box.x, box.y + 2.0f, box.width, box.height }, r, 4, Fade(BLACK, 0.45f));
        DrawRectangleRounded(box, r, 4, kElevated);
        DrawRectangleRoundedLinesEx(box, r, 4, 1.0f, Fade(color, 0.45f));
        TextBold(key, x + (width - textWidth) / 2, y + 4, size, color);
        return width;
    }

    void Button(Rectangle area, const char* label, ButtonKind kind, bool hover, int size)
    {
        const float r = Roundness(area, kRadius);
        const int textWidth = MeasureDisplay(label, size);
        const int tx = (int)(area.x + (area.width - (float)textWidth) * 0.5f), ty = (int)(area.y + (area.height - (float)size) * 0.5f);
        switch (kind)
        {
        case ButtonKind::Primary:
        {
            // Алтан градиент, гэрэлтэлттэй; hover үед илүү тод
            if (hover) DrawRectangleRounded({ area.x - 3.0f, area.y - 3.0f, area.width + 6.0f, area.height + 6.0f }, r, 6, Fade(kAccent, 0.18f));
            DrawRectangleRounded(area, r, 6, kOrange);
            DrawRectangleGradientV((int)area.x + 1, (int)area.y + 1, (int)area.width - 2, (int)area.height - 2,
                                   hover ? Color{ 255, 206, 84, 255 } : kAccent, Fade(kOrange, 0.0f));
            DrawRectangleRec({ area.x + 2.0f, area.y + 1.0f, area.width - 4.0f, 1.0f }, Fade(WHITE, 0.45f));
            TextDisplay(label, tx, ty, size, { 26, 18, 4, 255 });
            break;
        }
        case ButtonKind::Danger:
            DrawRectangleRounded(area, r, 6, hover ? Color{ 120, 30, 30, 235 } : Color{ 70, 22, 26, 225 });
            DrawRectangleRoundedLinesEx(area, r, 6, 1.0f, Fade(kDanger, hover ? 0.9f : 0.6f));
            TextDisplay(label, tx, ty, size, hover ? kText : kDanger);
            break;
        case ButtonKind::Ghost:
            if (hover) DrawRectangleRounded(area, r, 6, Fade(WHITE, 0.06f));
            TextDisplay(label, tx, ty, size, hover ? kText : kTextDim);
            break;
        case ButtonKind::Secondary:
        default:
            DrawRectangleRounded(area, r, 6, hover ? Color{ 34, 50, 70, 240 } : kElevated);
            DrawRectangleGradientV((int)area.x + 1, (int)area.y + 1, (int)area.width - 2, (int)(area.height * 0.5f), Fade(WHITE, 0.05f), Fade(WHITE, 0.0f));
            DrawRectangleRoundedLinesEx(area, r, 6, 1.0f, hover ? Fade(kAccent, 0.8f) : kBorderStrong);
            TextDisplay(label, tx, ty, size, hover ? kAccent : kText);
            break;
        }
    }

    void Fill(Rectangle area, Color color)
    {
        DrawRectangleRounded(area, Roundness(area, kRadius), 6, Harmonize(color));
    }

    void Stroke(Rectangle area, float thickness, Color color)
    {
        DrawRectangleRoundedLinesEx(area, Roundness(area, kRadius), 6, thickness, color);
    }

    void MenuRow(Rectangle area, bool hover, bool danger, bool selected)
    {
        const float r = Roundness(area, kRadius);
        const bool lit = hover || selected;
        const Color tone = danger ? kDanger : kAccent;
        DrawRectangleRounded(area, r, 6, lit ? Color{ 30, 42, 58, 240 } : Fade(kElevated, 0.82f));
        if (lit)
        {
            DrawRectangleGradientH((int)area.x + 1, (int)area.y + 1, (int)(area.width * 0.7f), (int)area.height - 2, Fade(tone, 0.22f), Fade(tone, 0.0f));
            DrawRectangleRec({ area.x, area.y + 2.0f, 3.0f, area.height - 4.0f }, tone);
        }
        DrawRectangleRoundedLinesEx(area, r, 6, 1.0f, lit ? Fade(tone, 0.75f) : kBorder);
    }

    void Backdrop(float alpha)
    {
        const int width = ViewWidth(), height = ViewHeight();
        DrawRectangle(0, 0, width, height, Fade(kBgDeep, alpha));
        // Захын харанхуйлал
        const int edge = height / 4;
        DrawRectangleGradientV(0, 0, width, edge, Fade(BLACK, 0.35f * alpha), Fade(BLACK, 0.0f));
        DrawRectangleGradientV(0, height - edge, width, edge, Fade(BLACK, 0.0f), Fade(BLACK, 0.35f * alpha));
    }

    void Header(float x, float y, float width, const char* title, const char* subtitle, int size)
    {
        TextDisplay(title, (int)x, (int)y, size, kText, 0.06f);
        const float line = y + (float)size + 8.0f;
        DrawRectangleRec({ x, line, width, 1.0f }, kBorder);
        DrawRectangleRec({ x, line - 1.0f, 46.0f, 3.0f }, kAccent);
        if (subtitle && subtitle[0]) Text(subtitle, (int)x, (int)(line + 9.0f), 15, kTextDim);
    }

    void CornerBrackets(Rectangle area, Color color, float length, float thickness)
    {
        const float l = std::min(length, std::min(area.width, area.height) * 0.3f);
        const float x0 = area.x, y0 = area.y, x1 = area.x + area.width, y1 = area.y + area.height;
        DrawRectangleRec({ x0, y0, l, thickness }, color);
        DrawRectangleRec({ x0, y0, thickness, l }, color);
        DrawRectangleRec({ x1 - l, y0, l, thickness }, color);
        DrawRectangleRec({ x1 - thickness, y0, thickness, l }, color);
        DrawRectangleRec({ x0, y1 - thickness, l, thickness }, color);
        DrawRectangleRec({ x0, y1 - l, thickness, l }, color);
        DrawRectangleRec({ x1 - l, y1 - thickness, l, thickness }, color);
        DrawRectangleRec({ x1 - thickness, y1 - l, thickness, l }, color);
    }

    void TextDisplay(const char* text, int x, int y, int size, Color color, float tracking)
    {
        UiTextDisplay(text, x, y, size, color, tracking);
    }

    int MeasureDisplay(const char* text, int size, float tracking)
    {
        return UiMeasureDisplay(text, size, tracking);
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
        Panel({ (float)x, (float)y, (float)width, 32.0f }, kPanel, kRadius);
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
        Panel({ (float)x, (float)y, (float)width, 32.0f }, kPanel, kRadius);
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
