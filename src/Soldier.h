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
    float health = 0.0f;
    float fireCooldown = 0.0f;
    float walkCycle = 0.0f;
    float moveAmount = 0.0f; // 1 = алхаа, >1 = гүйлт
    float recoil = 0.0f;     // 1 = дөнгөж буудсан
    float deathTimer = 0.0f;
    // Хаалгаар дамжих зам: аль өрөөнөөс аль өрөө рүү, хэддүгээр цэг рүү явж байгаа
    int routeFrom = -2, routeTo = -2, routeStage = 0;
    float bestDistance = 0.0f; // зорилт хүртэлх хамгийн бага зай (ахиц хэмжихэд)
    float stuckTimer = 0.0f;   // ахицгүй өнгөрсөн хугацаа
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
    Vector3 SlotPosition(int index, Vector3 playerPosition) const;

    std::vector<Soldier> soldiers;
};
