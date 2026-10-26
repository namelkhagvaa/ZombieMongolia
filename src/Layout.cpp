#include "Layout.h"

#include "AirportData.h"
#include "City.h"
#include "Procedural.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>

namespace
{
    const float kRoadFlatMargin = 8.0f; // замын тэнхлэгээс ийм зайд газар бүрэн тэгш

    float DistanceToSegment(float px, float pz, Vector2 a, Vector2 b)
    {
        const float abx = b.x - a.x;
        const float abz = b.y - a.y;
        const float lengthSq = abx * abx + abz * abz;
        float t = lengthSq > 0.0f ? ((px - a.x) * abx + (pz - a.y) * abz) / lengthSq : 0.0f;
        t = std::clamp(t, 0.0f, 1.0f);
        const float dx = px - (a.x + abx * t);
        const float dz = pz - (a.y + abz * t);
        return std::sqrt(dx * dx + dz * dz);
    }

    bool InsidePolygon(const Vector2* points, int count, float x, float z)
    {
        bool inside = false;
        for (int i = 0, j = count - 1; i < count; j = i++)
        {
            const Vector2 a = points[i], b = points[j];
            if ((a.y > z) != (b.y > z) && x < (b.x - a.x) * (z - a.y) / (b.y - a.y) + a.x)
            {
                inside = !inside;
            }
        }
        return inside;
    }
}

const Road& AirportAccessRoad()
{
    // Хойд шороон замаас салж, ХБЗ-ын хойд үзүүрийг тойрон терминалын урд талбай руу
    static const Road road = { RoadKind::Asphalt, 4.0f,
                               { { 359.0f, 1520.0f }, { 250.0f, 1575.0f }, { 120.0f, 1600.0f }, { -120.0f, 1610.0f },
                                 { -330.0f, 1605.0f }, { -520.0f, 1570.0f }, { -600.0f, 1515.0f } } };
    return road;
}

float TerminalFrame::Angle() const
{
    return std::atan2(axisZ.x, axisX.x);
}

void AirportParking(Vector2 corners[4])
{
    // Терминалын хуурай газрын талын нүүрний дагуу: урт тэнхлэг (0.71, 0.70), гүн (-0.70, 0.71)
    const Vector2 center = { -621.0f, 1484.0f }, along = { 0.71f, 0.70f }, across = { -0.70f, 0.71f };
    const float halfLength = 45.0f, halfDepth = 18.0f;
    const float signs[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
    for (int i = 0; i < 4; ++i)
    {
        corners[i] = { center.x + along.x * halfLength * signs[i][0] + across.x * halfDepth * signs[i][1],
                       center.y + along.y * halfLength * signs[i][0] + across.y * halfDepth * signs[i][1] };
    }
}

void Layout::BuildAirfield()
{
    using namespace airport;
    airfieldHull.assign(kPoints + kFlatHull.first, kPoints + kFlatHull.first + kFlatHull.count);
    airfieldMargin = kFlatMargin;
    float minX = 1e9f, minZ = 1e9f, maxX = -1e9f, maxZ = -1e9f;
    for (const Vector2& p : airfieldHull)
    {
        minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
        minZ = std::min(minZ, p.y); maxZ = std::max(maxZ, p.y);
    }
    airfieldBounds = { minX, minZ, maxX - minX, maxZ - minZ };

    // Засмал хучилтын тор: ХБЗ, явгалах зам, перрон, нисдэг тэрэгний талбай, орох зам
    const Road& access = AirportAccessRoad();
    for (const Vector2& p : access.points)
    {
        minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
        minZ = std::min(minZ, p.y); maxZ = std::max(maxZ, p.y);
    }
    pavedX0 = minX - 30.0f;
    pavedZ0 = minZ - 30.0f;
    pavedW = (int)((maxX - minX + 60.0f) / kPavedCell) + 1;
    pavedH = (int)((maxZ - minZ + 60.0f) / kPavedCell) + 1;
    paved.assign((size_t)pavedW * pavedH, 0);

    auto markSegment = [&](Vector2 a, Vector2 b, float halfWidth) {
        const float reach = halfWidth + kPavedCell * 0.5f;
        const int x0 = std::max(0, (int)((std::min(a.x, b.x) - reach - pavedX0) / kPavedCell));
        const int x1 = std::min(pavedW - 1, (int)((std::max(a.x, b.x) + reach - pavedX0) / kPavedCell));
        const int z0 = std::max(0, (int)((std::min(a.y, b.y) - reach - pavedZ0) / kPavedCell));
        const int z1 = std::min(pavedH - 1, (int)((std::max(a.y, b.y) + reach - pavedZ0) / kPavedCell));
        for (int iz = z0; iz <= z1; ++iz)
        {
            for (int ix = x0; ix <= x1; ++ix)
            {
                const float x = pavedX0 + ((float)ix + 0.5f) * kPavedCell, z = pavedZ0 + ((float)iz + 0.5f) * kPavedCell;
                if (DistanceToSegment(x, z, a, b) <= reach) paved[(size_t)iz * pavedW + ix] = 1;
            }
        }
    };
    auto markPolygon = [&](const Ring& ring) {
        const Vector2* points = kPoints + ring.first;
        float x0 = 1e9f, z0 = 1e9f, x1 = -1e9f, z1 = -1e9f;
        for (int i = 0; i < ring.count; ++i)
        {
            x0 = std::min(x0, points[i].x); x1 = std::max(x1, points[i].x);
            z0 = std::min(z0, points[i].y); z1 = std::max(z1, points[i].y);
        }
        const int ix0 = std::max(0, (int)((x0 - pavedX0) / kPavedCell) - 1);
        const int ix1 = std::min(pavedW - 1, (int)((x1 - pavedX0) / kPavedCell) + 1);
        const int iz0 = std::max(0, (int)((z0 - pavedZ0) / kPavedCell) - 1);
        const int iz1 = std::min(pavedH - 1, (int)((z1 - pavedZ0) / kPavedCell) + 1);
        for (int iz = iz0; iz <= iz1; ++iz)
        {
            for (int ix = ix0; ix <= ix1; ++ix)
            {
                const float x = pavedX0 + ((float)ix + 0.5f) * kPavedCell, z = pavedZ0 + ((float)iz + 0.5f) * kPavedCell;
                if (InsidePolygon(points, ring.count, x, z)) paved[(size_t)iz * pavedW + ix] = 1;
            }
        }
    };

    for (const Strip& strip : kStrips)
    {
        for (int i = 0; i + 1 < strip.count; ++i)
        {
            markSegment(kPoints[strip.first + i], kPoints[strip.first + i + 1],
                        strip.halfWidth + (strip.kind == 2 ? 0.0f : 1.5f));
        }
    }
    for (const Ring& apron : kAprons) markPolygon(apron);
    for (int area = 0; area < 2; ++area)
    {
        Vector2 corners[4];
        if (area == 0)
        {
            AirportParking(corners);
        }
        else
        {
            // Терминалын урд талбай (гүүрэн замын доор, налуу замууд)
            const TerminalFrame& f = kTerminalFrame;
            corners[0] = f.ToWorld(-116.0f, -72.0f); corners[1] = f.ToWorld(116.0f, -72.0f);
            corners[2] = f.ToWorld(116.0f, -22.0f); corners[3] = f.ToWorld(-116.0f, -22.0f);
        }
        const float x0 = std::min({ corners[0].x, corners[1].x, corners[2].x, corners[3].x });
        const float x1 = std::max({ corners[0].x, corners[1].x, corners[2].x, corners[3].x });
        const float z0 = std::min({ corners[0].y, corners[1].y, corners[2].y, corners[3].y });
        const float z1 = std::max({ corners[0].y, corners[1].y, corners[2].y, corners[3].y });
        for (float z = z0; z <= z1; z += kPavedCell)
        {
            for (float x = x0; x <= x1; x += kPavedCell)
            {
                const int ix = (int)((x - pavedX0) / kPavedCell), iz = (int)((z - pavedZ0) / kPavedCell);
                if (ix >= 0 && iz >= 0 && ix < pavedW && iz < pavedH && InsidePolygon(corners, 4, x, z))
                {
                    paved[(size_t)iz * pavedW + ix] = 1;
                }
            }
        }
    }
    for (const Ring& pad : kHelipads) markPolygon(pad);
    for (size_t i = 0; i + 1 < access.points.size(); ++i)
    {
        markSegment(access.points[i], access.points[i + 1], access.halfWidth + 1.0f);
    }
}

float Layout::AirfieldFlatDistance(float x, float z) const
{
    const float reach = airfieldMargin + kFarDistance;
    if (airfieldHull.empty() || x < airfieldBounds.x - reach || z < airfieldBounds.y - reach ||
        x > airfieldBounds.x + airfieldBounds.width + reach || z > airfieldBounds.y + airfieldBounds.height + reach)
    {
        return kFarDistance;
    }
    const int count = (int)airfieldHull.size();
    if (InsidePolygon(airfieldHull.data(), count, x, z)) return 0.0f;
    float best = kFarDistance + airfieldMargin;
    for (int i = 0, j = count - 1; i < count; j = i++)
    {
        best = std::min(best, DistanceToSegment(x, z, airfieldHull[j], airfieldHull[i]));
    }
    // Талбай дотор 0: бүрэн тэгш, гэхдээ суурин шиг гишгэгдсэн өнгөгүй, өвс нь ургана
    return std::max(0.0f, best - airfieldMargin);
}

float Layout::LakeDepth(float x, float z) const
{
    if (city)
    {
        // Хотын гол, цөөрөм: 3 м торын утгуудыг хооронд нь шугаман завсарлана (газар гөлгөр хотойно).
        const float fx = (x + kWorldHalfExtent) / kCityCell - 0.5f, fz = (z + kWorldHalfExtent) / kCityCell - 0.5f;
        const int ix = (int)std::floor(fx), iz = (int)std::floor(fz);
        if (ix < 0 || iz < 0 || ix + 1 >= cityCells || iz + 1 >= cityCells) return 0.0f;
        const float tx = fx - (float)ix, tz = fz - (float)iz;
        auto at = [&](int cx, int cz) { return (float)cityWater[(size_t)cz * cityCells + cx]; };
        const float top = at(ix, iz) + (at(ix + 1, iz) - at(ix, iz)) * tx;
        const float bottom = at(ix, iz + 1) + (at(ix + 1, iz + 1) - at(ix, iz + 1)) * tx;
        return (top + (bottom - top) * tz) / 40.0f;
    }
    float depth = 0.0f;
    for (const Lake& lake : lakes)
    {
        const float dx = x - lake.center.x, dz = z - lake.center.y;
        const float t = (dx * dx + dz * dz) / (lake.radius * lake.radius);
        if (t < 1.0f) depth = std::max(depth, 1.8f * (1.0f - t));
    }
    return depth;
}

float Layout::CityBuiltDistance(float x, float z) const
{
    if (!city || builtDistance.empty()) return 0.0f;
    // Нүдний төвүүдийн хооронд шугаман завсарлана (газрын хэлбэр гөлгөр)
    const float fx = (x + kWorldHalfExtent) / kBuiltCell - 0.5f, fz = (z + kWorldHalfExtent) / kBuiltCell - 0.5f;
    const int ix = std::clamp((int)std::floor(fx), 0, builtCells - 2), iz = std::clamp((int)std::floor(fz), 0, builtCells - 2);
    const float tx = std::clamp(fx - (float)ix, 0.0f, 1.0f), tz = std::clamp(fz - (float)iz, 0.0f, 1.0f);
    auto at = [&](int cx, int cz) { return (float)builtDistance[(size_t)cz * builtCells + cx]; };
    const float top = at(ix, iz) + (at(ix + 1, iz) - at(ix, iz)) * tx;
    const float bottom = at(ix, iz + 1) + (at(ix + 1, iz + 1) - at(ix, iz + 1)) * tx;
    return top + (bottom - top) * tz;
}

float Layout::ZoneDistance(float x, float z) const
{
    float best = kFarDistance;
    for (uint32_t index : cellZones[CellIndex(x, z)])
    {
        const FlatZone& zone = zones[index];
        best = std::min(best, std::sqrt((x - zone.center.x) * (x - zone.center.x) + (z - zone.center.y) * (z - zone.center.y)) - zone.radius);
    }
    return best;
}

float HeightGrid::Sample(float x, float z) const
{
    const float fx = std::clamp((x + half) / cell, 0.0f, (float)(side - 1) - 0.001f);
    const float fz = std::clamp((z + half) / cell, 0.0f, (float)(side - 1) - 0.001f);
    const int ix = (int)fx, iz = (int)fz;
    const float tx = fx - (float)ix, tz = fz - (float)iz;
    const float* row = &values[(size_t)iz * side + ix];
    const float top = row[0] + (row[1] - row[0]) * tx;
    const float bottom = row[side] + (row[side + 1] - row[side]) * tx;
    return top + (bottom - top) * tz;
}

float Layout::DemHeight(float x, float z) const
{
    if (nearDem.Covers(x, z)) return nearDem.Sample(x, z);
    return farDem.Empty() ? nearDem.Sample(x, z) : farDem.Sample(x, z);
}

bool Layout::NearestZone(float x, float z, float& distance, float& level) const
{
    bool found = false;
    distance = kFarDistance;
    for (uint32_t index : cellZones[CellIndex(x, z)])
    {
        const FlatZone& zone = zones[index];
        const float d = std::sqrt((x - zone.center.x) * (x - zone.center.x) + (z - zone.center.y) * (z - zone.center.y)) - zone.radius;
        if (d < distance)
        {
            distance = d;
            level = zone.level;
            found = true;
        }
    }
    return found;
}

float Layout::BuildLevel(float x, float z) const
{
    float distance = 0.0f, level = 0.0f;
    if (NearestZone(x, z, distance, level) && distance <= 0.0f) return level;
    return HasDem() ? DemHeight(x, z) : 0.0f;
}

float Mountain::Profile(float x, float z) const
{
    const float rx = (x - peak.x) / (x > peak.x ? west : east);
    const float rz = (z - peak.y) / (z > peak.y ? north : south);
    const float r2 = rx * rx + rz * rz;
    if (r2 >= 1.0f) return 0.0f;
    // Шовх оройтой конус: энгэр нь хотгор (бэлдээ зөөлөн хормой, дээшээ эгц), оргилдоо бага зэрэг бөөрөнхий
    const float t = 1.0f - std::sqrt(r2);
    return std::pow(t, 1.45f) * (1.0f - 0.25f * std::exp(-t * t * 60.0f) * 0.0f);
}

float Mountain::HeightAt(float x, float z) const
{
    const float profile = Profile(x, z);
    if (profile <= 0.0f) return 0.0f;
    // Боржин хадны нуруу, хавцал: ширүүн (ridged) шуугиан энгэрийн дээд хэсэгт, бэлдээ гөлгөр ногоон хормой
    const float ridge = 1.0f - std::fabs(Fbm(x * 0.009f + 3.0f, z * 0.009f + 11.0f, 4) * 2.0f - 1.0f);
    const float crag = (Fbm(x * 0.045f + 17.0f, z * 0.045f + 5.0f, 3) - 0.5f) * 14.0f; // хадан цохио
    const float rocky = SmoothStep(0.08f, 0.35f, profile);
    return height * profile + (ridge * ridge * 0.12f * height * std::sqrt(profile) + crag * rocky) * (1.0f - profile * 0.4f);
}

int Layout::CityGround(float x, float z) const
{
    if (!city) return 0;
    const int ix = (int)((x + kWorldHalfExtent) / kCityCell), iz = (int)((z + kWorldHalfExtent) / kCityCell);
    if (ix < 0 || iz < 0 || ix >= cityCells || iz >= cityCells) return 0;
    return cityGround[(size_t)iz * cityCells + ix];
}

bool Layout::Paved(float x, float z) const
{
    if (city) return CityGround(x, z) == 5; // хучилттай талбай (Сүхбаатарын талбай)
    if (!airfieldActive || paved.empty()) return false;
    const int ix = (int)std::floor((x - pavedX0) / kPavedCell);
    const int iz = (int)std::floor((z - pavedZ0) / kPavedCell);
    if (ix < 0 || iz < 0 || ix >= pavedW || iz >= pavedH) return false;
    return paved[(size_t)iz * pavedW + ix] != 0;
}

int Layout::CellIndex(float x, float z) const
{
    const int ix = std::clamp((int)((x + kWorldHalfExtent) / kCellSize), 0, gridSide - 1);
    const int iz = std::clamp((int)((z + kWorldHalfExtent) / kCellSize), 0, gridSide - 1);
    return iz * gridSide + ix;
}

void Layout::BuildIndex()
{
    segments.clear();
    for (const Road& road : roads)
    {
        for (size_t i = 0; i + 1 < road.points.size(); ++i)
        {
            segments.push_back({ road.points[i], road.points[i + 1], road.halfWidth, road.kind });
        }
    }

    gridSide = (int)std::ceil(kWorldHalfExtent * 2.0f / kCellSize);
    cellSegments.assign((size_t)gridSide * gridSide, {});
    cellZones.assign((size_t)gridSide * gridSide, {});

    // Нүд бүрд: тэр нүдний аль ч цэгээс kFarDistance дотор байж болох зам, бүсүүдийг бүртгэнэ.
    // (Хэсэг бүрийн хамрах тэгш өнцөгттэй давхцах нүднүүд: хотод хэдэн арван мянган хэсэг байдаг.)
    const float reach = kFarDistance + kRoadFlatMargin;
    auto cellOf = [&](float v) { return std::clamp((int)std::floor((v + kWorldHalfExtent) / kCellSize), 0, gridSide - 1); };
    auto mark = [&](std::vector<std::vector<uint32_t>>& cells, uint32_t index, float minX, float minZ, float maxX, float maxZ) {
        if (maxX < -kWorldHalfExtent || minX > kWorldHalfExtent || maxZ < -kWorldHalfExtent || minZ > kWorldHalfExtent) return;
        const int x0 = cellOf(minX), x1 = cellOf(maxX), z0 = cellOf(minZ), z1 = cellOf(maxZ);
        for (int iz = z0; iz <= z1; ++iz)
        {
            for (int ix = x0; ix <= x1; ++ix)
            {
                cells[(size_t)iz * gridSide + ix].push_back(index);
            }
        }
    };
    for (size_t i = 0; i < segments.size(); ++i)
    {
        const Segment& segment = segments[i];
        mark(cellSegments, (uint32_t)i, std::min(segment.a.x, segment.b.x) - reach, std::min(segment.a.y, segment.b.y) - reach,
             std::max(segment.a.x, segment.b.x) + reach, std::max(segment.a.y, segment.b.y) + reach);
    }
    for (size_t i = 0; i < zones.size(); ++i)
    {
        const float r = zones[i].radius + kFarDistance;
        mark(cellZones, (uint32_t)i, zones[i].center.x - r, zones[i].center.y - r, zones[i].center.x + r, zones[i].center.y + r);
    }
}

float Layout::RoadDistance(float x, float z) const
{
    float best = kFarDistance;
    for (uint32_t index : cellSegments[CellIndex(x, z)])
    {
        const Segment& segment = segments[index];
        best = std::min(best, DistanceToSegment(x, z, segment.a, segment.b) - segment.halfWidth);
    }
    return best;
}

float Layout::FlatDistance(float x, float z) const
{
    const int cell = CellIndex(x, z);
    float best = kFarDistance;
    for (uint32_t index : cellSegments[cell])
    {
        const Segment& segment = segments[index];
        best = std::min(best, DistanceToSegment(x, z, segment.a, segment.b) - kRoadFlatMargin);
    }
    for (uint32_t index : cellZones[cell])
    {
        const FlatZone& zone = zones[index];
        const float dx = x - zone.center.x;
        const float dz = z - zone.center.y;
        best = std::min(best, std::sqrt(dx * dx + dz * dz) - zone.radius);
    }
    return std::min(best, AirfieldFlatDistance(x, z));
}

bool Layout::RoadAt(float x, float z, RoadKind& kind) const
{
    // Засмал зам эхэнд бүртгэгдсэн тул огтлолцол дээр засмал зам давамгайлна.
    for (uint32_t index : cellSegments[CellIndex(x, z)])
    {
        const Segment& segment = segments[index];
        if (DistanceToSegment(x, z, segment.a, segment.b) <= segment.halfWidth)
        {
            kind = segment.kind;
            return true;
        }
    }
    if (Paved(x, z))
    {
        kind = RoadKind::Asphalt;
        return true;
    }
    return false;
}

float Layout::HighwayZ(float x) const
{
    const std::vector<Vector2>& points = roads[0].points;
    for (size_t i = 0; i + 1 < points.size(); ++i)
    {
        if (x <= points[i + 1].x)
        {
            const float t = (x - points[i].x) / (points[i + 1].x - points[i].x);
            return points[i].y + (points[i + 1].y - points[i].y) * std::clamp(t, 0.0f, 1.0f);
        }
    }
    return points.back().y;
}

const char* TownName(int province, bool& city)
{
    static const char* const names[] = {
        "Өлгий",       "Улаангом",  "Ховд",       "Улиастай",  "Алтай",    "Мөрөн",     "Баянхонгор", "Цэцэрлэг",
        "Булган",      "Арвайхээр", "Эрдэнэт",    "Даланзадгад", "Сүхбаатар", "Дархан",  "Зуунмод",   "Улаанбаатар",
        "Мандалговь",  "Чойр",      "Сайншанд",   "Чингис",    "Баруун-Урт", "Чойбалсан",
    };
    city = province == 10 || province == 13 || province == 15 || province == 19;
    return province >= 0 && province < (int)(sizeof(names) / sizeof(names[0])) ? names[province] : "Аймгийн төв";
}

Layout MakeLayout()
{
    Layout layout;
    const float edge = kWorldHalfExtent + 100.0f; // замууд газрын захаас цааш гарна

    // --- Засмал зам: ертөнцийг баруунаас зүүн тийш бүтэн хөндлөн гарна. Тоглогч (0, 0)-д зам дээр эхэлнэ. ---
    // Төв хэсэг (суурин, цэргийн постын орчим) шулуун. Зүүн тийш (-X) Буянт-Ухаа нисэх буудлын ХБЗ-ын
    // өмнөд үзүүрийг тойрч урагш эргэнэ (нисэх буудлын тэгш талбай бүх үед ижил).
    Road highway = { RoadKind::Asphalt, 4.0f, { { -edge, -700.0f } } };
    for (const Vector2 p : { Vector2{ -2900.0f, -820.0f }, Vector2{ -2600.0f, -960.0f }, Vector2{ -2300.0f, -1060.0f },
                             Vector2{ -2050.0f, -1090.0f }, Vector2{ -1800.0f, -1040.0f }, Vector2{ -1550.0f, -960.0f },
                             Vector2{ -1300.0f, -880.0f }, Vector2{ -1050.0f, -800.0f }, Vector2{ -800.0f, -700.0f },
                             Vector2{ -620.0f, -590.0f }, Vector2{ -500.0f, -460.0f }, Vector2{ -400.0f, -320.0f },
                             Vector2{ -300.0f, -150.0f }, Vector2{ -190.0f, -30.0f } })
    {
        highway.points.push_back(p);
    }
    highway.points.push_back({ -70.0f, 0.0f });
    highway.points.push_back({ 70.0f, 0.0f });
    highway.points.push_back({ 250.0f, -10.0f });
    for (float x = 530.0f; x <= edge; x += 280.0f)
    {
        highway.points.push_back({ x, -10.0f - 110.0f * std::sin((x - 250.0f) * 0.0029f) });
    }
    layout.roads.push_back(highway);

    // --- Төвийн гурван шороон зам: суурингаас хойш, засмал замаас урагш хоёр ---
    layout.roads.push_back({ RoadKind::Dirt, 2.2f,
                             { { 25.0f, 3.0f }, { 25.0f, 30.0f }, { 30.0f, 62.0f }, { 48.0f, 95.0f }, { 80.0f, 150.0f },
                               { 115.0f, 250.0f }, { 150.0f, 400.0f }, { 170.0f, 560.0f }, { 240.0f, 820.0f },
                               { 380.0f, 1100.0f }, { 360.0f, 1500.0f }, { 320.0f, 1900.0f }, { 280.0f, 2400.0f },
                               { 300.0f, edge } } });
    layout.roads.push_back({ RoadKind::Dirt, 2.2f,
                             { { -140.0f, -1.0f }, { -150.0f, -80.0f }, { -172.0f, -165.0f }, { -215.0f, -300.0f },
                               { -240.0f, -560.0f }, { -330.0f, -900.0f }, { -260.0f, -1300.0f }, { -380.0f, -1700.0f },
                               { -340.0f, -2200.0f }, { -360.0f, -edge } } });
    layout.roads.push_back({ RoadKind::Dirt, 2.2f,
                             { { 215.0f, -11.0f }, { 205.0f, -100.0f }, { 170.0f, -215.0f }, { 180.0f, -380.0f },
                               { 230.0f, -560.0f }, { 330.0f, -900.0f }, { 280.0f, -1300.0f }, { 420.0f, -1750.0f },
                               { 380.0f, -2300.0f }, { 400.0f, -edge } } });

    // --- Аймгийн төв / хот: гол өргөн чөлөө (засмал) засмал замаас хойш, бусад гудамж шороон ---
    {
        const float x0 = kTownStreets[0], x1 = kTownStreets[4], z0 = kTownRows[0], z1 = kTownRows[4];
        layout.roads.push_back({ RoadKind::Asphalt, 3.6f, { { kTownStreets[2], layout.HighwayZ(kTownStreets[2]) - 1.0f },
                                                            { kTownStreets[2], z1 + 6.0f } } });
        layout.roads.push_back({ RoadKind::Asphalt, 3.6f, { { x0 - 6.0f, kTownRows[2] }, { x1 + 6.0f, kTownRows[2] } } });
        for (int i : { 0, 1, 3, 4 })
        {
            layout.roads.push_back({ RoadKind::Dirt, 3.0f, { { kTownStreets[i], z0 - 4.0f }, { kTownStreets[i], z1 + 4.0f } } });
            layout.roads.push_back({ RoadKind::Dirt, 3.0f, { { x0 - 4.0f, kTownRows[i] }, { x1 + 4.0f, kTownRows[i] } } });
        }
    }

    // Төвийн замуудын дагуух жижиг суурингууд
    layout.hamlets.push_back({ 442.0f, 1100.0f });
    layout.hamlets.push_back({ -208.0f, -1300.0f });
    layout.hamlets.push_back({ 332.0f, -1300.0f });

    // --- Засмал замаас салах шороон замууд: хойш, урагш ээлжлэн ---
    const struct { float x; float direction; } branches[] = {
        { -2350.0f, 1.0f }, { -1500.0f, -1.0f }, { -1050.0f, -1.0f }, { 880.0f, 1.0f }, { 1450.0f, -1.0f }, { 2000.0f, 1.0f },
    };
    for (const auto& branch : branches)
    {
        const float startZ = layout.HighwayZ(branch.x);
        Road road = { RoadKind::Dirt, 2.2f, { { branch.x, startZ - branch.direction } } };
        for (int step = 1; step <= 12; ++step)
        {
            const float z = startZ + branch.direction * 250.0f * (float)step;
            const float x = branch.x + 110.0f * std::sin((float)step * 1.3f + branch.x * 0.01f);
            road.points.push_back({ x, z });

            // Засмал замаас 750 м ба 1750 м-т замын хажууд суурин
            if (step == 3 || step == 7)
            {
                layout.hamlets.push_back({ x + 62.0f, z });
            }
        }
        layout.roads.push_back(road);
    }

    // --- Тэгш бүсүүд ---
    layout.zones.push_back({ { 28.0f, 34.0f }, 58.0f }); // төв суурин + шатахуун түгээх газар
    layout.zones.push_back({ { -50.0f, 0.0f }, 32.0f }); // цэргийн пост
    layout.zones.push_back({ kTownCenter, 162.0f });     // аймгийн төв (гудамжны тор)
    layout.BuildAirfield();

    // Нуурууд: зам, суурин, нисэх буудлаас хол байрлах боломжтойг нь л үлдээнэ.
    {
        layout.BuildIndex();
        const Lake candidates[] = { { { 1150.0f, 760.0f }, 70.0f }, { { 860.0f, -1060.0f }, 60.0f },
                                    { { -1650.0f, 1280.0f }, 55.0f }, { { -700.0f, -1500.0f }, 65.0f },
                                    { { 1550.0f, -300.0f }, 50.0f }, { { -2150.0f, -250.0f }, 60.0f } };
        for (const Lake& lake : candidates)
        {
            bool clear = layout.FlatDistance(lake.center.x, lake.center.y) >= Layout::kFarDistance;
            for (int i = 0; i < 16 && clear; ++i)
            {
                const float a = 2.0f * PI * (float)i / 16.0f;
                const float r = lake.radius + 25.0f;
                clear = layout.FlatDistance(lake.center.x + std::cos(a) * r, lake.center.y + std::sin(a) * r) >= Layout::kFarDistance;
            }
            if (!clear) continue;
            layout.lakes.push_back(lake);
            layout.zones.push_back({ lake.center, lake.radius + 6.0f }); // эрэг тэгш
        }
    }
    for (const Vector2& hamlet : layout.hamlets)
    {
        layout.zones.push_back({ hamlet, 46.0f });
    }
    layout.BuildIndex();

    // --- Малчдын отрууд: эхний зургаа нь тогтмол байрлалтай, бусад нь талаар тарсан ---
    layout.camps = { { -182.0f, -178.0f }, { 470.0f, 262.0f }, { 150.0f, -232.0f },
                     { -150.0f, 180.0f },  { 330.0f, -150.0f }, { -90.0f, 290.0f } };

    Rng rng(90210u);
    for (int attempt = 0; attempt < 4000 && layout.camps.size() < 90; ++attempt)
    {
        const Vector2 candidate = { rng.Range(-kPlayableHalfExtent + 60.0f, kPlayableHalfExtent - 60.0f),
                                    rng.Range(-kPlayableHalfExtent + 60.0f, kPlayableHalfExtent - 60.0f) };

        // Замаас холгүй ч зам дээр биш, бусад суурин, отроос зайтай
        if (layout.FlatDistance(candidate.x, candidate.y) < 30.0f) continue;
        if (std::fabs(candidate.x) < 420.0f && std::fabs(candidate.y) < 420.0f) continue; // төвийн хэсэг гараар байрлуулсан

        bool crowded = false;
        for (const Vector2& other : layout.camps)
        {
            const float dx = other.x - candidate.x, dz = other.y - candidate.y;
            crowded = crowded || dx * dx + dz * dz < 260.0f * 260.0f;
        }
        if (!crowded)
        {
            layout.camps.push_back(candidate);
        }
    }

    for (const Vector2& camp : layout.camps)
    {
        layout.zones.push_back({ camp, 26.0f });
    }
    layout.BuildIndex();

    return layout;
}

// ---------------------------------------------------------------- Улаанбаатарын төв

namespace
{
    // Олон өнцөгтийг торын нүднүүдэд (нүдний төвөөр) дүүргэнэ: мөр бүрд огтлолцлын цэгүүдийн хооронд.
    template <typename Fill>
    void ScanPolygon(const std::vector<Vector2>& outline, float cell, int cells, Fill fill)
    {
        float minZ = 1e9f, maxZ = -1e9f;
        for (const Vector2& p : outline)
        {
            minZ = std::min(minZ, p.y);
            maxZ = std::max(maxZ, p.y);
        }
        const int z0 = std::max(0, (int)std::floor((minZ + kWorldHalfExtent) / cell));
        const int z1 = std::min(cells - 1, (int)std::floor((maxZ + kWorldHalfExtent) / cell));
        std::vector<float> xs;
        for (int iz = z0; iz <= z1; ++iz)
        {
            const float z = -kWorldHalfExtent + ((float)iz + 0.5f) * cell;
            xs.clear();
            for (size_t i = 0, j = outline.size() - 1; i < outline.size(); j = i++)
            {
                const Vector2 a = outline[i], b = outline[j];
                if ((a.y > z) != (b.y > z)) xs.push_back(a.x + (z - a.y) * (b.x - a.x) / (b.y - a.y));
            }
            std::sort(xs.begin(), xs.end());
            for (size_t k = 0; k + 1 < xs.size(); k += 2)
            {
                const int x0 = std::max(0, (int)std::ceil((xs[k] + kWorldHalfExtent) / cell - 0.5f));
                const int x1 = std::min(cells - 1, (int)std::floor((xs[k + 1] + kWorldHalfExtent) / cell - 0.5f));
                for (int ix = x0; ix <= x1; ++ix) fill(ix, iz);
            }
        }
    }
}

Layout MakeCityLayout(const CityData& data)
{
    Layout layout;
    layout.city = true;
    layout.capital = data.province == kUlaanbaatarProvince;
    layout.mountains = data.mountains;
    layout.nearDem = data.nearDem;
    layout.farDem = data.farDem;

    // Даалгаврын байгууламжууд тэгш газарт (Structures-ийн загварууд газрын түвшин 0-д барина):
    // застав (хуаран, дэлгүүр), ШТС, агуулах, хот айл, жижиг суурин
    layout.zones.push_back({ { -40.0f, 0.0f }, 55.0f });
    for (const CityPoi& poi : data.pois)
    {
        const Vector2 p = poi.position;
        switch (poi.kind)
        {
        case CityPoiKind::Station: layout.zones.push_back({ { p.x + 35.0f, p.y - 11.0f }, 24.0f }); break;
        case CityPoiKind::Warehouse: layout.zones.push_back({ { p.x + 5.0f, p.y }, 20.0f }); break;
        case CityPoiKind::Camp:
            layout.camps.push_back(p);
            layout.zones.push_back({ p, 26.0f });
            break;
        case CityPoiKind::Hamlet:
            layout.hamlets.push_back(p);
            layout.zones.push_back({ { p.x - 4.0f, p.y }, 46.0f });
            break;
        default: break;
        }
    }

    // Гудамжууд: засмал нь эхэнд (огтлолцол дээр засмал давамгайлна). Явган гудамж засмал.
    for (int pass = 0; pass < 2; ++pass)
    {
        for (const CityRoad& road : data.roads)
        {
            const bool dirt = road.dirt || road.klass == CityRoadClass::Track;
            if (dirt != (pass == 1) || road.points.size() < 2) continue;
            layout.roads.push_back({ dirt ? RoadKind::Dirt : RoadKind::Asphalt, road.halfWidth, road.points });
        }
    }
    layout.BuildIndex();
    if (layout.HasDem())
    {
        // Тэгш бүс бүр өөрийн талбайн дундаж өндөрт тэгшлэгдэнэ (төвийнх биш: газар бага ухагдаж, бага овойно)
        for (FlatZone& zone : layout.zones)
        {
            float sum = layout.DemHeight(zone.center.x, zone.center.y);
            int count = 1;
            for (const float ring : { 0.5f, 1.0f })
            {
                for (int k = 0; k < 8; ++k)
                {
                    const float angle = (float)k * PI * 0.25f + ring;
                    sum += layout.DemHeight(zone.center.x + std::cos(angle) * zone.radius * ring,
                                            zone.center.y + std::sin(angle) * zone.radius * ring);
                    ++count;
                }
            }
            zone.level = sum / (float)count;
        }
    }

    // Барилгаас хэр хол: барилгатай нүднүүдээс эхлэн (8 хөрш, 24 м алхмаар) тархаана
    {
        const int cells = (int)std::ceil(kWorldHalfExtent * 2.0f / Layout::kBuiltCell);
        layout.builtCells = cells;
        layout.builtDistance.assign((size_t)cells * cells, 600);
        std::vector<int> queue;
        for (const CityBuilding& b : data.buildings)
        {
            for (const Vector2& p : b.outline)
            {
                const int ix = (int)((p.x + kWorldHalfExtent) / Layout::kBuiltCell), iz = (int)((p.y + kWorldHalfExtent) / Layout::kBuiltCell);
                if (ix < 0 || iz < 0 || ix >= cells || iz >= cells) continue;
                uint16_t& d = layout.builtDistance[(size_t)iz * cells + ix];
                if (d != 0) queue.push_back(iz * cells + ix);
                d = 0;
            }
        }
        const uint16_t step = (uint16_t)Layout::kBuiltCell;
        for (size_t head = 0; head < queue.size(); ++head)
        {
            const int k = queue[head], ix = k % cells, iz = k / cells;
            const uint16_t next = (uint16_t)(layout.builtDistance[(size_t)k] + step);
            if (next >= 600) continue;
            for (int dz = -1; dz <= 1; ++dz)
            {
                for (int dx = -1; dx <= 1; ++dx)
                {
                    const int nx = ix + dx, nz = iz + dz;
                    if ((dx == 0 && dz == 0) || nx < 0 || nz < 0 || nx >= cells || nz >= cells) continue;
                    uint16_t& d = layout.builtDistance[(size_t)nz * cells + nx];
                    if (d > next)
                    {
                        d = next;
                        queue.push_back(nz * cells + nx);
                    }
                }
            }
        }
    }

    // Газрын бүрхэвч ба усны тор
    const float cell = Layout::kCityCell;
    const int cells = (int)std::ceil(kWorldHalfExtent * 2.0f / cell);
    layout.cityCells = cells;
    layout.cityGround.assign((size_t)cells * cells, 0);
    layout.cityWater.assign((size_t)cells * cells, 0);

    static const uint8_t kGroundOf[] = { 6, 1, 2, 3, 4, 5 }; // CityAreaKind -> CityGround
    // Том талбайг эхэлж, жижгийг сүүлд нь: цэцэрлэг доторх хучилттай талбай, спорт талбай харагдана.
    std::vector<const CityArea*> areas;
    for (const CityArea& area : data.areas) areas.push_back(&area);
    auto areaSize = [](const CityArea* a) {
        float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
        for (const Vector2& p : a->outline)
        {
            minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
            minZ = std::min(minZ, p.y); maxZ = std::max(maxZ, p.y);
        }
        return (maxX - minX) * (maxZ - minZ);
    };
    std::stable_sort(areas.begin(), areas.end(), [&](const CityArea* a, const CityArea* b) { return areaSize(a) > areaSize(b); });
    for (const CityArea* area : areas)
    {
        const uint8_t ground = kGroundOf[(int)area->kind];
        ScanPolygon(area->outline, cell, cells, [&](int ix, int iz) {
            const size_t k = (size_t)iz * cells + ix;
            layout.cityGround[k] = ground;
            if (ground == 6) layout.cityWater[k] = 60; // цөөрөм, голын сав: 1.5 м
        });
    }

    // Гол, горхи: тэнхлэгээс холдох тусам гүехэн (хашаа, хана, төмөр зам ус биш)
    for (const CityLine& line : data.lines)
    {
        if (line.kind != CityLineKind::River && line.kind != CityLineKind::Stream) continue;
        const float half = line.halfWidth + 3.0f; // эрэг налуу
        for (size_t i = 0; i + 1 < line.points.size(); ++i)
        {
            const Vector2 a = line.points[i], b = line.points[i + 1];
            const int x0 = std::max(0, (int)((std::min(a.x, b.x) - half + kWorldHalfExtent) / cell));
            const int x1 = std::min(cells - 1, (int)((std::max(a.x, b.x) + half + kWorldHalfExtent) / cell));
            const int z0 = std::max(0, (int)((std::min(a.y, b.y) - half + kWorldHalfExtent) / cell));
            const int z1 = std::min(cells - 1, (int)((std::max(a.y, b.y) + half + kWorldHalfExtent) / cell));
            for (int iz = z0; iz <= z1; ++iz)
            {
                for (int ix = x0; ix <= x1; ++ix)
                {
                    const float x = -kWorldHalfExtent + ((float)ix + 0.5f) * cell, z = -kWorldHalfExtent + ((float)iz + 0.5f) * cell;
                    const float d = DistanceToSegment(x, z, a, b);
                    if (d > half) continue;
                    const float depth = (line.kind == CityLineKind::River ? 2.0f : 1.0f) * SmoothStep(half, line.halfWidth * 0.5f, d);
                    const size_t k = (size_t)iz * cells + ix;
                    layout.cityWater[k] = std::max(layout.cityWater[k], (uint8_t)std::min(255.0f, depth * 40.0f));
                    if (d <= line.halfWidth) layout.cityGround[k] = 6;
                }
            }
        }
    }

    // Булган уулын цогцолбор (Цэцэрлэг): Зуу бурханы хийд = шатны доод үзүүрийн дэргэдэх Заяын хүрээнээс бусад,
    // суварганаас 300 м дотор хамгийн ойрын сүм хийдийн барилга. Будда хийдийн яг урд (шатны зүгт) дугуй талбайн
    // голд; хаалга нь түүнээс доош шатны чигээр бэлийн гудамжны дэргэд; амрах талбай шатны дунд.
    for (Mountain& m : layout.mountains)
    {
        if (m.stairs.x > 1e8f || m.stupa.x > 1e8f) continue;
        float best = 300.0f;
        const CityBuilding* temple = nullptr;
        Vector2 templeCenter = {};
        for (const CityBuilding& b : data.buildings)
        {
            if (b.type != CityBuildingType::Religious || b.outline.empty()) continue;
            Vector2 c = { 0.0f, 0.0f };
            for (const Vector2& p : b.outline) c = Vector2Add(c, p);
            c = Vector2Scale(c, 1.0f / (float)b.outline.size());
            if (Vector2Distance(c, m.stairs) < 150.0f) continue;
            const float distance = Vector2Distance(c, m.stupa);
            if (distance < best)
            {
                best = distance;
                temple = &b;
                templeCenter = c;
            }
        }
        m.buddha = m.stupa;
        if (temple)
        {
            const Vector2 toGate = Vector2Normalize(Vector2Subtract(m.stairs, templeCenter));
            float extent = 0.0f;
            for (const Vector2& p : temple->outline) extent = std::max(extent, Vector2DotProduct(Vector2Subtract(p, templeCenter), toGate));
            m.buddha = Vector2Add(templeCenter, Vector2Scale(toGate, extent + m.buddhaRadius + 1.0f));
        }
        const float length = Vector2Distance(m.buddha, m.stairs);
        const Vector2 down = Vector2Scale(Vector2Subtract(m.stairs, m.buddha), 1.0f / std::max(1.0f, length));
        m.gate = m.stairs;
        for (float along = 60.0f; along < length; along += 1.0f)
        {
            const Vector2 q = Vector2Add(m.buddha, Vector2Scale(down, along));
            if (layout.RoadDistance(q.x, q.y) < 9.0f)
            {
                m.gate = q;
                break;
            }
        }
        // Шат: хаалганаас Буддагийн талбайн зах хүртэл; амрах талбай түүний дунд
        const Vector2 plazaEdge = Vector2Subtract(m.buddha, Vector2Scale(Vector2Normalize(Vector2Subtract(m.buddha, m.gate)), m.buddhaRadius));
        m.rest = Vector2Lerp(m.gate, plazaEdge, 0.5f);
    }
    return layout;
}
