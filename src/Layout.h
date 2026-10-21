#pragma once

#include "raylib.h"

// Дэлгүүр (заставын хуаранд): дотор нь орж E дарахад зоосоор цэрэг элсүүлэх, зэвсэг сайжруулах,
// гранат, амь худалдаж авах цэс нээгдэнэ. Зомби дотогш орохгүй.
constexpr Vector2 kShopCenter = { -58.5f, -23.5f };

#include <cstdint>
#include <vector>

struct CityData;

// Аймгийн төвийн дэргэдэх уул (Цэцэрлэгийн Булган уул гэх мэт): зууван бэлтэй, оргил руу өндөрсөнө.
struct Mountain
{
    Vector2 peak;                     // оргил (x, z)
    float height;                     // хотоос дээш, м
    float west, east, south, north;   // оргилоос бэл хүртэлх зай (баруун = +X, хойд = +Z)
    Vector2 ovoo = { 1e9f, 1e9f };    // тахилгат овоо (байхгүй бол 1e9)

    // 0 (бэлээс гадна) .. 1 (оргил)
    float Profile(float x, float z) const;
    // Газрын өндөрт нэмэгдэх хэмжээ (хадтай ширүүн энгэр)
    float HeightAt(float x, float z) const;
};

// Талбарын ерөнхий төлөвлөлт: замууд, тэгш талбайнууд, отор ба жижиг суурингийн байрлал.
// Газрын гадаргуу (Terrain) болон байгууламж (Structures) хоёулаа үүнийг ашиглана.

// Ертөнцийн хэмжээ (метр). Газрын гадаргуу нь тоглогч явж болох талбайгаас бага зэрэг том.
constexpr float kWorldHalfExtent = 3100.0f;    // газрын гадаргуугийн хагас өргөн (нийт 6.2 км; Улаанбаатарын том нутгийг багтаана)
constexpr float kPlayableHalfExtent = 2150.0f; // тоглогч явж болох талбайн хагас өргөн (нийт 4.3 км)
// Улаанбаатар (аймгийн дугаар 15): Буянт-Ухаа нисэх буудлыг жинхэнэ хэмжээгээр нь багтаахын тулд нутаг нь илүү том.
constexpr int kUlaanbaatarProvince = 15;
constexpr float kUlaanbaatarMapScale = 1.24f;

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

// Нуур: газар хотойж, усны түвшин kLakeLevel (говьд хатсан давсны мараа)
struct Lake
{
    Vector2 center;
    float radius;
};
constexpr float kLakeLevel = -0.35f;

class Layout
{
public:
    std::vector<Road> roads;
    std::vector<FlatZone> zones;
    std::vector<Vector2> camps;   // малчдын отор
    std::vector<Vector2> hamlets; // жижиг суурин
    std::vector<Lake> lakes;

    // Нуурын хотгорын гүн (0 = нуурын гадна): газрын өндрөөс хасна.
    float LakeDepth(float x, float z) const;

    // Хамгийн ойрын замын захаас хэр хол байгаа (зам дээр бол сөрөг). kFarDistance-аар хязгаарлагдана.
    float RoadDistance(float x, float z) const;

    // Тэгш бүсээс (зам дагуух зурвас, суурин) хэр хол байгаа; <= 0 бол бүрэн тэгш. kFarDistance-аар хязгаарлагдана.
    float FlatDistance(float x, float z) const;

    // (x, z) цэг зам дээр байвал true, kind-д замын төрлийг буцаана (тээврийн хэрэгслийн хурдад).
    bool RoadAt(float x, float z, RoadKind& kind) const;

    // Засмал замын төв шугамын z координат (x-ээр).
    float HighwayZ(float x) const;

    // Буянт-Ухаа нисэх онгоцны буудал (зөвхөн Улаанбаатарын үед идэвхтэй): ХБЗ, явгалах зам, перрон дээр
    // өвс, чулуу ургахгүй, тээврийн хэрэгсэл засмал замын хурдаар явна. Газрын тэгш талбай бүх үед ижил.
    void SetAirfieldActive(bool active) { airfieldActive = active; }
    bool AirfieldActive() const { return airfieldActive; }
    bool Paved(float x, float z) const;

    // Зайн асуулгыг хурдасгах торыг (дахин) байгуулна. roads, zones өөрчлөгдсөний дараа дуудна.
    void BuildIndex();

    // --- Улаанбаатарын жинхэнэ төв (OpenStreetMap, City.h) ---
    // city = true бол газрын хэлбэр нь хотынх (Terrain), зам нь жинхэнэ гудамжууд, суурин отор байхгүй.
    bool IsCity() const { return city; }
    // Улаанбаатар (хотын газрын хэлбэр нь хойд зүгт өгсдөг толгод); бусад аймгийн төвд хотоос гарахад хээр тал
    bool IsCapital() const { return capital; }
    // Хамгийн ойрын барилга хүртэлх зай (24 м тор, 600 м хүртэл): хотын хил, газрын хэлбэрт
    float CityBuiltDistance(float x, float z) const;
    // Тэгш бүсээс хэр хол (зам тооцохгүй, хурдан)
    float ZoneDistance(float x, float z) const;
    // Хотын дэргэдэх уулс
    const std::vector<Mountain>& Mountains() const { return mountains; }
    // Хотын газрын бүрхэвч (3 м тор): 0 = энгийн (тоостой хөрс), 1 = цэцэрлэг, зүлэг, 2 = спорт талбай,
    // 3 = ой, 4 = оршуулгын газар, 5 = хучилттай талбай, 6 = ус.
    int CityGround(float x, float z) const;

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
    std::vector<std::vector<uint32_t>> cellSegments; // нүд бүрийн ойролцоох замын хэсгүүд
    std::vector<std::vector<uint32_t>> cellZones;    // нүд бүрийн ойролцоох тэгш бүсүүд

    // Хот
    friend Layout MakeCityLayout(const CityData& data);
    bool city = false;
    bool capital = false;
    static constexpr float kCityCell = 3.0f;
    static constexpr float kBuiltCell = 24.0f;
    int builtCells = 0;
    std::vector<uint16_t> builtDistance;            // барилгаас хэр хол (м)
    std::vector<Mountain> mountains;
    int cityCells = 0;                              // нэг талын нүдний тоо (тор нь ертөнцийг бүрэн хамарна)
    std::vector<uint8_t> cityGround;                // CityGround-ийн утга (0..6)
    std::vector<uint8_t> cityWater;                 // усны гүн (1/40 м нэгжээр): гол, цөөрөм

    // Нисэх буудал
    friend Layout MakeLayout();
    void BuildAirfield();
    float AirfieldFlatDistance(float x, float z) const;
    bool airfieldActive = false;
    std::vector<Vector2> airfieldHull;              // тэгшлэх гүдгэр олон өнцөгт
    Rectangle airfieldBounds = { 0, 0, 0, 0 };
    float airfieldMargin = 0.0f;
    static constexpr float kPavedCell = 2.0f;
    float pavedX0 = 0.0f, pavedZ0 = 0.0f;
    int pavedW = 0, pavedH = 0;
    std::vector<uint8_t> paved;                     // засмал хучилтын тор (2 м)
};

// Нисэх буудлын терминал руу орох засмал зам (зөвхөн Улаанбаатарын үед барина)
const Road& AirportAccessRoad();
// "Чингис хаан" терминалын гол байшингийн орон нутгийн тэнхлэг: x = урт тэнхлэг, z = агаарын (перроны) тал руу.
// Хуурай газрын (урд) нүүр нь z = -23, гүүрэн зам z = -29..-41, налуу замууд z = -67 хүртэл.
struct TerminalFrame
{
    Vector2 center;
    Vector2 axisX;
    Vector2 axisZ;
    Vector2 ToWorld(float x, float z) const
    {
        return { center.x + axisX.x * x + axisZ.x * z, center.y + axisX.y * x + axisZ.y * z };
    }
    float Angle() const; // MatrixRotateY-д өгөх өнцөг (локал +X -> axisX)
};
constexpr TerminalFrame kTerminalFrame = { { -563.75f, 1413.0f }, { -0.7118f, -0.7024f }, { 0.7024f, -0.7118f } };

// Терминалын урд талын машины зогсоол (дөрвөн булан, тойрох дарааллаар)
void AirportParking(Vector2 corners[4]);

Layout MakeLayout();
// Улаанбаатарын төвийн жинхэнэ зохион байгуулалт (OpenStreetMap-ийн гудамж, ус, цэцэрлэг).
Layout MakeCityLayout(const CityData& data);

// Аймгийн төв / хот: засмал замаас хойш гудамжны тортой том суурин. Гудамжнууд нь kTownStreets координатуудаар
// (X дагуу өргөн чөлөөнүүд, Z дагуу гудамжнууд) явж, хоорондоо 4 x 4 хороолол үүсгэнэ.
constexpr float kTownStreets[5] = { 110.0f, 165.0f, 220.0f, 275.0f, 330.0f };   // өргөн чөлөөнүүдийн X
constexpr float kTownRows[5] = { 20.0f, 75.0f, 130.0f, 185.0f, 240.0f };        // гудамжнуудын Z
constexpr Vector2 kTownCenter = { 220.0f, 130.0f };
inline bool InTown(float x, float z)
{
    return x > kTownStreets[0] - 8.0f && x < kTownStreets[4] + 8.0f && z > kTownRows[0] - 8.0f && z < kTownRows[4] + 8.0f;
}
// Аймгийн төвийн нэр (аяны дугаараар); city = true бол "хот" статустай (Эрдэнэт, Дархан, Улаанбаатар).
const char* TownName(int province, bool& city);

// Нарны чиглэл (газраас нар руу). Сүүдэр +X, +Z тал руу унана гэж байгууламжийн код тооцдог.
constexpr Vector3 kSunDirection = { -0.482f, 0.803f, -0.350f };
