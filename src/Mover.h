#pragma once

#include "World.h"
#include "raylib.h"

// AI-аар удирдагдах дүрийн (зомби, цэрэг) байрлал ба саад тойрох төлөв.
struct Mover
{
    Vector3 position = { 0.0f, 0.0f, 0.0f }; // хөлийн байрлал
    float facing = 0.0f;                     // радиан, 0 = +Z
    float detourTimer = 0.0f;                // >0 үед хана дагаж хажуу тийш явна
    float detourSign = 1.0f;                 // аль тал руу тойрох
};

// target руу хэвтээ чиглэлд явуулна. Хананд тулж гацвал хэсэг хугацаанд хажуу тийш тойруулна.
void MoveToward(Mover& mover, Vector3 target, float speed, float dt, const World& world, float radius,
                float height, float stepHeight);

// Хэвтээ (XZ) хавтгай дахь зай.
float HorizontalDistance(Vector3 a, Vector3 b);
