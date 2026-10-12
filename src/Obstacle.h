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
    bool walkable = false;  // шатны гишгүүр: ойролцоо өндөрт зогссон хүнийг хэвтээ чиглэлд хаахгүй, дээгүүр нь алхана

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

// Тусгай аргаар туулах гарц: шат, цонх. Тоглогч from цэгийн дэргэд F дарахад to цэг рүү шилжинэ
// (авирах, давж гарах, зүүгдэж буух хөдөлгөөнтэйгөөр). Хоёр чиглэлтэй гарц хоёр бичлэгээр хадгалагдана.
enum class PassageKind
{
    LadderUp,   // шатаар дээш авирах
    LadderDown, // шатаар доош буух
    WindowOut,  // 1 давхрын цонхоор үсэрч гарах
    WindowIn,   // 1 давхрын цонхоор авирч орох
    WindowDrop  // 2 давхрын цонхоор гарч, тавцангаас нь зүүгдээд доош үсрэх
};

struct Passage
{
    Vector3 from;   // эхлэх цэг (хөлийн байрлал)
    Vector3 to;     // хүрэх цэг; WindowDrop-д зүүгдэх байрлал (тэндээс чөлөөтэй унана)
    PassageKind kind;
    float facing;   // туулах үед харах зүг, радиан
    float wall = 0.0f; // цонх: from-оос ханын төв хүртэлх хэвтээ зай (дотор тавилга байвал цэг цааш шилждэг)
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

    // Хоёр давхар байшин: дотуур шатны доод, дээд үзүүр (зомби, цэрэг дээд давхар руу гарахад)
    bool hasStairs = false;
    Vector2 stairBottom = { 0.0f, 0.0f };
    Vector2 stairTop = { 0.0f, 0.0f };
    float upperFloor = 0.0f; // дээд давхрын шалны өндөр

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
