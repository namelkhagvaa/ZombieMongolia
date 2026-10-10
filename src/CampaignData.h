#pragma once

#include <vector>

// Аяны өгөгдөл: 22 үе (21 аймаг + Улаанбаатар) тус бүрийн зорилго ба 3-4 даалгавар.
// Даалгаврын систем (Mission.cpp) нь нэг ерөнхий код; үе хоорондын ялгаа бүхэлдээ энэ өгөгдөлд байна.

enum class ObjectiveType
{
    ReachLocation,   // тодорхой байршилд хүрэх
    InteractObject,  // объект идэвхжүүлэх (генератор, радио)
    RescueSurvivors, // иргэд эсвэл цэргүүдийг аврах
    CollectItems,    // эд зүйл цуглуулах
    DeliverItems,    // цуглуулсан хангамжийг заасан цэгт хүргэх
    ClearZone,       // бүсийн бүх зомби ба нөөцийг устгах
    DefendLocation,  // байршлыг тодорхой хугацаанд хамгаалах
    DefeatBoss       // тусгай дайсныг ялах
};

// Даалгаврын байршил. Төвийн дөрөв нь тогтмол газар; Camp / Hamlet нь тухайн аймгийн хилийн доторх,
// эхлэх цэгт ойрын N дэх отор / суурин (ordinal = 0 бол хамгийн ойр) тул аймаг бүрд өөр газар болно.
enum class SiteKind
{
    Checkpoint, // цэргийн пост
    Settlement, // төв суурин
    GasStation, // шатахуун түгээх газар
    Warehouse,  // агуулах
    Camp,       // малчны отор
    Hamlet      // жижиг суурин
};

struct SiteRef
{
    SiteKind kind;
    int ordinal = 0;
};

enum class PropKind
{
    None,
    Generator,
    Radio
};

enum class ItemKind
{
    Fuel,     // түлш
    Medicine, // эм
    Ammo,     // сум
    Food      // хүнс
};

enum class SurvivorKind
{
    Soldier, // аврагдсаны дараа багт нэгдэж, даалгавар дуустал хамт тулалдана
    Civilian // аврагдсаны дараа байрандаа аюулгүй үлдэнэ
};

struct ObjectiveDef
{
    const char* id;          // хадгалалт, лог дахь нэр (латин)
    const char* title;       // нэр
    const char* description; // тоглогч юу хийхийг тайлбарласан
    ObjectiveType type;
    SiteRef site;
    int count = 1;           // аврах хүн / цуглуулах зүйлийн тоо, эсвэл бүсийн зомбины нөөцийн үржүүлэгч (%)
    float seconds = 0.0f;    // хамгаалах хугацаа
    int after = -1;          // урьдчилсан нөхцөл: энэ индекстэй даалгавар биелсний дараа идэвхжинэ (-1 = шууд идэвхтэй)
    int reward = 40;         // зоос
    const char* action = ""; // E товчны үйлдлийн нэр: "E — <action>"
    PropKind prop = PropKind::None;
    ItemKind item = ItemKind::Fuel;
    SurvivorKind survivor = SurvivorKind::Civilian;
    int guards = 0;          // байршлыг сахих зомбины тоо (ClearZone биш даалгаварт)
};

struct StageDef
{
    const char* provinceName; // монгол нэр, том үсгээр
    const char* goal;         // үеийн ерөнхий зорилго
    std::vector<ObjectiveDef> objectives;
};

// provinceIndex: аяны дараалал (0 = Баян-Өлгий ... 21 = Дорнод).
const StageDef& StageFor(int provinceIndex);
