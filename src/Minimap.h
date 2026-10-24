#pragma once

#include "Player.h"
#include "Soldier.h"
#include "Vehicle.h"
#include "World.h"
#include "Zombie.h"

#include <vector>

class Navigator;
// Газрын зураг дээр харуулах хөтлөх зам (nullptr бол харуулахгүй)
void SetMapNavigator(const Navigator* navigator);

// Хамтарсан тоглолт / тулаан: газрын зураг дээрх хүмүүс (өөрийн баг ногоон, эсрэг баг улбар шар; нөгөө бодит
// тоглогч цагаан хүрээтэй том). nullptr бол багийн цэргүүдийг (SoldierSquad) ердийнхөөр зурна.
struct MapPerson
{
    Vector3 position;
    bool enemy;
    bool human; // нөгөө бодит тоглогч
};
void SetMapPeople(const std::vector<MapPerson>* people);

// Дэлгэцийн баруун доод буланд байрлах жижиг газрын зураг.
constexpr int kMinimapSize = 190;  // пиксел
constexpr int kMinimapTouchSize = 150; // мэдрэгчтэй дэлгэцэд (дээд мөр шахцалдахгүй)
constexpr int kMinimapMargin = 16;
int MinimapSize(); // одоогийн горимд (мэдрэгчтэй эсэх)

// Тоглогч төвд байх ба зураг камертай хамт эргэнэ (дээд тал = камерын харж буй зүг).
// Зам, барилга, саад, цэргүүд (ногоон), зомбинууд (улаан) харагдана.
// markers: идэвхтэй даалгавруудын байршил (шар ромбо; хүрээнээс гарсан бол ирмэг дээр).
// medKits: эмнэлгийн хайрцгууд (улаан загалмай; хүрээнээс гарсан бол ирмэг дээр).
void DrawMinimap(const World& world, const Player& player, float cameraYaw, const ZombieHorde& horde,
                 const SoldierSquad& squad, const Garage& garage, const std::vector<Vector3>& markers,
                 const std::vector<Vector3>& medKits);

// M товчоор нээгдэх бүтэн дэлгэцийн газрын зураг. Хойд зүг үргэлж дээшээ.
// Хулганы дугуй / хоёр хуруугаар чимхэж (pinch) томруулж, зүүн товч / нэг хуруугаар чирж зөөнө.
class FullMap
{
public:
    static constexpr float kMinZoom = 1.0f;  // 100%: аймгийн нутаг бүтнээрээ багтана
    static constexpr float kMaxZoom = 40.0f; // 4000%: гудамж, барилга тод
    static constexpr float kOpenSpan = 1600.0f; // нээх үед тоглогчийн эргэн тойронд ийм өргөн (метр) харагдана

    // Тоглогч дээр төвлөрч, ойроос (~1.6 км) нээгдэнэ; холдуулбал (100%) аймгийн нутаг бүтнээрээ.
    void Open(const World& world, const Player& player);
    void Close() { open = false; }
    bool IsOpen() const { return open; }

    void SetZoom(float newZoom);
    float Zoom() const { return zoom; }

    // Дугуй, чирэлт, C (тоглогч дээр төвлөрөх) оролтыг боловсруулна. Чирэлгүй товшилт = очих газар тэмдэглэх.
    void Update(const World& world, const Player& player);
    // Товшиж тэмдэглэсэн очих газар (нэг удаа). clear = true бол тэмдэглэгээг арилгах хүсэлт.
    bool TakeWaypoint(Vector2& point, bool& clear);
    void Draw(const World& world, const Player& player, const ZombieHorde& horde, const SoldierSquad& squad,
              const Garage& garage, const std::vector<Vector3>& markers, const std::vector<Vector3>& medKits,
              const char* title) const;

private:
    float BaseScale(const World& world) const; // 100% үеийн пиксел / метр

    bool open = false;
    float zoom = 1.0f;
    bool pressing = false;          // зүүн товч дарагдсан (товшилт эсвэл чирэлт)
    Vector2 pressAt = { 0.0f, 0.0f };
    float dragged = 0.0f;           // дарснаас хойш хөдөлсөн зай, пиксел
    bool hasWaypoint = false, clearWaypoint = false;
    Vector2 waypoint = { 0.0f, 0.0f };
    Vector2 focus = { 0.0f, 0.0f }; // дэлгэцийн төвд байх ертөнцийн цэг (x, z)
};
