#include "Terrain.h"

#include "MeshBuilder.h"
#include "Procedural.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
    const float kCellSize = 3.0f;
    const int kChunkCells = 20;          // нэг хэсэг = 60 x 60 м
    const float kLodDistance = 230.0f;   // үүнээс холын хэсгийг хоёр дахин бүдүүн тороор зурна
    const float kDrawDistance = 680.0f;  // үүнээс холын хэсгийг зурахгүй (мананд уусчихсан байдаг)
    const float kDetailLoadDistance = 150.0f; // өвс чулууны mesh-ийг үүсгэх зай

    // Mesh-ийг чөлөөлөх зай нь үүсгэх зайнаас хол: хил дээр нааш цааш явахад дахин дахин үүсгэхгүй.
    const float kGroundKeep = 160.0f;
    const float kFarKeep = 260.0f;
    const float kDetailKeep = 120.0f;

    // Тал хээрийн өнгөнүүд
    const Color kGrassGreen = { 110, 122, 70, 255 };
    const Color kGrassDry = { 160, 146, 88, 255 };
    const Color kEarth = { 128, 104, 74, 255 };
    const Color kRockyGround = { 122, 110, 96, 255 };
    const Color kTrampled = { 146, 128, 96, 255 };
    const Color kStone = { 128, 124, 118, 255 };
    const Color kBush = { 84, 98, 52, 255 };
    const Color kGrassLush = { 96, 126, 64, 255 };
    const Color kOchre = { 152, 130, 86, 255 };
    // Тал хээрийн цэцэг: алтан гагнуур (шар), хонхон цэцэг (ягаан хөх), цагаан цэцэг, улаан
    const Color kFlowers[4] = { { 236, 196, 52, 255 }, { 150, 110, 196, 255 }, { 236, 232, 220, 255 }, { 206, 72, 64, 255 } };
    const Color kBushDry = { 132, 112, 70, 255 };

    // Тэгшлээгүй үеийн толгодын өндөр.
    float Hills(float x, float z)
    {
        const float distance = std::sqrt(x * x + z * z);
        const float rolling = (Fbm(x * 0.018f, z * 0.018f, 3) - 0.5f) * 5.0f;
        const float big = Fbm(x * 0.0035f + 31.0f, z * 0.0035f + 17.0f, 3);
        const float far = SmoothStep(110.0f, 900.0f, distance); // төвөөс холдох тусам өндөр толгод
        return rolling + big * big * 70.0f * far + far * 5.0f;
    }

    // Улаанбаатарын төв: Туулын хөндийд тул урд хэсэг (төв талбай, Энхтайваны өргөн чөлөө) тэгш,
    // хойд зүгт (Чингэлтэй, Сүхбаатар дүүргийн гэр хороолол) толгод өөд аажмаар өгсөнө.
    float CityHills(float x, float z)
    {
        const float north = SmoothStep(450.0f, 2700.0f, z);
        const float rolling = (Fbm(x * 0.004f + 5.0f, z * 0.004f + 9.0f, 3) - 0.5f) * 20.0f * north;
        const float gentle = (Fbm(x * 0.008f + 21.0f, z * 0.008f + 4.0f, 2) - 0.5f) * 1.6f;
        return north * north * 52.0f + rolling + gentle * SmoothStep(300.0f, 900.0f, z);
    }

    bool InsideObstacle(const std::vector<const Obstacle*>& obstacles, float x, float z, float margin)
    {
        for (const Obstacle* o : obstacles)
        {
            if (std::fabs(x - o->center.x) <= o->size.x * 0.5f + margin &&
                std::fabs(z - o->center.z) <= o->size.z * 0.5f + margin)
            {
                return true;
            }
        }
        return false;
    }

    void AddGrassTuft(MeshBuilder& builder, Vector3 base, Rng& rng)
    {
        // Хиаг, ерхөг: ногоон, шаргал, цайвар ногоон хольцтой; ир бүр гадагшаа налж, үзүүр нь цайвар.
        const Color tint = rng.Chance(0.2f) ? MixColor(kGrassLush, kGrassGreen, rng.Next())
                                            : MixColor(kGrassGreen, kGrassDry, rng.Next());
        // Нимгэн ир нь ихэвчлэн нарны эсрэг харж бараан гардаг тул өнгийг нь цайвар авна.
        const Color root = ScaleColor(tint, 0.85f);
        Color tip = ScaleColor(tint, 1.55f);
        tip.a = 150; // alpha < 255: салхинд найгах хэмжээ (detail shader)
        const float height = rng.Range(0.22f, 0.62f);
        const int blades = rng.Chance(0.5f) ? 4 : 3;

        for (int blade = 0; blade < blades; ++blade)
        {
            const float angle = 2.0f * PI * ((float)blade + rng.Range(0.0f, 0.6f)) / (float)blades;
            const float sx = std::cos(angle + PI * 0.5f) * 0.05f;
            const float sz = std::sin(angle + PI * 0.5f) * 0.05f;
            const float lean = rng.Range(0.08f, 0.26f);
            const Vector3 top = { base.x + std::cos(angle) * lean, base.y + height * rng.Range(0.7f, 1.0f),
                                  base.z + std::sin(angle) * lean };
            builder.Triangle({ base.x - sx, base.y, base.z - sz }, { base.x + sx, base.y, base.z + sz }, top, root, root,
                             tip);
        }
    }

    // Зэрлэг цэцэг: нарийн иш дээр өнгөт толгой (хоёр загалмай гурвалжин)
    void AddFlower(MeshBuilder& builder, Vector3 base, Color petal, Rng& rng)
    {
        const float height = rng.Range(0.18f, 0.38f);
        const Vector3 head = { base.x + rng.Range(-0.04f, 0.04f), base.y + height, base.z + rng.Range(-0.04f, 0.04f) };
        Color stemTop = kGrassGreen;
        stemTop.a = 165;
        petal.a = 165;
        builder.Triangle({ base.x - 0.012f, base.y, base.z }, { base.x + 0.012f, base.y, base.z }, head, kGrassGreen, kGrassGreen, stemTop);
        const float r = rng.Range(0.04f, 0.07f);
        builder.Triangle({ head.x - r, head.y, head.z - r * 0.5f }, { head.x + r, head.y, head.z - r * 0.5f },
                         { head.x, head.y + r * 0.6f, head.z + r }, petal);
        builder.Triangle({ head.x, head.y, head.z - r }, { head.x - r * 0.8f, head.y + r * 0.4f, head.z + r * 0.6f },
                         { head.x + r * 0.8f, head.y + r * 0.4f, head.z + r * 0.6f }, ScaleColor(petal, 1.15f));
    }
}

Terrain::Terrain(const Layout& layout) : layout(layout)
{
    Rebuild();
}

void Terrain::Rebuild()
{
    for (Chunk& chunk : chunks)
    {
        UnloadBuiltMesh(chunk.ground);
        UnloadBuiltMesh(chunk.groundFar);
        UnloadBuiltMesh(chunk.details);
    }
    chunks.clear();
    loaded.clear();
    loadedMeshes = 0;
    maxHeight = 0.0f;

    cellSize = kCellSize;
    chunksPerSide = (int)std::ceil(kWorldHalfExtent * 2.0f / (cellSize * (float)kChunkCells));
    cells = chunksPerSide * kChunkCells;
    halfExtent = (float)cells * cellSize * 0.5f;

    // --- Өндрийн тор: бүх ертөнцөөр ---
    const int side = cells + 1;
    heights.resize((size_t)side * (size_t)side);
    for (int iz = 0; iz < side; ++iz)
    {
        const float z = -halfExtent + (float)iz * cellSize;
        for (int ix = 0; ix < side; ++ix)
        {
            const float x = -halfExtent + (float)ix * cellSize;
            float height;
            if (layout.IsCapital())
            {
                // Улаанбаатар: хойд зүгийн толгод; ШТС, застав зэрэг байгууламжийн доор тэгш
                height = CityHills(x, z) * SmoothStep(0.0f, 30.0f, layout.ZoneDistance(x, z)) - layout.LakeDepth(x, z);
            }
            else if (layout.IsCity())
            {
                // Аймгийн төв: барилгажсан хэсэг тэгш, хотоос гарах тусам хээр талын толгод (зам, хот айлын дэргэд тэгш)
                float mask = SmoothStep(60.0f, 420.0f, layout.CityBuiltDistance(x, z));
                if (mask > 0.0f) mask *= SmoothStep(0.0f, 30.0f, layout.FlatDistance(x, z));
                height = (mask > 0.0f ? Hills(x, z) * mask : 0.0f) - layout.LakeDepth(x, z);
            }
            else
            {
                // Зам, суурингийн орчим тэгш, тэндээс холдох тусам толгод аажим өндөрсөнө.
                const float mask = SmoothStep(0.0f, 30.0f, layout.FlatDistance(x, z));
                height = (mask > 0.0f ? Hills(x, z) * mask : 0.0f) - layout.LakeDepth(x, z);
            }
            heights[(size_t)iz * (size_t)side + (size_t)ix] = height;
            maxHeight = std::max(maxHeight, height);
        }
    }

    // --- Хэсгүүдийн хамрах бөмбөлөг (mesh-ийг дараа, хэрэг болоход үүсгэнэ) ---
    chunks.resize((size_t)chunksPerSide * (size_t)chunksPerSide);
    const float chunkSize = (float)kChunkCells * cellSize;
    for (int cz = 0; cz < chunksPerSide; ++cz)
    {
        for (int cx = 0; cx < chunksPerSide; ++cx)
        {
            Chunk& chunk = chunks[(size_t)cz * chunksPerSide + (size_t)cx];
            chunk.cellX = cx * kChunkCells;
            chunk.cellZ = cz * kChunkCells;

            float minY = 1e9f, maxY = -1e9f;
            for (int iz = chunk.cellZ; iz <= chunk.cellZ + kChunkCells; ++iz)
            {
                for (int ix = chunk.cellX; ix <= chunk.cellX + kChunkCells; ++ix)
                {
                    minY = std::min(minY, GridHeight(ix, iz));
                    maxY = std::max(maxY, GridHeight(ix, iz));
                }
            }

            chunk.center = { -halfExtent + ((float)cx + 0.5f) * chunkSize, (minY + maxY) * 0.5f,
                             -halfExtent + ((float)cz + 0.5f) * chunkSize };
            const float halfHeight = (maxY - minY) * 0.5f + 2.0f;
            chunk.radius = std::sqrt(chunkSize * chunkSize * 0.5f + halfHeight * halfHeight);
        }
    }
}

Terrain::~Terrain()
{
    for (Chunk& chunk : chunks)
    {
        UnloadBuiltMesh(chunk.ground);
        UnloadBuiltMesh(chunk.groundFar);
        UnloadBuiltMesh(chunk.details);
    }
}

Vector3 Terrain::GridNormal(int ix, int iz) const
{
    const int x0 = std::max(ix - 1, 0), x1 = std::min(ix + 1, cells);
    const int z0 = std::max(iz - 1, 0), z1 = std::min(iz + 1, cells);
    const float dx = (GridHeight(x1, iz) - GridHeight(x0, iz)) / ((float)(x1 - x0) * cellSize);
    const float dz = (GridHeight(ix, z1) - GridHeight(ix, z0)) / ((float)(z1 - z0) * cellSize);
    return Vector3Normalize({ -dx, 1.0f, -dz });
}

Color Terrain::GroundColor(int ix, int iz, Vector3 normal) const
{
    // Өнгө нь зөвхөн байрлалаас хамаарна: хөрш хэсэг, нарийн/бүдүүн тор ирмэгээрээ яг таарна.
    const float x = -halfExtent + (float)ix * cellSize, z = -halfExtent + (float)iz * cellSize;
    const float h = GridHeight(ix, iz);
    // Хот: тоостой хөрс, цэцэрлэг; аймгийн төвөөс гарахад хээр талын өнгө рүү аажим шилжинэ
    float urban = 0.0f;
    if (layout.IsCity())
    {
        urban = layout.IsCapital() ? 1.0f : SmoothStep(220.0f, 90.0f, layout.CityBuiltDistance(x, z));
        if (urban >= 1.0f) return CityGroundColor(x, z, normal);
    }

    // Том толбо: чийглэг ногоон хөндий ба хуурай шаргал тал
    const float moisture = Fbm(x * 0.011f + 7.0f, z * 0.011f + 3.0f, 3);
    Color color = MixColor(kGrassLush, kGrassGreen, SmoothStep(0.30f, 0.50f, moisture));
    color = MixColor(color, kGrassDry, SmoothStep(0.50f, 0.72f, moisture));

    // Дунд хэмжээний толбо: чийглэг бараан ногоон ба шар сүрлэн өвс ээлжилнэ
    const float patch = Fbm(x * 0.034f + 19.0f, z * 0.034f + 5.0f, 2);
    color = MixColor(color, { 80, 106, 58, 255 }, SmoothStep(0.6f, 0.78f, patch) * 0.55f);
    color = MixColor(color, { 178, 164, 110, 255 }, SmoothStep(0.4f, 0.24f, patch) * 0.45f);

    // Нар тусдаг энгэр хуурай, ар (сүүдэртэй) энгэр ногоон
    const float sunFacing = normal.x * kSunDirection.x + normal.z * kSunDirection.z;
    color = MixColor(color, kGrassDry, std::clamp(sunFacing * 3.5f, 0.0f, 0.55f));
    color = MixColor(color, kGrassLush, std::clamp(-sunFacing * 2.5f, 0.0f, 0.35f));

    // Өндөрт шаргал, оройд нь хад чулуу; эгц налуу чулуурхаг
    color = MixColor(color, kOchre, SmoothStep(12.0f, 36.0f, h) * 0.55f);
    color = MixColor(color, kRockyGround, SmoothStep(0.93f, 0.76f, normal.y));
    color = MixColor(color, kRockyGround, SmoothStep(40.0f, 72.0f, h) * 0.6f);

    // Жижиг нүцгэн хөрстэй толбо, бага зэргийн өнгөний хэлбэлзэл
    const float spots = ValueNoise(x * 0.09f + 40.0f, z * 0.09f + 11.0f);
    color = MixColor(color, kEarth, SmoothStep(0.74f, 0.96f, spots) * 0.4f);
    const float grain = ValueNoise(x * 0.43f + 3.0f, z * 0.43f + 9.0f);
    color = ScaleColor(color, 0.95f + grain * 0.1f);

    // Нуурын ёроол, эрэг: шавар, элс
    const float lake = layout.LakeDepth(x, z);
    if (lake > 0.0f) color = MixColor(color, { 132, 122, 100, 255 }, SmoothStep(0.0f, 0.6f, lake));

    // Суурин, замын орчим гишгэгдсэн шороо
    const float flat = layout.FlatDistance(x, z);
    color = MixColor(color, kTrampled, SmoothStep(6.0f, -10.0f, flat) * 0.55f);
    color = MixColor(color, kEarth, SmoothStep(3.0f, 0.0f, layout.RoadDistance(x, z)) * 0.5f);
    if (urban > 0.0f) color = MixColor(color, CityGroundColor(x, z, normal), urban);
    return color;
}

Color Terrain::CityGroundColor(float x, float z, Vector3 normal) const
{
    // Хотын хөрс: тоостой шороо, гишгэгдсэн хуурай өвс; цэцэрлэг, зүлэг ногоон.
    const float moisture = Fbm(x * 0.02f + 7.0f, z * 0.02f + 3.0f, 3);
    const float grain = ValueNoise(x * 0.43f + 3.0f, z * 0.43f + 9.0f);
    Color color = MixColor(kTrampled, kGrassDry, SmoothStep(0.35f, 0.65f, moisture));
    switch (layout.CityGround(x, z))
    {
    case 1: color = MixColor(kGrassGreen, kGrassLush, SmoothStep(0.3f, 0.7f, moisture)); break; // цэцэрлэг
    case 2: color = { 92, 124, 70, 255 }; break;                                                 // спорт талбай
    case 3: color = MixColor({ 74, 96, 54, 255 }, kGrassGreen, moisture); break;                 // ой
    case 4: color = MixColor(kGrassDry, kGrassGreen, 0.35f); break;                              // оршуулгын газар
    case 5: color = { 150, 146, 138, 255 }; break;                                               // хучилттай талбай
    case 6: color = { 112, 104, 86, 255 }; break;                                                // голын шавар
    default: break;
    }
    color = MixColor(color, kRockyGround, SmoothStep(0.93f, 0.8f, normal.y) * 0.6f);
    color = MixColor(color, kEarth, SmoothStep(4.0f, 0.0f, layout.RoadDistance(x, z)) * 0.45f); // замын захын шороо
    return ScaleColor(color, 0.95f + grain * 0.1f);
}

Mesh Terrain::BuildGround(const Chunk& chunk, int step) const
{
    // Газрын mesh: оройн normal ба өнгөтэй (гөлгөр гэрэлтүүлэг, зөөлөн өнгөний шилжилт).
    std::vector<float> positions, normals;
    std::vector<unsigned char> vertexColors;
    const int samples = kChunkCells / step + 1;
    std::vector<Vector3> gridPoint((size_t)samples * samples), gridNormal((size_t)samples * samples);
    std::vector<Color> gridColor((size_t)samples * samples);
    for (int j = 0; j < samples; ++j)
    {
        for (int i = 0; i < samples; ++i)
        {
            const int ix = chunk.cellX + i * step, iz = chunk.cellZ + j * step;
            const size_t k = (size_t)j * samples + i;
            gridPoint[k] = { -halfExtent + (float)ix * cellSize, GridHeight(ix, iz), -halfExtent + (float)iz * cellSize };
            gridNormal[k] = GridNormal(ix, iz);
            gridColor[k] = GroundColor(ix, iz, gridNormal[k]);
        }
    }
    auto vertex = [&](Vector3 p, Vector3 n, Color c) {
        positions.insert(positions.end(), { p.x, p.y, p.z });
        normals.insert(normals.end(), { n.x, n.y, n.z });
        vertexColors.insert(vertexColors.end(), { c.r, c.g, c.b, 255 });
    };
    auto corner = [&](int i, int j) {
        const size_t k = (size_t)j * samples + i;
        vertex(gridPoint[k], gridNormal[k], gridColor[k]);
    };
    for (int j = 0; j + 1 < samples; ++j)
    {
        for (int i = 0; i + 1 < samples; ++i)
        {
            // step = 1 үед HeightAt-ийн хуваалттай ижил байх ёстой.
            corner(i, j); corner(i, j + 1); corner(i + 1, j);
            corner(i + 1, j); corner(i, j + 1); corner(i + 1, j + 1);
        }
    }

    if (step > 1)
    {
        // Бүдүүн тор нь хөрш нарийн хэсэгтэй ирмэгээрээ яг таарахгүй тул завсрыг доош унжсан хормойгоор хаана.
        const float drop = 4.0f;
        auto skirt = [&](int i0, int j0, int i1, int j1) {
            const size_t a = (size_t)j0 * samples + i0, b = (size_t)j1 * samples + i1;
            const Vector3 pa = gridPoint[a], pb = gridPoint[b];
            const Vector3 qa = { pa.x, pa.y - drop, pa.z }, qb = { pb.x, pb.y - drop, pb.z };
            vertex(pa, gridNormal[a], gridColor[a]); vertex(pb, gridNormal[b], gridColor[b]); vertex(qb, gridNormal[b], gridColor[b]);
            vertex(pa, gridNormal[a], gridColor[a]); vertex(qb, gridNormal[b], gridColor[b]); vertex(qa, gridNormal[a], gridColor[a]);
        };
        const int last = samples - 1;
        for (int i = 0; i < last; ++i)
        {
            skirt(i, 0, i + 1, 0);
            skirt(i, last, i + 1, last);
            skirt(0, i, 0, i + 1);
            skirt(last, i, last, i + 1);
        }
    }

    Mesh mesh = {};
    mesh.vertexCount = (int)(positions.size() / 3);
    mesh.triangleCount = mesh.vertexCount / 3;
    mesh.vertices = (float*)MemAlloc((unsigned int)(positions.size() * sizeof(float)));
    mesh.normals = (float*)MemAlloc((unsigned int)(normals.size() * sizeof(float)));
    mesh.colors = (unsigned char*)MemAlloc((unsigned int)vertexColors.size());
    std::memcpy(mesh.vertices, positions.data(), positions.size() * sizeof(float));
    std::memcpy(mesh.normals, normals.data(), normals.size() * sizeof(float));
    std::memcpy(mesh.colors, vertexColors.data(), vertexColors.size());
    UploadMesh(&mesh, false);
    return mesh;
}

Mesh Terrain::BuildDetails(const Chunk& chunk) const
{
    const float chunkSize = (float)kChunkCells * cellSize;
    const float x0 = chunk.center.x - chunkSize * 0.5f;
    const float z0 = chunk.center.z - chunkSize * 0.5f;

    // Зөвхөн энэ хэсэгтэй давхцах саадуудыг шалгана.
    std::vector<const Obstacle*> nearby;
    if (obstacles)
    {
        for (const Obstacle& o : *obstacles)
        {
            if (std::fabs(o.center.x - chunk.center.x) <= chunkSize * 0.5f + o.size.x * 0.5f + 2.0f &&
                std::fabs(o.center.z - chunk.center.z) <= chunkSize * 0.5f + o.size.z * 0.5f + 2.0f)
            {
                nearby.push_back(&o);
            }
        }
    }

    MeshBuilder builder;
    Rng rng((uint32_t)(chunk.cellX * 31337 + chunk.cellZ * 2713 + 99));
    // Хотын барилгажсан хэсэг (Улаанбаатар бүхэлдээ)
    auto InUrban = [&](float x, float z) {
        return layout.IsCity() && (layout.IsCapital() || layout.CityBuiltDistance(x, z) < 150.0f);
    };

    // --- Өвс: бөөгнөрч ургана ---
    for (int i = 0; i < 800; ++i)
    {
        const float x = x0 + rng.Next() * chunkSize;
        const float z = z0 + rng.Next() * chunkSize;
        const bool keep = rng.Chance(0.75f);

        if (InUrban(x, z))
        {
            const int ground = layout.CityGround(x, z);
            if (ground >= 5 || (ground == 0 && !rng.Chance(0.3f))) continue;
        }
        if (Fbm(x * 0.05f, z * 0.05f, 2) < 0.44f) continue;
        if (layout.RoadDistance(x, z) < 0.5f || layout.Paved(x, z) || layout.LakeDepth(x, z) > 0.3f) continue;
        if (layout.FlatDistance(x, z) < 0.0f && !keep) continue; // суурин дотор сийрэг
        if (InsideObstacle(nearby, x, z, 0.15f)) continue;

        AddGrassTuft(builder, { x, HeightAt(x, z), z }, rng);
    }

    // --- Зэрлэг цэцэг: нэг төрлийнх нь бөөнөөрөө ургана ---
    for (int patch = 0; patch < 6; ++patch)
    {
        const float px = x0 + rng.Next() * chunkSize;
        const float pz = z0 + rng.Next() * chunkSize;
        const Color petal = kFlowers[rng.Int(0, 3)];
        if (layout.FlatDistance(px, pz) < 2.0f) continue; // суурин, замын ойролцоо гишгэгдсэн
        if (InUrban(px, pz) && layout.CityGround(px, pz) != 1) continue; // хотод зөвхөн цэцэрлэгт
        for (int i = 0; i < 14; ++i)
        {
            const float x = px + rng.Range(-2.5f, 2.5f), z = pz + rng.Range(-2.5f, 2.5f);
            if (InsideObstacle(nearby, x, z, 0.2f) || layout.Paved(x, z)) continue;
            AddFlower(builder, { x, HeightAt(x, z), z }, JitterColor(petal, rng, 0.08f), rng);
        }
    }

    // --- Жижиг чулуу: газарт хагас шигдсэн, заримдаа бөөгнөрсөн, хаг ургасан ---
    for (int i = 0; i < 34; ++i)
    {
        const float x = x0 + rng.Next() * chunkSize;
        const float z = z0 + rng.Next() * chunkSize;
        const float radius = rng.Range(0.12f, 0.5f);
        const Vector3 radii = { radius * rng.Range(0.8f, 1.4f), radius * 0.6f, radius * rng.Range(0.8f, 1.4f) };

        if (layout.RoadDistance(x, z) < 1.0f || layout.Paved(x, z)) continue;
        if (InUrban(x, z) && (layout.CityGround(x, z) != 0 || !rng.Chance(0.35f))) continue;
        if (InsideObstacle(nearby, x, z, 0.6f)) continue;

        const float ground = HeightAt(x, z);
        const Color stone = rng.Chance(0.25f) ? MixColor(kStone, { 150, 140, 96, 255 }, 0.4f) : JitterColor(kStone, rng, 0.15f);
        builder.Rock({ x, ground + radii.y * 0.15f, z }, radii, stone, rng);
        if (rng.Chance(0.35f))
        {
            const int extra = rng.Int(1, 3);
            for (int k = 0; k < extra; ++k)
            {
                const float r = radius * rng.Range(0.3f, 0.6f);
                const float ex = x + rng.Range(-1.0f, 1.0f) * radius * 2.0f, ez = z + rng.Range(-1.0f, 1.0f) * radius * 2.0f;
                builder.Rock({ ex, HeightAt(ex, ez) + r * 0.1f, ez }, { r, r * 0.6f, r }, JitterColor(kStone, rng, 0.15f), rng);
            }
        }
    }

    // --- Бут сөөг: тэгш бус газар, толгодын хажуугаар ---
    for (int i = 0; i < 12; ++i)
    {
        const float x = x0 + rng.Next() * chunkSize;
        const float z = z0 + rng.Next() * chunkSize;
        const float size = rng.Range(0.5f, 1.0f);
        const Color color = MixColor(kBush, kBushDry, rng.Next());

        if (Fbm(x * 0.02f + 7.0f, z * 0.02f + 3.0f, 2) < 0.48f) continue;
        if (layout.RoadDistance(x, z) < 2.0f || layout.Paved(x, z)) continue;
        if (InUrban(x, z) && layout.CityGround(x, z) != 1 && layout.CityGround(x, z) != 3) continue;
        if (InsideObstacle(nearby, x, z, 1.2f)) continue;

        // Харгана: доороо бараан, дээшээ цайвар бөмбөлгүүд, ёроолоос хуурай мөчир цухуйна
        const float ground = HeightAt(x, z);
        for (int part = 0; part < 6; ++part)
        {
            const float angle = (float)part * 1.9f + rng.Range(0.0f, 0.6f);
            const float spread = part == 0 ? 0.0f : size * 0.42f;
            const float r = size * rng.Range(0.32f, 0.5f) * (part == 0 ? 1.25f : 1.0f);
            const bool upper = part == 0 || part % 3 == 0;
            Color leaf = JitterColor(upper ? ScaleColor(color, 1.12f) : ScaleColor(color, 0.86f), rng, 0.08f);
            leaf.a = 235; // бага зэрэг найгана
            builder.Ellipsoid({ x + std::cos(angle) * spread, ground + r * (upper ? 1.15f : 0.75f), z + std::sin(angle) * spread },
                              { r, r * 0.8f, r }, 3, 6, leaf);
        }
        for (int twig = 0; twig < 3; ++twig)
        {
            const float a = rng.Range(0.0f, 2.0f * PI);
            const Vector3 root = { x + std::cos(a) * size * 0.3f, ground, z + std::sin(a) * size * 0.3f };
            const Vector3 tip = { x + std::cos(a) * size * 0.9f, ground + size * rng.Range(0.6f, 1.0f), z + std::sin(a) * size * 0.9f };
            builder.Triangle({ root.x - 0.02f, root.y, root.z }, { root.x + 0.02f, root.y, root.z }, tip, Color{ 96, 74, 52, 255 });
        }
    }

    return builder.Upload();
}

void Terrain::InvalidateDetails()
{
    for (int index : loaded)
    {
        Chunk& chunk = chunks[(size_t)index];
        if (chunk.hasDetails)
        {
            UnloadBuiltMesh(chunk.details);
            chunk.hasDetails = false;
            --loadedMeshes;
        }
    }
}

float Terrain::HeightAt(float x, float z) const
{
    const float limit = (float)cells - 0.001f;
    const float gx = std::clamp((x + halfExtent) / cellSize, 0.0f, limit);
    const float gz = std::clamp((z + halfExtent) / cellSize, 0.0f, limit);
    const int ix = (int)gx;
    const int iz = (int)gz;
    const float fx = gx - (float)ix;
    const float fz = gz - (float)iz;

    const float h00 = GridHeight(ix, iz);
    const float h10 = GridHeight(ix + 1, iz);
    const float h01 = GridHeight(ix, iz + 1);
    const float h11 = GridHeight(ix + 1, iz + 1);

    if (fx + fz <= 1.0f)
    {
        return h00 + (h10 - h00) * fx + (h01 - h00) * fz;
    }
    return h11 + (h01 - h11) * (1.0f - fx) + (h10 - h11) * (1.0f - fz);
}

void Terrain::ChunkRange(Vector3 position, float radius, int& x0, int& x1, int& z0, int& z1) const
{
    const float chunkSize = (float)kChunkCells * cellSize;
    x0 = std::clamp((int)std::floor((position.x - radius + halfExtent) / chunkSize), 0, chunksPerSide - 1);
    x1 = std::clamp((int)std::floor((position.x + radius + halfExtent) / chunkSize), 0, chunksPerSide - 1);
    z0 = std::clamp((int)std::floor((position.z - radius + halfExtent) / chunkSize), 0, chunksPerSide - 1);
    z1 = std::clamp((int)std::floor((position.z + radius + halfExtent) / chunkSize), 0, chunksPerSide - 1);
}

void Terrain::Stream(Vector3 viewPosition, int budget)
{
    // --- Холдсон хэсгүүдийн mesh-ийг чөлөөлнө ---
    for (size_t i = 0; i < loaded.size();)
    {
        Chunk& chunk = chunks[(size_t)loaded[i]];
        const float distance = Vector3Distance(chunk.center, viewPosition) - chunk.radius;

        if (chunk.hasGround && distance > kLodDistance + kGroundKeep)
        {
            UnloadBuiltMesh(chunk.ground);
            chunk.hasGround = false;
            --loadedMeshes;
        }
        if (chunk.hasFar && distance > kDrawDistance + kFarKeep)
        {
            UnloadBuiltMesh(chunk.groundFar);
            chunk.hasFar = false;
            --loadedMeshes;
        }
        if (chunk.hasDetails && distance > kDetailLoadDistance + kDetailKeep)
        {
            UnloadBuiltMesh(chunk.details);
            chunk.hasDetails = false;
            --loadedMeshes;
        }

        if (!chunk.hasGround && !chunk.hasFar && !chunk.hasDetails)
        {
            loaded[i] = loaded.back();
            loaded.pop_back();
        }
        else
        {
            ++i;
        }
    }

    // --- Хэрэгтэй боловч байхгүй mesh-үүдийг ойроос нь эхлэн үүсгэнэ ---
    struct Job
    {
        float distance;
        int chunk;
        int kind; // 0 = нарийн газар, 1 = бүдүүн газар, 2 = өвс чулуу
    };
    std::vector<Job> jobs;

    int x0, x1, z0, z1;
    ChunkRange(viewPosition, kDrawDistance + 120.0f, x0, x1, z0, z1);
    for (int cz = z0; cz <= z1; ++cz)
    {
        for (int cx = x0; cx <= x1; ++cx)
        {
            const int index = cz * chunksPerSide + cx;
            const Chunk& chunk = chunks[(size_t)index];
            const float distance = Vector3Distance(chunk.center, viewPosition) - chunk.radius;

            if (distance < kLodDistance + 40.0f && !chunk.hasGround) jobs.push_back({ distance, index, 0 });
            // Бүдүүн тор: ойрын хэсэгт ч үүсгэнэ, нарийн нь бэлэн болох хүртэл газар цоорхой харагдахгүй.
            if (distance < kDrawDistance + 60.0f && !chunk.hasFar) jobs.push_back({ distance - 60.0f, index, 1 });
            if (distance < kDetailLoadDistance && !chunk.hasDetails) jobs.push_back({ distance + 30.0f, index, 2 });
        }
    }
    std::sort(jobs.begin(), jobs.end(), [](const Job& a, const Job& b) { return a.distance < b.distance; });

    for (const Job& job : jobs)
    {
        const int cost = job.kind == 1 ? 1 : 3; // нарийн тор, өвс чулуу нь бүдүүн торноос ~3 дахин үнэтэй
        if (budget < cost)
        {
            break;
        }
        budget -= cost;

        Chunk& chunk = chunks[(size_t)job.chunk];
        const bool wasEmpty = !chunk.hasGround && !chunk.hasFar && !chunk.hasDetails;
        if (job.kind == 0)
        {
            chunk.ground = BuildGround(chunk, 1);
            chunk.hasGround = true;
        }
        else if (job.kind == 1)
        {
            chunk.groundFar = BuildGround(chunk, 2);
            chunk.hasFar = true;
        }
        else
        {
            chunk.details = BuildDetails(chunk);
            chunk.hasDetails = true;
        }
        ++loadedMeshes;
        if (wasEmpty)
        {
            loaded.push_back(job.chunk);
        }
    }
}

void Terrain::DrawShadowCasters(Vector3 center, float radius, const Material& material) const
{
    int x0, x1, z0, z1;
    ChunkRange(center, radius, x0, x1, z0, z1);
    for (int cz = z0; cz <= z1; ++cz)
    {
        for (int cx = x0; cx <= x1; ++cx)
        {
            const Chunk& chunk = chunks[(size_t)cz * chunksPerSide + (size_t)cx];
            if (chunk.hasGround) DrawBuiltMesh(chunk.ground, material);
            else if (chunk.hasFar) DrawBuiltMesh(chunk.groundFar, material);
        }
    }
}

void Terrain::Draw(const Camera3D& camera, const Material& material, const Material& detailMaterial, float detailDistance) const
{
    const ViewCone view = ViewCone::From(camera);

    int x0, x1, z0, z1;
    ChunkRange(camera.position, kDrawDistance + 60.0f, x0, x1, z0, z1);

    lastDrawnChunks = 0;
    for (int cz = z0; cz <= z1; ++cz)
    {
        for (int cx = x0; cx <= x1; ++cx)
        {
            const Chunk& chunk = chunks[(size_t)cz * chunksPerSide + (size_t)cx];
            if (!view.Sees(chunk.center, chunk.radius, kDrawDistance))
            {
                continue;
            }

            // Ойрын хэсэг бүрэн нарийвчлалтай, холынх бүдүүн тороор. Хүссэн нь бэлэн биш бол нөгөөг нь зурна.
            const float distance = Vector3Distance(chunk.center, camera.position);
            const bool wantFine = distance < kLodDistance + chunk.radius;
            if (wantFine && chunk.hasGround) DrawBuiltMesh(chunk.ground, material);
            else if (chunk.hasFar) DrawBuiltMesh(chunk.groundFar, material);
            else if (chunk.hasGround) DrawBuiltMesh(chunk.ground, material);
            else continue;

            if (chunk.hasDetails && distance < detailDistance + chunk.radius)
            {
                DrawBuiltMesh(chunk.details, detailMaterial);
            }
            ++lastDrawnChunks;
        }
    }
}
