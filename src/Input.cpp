#include "Input.h"

#include "Platform.h"
#include "UiFont.h"
#include "UiStyle.h"
#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
    // ------------------------------------------------------------ Харагдац
    RenderTexture2D target = {};
    bool scaled = false;
    int viewWidth = 0, viewHeight = 0;

    // Дэлгэцийн цэгийг харагдацын координат руу
    Vector2 ToView(Vector2 screen)
    {
        if (!scaled)
        {
            return screen;
        }
        return { screen.x * (float)viewWidth / (float)GetScreenWidth(), screen.y * (float)viewHeight / (float)GetScreenHeight() };
    }

    // ------------------------------------------------------------ Товчлуурууд
    const int kKeyCount = 512;
    bool touchMode = false;
    bool virtualDown[kKeyCount] = {};
    bool virtualPressed[kKeyCount] = {};
    bool fireDown = false, firePressed = false;
    bool aimLatched = false;
    bool pointerDown = false, pointerPressed = false;
    Vector2 pointer = { 0.0f, 0.0f };
    Vector2 look = { 0.0f, 0.0f };
    Vector2 move = { 0.0f, 0.0f };
    float wheel = 0.0f;

    const float kLookScale = 1.7f;      // хуруугаар чирэх нь хулганаас удаан тул үржүүлнэ
    const float kStickRadius = 92.0f;   // хөдлөх хүрдний хүрээ, харагдацын пикселээр
    const float kStickGrab = 1.7f;      // хүрдний төвөөс ийм (радиусын) зайд буусан хуруу хүрдийг барина
    // Гүйлтийн түгжээ (PUBG шиг): хүрдийг дээш нь ийм (радиусын) зайд, тэмдэг хүртэл чирч тавибал
    // хуруугүй ч урагш гүйсээр байна. Хүрдэнд дахин хүрэхэд түгжээ тайлагдана.
    const float kLockDistance = 1.75f;
    bool lockArmed = false, autoRun = false, stickWasActive = false;
    // Хүрдийг хажуу тийш хазайлгахад камер тэр зүг рүү эргэнэ: нэг хуруугаар явж, зүглэж болно.
    const float kStickTurn = 720.0f;    // бүрэн хазайлтад секундэд эргэх хэмжээ, "хулганы пиксел"-ээр (~125 градус/сек)
    const float kStickStrafe = 0.35f;   // хүрдээр хажуу тийш алхах хурдны хувь
    bool stickTurns = false;
    float pinch = 1.0f, pinchDistance = 0.0f; // хоёр хурууны чимхэлт: харьцаа, өмнөх frame-ийн зай
    Vector2 pinchCenter = { 0.0f, 0.0f };            // энэ frame-д хүрд камерыг эргүүлж байгаа эсэх

    enum class Kind
    {
        Key,   // дарахад нэг удаа "товч дарагдсан"
        Hold,  // дарж байх хугацаанд "товч дарагдсан хэвээр"
        Fire,  // хулганы зүүн товч; хурууг чирэхэд камер мөн эргэнэ
        Aim,   // онилгоог асааж унтраана (хулганы баруун товч)
        Wheel, // газрын зураг томруулах / жижигрүүлэх
        Menu   // ⚙ тохиргооны задрах цэсийг нээж хаана
    };

    enum Glyph
    {
        kGlyphNone,
        kGlyphBackpack, // үүргэвч (зурган икон байхгүй тул шугамаар)
        kGlyphHidden,   // зурахгүй: доор нь HUD өөрөө зурсан (багийн хөрөг)
        kGlyphVehicle,  // унааны дүрс, доор нь жижиг бичвэр (унаанд суух / буух)
    };

    struct Button
    {
        Rectangle area;
        const char* label;
        Kind kind;
        int key;          // Key / Hold: raylib-ийн товчны код; Wheel: +1 / -1
        bool round;
        bool highlighted; // идэвхтэй төлөв (сонгогдсон зэвсэг, асаалттай онилгоо)
        bool dim;         // үхсэн гишүүн гэх мэт
        int icon = -1;    // >= 0 бол бичвэрийн оронд зэвсгийн дүрс (WeaponType)
        int check = -1;   // >= 0 бол чагттай товч: 1 = чагтлагдсан, 0 = үгүй
        int uiIcon = -1;  // >= 0 бол бичвэрийн зүүн талд Ui::Icon (бичвэр хоосон бол зөвхөн икон)
        int glyph = kGlyphNone;
        bool opaque = false; // задрах цэсний мөр: бусад товчны дээр тод харагдана
    };
    bool settingsOpen = false; // ⚙ задрах цэс (авто онилгоо, камер, энд бай)
    bool settingsRows = false; // Add() одоо задрах цэсний мөр нэмж байна
    std::vector<Button> buttons;

    enum class Role
    {
        None,
        Stick,
        Look,
        Button,
        Pointer
    };
    struct Finger
    {
        int id = -1;
        Vector2 start = {}, position = {};
        Role role = Role::None;
        int button = -1;
        bool seen = false;
        bool fresh = false; // энэ frame-д буусан
    };
    Finger fingers[10];
    Vector2 stickCenter = { 150.0f, 0.0f };
    bool stickActive = false;

    Rectangle Circle(float cx, float cy, float radius)
    {
        return { cx - radius, cy - radius, radius * 2.0f, radius * 2.0f };
    }

    void Add(Rectangle area, const char* label, Kind kind, int key, bool round = true, bool highlighted = false,
             bool dim = false)
    {
        buttons.push_back({ area, label, kind, key, round, highlighted, dim, -1, -1, -1, kGlyphNone, settingsRows });
    }

    // Нээлттэй дэлгэцэд тохирох товчлууруудын байрлалыг бүтээнэ (харагдацын координатаар).
    void BuildButtons(const TouchState& state)
    {
        buttons.clear();
        const float w = (float)ViewWidth(), h = (float)ViewHeight();

        switch (state.screen)
        {
        case TouchScreen::Gameplay:
        {
            // --- Баруун доод: тулааны товчлуурууд ---
            if (!state.driving)
            {
                // Явган (PUBG шиг): том БУУДАХ голд, эргэн тойронд нь ҮСРЭХ / СУУХ / ХЭВТЭХ / ЦЭНЭГЛЭХ / ОНИЛОХ
                Add(Circle(w - 180.0f, h - 262.0f, 74.0f), "БУУДАХ", Kind::Fire, 0);
                Add(Circle(w - 66.0f, h - 410.0f, 46.0f), state.sniper ? "ДУРАН" : "ОНИЛОХ", Kind::Aim, 0, true, aimLatched);
                Add(Circle(w - 74.0f, h - 122.0f, 50.0f), state.stance != 0 ? "БОСОХ" : "ҮСРЭХ", Kind::Hold, KEY_SPACE);
                Add(Circle(w - 192.0f, h - 80.0f, 44.0f), "СУУХ", Kind::Key, KEY_C, true, state.stance == 1);
                Add(Circle(w - 300.0f, h - 62.0f, 40.0f), "ХЭВТЭХ", Kind::Key, KEY_Z, true, state.stance == 2);
                Add(Circle(w - 336.0f, h - 312.0f, 42.0f), "ЦЭНЭГЛЭХ", Kind::Key, KEY_R);
                // Зүүн гарын эрхий хуруунд: хүрдний дээд зүүн талд хоёр дахь буудах товч
                Add(Circle(70.0f, std::max(h - 400.0f, 215.0f), 52.0f), "БУУДАХ", Kind::Fire, 0);
            }
            else
            {
                // Жолоодох үед: хий (БУУДАХ-ын байранд), тоормос, цэнэглэх
                Add(Circle(w - 132.0f, h - 166.0f, 73.0f), "", Kind::Fire, 0);
                Add(Circle(w - 280.0f, h - 96.0f, 52.0f), state.sniper ? "ДУРАН" : "ОНИЛОХ", Kind::Aim, 0, true, aimLatched);
                Add(Circle(w - 82.0f, h - 326.0f, 50.0f), "ТООРМОС", Kind::Hold, KEY_SPACE);
                Add(Circle(w - 262.0f, h - 240.0f, 45.0f), "ЦЭНЭГЛЭХ", Kind::Key, KEY_R);
            }

            // --- Нөхцөлт үйлдлүүд (зөвхөн боломжтой үед): дэлгэцийн голоос баруун тийш, PUBG-ийн "авах" шиг ---
            // Баруун талын товчнуудын (ЦЭНЭГЛЭХ-ийн зүүн ирмэг ~w-378) зүүн талд: 16:9 дэлгэцэд давхцдаг байсан
            const float actionX = std::min(w * 0.5f + 90.0f, w - 620.0f);
            float actionY = h * 0.5f - 18.0f;
            if (state.useLabel && state.useVehicle)
            {
                // Унаанд суух / буух: жижиг дугуй дүрст товч баруун талын товчнуудын дээгүүр (дэлгэцийн голд биш:
                // унаа, бай халхлагдахгүй). Явган ба жолоодох үеийн товчнуудтай давхцахгүй байрлал.
                Add(Circle(w - 270.0f, h - 440.0f, 34.0f), state.useLabel, Kind::Key, KEY_F, true, true);
                buttons.back().glyph = kGlyphVehicle;
            }
            else if (state.useLabel)
            {
                Add({ actionX, actionY, 220.0f, 58.0f }, state.useLabel, Kind::Key, KEY_F, false, true);
                actionY += 68.0f;
            }
            if (state.actLabel)
            {
                Add({ actionX, actionY, 220.0f, 58.0f }, state.actLabel, Kind::Key, KEY_E, false, true);
            }

            // --- Доод голд: зэвсгүүд ---
            const float slotW = 104.0f, slotH = 62.0f, gap = 8.0f;
            const float total = (float)state.weaponCount * slotW + (float)(state.weaponCount - 1) * gap;
            for (int i = 0; i < state.weaponCount; ++i)
            {
                Add({ (w - total) * 0.5f + (float)i * (slotW + gap), h - slotH - 10.0f, slotW, slotH }, state.weaponNames[i],
                    Kind::Key, KEY_ONE + i, false, i == state.weaponCurrent);
                buttons.back().icon = state.weaponTypes[i];
            }

            // --- Баруун дээд: жижиг газрын зураг (товшиход бүтэн зураг), зүүн талд нь гурван дүрст товч ---
            // ⚙ тохиргоо (задрах цэс), үүргэвч = даалгаврын журнал, ≡ тоглоомын цэс (ESC-тэй адил).
            const float map = (float)state.minimapSize;
            Add({ w - map - 16.0f, 16.0f, map, map }, "", Kind::Key, KEY_M, false);
            const float iconSide = 46.0f, iconGap = 8.0f;
            const float iconsRight = w - map - 16.0f - 6.0f - 12.0f; // газрын зургийн шилэн хүрээний зүүн талд
            const float menuX = iconsRight - iconSide;
            const float journalX = menuX - iconGap - iconSide;
            const float gearX = journalX - iconGap - iconSide;
            Add({ gearX, 16.0f, iconSide, iconSide }, "", Kind::Menu, 0, false, settingsOpen);
            buttons.back().uiIcon = (int)Ui::Icon::Settings;
            Add({ journalX, 16.0f, iconSide, iconSide }, "", Kind::Key, KEY_Q, false);
            buttons.back().glyph = kGlyphBackpack;
            Add({ menuX, 16.0f, iconSide, iconSide }, "", Kind::Key, KEY_ESCAPE, false);
            buttons.back().uiIcon = (int)Ui::Icon::Menu;

            // Баг: зүүн дээд буланд HUD-ийн зурсан хөрөг бүрийг товшиж тэр гишүүнийг удирдана
            for (int i = 0; i < 3; ++i)
            {
                if (state.teamTiles[i].width <= 0.0f) continue;
                Add(state.teamTiles[i], "", Kind::Key, KEY_F1 + i, false);
                buttons.back().glyph = kGlyphHidden;
            }

            if (settingsOpen)
            {
                // Задрах цэс: дүрст товчнуудын доор, баруун ирмэгээ тэдэнтэй тэгшилсэн нэг багана
                const float panelW = 230.0f, rowH = 38.0f, rowGap = 4.0f;
                settingsRows = true;
                const float x = iconsRight - panelW;
                float y = 16.0f + iconSide + 10.0f;
                // Автомат онилгоо: чагтлагдсан бол хараа ойрын зомби руу өөрөө татагдана, үгүй бол өөрөө онилно.
                Add({ x, y, panelW, rowH }, "АВТО ОНИЛГОО", Kind::Key, KEY_T, false, state.aimAssist);
                buttons.back().check = state.aimAssist ? 1 : 0;
                y += rowH + rowGap;
                // Камерын горим: 1-р этгээд (зөвхөн гар, буу) / 3-р этгээд (бүтэн бие)
                Add({ x, y, panelW, rowH }, state.firstPerson ? "КАМЕР: 1-Р" : "КАМЕР: 3-Р", Kind::Key, KEY_V, false,
                    state.firstPerson);
                y += rowH + rowGap;
                if (state.showHold)
                {
                    // Удирдаж буй гишүүнийг байранд нь үлдээнэ (дараа нь өөр гишүүн рүү шилжинэ).
                    Add({ x, y, panelW, rowH }, "ЭНД БАЙ", Kind::Key, KEY_H, false, state.holdMarked);
                }
                settingsRows = false;
            }

            if (state.joinRequest)
            {
                // Хамтарсан тоглолтын хүсэлт (дэлгэцийн дээд голын картын доор)
                Add({ w * 0.5f - 230.0f, 150.0f, 220.0f, 50.0f }, "ЗӨВШӨӨРӨХ", Kind::Key, KEY_Y, false, true);
                Add({ w * 0.5f + 10.0f, 150.0f, 220.0f, 50.0f }, "ТАТГАЛЗАХ", Kind::Key, KEY_N, false);
            }
            break;
        }

        case TouchScreen::FullMap:
            Add({ w - 96.0f, 20.0f, 76.0f, 60.0f }, "X", Kind::Key, KEY_M, false, true);
            buttons.back().uiIcon = (int)Ui::Icon::Close;
            Add({ w - 96.0f, 100.0f, 76.0f, 60.0f }, "+", Kind::Wheel, 1, false);
            Add({ w - 96.0f, 170.0f, 76.0f, 60.0f }, "–", Kind::Wheel, -1, false);
            Add({ w - 96.0f, 250.0f, 76.0f, 60.0f }, "ТӨВ", Kind::Key, KEY_C, false);
            break;

        case TouchScreen::CampaignMap:
        {
            // Дэлгүүрийн 1..5 товч: дэлгэцийн доод ирмэгээр (аймгийг хоёр товшиж эхлүүлнэ)
            static const char* const numbers[] = { "1", "2", "3", "4", "5" };
            for (int i = 0; i < 5; ++i)
            {
                Add({ 20.0f + (float)i * 78.0f, h - 70.0f, 68.0f, 54.0f }, numbers[i], Kind::Key, KEY_ONE + i, false);
            }
            break;
        }

        case TouchScreen::Pause:
            break; // мөрийг товшино (Game шалгана)

        case TouchScreen::Shop:
            Add({ w - 196.0f, 20.0f, 176.0f, 60.0f }, "ХААХ", Kind::Key, KEY_E, false, true);
            break;

        case TouchScreen::Journal:
        case TouchScreen::Result:
            break; // хаана ч товшиход хаагдана / үргэлжилнэ
        }
    }

    int ButtonAt(Vector2 point)
    {
        // Сүүлд нэмсэн нь дээр зурагддаг тул араас нь шалгана.
        for (int i = (int)buttons.size() - 1; i >= 0; --i)
        {
            const Button& button = buttons[(size_t)i];
            if (button.round)
            {
                const Vector2 center = { button.area.x + button.area.width * 0.5f, button.area.y + button.area.height * 0.5f };
                if (Vector2Distance(center, point) <= button.area.width * 0.5f + 8.0f) return i;
            }
            else if (CheckCollisionPointRec(point, button.area))
            {
                return i;
            }
        }
        return -1;
    }

    void Press(int key)
    {
        if (key >= 0 && key < kKeyCount)
        {
            virtualPressed[key] = true;
            virtualDown[key] = true;
        }
    }

    // Хүрэлтүүдийг уншина. Компьютер дээрх туршилтад хулганы зүүн товч нэг хурууг орлоно.
    int ReadTouches(Vector2* positions, int* ids, int capacity)
    {
#if defined(__ANDROID__)
        const int count = std::min(GetTouchPointCount(), capacity);
        for (int i = 0; i < count; ++i)
        {
            positions[i] = ToView(GetTouchPosition(i));
            ids[i] = GetTouchPointId(i);
        }
        return count;
#elif defined(__EMSCRIPTEN__)
        float xy[2 * 10];
        const int count = WebReadTouches(xy, ids, std::min(capacity, 10));
        for (int i = 0; i < count; ++i)
        {
            positions[i] = ToView({ xy[2 * i], xy[2 * i + 1] });
        }
        return count;
#else
        if (capacity > 0 && IsMouseButtonDown(MOUSE_BUTTON_LEFT))
        {
            positions[0] = ToView(GetMousePosition());
            ids[0] = 0;
            return 1;
        }
        return 0;
#endif
    }
}

// ================================================================ Харагдац

void ViewInit(int targetHeight)
{
    viewWidth = GetScreenWidth();
    viewHeight = GetScreenHeight();
    scaled = false;
    // (Дэлгэц зорилтот өндрөөс намхан бол өөрийнх нь өндрөөр: утсан дээр дотоод зураг үргэлж хэрэглэгдэнэ,
    // учир нь түүний гүний buffer нь дэлгэцийнхээс нарийвчлал сайтай.)
    targetHeight = std::min(targetHeight, GetScreenHeight());
    if (targetHeight > 0 && GetScreenHeight() >= targetHeight)
    {
        viewHeight = targetHeight;
        viewWidth = (int)std::lround((double)targetHeight * GetScreenWidth() / GetScreenHeight());
        target = LoadRenderTexture(viewWidth, viewHeight);
        SetTextureFilter(target.texture, TEXTURE_FILTER_BILINEAR);
        scaled = target.id != 0;
        if (!scaled)
        {
            viewWidth = GetScreenWidth();
            viewHeight = GetScreenHeight();
        }
        TraceLog(LOG_INFO, "VIEW: rendering at %dx%d, screen %dx%d", viewWidth, viewHeight, GetScreenWidth(), GetScreenHeight());
    }
}

void ViewShutdown()
{
    if (scaled)
    {
        UnloadRenderTexture(target);
        scaled = false;
    }
}

int ViewWidth()
{
    return scaled ? viewWidth : GetScreenWidth();
}

int ViewHeight()
{
    return scaled ? viewHeight : GetScreenHeight();
}

void ViewBegin()
{
    if (scaled)
    {
        BeginTextureMode(target);
    }
}

void ViewEnd()
{
    if (scaled)
    {
        EndTextureMode();
        // Дотоод зураг босоо тэнхлэгээр урвуу хадгалагддаг тул эргүүлж сунгана.
        DrawTexturePro(target.texture, { 0.0f, 0.0f, (float)viewWidth, -(float)viewHeight },
                       { 0.0f, 0.0f, (float)GetScreenWidth(), (float)GetScreenHeight() }, { 0.0f, 0.0f }, 0.0f, WHITE);
    }
}

Image ViewCapture()
{
    if (scaled)
    {
        Image image = LoadImageFromTexture(target.texture);
        ImageFlipVertical(&image);
        ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8); // хагас тунгалаг зураглалын alpha-г хаяна
        return image;
    }
    // LoadImageFromScreen нь DPI масштабтай дэлгэц дээр буруу хэмжээ уншдаг тул шууд уншина.
    return { rlReadScreenPixels(GetRenderWidth(), GetRenderHeight()), GetRenderWidth(), GetRenderHeight(), 1,
             PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
}

Vector2 ViewWorldToScreen(Vector3 position, Camera3D camera)
{
    return GetWorldToScreenEx(position, camera, ViewWidth(), ViewHeight());
}

// ================================================================ Оролт

void InSetTouchMode(bool enabled)
{
    touchMode = enabled;
}

bool InTouchMode()
{
    return touchMode;
}

void InBeginFrame(const TouchState& state)
{
    std::fill(virtualPressed, virtualPressed + kKeyCount, false);
    std::fill(virtualDown, virtualDown + kKeyCount, false);
    firePressed = false;
    fireDown = false;
    pointerPressed = false;
    pointerDown = false;
    look = { 0.0f, 0.0f };
    move = { 0.0f, 0.0f };
    wheel = 0.0f;
    pinch = 1.0f;

#if defined(__EMSCRIPTEN__)
    // Хөтөч: дэлгэцэд хүрмэгц мэдрэгчтэй горимд шилжинэ (утас гэж танигдаагүй таблет г.м.)
    if (!touchMode && WebReadTouches(nullptr, nullptr, 0) > 0) touchMode = true;
#endif
    if (!touchMode)
    {
        pointer = ToView(GetMousePosition());
        return;
    }

    BuildButtons(state);
    const bool gameplay = state.screen == TouchScreen::Gameplay;
    if (!gameplay)
    {
        aimLatched = false;
    }
    // Хөдлөх хүрд нэг байрандаа (зүүн доод буланд) байна: хуруу хаана ч буусан тийш нүүхгүй.
    stickCenter = { 176.0f, (float)ViewHeight() - 202.0f };

    Vector2 positions[10] = {};
    int ids[10] = {};
    const int count = ReadTouches(positions, ids, 10);

    for (Finger& finger : fingers) finger.seen = false;
    stickActive = false;

    for (int i = 0; i < count; ++i)
    {
        // Өмнөх frame-ийн хуруу мөн үү?
        Finger* finger = nullptr;
        for (Finger& candidate : fingers)
        {
            if (candidate.id == ids[i]) finger = &candidate;
        }

        const Vector2 position = positions[i];
        if (!finger)
        {
            // Шинэ хүрэлт: юун дээр буусныг тодорхойлно.
            for (Finger& candidate : fingers)
            {
                if (candidate.id < 0)
                {
                    finger = &candidate;
                    break;
                }
            }
            if (!finger) continue;

            *finger = Finger{};
            finger->fresh = true;
            finger->id = ids[i];
            finger->start = finger->position = position;

            const int hit = ButtonAt(position);
            if (hit >= 0)
            {
                finger->role = Role::Button;
                finger->button = hit;
                const Button& button = buttons[(size_t)hit];
                switch (button.kind)
                {
                case Kind::Key:
                case Kind::Hold: Press(button.key); break;
                case Kind::Fire: firePressed = true; break;
                case Kind::Aim: aimLatched = !aimLatched; break;
                case Kind::Wheel: wheel += (float)button.key; break;
                case Kind::Menu: settingsOpen = !settingsOpen; break;
                }
            }
            else if (gameplay && Vector2Distance(position, stickCenter) < kStickRadius * kStickGrab)
            {
                autoRun = false; // хүрдэнд дахин хүрвэл гүйлтийн түгжээ тайлагдана
                finger->role = Role::Stick; // хүрд дээр (эсвэл ойролцоо) буусан хуруу хөдөлгөөнийг удирдана
            }
            else if (gameplay)
            {
                settingsOpen = false; // задрах цэсний гадна хүрвэл хаагдана
                finger->role = Role::Look;
            }
            else
            {
                finger->role = Role::Pointer;
                pointerPressed = true;
                if (state.screen == TouchScreen::Journal) Press(KEY_Q);
                if (state.screen == TouchScreen::Result) Press(KEY_ENTER);
            }
        }

        finger->seen = true;
        const Vector2 delta = Vector2Subtract(position, finger->position);
        finger->position = position;

        switch (finger->role)
        {
        case Role::Stick:
        {
            Vector2 offset = Vector2Subtract(position, stickCenter);
            // Хуруу хүрдний дээд талын түгжээний тэмдэг хүртэл хүрсэн эсэх (тавибал түгжигдэнэ)
            lockArmed = offset.y < -kStickRadius * kLockDistance + 26.0f && std::fabs(offset.x) < kStickRadius * 0.9f;
            const float length = Vector2Length(offset);
            if (length > kStickRadius)
            {
                offset = Vector2Scale(offset, kStickRadius / length);
            }
            move = { offset.x / kStickRadius, -offset.y / kStickRadius };
            if (Vector2Length(move) < 0.14f) move = { 0.0f, 0.0f }; // төвийн жижиг чөлөө
            stickActive = true;
            break;
        }
        case Role::Look:
            look = Vector2Add(look, Vector2Scale(delta, kLookScale));
            break;
        case Role::Button:
        {
            if (finger->button >= (int)buttons.size()) break;
            const Button& button = buttons[(size_t)finger->button];
            if (button.kind == Kind::Hold && button.key < kKeyCount) virtualDown[button.key] = true;
            if (button.kind == Kind::Fire)
            {
                fireDown = true;
                look = Vector2Add(look, Vector2Scale(delta, kLookScale)); // буудангаа онилно
            }
            break;
        }
        case Role::Pointer:
            pointerDown = true;
            // Хоёр эрхийгээр бичихэд: шинээр буусан хурууны байрлалыг хуучин (дарсаар буй) хуруу дарж болохгүй
            if (finger->fresh || !pointerPressed) pointer = position;
            finger->fresh = false;
            look = Vector2Add(look, delta); // газрын зургийг чирэхэд
            break;
        case Role::None:
            break;
        }
    }

    for (Finger& finger : fingers)
    {
        if (!finger.seen) finger.id = -1; // хуруу өргөгдсөн
    }

    // Хоёр хуруугаар чимхэх: зайн харьцаагаар томруулж, голын цэгийн шилжилтээр зөөнө
    const Finger* pair[2] = { nullptr, nullptr };
    int pointers = 0;
    for (const Finger& finger : fingers)
    {
        if (finger.id >= 0 && finger.role == Role::Pointer && pointers < 2) pair[pointers++] = &finger;
    }
    if (pointers == 2)
    {
        const float distance = Vector2Distance(pair[0]->position, pair[1]->position);
        const Vector2 middle = Vector2Scale(Vector2Add(pair[0]->position, pair[1]->position), 0.5f);
        if (pinchDistance > 0.0f && distance > 1.0f)
        {
            pinch = distance / pinchDistance;
            look = Vector2Subtract(middle, pinchCenter);
        }
        else
        {
            look = { 0.0f, 0.0f };
        }
        pinchDistance = distance;
        pinchCenter = middle;
    }
    else
    {
        pinchDistance = 0.0f;
    }

    // Гүйлтийн түгжээ: тэмдэг дээр хуруугаа өргөвөл түгжигдэнэ; явган биш бол тайлагдана
    if (stickWasActive && !stickActive && lockArmed) autoRun = true;
    if (!stickActive) lockArmed = false;
    if (!gameplay || state.driving) autoRun = lockArmed = false;
    stickWasActive = stickActive;
    if (lockArmed || (autoRun && !stickActive)) move = { 0.0f, 1.0f };

    // Явган үед хүрдийн хажуу тийш хазайлт камерыг эргүүлнэ (бага хазайлтад зөөлөн, их хазайлтад хурдан).
    // Унаа жолоодоход хүрд нь жолоо тул камерыг эргүүлэхгүй.
    stickTurns = gameplay && stickActive && !state.driving;
    if (stickTurns)
    {
        look.x += move.x * std::fabs(move.x) * kStickTurn * GetFrameTime();
    }
}

float InStrafeScale()
{
    return stickTurns ? kStickStrafe : 1.0f;
}

namespace
{
    bool muted = false;
}

void InSetMuted(bool value)
{
    muted = value;
}

bool InKeyDown(int key)
{
    if (muted) return false;
    return IsKeyDown(key) || (key >= 0 && key < kKeyCount && virtualDown[key]);
}

bool InKeyPressed(int key)
{
    if (muted) return false;
    return IsKeyPressed(key) || (key >= 0 && key < kKeyCount && virtualPressed[key]);
}

bool InMouseDown(int button)
{
    if (muted) return false;
    if (touchMode)
    {
        return button == MOUSE_BUTTON_LEFT ? (fireDown || pointerDown) : button == MOUSE_BUTTON_RIGHT ? aimLatched : false;
    }
    return IsMouseButtonDown(button);
}

bool InMousePressed(int button)
{
    if (muted) return false;
    if (touchMode)
    {
        return button == MOUSE_BUTTON_LEFT && (firePressed || pointerPressed);
    }
    return IsMouseButtonPressed(button);
}

Vector2 InLookDelta()
{
    if (muted) return { 0.0f, 0.0f };
    return touchMode ? look : GetMouseDelta();
}

float InWheel()
{
    if (muted) return 0.0f;
    return touchMode ? wheel : GetMouseWheelMove();
}

Vector2 InPointer()
{
    return pointer;
}

float InPinch()
{
    return muted ? 1.0f : pinch;
}

Vector2 InPinchCenter()
{
    return pinchCenter;
}

bool InPinching()
{
    return pinchDistance > 0.0f;
}

namespace
{
    Vector2 testMove = { 0.0f, 0.0f };
}

void InSetTestMove(Vector2 axis)
{
    testMove = axis;
}

Vector2 InMoveAxis()
{
    if (muted) return { 0.0f, 0.0f };
    Vector2 axis = { (float)IsKeyDown(KEY_D) - (float)IsKeyDown(KEY_A), (float)IsKeyDown(KEY_W) - (float)IsKeyDown(KEY_S) };
    axis = Vector2Add(Vector2Add(axis, move), testMove);
    const float length = Vector2Length(axis);
    return length > 1.0f ? Vector2Scale(axis, 1.0f / length) : axis;
}

bool InSprint()
{
    if (muted) return false;
    // Хүрдийг захад нь хүртэл урагш түлхвэл гүйнэ.
    return IsKeyDown(KEY_LEFT_SHIFT) || autoRun || lockArmed || (move.y > 0.5f && Vector2Length(move) > 0.93f);
}

void InDrawTouch(const TouchState& state)
{
    if (!touchMode)
    {
        return;
    }

    const Color fill = { 12, 16, 22, 140 };
    const Color fillActive = { 60, 46, 14, 190 };
    const Color edge = { 255, 255, 255, 60 };
    const Color accent = Ui::kAccent;
    const Color text = Ui::kText;

    for (const Button& button : buttons)
    {
        if (button.glyph == kGlyphHidden) continue;
        if (state.screen == TouchScreen::Gameplay && button.key == KEY_M && button.kind == Kind::Key && !button.label[0])
        {
            // Жижиг газрын зураг өөрөө зурагдсан: баруун доод буланд нь "томруулах" тэмдэг л нэмнэ
            const Vector2 corner = { button.area.x + button.area.width - 4.0f, button.area.y + button.area.height - 4.0f };
            DrawCircleV(corner, 17.0f, { 12, 16, 22, 230 });
            DrawRing(corner, 15.5f, 17.0f, 0.0f, 360.0f, 32, { 120, 190, 255, 200 });
            if (!Ui::DrawIcon(Ui::Icon::Fullscreen, { corner.x - 10.0f, corner.y - 10.0f, 20.0f, 20.0f }))
            {
                for (int k = 0; k < 4; ++k)
                {
                    const float sx = (k & 1) ? 1.0f : -1.0f, sy = (k & 2) ? 1.0f : -1.0f;
                    const Vector2 tip = { corner.x + sx * 8.0f, corner.y + sy * 8.0f };
                    DrawLineEx(tip, { tip.x - sx * 5.0f, tip.y }, 2.0f, WHITE);
                    DrawLineEx(tip, { tip.x, tip.y - sy * 5.0f }, 2.0f, WHITE);
                }
            }
            continue;
        }

        // Дарагдаж буй товч бага зэрэг жижгэрч, тодорно.
        bool pressed = false;
        for (const Finger& finger : fingers)
        {
            if (finger.id >= 0 && finger.role == Role::Button && &buttons[(size_t)finger.button] == &button) pressed = true;
        }
        const Color outline = button.highlighted ? accent : edge;
        const Vector2 center = { button.area.x + button.area.width * 0.5f, button.area.y + button.area.height * 0.5f };
        if (button.round)
        {
            const float radius = button.area.width * 0.5f * (pressed ? 0.94f : 1.0f);
            if (button.kind == Kind::Fire)
            {
                // Галын товч: улаан градиент, гэрэлтсэн хүрээ
                DrawCircleGradient((int)center.x, (int)center.y, radius, { 220, 70, 56, (unsigned char)(pressed ? 230 : 190) },
                                   { 120, 24, 20, (unsigned char)(pressed ? 230 : 170) });
                DrawRing(center, radius - 2.0f, radius, 0.0f, 360.0f, 48, { 255, 160, 140, 160 });
                DrawRing(center, radius * 0.5f - 1.5f, radius * 0.5f, 0.0f, 360.0f, 32, { 255, 220, 210, 120 });
            }
            else
            {
                DrawCircleV(center, radius, button.highlighted || pressed ? fillActive : fill);
                DrawRing(center, radius - (button.highlighted ? 2.5f : 1.5f), radius, 0.0f, 360.0f, 48, outline);
            }
        }
        else
        {
            const float radius = std::min(button.area.height * 0.5f, 14.0f);
            const float roundness = 2.0f * radius / std::min(button.area.width, button.area.height);
            const Color back = button.highlighted || pressed ? fillActive : fill;
            DrawRectangleRounded(button.area, roundness, 8, button.opaque ? Color{ back.r, back.g, back.b, 238 } : back);
            DrawRectangleRoundedLinesEx(button.area, roundness, 8, button.highlighted ? 2.0f : 1.0f, outline);
        }

        if (button.glyph == kGlyphBackpack)
        {
            // Үүргэвч: бие, дээд хавтас, бариул, урд халаас
            const Color c = button.highlighted ? accent : text;
            const float s = button.area.height;
            const Rectangle body = { center.x - s * 0.22f, center.y - s * 0.16f, s * 0.44f, s * 0.40f };
            DrawRectangleRoundedLinesEx(body, 0.35f, 6, 2.2f, c);
            DrawRectangleRoundedLinesEx({ body.x + s * 0.04f, body.y - s * 0.02f, body.width - s * 0.08f, s * 0.14f }, 0.5f, 6, 2.0f, c);
            DrawRing({ center.x, body.y - s * 0.02f }, s * 0.07f, s * 0.07f + 2.0f, 180.0f, 360.0f, 12, c);
            DrawRectangleRoundedLinesEx({ center.x - s * 0.12f, body.y + body.height * 0.52f, s * 0.24f, body.height * 0.32f }, 0.3f, 4, 1.8f, c);
        }
        else if (button.glyph == kGlyphVehicle)
        {
            // Унааны дүрс (зураг байхгүй бол шугамаар), товчны доор жижиг бичвэр
            const float side = button.area.width * 0.62f;
            if (!Ui::DrawIcon(Ui::Icon::MapVehicle, { center.x - side * 0.5f, center.y - side * 0.5f, side, side }))
            {
                const Rectangle body = { center.x - side * 0.4f, center.y - side * 0.12f, side * 0.8f, side * 0.3f };
                DrawRectangleRounded(body, 0.4f, 6, text);
                DrawRectangleRounded({ body.x + side * 0.14f, body.y - side * 0.2f, side * 0.52f, side * 0.24f }, 0.4f, 6, text);
                DrawCircleV({ body.x + side * 0.18f, body.y + body.height }, side * 0.09f, { 20, 20, 24, 255 });
                DrawCircleV({ body.x + body.width - side * 0.18f, body.y + body.height }, side * 0.09f, { 20, 20, 24, 255 });
            }
            const int size = 11;
            Ui::TextShadow(button.label, (int)(center.x - (float)Ui::MeasureBold(button.label, size) * 0.5f),
                           (int)(button.area.y + button.area.height) + 3, size, text, true);
        }
        else if (button.uiIcon >= 0 && Ui::HasIcon((Ui::Icon)button.uiIcon) && button.label && !button.label[0])
        {
            // Зөвхөн дүрс (⚙, ≡)
            const float side = std::min(button.area.width, button.area.height) * 0.56f;
            Ui::DrawIcon((Ui::Icon)button.uiIcon, { center.x - side * 0.5f, center.y - side * 0.5f, side, side },
                         button.highlighted ? accent : WHITE);
        }
        else if (button.icon >= 0)
        {
            // Зэвсгийн товч: дүрс, доор нь жижиг нэр
            const Color tint = button.highlighted ? accent : text;
            Ui::WeaponIcon(button.icon, { button.area.x + 10.0f, button.area.y + 6.0f, button.area.width - 20.0f, 28.0f }, tint);
            const char* name = button.label;
            if (name[0] >= '0' && name[0] <= '9' && name[1] == ' ') name += 2;
            int size = 11;
            while (size > 8 && Ui::MeasureBold(name, size) > (int)button.area.width - 8) --size;
            Ui::TextBold(name, (int)(center.x - (float)Ui::MeasureBold(name, size) * 0.5f), (int)(button.area.y + button.area.height) - size - 7,
                         size, button.highlighted ? accent : Ui::kTextDim);
        }
        else if (button.check >= 0)
        {
            // Чагттай товч: зүүн талд хайрцаг, баруун талд бичвэр
            const Rectangle boxArea = { button.area.x + 12.0f, center.y - 10.0f, 20.0f, 20.0f };
            DrawRectangleRounded(boxArea, 0.3f, 6, button.check ? accent : Color{ 255, 255, 255, 30 });
            DrawRectangleRoundedLinesEx(boxArea, 0.3f, 6, 1.5f, button.check ? accent : edge);
            if (button.check)
            {
                const Color tick = { 30, 22, 6, 255 };
                DrawLineEx({ boxArea.x + 4.0f, boxArea.y + 10.0f }, { boxArea.x + 8.5f, boxArea.y + 15.0f }, 3.0f, tick);
                DrawLineEx({ boxArea.x + 8.5f, boxArea.y + 15.0f }, { boxArea.x + 16.0f, boxArea.y + 5.0f }, 3.0f, tick);
            }
            int size = 16;
            while (size > 11 && Ui::MeasureBold(button.label, size) > (int)button.area.width - 52) --size;
            Ui::TextShadow(button.label, (int)boxArea.x + 30, (int)(center.y - (float)size * 0.55f), size,
                           button.check ? accent : text, true);
        }
        else if (button.uiIcon >= 0 && Ui::HasIcon((Ui::Icon)button.uiIcon) && button.label && button.label[0] == 'X' && !button.label[1])
        {
            // "X" (хаах): бичвэрийн оронд икон
            const float side = std::min(button.area.width, button.area.height) * 0.55f;
            Ui::DrawIcon((Ui::Icon)button.uiIcon, { center.x - side * 0.5f, center.y - side * 0.5f, side, side });
        }
        else if (button.label && button.label[0])
        {
            // Икон (байвал) бичвэрийн зүүн талд, хоёулаа товчны төвд
            const bool hasIcon = button.uiIcon >= 0 && Ui::HasIcon((Ui::Icon)button.uiIcon);
            const int iconSpace = hasIcon ? 24 : 0;
            int size = button.round ? 16 : 18;
            while (size > 11 && Ui::MeasureBold(button.label, size) + iconSpace > (int)button.area.width - 12) --size;
            const int textWidth = Ui::MeasureBold(button.label, size);
            const float startX = center.x - (float)(textWidth + iconSpace) * 0.5f;
            if (hasIcon) Ui::DrawIcon((Ui::Icon)button.uiIcon, { startX, center.y - 10.0f, 20.0f, 20.0f });
            Ui::TextShadow(button.label, (int)startX + iconSpace, (int)(center.y - (float)size * 0.55f), size,
                           button.dim ? Ui::kDanger : button.highlighted ? accent : text, true);
        }
    }

    if (state.screen == TouchScreen::Gameplay)
    {
        // Гүйлтийн түгжээ: хүрдний дээд талд (хүрдэнд хүрэх эсвэл түгжигдсэн үед) тэмдэг
        if (!state.driving && (stickActive || autoRun))
        {
            const Vector2 lockAt = { stickCenter.x, stickCenter.y - kStickRadius * kLockDistance };
            const bool lit = lockArmed || autoRun;
            DrawLineEx({ stickCenter.x, stickCenter.y - kStickRadius }, { lockAt.x, lockAt.y + 26.0f }, 2.0f, { 255, 255, 255, 50 });
            DrawCircleV(lockAt, 26.0f, lit ? Color{ 60, 46, 14, 200 } : Color{ 10, 12, 16, 120 });
            DrawRing(lockAt, 24.0f, 26.0f, 0.0f, 360.0f, 40, lit ? accent : edge);
            // Дээш харсан давхар сум ("гүйх")
            for (float dy : { -6.0f, 4.0f })
            {
                const Color c = lit ? accent : Color{ 255, 255, 255, 200 };
                DrawLineEx({ lockAt.x - 9.0f, lockAt.y + dy + 5.0f }, { lockAt.x, lockAt.y + dy - 4.0f }, 3.0f, c);
                DrawLineEx({ lockAt.x, lockAt.y + dy - 4.0f }, { lockAt.x + 9.0f, lockAt.y + dy + 5.0f }, 3.0f, c);
            }
            const char* label = autoRun ? "ГҮЙЖ БАЙНА" : "ГҮЙЛТ";
            Ui::TextShadow(label, (int)(lockAt.x - (float)Ui::MeasureBold(label, 13) * 0.5f), (int)lockAt.y - 46, 13,
                           lit ? accent : text, true);
        }

        // Хөдлөх хүрд: хүрээ ба хуруун доорх бариул
        Vector2 knob = stickCenter;
        if (stickActive || autoRun)
        {
            knob = { stickCenter.x + move.x * kStickRadius, stickCenter.y - move.y * kStickRadius };
        }
        DrawCircleV(stickCenter, kStickRadius, { 12, 16, 22, 80 });
        DrawRing(stickCenter, kStickRadius - 2.0f, kStickRadius, 0.0f, 360.0f, 64, InSprint() ? accent : edge);
        // Чиглэлийн жижиг сумнууд
        for (int i = 0; i < 4; ++i)
        {
            const float a = (float)i * PI * 0.5f;
            const Vector2 dir = { std::cos(a), std::sin(a) };
            const Vector2 tip = { stickCenter.x + dir.x * (kStickRadius - 10.0f), stickCenter.y + dir.y * (kStickRadius - 10.0f) };
            const Vector2 side = { -dir.y * 6.0f, dir.x * 6.0f };
            const Vector2 back = { tip.x - dir.x * 8.0f, tip.y - dir.y * 8.0f };
            DrawTriangle(tip, { back.x - side.x, back.y - side.y }, { back.x + side.x, back.y + side.y }, { 255, 255, 255, 70 });
            DrawTriangle(tip, { back.x + side.x, back.y + side.y }, { back.x - side.x, back.y - side.y }, { 255, 255, 255, 70 });
        }
        DrawCircleV(knob, 40.0f, { 255, 255, 255, stickActive ? (unsigned char)150 : (unsigned char)70 });
        DrawRing(knob, 38.0f, 40.0f, 0.0f, 360.0f, 48, InSprint() ? accent : Color{ 255, 255, 255, 120 });
    }
    else if (state.screen == TouchScreen::Journal || state.screen == TouchScreen::Result)
    {
        const char* hint = state.screen == TouchScreen::Journal ? "Хаахын тулд дэлгэцэд товшино уу" : "Үргэлжлүүлэхийн тулд дэлгэцэд товшино уу";
        const int textWidth = UiMeasure(hint, 24);
        UiTextShadow(hint, (ViewWidth() - textWidth) / 2, ViewHeight() - 44, 24, accent);
    }
}

bool InActivity()
{
    if (touchMode)
    {
        for (const Finger& finger : fingers)
        {
            if (finger.id >= 0) return true;
        }
        return false;
    }
    const Vector2 delta = GetMouseDelta();
    if (std::fabs(delta.x) + std::fabs(delta.y) > 0.5f) return true;
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) return true;
    for (int key : { KEY_W, KEY_A, KEY_S, KEY_D, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_SPACE, KEY_LEFT_SHIFT, KEY_E,
                     KEY_R, KEY_C, KEY_Z, KEY_F })
    {
        if (IsKeyDown(key)) return true;
    }
    return false;
}
