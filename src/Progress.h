#pragma once

#include "Weapon.h"

#include <vector>

// Нэг үеийн (аймгийн) даалгаврын хадгалагдах явц.
struct StageSave
{
    static constexpr int kMaxObjectives = 4;

    unsigned done = 0;                        // бит i = i-р даалгавар биелсэн
    unsigned entities[kMaxObjectives] = {};   // даалгавар бүрд: бит k = k-р хүн аврагдсан / зүйл авагдсан
    unsigned clearedZones = 0;                // бит i = i-р даалгаврын бүс бүрэн цэвэрлэгдсэн (дахин spawn болохгүй)
    int killedSmall = 0;                      // энэ үед устгасан алхагч + гүйгч зомби
    int killedBig = 0;                        // энэ үед устгасан том зомби

    bool Empty() const
    {
        return done == 0 && clearedZones == 0 && killedSmall == 0 && killedBig == 0 && entities[0] == 0 && entities[1] == 0 && entities[2] == 0 &&
               entities[3] == 0;
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
    bool firstPerson = false;
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
