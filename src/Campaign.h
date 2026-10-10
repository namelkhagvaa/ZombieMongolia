#pragma once

#include "Progress.h"
#include "Regions.h"

#include <vector>

// Чөлөөлөх нутгууд (21 аймаг ба Улаанбаатар) нь Regions.h-д тодорхойлогдсон.
// Нэг даалгавар = нэг аймаг; талбар нь тухайн аймгийн хилийн хэлбэртэй.

// Даалгаврын хүндрэл: аяны дарааллын дугаараас хамаарна.
struct MissionParams
{
    int waves;          // давалгааны (үе шатны) тоо
    int baseZombies;    // эхний давалгааны зомбины тоо
    float healthScale;  // зомбины амины үржүүлэгч
    float zombieDamage; // энгийн зомбины нэг цохилт
    float speedBonus;   // зомбины нэмэлт хурд, м/с
    int reward;         // анх чөлөөлөхөд өгөх зоос
};

MissionParams MissionFor(int provinceIndex);

// Эхний (хамгийн баруун) аймаг үргэлж нээлттэй; бусад нь өмнөх аймгаа чөлөөлсний дараа нээгдэнэ.
bool IsProvinceUnlocked(const Progress& progress, int provinceIndex);

// Газрын зураг + дэлгүүрийн дэлгэц.
class MapScreen
{
public:
    enum class Result
    {
        None,
        StartMission
    };

    void SelectNextTarget(const Progress& progress);

    // Худалдан авалт хийгдсэн бол progressChanged = true.
    Result Update(Progress& progress, bool& progressChanged);
    void Draw(const Progress& progress) const;

    int Selected() const { return selected; }
    void Select(int provinceIndex) { selected = provinceIndex; }

private:
    int selected = 0;
};
