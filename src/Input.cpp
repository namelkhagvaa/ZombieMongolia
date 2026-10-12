#include "Input.h"

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
    const float kStickRadius = 78.0f;   // хөдлөх хүрдний хүрээ, харагдацын пикселээр
    // Хүрдийг хажуу тийш хазайлгахад камер тэр зүг рүү эргэнэ: нэг хуруугаар явж, зүглэж болно.
    const float kStickTurn = 720.0f;    // бүрэн хазайлтад секундэд эргэх хэмжээ, "хулганы пиксел"-ээр (~125 градус/сек)
    const float kStickStrafe = 0.35f;   // хүрдээр хажуу тийш алхах хурдны хувь
    bool stickTurns = false;            // энэ frame-д хүрд камерыг эргүүлж байгаа эсэх

    enum class Kind
    {
        Key,   // дарахад нэг удаа "товч дарагдсан"
        Hold,  // дарж байх хугацаанд "товч дарагдсан хэвээр"
        Fire,  // хулганы зүүн товч; хурууг чирэхэд камер мөн эргэнэ
        Aim,   // онилгоог асааж унтраана (хулганы баруун товч)
        Wheel  // газрын зураг томруулах / жижигрүүлэх
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
    };
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
    };
    Finger fingers[10];
    Vector2 stickCenter = { 150.0f, 0.0f };
    bool stickActive = false;
    char memberLabels[3][64];

    Rectangle Circle(float cx, float cy, float radius)
    {
        return { cx - radius, cy - radius, radius * 2.0f, radius * 2.0f };
    }

    void Add(Rectangle area, const char* label, Kind kind, int key, bool round = true, bool highlighted = false,
             bool dim = false)
    {
        buttons.push_back({ area, label, kind, key, round, highlighted, dim, -1, -1 });
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
            Add(Circle(w - 118.0f, h - 150.0f, 62.0f), state.driving ? "" : "БУУДАХ", Kind::Fire, 0);
            Add(Circle(w - 250.0f, h - 88.0f, 44.0f), state.sniper ? "ДУРАН" : "ОНИЛОХ", Kind::Aim, 0, true, aimLatched);
            Add(Circle(w - 74.0f, h - 292.0f, 42.0f), state.driving ? "ТООРМОС" : state.stance != 0 ? "БОСОХ" : "ҮСРЭХ",
                Kind::Hold, KEY_SPACE);
            Add(Circle(w - 232.0f, h - 214.0f, 38.0f), "ЦЭНЭГЛЭХ", Kind::Key, KEY_R);
            if (!state.driving)
            {
                Add(Circle(w - 350.0f, h - 130.0f, 38.0f), "СУУХ", Kind::Key, KEY_C, true, state.stance == 1);
                Add(Circle(w - 334.0f, h - 228.0f, 38.0f), "ХЭВТЭХ", Kind::Key, KEY_Z, true, state.stance == 2);
                // Зүүн гарын эрхий хуруунд: хүрдийн дээр тогтсон байрлалтай хоёр дахь буудах товч
                // (хүрдээс хамаарахгүй; товч хүрдийн бүсээс түрүүлж шалгагдана).
                // Хүрдийн (төв нь доороос 190, радиус ~80) шууд дээр; намхан дэлгэцэд дээд талын товчнуудаас доош.
                Add(Circle(110.0f, std::max(h - 340.0f, 215.0f), 52.0f), "БУУДАХ", Kind::Fire, 0);
            }

            // --- Нөхцөлт үйлдлүүд: зөвхөн боломжтой үед гарна ---
            float actionY = h - 392.0f;
            if (state.useLabel)
            {
                Add({ w - 210.0f, actionY, 190.0f, 54.0f }, state.useLabel, Kind::Key, KEY_F, false, true);
                actionY -= 64.0f;
            }
            if (state.actLabel)
            {
                Add({ w - 210.0f, actionY, 190.0f, 54.0f }, state.actLabel, Kind::Key, KEY_E, false, true);
            }

            // --- Доод голд: зэвсгүүд ---
            const float slotW = 92.0f, slotH = 56.0f, gap = 8.0f;
            const float total = (float)state.weaponCount * slotW + (float)(state.weaponCount - 1) * gap;
            for (int i = 0; i < state.weaponCount; ++i)
            {
                Add({ (w - total) * 0.5f + (float)i * (slotW + gap), h - slotH - 10.0f, slotW, slotH }, state.weaponNames[i],
                    Kind::Key, KEY_ONE + i, false, i == state.weaponCurrent);
                buttons.back().icon = state.weaponTypes[i];
            }

            // --- Баруун дээд: жижиг газрын зураг (товшиход бүтэн зураг), зүүн талд нь баг ---
            const float map = (float)state.minimapSize;
            Add({ w - map - 16.0f, 16.0f, map, map }, "", Kind::Key, KEY_M, false);
            for (int i = 0; i < 3; ++i)
            {
                const TouchState::Member& member = state.team[i];
                if (member.alive) std::snprintf(memberLabels[i], sizeof(memberLabels[i]), "%s  %d", member.name, member.health);
                else std::snprintf(memberLabels[i], sizeof(memberLabels[i]), "%s  —", member.name);
                Add({ w - map - 16.0f - 232.0f, 16.0f + (float)i * 52.0f, 220.0f, 44.0f }, memberLabels[i], Kind::Key,
                    KEY_F1 + i, false, member.active, !member.alive);
            }

            // --- Зүүн дээд, даалгаврын мөрийн доор: журнал, буцах ---
            Add({ 16.0f, 112.0f, 130.0f, 42.0f }, "ДААЛГАВАР", Kind::Key, KEY_Q, false);
            Add({ 154.0f, 112.0f, 130.0f, 42.0f }, "УХРАХ", Kind::Key, KEY_TAB, false);
            // Автомат онилгоо: чагтлагдсан бол хараа ойрын зомби руу өөрөө татагдана, үгүй бол өөрөө онилно.
            Add({ 292.0f, 112.0f, 200.0f, 42.0f }, "АВТО ОНИЛГОО", Kind::Key, KEY_T, false, state.aimAssist);
            buttons.back().check = state.aimAssist ? 1 : 0;
            if (state.joinRequest)
            {
                // Хамтарсан тоглолтын хүсэлт (дэлгэцийн дээд голын картын доор)
                Add({ w * 0.5f - 230.0f, 150.0f, 220.0f, 50.0f }, "ЗӨВШӨӨРӨХ", Kind::Key, KEY_Y, false, true);
                Add({ w * 0.5f + 10.0f, 150.0f, 220.0f, 50.0f }, "ТАТГАЛЗАХ", Kind::Key, KEY_N, false);
            }
            // Камерын горим: 1-р этгээд (зөвхөн гар, буу) / 3-р этгээд (бүтэн бие)
            Add({ 500.0f, 112.0f, 150.0f, 42.0f }, state.firstPerson ? "КАМЕР: 1-Р" : "КАМЕР: 3-Р", Kind::Key, KEY_V, false,
                state.firstPerson);
            break;
        }

        case TouchScreen::FullMap:
            Add({ w - 96.0f, 20.0f, 76.0f, 60.0f }, "X", Kind::Key, KEY_M, false, true);
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
    if (stickCenter.y == 0.0f)
    {
        stickCenter = { 160.0f, (float)ViewHeight() - 190.0f };
    }

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
                }
            }
            else if (gameplay && position.x < (float)ViewWidth() * 0.42f && position.y > (float)ViewHeight() * 0.3f)
            {
                finger->role = Role::Stick; // хөдлөх хүрд хуруу буусан газраас эхэлнэ
                stickCenter = position;
            }
            else if (gameplay)
            {
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
            pointer = position;
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

bool InKeyDown(int key)
{
    return IsKeyDown(key) || (key >= 0 && key < kKeyCount && virtualDown[key]);
}

bool InKeyPressed(int key)
{
    return IsKeyPressed(key) || (key >= 0 && key < kKeyCount && virtualPressed[key]);
}

bool InMouseDown(int button)
{
    if (touchMode)
    {
        return button == MOUSE_BUTTON_LEFT ? (fireDown || pointerDown) : button == MOUSE_BUTTON_RIGHT ? aimLatched : false;
    }
    return IsMouseButtonDown(button);
}

bool InMousePressed(int button)
{
    if (touchMode)
    {
        return button == MOUSE_BUTTON_LEFT && (firePressed || pointerPressed);
    }
    return IsMouseButtonPressed(button);
}

Vector2 InLookDelta()
{
    return touchMode ? look : GetMouseDelta();
}

float InWheel()
{
    return touchMode ? wheel : GetMouseWheelMove();
}

Vector2 InPointer()
{
    return pointer;
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
    Vector2 axis = { (float)IsKeyDown(KEY_D) - (float)IsKeyDown(KEY_A), (float)IsKeyDown(KEY_W) - (float)IsKeyDown(KEY_S) };
    axis = Vector2Add(Vector2Add(axis, move), testMove);
    const float length = Vector2Length(axis);
    return length > 1.0f ? Vector2Scale(axis, 1.0f / length) : axis;
}

bool InSprint()
{
    // Хүрдийг захад нь хүртэл урагш түлхвэл гүйнэ.
    return IsKeyDown(KEY_LEFT_SHIFT) || (move.y > 0.5f && Vector2Length(move) > 0.93f);
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
        if (state.screen == TouchScreen::Gameplay && button.key == KEY_M && button.kind == Kind::Key && !button.label[0])
        {
            continue; // жижиг газрын зураг өөрөө зурагдсан: дээр нь зөвхөн товших талбар
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
            DrawRectangleRounded(button.area, roundness, 8, button.highlighted || pressed ? fillActive : fill);
            DrawRectangleRoundedLinesEx(button.area, roundness, 8, button.highlighted ? 2.0f : 1.0f, outline);
        }

        if (button.icon >= 0)
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
        else if (button.label && button.label[0])
        {
            int size = button.round ? 16 : 18;
            while (size > 11 && Ui::MeasureBold(button.label, size) > (int)button.area.width - 12) --size;
            const int textWidth = Ui::MeasureBold(button.label, size);
            Ui::TextShadow(button.label, (int)(center.x - (float)textWidth * 0.5f), (int)(center.y - (float)size * 0.55f), size,
                           button.dim ? Ui::kDanger : button.highlighted ? accent : text, true);
        }
    }

    if (state.screen == TouchScreen::Gameplay)
    {
        // Хөдлөх хүрд: хүрээ ба хуруун доорх бариул
        Vector2 knob = stickCenter;
        if (stickActive)
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
        DrawCircleV(knob, 34.0f, { 255, 255, 255, stickActive ? (unsigned char)150 : (unsigned char)70 });
        DrawRing(knob, 32.0f, 34.0f, 0.0f, 360.0f, 48, InSprint() ? accent : Color{ 255, 255, 255, 120 });
    }
    else if (state.screen == TouchScreen::Journal || state.screen == TouchScreen::Result)
    {
        const char* hint = state.screen == TouchScreen::Journal ? "Хаахын тулд дэлгэцэд товшино уу" : "Үргэлжлүүлэхийн тулд дэлгэцэд товшино уу";
        const int textWidth = UiMeasure(hint, 24);
        UiTextShadow(hint, (ViewWidth() - textWidth) / 2, ViewHeight() - 44, 24, accent);
    }
}
