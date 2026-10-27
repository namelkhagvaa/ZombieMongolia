#pragma once

#include "Layout.h"
#include "raylib.h"

#include <cstdint>
#include <string>
#include <vector>

// Улаанбаатар ба аймгийн төвүүд (төвөөс ±2.65 км): OpenStreetMap-ийн жинхэнэ зам, барилга, хашаа, ус, цэцэрлэг.
// assets/city/ub.bin (Улаанбаатар), assets/city/pNN.bin (аймаг, аяны дугаараар)-ийг tools/gen_city.py үүсгэнэ.
// Координат нь тоглоомын (x, z) метр: хойд = +Z, зүүн = -X. Аймгийн төвд (0, 0) нь застав (хуаран, дэлгүүр).
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
    River, Stream, Railway,
    Fence, // банзан хашаа (гэр хорооллын хашаа)
    Wall,  // чулуун, тоосгон хана
    Road,  // алсын газрын засмал авто зам (farLines)
    DirtRoad
};

// Онцлох газар
enum class CityPoiKind : uint8_t
{
    Fuel, Square, Stadium, Police, Hospital, Museum, Government,
    Station,  // тоглоомын ШТС барих жинхэнэ бензин колонк: position нь ШТС-ийн загварыг шилжүүлэх зөрүү
    TownSite, // хотын төвийн даалгаврын газар (төвийн талбай)
    Warehouse,// тоглоомын агуулахын төв (Warehouse(cx, cz))
    Camp,     // хот айл (малчдын отор)
    Hamlet    // хотоос гадна жижиг суурин
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
    std::vector<Mountain> mountains;   // хотын дэргэдэх уулс (Layout.h)
    std::vector<std::string> mountainNames;
    // Газрын жинхэнэ өндөр (version 3): тоглох талбай (15 м), алсын газар 10 км хүртэл (60 м)
    HeightGrid nearDem, farDem;
    // Алсын газрын гол мөрөн, авто зам (CityLine) ба нуур (Water), ой (Forest)
    std::vector<CityLine> farLines;
    std::vector<CityArea> farAreas;
    std::vector<CityRoad> roads;
    std::vector<CityBuilding> buildings;
    std::vector<CityArea> areas;
    std::vector<CityLine> lines;
    std::vector<CityPoi> pois;

    int province = -1;

    bool Loaded() const { return !buildings.empty(); }
    // ordinal дахь тухайн төрлийн газар; байхгүй бол false.
    bool Find(CityPoiKind kind, int ordinal, Vector2& out) const;
};

// Тухайн аймгийн (аяны дугаар) хотын өгөгдөл. Сүүлд уншсанаа л хадгална; файл байхгүй бол хоосон
// (аймаг хуучин, зохиомол хэлбэрээрээ үлдэнэ).
const CityData& CityFor(int province);
// Хотын өгөгдлийн файл assets доторх замаар ("city/ub.bin", "city/p05.bin")
std::string CityFile(int province);
// Сүүлд CityFor-оор уншсан хот (World хотын горимд шилжихдээ уншина).
const CityData& CurrentCity();

// Даалгаврын жинхэнэ газрууд (хотын горимд). Застав (дэлгүүртэй цэргийн хуаран): бусад аймгийнхтай ижил
// байрлалд (-48, 17); Улаанбаатарт Сүхбаатарын талбай дээр, аймгийн төвд gen_city-ийн сонгосон задгай газар.
// Хотын төв: TownSite (Улаанбаатарт Чойжин ламын сүмийн хойд талын талбай).
Vector2 CityTownSite(const CityData& city);
// ШТС: эхний Station (тоглоомын ШТС-ийн загварыг (30, -11.5) орчмоос энэ зөрүүгээр шилжүүлсэн).
Vector2 CityGasSite(const CityData& city);
// Агуулахын хаалганы өмнө (агуулахгүй бол хотын төв)
Vector2 CityWarehouseSite(const CityData& city);
// Хотын хил (тоглох талбайн хагас өргөн)
constexpr float kCityHalfExtent = 2600.0f;
