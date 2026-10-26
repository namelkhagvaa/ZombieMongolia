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

void BuildStaticStructures(const Layout& layout, const Terrain& terrain, std::vector<Obstacle>& obstacles,
                           MeshBuilder& solid, MeshBuilder& shadows, MeshBuilder& glass,
                           std::vector<Interior>& interiors, std::vector<Door>& doors,
                           std::vector<Passage>& passages, std::vector<WallSegment>& walls,
                           std::vector<FencePanel>& panels);

// Улаанбаатарын хотод унаж болох машин тавих гудамжны зах (x, z, y = чиглэл радианаар). BuildStaticStructures
// хотын layout-аар дуудагдсаны дараа бөглөгдөнө.
const std::vector<Vector3>& CityVehicleSpots();

void BuildSettlementStructures(int variant, const Layout& layout, const Terrain& terrain,
                               std::vector<Obstacle>& obstacles, MeshBuilder& solid, MeshBuilder& shadows,
                               MeshBuilder& glass, std::vector<Interior>& interiors, std::vector<Door>& doors,
                           std::vector<Passage>& passages, std::vector<WallSegment>& walls,
                           std::vector<FencePanel>& panels);
