#include "World.h"

#include "VehicleModel.h"

#include "City.h"
#include "Config.h"
#include "Procedural.h"
#include "Structures.h"
#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
    const float kPieceSize = 240.0f;          // хөдөлгөөнгүй геометрийг хуваах бүсийн хэмжээ
    const float kSceneryNear = 55.0f;         // үүнээс ойр мод, торхыг текстуртай загвараар зурна
    const float kStructureDistance = 720.0f;  // барилга, замыг зурах дээд зай (цааш нь мананд уусна)
    const float kShadowDistance = 320.0f;
    const float kGlassDistance = 320.0f;
    const float kBoundaryDistance = 520.0f;
    const float kGridCell = 32.0f;            // саадын торын нүдний хэмжээ
    const int kStreamBudgetPerFrame = 6;      // нэг frame-д үүсгэх газрын mesh-ийн ажлын хэмжээ
}

World::World(float halfSize) : halfSize(halfSize), layout(MakeLayout()), terrain(layout)
{
    // Тогтмол хэсэг: зам, пост, агуулах, шон, чулуу. Нэг удаа үүснэ (Улаанбаатарын хотод орох, гарахад дахин).
    BuildStatic();
    terrain.SetObstacles(&obstacles); // өвс, чулууг саад дотор ургуулахгүй

    // Суурингууд: эхлээд зохиомол хээр талаар (99: аль ч аймгийн хот биш); үе эхлэх бүрд тухайн үеийнхээр солигдоно.
    BuildSettlements(99);

    // Даалгавар эхлэх хүртэл: бүх талбарыг хамарсан дөрвөлжин хязгаар (тэмдэг шонгүй).
    const float edge = halfSize - 1.0f;
    Shape square;
    square.outline = { { -edge, -edge }, { edge, -edge }, { edge, edge }, { -edge, edge } };
    SetBoundary(square, false);
}

World::~World()
{
    for (MeshPiece& piece : farLand) UnloadMesh(piece.mesh);
    UnloadPieces(structures);
    UnloadPieces(shadows);
    UnloadPieces(glass);
    UnloadPieces(settlementStructures);
    UnloadPieces(settlementShadows);
    UnloadPieces(settlementGlass);
    UnloadPieces(fences);
    UnloadPieces(settlementFences);
    UnloadPieces(sceneryPieces);
    UnloadPieces(settlementSceneryPieces);
    UnloadPieces(boundaryPosts);
    UnloadPieces(boundaryBand);
}

void World::BuildStatic()
{
    UnloadPieces(structures);
    UnloadPieces(shadows);
    UnloadPieces(glass);
    UnloadPieces(fences);
    UnloadPieces(settlementFences);
    UnloadPieces(sceneryPieces);
    UnloadPieces(settlementSceneryPieces);
    scenery.clear();
    sceneryCpu.clear();
    sceneryNear.clear();
    obstacles.clear();
    interiors.clear();
    doors.clear();
    passages.clear();
    walls.clear();
    panels.clear();
    brokenPanels.clear();

    MeshBuilder solidBuilder, shadowBuilder, glassBuilder, sceneryBuilder;
    std::vector<SceneryPlacement> placements;
    BuildStaticStructures(layout, terrain, obstacles, solidBuilder, shadowBuilder, glassBuilder, interiors, doors,
                          passages, walls, panels, sceneryBuilder, placements);
    sceneryPieces = UploadScenery(sceneryBuilder, placements, false);
    staticSceneryCount = scenery.size();
    staticSceneryCpu = sceneryCpu.size();
    fences = ExtractFences(solidBuilder, 0, false);
    staticPanelCount = panels.size();
    staticPassageCount = passages.size();
    staticWallCount = walls.size();
    staticInteriorCount = interiors.size();
    staticDoorCount = doors.size();

    structures = solidBuilder.UploadPartitioned(kPieceSize);
    shadows = shadowBuilder.UploadPartitioned(kPieceSize);
    glass = glassBuilder.UploadPartitioned(kPieceSize);
    staticObstacleCount = obstacles.size();

    TraceLog(LOG_INFO, "WORLD: %d static structure triangles in %d pieces, %d colliders, %d fence walls",
             solidBuilder.TriangleCount(), (int)structures.size(), (int)obstacles.size(), (int)walls.size());
}

void World::SwitchCity(int province)
{
    const double start = GetTime();
    if (cityProvince < 0) steppeLayout = std::move(layout); // хээр талаас гарахдаа хадгална
    cityProvince = province;
    if (province >= 0)
    {
        layout = MakeCityLayout(CityFor(province));
    }
    else
    {
        layout = std::move(steppeLayout);
        steppeLayout = Layout();
    }
    terrain.Rebuild();
    BuildStatic();
    BuildFarLand();
    TraceLog(LOG_INFO, "WORLD: %s layout ready in %.2f s", province >= 0 ? "city" : "steppe", GetTime() - start);
}

void World::BuildFarLand()
{
    for (MeshPiece& piece : farLand) UnloadMesh(piece.mesh);
    farLand.clear();
    const HeightGrid& grid = layout.FarDem();
    if (grid.Empty()) return;
    const CityData& city = CurrentCity();
    const int n = grid.side;
    const float cell = grid.cell, half = grid.half;
    auto at = [&](int ix, int iz) { return grid.values[(size_t)std::clamp(iz, 0, n - 1) * n + std::clamp(ix, 0, n - 1)]; };

    // Ус (1), ой (2): олон өнцөгтүүдийг торын оройнуудад
    std::vector<uint8_t> cover((size_t)n * n, 0);
    std::vector<float> xs;
    for (const CityArea& area : city.farAreas)
    {
        const uint8_t value = area.kind == CityAreaKind::Forest ? 2 : 1;
        float minZ = 1e9f, maxZ = -1e9f;
        for (const Vector2& p : area.outline) { minZ = std::min(minZ, p.y); maxZ = std::max(maxZ, p.y); }
        const int z0 = std::max(0, (int)std::ceil((minZ + half) / cell)), z1 = std::min(n - 1, (int)std::floor((maxZ + half) / cell));
        for (int iz = z0; iz <= z1; ++iz)
        {
            const float z = -half + (float)iz * cell;
            xs.clear();
            for (size_t i = 0, j = area.outline.size() - 1; i < area.outline.size(); j = i++)
            {
                const Vector2 a = area.outline[i], b = area.outline[j];
                if ((a.y > z) != (b.y > z)) xs.push_back(a.x + (z - a.y) * (b.x - a.x) / (b.y - a.y));
            }
            std::sort(xs.begin(), xs.end());
            for (size_t k = 0; k + 1 < xs.size(); k += 2)
            {
                const int x0 = std::max(0, (int)std::ceil((xs[k] + half) / cell)), x1 = std::min(n - 1, (int)std::floor((xs[k + 1] + half) / cell));
                for (int ix = x0; ix <= x1; ++ix)
                {
                    uint8_t& c = cover[(size_t)iz * n + ix];
                    if (c != 1) c = value; // ус ойг дарна
                }
            }
        }
    }

    // Тоглох талбайн доор (ойрын газар зурагдана) бага зэрэг доогуур: голын сав, хотын тэгш газар цоолж гарахгүй
    const float inner = kWorldHalfExtent - 60.0f;
    auto position = [&](int ix, int iz) {
        const float x = -half + (float)ix * cell, z = -half + (float)iz * cell;
        const bool under = std::fabs(x) < inner && std::fabs(z) < inner;
        return Vector3{ x, at(ix, iz) - (under ? 3.0f : 0.6f), z };
    };
    auto normal = [&](int ix, int iz) {
        return Vector3Normalize({ (at(ix - 1, iz) - at(ix + 1, iz)) / (2.0f * cell), 1.0f, (at(ix, iz - 1) - at(ix, iz + 1)) / (2.0f * cell) });
    };
    auto color = [&](int ix, int iz, Vector3 nrm) {
        const float h = at(ix, iz);
        const float x = -half + (float)ix * cell, z = -half + (float)iz * cell;
        const float grain = ValueNoise(x * 0.004f + 3.0f, z * 0.004f + 7.0f);
        Vector3 c = Vector3Lerp({ 0.44f, 0.48f, 0.28f }, { 0.58f, 0.52f, 0.35f }, SmoothStep(60.0f, 500.0f, h) * 0.8f + grain * 0.15f);
        c = Vector3Lerp(c, Vector3Lerp({ 0.45f, 0.42f, 0.39f }, { 0.32f, 0.31f, 0.31f }, grain), SmoothStep(0.86f, 0.62f, nrm.y) * 0.85f);
        const uint8_t kind = cover[(size_t)iz * n + ix];
        if (kind == 2) c = Vector3Lerp(c, { 0.24f, 0.33f, 0.21f }, 0.85f);           // ой
        const float snowLine = 1050.0f + grain * 250.0f;                               // хотоос дээш (Алтай, Хангай)
        c = Vector3Lerp(c, { 0.92f, 0.93f, 0.96f }, SmoothStep(snowLine, snowLine + 150.0f, h) * SmoothStep(0.55f, 0.8f, nrm.y));
        if (kind == 1) c = { 0.27f, 0.41f, 0.51f };                                   // нуур, гол
        return c;
    };

    const int piece = 24;
    for (int pz = 0; pz < n - 1; pz += piece)
    {
        for (int px = 0; px < n - 1; px += piece)
        {
            std::vector<float> positions, normals;
            std::vector<unsigned char> colors;
            Vector3 low = { 1e9f, 1e9f, 1e9f }, high = { -1e9f, -1e9f, -1e9f };
            auto vertex = [&](int ix, int iz) {
                const Vector3 p = position(ix, iz), nrm = normal(ix, iz), c = color(ix, iz, nrm);
                positions.insert(positions.end(), { p.x, p.y, p.z });
                normals.insert(normals.end(), { nrm.x, nrm.y, nrm.z });
                colors.insert(colors.end(), { (unsigned char)(c.x * 255.0f), (unsigned char)(c.y * 255.0f), (unsigned char)(c.z * 255.0f), 255 });
                low = Vector3Min(low, p);
                high = Vector3Max(high, p);
            };
            for (int iz = pz; iz < std::min(pz + piece, n - 1); ++iz)
            {
                for (int ix = px; ix < std::min(px + piece, n - 1); ++ix)
                {
                    vertex(ix, iz); vertex(ix, iz + 1); vertex(ix + 1, iz);
                    vertex(ix + 1, iz); vertex(ix, iz + 1); vertex(ix + 1, iz + 1);
                }
            }
            MeshPiece part;
            Mesh& mesh = part.mesh;
            mesh.vertexCount = (int)(positions.size() / 3);
            mesh.triangleCount = mesh.vertexCount / 3;
            mesh.vertices = (float*)MemAlloc((unsigned int)(positions.size() * sizeof(float)));
            mesh.normals = (float*)MemAlloc((unsigned int)(normals.size() * sizeof(float)));
            mesh.colors = (unsigned char*)MemAlloc((unsigned int)colors.size());
            std::memcpy(mesh.vertices, positions.data(), positions.size() * sizeof(float));
            std::memcpy(mesh.normals, normals.data(), normals.size() * sizeof(float));
            std::memcpy(mesh.colors, colors.data(), colors.size());
            UploadMesh(&mesh, false);
            part.center = Vector3Scale(Vector3Add(low, high), 0.5f);
            part.radius = Vector3Distance(low, high) * 0.5f;
            farLand.push_back(part);
        }
    }

    // Гол мөрөн, авто зам: алсаас харагдахаар өргөн зурвас (тоглох талбайд ойрын газрын гол, зам бий тул алгасна)
    std::vector<float> positions, normals;
    std::vector<unsigned char> colors;
    Color tone = { 70, 105, 130, 255 };
    auto add = [&](Vector2 p, float y) {
        positions.insert(positions.end(), { p.x, y, p.y });
        normals.insert(normals.end(), { 0.0f, 1.0f, 0.0f });
        colors.insert(colors.end(), { tone.r, tone.g, tone.b, 255 });
    };
    for (const CityLine& line : city.farLines)
    {
        const bool road = line.kind == CityLineKind::Road || line.kind == CityLineKind::DirtRoad;
        tone = line.kind == CityLineKind::Road ? Color{ 64, 64, 68, 255 } : road ? Color{ 140, 118, 88, 255 } : Color{ 70, 105, 130, 255 };
        const float width = std::max(line.halfWidth, road ? 4.0f : 6.0f);
        const float lift = road ? 0.1f : -0.2f; // зам гол дээгүүр (гүүр)
        for (size_t i = 0; i + 1 < line.points.size(); ++i)
        {
            const Vector2 a = line.points[i], b = line.points[i + 1];
            if (std::fabs(a.x) < inner && std::fabs(a.y) < inner && std::fabs(b.x) < inner && std::fabs(b.y) < inner) continue;
            const float length = Vector2Distance(a, b);
            if (length < 0.5f) continue;
            const Vector2 side = { -(b.y - a.y) / length * width, (b.x - a.x) / length * width };
            const float ya = grid.Sample(a.x, a.y) + lift, yb = grid.Sample(b.x, b.y) + lift;
            add(Vector2Add(a, side), ya); add(Vector2Subtract(a, side), ya); add(Vector2Add(b, side), yb);
            add(Vector2Add(b, side), yb); add(Vector2Subtract(a, side), ya); add(Vector2Subtract(b, side), yb);
        }
    }
    if (!positions.empty())
    {
        MeshPiece part;
        Mesh& mesh = part.mesh;
        mesh.vertexCount = (int)(positions.size() / 3);
        mesh.triangleCount = mesh.vertexCount / 3;
        mesh.vertices = (float*)MemAlloc((unsigned int)(positions.size() * sizeof(float)));
        mesh.normals = (float*)MemAlloc((unsigned int)(normals.size() * sizeof(float)));
        mesh.colors = (unsigned char*)MemAlloc((unsigned int)colors.size());
        std::memcpy(mesh.vertices, positions.data(), positions.size() * sizeof(float));
        std::memcpy(mesh.normals, normals.data(), normals.size() * sizeof(float));
        std::memcpy(mesh.colors, colors.data(), colors.size());
        UploadMesh(&mesh, false);
        part.center = { 0.0f, 0.0f, 0.0f };
        part.radius = half * 1.5f;
        farLand.push_back(part);
    }
    TraceLog(LOG_INFO, "WORLD: far land %d x %d (%.0f m), %d pieces", n, n, cell, (int)farLand.size());
}

void World::BuildSettlements(int variant)
{
    if (variant == settlementVariant)
    {
        return;
    }
    settlementVariant = variant;

    // Аймгийн жинхэнэ төв (assets/city-д өгөгдөл байвал), эс бөгөөс зохиомол хээр тал
    const int city = CityFor(variant).Loaded() ? variant : -1;
    if (city != cityProvince)
    {
        SwitchCity(city);
    }

    // Өмнөх үеийн суурингуудыг арилгана: геометр, мөргөлдөөн, хаалга, дотор тал.
    UnloadPieces(settlementStructures);
    UnloadPieces(settlementShadows);
    UnloadPieces(settlementGlass);
    UnloadPieces(settlementFences);
    ShowAllScenery(); // (тогтмол хэсгийн нуусан модыг сэргээнэ; суурингийнх устна)
    UnloadPieces(settlementSceneryPieces);
    scenery.resize(staticSceneryCount);
    sceneryCpu.resize(staticSceneryCpu);
    obstacles.resize(staticObstacleCount);
    interiors.resize(staticInteriorCount);
    doors.resize(staticDoorCount);
    passages.resize(staticPassageCount);
    ropes.clear();
    walls.resize(staticWallCount);
    panels.resize(staticPanelCount);
    brokenPanels.erase(std::remove_if(brokenPanels.begin(), brokenPanels.end(), [&](int i) { return (size_t)i >= staticPanelCount; }),
                       brokenPanels.end());

    // Буянт-Ухаа нисэх буудлын хучилт зөвхөн хуучин (зохиомол) Улаанбаатарын үед (өвс ургахгүй, засмал замын хурд)
    layout.SetAirfieldActive(variant == kUlaanbaatarProvince && cityProvince < 0);

    MeshBuilder solidBuilder, shadowBuilder, glassBuilder, sceneryBuilder;
    std::vector<SceneryPlacement> placements;
    BuildSettlementStructures(variant, layout, terrain, obstacles, solidBuilder, shadowBuilder, glassBuilder, interiors,
                              doors, passages, walls, panels, sceneryBuilder, placements);
    settlementSceneryPieces = UploadScenery(sceneryBuilder, placements, true);
    settlementFences = ExtractFences(solidBuilder, staticPanelCount, true);

    settlementStructures = solidBuilder.UploadPartitioned(kPieceSize);
    settlementShadows = shadowBuilder.UploadPartitioned(kPieceSize);
    settlementGlass = glassBuilder.UploadPartitioned(kPieceSize);

    BuildObstacleGrid();
    FitWindowPassages();
    terrain.InvalidateDetails(); // өвс, чулуу шинэ барилгуудын дотор үлдэхгүй

    TraceLog(LOG_INFO, "WORLD: settlements variant %d: %d triangles, %d colliders total, %d enterable buildings", variant,
             solidBuilder.TriangleCount(), (int)obstacles.size(), (int)interiors.size());
    TraceLog(LOG_INFO, "WORLD: scenery %d trees and barrels (%d static), %d low-poly triangles", (int)scenery.size(),
             (int)staticSceneryCount, (int)(sceneryCpu.size() / 9));
}

void World::Stream(Vector3 viewPosition, bool everything)
{
    terrain.Stream(viewPosition, everything ? 1000000 : kStreamBudgetPerFrame);
}

// ---------------------------------------------------------------- Саадын тор

int World::GridCell(float value) const
{
    return std::clamp((int)((value + kWorldHalfExtent) / kGridCell), 0, gridSide - 1);
}

void World::BuildObstacleGrid()
{
    gridSide = (int)std::ceil(kWorldHalfExtent * 2.0f / kGridCell);
    obstacleGrid.assign((size_t)gridSide * gridSide, {});
    wallGrid.assign((size_t)gridSide * gridSide, {});
    panelGrid.assign((size_t)gridSide * gridSide, {});
    for (int i = 0; i < (int)panels.size(); ++i)
    {
        const Vector2 mid = Vector2Lerp(panels[(size_t)i].a, panels[(size_t)i].b, 0.5f);
        panelGrid[(size_t)GridCell(mid.y) * gridSide + GridCell(mid.x)].push_back(i);
    }
    sceneryGrid.assign((size_t)gridSide * gridSide, {});
    for (int i = 0; i < (int)scenery.size(); ++i)
    {
        sceneryGrid[(size_t)GridCell(scenery[(size_t)i].base.z) * gridSide + GridCell(scenery[(size_t)i].base.x)].push_back(i);
    }
    interiorGrid.assign((size_t)gridSide * gridSide, {});
    doorGrid.assign((size_t)gridSide * gridSide, {});
    for (int i = 0; i < (int)interiors.size(); ++i)
    {
        const Interior& r = interiors[(size_t)i];
        const float reach = r.round ? r.halfX : std::sqrt(r.halfX * r.halfX + r.halfZ * r.halfZ);
        for (int z = GridCell(r.center.y - reach); z <= GridCell(r.center.y + reach); ++z)
        {
            for (int x = GridCell(r.center.x - reach); x <= GridCell(r.center.x + reach); ++x) interiorGrid[(size_t)z * gridSide + x].push_back(i);
        }
    }
    for (int i = 0; i < (int)doors.size(); ++i)
    {
        const Vector3 c = doors[(size_t)i].Center();
        const float reach = doors[(size_t)i].width + 0.5f;
        for (int z = GridCell(c.z - reach); z <= GridCell(c.z + reach); ++z)
        {
            for (int x = GridCell(c.x - reach); x <= GridCell(c.x + reach); ++x) doorGrid[(size_t)z * gridSide + x].push_back(i);
        }
    }
    pitchedRoofs = StaticPitchedRoofs();
    pitchedRoofs.insert(pitchedRoofs.end(), SettlementPitchedRoofs().begin(), SettlementPitchedRoofs().end());
    roofGrid.assign((size_t)gridSide * gridSide, {});
    for (int i = 0; i < (int)pitchedRoofs.size(); ++i)
    {
        const PitchedRoof& r = pitchedRoofs[(size_t)i];
        const float reach = std::sqrt(r.reachLong * r.reachLong + r.reachShort * r.reachShort);
        for (int z = GridCell(r.center.y - reach); z <= GridCell(r.center.y + reach); ++z)
        {
            for (int x = GridCell(r.center.x - reach); x <= GridCell(r.center.x + reach); ++x) roofGrid[(size_t)z * gridSide + x].push_back(i);
        }
    }
    for (int i = 0; i < (int)walls.size(); ++i)
    {
        const WallSegment& w = walls[(size_t)i];
        const int x0 = GridCell(std::min(w.a.x, w.b.x)), x1 = GridCell(std::max(w.a.x, w.b.x));
        const int z0 = GridCell(std::min(w.a.y, w.b.y)), z1 = GridCell(std::max(w.a.y, w.b.y));
        for (int z = z0; z <= z1; ++z)
        {
            for (int x = x0; x <= x1; ++x) wallGrid[(size_t)z * gridSide + x].push_back(i);
        }
    }

    for (int i = 0; i < (int)obstacles.size(); ++i)
    {
        const Obstacle& o = obstacles[i];
        const int x0 = GridCell(o.center.x - o.size.x * 0.5f), x1 = GridCell(o.center.x + o.size.x * 0.5f);
        const int z0 = GridCell(o.center.z - o.size.z * 0.5f), z1 = GridCell(o.center.z + o.size.z * 0.5f);
        for (int z = z0; z <= z1; ++z)
        {
            for (int x = x0; x <= x1; ++x)
            {
                obstacleGrid[(size_t)z * gridSide + x].push_back(i);
            }
        }
    }
}

void World::ObstaclesNear(float minX, float minZ, float maxX, float maxZ, std::vector<int>& out) const
{
    const int x0 = GridCell(minX), x1 = GridCell(maxX);
    const int z0 = GridCell(minZ), z1 = GridCell(maxZ);
    for (int z = z0; z <= z1; ++z)
    {
        for (int x = x0; x <= x1; ++x)
        {
            const std::vector<int>& cell = obstacleGrid[(size_t)z * gridSide + x];
            out.insert(out.end(), cell.begin(), cell.end());
        }
    }
}

// ---------------------------------------------------------------- Хил

void World::SetBoundary(const Shape& shape, bool markers)
{
    boundary = shape;
    boundaryFill = Triangulate(boundary.outline);
    boundaryHoleFill.clear();
    for (const Polygon& hole : boundary.holes)
    {
        const std::vector<Vector2> triangles = Triangulate(hole);
        boundaryHoleFill.insert(boundaryHoleFill.end(), triangles.begin(), triangles.end());
    }

    UnloadPieces(boundaryPosts);
    UnloadPieces(boundaryBand);
    if (!markers)
    {
        return;
    }

    // --- Хилийн тэмдэг: улаан цагаан шонгууд, хооронд нь хагас тунгалаг зурвас ---
    const float spacing = 10.0f;
    const Color white = { 226, 222, 210, 255 };
    const Color red = { 176, 48, 40, 255 };
    const Color band = { 200, 60, 46, 70 };
    MeshBuilder posts, bands;

    auto addRing = [&](const Polygon& ring) {
        for (size_t i = 0; i < ring.size(); ++i)
        {
            const Vector2 a = ring[i];
            const Vector2 b = ring[(i + 1) % ring.size()];
            const float length = Vector2Distance(a, b);
            const int segments = std::max(1, (int)std::ceil(length / spacing));

            for (int s = 0; s < segments; ++s)
            {
                const Vector2 p0 = Vector2Lerp(a, b, (float)s / (float)segments);
                const Vector2 p1 = Vector2Lerp(a, b, (float)(s + 1) / (float)segments);
                const float h0 = terrain.HeightAt(p0.x, p0.y);
                const float h1 = terrain.HeightAt(p1.x, p1.y);

                posts.Box({ p0.x, h0 + 0.9f, p0.y }, { 0.2f, 1.8f, 0.2f }, white);
                posts.Box({ p0.x, h0 + 1.45f, p0.y }, { 0.22f, 0.36f, 0.22f }, red);
                posts.Box({ p0.x, h0 + 0.6f, p0.y }, { 0.22f, 0.36f, 0.22f }, red);

                bands.Quad({ p0.x, h0 + 0.4f, p0.y }, { p1.x, h1 + 0.4f, p1.y }, { p1.x, h1 + 1.6f, p1.y },
                           { p0.x, h0 + 1.6f, p0.y }, band);
            }
        }
    };

    addRing(boundary.outline);
    for (const Polygon& hole : boundary.holes)
    {
        addRing(hole);
    }

    boundaryPosts = posts.UploadPartitioned(kPieceSize);
    boundaryBand = bands.UploadPartitioned(kPieceSize);
}

// ---------------------------------------------------------------- Хаалга, дотор тал

namespace
{
    const float kDoorPoundTime = 1.0f;     // зомби хаалгыг эвдэхэд шаардагдах хугацаа
    const float kDoorDrawDistance = 90.0f;

    // Хайрцаг саадаас босоо цилиндрийг хэвтээ чиглэлд түлхэж гаргана.
    void PushOutOfBox(const Obstacle& o, Vector3& feetPos, float radius)
    {
        const float halfX = o.size.x * 0.5f;
        const float halfZ = o.size.z * 0.5f;
        const float localX = feetPos.x - o.center.x;
        const float localZ = feetPos.z - o.center.z;

        const float closestX = Clamp(localX, -halfX, halfX);
        const float closestZ = Clamp(localZ, -halfZ, halfZ);
        const float dx = localX - closestX;
        const float dz = localZ - closestZ;
        const float distSq = dx * dx + dz * dz;

        if (distSq >= radius * radius)
        {
            return;
        }

        if (distSq > 1e-8f)
        {
            // Төв нь гадна: хамгийн ойр цэгээс холдуулж түлхэнэ.
            const float dist = std::sqrt(distSq);
            const float push = (radius - dist) / dist;
            feetPos.x += dx * push;
            feetPos.z += dz * push;
        }
        else
        {
            // Төв нь хайрцаг дотор: хамгийн ойр хана руу гаргана.
            const float penX = halfX - std::fabs(localX);
            const float penZ = halfZ - std::fabs(localZ);
            if (penX < penZ)
            {
                feetPos.x = o.center.x + (localX < 0.0f ? -1.0f : 1.0f) * (halfX + radius);
            }
            else
            {
                feetPos.z = o.center.z + (localZ < 0.0f ? -1.0f : 1.0f) * (halfZ + radius);
            }
        }
    }
}

int World::NearestDoor(Vector3 position, float maxDistance) const
{
    int best = -1;
    float bestDistance = maxDistance;
    for (int i = 0; i < (int)doors.size(); ++i)
    {
        const Vector3 center = doors[i].Center();
        if (std::fabs(center.x - position.x) > maxDistance || std::fabs(center.z - position.z) > maxDistance) continue;

        const float dx = center.x - position.x, dz = center.z - position.z;
        const float distance = std::sqrt(dx * dx + dz * dz);
        if (distance < bestDistance && std::fabs(doors[i].hinge.y - position.y) < 2.0f)
        {
            best = i;
            bestDistance = distance;
        }
    }
    return best;
}

std::vector<MeshPiece> World::UploadScenery(const MeshBuilder& builder, const std::vector<SceneryPlacement>& placed, bool settlement)
{
    std::vector<std::pair<size_t, size_t>> groups;
    for (const SceneryPlacement& p : placed) groups.push_back({ p.first, p.count });
    std::vector<int> piece, start;
    std::vector<MeshPiece> pieces = builder.UploadGroups(kPieceSize, groups, piece, start);
    for (size_t i = 0; i < placed.size(); ++i)
    {
        SceneryItem item;
        item.base = placed[i].base;
        item.height = placed[i].height;
        item.yaw = placed[i].yaw;
        item.kind = placed[i].kind;
        item.settlement = settlement;
        item.piece = piece[i];
        item.vertexStart = start[i];
        item.count = placed[i].count;
        item.cpu = sceneryCpu.size();
        builder.CopyPositions(placed[i].first, placed[i].count, sceneryCpu);
        scenery.push_back(item);
    }
    return pieces;
}

void World::SetSceneryHidden(int index, bool hidden)
{
    SceneryItem& item = scenery[(size_t)index];
    if (item.hidden == hidden || item.count == 0) return;
    const std::vector<MeshPiece>& list = item.settlement ? settlementSceneryPieces : sceneryPieces;
    if (item.piece < 0 || item.piece >= (int)list.size() || list[(size_t)item.piece].mesh.vertexCount <= 0) return;
    item.hidden = hidden;
    // Нуух: гурвалжнуудыг нэг цэг болгоно; харуулах: хадгалсан байрлалыг буцаана
    static std::vector<float> collapsed;
    collapsed.assign(item.count * 9, 0.0f);
    const float* data = hidden ? collapsed.data() : &sceneryCpu[item.cpu];
    UpdateMeshBuffer(list[(size_t)item.piece].mesh, 0, data, (int)(item.count * 9 * sizeof(float)),
                     item.vertexStart * 3 * (int)sizeof(float));
}

void World::ShowAllScenery()
{
    for (int index : sceneryNear) SetSceneryHidden(index, false);
    sceneryNear.clear();
}

void World::UpdateScenery(Vector3 viewPosition)
{
    if (sceneryGrid.empty()) return;
    // Шинэ ойрын жагсаалт: камерын ойрын нүднүүдээс, загвар нь ачаалагдсан төрлүүд
    static std::vector<int> wanted;
    wanted.clear();
    const int c0x = GridCell(viewPosition.x - kSceneryNear), c1x = GridCell(viewPosition.x + kSceneryNear);
    const int c0z = GridCell(viewPosition.z - kSceneryNear), c1z = GridCell(viewPosition.z + kSceneryNear);
    for (int cz = c0z; cz <= c1z; ++cz)
    {
        for (int cx = c0x; cx <= c1x; ++cx)
        {
            for (int index : sceneryGrid[(size_t)cz * gridSide + cx])
            {
                const SceneryItem& item = scenery[(size_t)index];
                if (!HasSceneryModel((SceneryModelId)item.kind)) continue;
                const float dx = item.base.x - viewPosition.x, dz = item.base.z - viewPosition.z;
                if (dx * dx + dz * dz < kSceneryNear * kSceneryNear) wanted.push_back(index);
            }
        }
    }
    // Ойроос гарсан: хөнгөн хувилбарыг буцаана; шинээр орсон: нууна
    for (int index : sceneryNear)
    {
        if (std::find(wanted.begin(), wanted.end(), index) == wanted.end()) SetSceneryHidden(index, false);
    }
    for (int index : wanted) SetSceneryHidden(index, true);
    sceneryNear = wanted;
}

void World::DrawNearScenery(const ViewCone* view, const Material* depth) const
{
    for (int index : sceneryNear)
    {
        const SceneryItem& item = scenery[(size_t)index];
        if (view && !view->Sees({ item.base.x, item.base.y + item.height * 0.5f, item.base.z }, item.height * 0.6f, kSceneryNear + 10.0f))
        {
            continue;
        }
        rlPushMatrix();
        rlTranslatef(item.base.x, item.base.y, item.base.z);
        rlRotatef(item.yaw * RAD2DEG, 0.0f, 1.0f, 0.0f);
        rlScalef(item.height, item.height, item.height);
        DrawSceneryModel((SceneryModelId)item.kind, depth);
        rlPopMatrix();
    }
}

std::vector<MeshPiece> World::ExtractFences(MeshBuilder& solid, size_t from, bool settlement)
{
    std::vector<std::pair<size_t, size_t>> ranges;
    for (size_t i = from; i < panels.size(); ++i) ranges.push_back({ panels[i].first, panels[i].count });
    MeshBuilder fence = solid.Extract(ranges);
    std::vector<std::pair<size_t, size_t>> groups;
    size_t next = 0;
    for (size_t i = from; i < panels.size(); ++i)
    {
        groups.push_back({ next, panels[i].count });
        next += panels[i].count;
    }
    std::vector<int> piece, start;
    std::vector<MeshPiece> pieces = fence.UploadGroups(kPieceSize, groups, piece, start);
    for (size_t i = from; i < panels.size(); ++i)
    {
        panels[i].settlement = settlement;
        panels[i].piece = piece[i - from];
        panels[i].vertexStart = start[i - from];
    }
    return pieces;
}

void World::BreakPanel(int index, Vector2 direction)
{
    FencePanel& panel = panels[(size_t)index];
    panel.broken = true;
    const Vector2 along = Vector2Normalize(Vector2Subtract(panel.b, panel.a));
    panel.fall = { -along.y, along.x };
    if (Vector2DotProduct(panel.fall, direction) < 0.0f) panel.fall = Vector2Negate(panel.fall);
    brokenPanels.push_back(index);
    // Мөргөлдөөнгүй болгоно (торын нүднүүдэд үлдэнэ, өндөр нь хаахгүй)
    if (panel.wall >= 0 && panel.wall < (int)walls.size()) walls[(size_t)panel.wall].top = walls[(size_t)panel.wall].bottom = -1.0e6f;
    if (panel.obstacle >= 0 && panel.obstacle < (int)obstacles.size())
    {
        obstacles[(size_t)panel.obstacle].center.y = -1.0e6f;
        obstacles[(size_t)panel.obstacle].size = { 0.0f, 0.0f, 0.0f };
    }
    // Геометрийг нуух: гурвалжнуудыг нэг цэг болгоно (зурагдахгүй)
    const std::vector<MeshPiece>& list = panel.settlement ? settlementFences : fences;
    if (panel.piece >= 0 && panel.piece < (int)list.size() && list[(size_t)panel.piece].mesh.vertexCount > 0)
    {
        const std::vector<float> collapsed(panel.count * 9, 0.0f);
        UpdateMeshBuffer(list[(size_t)panel.piece].mesh, 0, collapsed.data(), (int)(collapsed.size() * sizeof(float)),
                         panel.vertexStart * 3 * (int)sizeof(float));
    }
}

int World::BreakFences(Vector3 position, float radius, Vector2 direction, float speed)
{
    if (panelGrid.empty()) return 0;
    int broken = 0;
    const Vector2 at = { position.x, position.z };
    for (int cz = GridCell(position.z - kGridCell); cz <= GridCell(position.z + kGridCell); ++cz)
    {
        for (int cx = GridCell(position.x - kGridCell); cx <= GridCell(position.x + kGridCell); ++cx)
        {
            for (int index : panelGrid[(size_t)cz * gridSide + cx])
            {
                const FencePanel& panel = panels[(size_t)index];
                if (panel.broken || speed < panel.breakSpeed) continue;
                const Vector2 ab = Vector2Subtract(panel.b, panel.a);
                const float t = Clamp(Vector2DotProduct(Vector2Subtract(at, panel.a), ab) / std::max(1e-4f, Vector2DotProduct(ab, ab)), 0.0f, 1.0f);
                const Vector2 closest = Vector2Add(panel.a, Vector2Scale(ab, t));
                const Vector2 to = Vector2Subtract(closest, at);
                if (Vector2Length(to) > radius || Vector2DotProduct(to, direction) <= 0.0f) continue;
                if (std::fabs(GroundHeight(closest.x, closest.y) - position.y) > 2.0f) continue;
                BreakPanel(index, direction);
                ++broken;
            }
        }
    }
    return broken;
}

void World::SetDoorOpen(int index, bool open)
{
    doors[index].open = open;
    doors[index].pound = 0.0f;
}

void World::CloseAllDoors()
{
    for (Door& door : doors)
    {
        door.open = false;
        door.swing = 0.0f;
        door.pound = 0.0f;
    }
}

void World::UpdateDoors(float dt)
{
    for (Door& door : doors)
    {
        const float target = door.open ? 1.0f : 0.0f;
        if (door.swing != target)
        {
            const float step = 3.5f * dt;
            door.swing = door.swing < target ? std::min(target, door.swing + step) : std::max(target, door.swing - step);
        }
    }
}

void World::PoundDoors(Vector3 position, float dt)
{
    const int index = NearestDoor(position, 1.6f);
    if (index < 0 || doors[index].open)
    {
        return;
    }

    doors[index].pound += dt;
    if (doors[index].pound >= kDoorPoundTime)
    {
        SetDoorOpen(index, true); // хаалга эвдэрч онгойно
    }
}

bool World::SameRoof(Vector3 a, Vector3 b) const
{
    // a-аас b хүртэл дээвэр дагуу алхана: гадаргуу огцом өөрчлөгдвөл (ирмэг, өндөр хэсэг) өөр дээвэр
    const float length = Vector2Distance({ a.x, a.z }, { b.x, b.z });
    const int steps = std::max(1, (int)(length / 0.5f));
    float previous = a.y;
    for (int k = 1; k <= steps; ++k)
    {
        const float t = (float)k / (float)steps;
        const float height = SurfaceHeightAt(Lerp(a.x, b.x, t), Lerp(a.z, b.z, t), previous + 0.6f);
        if (std::fabs(height - previous) > 1.5f) return false;
        previous = height;
    }
    return std::fabs(previous - b.y) < 1.5f;
}

bool World::AddRope(Vector3 onRoof, float radius)
{
    const float roof = SurfaceHeightAt(onRoof.x, onRoof.z, onRoof.y + 0.6f);
    if (roof < GroundHeight(onRoof.x, onRoof.z) + 3.0f) return false; // дээвэр биш
    const Vector3 here = { onRoof.x, roof, onRoof.z };
    // Энэ дээвэр дээр (дагуу нь алхаж хүрэх) олс, шат аль хэдийн байвал дахин тавихгүй
    for (const Passage& passage : passages)
    {
        if (passage.kind != PassageKind::LadderDown) continue; // (from нь дээд цэг)
        if (Vector2Distance({ passage.from.x, passage.from.z }, { here.x, here.z }) < radius && SameRoof(here, passage.from)) return false;
    }
    // 32 зүгт дээвэр дагуу гадагш алхаж, гадаргуу 2 м-ээс илүү огцом доошилсон цэг: ирмэг.
    // (Налуу дээвэр аажим доошилдог тул өмнөх алхамтай харьцуулна; илүү өндөр хэсэгт тулвал тэр зүг хаалттай.)
    struct Edge
    {
        float distance, top;
        Vector2 dir;
    };
    std::vector<Edge> edges;
    for (int k = 0; k < 32; ++k)
    {
        const float a = (float)k * (2.0f * PI / 32.0f);
        const Vector2 dir = { std::sin(a), std::cos(a) };
        float previous = roof;
        for (float r = 0.25f; r < 90.0f; r += 0.25f)
        {
            const float height = SurfaceHeightAt(onRoof.x + dir.x * r, onRoof.z + dir.y * r, previous + 0.6f);
            if (height < previous - 2.0f)
            {
                edges.push_back({ r, previous, dir });
                break;
            }
            if (height > previous + 1.5f) break;
            previous = height;
        }
    }
    std::sort(edges.begin(), edges.end(), [](const Edge& a, const Edge& b) { return a.distance < b.distance; });
    // Ойрын ирмэгээс эхлэн: доор нь газар дээр (хананы дэргэдэх намхан тавцан, шат, хашлага дээр биш: AI цэрэг гишгэж
    // гарч чаддаггүй), эргэн тойрондоо чөлөөтэй (хашаа, хананы завсарт биш: цэргүүд хүрч чадна) цэг.
    const GameConfig config;
    const float clearance = 1.1f;
    float best = -1.0f, bestTop = roof, gap = 0.45f;
    Vector2 bestDir = { 1.0f, 0.0f }, edge = { 0.0f, 0.0f };
    for (const Edge& candidate : edges)
    {
        const Vector2 at = { onRoof.x + candidate.dir.x * candidate.distance, onRoof.z + candidate.dir.y * candidate.distance };
        for (float d = 0.45f; d <= 3.0f && best < 0.0f; d += 0.25f)
        {
            const Vector2 p = Vector2Add(at, Vector2Scale(candidate.dir, d));
            Vector3 free = { p.x, SurfaceHeightAt(p.x, p.y, candidate.top - 2.0f), p.y };
            if (free.y > GroundHeight(p.x, p.y) + 0.15f || free.y > candidate.top - 3.0f) continue;
            ResolveHorizontal(free, clearance, config.playerHeight, 0.0f);
            if (Vector2Distance({ free.x, free.z }, p) > 0.05f) continue;
            best = candidate.distance;
            bestTop = candidate.top;
            bestDir = candidate.dir;
            edge = at;
            gap = d;
        }
        if (best >= 0.0f) break;
    }
    if (best < 0.0f)
    {
        // Чөлөөтэй газар олдсонгүй: хамгийн ойрын ирмэгийн яг доор (тоглогч ч гэсэн буцаж авирч чадна)
        if (edges.empty()) return false;
        best = edges[0].distance;
        bestTop = edges[0].top;
        bestDir = edges[0].dir;
        edge = { onRoof.x + bestDir.x * best, onRoof.z + bestDir.y * best };
        gap = 0.45f;
    }
    const Vector2 out = Vector2Add(edge, Vector2Scale(bestDir, gap));
    const float ground = SurfaceHeightAt(out.x, out.y, bestTop - 2.0f);
    if (ground > bestTop - 3.0f) return false;
    // Шаттай адил: доод цэгээс олс хүртэл gap, дээд цэг дээвэр дээр ирмэгээс 0.9 м дотор; хана руу (дотогш) харна
    const Vector3 bottom = { out.x, ground, out.y };
    const Vector2 inside = { edge.x - bestDir.x * 0.9f, edge.y - bestDir.y * 0.9f };
    const Vector3 top = { inside.x, SurfaceHeightAt(inside.x, inside.y, bestTop + 0.6f) + 0.05f, inside.y };
    const float facing = std::atan2(-bestDir.x, -bestDir.y);
    Passage up = { bottom, top, PassageKind::LadderUp, facing, gap };
    up.rope = true;
    Passage down = { top, bottom, PassageKind::LadderDown, facing, gap };
    down.rope = true;
    passages.push_back(up);
    passages.push_back(down);
    ropes.push_back({ { edge.x + bestDir.x * 0.05f, bestTop + 0.25f, edge.y + bestDir.y * 0.05f }, { edge.x + bestDir.x * 0.05f, ground, edge.y + bestDir.y * 0.05f } });
    TraceLog(LOG_INFO, "WORLD: rope from the roof at %.1f m down to %.1f m (%.0f, %.0f)", bestTop, ground, edge.x, edge.y);
    return true;
}

void World::DrawRopes() const
{
    for (const Rope& rope : ropes)
    {
        // Ирмэг дээрх дэгээ, доош унжсан олс (бага зэрэг тахийсан)
        DrawCubeV(rope.top, { 0.3f, 0.12f, 0.3f }, { 60, 60, 64, 255 });
        const int segments = 10;
        for (int i = 0; i < segments; ++i)
        {
            const float t0 = (float)i / segments, t1 = (float)(i + 1) / segments;
            const float sway0 = 0.12f * std::sin(t0 * PI), sway1 = 0.12f * std::sin(t1 * PI);
            const Vector3 a = { rope.top.x + sway0, Lerp(rope.top.y, rope.bottom.y, t0), rope.top.z };
            const Vector3 b = { rope.top.x + sway1, Lerp(rope.top.y, rope.bottom.y, t1), rope.top.z };
            DrawCylinderEx(a, b, 0.035f, 0.035f, 5, { 150, 120, 70, 255 });
        }
    }
}

int World::NearestPassage(Vector3 position, float maxDistance) const
{
    int best = -1;
    float bestDistance = maxDistance;
    for (int i = 0; i < (int)passages.size(); ++i)
    {
        const Vector3 from = passages[(size_t)i].from;
        if (std::fabs(from.y - position.y) > 0.9f) // өөр давхрын гарц
        {
            continue;
        }
        const float dx = from.x - position.x, dz = from.z - position.z;
        const float distance = std::sqrt(dx * dx + dz * dz);
        if (distance < bestDistance)
        {
            best = i;
            bestDistance = distance;
        }
    }
    return best;
}

bool World::InSafeRoom(float x, float z) const
{
    const int room = InteriorAt(x, z);
    return room >= 0 && interiors[room].safe;
}

void World::KeepOutOfSafeRooms(Vector3& position, float radius) const
{
    for (const Interior& room : interiors)
    {
        if (!room.safe || room.round) continue;
        // Ханын зузааныг (0.2 м) оруулж гадна талаас нь хэмжинэ.
        // (+0.3 м: том зомбины гар, мөр ханаар нэвт харагдахгүй)
        const float halfX = room.halfX + 0.5f + radius, halfZ = room.halfZ + 0.5f + radius;
        const float dx = position.x - room.center.x, dz = position.z - room.center.y;
        const float overX = halfX - std::fabs(dx), overZ = halfZ - std::fabs(dz);
        if (overX <= 0.0f || overZ <= 0.0f) continue;
        if (overX < overZ) position.x += dx < 0.0f ? -overX : overX;
        else position.z += dz < 0.0f ? -overZ : overZ;
    }
}

int World::InteriorAt(float x, float z) const
{
    if (interiorGrid.empty()) return -1;
    // Торын нүдний барилгууд л (хотод мянга гаруй барилга)
    for (int i : interiorGrid[(size_t)GridCell(z) * gridSide + GridCell(x)])
    {
        if (interiors[(size_t)i].Contains(x, z)) return i;
    }
    return -1;
}

void World::DrawDoors(Vector3 viewPosition) const
{
    for (const Door& door : doors)
    {
        if (std::fabs(door.hinge.x - viewPosition.x) > kDoorDrawDistance ||
            std::fabs(door.hinge.z - viewPosition.z) > kDoorDrawDistance)
        {
            continue;
        }

        // Хаалттай ба нээлттэй өнцгийн хооронд хамгийн богино замаар эргэнэ.
        const float turn = std::remainder(door.openYaw - door.closedYaw, 2.0f * PI);
        const float yaw = door.closedYaw + turn * door.swing;

        rlPushMatrix();
        rlTranslatef(door.hinge.x, door.hinge.y, door.hinge.z);
        rlRotatef(yaw * RAD2DEG, 0.0f, 1.0f, 0.0f);
        DrawCubeV({ 0.0f, door.height * 0.5f, door.width * 0.5f }, { 0.07f, door.height, door.width }, door.color);
        // Хөндлөн банз ба бариул
        const Color trim = { (unsigned char)(door.color.r * 3 / 5), (unsigned char)(door.color.g * 3 / 5),
                             (unsigned char)(door.color.b * 3 / 5), 255 };
        DrawCubeV({ 0.0f, door.height * 0.25f, door.width * 0.5f }, { 0.09f, 0.08f, door.width * 0.9f }, trim);
        DrawCubeV({ 0.0f, door.height * 0.75f, door.width * 0.5f }, { 0.09f, 0.08f, door.width * 0.9f }, trim);
        DrawCubeV({ 0.0f, door.height * 0.5f, door.width * 0.85f }, { 0.14f, 0.1f, 0.06f }, { 190, 170, 110, 255 });
        rlPopMatrix();
    }

    // Унаагаар нураасан хашааны хэсэг: газар хэвтэнэ
    for (int index : brokenPanels)
    {
        const FencePanel& panel = panels[(size_t)index];
        const Vector2 mid = Vector2Lerp(panel.a, panel.b, 0.5f);
        if (std::fabs(mid.x - viewPosition.x) > kDoorDrawDistance || std::fabs(mid.y - viewPosition.z) > kDoorDrawDistance) continue;
        const Vector2 along = Vector2Normalize(Vector2Subtract(panel.b, panel.a));
        const Vector2 lying = Vector2Add(mid, Vector2Scale(panel.fall, panel.height * 0.5f + 0.1f));
        rlPushMatrix();
        rlTranslatef(lying.x, GroundHeight(lying.x, lying.y) + 0.05f, lying.y);
        rlRotatef(std::atan2(along.x, along.y) * RAD2DEG, 0.0f, 1.0f, 0.0f);
        rlRotatef(4.0f, 1.0f, 0.0f, 0.0f); // бага зэрэг хазгай
        DrawCubeV({ 0.0f, 0.0f, 0.0f }, { panel.height, 0.07f, Vector2Distance(panel.a, panel.b) }, ScaleColor(panel.color, 0.8f));
        rlPopMatrix();
    }
}

// ---------------------------------------------------------------- Асуулгууд

Surface World::SurfaceAt(float x, float z) const
{
    RoadKind kind;
    if (layout.RoadAt(x, z, kind))
    {
        return kind == RoadKind::Asphalt ? Surface::Asphalt : Surface::Dirt;
    }
    return Surface::Steppe;
}

float World::SurfaceHeightAt(float x, float z, float maxY) const
{
    float height = terrain.HeightAt(x, z);
    for (int index : obstacleGrid[(size_t)GridCell(z) * gridSide + GridCell(x)])
    {
        const Obstacle& o = obstacles[index];
        const bool inside = std::fabs(x - o.center.x) <= o.size.x * 0.5f &&
                            std::fabs(z - o.center.z) <= o.size.z * 0.5f;
        if (inside && o.Top() <= maxY && o.Top() > height)
        {
            height = o.Top();
        }
    }
    // Хотын налуу хананы байшингийн дээвэр: зөвхөн өндөрт байгаа (дээвэр дээр, нисдэг тэрэг) асуухад —
    // газар дээрх олон мянган зомби, хүнд энэ тооцоо ажиллахгүй
    if (maxY > height + 2.5f && (!wallGrid.empty() || !roofGrid.empty()))
    {
        const float roof = RoofHeight(x, z);
        if (roof <= maxY && roof > height) height = roof;
    }
    return height;
}

void World::FitWindowPassages()
{
    const GameConfig config;
    auto isFree = [&](Vector3 point) {
        Vector3 free = point;
        ResolveHorizontal(free, config.playerRadius, config.playerHeight, config.stepHeight);
        return Vector2Distance({ free.x, free.z }, { point.x, point.z }) < 0.05f;
    };
    // Ханын төвөөс dir зүгт эхлээд base метрт, саадтай бол алхам алхмаар цааш, бага зэрэг хажуу тийш чөлөөт
    // цэг хайна (цонхны доор ор, ширээ байвал түүний цаана буух / түүний цаанаас эхэлнэ).
    auto findFree = [&](Vector3 center, Vector3 dir, float base, float y, Vector3& out) {
        const Vector3 side = { -dir.z, 0.0f, dir.x };
        for (int k = 0; k < 6; ++k)
        {
            for (float lateral : { 0.0f, 0.45f, -0.45f })
            {
                const float distance = base + 0.4f * (float)k;
                const Vector3 point = { center.x + dir.x * distance + side.x * lateral, y,
                                        center.z + dir.z * distance + side.z * lateral };
                if (isFree(point))
                {
                    out = point;
                    return true;
                }
            }
        }
        return false;
    };

    int moved = 0, removed = 0;
    std::vector<Passage> kept(passages.begin(), passages.begin() + (long)staticPassageCount);
    for (size_t i = staticPassageCount; i < passages.size(); ++i)
    {
        Passage passage = passages[i];
        const Vector3 axis = Vector3Normalize({ passage.to.x - passage.from.x, 0.0f, passage.to.z - passage.from.z });
        const Vector3 center = { passage.from.x + axis.x * passage.wall, 0.0f, passage.from.z + axis.z * passage.wall };
        const Vector3 back = Vector3Negate(axis);
        bool usable = true;
        switch (passage.kind)
        {
        case PassageKind::WindowOut: // дотроос гадагш
            usable = findFree(center, back, 0.7f, passage.from.y, passage.from) &&
                     findFree(center, axis, 1.05f, passage.to.y, passage.to);
            break;
        case PassageKind::WindowIn: // гаднаас дотогш
            usable = findFree(center, back, 1.05f, passage.from.y, passage.from) &&
                     findFree(center, axis, 0.7f, passage.to.y, passage.to);
            break;
        case PassageKind::WindowDrop:
        {
            // Зүүгдэх цэгээс доош унана: газарт буух цэг ойрхон чөлөөтэй байх ёстой (хананаас түлхэгдэх нь хэвийн).
            Vector3 landing = { passage.to.x, terrain.HeightAt(passage.to.x, passage.to.z), passage.to.z };
            Vector3 free = landing;
            ResolveHorizontal(free, config.playerRadius, config.playerHeight, config.stepHeight);
            usable = findFree(center, back, 0.7f, passage.from.y, passage.from) &&
                     Vector2Distance({ free.x, free.z }, { landing.x, landing.z }) < 0.9f;
            break;
        }
        default: break;
        }
        if (!usable)
        {
            ++removed;
            continue;
        }
        const float wall = Vector2Distance({ passage.from.x, passage.from.z }, { center.x, center.z });
        moved += std::fabs(wall - passage.wall) > 0.05f || Vector3Distance(passage.to, passages[i].to) > 0.05f;
        passage.wall = wall;
        kept.push_back(passage);
    }
    passages = std::move(kept);
    TraceLog(LOG_INFO, "WORLD: window passages: %d moved past furniture or fences, %d removed", moved, removed);
}

namespace
{
    // Тойрог (center, radius)-ийг a-b хэрчмээс (зузаан thickness) хэвтээ чиглэлд түлхэж гаргана.
    void PushOutOfSegment(Vector2 a, Vector2 b, float thickness, Vector3& feetPos, float radius)
    {
        const Vector2 ab = Vector2Subtract(b, a);
        const float lengthSq = Vector2LengthSqr(ab);
        const Vector2 p = { feetPos.x, feetPos.z };
        const float t = lengthSq > 1e-8f ? std::clamp(Vector2DotProduct(Vector2Subtract(p, a), ab) / lengthSq, 0.0f, 1.0f) : 0.0f;
        const Vector2 closest = Vector2Add(a, Vector2Scale(ab, t));
        Vector2 away = Vector2Subtract(p, closest);
        const float limit = radius + thickness;
        const float distSq = Vector2LengthSqr(away);
        if (distSq >= limit * limit) return;
        float dist = std::sqrt(distSq);
        if (dist < 1e-4f)
        {
            // Яг шугам дээр: шугамын перпендикуляр зүг рүү
            const float length = std::sqrt(std::max(lengthSq, 1e-8f));
            away = { -ab.y / length, ab.x / length };
            dist = 1.0f;
            feetPos.x = closest.x + away.x * limit;
            feetPos.z = closest.y + away.y * limit;
            return;
        }
        feetPos.x = closest.x + away.x / dist * limit;
        feetPos.z = closest.y + away.y / dist * limit;
    }

    // a-b ба c-d хэрчмүүд огтлолцох бол a-b дагуух хувь (0..1), үгүй бол -1
    float SegmentHit(Vector2 a, Vector2 b, Vector2 c, Vector2 d)
    {
        const Vector2 r = Vector2Subtract(b, a), s = Vector2Subtract(d, c);
        const float denom = r.x * s.y - r.y * s.x;
        if (std::fabs(denom) < 1e-8f) return -1.0f;
        const Vector2 ac = Vector2Subtract(c, a);
        const float t = (ac.x * s.y - ac.y * s.x) / denom;
        const float u = (ac.x * r.y - ac.y * r.x) / denom;
        return t >= 0.0f && t <= 1.0f && u >= 0.0f && u <= 1.0f ? t : -1.0f;
    }
}

void World::ResolveHorizontal(Vector3& feetPos, float radius, float height, float stepHeight, bool person) const
{
    // Зөвхөн ойр орчмын нүднүүдийн саадуудыг шалгана.
    const int x0 = GridCell(feetPos.x - radius), x1 = GridCell(feetPos.x + radius);
    const int z0 = GridCell(feetPos.z - radius), z1 = GridCell(feetPos.z + radius);

    for (int cz = z0; cz <= z1; ++cz)
    {
        for (int cx = x0; cx <= x1; ++cx)
        {
            for (int index : obstacleGrid[(size_t)cz * gridSide + cx])
            {
                const Obstacle& o = obstacles[index];

                // Гишгээд гарч болох намхан, эсвэл толгойноос дээш байгаа саад хэвтээ чиглэлд хаахгүй.
                // Шатны гишгүүр: биеийн өргөн гишгүүрээс их тул дараагийн хоёр гишгүүр хүртэлх өндрийг хаахгүй;
                // харин шатыг хажуугаас нь (газрын түвшнээс өндөр хэсгийг) нэвтлэхгүй.
                const float step = o.walkable ? std::max(stepHeight, 0.62f) : stepHeight;
                if (o.Top() <= feetPos.y + step || o.Bottom() >= feetPos.y + height)
                {
                    continue;
                }
                PushOutOfBox(o, feetPos, radius);
            }
        }
    }

    if (!wallGrid.empty())
    {
        // Хашаа: налуу нимгэн хана (хүн, зомби, унаа бүгд нэвтрэхгүй)
        for (int cz = z0; cz <= z1; ++cz)
        {
            for (int cx = x0; cx <= x1; ++cx)
            {
                for (int index : wallGrid[(size_t)cz * gridSide + cx])
                {
                    const WallSegment& w = walls[(size_t)index];
                    if (w.top <= feetPos.y + stepHeight || w.bottom >= feetPos.y + height) continue;
                    PushOutOfSegment(w.a, w.b, w.halfThickness, feetPos, person ? radius : radius * 0.6f);
                }
            }
        }
    }
    if (person)
    {
        // Зогсож буй (удаан явж буй) унаа
        for (const VehicleBlocker& v : vehicleBlockers)
        {
            if (std::fabs(v.a.x - feetPos.x) > 8.0f || std::fabs(v.a.y - feetPos.z) > 8.0f) continue;
            if (v.top <= feetPos.y + stepHeight || v.bottom >= feetPos.y + height) continue;
            PushOutOfSegment(v.a, v.b, v.radius, feetPos, radius);
        }
    }

    // Хаалттай (эсвэл дөнгөж хаагдаж буй) хаалганууд (торын нүдээр)
    if (!doorGrid.empty())
    {
        for (int index : doorGrid[(size_t)GridCell(feetPos.z) * gridSide + GridCell(feetPos.x)])
        {
            const Door& door = doors[(size_t)index];
            if (door.swing > 0.5f) continue;
            if (std::fabs(door.hinge.x - feetPos.x) > 4.0f || std::fabs(door.hinge.z - feetPos.z) > 4.0f) continue;
            const Obstacle blocker = door.Blocker();
            if (blocker.Top() > feetPos.y + stepHeight && blocker.Bottom() < feetPos.y + height)
            {
                PushOutOfBox(blocker, feetPos, radius);
            }
        }
    }

    // Талбарын хил: аймгийн хилийн хэлбэр (ба дотор нь орж болохгүй нүх)
    const Vector2 constrained = boundary.Constrain({ feetPos.x, feetPos.z });
    feetPos.x = constrained.x;
    feetPos.z = constrained.y;

    // Ертөнцийн гадна хязгаар (аюулгүйн үүднээс)
    const float limit = halfSize - radius;
    feetPos.x = Clamp(feetPos.x, -limit, limit);
    feetPos.z = Clamp(feetPos.z, -limit, limit);
}

float World::WaterDepth(float x, float z) const
{
    // Хотын гол, цөөрмийн гадаргуу ёроолоос LakeDepth - 0.45 м дээр (Structures: CityWaterPoint); хээр талын нуур усгүй
    return layout.IsCity() ? std::max(0.0f, layout.LakeDepth(x, z) - 0.45f) : 0.0f;
}

float World::WadeFactor(Vector3 feet) const
{
    const float depth = WaterDepth(feet.x, feet.z);
    if (depth <= 0.2f || feet.y > GroundHeight(feet.x, feet.z) + depth) return 1.0f; // гүехэн эсвэл гүүр дээр
    return depth > kSwimDepth ? 0.45f : 1.0f - 0.5f * depth / kSwimDepth;
}

float World::WallRayDistance(Ray ray, float maxDistance) const
{
    // Сум, камер хаадаг налуу хана (хотын байшин): туяаны хэвтээ төсөөллийг хэрчимтэй огтлолцуулна
    float distance = maxDistance;
    if (wallGrid.empty()) return distance;
    const Vector2 from = { ray.position.x, ray.position.z };
    const Vector2 to = { ray.position.x + ray.direction.x * maxDistance, ray.position.z + ray.direction.z * maxDistance };
    const int x0 = GridCell(std::min(from.x, to.x)), x1 = GridCell(std::max(from.x, to.x));
    const int z0 = GridCell(std::min(from.y, to.y)), z1 = GridCell(std::max(from.y, to.y));
    for (int cz = z0; cz <= z1; ++cz)
    {
        for (int cx = x0; cx <= x1; ++cx)
        {
            for (int index : wallGrid[(size_t)cz * gridSide + cx])
            {
                const WallSegment& w = walls[(size_t)index];
                if (!w.blocksRays) continue;
                const float t = SegmentHit(from, to, w.a, w.b);
                if (t < 0.0f) continue;
                const float along = t * maxDistance;
                const float y = ray.position.y + ray.direction.y * along;
                if (y >= w.bottom && y <= w.top && along < distance) distance = along;
            }
        }
    }
    return distance;
}

float World::PitchedRoofAt(float x, float z) const
{
    if (roofGrid.empty()) return -1.0e9f;
    float height = -1.0e9f;
    for (int index : roofGrid[(size_t)GridCell(z) * gridSide + GridCell(x)])
    {
        height = std::max(height, pitchedRoofs[(size_t)index].HeightAt(x, z));
    }
    return height;
}

float World::RoofHeight(float x, float z) const
{
    const float pitched = PitchedRoofAt(x, z);
    if (pitched > -1.0e8f) return pitched;
    if (wallGrid.empty()) return -1.0e9f;
    const float ground = terrain.HeightAt(x, z);
    const Vector2 from = { x, z };
    const Vector2 dirs[4] = { { 1.0f, 0.0f }, { -1.0f, 0.0f }, { 0.0f, 1.0f }, { 0.0f, -1.0f } };
    const float reach = 60.0f;
    float roof = 1.0e9f;
    for (const Vector2& dir : dirs)
    {
        const Vector2 to = Vector2Add(from, Vector2Scale(dir, reach));
        const int x0 = GridCell(std::min(from.x, to.x)), x1 = GridCell(std::max(from.x, to.x));
        const int z0 = GridCell(std::min(from.y, to.y)), z1 = GridCell(std::max(from.y, to.y));
        float nearest = 2.0f, top = 0.0f;
        for (int cz = z0; cz <= z1; ++cz)
        {
            for (int cx = x0; cx <= x1; ++cx)
            {
                for (int index : wallGrid[(size_t)cz * gridSide + cx])
                {
                    const WallSegment& w = walls[(size_t)index];
                    if (w.top - ground < 3.0f) continue; // хашаа биш, байшингийн хана
                    const float t = SegmentHit(from, to, w.a, w.b);
                    if (t >= 0.0f && t < nearest)
                    {
                        nearest = t;
                        top = w.top;
                    }
                }
            }
        }
        if (nearest > 1.0f) return -1.0e9f; // энэ зүгт хана алга: байшин дотор биш
        roof = std::min(roof, top);
    }
    return roof;
}

bool World::CrossesWall(Vector2 a, Vector2 b, float feetY) const
{
    if (wallGrid.empty()) return false;
    const int x0 = GridCell(std::min(a.x, b.x)), x1 = GridCell(std::max(a.x, b.x));
    const int z0 = GridCell(std::min(a.y, b.y)), z1 = GridCell(std::max(a.y, b.y));
    for (int cz = z0; cz <= z1; ++cz)
    {
        for (int cx = x0; cx <= x1; ++cx)
        {
            for (int index : wallGrid[(size_t)cz * gridSide + cx])
            {
                const WallSegment& w = walls[(size_t)index];
                if (w.top <= feetY + 0.3f || w.bottom >= feetY + 1.6f) continue;
                if (SegmentHit(a, b, w.a, w.b) >= 0.0f) return true;
            }
        }
    }
    return false;
}

bool World::FindVault(Vector3 feet, Vector2 dir, float reach, float radius, float maxRise, Passage& out) const
{
    if (wallGrid.empty()) return false;
    const Vector2 from = { feet.x, feet.z }, to = Vector2Add(from, Vector2Scale(dir, reach));
    const int x0 = GridCell(std::min(from.x, to.x) - 0.5f), x1 = GridCell(std::max(from.x, to.x) + 0.5f);
    const int z0 = GridCell(std::min(from.y, to.y) - 0.5f), z1 = GridCell(std::max(from.y, to.y) + 0.5f);
    const WallSegment* best = nullptr;
    float bestT = 2.0f;
    for (int cz = z0; cz <= z1; ++cz)
    {
        for (int cx = x0; cx <= x1; ++cx)
        {
            for (int index : wallGrid[(size_t)cz * gridSide + cx])
            {
                const WallSegment& w = walls[(size_t)index];
                const float rise = w.top - feet.y;
                if (!w.climbable || rise > maxRise || rise < 0.3f || w.bottom > feet.y + 1.0f) continue;
                const float t = SegmentHit(from, to, w.a, w.b);
                if (t >= 0.0f && t < bestT)
                {
                    bestT = t;
                    best = &w;
                }
            }
        }
    }
    if (!best) return false;
    const float wallDistance = bestT * reach;
    // Цаад талын чөлөөт газар (хашааны ард өөр хашаа, саад байвал бага зэрэг цааш)
    for (float extra : { 0.45f, 0.85f })
    {
        const Vector2 land = Vector2Add(from, Vector2Scale(dir, wallDistance + best->halfThickness + radius + extra));
        Vector3 landing = { land.x, terrain.HeightAt(land.x, land.y), land.y };
        landing.y = SurfaceHeightAt(land.x, land.y, landing.y + 0.5f);
        Vector3 resolved = landing;
        ResolveHorizontal(resolved, radius, 1.7f, 0.3f);
        if (Vector2Distance({ resolved.x, resolved.z }, land) > 0.2f || std::fabs(landing.y - feet.y) > 1.5f) continue;
        if (CrossesWall({ land.x, land.y }, Vector2Add(from, Vector2Scale(dir, wallDistance + best->halfThickness + 0.02f)), landing.y)) continue;
        out = {};
        out.from = feet;
        out.to = landing;
        out.kind = PassageKind::FenceOver;
        out.facing = std::atan2(dir.x, dir.y);
        out.wall = wallDistance;
        out.top = best->top;
        return true;
    }
    return false;
}

float World::DoorRayDistance(Ray ray, float maxDistance) const
{
    // Хаалттай хаалга сум, камерыг хаана.
    float distance = maxDistance;
    for (const Door& door : doors)
    {
        if (door.swing > 0.5f) continue;
        if (std::fabs(door.hinge.x - ray.position.x) > maxDistance + 2.0f ||
            std::fabs(door.hinge.z - ray.position.z) > maxDistance + 2.0f)
        {
            continue;
        }
        const RayCollision hit = GetRayCollisionBox(ray, door.Blocker().Bounds());
        if (hit.hit && hit.distance < distance)
        {
            distance = hit.distance;
        }
    }
    return distance;
}

float World::ClipDistance(Vector3 from, Vector3 dir, float maxDistance) const
{
    const float margin = 0.25f;
    const Ray ray = { from, dir };
    float distance = maxDistance;

    const Vector3 end = Vector3Add(from, Vector3Scale(dir, maxDistance));
    const int x0 = GridCell(std::min(from.x, end.x)), x1 = GridCell(std::max(from.x, end.x));
    const int z0 = GridCell(std::min(from.z, end.z)), z1 = GridCell(std::max(from.z, end.z));
    for (int cz = z0; cz <= z1; ++cz)
    {
        for (int cx = x0; cx <= x1; ++cx)
        {
            for (int index : obstacleGrid[(size_t)cz * gridSide + cx])
            {
                const Obstacle& o = obstacles[index];
                if (!o.blocksRays) continue;
                const RayCollision hit = GetRayCollisionBox(ray, o.Bounds());
                if (hit.hit && hit.distance - margin < distance)
                {
                    distance = hit.distance - margin;
                }
            }
        }
    }

    distance = std::min(distance, DoorRayDistance(ray, maxDistance) - margin);

    // Толгодын налуу: камер газар доогуур орохоос өмнө зогсооно.
    const float step = 0.5f;
    for (float t = step; t < distance; t += step)
    {
        const Vector3 p = Vector3Add(from, Vector3Scale(dir, t));
        if (p.y < terrain.HeightAt(p.x, p.z) + 0.3f)
        {
            distance = t - step;
            break;
        }
    }
    return std::max(0.5f, std::min(distance, WallRayDistance(ray, maxDistance) - margin));
}

float World::RayDistance(Ray ray, float maxDistance) const
{
    float distance = maxDistance;

    // Туяаны хамрах тэгш өнцөгт доторх нүднүүдийн саадууд
    const Vector3 end = Vector3Add(ray.position, Vector3Scale(ray.direction, maxDistance));
    const int x0 = GridCell(std::min(ray.position.x, end.x)), x1 = GridCell(std::max(ray.position.x, end.x));
    const int z0 = GridCell(std::min(ray.position.z, end.z)), z1 = GridCell(std::max(ray.position.z, end.z));
    for (int cz = z0; cz <= z1; ++cz)
    {
        for (int cx = x0; cx <= x1; ++cx)
        {
            for (int index : obstacleGrid[(size_t)cz * gridSide + cx])
            {
                const Obstacle& o = obstacles[index];
                if (!o.blocksRays) continue;
                const RayCollision hit = GetRayCollisionBox(ray, o.Bounds());
                if (hit.hit && hit.distance < distance)
                {
                    distance = hit.distance;
                }
            }
        }
    }

    distance = std::min(distance, DoorRayDistance(ray, maxDistance));

    // Газрын гадаргуу: туяаг алхам алхмаар шалгаж, огтлолцлыг хоёр хуваах аргаар нарийвчилна.
    const float step = 2.0f;
    float previous = 0.0f;
    for (float t = step; previous < distance; t += step)
    {
        const float current = std::min(t, distance);
        const Vector3 p = Vector3Add(ray.position, Vector3Scale(ray.direction, current));

        // Дээшээ чиглэсэн туяа хамгийн өндөр толгодоос дээш гарсан бол цааш шалгах хэрэггүй.
        if (ray.direction.y >= 0.0f && p.y > terrain.MaxHeight())
        {
            break;
        }

        if (p.y < terrain.HeightAt(p.x, p.z))
        {
            float low = previous, high = current;
            for (int i = 0; i < 6; ++i)
            {
                const float mid = (low + high) * 0.5f;
                const Vector3 m = Vector3Add(ray.position, Vector3Scale(ray.direction, mid));
                if (m.y < terrain.HeightAt(m.x, m.z)) high = mid;
                else low = mid;
            }
            return high;
        }
        previous = current;
    }
    return std::min(distance, WallRayDistance(ray, maxDistance));
}

// ---------------------------------------------------------------- Зураглал

void World::Draw(const Camera3D& camera, const Material& material, const Material& groundMaterial,
                 const Material& detailMaterial, float detailDistance, bool realShadows) const
{
    const ViewCone view = ViewCone::From(camera);

    // Гараар үүсгэсэн mesh-үүдийн гурвалжны эргэлтийн чиглэл жигд биш тул хоёр талаас нь зурна.
    rlDisableBackfaceCulling();

    terrain.Draw(camera, groundMaterial, detailMaterial, detailDistance);
    DrawPieces(structures, material, view, kStructureDistance);
    DrawPieces(settlementStructures, material, view, kStructureDistance);
    DrawPieces(fences, material, view, kStructureDistance);
    DrawPieces(settlementFences, material, view, kStructureDistance);
    DrawPieces(sceneryPieces, material, view, kStructureDistance);
    DrawPieces(settlementSceneryPieces, material, view, kStructureDistance);
    DrawPieces(boundaryPosts, material, view, kBoundaryDistance);
    DrawNearScenery(&view, nullptr);
    DrawRopes();

    // Сүүдэр: газар дээр наалдсан хагас тунгалаг давхарга, гүний буферт бичихгүй.
    // Нарны жинхэнэ сүүдэр (shadow map) асаалттай бол ойр орчимд хэрэггүй: зөвхөн түүний хүрээнээс цааш.
    if (!realShadows)
    {
        rlDisableDepthMask();
        DrawPieces(shadows, material, view, kShadowDistance);
        DrawPieces(settlementShadows, material, view, kShadowDistance);
        rlEnableDepthMask();
    }

    rlEnableBackfaceCulling();
}

void World::DrawShadowCasters(Vector3 center, float radius, const Material& material) const
{
    // Нарны зүгээс: харах конусгүй, center орчмын бүх зүйл
    const ViewCone all = { center, { 0.0f, 0.0f, 1.0f }, PI };
    rlDisableBackfaceCulling();
    terrain.DrawShadowCasters(center, radius, material);
    DrawPieces(structures, material, all, radius);
    DrawPieces(settlementStructures, material, all, radius);
    DrawPieces(fences, material, all, radius);
    DrawPieces(settlementFences, material, all, radius);
    DrawPieces(sceneryPieces, material, all, radius);
    DrawPieces(settlementSceneryPieces, material, all, radius);
    DrawNearScenery(nullptr, &material);
    rlEnableBackfaceCulling();
}

void World::DrawTransparent(const Camera3D& camera, const Material& material) const
{
    const ViewCone view = ViewCone::From(camera);

    rlDisableBackfaceCulling();
    rlDisableDepthMask();
    DrawPieces(glass, material, view, kGlassDistance);
    DrawPieces(settlementGlass, material, view, kGlassDistance);
    DrawPieces(boundaryBand, material, view, kBoundaryDistance);
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
}

void World::DrawColliders(Vector3 viewPosition) const
{
    std::vector<int> nearby;
    ObstaclesNear(viewPosition.x - 60.0f, viewPosition.z - 60.0f, viewPosition.x + 60.0f, viewPosition.z + 60.0f, nearby);
    for (int index : nearby)
    {
        const Obstacle& o = obstacles[index];
        DrawBoundingBox(o.Bounds(), o.blocksRays ? LIME : SKYBLUE);
    }
}

