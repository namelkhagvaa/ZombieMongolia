#pragma once

#include "raylib.h"

#include <cstdint>
#include <string>
#include <vector>

// Улаанбаатарын төв (Сүхбаатарын талбайгаас ±2.65 км): OpenStreetMap-ийн жинхэнэ зам, барилга, ус, цэцэрлэг.
// assets/city/ub.bin-ийг tools/gen_city.py үүсгэнэ. Координат нь тоглоомын (x, z) метр: хойд = +Z, зүүн = -X.
// Өгөгдөл: (c) OpenStreetMap contributors, ODbL 1.0.

// Замын анги (tools/gen_city.py-ийн ROAD_CLASS)
enum class CityRoadClass : uint8_t
{
    Trunk = 0,       // их зам, өргөн чөлөө (Энхтайваны өргөн чөлөө)
    Primary = 1,
    Secondary = 2,
    Tertiary = 3,
    Residential = 4, // хорооллын гудамж
    Service = 5,     // байрны хашааны зам
    Track = 6,       // шороон зам
    Pedestrian = 7   // явган гудамж
};

// Барилгын төрөл (tools/gen_city.py-ийн BUILDING_TYPE)
enum class CityBuildingType : uint8_t
{
    Generic, Apartments, House, Ger, Commercial, School, Industrial, Religious, Government, Garage
};

// Талбайн төрөл
enum class CityAreaKind : uint8_t
{
    Water, Park, Pitch, Forest, Cemetery, Plaza
};

// Шугаман зүйл
enum class CityLineKind : uint8_t
{
    River, Stream, Railway
};

// Онцлох газар
enum class CityPoiKind : uint8_t
{
    Fuel, Square, Stadium, Police, Hospital, Museum, Government,
    Station // тоглоомын ШТС барих жинхэнэ бензин колонк: position нь ШТС-ийн загварыг шилжүүлэх зөрүү
};

struct CityRoad
{
    CityRoadClass klass;
    bool dirt;
    float halfWidth;
    std::vector<Vector2> points;
};

struct CityBuilding
{
    CityBuildingType type;
    int levels;
    float height;
    std::vector<Vector2> outline; // цагийн зүүний эсрэг (дээрээс харахад), давхардсан төгсгөлгүй
};

struct CityArea
{
    CityAreaKind kind;
    std::vector<Vector2> outline;
};

struct CityLine
{
    CityLineKind kind;
    float halfWidth;
    std::vector<Vector2> points;
};

struct CityPoi
{
    CityPoiKind kind;
    Vector2 position;
    std::string name;
};

struct CityData
{
    std::vector<CityRoad> roads;
    std::vector<CityBuilding> buildings;
    std::vector<CityArea> areas;
    std::vector<CityLine> lines;
    std::vector<CityPoi> pois;

    bool Loaded() const { return !buildings.empty(); }
};

// Эхний дуудалтаар файлыг уншина (олдохгүй бол хоосон: Улаанбаатар хуучин, зохиомол хэлбэрээрээ үлдэнэ).
const CityData& UlaanbaatarCity();

// Даалгаврын жинхэнэ газрууд (хотын горимд)
// Застав (дэлгүүртэй цэргийн хуаран): Сүхбаатарын талбай дээр, бусад аймгийнхтай ижил байрлалд (-48, 17).
// Хотын төв: Чойжин ламын сүм музейн хойд талын талбай (Сүхбаатарын талбайгаас 350 м урагш).
constexpr Vector2 kCityTownSite = { -18.0f, -342.0f };
// Шатахуун түгээх газар: "Петровис" ШТС (Төв цэнгэлдэхийн хойд талд). Тоглоомын ШТС-ийн загварыг
// (бусад аймагт (30, -11.5) орчимд) энэ зөрүүгээр шилжүүлж, OSM-ийн саравчны оронд барина.
constexpr Vector2 kCityGasOffset = { 320.0f, -1103.5f };
constexpr Vector2 kCityGasSite = { 31.0f + kCityGasOffset.x, -21.0f + kCityGasOffset.y };
// Хотын хил (тоглох талбайн хагас өргөн)
constexpr float kCityHalfExtent = 2600.0f;
