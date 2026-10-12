#if defined(__ANDROID__)
#include <cstdio>
#include <android_native_app_glue.h>
extern "C" struct android_app* GetAndroidApp(void); // raylib-ийн Android давхаргаас
#endif

#include "Progress.h"

#include "Shop.h"
#include "raylib.h"

#include <algorithm>
#include <fstream>
#include <string>

namespace
{
    std::string overridePath;

    std::string SavePath()
    {
#if defined(__ANDROID__)
        // Апп-ын дотоод хадгалах сан (зөвхөн энэ апп унших, бичих эрхтэй).
        return std::string(GetAndroidApp()->activity->internalDataPath) + "/savegame.txt";
#else
        return overridePath.empty() ? std::string(GetApplicationDirectory()) + "savegame.txt" : overridePath;
#endif
    }
}

void SetSavePath(const char* path)
{
    overridePath = path ? path : "";
}

int Progress::LiberatedCount() const
{
    return (int)std::count(liberated.begin(), liberated.end(), true);
}

bool LoadProgress(Progress& progress, int provinceCount)
{
    progress = Progress{};
    progress.liberated.assign(provinceCount, false);
    progress.stages.assign(provinceCount, StageSave{});

    std::ifstream file(SavePath());
    if (!file)
    {
        return false;
    }

    // Мөр бүр: "түлхүүр утга"
    int format = 1; // "format" мөргүй файл бол хуучин хувилбар
    std::string key;
    while (file >> key)
    {
        if (key == "liberated")
        {
            std::string flags;
            file >> flags;
            for (size_t i = 0; i < flags.size() && i < progress.liberated.size(); ++i)
            {
                progress.liberated[i] = flags[i] == '1';
            }
            continue;
        }

        if (key == "stage")
        {
            // stage <аймгийн индекс> <done> <entities x4> <clearedZones>
            int index = -1;
            StageSave stage;
            file >> index >> stage.done;
            for (unsigned& mask : stage.entities) file >> mask;
            file >> stage.clearedZones;
            // Хуучин хадгалалтад устгасан зомбины тоо байхгүй: мөрийн үлдсэн хэсгээс байвал уншина.
            std::string rest;
            std::getline(file, rest);
            std::sscanf(rest.c_str(), "%d %d", &stage.killedSmall, &stage.killedBig);
            if (file && index >= 0 && index < (int)progress.stages.size())
            {
                progress.stages[index] = stage;
            }
            continue;
        }

        int value = 0;
        if (!(file >> value))
        {
            break;
        }

        if (key == "coins") progress.coins = std::max(0, value);
        else if (key == "kills") progress.kills = std::max(0, value);
        else if (key == "damage") progress.weapon.damage = std::clamp(value, 0, kMaxWeaponLevel);
        else if (key == "fireRate") progress.weapon.fireRate = std::clamp(value, 0, kMaxWeaponLevel);
        else if (key == "magazine") progress.weapon.magazine = std::clamp(value, 0, kMaxWeaponLevel);
        else if (key == "health") progress.healthLevel = std::clamp(value, 0, kMaxHealthLevel);
        else if (key == "soldiers") progress.soldiers = std::clamp(value, 0, kMaxSoldiers);
        else if (key == "aimAssist") progress.aimAssist = value != 0;
        else if (key == "firstPerson") progress.firstPerson = value != 0;
        else if (key == "netTag") progress.netTag = value;
        else if (key == "format") format = value;
    }

    if (format < 2)
    {
        // Хуучин хадгалалт: аймгуудын дараалал өөр байсан (Улаанбаатараас эхэлдэг).
        // Чөлөөлсөн аймгийн ТОО-г хадгалж, шинэ дарааллын (баруунаас зүүн тийш) эхний аймгуудад шилжүүлнэ.
        const int count = progress.LiberatedCount();
        for (int i = 0; i < (int)progress.liberated.size(); ++i)
        {
            progress.liberated[i] = i < count;
        }
    }
    return !file.bad();
}

bool SaveProgress(const Progress& progress)
{
    std::ofstream file(SavePath(), std::ios::trunc);
    if (!file)
    {
        return false;
    }

    file << "format 2\n"; // 2 = аймгууд баруунаас зүүн тийш дараалалтай
    file << "coins " << progress.coins << '\n';
    file << "kills " << progress.kills << '\n';
    file << "damage " << progress.weapon.damage << '\n';
    file << "fireRate " << progress.weapon.fireRate << '\n';
    file << "magazine " << progress.weapon.magazine << '\n';
    file << "health " << progress.healthLevel << '\n';
    file << "soldiers " << progress.soldiers << '\n';
    file << "aimAssist " << (progress.aimAssist ? 1 : 0) << '\n';
    file << "firstPerson " << (progress.firstPerson ? 1 : 0) << '\n';
    file << "netTag " << progress.netTag << '\n';

    file << "liberated ";
    for (bool flag : progress.liberated)
    {
        file << (flag ? '1' : '0');
    }
    file << '\n';

    // Дуусаагүй үеүдийн даалгаврын явц
    for (size_t i = 0; i < progress.stages.size(); ++i)
    {
        const StageSave& stage = progress.stages[i];
        if (stage.Empty()) continue;

        file << "stage " << i << ' ' << stage.done;
        for (unsigned mask : stage.entities) file << ' ' << mask;
        file << ' ' << stage.clearedZones << ' ' << stage.killedSmall << ' ' << stage.killedBig << '\n';
    }

    return file.good();
}
