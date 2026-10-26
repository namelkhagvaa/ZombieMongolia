#pragma once

#include "raylib.h"

// Ястай морины загвар (assets/horse_mesh.glb, tools/fix_horse_rig.py-аар зассан rig): хөл, хүзүү, сүүлийг
// кодоор хөдөлгөнө (загварт анимаци байхгүй). Файл байхгүй бол унаа кодоор үүсгэсэн морьтойгоо үлдэнэ.
bool LoadHorseModel(Shader shader);
void UnloadHorseModel();
bool HasHorseModel();

// Унааны орон зайд (урагш +Z, туурай y = 0) зурна. animation: давхилтын үе (Vehicle::animation),
// gait 0..1: хурд (0 = зогссон), steer -1..1: эргэх зүг рүү толгойгоо эргүүлнэ.
// seed: морь бүрийн зогсолтын хөдөлгөөнийг (сүүл шарвах, толгой эргүүлэх) өөр хэмнэлтэй болгоно.
// Давхилтын алхмын үе 0..1 (0 = хойд зүүн хөл газарт буух агшин). Бие, унаачийн дэгдэлтийг үүнтэй тааруулна.
float HorseStridePhase(float animation);

// gallop 0..1: хатирах -> давхих (хөлийн дараалал, савлалт өөрчлөгдөнө).
// dead: 0 = амьд, 1 = үхсэн (хөл нугарч сулрах, хүзүү, толгой унжих, сүүл хөдлөхгүй)
void DrawHorseModel(float animation, float gait, float steer, float seed = 0.0f, float gallop = 0.0f, float dead = 0.0f);
