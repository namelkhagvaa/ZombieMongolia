#if defined(__ANDROID__)
#include <cstdio>
#include <android_native_app_glue.h>
extern "C" struct android_app* GetAndroidApp(void); // raylib-ийн Android давхаргаас
#endif

#include "Progress.h"

#include "Platform.h"
#include "Shop.h"
#include "Team.h"
#include "raylib.h"

#include <algorithm>
#include <cstdlib>
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
#elif defined(_WIN32)
        // Хэрэглэгчийн хавтас (%LOCALAPPDATA%\ZombieMongolia): Program Files-д суулгасан үед exe-ийн хавтсанд
        // бичих эрхгүй. Хуучин хувилбар exe-ийн хажууд хадгалдаг байсан тул тэр файл байвал анх удаа хуулна.
        if (!overridePath.empty()) return overridePath;
        const char* local = std::getenv("LOCALAPPDATA");
        if (!local || !*local) return std::string(GetApplicationDirectory()) + "savegame.txt";
        const std::string folder = std::string(local) + "\\ZombieMongolia";
        if (!DirectoryExists(folder.c_str())) MakeDirectory(folder.c_str());
        const std::string path = folder + "\\savegame.txt";
        const std::string legacy = std::string(GetApplicationDirectory()) + "savegame.txt";
        if (!FileExists(path.c_str()) && FileExists(legacy.c_str()))
        {
            std::ifstream from(legacy, std::ios::binary);
            std::ofstream to(path, std::ios::binary);
            to << from.rdbuf();
        }
        return path;
#elif defined(__EMSCRIPTEN__)
        // Хөтөч: санах ойн файлын системд; WebPersistFile нь localStorage-д хуулна.
        return overridePath.empty() ? std::string("/savegame.txt") : overridePath;
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

    WebRestoreFile(SavePath().c_str()); // хөтөч: localStorage-оос сэргээнэ
    std::ifstream file(SavePath());
    if (!file)
    {
        return false;
    }

    // Мөр бүр: "түлхүүр утга"
    int format = 1; // "format" мөргүй файл бол хуучин хувилбар
    int savedFirstPerson = -1, viewVersion = 1, graphicsVersion = 1;
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
        else if (key == "fullscreen") progress.fullscreen = value != 0;
        else if (key == "graphics") progress.graphics = std::clamp(value, 0, 2);
        else if (key == "firstPerson") savedFirstPerson = value;
        else if (key == "viewVersion") viewVersion = value;
        else if (key == "graphicsVersion") graphicsVersion = value;
        else if (key == "sound") progress.sound = value != 0;
        else if (key == "soundLevel") progress.soundLevel = std::clamp(value, 0, 2);
        else if (key == "netTag") progress.netTag = value;
        else if (key == "leaderSkin") progress.leaderSkin = std::clamp(value, 0, kLeaderSkinCount - 1);
        else if (key == "format") format = value;
    }

    // Камер: 2-р хувилбараас 1-р этгээд анхдагч болсон. Хуучин хадгалалтын 3-р этгээдийн утгыг нэг удаа дарна;
    // үүнээс хойш тоглогчийн сонголт (V товч) хадгалагдана.
    progress.firstPerson = viewVersion >= 2 && savedFirstPerson >= 0 ? savedFirstPerson != 0 : true;
    // Өгөгдмөл график "бага" болсон: хуучин хадгалалтын тохиргоог нэг удаа бууруулна (дараа нь цэснээс өөрчилж болно).
    if (graphicsVersion < 2) progress.graphics = 0;

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
    file << "viewVersion 2\n";
    file << "fullscreen " << (progress.fullscreen ? 1 : 0) << '\n';
    file << "graphics " << progress.graphics << '\n';
    file << "graphicsVersion 2\n";
    file << "sound " << (progress.sound ? 1 : 0) << '\n';
    file << "soundLevel " << progress.soundLevel << '\n';
    file << "netTag " << progress.netTag << '\n';
    file << "leaderSkin " << progress.leaderSkin << '\n';

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

    file.close();
    WebPersistFile(SavePath().c_str()); // хөтөч: localStorage-д хадгална
    return !file.fail();
}
