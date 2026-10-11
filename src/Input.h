#pragma once

#include "raylib.h"

// Оролт ба дэлгэцийн харагдацын давхарга.
//
// Тоглоомын код гар, хулганыг шууд уншихын оронд энд байгаа In... функцуудыг дуудна. Тэд жинхэнэ гар,
// хулганыг мэдрэгчтэй дэлгэцийн товчлууруудтай (утсан дээр) нэгтгэж өгнө: товчлуур дарахад харгалзах
// "товч" дарагдсан мэт болдог тул тоглоомын логик хоёр төхөөрөмжид ижил хэвээр.
//
// Харагдац (View): утасны дэлгэц өндөр нягтралтай тул тоглоомыг тогтсон өндөртэй (720) дотоод зурагт
// зураад дэлгэц рүү сунгана. Ингэснээр бичвэр, товчлуур ямар ч утсан дээр ижил хэмжээтэй, зурах ачаалал бага.

// ---------------------------------------------------------------- Харагдац

// targetHeight > 0 бол тоглоомыг ийм өндөртэй дотоод зурагт зурна (дэлгэц түүнээс өндөр үед); 0 = шууд.
void ViewInit(int targetHeight);
void ViewShutdown();
int ViewWidth();
int ViewHeight();
// BeginDrawing / EndDrawing хоёрын хооронд: бүх зураглалыг энэ хоёрын дунд хийнэ.
void ViewBegin();
void ViewEnd();
// GetWorldToScreen-ийн харагдацын хэмжээгээр тооцсон хувилбар.
Vector2 ViewWorldToScreen(Vector3 position, Camera3D camera);

// ---------------------------------------------------------------- Мэдрэгчтэй дэлгэцийн төлөв

// Аль дэлгэц нээлттэй байгаагаас хамаарч өөр товчлуурууд гарна.
enum class TouchScreen
{
    Gameplay,    // тоглож буй: хөдлөх хүрд, харах, буудах зэрэг бүх товчлуур
    Journal,     // даалгаврын журнал: хаана ч товшиход хаагдана
    FullMap,     // бүтэн газрын зураг: чирэх, томруулах, хаах
    CampaignMap, // аяны газрын зураг + дэлгүүр
    Result       // ялалт / ялагдлын дэлгэц: товшиж үргэлжлүүлнэ
};

struct TouchState
{
    TouchScreen screen = TouchScreen::CampaignMap;

    // Зэвсгийн нүднүүд (товшиж сонгоно)
    int weaponCount = 0;
    const char* const* weaponNames = nullptr;
    int weaponCurrent = -1;

    // Багийн гурван гишүүн (товшиж шилжинэ)
    struct Member
    {
        const char* name = "";
        int health = 0;
        bool alive = true;
        bool active = false;
    };
    Member team[3];

    const char* useLabel = nullptr; // F-ийн үйлдэл: "Суух", "Буух", "Хаалга"; nullptr = товчлуур гарахгүй
    const char* actLabel = nullptr; // E-ийн үйлдэл (даалгаврын объект); nullptr = гарахгүй
    bool driving = false;           // унаа жолоодож буй: үсрэх товч тоормос болно
    bool sniper = false;            // онилох товч дуран хараа нээнэ
    int stance = 0;                 // 0 босоо, 1 суусан, 2 хэвтсэн (товчлуурыг тодруулахад)
    int minimapSize = 0;            // жижиг газрын зургийн хэмжээ (баруун дээд буланд; товшиход бүтэн зураг нээгдэнэ)
};

// ---------------------------------------------------------------- Оролт

void InSetTouchMode(bool enabled);
bool InTouchMode();

// Frame бүрийн эхэнд: мэдрэгчийн хүрэлтүүдийг уншиж, товчлууруудын төлөвийг шинэчилнэ.
void InBeginFrame(const TouchState& state);
// Мэдрэгчтэй дэлгэцийн товчлууруудыг зурна (бусад 2D зураглалын дараа).
void InDrawTouch(const TouchState& state);

bool InKeyDown(int key);
bool InKeyPressed(int key);
bool InMouseDown(int button);
bool InMousePressed(int button);
Vector2 InLookDelta(); // камер эргүүлэх шилжилт (хулганы хөдөлгөөн эсвэл хуруугаар чирэх), пикселээр
float InWheel();
Vector2 InPointer();   // заагчийн байрлал харагдацын координатаар (хулгана эсвэл эхний хуруу)

// Хөдөлгөөний чиглэл: x = баруун (+) / зүүн (-), y = урагш (+) / хойш (-); урт нь 1-ээс ихгүй.
Vector2 InMoveAxis();
bool InSprint();
// Туршилт: тоглогчийг тогтмол чиглэлд алхуулна (x = баруун, y = урагш).
void InSetTestMove(Vector2 axis);
// Мэдрэгчтэй дэлгэцийн хүрдээр явахад хажуу тийш алхах хэсгийн үржүүлэгч (хүрд камерыг мөн эргүүлдэг тул
// хажуу тийш бүрэн хурдаар гулсахгүй); гараар удирдахад 1.
float InStrafeScale();
