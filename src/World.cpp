#include "World.h"

#include "Structures.h"
#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>

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
    // Тогтмол хэсэг: зам, пост, агуулах, шон, чулуу. Нэг удаа үүснэ.
    MeshBuilder solidBuilder, shadowBuilder, glassBuilder;
    BuildStaticStructures(layout, terrain, obstacles, solidBuilder, shadowBuilder, glassBuilder, interiors, doors);

    structures = solidBuilder.UploadPartitioned(kPieceSize);
    shadows = shadowBuilder.UploadPartitioned(kPieceSize);
    glass = glassBuilder.UploadPartitioned(kPieceSize);
    staticObstacleCount = obstacles.size();
    terrain.SetObstacles(&obstacles); // өвс, чулууг саад дотор ургуулахгүй

    TraceLog(LOG_INFO, "WORLD: %d static structure triangles in %d pieces, %d colliders", solidBuilder.TriangleCount(),
             (int)structures.size(), (int)obstacles.size());

    // Суурингууд: эхлээд 0-р хувилбараар; үе эхлэх бүрд тухайн үеийнхээр солигдоно.
    BuildSettlements(0);

    // Даалгавар эхлэх хүртэл: бүх талбарыг хамарсан дөрвөлжин хязгаар (тэмдэг шонгүй).
    const float edge = halfSize - 1.0f;
    Shape square;
    square.outline = { { -edge, -edge }, { edge, -edge }, { edge, edge }, { -edge, edge } };
    SetBoundary(square, false);
}

World::~World()
{
    UnloadPieces(structures);
    UnloadPieces(shadows);
    UnloadPieces(glass);
    UnloadPieces(settlementStructures);
    UnloadPieces(settlementShadows);
    UnloadPieces(settlementGlass);
    UnloadPieces(boundaryPosts);
    UnloadPieces(boundaryBand);
}

void World::BuildSettlements(int variant)
{
    if (variant == settlementVariant)
    {
        return;
    }
    settlementVariant = variant;

    // Өмнөх үеийн суурингуудыг арилгана: геометр, мөргөлдөөн, хаалга, дотор тал.
    UnloadPieces(settlementStructures);
    UnloadPieces(settlementShadows);
    UnloadPieces(settlementGlass);
    obstacles.resize(staticObstacleCount);
    interiors.clear();
    doors.clear();

    MeshBuilder solidBuilder, shadowBuilder, glassBuilder;
    BuildSettlementStructures(variant, layout, terrain, obstacles, solidBuilder, shadowBuilder, glassBuilder, interiors,
                              doors);

    settlementStructures = solidBuilder.UploadPartitioned(kPieceSize);
    settlementShadows = shadowBuilder.UploadPartitioned(kPieceSize);
    settlementGlass = glassBuilder.UploadPartitioned(kPieceSize);

    BuildObstacleGrid();
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
    const float kDoorPoundTime = 2.5f;     // зомби хаалгыг эвдэхэд шаардагдах хугацаа
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
                if (o.Top() <= feetPos.y + stepHeight || o.Bottom() >= feetPos.y + height)
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

void World::Draw(const Camera3D& camera, const Material& material, float detailDistance) const
{
    const ViewCone view = ViewCone::From(camera);

    // Гараар үүсгэсэн mesh-үүдийн гурвалжны эргэлтийн чиглэл жигд биш тул хоёр талаас нь зурна.
    rlDisableBackfaceCulling();

    terrain.Draw(camera, material, detailDistance);
    DrawPieces(structures, material, view, kStructureDistance);
    DrawPieces(settlementStructures, material, view, kStructureDistance);
    DrawPieces(boundaryPosts, material, view, kBoundaryDistance);

    // Сүүдэр: газар дээр наалдсан хагас тунгалаг давхарга, гүний буферт бичихгүй.
    rlDisableDepthMask();
    DrawPieces(shadows, material, view, kShadowDistance);
    DrawPieces(settlementShadows, material, view, kShadowDistance);
    rlEnableDepthMask();

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
