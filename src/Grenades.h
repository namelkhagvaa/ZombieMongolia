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

    // Дэлбэрэхэд ойрын зомби, тоглогч хоёуланд зайнаас хамаарсан хохирол учруулна.
    void Update(float dt, const World& world, ZombieHorde& horde, Player& player, Particles& particles);
    void Draw() const;
    void Clear() { grenades.clear(); }

private:
    struct Grenade
    {
        Vector3 position;
        Vector3 velocity;
        float fuse;
    };

    std::vector<Grenade> grenades;
};
