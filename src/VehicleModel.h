#pragma once

#include "raylib.h"

// Текстуртай унааны загвар (assets/cars/<name>.vmesh + <name>_color.png; tools/vehicleprep.cpp бэлтгэнэ).
// Их бие ба дугуйнууд тусдаа хэсэг: дугуй бүр өөрийн төвөө тойрон эргэж, урд дугуйнууд жолоогоор эргэнэ.
// Файл байхгүй бол унаа кодоор угсарсан хэлбэрээрээ зурагдана.
enum class VehicleModelId
{
    Jeep,  // G-Class
    Truck, // Mercedes Zetros маягийн 6x6
    Suv,       // Nissan X-Trail
    Hatchback, // Toyota Yaris
    Motorcycle, // хээрийн мотоцикл (assets/cars/motocycle.glb -> moto.vmesh)
    Trike,      // гурван дугуйт ачааны мотоцикл (3duguit.glb -> trike.vmesh): урд нэг, хойд хоёр дугуй
    Helicopter, // унадаг Ми-8 (unadag_helicopter.glb -> heli.vmesh): сэнс тусдаа хэсэг (kind 3, 4)
    DropHelicopter, // ачаа буулгадаг нисдэг тэрэг (drop_buulgadag_helicopter.glb -> dropheli.vmesh)
    Count
};

void LoadVehicleModels(Shader shader);
void UnloadVehicleModels();
bool HasVehicleModel(VehicleModelId id);

// Одоогийн rlgl хувиргалтаар (унааны төв, газрын түвшинд) зурна. distance нь явсан зай (дугуйн эргэлт),
// steer -1..1. depth != nullptr бол сүүдрийн зурагт тэр материалаар зурна. tint нь эвдрэлийн бараан өнгө.
// rotor: нисдэг тэрэгний сэнсний эргэлт (радиан; сүүлний сэнс 5 дахин хурдан).
void DrawVehicleModel(VehicleModelId id, float distance, float steer, const Material* depth, Color tint, float rotor = 0.0f);
float VehicleModelWheelRadius(VehicleModelId id);
// kind төрлийн эхний хэсгийн эргэх төв (м; байхгүй бол 0): 3 үндсэн сэнсний тэнхлэг, 4 сүүлний сэнс
Vector3 VehicleModelPivot(VehicleModelId id, unsigned int kind);

// Унаагүй, нэг хэсэгтэй текстуртай загварууд (assets/buu/<name>.vmesh + <name>_color.png; vehicleprep prop бэлтгэнэ).
// Байрлал нь загварын орон зайд: урт тэнхлэг +Z, эх нь z = 0 дээр (tools/prop_align.py).
enum class PropModelId
{
    Ak47,   // автомат буу: бөгс z = 0, ам z = 1.15, гол төмрийн тэнхлэг y = 0, x = 0
    Casing, // буунаас үсрэх сум: ёроол z = 0, үзүүр z = 0.057
    Sniper, // дурантай буу: бөгс z = 0, ам z = 1.49, гол төмөр y = 0, x = 0
    Pistol, // гар буу: ар тал z = 0, ам z = 0.23, гол төмөр y = 0, x = 0
    Shotgun, // үрлэн буу: бөгс z = 0, ам z = 1.255, гол төмөр ба сумны хоолойн дунд y = 0, x = 0
    Knife,   // хутга: толгой z = 0, үзүүр z = 0.44, ирний нуруу +Y, ир нь YZ хавтгайд
    Count
};

// Байгалийн зүйлс (assets/trees, assets/pooshig: <name>.vmesh + <name>_color.png; VPREP_HEIGHT=1 vehicleprep prop бэлтгэнэ).
// Загвар нь ёроол y = 0, өндөр 1 м, төв нь x = z = 0: зурахдаа өндрөөр нь масштаблана.
enum class SceneryModelId
{
    Larch,      // шинэс (trees/larch)
    Birch,      // хус (trees/birch)
    Elm,        // хайлаас, хотын мод (trees/elm)
    Saxaul,     // заг (trees/saxaul)
    OldBarrel,  // зэвэрсэн хоосон торх (pooshig/barrel_old)
    FuelBarrel, // бензинтэй улаан торх (pooshig/barrel_fuel)
    Count
};

void LoadSceneryModels(Shader shader);
void UnloadSceneryModels();
bool HasSceneryModel(SceneryModelId id);
// Одоогийн rlgl хувиргалтаар зурна. depth != nullptr бол сүүдрийн зурагт тэр материалаар.
void DrawSceneryModel(SceneryModelId id, const Material* depth = nullptr);

void LoadPropModels(Shader shader);
void UnloadPropModels();
bool HasPropModel(PropModelId id);
// Сүүдрийн зургийн үед (nullptr биш бол) бүх загварыг тэр материалаар зурна.
void SetPropDepthMaterial(const Material* depth);
// Одоогийн rlgl хувиргалтаар зурна.
void DrawPropModel(PropModelId id, Color tint = WHITE);
