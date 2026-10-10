#pragma once

#include "raylib.h"

#include <cmath>

// Мөргөлдөөний хувьд бүх саад нь тэнхлэгтэй зэрэгцээ хайрцаг (AABB).
// Харагдах хэлбэрийг нь тусад нь статик mesh-д зурдаг.
struct Obstacle
{
    Vector3 center;
    Vector3 size;
    bool blocksRays = true; // false: торон хашаа шиг сум, камер нэвтэрнэ

    float Top() const { return center.y + size.y * 0.5f; }
    float Bottom() const { return center.y - size.y * 0.5f; }
    BoundingBox Bounds() const
    {
        return { { center.x - size.x * 0.5f, center.y - size.y * 0.5f, center.z - size.z * 0.5f },
                 { center.x + size.x * 0.5f, center.y + size.y * 0.5f, center.z + size.z * 0.5f } };
    }
};

// Нээж хаадаг хаалга. Хаалттай үедээ саад болж, нээлттэй үедээ гадагш эргэсэн байна.
struct Door
{
    Vector3 hinge;     // нугасны доод цэг
    float width;
    float height;
    float closedYaw;   // хаалттай үед хавтас нугаснаас аль зүг рүү сунах (радиан, 0 = +Z)
    float openYaw;     // нээлттэй үед
    Color color;
    bool open = false;
    float swing = 0.0f; // 0 = хаалттай, 1 = бүрэн нээлттэй (зөөлөн шилжинэ)
    float pound = 0.0f; // зомби хэр удаан нүдсэн (секунд); хангалттай болбол эвдэрч нээгдэнэ

    Vector3 Center() const
    {
        return { hinge.x + std::sin(closedYaw) * width * 0.5f, hinge.y + height * 0.5f,
                 hinge.z + std::cos(closedYaw) * width * 0.5f };
    }

    // Хаалттай хавтасны мөргөлдөөний хайрцаг.
    Obstacle Blocker() const
    {
        const float alongX = std::fabs(std::sin(closedYaw)) * width;
        const float alongZ = std::fabs(std::cos(closedYaw)) * width;
        return { Center(), { alongX + 0.25f, height, alongZ + 0.25f }, true };
    }
};

// Дотогш орж болох барилгын (гэр, байшин) дотор тал ба хаалга.
// Багийн цэргүүд тоглогчийг дагаж орохдоо хаалганы хоёр цэгээр дамжина.
struct Interior
{
    Vector2 center;      // (x, z)
    float halfX, halfZ;  // тэгш өнцөгт бол хагас хэмжээ; дугуй бол halfX = радиус
    bool round;
    Vector2 doorOutside; // хаалганы гадна талын цэг
    Vector2 doorInside;  // хаалганы дотор талын цэг
    int door;            // World-ийн хаалганы жагсаалт дахь индекс

    bool Contains(float x, float z) const
    {
        const float dx = x - center.x, dz = z - center.y;
        if (round)
        {
            return dx * dx + dz * dz < halfX * halfX;
        }
        return std::fabs(dx) < halfX && std::fabs(dz) < halfZ;
    }
};
