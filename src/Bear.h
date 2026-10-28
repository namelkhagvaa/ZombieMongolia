#pragma once

#include "raylib.h"

// Баавгай (ZombieHorde-д ZombieType::Bear). Ястай загвар (assets/bear_mesh.glb + bear_color.png, tools/prep_bear.py):
// хөл, толгой, сүүлийг кодоор хөдөлгөнө (загварт анимаци байхгүй). Файл байхгүй бол кодоор үүсгэсэн баавгай.
bool LoadBearModel(Shader shader);
void UnloadBearModel();

// Кодоор үүсгэсэн баавгай (загваргүй үед, сүүдрийн давхаргад) энэ материалаар зурагдана; nullptr бол шууд зурна.
// shadow = true: сүүдрийн давхарга (ястай загварын оронд хөнгөн дүрс).
void SetBearMaterial(const Material* material, bool shadow = false);

// feet: биеийн доорх газрын цэг, facing: харах зүг (+Z = 0), cycle: алхааны үе (Zombie::walkCycle, радиан),
// speed: бодит хурд м/с (алхах -> давхих), attack 1..0: савраар шүүрэх, dead 0..1: хажуу тийшээ хөмрөх.
// detailed = false бол нүд, хумс зэрэг жижиг хэсгийг зурахгүй (хол). seed: баавгай бүрийн зогсолтын хөдөлгөөн.
void DrawBear(Vector3 feet, float facing, float scale, float cycle, float speed, float attack, float dead, bool detailed,
              float seed = 0.0f);
