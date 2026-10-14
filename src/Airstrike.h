#pragma once

#include "raylib.h"

#include <algorithm>
#include <vector>

class Particles;
class World;
class ZombieHorde;

// Үе давсны дараах богино кино: холбоогоор агаарын цохилт дуудаж, гурван онгоц нутаг дэвсгэрийн дээгүүр
// нисэн өнгөрөхдөө үлдсэн зомбинуудыг бөмбөгдөнө. Дуусахад (эсвэл ENTER / товшилтоор алгасахад)
// бүх үлдсэн зомби устаж, ялалтын дэлгэц гарна.
class Airstrike
{
public:
    static constexpr float kDuration = 10.0f;

    // center: тоглогчийн байрлал (онгоцнууд дээгүүр нь нисэнэ), heading: нислэгийн зүг (радиан).
    void Start(Vector3 center, float heading);
    void Stop() { active = false; }
    bool Active() const { return active; }
    bool Finished() const { return active && time >= kDuration; }
    void Skip() { time = std::max(time, kDuration - 0.01f); }
    // Хамтарсан тоглолтын зочин алгасахгүй (хост дуусгана).
    void SetSkippable(bool value) { skippable = value; }
    bool Skippable() const { return skippable; }

    // Онгоц, бөмбөг хөдөлгөж, бөмбөг газарт хүрэхэд дэлбэлнэ. Төгсгөлд үлдсэн бүх зомбийг устгана.
    void Update(float dt, const World& world, ZombieHorde& horde, Particles& particles);

    Camera3D GetCamera() const { return camera; }
    void Draw() const;        // онгоц, бөмбөг (гэрэлтүүлэгтэй)
    void DrawOverlay() const; // киноны хар хүрээ, холбооны бичиг

private:
    struct Bomb
    {
        Vector3 from, to;
        float release; // хаях агшин (кино эхэлснээс хойш, секунд)
        bool dropped = false;
        bool exploded = false;
    };

    void Target(const ZombieHorde& horde); // бөмбөг хаях цэгүүд (эхний Update-д)
    Vector3 PlanePosition(int index, float at) const;
    void UpdateCamera(float dt);

    bool active = false;
    bool cleared = false;
    bool targeted = false;
    bool skippable = true;
    float time = 0.0f;
    float shake = 0.0f;
    Vector3 center = { 0.0f, 0.0f, 0.0f };
    Vector3 direction = { 0.0f, 0.0f, 1.0f }; // нислэгийн зүг (хэвтээ)
    Vector3 side = { 1.0f, 0.0f, 0.0f };
    float altitude = 0.0f;
    std::vector<Bomb> bombs;
    Camera3D camera = {};
};
