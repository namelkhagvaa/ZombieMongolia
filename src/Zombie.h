#pragma once

#include "Campaign.h"
#include "Mover.h"
#include "Net.h"
#include "World.h"
#include "raylib.h"

#include <vector>

class Player;
class SoldierSquad;

enum class ZombieType
{
    Walker, // энгийн: доголж, найгаж алхана
    Runner, // хурдан, сул: урагш бөхийж гүйнэ
    Brute   // удаан, бат бөх: хүнд алхаатай
};

// Үеийн зомбины нөөц аймгийн даалгавруудад хуваагдана: даалгавар бүрийг өөрийн зомбинууд хамгаална
// (Mission). Нөөцийн дугаар = даалгаврын индекс (StageSave::kMaxObjectives хүртэл).
constexpr int kHordePools = 12;

struct Zombie
{
    Mover mover;
    ZombieType type = ZombieType::Walker;
    float health = 0.0f;
    float maxHealth = 0.0f;
    float speed = 0.0f;
    float damage = 0.0f;
    float scale = 1.0f;
    float attackCooldown = 0.0f;
    float attackAnim = 0.0f; // >0: гараа савж цохиж байна
    float stagger = 0.0f;    // >0: суманд оногдоод цочсон, удааширна
    float deathTimer = 0.0f; // үхсэнээс хойших хугацаа
    bool airborne = false;   // машинд мөргүүлээд агаарт шидэгдсэн
    Vector3 flight = { 0.0f, 0.0f, 0.0f }; // шидэгдсэн үеийн хурд
    float tumble = 0.0f;     // агаарт эргэлдэх өнцөг, градус
    float walkCycle = 0.0f;
    float gaitSpeed = 0.0f;  // газраар явж буй бодит хурд, м/с (хөлийн алхаа үүнтэй таарна)
    Vector3 lastPosition = { 0.0f, 0.0f, 0.0f };
    int reward = 0;
    int zone = 0;         // аль spawn бүсэд харьяалагдах (0 = бүсгүй, тэнүүчилсэн)
    int pool = -1;        // аль даалгаврын нөөцөөс гарсан (0..kHordePools-1), -1 = нөөцөөс бус (аварга, туршилт)
    bool alerted = false; // бай руугаа хөөж эхэлсэн; үгүй бол байрандаа зогсоно
    // Барилга руу нэвтрэх: сонгосон гарц (-1 = хаалга, бусад нь цонхны гарцын индекс), аль барилгад зориулсан
    int entryRoom = -1;
    int entryPassage = -1;
    float entryChosenAt = -100.0f;
    // Цонхоор авирч орж яваа: үлдсэн хугацаа, эхлэх ба хүрэх цэг
    float climbTimer = 0.0f;
    Vector3 climbFrom = { 0.0f, 0.0f, 0.0f };
    Vector3 climbTo = { 0.0f, 0.0f, 0.0f };
    float climbTop = 0.0f; // хашаа: орой (нумын өндөр); 0 = цонх
    bool boss = false;    // аварга: толгойн буудлага, хутга, машины цохилтод шууд үхэхгүй
    // Онцлог (жирийн зомбинд): бөөн зомби үхэхдээ дэлбэрнэ, орилогч хүн хармагц орилж холын зомбиудыг дуудна
    bool exploder = false;
    bool screamer = false;
    bool screamed = false;
    float screamTimer = 0.0f; // >0: одоо орилж байна (зочинд дуу)

    // Зомби бүрийн санамсаргүй гадаад төрх
    Color shirt = {};
    Color pants = {};
    Color skin = {};
    Color hair = {};
    bool missingArm = false;
    bool bloody = false;
    bool shirtless = false;
    bool openRibs = false;
    bool tornLeg = false;
    bool oneEye = false;
    bool headWound = false;
    int hairStyle = 0;
    float headRoll = 0.0f; // толгойн хазайлт, градус
    float limp = 0.0f;     // 0..1 баруун хөлөө чирэх хэмжээ
    int armStyle = 0;      // гараа барих хэлбэр

    // Хамтарсан тоглолт: тогтмол дугаар (хостын), зочин дээр хостоос ирсэн байрлал руу зөөлөн хөдөлнө.
    uint32_t id = 0;
    Vector3 netTarget = { 0.0f, 0.0f, 0.0f };
    float predicted = 0.0f; // зочин: өөрийн сумаар үхсэн гэж урьдчилан тооцсон (хост батлах хүртэл, секунд)
    float netFacing = 0.0f;
    float netSeen = 0.0f; // зочин: сүүлд snapshot-д орсноос хойш
    float netSent = -1.0f; // зочин: тэр snapshot-ыг хост илгээсэн цаг (хурдыг тооцоход)
    float netPrevSent = -1.0f;                  // зочин: өмнөх мэдээ (жигд шилжүүлэхэд)
    Vector3 netPrevTarget = { 0.0f, 0.0f, 0.0f };
    Vector3 netVelocity = { 0.0f, 0.0f, 0.0f }; // зочин: хоёр snapshot-оос тооцсон хурд (хоцролтыг нөхөж урагш таамаглана)
    bool netAttack = false;                     // зочин: өмнөх snapshot-д довтолж байсан (шинэ цохилтыг нэг л удаа эхлүүлнэ)

    bool IsAlive() const { return health > 0.0f; }
    float Radius() const { return 0.4f * scale; }
    float Height() const { return 1.92f * scale; } // толгойн оройг оруулаад
    Vector3 Chest() const;
};

struct ZombieHit
{
    int index = -1; // -1 = оноогүй
    float distance = 0.0f;
    bool headshot = false;
};

// Бүх зомбийг удирдана: хөдөлгөөн, дайралт, оногдолт, үхэл, зураглал.
class ZombieHorde
{
public:
    void Clear();
    // zone: харьяалагдах бүсийн дугаар. alerted = true бол шууд бай руугаа хөөнө. pool: аль даалгаврын нөөц.
    void Spawn(ZombieType type, Vector3 position, const MissionParams& mission, int zone = 0, bool alerted = true, int pool = -1);
    // Аварга зомби: асар их амьтай, том биетэй.
    void SpawnBoss(Vector3 position, const MissionParams& mission, int zone);
    // Тухайн бүсийн амьд зомбины тоо.
    int AliveInZone(int zone) const;
    // Тухайн бүсийн эхний maxTargets амьд зомбид хохирол учруулна (туршилтын автомат тоглолтод).
    void DamageZone(int zone, float amount, int maxTargets);

    // Хамгийн ойрын амьд бай (тоглогч эсвэл цэрэг) руу явж, хүрвэл цохино.
    // world нь тогтмол биш: зомби хаалттай хаалгыг нүдэж эвдэнэ.
    void Update(float dt, World& world, Player& player, SoldierSquad& squad);

    // minDistance-аас ойрхон оногдлыг тооцохгүй (камер, тоглогч хоёрын хоорондох зомби).
    ZombieHit Raycast(Ray ray, float minDistance, float maxDistance) const;
    int Nearest(Vector3 position, float maxDistance) const; // -1 = байхгүй
    int FindId(uint32_t id) const;                          // амьд зомбины индекс (-1 = байхгүй)
    void Damage(int index, float amount);
    // Бүрэн амины fraction хувийг авна (хутга: зомбины төрлөөс үл хамааран тогтмол тооны цохилт).
    void DamageFraction(int index, float fraction);
    // position-оос facing зүгт (радиан) байгаа, reach доторх хамгийн ойрын зомби; -1 = байхгүй.
    int NearestInFront(Vector3 position, float facing, float reach) const;
    // Дэлбэрэлт: radius доторх зомби бүр төвөөс холдох тусам бага хохирол авна. Үхсэн тоог буцаана.
    int Explode(Vector3 center, float radius, float maxDamage);
    // Тээврийн хэрэгслээр дайрах: radius доторх зомби бүрд damage хохирол. Хүрсэн зомбины тоог буцаана.
    // launch = true бол зомби шууд үхэж, push (унааны хурдны вектор) чиглэлд агаарт шидэгдэнэ.
    int Ram(Vector3 center, float radius, float damage, bool launch, Vector3 push);

    // --- Хамтарсан тоглолт ---
    // Хост: хоёр дахь тоглогч (зочин) — зомбинууд түүнийг ч бай болгоно; цохисон хохирлыг цуглуулна.
    void SetRemoteTarget(bool active, Vector3 position) { remoteActive = active; remotePosition = position; }
    float TakeRemoteDamage() { const float d = remoteDamage; remoteDamage = 0.0f; return d; }
    // Хост: зочны сум. Үхвэл true, coins-д шагнал, position-д байрлал.
    bool DamageById(uint32_t id, float amount, int& coins, Vector3& position);
    // Хост: зочинд илгээх ойрын зомбинууд
    void CollectStates(Vector3 around, float radius, std::vector<Net::ZombieState>& out) const;
    // Зочин: зомбинууд хостоос ирсэн мэдээллээр л хөдөлнө, хохирол хүлээж авахгүй (хост тооцно).
    void SetPuppet(bool active) { puppet = active; }
    bool IsPuppet() const { return puppet; }
    void ApplySnapshot(const std::vector<Net::ZombieState>& states, const MissionParams& mission, float sentAt = 0.0f);
    void UpdatePuppets(float dt);

    // Бүх зомбины хөдөлгөөний хурдны үржүүлэгч (устгасан тоогоор өснө: Game тохируулна).
    void SetSpeedMultiplier(float multiplier) { speedMultiplier = multiplier; }
    float SpeedMultiplier() const { return speedMultiplier; }

    // Сүүлд авснаас хойш устгасан зомбины шагналыг авч тэглэнэ.
    void TakeRewards(int& coins, int& kills);

    // Устгагдсан зомби бүрийн байрлал ба шагнал (дээр нь "+N" зоос хөвүүлэхэд).
    struct KillReward
    {
        Vector3 position;
        int coins;
    };
    void TakeKillRewards(std::vector<KillReward>& out)
    {
        out.insert(out.end(), killRewards.begin(), killRewards.end());
        killRewards.clear();
    }
    // Хамтарсан тоглолт (хост): true үед устгасан зомбины зоос зочинд очно (зочны унаа мөргөхөд).
    // Үеийн тоололд (нөөц) тоологдсоор.
    void SetCreditRemote(bool enabled) { creditRemote = enabled; }
    // Үхсэн бөөн зомбины дэлбэрэлтүүд (Game: гал, хохирол; зочинд зөвхөн харагдана)
    std::vector<Vector3> TakeExploderBlasts()
    {
        std::vector<Vector3> out;
        out.swap(exploderBlasts);
        return out;
    }
    void TakeRemoteRewards(std::vector<KillReward>& out)
    {
        out.insert(out.end(), remoteRewards.begin(), remoteRewards.end());
        remoteRewards.clear();
    }

    int AliveCount() const;
    int Count() const { return (int)zombies.size(); } // цогцсыг оруулаад

    // Сүүлийн дуудалтаас хойш устгагдсан зомбины тоо, хэмжээгээр (аварга тоологдохгүй).
    void TakeKillTypes(int& small, int& big);
    // Мөн даалгаврын нөөцөөр: kills[нөөц][0 = жижиг, 1 = том]-д нэмнэ.
    void TakePoolKills(int kills[kHordePools][2]);
    // Буун дуу, дэлбэрэлт: center-ээс radius доторх зомбинууд сэрж, дуу гарсан зүг рүү ирнэ.
    void AlertAround(Vector3 center, float radius);
    // Амьд зомбины тоо, хэмжээгээр (аварга тоологдохгүй).
    void CountAlive(int& small, int& big) const;
    // Даалгаврын нөөц бүрийн амьд зомби: alive[нөөц][0 = жижиг, 1 = том] (тэглээд тоолно).
    void CountAliveByPool(int alive[kHordePools][2]) const;
    // centers-ийн аль алинаас distance-аас хол байгаа амьд зомбийг шагналгүйгээр арилгана (үеийн нөөцөд буцна).
    // ambientOnly: зөвхөн тэнүүлч (бүс 0) зомби.
    void RemoveFar(const std::vector<Vector3>& centers, float distance, bool ambientOnly);
    const Zombie& At(int index) const { return zombies[index]; }

    // viewPosition-оос maxDistance-аас хол байгаа зомбийг зурахгүй.
    void Draw(Vector3 viewPosition, float maxDistance, bool showColliders) const;

private:
    std::vector<Zombie> zombies; // үхсэн нь хэдэн секунд газар хэвтээд арилна
    int pendingCoins = 0;
    int pendingKills = 0;
    float speedMultiplier = 1.0f;
    uint32_t nextId = 0;
    bool puppet = false;
    bool remoteActive = false;
    Vector3 remotePosition = { 0.0f, 0.0f, 0.0f };
    float remoteDamage = 0.0f;
    std::vector<KillReward> killRewards;
    std::vector<Vector3> exploderBlasts;
    // Зочин: хостын цагаар харуулж буй мөч (0.1 с хоцроосон), сүүлд ирсэн snapshot-ын цаг
    float netClock = -1.0f, netLatest = -1.0f;
    bool creditRemote = false;
    std::vector<KillReward> remoteRewards; // зочинд илгээх зоос
    float navClock = 0.0f; // орох гарцыг дахин сонгох хугацааг тоолно
    int pendingSmallKills = 0; // алхагч + гүйгч
    int pendingBigKills = 0;   // том зомби (аварга орохгүй)
    int pendingPoolKills[kHordePools][2] = {};
};
