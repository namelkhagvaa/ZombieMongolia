#pragma once

#include "Weapon.h"

#include <vector>

// Даалгаврын өгөгдлийн (CampaignData.cpp) хувилбар: өөрчлөгдөхөд хадгалсан дуусаагүй явц шинээр эхэлнэ.
constexpr int kMissionsVersion = 2;

// Нэг үеийн (аймгийн) даалгаврын хадгалагдах явц.
struct StageSave
{
    static constexpr int kMaxObjectives = 12; // үе шатны даалгаврын дээд тоо (done битүүд)
    static constexpr int kEntitySlots = 12;   // даалгавар бүрийн хүн / зүйлийн явц (эхний 4 нь "stage" мөрөнд, бусад нь "stagex")

    unsigned done = 0;                        // бит i = i-р даалгавар биелсэн
    unsigned entities[kEntitySlots] = {};     // даалгавар бүрд: бит k = k-р хүн аврагдсан / зүйл авагдсан
    unsigned clearedZones = 0;                // бит i = i-р даалгаврын бүс бүрэн цэвэрлэгдсэн (дахин spawn болохгүй)
    int killedSmall = 0;                      // энэ үед устгасан алхагч + гүйгч зомби
    int killedBig = 0;                        // энэ үед устгасан том зомби
    int guardKills[kMaxObjectives][2] = {};   // даалгавар бүрийн хамгаалагч зомбиос устгасан: [0] жижиг, [1] том

    bool Empty() const
    {
        if (done != 0 || clearedZones != 0 || killedSmall != 0 || killedBig != 0) return false;
        for (unsigned mask : entities)
        {
            if (mask != 0) return false;
        }
        return true;
    }
};

// Даалгавар хооронд хадгалагдах тоглогчийн ахиц.
struct Progress
{
    std::vector<StageSave> stages; // аймаг бүрээр: дуусаагүй үеийн даалгаврын явц
    int coins = 0;
    int kills = 0;
    WeaponLevels weapon;
    int healthLevel = 0;
    int soldiers = 0;
    bool aimAssist = false;      // утсан дээр автомат онилгоо (тохиргоо)
    bool firstPerson = true;     // анхдагч: 1-р этгээд (зөвхөн зэвсэг харагдана)
    bool fullscreen = true;      // компьютер: бүтэн дэлгэцээр тоглох (F11 / цэс)
    int graphics = 0;            // 0 бага (өгөгдмөл), 1 дунд (сүүдэр), 2 өндөр (хүний сүүдэр, bloom)
    bool sound = true;           // дуу асаалттай
    int soundLevel = 0;          // 0 = 100%, 1 = 60%, 2 = 30%
    int leaderSkin = 0;          // 1-р дайчны дүр (Team.h: 0 цэрэг, 1 Model-1, 2 Model-3)
    int netTag = 0;              // хамтарсан тоглолтод харагдах дугаар (1000..9999)    // камер: 1-р этгээд (зөвхөн гар, буу) эсвэл 3-р этгээд (бүтэн бие)
    std::vector<bool> liberated; // аймаг бүрээр

    int LiberatedCount() const;
};

// Файл нь exe-ийн хажууд savegame.txt нэрээр хадгалагдана.
// Файл байхгүй эсвэл уншигдахгүй бол progress-ийг шинэ тоглоомын утгаар үлдээж false буцаана.
bool LoadProgress(Progress& progress, int provinceCount);
bool SaveProgress(const Progress& progress);

// Хадгалах файлын замыг солино (туршилтад: жинхэнэ хадгалалтыг хөндөхгүй). nullptr = анхдагч.
void SetSavePath(const char* path);
