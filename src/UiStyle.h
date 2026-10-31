#pragma once

#include "raylib.h"

// Тоглоомын UI-ийн нэгдсэн дизайн систем (2026): бараан хөх цэргийн командын самбар — нимгэн хүрээ, бага
// бөөрөнхий (тактикийн) булан, алтан / улбар шар онцлол, хяналттай гэрэлтэлт. HUD, мэдрэгчтэй товчлуур, газрын
// зургийн дэлгэц, цонх бүгд өнгө, хэлбэрээ эндээс авна (дэлгэц бүр өнгөө өөрөө бичихгүй).
namespace Ui
{
    // --- Өнгөний токенууд ---
    inline constexpr Color kBgDeep = { 8, 13, 22, 255 };        // #080D16 үндсэн дэвсгэр
    inline constexpr Color kBg = { 16, 24, 36, 255 };           // #101824 хоёрдогч дэвсгэр
    inline constexpr Color kPanelSolid = { 20, 30, 43, 255 };   // #141E2B самбар
    inline constexpr Color kElevated = { 26, 38, 53, 255 };     // #1A2635 өргөгдсөн (карт, мөр)
    inline constexpr Color kPanel = { 20, 30, 43, 214 };        // HUD-ын хагас тунгалаг самбар
    inline constexpr Color kPanelSoft = { 20, 30, 43, 150 };
    inline constexpr Color kBorder = { 255, 255, 255, 26 };     // rgba(255,255,255,0.10)
    inline constexpr Color kBorderStrong = { 255, 255, 255, 52 };
    inline constexpr Color kText = { 244, 246, 250, 255 };      // #F4F6FA
    inline constexpr Color kTextDim = { 154, 167, 183, 255 };   // #9AA7B7
    inline constexpr Color kAccent = { 255, 181, 27, 255 };     // #FFB51B алт: үндсэн үйлдэл, идэвхтэй
    inline constexpr Color kOrange = { 244, 123, 32, 255 };     // #F47B20 улбар шар: хоёрдогч онцлол
    inline constexpr Color kAccentDark = { 96, 64, 10, 215 };   // сонгогдсон мөрийн дэвсгэр
    inline constexpr Color kDanger = { 240, 68, 68, 255 };      // #F04444
    inline constexpr Color kGood = { 57, 200, 121, 255 };       // #39C879
    inline constexpr Color kInfo = { 72, 190, 236, 255 };       // мэдээлэл, хамтын тоглолт
    inline constexpr float kRadius = 4.0f;                      // бүх самбар, товчны булан (тактикийн, бага бөөрөнхий)

    // Шил самбар: бөөрөнхий булан, нимгэн цайвар хүрээ. accent > 0 бол зүүн ирмэгт өнгөт зураас.
    void Panel(Rectangle area, Color fill = kPanel, float radius = 10.0f);
    void PanelAccent(Rectangle area, Color accent, Color fill = kPanel, float radius = 10.0f);
    void Outline(Rectangle area, Color color, float thickness = 2.0f, float radius = 10.0f);

    // Явцын зурвас (амь, цэнэглэлт, XP): хурц булантай, 10%-ийн хуваалттай, дүүргэлтийн үзүүрт гэрэлтэлт. ratio 0..1.
    void Bar(Rectangle area, float ratio, Color fill, Color back = { 255, 255, 255, 30 });

    // Товч: үндсэн (алтан градиент, бараан бичиг, гэрэлтэлт), хоёрдогч (өргөгдсөн, хүрээтэй), аюултай, ил (хүрээгүй).
    enum class ButtonKind { Primary, Secondary, Danger, Ghost };
    void Button(Rectangle area, const char* label, ButtonKind kind = ButtonKind::Secondary, bool hover = false, int size = 18);
    // Энгийн дүүргэлт / хүрээ дизайн системийн булангаар (дэлгэцүүдийн шууд зурдаг товч, шошго, нүд): бараан саарал
    // өнгийг самбарын өнгө рүү шилжүүлнэ.
    void Fill(Rectangle area, Color color);
    void Stroke(Rectangle area, float thickness, Color color);
    // Цэсийн мөр / карт: энгийн (өргөгдсөн), hover / сонгогдсон (алтан градиент, зүүн ирмэгт алтан зураас, алтан
    // хүрээ), аюултай мөр hover үед улаан.
    void MenuRow(Rectangle area, bool hover, bool danger = false, bool selected = false);
    // Цонхны ард: дэлгэцийг бараан хөхөөр бүдгэрүүлж, захыг харанхуйлна (vignette).
    void Backdrop(float alpha = 0.72f);
    // Самбарын толгой: гарчиг (гарчгийн фонт), доор нь нимгэн алтан зураас; дэд гарчиг (бүдэг) сонголттой.
    void Header(float x, float y, float width, const char* title, const char* subtitle = nullptr, int size = 26);
    // Тактикийн булангийн хаалт (дөрвөн буланд L): онцлох самбар, сонгогдсон карт.
    void CornerBrackets(Rectangle area, Color color, float length = 12.0f, float thickness = 2.0f);
    // Гарчгийн фонт (Bahnschrift; байхгүй бол тод)
    void TextDisplay(const char* text, int x, int y, int size, Color color, float tracking = 0.04f);
    int MeasureDisplay(const char* text, int size, float tracking = 0.04f);

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
        UpgradeStrength, UpgradeDefense, UpgradeHealth, UpgradeInventory, UpgradeSpeed,
        Count
    };
    bool HasIcon(Icon icon);
    // box-д (харьцаагаа хадгалан, төвд нь) зурна. rotation нь төвөө тойрсон эргэлт (градус, цагийн зүүний дагуу).
    // Файл байхгүй бол юу ч зурахгүй, false буцаана.
    bool DrawIcon(Icon icon, Rectangle box, Color tint = WHITE, float rotation = 0.0f);
    // Chip шиг, гэхдээ өнгөт цэгийн оронд икон. Өргөнийг буцаана. Икон байхгүй бол dot-той энгийн Chip.
    int IconChip(Icon icon, const char* text, int x, int y, Color textColor, Color dot, bool right = false);
}
