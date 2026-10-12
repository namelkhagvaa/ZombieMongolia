#pragma once

#include "Particles.h"
#include "World.h"
#include "raylib.h"

#include <vector>

class Player;
class ZombieHorde;

// Шидсэн гранатууд: нум зурж нисэж, газарт ойж, хугацаа дуусахад дэлбэрнэ.
class Grenades
{
public:
    void Throw(Vector3 from, Vector3 velocity);
    // Пуужин: таталцалгүй шулуун нисэж, хана, газар, зомбид тусмагц дэлбэрнэ.
    void Launch(Vector3 from, Vector3 velocity);

    // Дэлбэрэхэд ойрын зомби, тоглогч хоёуланд зайнаас хамаарсан хохирол учруулна.
    void Update(float dt, const World& world, ZombieHorde& horde, Player& player, Particles& particles);
    void Draw() const;
    void Clear() { grenades.clear(); blasts.clear(); }
    // Сүүлд авснаас хойших дэлбэрэлтүүд (байрлал, радиус): бензинтэй торхнуудыг дэлбэлэхэд.
    struct BlastEvent
    {
        Vector3 position;
        float radius;
        float damage;
    };
    std::vector<BlastEvent> TakeBlasts()
    {
        std::vector<BlastEvent> out;
        out.swap(blasts);
        return out;
    }

private:
    struct Grenade
    {
        Vector3 position;
        Vector3 velocity;
        float fuse;
        bool rocket = false;
        float trail = 0.0f; // утааны дараагийн хэсэг гарах хүртэлх хугацаа
    };

    std::vector<Grenade> grenades;
    std::vector<BlastEvent> blasts;
};
