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
    // Унаа, морины ард босох тоос: том, зөөлөн бөмбөлөг, аажим тэлж, хөөрч арилна.
    void Kickup(Vector3 at, Vector3 drift, Color color);
    // Буунаас хажуу тийш үсрэх хонгио.
    void Shell(Vector3 at, Vector3 sideDirection);
    // Бууны амны гал (хэдхэн frame).
    void Flash(Vector3 at, Vector3 direction);
    // Гранатын дэлбэрэлт: галт бөмбөлөг, тоос шороо, хэлтэрхий.
    void Explosion(Vector3 at);
    // Сүүлийн дэлбэрэлтийн төвүүд (унаа гэмтээхэд): авсны дараа цэвэрлэгдэнэ.
    void TakeExplosions(std::vector<Vector3>& out) { out.insert(out.end(), recentBlasts.begin(), recentBlasts.end()); recentBlasts.clear(); }

    void Update(float dt, const World& world);
    void Draw() const;      // гэрэлтүүлэгтэй хэсэг (shader дотор)
    void DrawPuffs() const; // хагас тунгалаг тоос (бусад зүйлсийн дараа)
    void DrawUnlit() const; // өөрөө гэрэлтдэг хэсэг (shader-ийн гадна)
    void Clear();

private:
    enum class Kind
    {
        Blood,
        Dust,
        Shell,  // хэлтэрхий (жижиг шоо)
        Casing, // буунаас үсрэх сум: assets/buu/sum загвар (байхгүй бол шоо)
        Puff
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
        Vector3 axis; // Casing: эргэлдэх тэнхлэг
        float spin;   // Casing: эргэлтийн хурд, градус/с
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
    std::vector<Vector3> recentBlasts;
};
