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

// Нимгэн босоо хана (хашаа, хотын налуу байшингийн хана): хоёр цэгийн хоорондох хавтан. Налуу байрлалыг AABB-ээр
// илэрхийлж чадахгүй тул тусдаа. Явган хүн, зомбийг хаана (унаа нэвтэрнэ). climbable: цэрэг, тоглогч давж гарна
// (хашаа). blocksRays: сум, камерыг хаана (байшингийн хана; хашаа хаахгүй).
struct WallSegment
{
    Vector2 a, b;           // (x, z)
    float bottom, top;      // доод, дээд өндөр
    float halfThickness = 0.08f;
    bool climbable = true;
    bool blocksRays = false;
};

// Унаагаар дайрвал нурдаг хашааны хэсэг (~2 м). Геометр нь World-ийн тусдаа хашааны mesh-д (нурахад нуугдана),
// мөргөлдөөн нь wall (хотын хашаа) эсвэл obstacle (банзан хашаа).
struct FencePanel
{
    Vector2 a, b;            // суурийн хэрчим (x, z)
    float height = 2.0f;
    Color color = { 120, 100, 80, 255 };
    float breakSpeed = 3.0f; // ийм хурдаас (м/с) дээш дайрвал нурна
    int wall = -1, obstacle = -1;
    size_t first = 0, count = 0; // гурвалжны муж: бүтээх үед solid builder-т, дараа нь хашааны mesh-д
    int piece = -1;          // хашааны mesh-ийн хэсэг ба түүн доторх эхний орой
    int vertexStart = 0;
    bool settlement = false; // суурингийн (үе бүрд шинээр) эсвэл тогтмол хэсгийнх
    bool broken = false;
    Vector2 fall = {};       // нурсан зүг
};

// Явган хүмүүст зориулсан унааны мөргөлдөөн: тэнхлэгийн дагуух хэрчим + радиус (капсул)
struct VehicleBlocker
{
    Vector2 a, b;
    float radius;
    float bottom, top;
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
    bool gate = false;  // хашааны хаалга: унаа түрж нээнэ

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
    WindowDrop, // 2 давхрын цонхоор гарч, тавцангаас нь зүүгдээд доош үсрэх
    FenceOver   // хашаа давах (World::FindVault: тоглоомын явцад үүсгэнэ, top = хашааны орой)
};

struct Passage
{
    Vector3 from;   // эхлэх цэг (хөлийн байрлал)
    Vector3 to;     // хүрэх цэг; WindowDrop-д зүүгдэх байрлал (тэндээс чөлөөтэй унана)
    PassageKind kind;
    float facing;   // туулах үед харах зүг, радиан
    float wall = 0.0f; // цонх: from-оос ханын төв хүртэлх хэвтээ зай (дотор тавилга байвал цэг цааш шилждэг);
                       // шат: доод цэгээс шат хүртэлх хэвтээ зай (харах зүгийн дагуу)
    float top = 0.0f;  // хашаа: давах оройн өндөр
    bool rope = false; // шат биш олс (нисдэг тэргээр буусан өндөр байшингийн ирмэгт)
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

    // Дэлгүүр: зомби дотогш орохгүй, доторх хүнийг бай болгохгүй.
    bool safe = false;
    // Тэгш өнцөгтийн halfX талын чиглэл (хотын налуу байшин); анхдагч нь +X
    Vector2 axis = { 1.0f, 0.0f };

    bool Contains(float x, float z) const
    {
        const float dx = x - center.x, dz = z - center.y;
        if (round)
        {
            return dx * dx + dz * dz < halfX * halfX;
        }
        const float along = dx * axis.x + dz * axis.y, across = -dx * axis.y + dz * axis.x;
        return std::fabs(along) < halfX && std::fabs(across) < halfZ;
    }
};
