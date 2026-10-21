#include "World.h"

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
    UnloadPieces(boundaryPosts);
    UnloadPieces(boundaryBand);
}

void World::BuildStatic()
{
    UnloadPieces(structures);
    UnloadPieces(shadows);
    UnloadPieces(glass);
    obstacles.clear();
    interiors.clear();
    doors.clear();
    passages.clear();

    MeshBuilder solidBuilder, shadowBuilder, glassBuilder;
    BuildStaticStructures(layout, terrain, obstacles, solidBuilder, shadowBuilder, glassBuilder, interiors, doors,
                          passages);
    staticPassageCount = passages.size();

    structures = solidBuilder.UploadPartitioned(kPieceSize);
    shadows = shadowBuilder.UploadPartitioned(kPieceSize);
    glass = glassBuilder.UploadPartitioned(kPieceSize);
    staticObstacleCount = obstacles.size();

    TraceLog(LOG_INFO, "WORLD: %d static structure triangles in %d pieces, %d colliders", solidBuilder.TriangleCount(),
             (int)structures.size(), (int)obstacles.size());
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

    // Гол мөрөн: алсаас харагдахаар өргөн зурвас (тоглох талбайд ойрын газрын гол бий тул алгасна)
    std::vector<float> positions, normals;
    std::vector<unsigned char> colors;
    auto add = [&](Vector2 p, float y) {
        positions.insert(positions.end(), { p.x, y, p.y });
        normals.insert(normals.end(), { 0.0f, 1.0f, 0.0f });
        colors.insert(colors.end(), { 70, 105, 130, 255 });
    };
    for (const CityLine& line : city.farLines)
    {
        const float width = std::max(line.halfWidth, 6.0f);
        for (size_t i = 0; i + 1 < line.points.size(); ++i)
        {
            const Vector2 a = line.points[i], b = line.points[i + 1];
            if (std::fabs(a.x) < inner && std::fabs(a.y) < inner && std::fabs(b.x) < inner && std::fabs(b.y) < inner) continue;
            const float length = Vector2Distance(a, b);
            if (length < 0.5f) continue;
            const Vector2 side = { -(b.y - a.y) / length * width, (b.x - a.x) / length * width };
            const float ya = grid.Sample(a.x, a.y) - 0.2f, yb = grid.Sample(b.x, b.y) - 0.2f;
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
    obstacles.resize(staticObstacleCount);
    interiors.clear();
    doors.clear();
    passages.resize(staticPassageCount);

    // Буянт-Ухаа нисэх буудлын хучилт зөвхөн хуучин (зохиомол) Улаанбаатарын үед (өвс ургахгүй, засмал замын хурд)
    layout.SetAirfieldActive(variant == kUlaanbaatarProvince && cityProvince < 0);

    MeshBuilder solidBuilder, shadowBuilder, glassBuilder;
    BuildSettlementStructures(variant, layout, terrain, obstacles, solidBuilder, shadowBuilder, glassBuilder, interiors,
                              doors, passages);

    settlementStructures = solidBuilder.UploadPartitioned(kPieceSize);
    settlementShadows = shadowBuilder.UploadPartitioned(kPieceSize);
    settlementGlass = glassBuilder.UploadPartitioned(kPieceSize);

    BuildObstacleGrid();
    FitWindowPassages();
    terrain.InvalidateDetails(); // өвс, чулуу шинэ барилгуудын дотор үлдэхгүй

    TraceLog(LOG_INFO, "WORLD: settlements variant %d: %d triangles, %d colliders total, %d enterable buildings", variant,
             solidBuilder.TriangleCount(), (int)obstacles.size(), (int)interiors.size());
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
    for (int i = 0; i < (int)interiors.size(); ++i)
    {
        // Хурдан шүүлт: ихэнх барилга хол байдаг
        if (std::fabs(interiors[i].center.x - x) > 12.0f || std::fabs(interiors[i].center.y - z) > 12.0f) continue;
        if (interiors[i].Contains(x, z))
        {
            return i;
        }
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

void World::ResolveHorizontal(Vector3& feetPos, float radius, float height, float stepHeight) const
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

    // Хаалттай (эсвэл дөнгөж хаагдаж буй) хаалганууд
    for (const Door& door : doors)
    {
        if (door.swing > 0.5f) continue;
        if (std::fabs(door.hinge.x - feetPos.x) > 4.0f || std::fabs(door.hinge.z - feetPos.z) > 4.0f) continue;

        const Obstacle blocker = door.Blocker();
        if (blocker.Top() > feetPos.y + stepHeight && blocker.Bottom() < feetPos.y + height)
        {
            PushOutOfBox(blocker, feetPos, radius);
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
    return std::max(distance, 0.5f);
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
    return distance;
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
    DrawPieces(boundaryPosts, material, view, kBoundaryDistance);

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
