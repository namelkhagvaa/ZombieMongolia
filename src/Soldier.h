#pragma once

#include "Mover.h"
#include "Particles.h"
#include "Tracers.h"
#include "World.h"
#include "raylib.h"

#include <utility>
#include <vector>

class ZombieHorde;

struct Soldier
{
    Mover mover;
    int member = -1;         // багийн гишүүний дугаар (0..2); -1 = элсүүлсэн жирийн цэрэг
    bool guestTeam = false;  // хамтарсан тоглолт: зочны багийн гишүүн (зочныг дагана, хостын F1-F3-т орохгүй)
    float health = 0.0f;
    float maxHealth = 80.0f;
    float fireCooldown = 0.0f;
    float walkCycle = 0.0f;
    float moveAmount = 0.0f; // 1 = алхаа, >1 = гүйлт
    float speed = 0.0f;      // одоогийн хурд, м/с: зорилтот хурд руу зөөлөн шилжинэ (EaseSpeed)
    float recoil = 0.0f;     // 1 = дөнгөж буудсан
    float aimHold = 0.0f;    // >0 үед буугаа онилсон хэвээр барина (буудсаны дараа, тулааны үед)
    float aimPose = 0.0f;    // 0 = бэлэн барьсан .. 1 = онилсон (зөөлөн шилжинэ)
    float deathTimer = 0.0f;
    // Хаалгаар дамжих зам: аль өрөөнөөс аль өрөө рүү, хэддүгээр цэг рүү явж байгаа
    int routeFrom = -2, routeTo = -2, routeStage = 0;
    float bestDistance = 0.0f; // зорилт хүртэлх хамгийн бага зай (ахиц хэмжихэд)
    float stuckTimer = 0.0f;   // ахицгүй өнгөрсөн хугацаа
    int stuckCount = 0;        // дараалан гацсан тоо (тойрох зүгээ сольсон ч ахицгүй)
    // "Энд бай": багийн гишүүн тоглогчийг дагахгүй, holdPoint дээр зогсож тэндээсээ зомби устгана.
    bool holding = false;
    Vector3 holdPoint = { 0.0f, 0.0f, 0.0f };
    // Багийн гишүүний бие даасан тулаан: дараагийн байр сонгох хүртэлх хугацаа, сонгосон байр
    float tacticTimer = 0.0f;
    Vector3 tacticPoint = { 0.0f, 0.0f, 0.0f };
    bool shifting = false;     // сонгосон байр руугаа шилжиж яваа
    bool fighting = false;     // зомбитой бие даан байлдаж байгаа (тоглогчийг дагахгүй)
    float blockedTime = 0.0f;  // бай харагдахгүй (хана, толгод халхалсан) өнгөрсөн хугацаа
    // Элсүүлсэн цэргийн харуул: удирдагчийн эргэн тойронд сонгосон байр, гадагш ажиглах зүг
    Vector3 guardPost = { 0.0f, 0.0f, 0.0f };
    Vector3 guardAnchor = { 0.0f, 0.0f, 0.0f }; // байр сонгох үеийн удирдагчийн байрлал
    float guardTimer = 0.0f;   // шинэ байр сонгох хүртэлх хугацаа
    float guardWatch = 0.0f;   // ажиглах зүг, радиан
    // Унаа: тоглогч унаа унахад багийн гишүүд ойрын хэрэгсэл рүү очиж сууна (Game удирдана).
    int riding = -1;           // сууж яваа хэрэгслийн индекс; -1 = явган
    bool boarding = false;     // boardPoint дахь хэрэгсэл рүү гүйж яваа
    Vector3 boardPoint = { 0.0f, 0.0f, 0.0f };
    int seatStyle = 0;         // 1 = мотоцикл, 2 = эмээл, 3 = бүхээг дотор (харагдахгүй), 4 = тэвшинд
    Vector3 seat = { 0.0f, 0.0f, 0.0f }; // ташааны ертөнцийн байрлал
    float seatHeading = 0.0f;  // унааны зүг (буудахад бие эргэх ч ташаа, хөл энэ зүгт үлдэнэ)
    bool hasRein = false;      // жолооч: зүүн гар жолоо (бариул, хазаар) барина
    Vector3 rein = { 0.0f, 0.0f, 0.0f };
    bool moving = false;
    bool alive = true;
    // Хамтарсан тоглолт: буудсан сумны тоолуур ба сүүлийн сумны туссан цэг (зочинд сумны мөр зурахад)
    uint8_t shots = 0;
    Vector3 shotEnd = { 0.0f, 0.0f, 0.0f };
    // Тулаан: тасарсан / хөдлөхгүй тоглогчийн дүр AI болоход эхний хугацаанд хөдлөхгүй (секунд)
    float frozen = 0.0f;
};

// Тулаан (нээлттэй өрөө): AI дайчин буудаж болох хүн (байрлал = цээж). id: squad-ийн индекс, эсвэл kHostTarget /
// kGuestTarget (хостын тоглогч, зочны дүр).
struct HumanTarget
{
    Vector3 chest;
    int id;
};
struct HumanHit
{
    int id;
    float damage;
};
constexpr int kHostTarget = -1, kGuestTarget = -2;

// Элсүүлсэн цэргүүд: удирдагчийн эргэн тойронд харуул хамгаалалтын байр эзэлж, зомбитой бие даан байлдана.
// Багийн гишүүд: удирдагчийг зайтай дагаж, түүнд ойртсон зомбийг түрүүлж буудна.
class SoldierSquad
{
public:
    static constexpr float kMaxHealth = 80.0f;

    // Даалгаврын эхэнд count цэргийг тоглогчийн эргэн тойронд байрлуулна.
    void Reset(int count, Vector3 around, const World& world);
    // Аврагдсан цэргийг багт нэмнэ (тухайн даалгавар дуустал).
    void Add(Vector3 position, float facing);
    // Тоглогчийн удирдаагүй багийн гишүүнийг нэмнэ (дагаж, хамгаална).
    void AddMember(int member, Vector3 position, float facing, float health, float maxHealth);
    // Багийн гишүүний индекс; -1 = байхгүй (тоглогч өөрөө удирдаж байгаа). Зочны багийнхныг тооцохгүй.
    int FindMember(int member) const;
    // Хамтарсан тоглолт: зочны удирдаагүй хоёр дайчин (зочныг дагаж байлдана)
    void AddGuestTeam(int controlledMember, Vector3 around, const World& world, float health = 100.0f);
    void RemoveGuestTeam();
    bool HasGuestTeam() const;
    // Зочны багийнхны дагах байрлал (зочин холбогдсон үед)
    void SetGuestLeader(bool active, Vector3 position) { guestLeaderActive = active; guestLeader = position; }
    // world нь тогтмол биш: цэргүүд замдаа таарсан хаалгыг онгойлгоно.
    void Update(float dt, World& world, Vector3 playerPosition, ZombieHorde& horde, Tracers& tracers,
                Particles& particles);
    void Damage(int index, float amount);
    // Тулаан: багийн гишүүд (элсүүлсэн цэрэг биш) эсрэг багийн хүмүүс рүү ч буудна. hostSide: хостын багийн бай
    // (зочны дайчид буудна), guestSide: зочны багийн бай (хостын дайчид буудна). Оносон хохирлыг TakeHumanHits.
    void SetBattle(bool active, std::vector<HumanTarget> hostSide, std::vector<HumanTarget> guestSide)
    {
        battle = active;
        hostTargets = std::move(hostSide);
        guestTargets = std::move(guestSide);
    }
    std::vector<HumanHit> TakeHumanHits() { return std::exchange(humanHits, {}); }
    // Тулаанд: тасарсан зочны дүрийг зочны багийн AI дайчин болгоно (frozen секунд хөдлөхгүй).
    void AddGuestFighter(int member, Vector3 position, float facing, float health, float frozen);
    // viewPosition: камерын байрлал (холын цэргийг хялбар хувилбараар зурна).
    void Draw(Vector3 viewPosition, bool showColliders) const;
    // 1-р дайчны дүр: өөрийн баг ба (хамтарсан тоглолтод) зочны баг
    void SetLeaderSkins(int own, int guest)
    {
        leaderSkin = own;
        guestLeaderSkin = guest;
    }

    std::vector<Soldier>& All() { return soldiers; }
    const std::vector<Soldier>& All() const { return soldiers; }
    int Total() const { return (int)soldiers.size(); }
    int AliveCount() const;

private:
    int leaderSkin = 0, guestLeaderSkin = 0;
    Vector3 SlotPosition(int index, Vector3 playerPosition, float radius) const;
    // Элсүүлсэн цэргийн дараагийн харуулын байр: өөрийн хэсэг (сектор) дотор, удирдагчаас 6-12 м.
    void ChooseGuardPost(int index, Vector3 playerPosition, const World& world);

    std::vector<Soldier> soldiers;
    bool battle = false;
    std::vector<HumanTarget> hostTargets, guestTargets;
    std::vector<HumanHit> humanHits;
    bool guestLeaderActive = false;
    Vector3 guestLeader = { 0.0f, 0.0f, 0.0f };
};
