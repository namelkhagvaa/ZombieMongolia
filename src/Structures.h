#pragma once

#include "Layout.h"
#include "MeshBuilder.h"
#include "Obstacle.h"
#include "Terrain.h"

#include <vector>

// Талбарын хөдөлгөөнгүй объектуудыг байрлуулна. Хоёр хэсэгт хуваагдана:
//
//  1. Тогтмол хэсэг (тоглоом эхлэхэд нэг удаа): зам, цэргийн пост, агуулах, шатахуун түгээх газар,
//     зам дагуух шон ба орхигдсон машин, том чулуунууд.
//  2. Суурингууд (үе эхлэх бүрд): төв суурингийн хашаанууд, жижиг суурингууд, малчдын отрууд.
//     variant (аяны үеийн дугаар) бүрд байшингийн тоо, хэмжээ, өнгө, дээвэр, хашааны хуваарилалт өөр.
//
// Гаралт:
//  solid   - тунгалаг биш геометр
//  shadows - газар дээрх нарны сүүдэр (хагас тунгалаг)
//  glass   - торон хашаа зэрэг хагас тунгалаг гадаргуу
//  obstacles - мөргөлдөөний хайрцгууд (жагсаалтын төгсгөлд нэмэгдэнэ)
//  interiors, doors - дотогш орж болох барилгуудын дотор тал ба нээж хаадаг хаалганууд
//  passages - шат, цонх зэрэг тусгай гарцууд

// Мод, торх (scenery): хөнгөн хувилбар нь scenery builder-т (solid-д биш), байрлал нь placements-д. Ойрын зайд
// World үүнийг нууж текстуртай загвараар (VehicleModel.h SceneryModelId) зурна.
struct SceneryPlacement
{
    Vector3 base;       // ёроолын төв (газар)
    float height;       // өндөр, м (загвар 1 м)
    float yaw;          // эргэлт, радиан
    int kind;           // SceneryModelId: 0 шинэс, 1 хус, 2 хайлаас, 3 заг, 4 хуучин торх
    size_t first, count; // scenery builder-ийн гурвалжнууд
};

void BuildStaticStructures(const Layout& layout, const Terrain& terrain, std::vector<Obstacle>& obstacles,
                           MeshBuilder& solid, MeshBuilder& shadows, MeshBuilder& glass,
                           std::vector<Interior>& interiors, std::vector<Door>& doors,
                           std::vector<Passage>& passages, std::vector<WallSegment>& walls,
                           std::vector<FencePanel>& panels, MeshBuilder& scenery, std::vector<SceneryPlacement>& placements);

// Хотын байшингийн налуу дээвэр (хоёр налуу, дөрвөн налуу): дээвэр дээр алхах, нисдэг тэрэг буух өндөр
// (World::SurfaceHeightAt). Хананы оройг (World::RoofHeight) биш, дээврийн жинхэнэ гадаргууг.
struct PitchedRoof
{
    Vector2 center = { 0.0f, 0.0f };
    Vector2 axis = { 1.0f, 0.0f }; // нурууны (урт талын) чиглэл
    float reachLong = 0.0f, reachShort = 0.0f; // хаяаг оруулсан хагас хэмжээ
    float eave = 0.0f;      // энэ өндөрт slopeHalf зайд (нуруунаас хөндлөн)
    float rise = 0.0f;      // нуруу eave-ээс ийм өндөр
    float slopeHalf = 1.0f;
    float ridgeHalf = 1.0e6f; // дөрвөн налуу: нурууны хагас урт (үзүүр нь налуу); хоёр налуу: хязгааргүй
    // (x, z) дээрх дээврийн өндөр; дээвэр хүрэхгүй бол -1e9
    float HeightAt(float x, float z) const;
};
// BuildStaticStructures, BuildSettlementStructures тус бүр өөрийнхөө жагсаалтыг шинэчилнэ.
const std::vector<PitchedRoof>& StaticPitchedRoofs();
const std::vector<PitchedRoof>& SettlementPitchedRoofs();

// Улаанбаатарын хотод унаж болох машин тавих гудамжны зах (x, z, y = чиглэл радианаар). BuildStaticStructures
// хотын layout-аар дуудагдсаны дараа бөглөгдөнө.
const std::vector<Vector3>& CityVehicleSpots();

void BuildSettlementStructures(int variant, const Layout& layout, const Terrain& terrain,
                               std::vector<Obstacle>& obstacles, MeshBuilder& solid, MeshBuilder& shadows,
                               MeshBuilder& glass, std::vector<Interior>& interiors, std::vector<Door>& doors,
                           std::vector<Passage>& passages, std::vector<WallSegment>& walls,
                           std::vector<FencePanel>& panels, MeshBuilder& scenery, std::vector<SceneryPlacement>& placements);
