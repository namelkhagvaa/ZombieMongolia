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
