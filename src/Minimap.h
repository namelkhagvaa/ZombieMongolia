#pragma once

#include "Player.h"
#include "Soldier.h"
#include "Vehicle.h"
#include "World.h"
#include "Zombie.h"

#include <vector>

// Дэлгэцийн баруун доод буланд байрлах жижиг газрын зураг.
constexpr int kMinimapSize = 190;  // пиксел
constexpr int kMinimapMargin = 16;

// Тоглогч төвд байх ба зураг камертай хамт эргэнэ (дээд тал = камерын харж буй зүг).
// Зам, барилга, саад, цэргүүд (ногоон), зомбинууд (улаан) харагдана.
// markers: идэвхтэй даалгавруудын байршил (шар ромбо; хүрээнээс гарсан бол ирмэг дээр).
// medKits: эмнэлгийн хайрцгууд (улаан загалмай; хүрээнээс гарсан бол ирмэг дээр).
void DrawMinimap(const World& world, const Player& player, float cameraYaw, const ZombieHorde& horde,
                 const SoldierSquad& squad, const Garage& garage, const std::vector<Vector3>& markers,
                 const std::vector<Vector3>& medKits);

// M товчоор нээгдэх бүтэн дэлгэцийн газрын зураг. Хойд зүг үргэлж дээшээ.
// Хулганы дугуйгаар 100%..1000% томруулж, зүүн товчоор чирж зөөнө.
class FullMap
{
public:
    static constexpr float kMinZoom = 1.0f;  // 100%: аймгийн нутаг бүтнээрээ багтана
    static constexpr float kMaxZoom = 10.0f; // 1000%

    // Нээх үед 100%: тухайн аймгийн хилийн хэлбэр бүтнээрээ харагдана.
    void Open(const World& world);
    void Close() { open = false; }
    bool IsOpen() const { return open; }

    void SetZoom(float newZoom);
    float Zoom() const { return zoom; }

    // Дугуй, чирэлт, C (тоглогч дээр төвлөрөх) оролтыг боловсруулна.
    void Update(const World& world, const Player& player);
    void Draw(const World& world, const Player& player, const ZombieHorde& horde, const SoldierSquad& squad,
              const Garage& garage, const std::vector<Vector3>& markers, const std::vector<Vector3>& medKits,
              const char* title) const;

private:
    float BaseScale(const World& world) const; // 100% үеийн пиксел / метр

    bool open = false;
    float zoom = 1.0f;
    Vector2 focus = { 0.0f, 0.0f }; // дэлгэцийн төвд байх ертөнцийн цэг (x, z)
};
