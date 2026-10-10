#pragma once

#include "World.h"
#include "raylib.h"

#include <vector>

// Богино настай жижиг эффектүүд: цус, хонгио, сумны тоос, бууны амны гал.
class Particles
{
public:
    // at цэгээс direction чиглэлд цус үсэрнэ; газар унаад толбо болж үлдэнэ.
    void Blood(Vector3 at, Vector3 direction, int count);
    // Сум газар, хананд тусахад гарах тоос.
    void Dust(Vector3 at);
    // Буунаас хажуу тийш үсрэх хонгио.
    void Shell(Vector3 at, Vector3 sideDirection);
    // Бууны амны гал (хэдхэн frame).
    void Flash(Vector3 at, Vector3 direction);
    // Гранатын дэлбэрэлт: галт бөмбөлөг, тоос шороо, хэлтэрхий.
    void Explosion(Vector3 at);

    void Update(float dt, const World& world);
    void Draw() const;      // гэрэлтүүлэгтэй хэсэг (shader дотор)
    void DrawUnlit() const; // өөрөө гэрэлтдэг хэсэг (shader-ийн гадна)
    void Clear();

private:
    enum class Kind
    {
        Blood,
        Dust,
        Shell
    };

    struct Particle
    {
        Kind kind;
        Vector3 position;
        Vector3 velocity;
        float size;
        float life;
        float maxLife;
        Color color;
        bool settled; // газар хүрээд зогссон
    };

    struct MuzzleFlash
    {
        Vector3 position;
        Vector3 direction;
        float life;
    };

    struct Blast
    {
        Vector3 position;
        float life;
    };

    void Add(const Particle& particle);

    std::vector<Particle> particles;
    std::vector<MuzzleFlash> flashes;
    std::vector<Blast> blasts;
};
