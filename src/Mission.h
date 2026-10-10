#pragma once

#include "Campaign.h"
#include "CampaignData.h"
#include "Player.h"
#include "Progress.h"
#include "Soldier.h"
#include "World.h"
#include "Zombie.h"
#include "raylib.h"

#include <string>
#include <vector>

enum class ObjectiveStatus
{
    Locked, // урьдчилсан нөхцөл биелээгүй
    Active, // одоо биелүүлж болно
    Done
};

// Даалгаврын объект: аврах хүн, цуглуулах зүйл, идэвхжүүлэх төхөөрөмж, хүлээлгэн өгөх цэг.
struct MissionEntity
{
    Vector3 position = {};
    float facing = 0.0f;
    bool used = false; // аврагдсан / авагдсан / идэвхжсэн
};

struct ObjectiveState
{
    ObjectiveStatus status = ObjectiveStatus::Locked;
    Vector3 location = {};  // даалгаврын байршил (газар дээрх цэг)
    float radius = 0.0f;    // хүрэх / хамгаалах / цэвэрлэх бүсийн радиус
    std::vector<MissionEntity> entities;
    int progress = 0;       // аварсан / цуглуулсан тоо, эсвэл хамгаалсан секунд
    int target = 1;
    float timer = 0.0f;     // хамгаалсан хугацаа
    float spawnTimer = 0.0f;
    int reserve = 0;        // бүсэд гарч ирээгүй үлдсэн зомбины нөөц
    bool spawned = false;   // харуул зомби / аварга гарсан эсэх
};

// Нэг үеийн (аймгийн) даалгаврын ажиллах төлөв. Өгөгдөл нь CampaignData-аас, явц нь StageSave-д хадгалагдана.
class Mission
{
public:
    // save: энэ аймгийн хадгалагдсан явц; Mission үүнийг шууд шинэчилнэ (Game файл руу бичнэ).
    void Start(int provinceIndex, const MissionParams& params, const World& world, StageSave& save);

    // interactPressed: E товч дарагдсан эсэх. onFoot: тоглогч явган (унаанаас харилцаж болохгүй).
    void Update(float dt, World& world, Player& player, ZombieHorde& horde, SoldierSquad& squad, bool interactPressed,
                bool onFoot);

    bool IsComplete() const;
    bool TakeDirty();  // сүүлийн дуудалтаас хойш хадгалах зүйл гарсан бол true
    int TakeReward();  // сүүлийн дуудалтаас хойш олсон зоос

    const StageDef& Stage() const { return *stage; }
    int ProvinceIndex() const { return province; }
    int ObjectiveCount() const { return (int)states.size(); }
    const ObjectiveState& State(int index) const { return states[index]; }
    int CompletedCount() const;
    int FocusObjective() const;                  // одоо хийх ёстой (эхний идэвхтэй) даалгавар; -1 = байхгүй
    std::string ProgressText(int index) const;   // "2 / 3", "45 / 60 сек" гэх мэт; явцгүй төрөлд хоосон
    float DistanceTo(int index, Vector3 from) const;

    const char* Prompt() const { return prompt.empty() ? nullptr : prompt.c_str(); } // "E — Генератор асаах"
    const char* Notice() const { return noticeTimer > 0.0f ? notice.c_str() : nullptr; }

    // Газрын зурагт: идэвхтэй даалгавруудын байршил.
    void Markers(std::vector<Vector3>& out) const;

    void Draw(Vector3 viewPosition) const;                           // гэрэлтүүлэгтэй хэсэг
    void DrawBeacons(const World& world, Vector3 viewPosition) const; // чиглүүлэх тэмдэг (shader-ийн гадна)
    void DrawJournal() const;                                         // Q цонх (2D)

    // Туршилт: идэвхтэй даалгаврыг автоматаар гүйцэтгэнэ (тоглогчийг зөөж, бүсийн зомбийг устгана).
    // E дарах шаардлагатай мөчид true буцаана.
    bool AutoPlay(float dt, const World& world, Player& player, ZombieHorde& horde);

private:
    void Activate(int index);
    void Complete(int index);
    Vector3 ResolveSite(const World& world, SiteRef site) const;
    void UpdateZone(int index, float dt, const World& world, const Player& player, ZombieHorde& horde);
    void UpdateAmbient(float dt, const World& world, const Player& player, ZombieHorde& horde);
    bool InClearedZone(Vector3 position) const;

    const StageDef* stage = nullptr;
    StageSave* save = nullptr;
    MissionParams params = {};
    int province = 0;
    std::vector<ObjectiveState> states;

    std::string prompt;
    std::string notice;
    float noticeTimer = 0.0f;
    float ambientTimer = 0.0f;
    float autoTimer = 0.0f;
    int rewardPending = 0;
    bool dirty = false;
};
