#pragma once

#include "Mover.h"
#include "Particles.h"
#include "Tracers.h"
#include "World.h"
#include "raylib.h"

#include <vector>

class ZombieHorde;

struct Soldier
{
    Mover mover;
    int member = -1;         // багийн гишүүний дугаар (0..2); -1 = элсүүлсэн жирийн цэрэг
    float health = 0.0f;
    float maxHealth = 80.0f;
    float fireCooldown = 0.0f;
    float walkCycle = 0.0f;
    float moveAmount = 0.0f; // 1 = алхаа, >1 = гүйлт
    float recoil = 0.0f;     // 1 = дөнгөж буудсан
    float aimHold = 0.0f;    // >0 үед буугаа онилсон хэвээр барина (буудсаны дараа, тулааны үед)
    float aimPose = 0.0f;    // 0 = бэлэн барьсан .. 1 = онилсон (зөөлөн шилжинэ)
    float deathTimer = 0.0f;
    // Хаалгаар дамжих зам: аль өрөөнөөс аль өрөө рүү, хэддүгээр цэг рүү явж байгаа
    int routeFrom = -2, routeTo = -2, routeStage = 0;
    float bestDistance = 0.0f; // зорилт хүртэлх хамгийн бага зай (ахиц хэмжихэд)
    float stuckTimer = 0.0f;   // ахицгүй өнгөрсөн хугацаа
    // Багийн гишүүний бие даасан тулаан: дараагийн байр сонгох хүртэлх хугацаа, сонгосон байр
    float tacticTimer = 0.0f;
    Vector3 tacticPoint = { 0.0f, 0.0f, 0.0f };
    bool shifting = false;     // сонгосон байр руугаа шилжиж яваа
    bool fighting = false;     // зомбитой бие даан байлдаж байгаа (тоглогчийг дагахгүй)
    float blockedTime = 0.0f;  // бай харагдахгүй (хана, толгод халхалсан) өнгөрсөн хугацаа
    // Унаа: тоглогч унаа унахад багийн гишүүд ойрын хэрэгсэл рүү очиж сууна (Game удирдана).
    int riding = -1;           // сууж яваа хэрэгслийн индекс; -1 = явган
    bool boarding = false;     // boardPoint дахь хэрэгсэл рүү гүйж яваа
    Vector3 boardPoint = { 0.0f, 0.0f, 0.0f };
    int seatStyle = 0;         // 1 = мотоцикл, 2 = эмээл, 3 = бүхээг дотор (харагдахгүй), 4 = тэвшинд
    Vector3 seat = { 0.0f, 0.0f, 0.0f }; // ташааны ертөнцийн байрлал
    bool moving = false;
    bool alive = true;
};

// Элсүүлсэн цэргүүд: тоглогчийг дагаж, ойрын зомбийг буудна.
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
    // Багийн гишүүний индекс; -1 = байхгүй (тоглогч өөрөө удирдаж байгаа).
    int FindMember(int member) const;
    // world нь тогтмол биш: цэргүүд замдаа таарсан хаалгыг онгойлгоно.
    void Update(float dt, World& world, Vector3 playerPosition, ZombieHorde& horde, Tracers& tracers,
                Particles& particles);
    void Damage(int index, float amount);
    // viewPosition: камерын байрлал (холын цэргийг хялбар хувилбараар зурна).
    void Draw(Vector3 viewPosition, bool showColliders) const;

    std::vector<Soldier>& All() { return soldiers; }
    const std::vector<Soldier>& All() const { return soldiers; }
    int Total() const { return (int)soldiers.size(); }
    int AliveCount() const;

private:
    Vector3 SlotPosition(int index, Vector3 playerPosition, float radius) const;

    std::vector<Soldier> soldiers;
};
