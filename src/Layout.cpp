#include "Layout.h"

#include "Procedural.h"

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
    const float reach = kFarDistance + kRoadFlatMargin;
    for (int iz = 0; iz < gridSide; ++iz)
    {
        for (int ix = 0; ix < gridSide; ++ix)
        {
            const float x0 = -kWorldHalfExtent + (float)ix * kCellSize, x1 = x0 + kCellSize;
            const float z0 = -kWorldHalfExtent + (float)iz * kCellSize, z1 = z0 + kCellSize;
            const size_t cell = (size_t)iz * gridSide + ix;

            for (size_t s = 0; s < segments.size(); ++s)
            {
                const Segment& segment = segments[s];
                if (std::max(segment.a.x, segment.b.x) < x0 - reach || std::min(segment.a.x, segment.b.x) > x1 + reach ||
                    std::max(segment.a.y, segment.b.y) < z0 - reach || std::min(segment.a.y, segment.b.y) > z1 + reach)
                {
                    continue;
                }
                cellSegments[cell].push_back((uint16_t)s);
            }

            for (size_t i = 0; i < zones.size(); ++i)
            {
                const float r = zones[i].radius + kFarDistance;
                if (zones[i].center.x + r < x0 || zones[i].center.x - r > x1 || zones[i].center.y + r < z0 ||
                    zones[i].center.y - r > z1)
                {
                    continue;
                }
                cellZones[cell].push_back((uint16_t)i);
            }
        }
    }
}

float Layout::RoadDistance(float x, float z) const
{
    float best = kFarDistance;
    for (uint16_t index : cellSegments[CellIndex(x, z)])
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
    for (uint16_t index : cellSegments[cell])
    {
        const Segment& segment = segments[index];
        best = std::min(best, DistanceToSegment(x, z, segment.a, segment.b) - kRoadFlatMargin);
    }
    for (uint16_t index : cellZones[cell])
    {
        const FlatZone& zone = zones[index];
        const float dx = x - zone.center.x;
        const float dz = z - zone.center.y;
        best = std::min(best, std::sqrt(dx * dx + dz * dz) - zone.radius);
    }
    return best;
}

bool Layout::RoadAt(float x, float z, RoadKind& kind) const
{
    // Засмал зам эхэнд бүртгэгдсэн тул огтлолцол дээр засмал зам давамгайлна.
    for (uint16_t index : cellSegments[CellIndex(x, z)])
    {
        const Segment& segment = segments[index];
        if (DistanceToSegment(x, z, segment.a, segment.b) <= segment.halfWidth)
        {
            kind = segment.kind;
            return true;
        }
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

Layout MakeLayout()
{
    Layout layout;
    const float edge = kWorldHalfExtent + 100.0f; // замууд газрын захаас цааш гарна

    // --- Засмал зам: ертөнцийг баруунаас зүүн тийш бүтэн хөндлөн гарна. Тоглогч (0, 0)-д зам дээр эхэлнэ. ---
    // Төв хэсэг (суурин, цэргийн постын орчим) шулуун, хоёр тийшээ аажим тахирлана.
    Road highway = { RoadKind::Asphalt, 4.0f, {} };
    for (float x = -edge; x < -250.0f; x += 280.0f)
    {
        highway.points.push_back({ x, 6.0f + 95.0f * std::sin((-x - 250.0f) * 0.0033f) });
    }
    highway.points.push_back({ -250.0f, 6.0f });
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
                               { 115.0f, 250.0f }, { 150.0f, 400.0f }, { 170.0f, 560.0f }, { 140.0f, 820.0f },
                               { 260.0f, 1100.0f }, { 200.0f, 1500.0f }, { 320.0f, 1900.0f }, { 280.0f, 2400.0f },
                               { 300.0f, edge } } });
    layout.roads.push_back({ RoadKind::Dirt, 2.2f,
                             { { -140.0f, -1.0f }, { -150.0f, -80.0f }, { -172.0f, -165.0f }, { -215.0f, -300.0f },
                               { -240.0f, -560.0f }, { -330.0f, -900.0f }, { -260.0f, -1300.0f }, { -380.0f, -1700.0f },
                               { -340.0f, -2200.0f }, { -360.0f, -edge } } });
    layout.roads.push_back({ RoadKind::Dirt, 2.2f,
                             { { 215.0f, -11.0f }, { 205.0f, -100.0f }, { 170.0f, -215.0f }, { 180.0f, -380.0f },
                               { 230.0f, -560.0f }, { 330.0f, -900.0f }, { 280.0f, -1300.0f }, { 420.0f, -1750.0f },
                               { 380.0f, -2300.0f }, { 400.0f, -edge } } });

    // Төвийн замуудын дагуух жижиг суурингууд
    layout.hamlets.push_back({ 312.0f, 1100.0f });
    layout.hamlets.push_back({ -208.0f, -1300.0f });
    layout.hamlets.push_back({ 332.0f, -1300.0f });

    // --- Засмал замаас салах шороон замууд: хойш, урагш ээлжлэн ---
    const struct { float x; float direction; } branches[] = {
        { -2050.0f, 1.0f }, { -1500.0f, -1.0f }, { -950.0f, 1.0f }, { 880.0f, 1.0f }, { 1450.0f, -1.0f }, { 2000.0f, 1.0f },
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
    for (const Vector2& hamlet : layout.hamlets)
    {
        layout.zones.push_back({ hamlet, 46.0f });
    }
    layout.BuildIndex();

    // --- Малчдын отрууд: эхний зургаа нь тогтмол байрлалтай, бусад нь талаар тарсан ---
    layout.camps = { { -182.0f, -178.0f }, { 228.0f, 172.0f }, { 150.0f, -232.0f },
                     { -276.0f, 204.0f },  { 330.0f, -150.0f }, { -90.0f, 290.0f } };

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
