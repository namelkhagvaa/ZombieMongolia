#pragma once

#include "World.h"
#include "raylib.h"

#include <vector>

// Богино настай жижиг эффектүүд: цус, хонгио, сумны тоос, бууны амны гал, дэлбэрэлт.
// Цус, тоос, утаа, гал нь камер руу харсан зөөлөн зураг (billboard): процедурын текстураар ирмэг нь бүдэг,
// жигд биш хэлбэртэй, эргэлдэж тэлнэ. Газарт унасан цус, дэлбэрэлтийн түлэгдэлт газар дээрх толбо болж үлдэнэ.
class Particles
{
public:
    // at цэгээс direction чиглэлд цус үсэрнэ (дусал + манан); газар унаад толбо болж үлдэнэ.
    void Blood(Vector3 at, Vector3 direction, int count);
    // Сум газар, хананд тусахад гарах тоос.
    void Dust(Vector3 at);
    // Унаа, морины ард босох тоос: том, зөөлөн бөмбөлөг, аажим тэлж, хөөрч арилна.
    void Kickup(Vector3 at, Vector3 drift, Color color);
    // Буунаас хажуу тийш үсрэх хонгио.
    void Shell(Vector3 at, Vector3 sideDirection);
    // Бууны амны гал, металд тусах оч (хэдхэн frame).
    void Flash(Vector3 at, Vector3 direction);
    // Нэг гэрэлтэх дөл (шатаж буй торх, пуужингийн ар): дээш хөөрч, шараас улаан болж унтарна.
    void Flame(Vector3 at, Vector3 velocity, float size, float life);
    // Нэг утааны бөмбөлөг (шаталтын бараан утаа, пуужингийн цагаан мөр): дээш хөөрч тэлнэ.
    void Smoke(Vector3 at, float size, Color color);
    // Дэлбэрэлт: галт бөмбөлөг, оч, хэлтэрхий, газраар тархах тоос, хөөрөх утаа, түлэгдсэн ул мөр.
    void Explosion(Vector3 at);
    // Сүүлийн дэлбэрэлтийн төвүүд (унаа гэмтээхэд): авсны дараа цэвэрлэгдэнэ.
    void TakeExplosions(std::vector<Vector3>& out) { out.insert(out.end(), recentBlasts.begin(), recentBlasts.end()); recentBlasts.clear(); }

    void Update(float dt, const World& world);
    void Draw() const; // гэрэлтүүлэгтэй хэсэг (shader дотор): хэлтэрхий, хонгио
    // Хагас тунгалаг зөөлөн хэсэг (орчны shader-ийн гадна, 3D дотор): газрын толбо, цус, тоос, утаа.
    // light: тухайн цагийн гэрлийн өнгө (шөнө бараан)
    void DrawSoft(const Camera3D& camera, Color light) const;
    void DrawUnlit(const Camera3D& camera) const; // өөрөө гэрэлтдэг хэсэг: амны гал, дэлбэрэлтийн гал, оч
    void Clear();

private:
    enum class Kind
    {
        Blood,     // нисэх дусал -> газарт толбо
        BloodMist, // онох мөчийн улаан манан
        Dust,      // сум тусах, дэлбэрэлтийн тоос
        Shell,     // хэлтэрхий (жижиг шоо)
        Casing,    // буунаас үсрэх сум: assets/buu/sum загвар (байхгүй бол шоо)
        Puff,      // унааны тоос, яндангийн утаа
        Smoke,     // дэлбэрэлтийн бараан утаа (дээш хөөрнө)
        Fire,      // дэлбэрэлтийн гал (өөрөө гэрэлтэнэ)
        Flame,     // шаталтын жижиг дөл (өөрөө гэрэлтэнэ, босоо сунасан)
        Spark,     // оч (өөрөө гэрэлтэнэ)
        Scorch     // газар дээрх түлэгдсэн ул мөр
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
        bool settled;   // газар хүрээд зогссон
        Vector3 axis;   // Casing: эргэлдэх тэнхлэг
        float spin;     // Casing: эргэлтийн хурд, градус/с; зөөлөн хэсэгт: эргэлтийн хурд, рад/с
        float rotation; // зөөлөн хэсэг: зургийн эргэлт, рад
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
