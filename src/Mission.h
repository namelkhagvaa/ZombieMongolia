#pragma once

#include "Campaign.h"
#include "CampaignData.h"
#include "ObjectiveIcons.h"
#include "Player.h"
#include "Progress.h"
#include "Soldier.h"
#include "World.h"
#include "Zombie.h"
#include "raylib.h"

#include <algorithm>
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
    // Зочин: хостын (өрөөний) ахицыг авна: биелсэн даалгавар, аврагдсан хүн, дараагийн идэвхтэй даалгавар
    // (журнал, газрын зургийн тэмдэглэгээ хосттой ижил).
    void SyncFrom(const StageSave& host);
    // Ахицыг өөр хадгалалтад бичнэ (зочин ялж хост болоход: өрөөний ахиц тоглогчийн өөрийн хадгалалт болно)
    void BindSave(StageSave& stageSave) { save = &stageSave; }

    // interactPressed: E товч дарагдсан эсэх. onFoot: тоглогч явган (унаанаас харилцаж болохгүй).
    // Хамтарсан тоглолт: бусад тоглогчдын байрлал (бүс, хүрэх газар, хамгаалагчдын тооцоонд)
    void SetPartners(const std::vector<Vector3>& positions) { partners = positions; }
    void Update(float dt, World& world, Player& player, ZombieHorde& horde, SoldierSquad& squad, bool interactPressed,
                bool onFoot);

    // Зочин: Update-ийн оронд (даалгаврыг хост тооцно): ойрын объектын бичгийг харуулж, E дарвал хост руу илгээх
    // объектыг буцаана. active = false: бичиг гаргахгүй (унасан, тулаан).
    bool GuestInteract(Vector3 position, bool active, bool onFoot, bool pressed, int& objective, int& entity);
    // Хост: зочин объектод E дарав. Хост өөрөө шалгана: идэвхтэй даалгавар, зочин үнэхээр дэргэд нь. guestOwner: зочны дугаар
    bool PartnerInteract(int objective, int entity, Vector3 position, SoldierSquad& squad, int guestOwner);

    bool IsComplete() const;

    // Үеийн зомбины нөөц: ийм тооны зомби л гарч ирнэ; бүгдийг устгасны дараа шинээр гарахгүй.
    // Үе давахын тулд бүх даалгавар биелж, нөөц бүхэлдээ устгагдсан байх ёстой; дараа нь онгоц газрын
    // зураг дээр үлдсэн зомбийг бөмбөгдөх кино явна (Airstrike).
    void SetQuota(int small, int big)
    {
        quotaSmall = small;
        quotaBig = big;
    }
    int QuotaSmall() const { return quotaSmall; }
    int QuotaBig() const { return quotaBig; }
    int KilledSmall() const { return save ? std::min(save->killedSmall, quotaSmall) : 0; }
    int KilledBig() const { return save ? std::min(save->killedBig, quotaBig) : 0; }
    bool QuotaMet() const { return KilledSmall() >= quotaSmall && KilledBig() >= quotaBig; }


    // --- Даалгаврын хамгаалагч зомби ---
    // Үеийн нөөц аймгийн даалгавруудад тэнцүү хуваагдана: даалгавар бүрийг өөрийн зомбинууд хамгаална.
    // Баг (тоглогч эсвэл цэрэг) даалгаврын газарт ойртоход тэр нөөцөөс гарч ирж, бүр ойртоход дайрна.
    // Бусад газар тайван (тэнүүчлэх зомби байхгүй).
    int PoolQuota(int objective, bool big) const;
    // Даалгаврын нөөцийн үлдсэн зомби (устгагдаагүй, амьд байгааг оруулаад)
    int PoolLeft(int objective, bool big) const;
    // Тоглогч одоо байгаа хамгаалалттай даалгаврын газар (-1 = аль нэгнийх нь ойролцоо биш)
    int GuardedObjective() const { return guardedObjective; }

    bool TakeDirty();  // сүүлийн дуудалтаас хойш хадгалах зүйл гарсан бол true
    int TakeReward();  // сүүлийн дуудалтаас хойш олсон зоос

    const StageDef& Stage() const { return *stage; }
    int ProvinceIndex() const { return province; }
    int ObjectiveCount() const { return (int)states.size(); }
    const ObjectiveState& State(int index) const { return states[index]; }
    int CompletedCount() const;
    int FocusObjective() const;                  // одоо хийх ёстой (эхний идэвхтэй) даалгавар; -1 = байхгүй
    std::string ProgressText(int index) const;   // "2 / 3", "45 / 60 сек" гэх мэт; явцгүй төрөлд хоосон
    // Юу хийхийг нэг мөрөөр ("Хүмүүсийн дэргэд очиж E дар", "Үлдсэн 12 зомбийг устга"): HUD-ийн картад
    std::string HowTo(int index) const;
    // Бүс цэвэрлэх / аварга: устгах ёстой үлдсэн зомби (нөөц + бүсийн ойролцоох амьд); бусад төрөлд -1
    int ZoneRemaining(int index) const;
    float DistanceTo(int index, Vector3 from) const;

    const char* Prompt() const { return prompt.empty() ? nullptr : prompt.c_str(); } // "E — Генератор асаах"
    const char* Notice() const { return noticeTimer > 0.0f ? notice.c_str() : nullptr; }

    // Газрын зурагт: идэвхтэй даалгавруудын байршил (бүгд биелсний дараа хамгаалагч үлдсэн газрууд).
    void Markers(std::vector<Vector3>& out) const;
    // Мөн адил, төрөл ба нэртэй (газрын зураг, дэлгэц дээрх чиглүүлэгчид)
    void MapMarkers(std::vector<MapMarker>& out) const;
    float ProgressFraction(int index) const; // 0..1 (тоо, хугацаатай даалгавар); бусад төрөлд -1

    // --- Даалгаврыг ойлгомжтой болгох ---
    // Шинэ даалгавар нээгдэх, даалгавар биелэхэд дэлгэцийн дээд хэсэгт гарах баннер (дараалан нэг нэгээр)
    struct Banner
    {
        int objective;
        bool completed;
        float age = 0.0f;
    };
    const Banner* CurrentBanner() const { return banners.empty() || BriefingOpen() ? nullptr : &banners.front(); }
    int TakeBannerSound(); // 0 = байхгүй, 1 = шинэ даалгавар, 2 = биеллээ (Game дуу тоглуулна)
    // Баннер, танилцуулгын цаг, гацахад зөвлөгөө: frame бүрд (Update зогссон, зочин үед ч)
    void UpdatePresentation(float dt, Vector3 player);
    // Аймгийн танилцуулга: аймгийн нэр, зорилго, даалгаврын жагсаалт (эхлэхэд; дурын товчоор хаагдана)
    bool BriefingOpen() const { return briefingTimer > 0.0f; }
    void DismissBriefing() { briefingTimer = std::min(briefingTimer, 0.35f); }
    void SetBriefing(bool enabled) { briefingEnabled = enabled; }
    void DrawBanner() const;
    void DrawBriefing() const;

    void Draw(Vector3 viewPosition) const;                           // гэрэлтүүлэгтэй хэсэг
    void DrawBeacons(const World& world, Vector3 viewPosition) const; // чиглүүлэх тэмдэг (shader-ийн гадна)
    void DrawJournal() const;                                         // Q цонх (2D)

    // Туршилт: идэвхтэй даалгаврыг автоматаар гүйцэтгэнэ (тоглогчийг зөөж, бүсийн зомбийг устгана).
    // E дарах шаардлагатай мөчид true буцаана.
    bool AutoPlay(float dt, const World& world, Player& player, ZombieHorde& horde);

private:
    std::vector<Vector3> partners;
    void Activate(int index);
    void Complete(int index);
    // Объектыг ашиглав: аврах, цуглуулах, идэвхжүүлэх (guestOwner >= 0: аврагдсан цэрэг тэр зочны багт)
    void UseEntity(int objective, int entity, SoldierSquad& squad, int guestOwner);
    void PushBanner(int objective, bool completed);
    // Гацахад зөвлөгөө: одоогийн даалгаварт ойртохгүй, явц гарахгүй удвал газрын зургийг ашиглахыг сануулна
    void UpdateHint(float dt, Vector3 player);
    Vector3 ResolveSite(const World& world, SiteRef site) const;
    void UpdateZone(int index, float dt, const World& world, const Player& player, ZombieHorde& horde);
    // Баг ойртсон даалгаврын газарт хамгаалагч зомби гаргана, хол хоцорсныг нөөцөд буцаана.
    void UpdateGuards(float dt, const World& world, const Player& player, ZombieHorde& horde, const SoldierSquad& squad);
    bool InClearedZone(Vector3 position) const;
    // pool (даалгаврын индекс) нөөцөөс нэг зомби гаргана (том, жижгийг үлдсэн тооны харьцаагаар сонгоно).
    // Нөөц дууссан бол false.
    bool SpawnFromPool(ZombieHorde& horde, Vector3 position, int zone, bool alerted, int pool);

    const StageDef* stage = nullptr;
    StageSave* save = nullptr;
    MissionParams params = {};
    int province = 0;
    std::vector<ObjectiveState> states;
    int quotaSmall = 1500;  // алхагч + гүйгч
    int quotaBig = 300;     // том зомби
    int aliveSmall = 0;     // одоо амьд байгаа (frame бүрд шинэчлэгдэнэ)
    int aliveBig = 0;
    int alivePool[kHordePools][2] = {}; // даалгавар бүрийн нөөцөөс одоо амьд: [0] жижиг, [1] том
    std::vector<int> zoneNear;          // даалгавар бүрийн бүсийн ойролцоох амьд зомби (UpdateZone)
    int guardedObjective = -1;

    std::string prompt;
    std::string notice;
    std::vector<Banner> banners;
    int bannerSound = 0;
    float briefingTimer = 0.0f;
    bool briefingEnabled = true;
    bool syncedOnce = false;     // зочин: эхний синк (тэр үеийн нээгдсэн даалгавруудад баннер гаргахгүй)
    int hintObjective = -1;
    float hintTimer = 0.0f, hintBest = 1e9f;
    int hintProgress = -1;
    unsigned int hintShown = 0;  // зөвлөгөө гарсан даалгаврууд (нэг удаа)
    float noticeTimer = 0.0f;
    float guardTimer = 0.0f;
    float autoTimer = 0.0f;
    int rewardPending = 0;
    bool dirty = false;
};
