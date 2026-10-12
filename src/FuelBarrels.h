#pragma once

#include "Particles.h"
#include "World.h"
#include "raylib.h"

#include <vector>

class Player;
class SoldierSquad;
class ZombieHorde;

// Бензинтэй улаан торхнууд: талбараар тархсан, буудах, гранат, пуужин, унаагаар мөргөхөд дэлбэрнэ.
// Дэлбэрэлт нь ойрын зомби, тоглогч, цэргүүдэд хохирол учруулж, хөрш торхнуудыг гинжин байдлаар дэлбэлнэ.
class FuelBarrels
{
public:
    // Даалгаврын эхэнд: аймаг бүрт өөр байрлалд (seed), замын хажуу, суурин, шатахуун түгээх газар, заставын орчимд.
    void Spawn(const World& world, int seed);
    void Clear() { barrels.clear(); exploded.clear(); }

    // Сумны туяа торхонд тусах эсэх: тусвал индекс, зайг буцаана (-1 = үгүй).
    int Raycast(Ray ray, float minDistance, float maxDistance, float& distance) const;
    // Сумны хохирол: ам нь цоорч, хэсэг хугацааны дараа (эсвэл шууд) дэлбэрнэ.
    void Damage(int index, float amount);
    // Гаднын дэлбэрэлт (гранат, пуужин): radius доторх торхнууд дэлбэрнэ.
    void Blast(Vector3 center, float radius);

    void Update(float dt, ZombieHorde& horde, Player& player, SoldierSquad& squad, Particles& particles);
    // Тоглогчийг торхноос түлхэнэ (дундуур нь гарахгүй).
    void PushOut(Vector3& position, float radius) const;
    // Хурдтай унаа мөргөвөл дэлбэрнэ.
    void Ram(Vector3 center, float radius, float speed);
    // Хамтарсан тоглолт: хост дээр дэлбэрсэн торхны индексүүд (зочинд илгээнэ), зочин дээр шууд дэлбэлэх.
    void TakeExploded(std::vector<int>& out) { out.insert(out.end(), exploded.begin(), exploded.end()); exploded.clear(); }
    void Trigger(int index)
    {
        if (index >= 0 && index < (int)barrels.size() && !barrels[(size_t)index].exploded) barrels[(size_t)index].fuse = 0.0f;
    }

    void Draw(Vector3 viewPosition) const;      // гэрэлтүүлэгтэй
    void DrawUnlit(Vector3 viewPosition) const; // дэлбэрсний дараах гал

private:
    struct Barrel
    {
        Vector3 position;   // ёроолын төв
        float health = 25.0f;
        float fuse = -1.0f; // >= 0 бол дэлбэрэх хүртэлх хугацаа (гинжин дэлбэрэлт, цоорсон торх)
        bool exploded = false;
        float burn = 0.0f;  // дэлбэрсний дараа шатах хугацаа
        float smoke = 0.0f; // утааны дараагийн хэсэг хүртэл
        float tilt = 0.0f;  // бага зэрэг хазайсан (санамсаргүй)
    };

    void Explode(Barrel& barrel, ZombieHorde& horde, Player& player, SoldierSquad& squad, Particles& particles);

    std::vector<Barrel> barrels;
    std::vector<int> exploded;
};
