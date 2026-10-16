#pragma once

#include "raylib.h"

// Текстуртай унааны загвар (assets/cars/<name>.vmesh + <name>_color.png; tools/vehicleprep.cpp бэлтгэнэ).
// Их бие ба дугуйнууд тусдаа хэсэг: дугуй бүр өөрийн төвөө тойрон эргэж, урд дугуйнууд жолоогоор эргэнэ.
// Файл байхгүй бол унаа кодоор угсарсан хэлбэрээрээ зурагдана.
enum class VehicleModelId
{
    Jeep,  // G-Class
    Truck, // Mercedes Zetros маягийн 6x6
    Count
};

void LoadVehicleModels(Shader shader);
void UnloadVehicleModels();
bool HasVehicleModel(VehicleModelId id);

// Одоогийн rlgl хувиргалтаар (унааны төв, газрын түвшинд) зурна. spin нь дугуйн эргэлт (радиан),
// steer -1..1. depth != nullptr бол сүүдрийн зурагт тэр материалаар зурна. tint нь эвдрэлийн бараан өнгө.
void DrawVehicleModel(VehicleModelId id, float distance, float steer, const Material* depth, Color tint);
float VehicleModelWheelRadius(VehicleModelId id);

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

void LoadPropModels(Shader shader);
void UnloadPropModels();
bool HasPropModel(PropModelId id);
// Сүүдрийн зургийн үед (nullptr биш бол) бүх загварыг тэр материалаар зурна.
void SetPropDepthMaterial(const Material* depth);
// Одоогийн rlgl хувиргалтаар зурна.
void DrawPropModel(PropModelId id, Color tint = WHITE);
