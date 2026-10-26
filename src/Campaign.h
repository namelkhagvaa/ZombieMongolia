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
    void Preload() const; // дэвсгэр зураг, аймгуудын дүрсийг урьдчилан ачаална
    void DrawConfirm(const Progress& progress) const; // (газрын зургийн бусад самбарын дараа: хамгийн дээр)

    int Selected() const { return selected; }
    void Select(int provinceIndex) { selected = provinceIndex; }
    // Сайжруулалт авах баталгаажуулах цонх
    bool DialogOpen() const { return confirmItem >= 0; }
    void CloseDialog() { confirmItem = -1; }

private:
    int confirmItem = -1;                 // баталгаажуулж буй сайжруулалт (ShopItem), -1 = цонхгүй
    mutable Rectangle shopCards[8] = {};  // сайжруулалтын мөрүүд (Draw тооцно, Update товшилтыг шалгана)
    mutable Rectangle confirmYes = {}, confirmNo = {}, confirmPanel = {};
    int selected = 0;
    // Давхар товшилт: сүүлийн товшсон аймаг ба цаг
    int lastClicked = -1;
    double lastClickTime = -1.0;
};
