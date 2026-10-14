#pragma once

#include "raylib.h"

// Дэлгүүр (заставын хуаранд): дотор нь орж E дарахад зоосоор цэрэг элсүүлэх, зэвсэг сайжруулах,
// гранат, амь худалдаж авах цэс нээгдэнэ. Зомби дотогш орохгүй.
constexpr Vector2 kShopCenter = { -58.5f, -23.5f };

#include <cstdint>
#include <vector>

// Талбарын ерөнхий төлөвлөлт: замууд, тэгш талбайнууд, отор ба жижиг суурингийн байрлал.
// Газрын гадаргуу (Terrain) болон байгууламж (Structures) хоёулаа үүнийг ашиглана.

// Ертөнцийн хэмжээ (метр). Газрын гадаргуу нь тоглогч явж болох талбайгаас бага зэрэг том.
constexpr float kWorldHalfExtent = 2700.0f;    // газрын гадаргуугийн хагас өргөн (нийт 5.4 км)
constexpr float kPlayableHalfExtent = 2150.0f; // тоглогч явж болох талбайн хагас өргөн (нийт 4.3 км)

enum class RoadKind
{
    Asphalt, // эвдэрсэн засмал зам
    Dirt     // шороон зам
};

struct Road
{
    RoadKind kind;
    float halfWidth;
    std::vector<Vector2> points; // (x, z) тэнхлэгийн шугам
};

struct FlatZone
{
    Vector2 center; // (x, z)
    float radius;
};

class Layout
{
public:
    std::vector<Road> roads;
    std::vector<FlatZone> zones;
    std::vector<Vector2> camps;   // малчдын отор
    std::vector<Vector2> hamlets; // жижиг суурин

    // Хамгийн ойрын замын захаас хэр хол байгаа (зам дээр бол сөрөг). kFarDistance-аар хязгаарлагдана.
    float RoadDistance(float x, float z) const;

    // Тэгш бүсээс (зам дагуух зурвас, суурин) хэр хол байгаа; <= 0 бол бүрэн тэгш. kFarDistance-аар хязгаарлагдана.
    float FlatDistance(float x, float z) const;

    // (x, z) цэг зам дээр байвал true, kind-д замын төрлийг буцаана (тээврийн хэрэгслийн хурдад).
    bool RoadAt(float x, float z, RoadKind& kind) const;

    // Засмал замын төв шугамын z координат (x-ээр).
    float HighwayZ(float x) const;

    // Зайн асуулгыг хурдасгах торыг (дахин) байгуулна. roads, zones өөрчлөгдсөний дараа дуудна.
    void BuildIndex();

    // Зайн асуулгууд үүнээс холыг ялгахгүй (ийм зайд газрын хэлбэр, өнгөд нөлөөлөхөө больдог).
    static constexpr float kFarDistance = 45.0f;

private:
    struct Segment
    {
        Vector2 a, b;
        float halfWidth;
        RoadKind kind;
    };

    static constexpr float kCellSize = 120.0f;

    int CellIndex(float x, float z) const;

    std::vector<Segment> segments;
    int gridSide = 0;
    std::vector<std::vector<uint16_t>> cellSegments; // нүд бүрийн ойролцоох замын хэсгүүд
    std::vector<std::vector<uint16_t>> cellZones;    // нүд бүрийн ойролцоох тэгш бүсүүд
};

Layout MakeLayout();

// Нарны чиглэл (газраас нар руу). Сүүдэр +X, +Z тал руу унана гэж байгууламжийн код тооцдог.
constexpr Vector3 kSunDirection = { -0.482f, 0.803f, -0.350f };
