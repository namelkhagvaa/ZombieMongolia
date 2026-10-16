#pragma once

#include "raylib.h"

// Тоглоомын UI-ийн нэгдсэн хэв маяг: бөөрөнхий булантай хагас тунгалаг "шил" самбар, товчны тэмдэг,
// бөөрөнхий явцын зурвас. HUD, мэдрэгчтэй товчлуур, газрын зургийн дэлгэц бүгд эндээс өнгө, хэлбэрээ авна.
namespace Ui
{
    // --- Өнгө ---
    inline constexpr Color kPanel = { 12, 16, 22, 178 };       // самбарын дэвсгэр
    inline constexpr Color kPanelSoft = { 12, 16, 22, 120 };
    inline constexpr Color kBorder = { 255, 255, 255, 34 };    // самбарын нимгэн хүрээ
    inline constexpr Color kText = { 242, 240, 234, 255 };
    inline constexpr Color kTextDim = { 178, 184, 192, 255 };
    inline constexpr Color kAccent = { 255, 192, 64, 255 };    // алтан шар: идэвхтэй, онцлох
    inline constexpr Color kAccentDark = { 120, 86, 20, 200 };
    inline constexpr Color kDanger = { 255, 92, 76, 255 };
    inline constexpr Color kGood = { 112, 214, 132, 255 };
    inline constexpr Color kInfo = { 96, 196, 236, 255 };

    // Шил самбар: бөөрөнхий булан, нимгэн цайвар хүрээ. accent > 0 бол зүүн ирмэгт өнгөт зураас.
    void Panel(Rectangle area, Color fill = kPanel, float radius = 10.0f);
    void PanelAccent(Rectangle area, Color accent, Color fill = kPanel, float radius = 10.0f);
    void Outline(Rectangle area, Color color, float thickness = 2.0f, float radius = 10.0f);

    // Бөөрөнхий явцын зурвас (амь, цэнэглэлт). ratio 0..1.
    void Bar(Rectangle area, float ratio, Color fill, Color back = { 255, 255, 255, 30 });

    // Товчны тэмдэг: "F", "Q", "1" гэх мэт. Зүүн дээд булангаас нь зурна; өргөнийг буцаана.
    int KeyBadge(const char* key, int x, int y, int size, Color color = kText);

    // Бичвэр: энгийн, тод (гарчиг, тоо). Сүүдэргүй (самбар дээр); Shadow хувилбар нь ил дэвсгэр дээр.
    void Text(const char* text, int x, int y, int size, Color color);
    void TextBold(const char* text, int x, int y, int size, Color color);
    void TextShadow(const char* text, int x, int y, int size, Color color, bool bold = false);
    int Measure(const char* text, int size);
    int MeasureBold(const char* text, int size);

    // Жижиг "чип": самбар доторх товч мэдээлэл (зоос, устгасан тоо). Өргөнийг буцаана.
    // dot: зүүн талын өнгөт цэг (alpha = 0 бол зурахгүй). right = true үед x нь баруун ирмэг.
    int Chip(const char* text, int x, int y, Color textColor, Color dot, bool right = false);

    // Зэвсгийн дүрс (баруун тийш харсан бараан сүүдэр биш, цул өнгөтэй). type нь WeaponType-ийн дугаар:
    // 0 хутга, 1 гар буу, 2 автомат, 3 үрлэн буу, 4 гранат, 5 дурантай буу, 6 пуужин харвагч.
    void WeaponIcon(int type, Rectangle box, Color color);

    // Алтан зоос: хүрээ, гүдгэр гадаргуу, дотор цагираг, ₮ тэмдэг, гялбаа. alpha 0..1.
    void Coin(Vector2 center, float radius, float alpha = 1.0f);

    // --- Зурган иконууд (assets/huds, 128x128 PNG): анх зурахад ачаална ---
    enum class Icon
    {
        TimeDay, TimeSunset, TimeNight, Compass, KillCount, Currency, Team, Timer,
        Zombie, ZombieElite, SpeedBoost, Location, Mission, MissionComplete, Warning, Reward,
        Health, Armor, Ammo, AmmoLarge, AmmoClip, WeaponAutomatic, WeaponPistol, WeaponSniper, WeaponMelee, Grenade,
        Settings, Sound, Fullscreen, Pause, Play, Close, Back, Menu,
        MapPlayer, MapZombie, MapElite, MapAlly, MapMission, MapBase, MapVehicle, MapHorse, MapSupply, MapTower,
        Count
    };
    bool HasIcon(Icon icon);
    // box-д (харьцаагаа хадгалан, төвд нь) зурна. rotation нь төвөө тойрсон эргэлт (градус, цагийн зүүний дагуу).
    // Файл байхгүй бол юу ч зурахгүй, false буцаана.
    bool DrawIcon(Icon icon, Rectangle box, Color tint = WHITE, float rotation = 0.0f);
    // Chip шиг, гэхдээ өнгөт цэгийн оронд икон. Өргөнийг буцаана. Икон байхгүй бол dot-той энгийн Chip.
    int IconChip(Icon icon, const char* text, int x, int y, Color textColor, Color dot, bool right = false);
}
