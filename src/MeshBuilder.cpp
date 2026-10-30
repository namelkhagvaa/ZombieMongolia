#include "MeshBuilder.h"

#include "Input.h"

#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <utility>

void MeshBuilder::SetTransform(const Matrix& matrix)
{
    transform = matrix;
    hasTransform = true;
}

void MeshBuilder::ResetTransform()
{
    hasTransform = false;
}

Vector3 MeshBuilder::Placed(Vector3 local) const
{
    if (hasTransform) local = Vector3Transform(local, transform);
    return Vector3Add(local, offset);
}

void MeshBuilder::Vertex(Vector3 position, Color color)
{
    if (hasTransform)
    {
        position = Vector3Transform(position, transform);
    }
    position = Vector3Add(position, offset);
    vertices.push_back(position.x);
    vertices.push_back(position.y);
    vertices.push_back(position.z);
    colors.push_back(color.r);
    colors.push_back(color.g);
    colors.push_back(color.b);
    colors.push_back(color.a);
}

void MeshBuilder::Triangle(Vector3 a, Vector3 b, Vector3 c, Color color)
{
    Vertex(a, color);
    Vertex(b, color);
    Vertex(c, color);
}

void MeshBuilder::Triangle(Vector3 a, Vector3 b, Vector3 c, Color ca, Color cb, Color cc)
{
    Vertex(a, ca);
    Vertex(b, cb);
    Vertex(c, cc);
}

void MeshBuilder::Quad(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color color)
{
    Triangle(a, b, c, color);
    Triangle(a, c, d, color);
}

void MeshBuilder::Box(Vector3 center, Vector3 size, Color color)
{
    const float x0 = center.x - size.x * 0.5f, x1 = center.x + size.x * 0.5f;
    const float y0 = center.y - size.y * 0.5f, y1 = center.y + size.y * 0.5f;
    const float z0 = center.z - size.z * 0.5f, z1 = center.z + size.z * 0.5f;

    Quad({ x0, y1, z0 }, { x0, y1, z1 }, { x1, y1, z1 }, { x1, y1, z0 }, color); // дээд
    Quad({ x0, y0, z0 }, { x1, y0, z0 }, { x1, y0, z1 }, { x0, y0, z1 }, color); // доод
    Quad({ x0, y0, z1 }, { x1, y0, z1 }, { x1, y1, z1 }, { x0, y1, z1 }, color); // +Z
    Quad({ x1, y0, z0 }, { x0, y0, z0 }, { x0, y1, z0 }, { x1, y1, z0 }, color); // -Z
    Quad({ x1, y0, z1 }, { x1, y0, z0 }, { x1, y1, z0 }, { x1, y1, z1 }, color); // +X
    Quad({ x0, y0, z0 }, { x0, y0, z1 }, { x0, y1, z1 }, { x0, y1, z0 }, color); // -X
}

void MeshBuilder::Cylinder(Vector3 base, float radiusBottom, float radiusTop, float height, int sides, Color color,
                           bool cap)
{
    const Vector3 topCenter = { base.x, base.y + height, base.z };
    for (int i = 0; i < sides; ++i)
    {
        const float a0 = 2.0f * PI * (float)i / (float)sides;
        const float a1 = 2.0f * PI * (float)(i + 1) / (float)sides;
        const Vector3 b0 = { base.x + std::sin(a0) * radiusBottom, base.y, base.z + std::cos(a0) * radiusBottom };
        const Vector3 b1 = { base.x + std::sin(a1) * radiusBottom, base.y, base.z + std::cos(a1) * radiusBottom };
        const Vector3 t0 = { base.x + std::sin(a0) * radiusTop, topCenter.y, base.z + std::cos(a0) * radiusTop };
        const Vector3 t1 = { base.x + std::sin(a1) * radiusTop, topCenter.y, base.z + std::cos(a1) * radiusTop };

        if (radiusTop > 0.0f)
        {
            Quad(b0, b1, t1, t0, color);
            if (cap)
            {
                Triangle(topCenter, t0, t1, color);
            }
        }
        else
        {
            Triangle(b0, b1, topCenter, color);
        }
    }
}

void MeshBuilder::GableRoof(Vector3 baseCenter, float width, float depth, float height, bool ridgeAlongX,
                            Color color)
{
    const float x0 = baseCenter.x - width * 0.5f, x1 = baseCenter.x + width * 0.5f;
    const float z0 = baseCenter.z - depth * 0.5f, z1 = baseCenter.z + depth * 0.5f;
    const float y0 = baseCenter.y, y1 = baseCenter.y + height;
    const Color gable = ScaleColor(color, 0.85f);

    if (ridgeAlongX)
    {
        const Vector3 r0 = { x0, y1, baseCenter.z };
        const Vector3 r1 = { x1, y1, baseCenter.z };
        Quad({ x0, y0, z0 }, { x1, y0, z0 }, r1, r0, color);
        Quad({ x1, y0, z1 }, { x0, y0, z1 }, r0, r1, color);
        Triangle({ x0, y0, z0 }, { x0, y0, z1 }, r0, gable);
        Triangle({ x1, y0, z1 }, { x1, y0, z0 }, r1, gable);
    }
    else
    {
        const Vector3 r0 = { baseCenter.x, y1, z0 };
        const Vector3 r1 = { baseCenter.x, y1, z1 };
        Quad({ x0, y0, z1 }, { x0, y0, z0 }, r0, r1, color);
        Quad({ x1, y0, z0 }, { x1, y0, z1 }, r1, r0, color);
        Triangle({ x1, y0, z0 }, { x0, y0, z0 }, r0, gable);
        Triangle({ x0, y0, z1 }, { x1, y0, z1 }, r1, gable);
    }
}

void MeshBuilder::Blob(Vector3 center, Vector3 radii, Color color, Rng& rng, float colorJitter)
{
    // Найман талстын 6 оройг санамсаргүй сунгаж жигд бус хэлбэр гаргана.
    Vector3 px = { center.x + radii.x * rng.Range(0.7f, 1.15f), center.y + radii.y * rng.Range(-0.2f, 0.2f), center.z };
    Vector3 nx = { center.x - radii.x * rng.Range(0.7f, 1.15f), center.y + radii.y * rng.Range(-0.2f, 0.2f), center.z };
    Vector3 pz = { center.x, center.y + radii.y * rng.Range(-0.2f, 0.2f), center.z + radii.z * rng.Range(0.7f, 1.15f) };
    Vector3 nz = { center.x, center.y + radii.y * rng.Range(-0.2f, 0.2f), center.z - radii.z * rng.Range(0.7f, 1.15f) };
    Vector3 py = { center.x + radii.x * rng.Range(-0.3f, 0.3f), center.y + radii.y * rng.Range(0.8f, 1.15f),
                   center.z + radii.z * rng.Range(-0.3f, 0.3f) };
    Vector3 ny = { center.x, center.y - radii.y, center.z };

    const Vector3 ring[4] = { px, pz, nx, nz };
    for (int i = 0; i < 4; ++i)
    {
        const Vector3 a = ring[i];
        const Vector3 b = ring[(i + 1) % 4];
        Triangle(a, b, py, JitterColor(color, rng, colorJitter));
        Triangle(b, a, ny, JitterColor(ScaleColor(color, 0.8f), rng, colorJitter));
    }
}

void MeshBuilder::Rock(Vector3 center, Vector3 radii, Color color, Rng& rng)
{
    const int sides = 7;
    Vector3 lower[sides], upper[sides];
    const float twist = rng.Range(0.0f, 2.0f * PI);
    for (int i = 0; i < sides; ++i)
    {
        const float a = twist + 2.0f * PI * (float)i / (float)sides;
        const float r = rng.Range(0.8f, 1.12f);
        lower[i] = { center.x + std::cos(a) * radii.x * r, center.y - radii.y * 0.35f, center.z + std::sin(a) * radii.z * r };
        const float b = a + PI / (float)sides;
        const float u = rng.Range(0.5f, 0.75f);
        upper[i] = { center.x + std::cos(b) * radii.x * u, center.y + radii.y * rng.Range(0.45f, 0.7f),
                     center.z + std::sin(b) * radii.z * u };
    }
    const Vector3 top = { center.x + radii.x * rng.Range(-0.15f, 0.15f), center.y + radii.y * rng.Range(0.85f, 1.0f),
                          center.z + radii.z * rng.Range(-0.15f, 0.15f) };
    const Vector3 bottom = { center.x, center.y - radii.y, center.z };
    const Color dark = ScaleColor(color, 0.78f);
    for (int i = 0; i < sides; ++i)
    {
        const int j = (i + 1) % sides;
        Triangle(lower[i], upper[i], lower[j], JitterColor(color, rng, 0.06f));
        Triangle(lower[j], upper[i], upper[j], JitterColor(color, rng, 0.06f));
        Triangle(upper[i], top, upper[j], JitterColor(ScaleColor(color, 1.08f), rng, 0.05f));
        Triangle(lower[j], bottom, lower[i], dark);
    }
}

void MeshBuilder::BevelBox(Vector3 center, Vector3 size, float bevel, Color color)
{
    // Булан бүрийг гурван цэгээр (тэнхлэг бүрийн дагуу bevel-ээр дотогш) солино:
    // 6 үндсэн тал, 12 налуу ирмэг, 8 булангийн гурвалжин.
    const float hx = size.x * 0.5f, hy = size.y * 0.5f, hz = size.z * 0.5f;
    bevel = std::min({ bevel, hx * 0.9f, hy * 0.9f, hz * 0.9f });
    const Color edge = ScaleColor(color, 0.92f);
    auto P = [&](float sx, float sy, float sz, int inset) {
        // inset: аль тэнхлэгийн дагуу дотогш (0 = x, 1 = y, 2 = z) ороогүй хувилбар; бусад хоёр нь дотогш.
        const float x = sx * (inset == 0 ? hx : hx - bevel);
        const float y = sy * (inset == 1 ? hy : hy - bevel);
        const float z = sz * (inset == 2 ? hz : hz - bevel);
        return Vector3{ center.x + x, center.y + y, center.z + z };
    };
    // Тал бүр: тэр тэнхлэгийн дагуу гадна байх 4 булангийн цэг
    for (int axis = 0; axis < 3; ++axis)
    {
        for (float s : { -1.0f, 1.0f })
        {
            Vector3 c[4];
            const float u[4] = { -1, 1, 1, -1 }, v[4] = { -1, -1, 1, 1 };
            for (int k = 0; k < 4; ++k)
            {
                float sx = 0, sy = 0, sz = 0;
                if (axis == 0) { sx = s; sy = u[k]; sz = v[k]; }
                if (axis == 1) { sy = s; sz = u[k]; sx = v[k]; }
                if (axis == 2) { sz = s; sx = u[k]; sy = v[k]; }
                c[k] = P(sx, sy, sz, axis);
            }
            if (s > 0) Quad(c[0], c[1], c[2], c[3], color);
            else Quad(c[0], c[3], c[2], c[1], color);
        }
    }
    // Ирмэгүүд: хоёр тэнхлэгийн тэмдэг тогтмол, гурав дахь нь дагуу
    for (int along = 0; along < 3; ++along)
    {
        const int a = (along + 1) % 3, b = (along + 2) % 3;
        for (float sa : { -1.0f, 1.0f })
        {
            for (float sb : { -1.0f, 1.0f })
            {
                float s0[3], s1[3];
                s0[along] = -1; s1[along] = 1;
                s0[a] = s1[a] = sa;
                s0[b] = s1[b] = sb;
                const Vector3 p0 = P(s0[0], s0[1], s0[2], a), p1 = P(s1[0], s1[1], s1[2], a);
                const Vector3 q0 = P(s0[0], s0[1], s0[2], b), q1 = P(s1[0], s1[1], s1[2], b);
                if (sa * sb > 0) Quad(p0, p1, q1, q0, edge);
                else Quad(p0, q0, q1, p1, edge);
            }
        }
    }
    // Булангууд
    for (float sx : { -1.0f, 1.0f })
    {
        for (float sy : { -1.0f, 1.0f })
        {
            for (float sz : { -1.0f, 1.0f })
            {
                const Vector3 a = P(sx, sy, sz, 0), b = P(sx, sy, sz, 1), c = P(sx, sy, sz, 2);
                if (sx * sy * sz > 0) Triangle(a, b, c, edge);
                else Triangle(a, c, b, edge);
            }
        }
    }
}

void MeshBuilder::Tube(Vector3 a, Vector3 b, float radiusA, float radiusB, int sides, Color color, bool caps)
{
    const Vector3 axis = Vector3Subtract(b, a);
    const float length = Vector3Length(axis);
    if (length < 1e-4f) return;
    const Vector3 dir = Vector3Scale(axis, 1.0f / length);
    // Тэнхлэгт перпендикуляр хоёр нэгж вектор
    const Vector3 helper = std::fabs(dir.y) < 0.9f ? Vector3{ 0.0f, 1.0f, 0.0f } : Vector3{ 1.0f, 0.0f, 0.0f };
    const Vector3 u = Vector3Normalize(Vector3CrossProduct(dir, helper));
    const Vector3 v = Vector3CrossProduct(dir, u);
    for (int i = 0; i < sides; ++i)
    {
        const float a0 = 2.0f * PI * (float)i / (float)sides, a1 = 2.0f * PI * (float)(i + 1) / (float)sides;
        const Vector3 r0 = Vector3Add(Vector3Scale(u, std::cos(a0)), Vector3Scale(v, std::sin(a0)));
        const Vector3 r1 = Vector3Add(Vector3Scale(u, std::cos(a1)), Vector3Scale(v, std::sin(a1)));
        const Vector3 pa0 = Vector3Add(a, Vector3Scale(r0, radiusA)), pa1 = Vector3Add(a, Vector3Scale(r1, radiusA));
        const Vector3 pb0 = Vector3Add(b, Vector3Scale(r0, radiusB)), pb1 = Vector3Add(b, Vector3Scale(r1, radiusB));
        Quad(pa0, pa1, pb1, pb0, color);
        if (caps)
        {
            if (radiusA > 0.0f) Triangle(a, pa1, pa0, color);
            if (radiusB > 0.0f) Triangle(b, pb0, pb1, color);
        }
    }
}

void MeshBuilder::Ellipsoid(Vector3 center, Vector3 radii, int rings, int segments, Color color)
{
    auto point = [&](int ring, int segment) {
        const float theta = PI * (float)ring / (float)rings;            // 0 = дээд туйл
        const float phi = 2.0f * PI * (float)segment / (float)segments;
        return Vector3{ center.x + radii.x * std::sin(theta) * std::sin(phi), center.y + radii.y * std::cos(theta),
                        center.z + radii.z * std::sin(theta) * std::cos(phi) };
    };
    for (int r = 0; r < rings; ++r)
    {
        for (int s = 0; s < segments; ++s)
        {
            const Vector3 a = point(r, s), b = point(r + 1, s), c = point(r + 1, s + 1), d = point(r, s + 1);
            if (r == 0) Triangle(a, b, c, color);
            else if (r == rings - 1) Triangle(a, b, d, color);
            else Quad(a, b, c, d, color);
        }
    }
}

void MeshBuilder::DrawImmediate() const
{
    const int count = (int)(vertices.size() / 3);
    const int chunk = 3000; // rlgl-ийн batch-д багтаахаар хэсэгчилнэ (3-т хуваагдана)
    for (int start = 0; start < count; start += chunk)
    {
        const int end = std::min(count, start + chunk);
        rlCheckRenderBatchLimit(end - start);
        rlBegin(RL_TRIANGLES);
        for (int i = start; i < end; ++i)
        {
            rlColor4ub(colors[(size_t)i * 4], colors[(size_t)i * 4 + 1], colors[(size_t)i * 4 + 2], colors[(size_t)i * 4 + 3]);
            rlVertex3f(vertices[(size_t)i * 3], vertices[(size_t)i * 3 + 1], vertices[(size_t)i * 3 + 2]);
        }
        rlEnd();
    }
}

namespace
{
    Mesh UploadArrays(const std::vector<float>& positions, const std::vector<unsigned char>& vertexColors)
    {
        Mesh mesh = {};
        if (positions.empty())
        {
            return mesh;
        }

        mesh.vertexCount = (int)(positions.size() / 3);
        mesh.triangleCount = mesh.vertexCount / 3;

        // raylib UnloadMesh нь MemFree ашигладаг тул MemAlloc-оор хуулна.
        mesh.vertices = (float*)MemAlloc((unsigned int)(positions.size() * sizeof(float)));
        mesh.colors = (unsigned char*)MemAlloc((unsigned int)vertexColors.size());
        std::memcpy(mesh.vertices, positions.data(), positions.size() * sizeof(float));
        std::memcpy(mesh.colors, vertexColors.data(), vertexColors.size());

        UploadMesh(&mesh, false);
        // GPU-д хуулагдсан тул санах ой дахь хуулбарыг чөлөөлнө (суурингууд л гэхэд хэдэн арван MB; утсан дээрх
        // хөтөч санах ой хэтэрвэл хуудсыг зогсоодог). Зурахад хэрэггүй, UnloadMesh нь nullptr-ийг алгасна.
        MemFree(mesh.vertices);
        MemFree(mesh.colors);
        mesh.vertices = nullptr;
        mesh.colors = nullptr;
        return mesh;
    }
}

Mesh MeshBuilder::Upload() const
{
    return UploadArrays(vertices, colors);
}

void MeshBuilder::EncodeGroundHeight(const std::function<float(float, float)>& ground)
{
    const size_t count = vertices.size() / 3;
    for (size_t i = 0; i < count; ++i)
    {
        unsigned char& alpha = colors[i * 4 + 3];
        if (alpha != 255) continue;
        const float* p = &vertices[i * 3];
        const float height = p[1] - ground(p[0], p[2]);
        alpha = (unsigned char)std::clamp((int)std::lround(height / 0.4f), 0, 250);
    }
}

std::vector<MeshPiece> MeshBuilder::UploadPartitioned(float cellSize) const
{
    // Гурвалжин бүрийг хүндийн төвийнх нь нүдэнд хийнэ: зөвхөн дугаарыг нь (бүх геометрийг дахин хуулбал
    // суурингууд дээр санах ой хэдэн арван MB-аар өсөж, утсан дээрх хөтөч хуудсыг зогсоодог байсан).
    std::map<std::pair<int, int>, std::vector<uint32_t>> buckets;
    const size_t triangleCount = vertices.size() / 9;
    for (size_t t = 0; t < triangleCount; ++t)
    {
        const float* v = &vertices[t * 9];
        const float cx = (v[0] + v[3] + v[6]) / 3.0f;
        const float cz = (v[2] + v[5] + v[8]) / 3.0f;
        buckets[{ (int)std::floor(cx / cellSize), (int)std::floor(cz / cellSize) }].push_back((uint32_t)t);
    }

    // Нүд бүрийн геометрийг нэг нэгээр нь цуглуулж, GPU-д хуулна (нэг л нүдний хуулбар зэрэг байна).
    std::vector<MeshPiece> pieces;
    std::vector<float> p;
    std::vector<unsigned char> pieceColors;
    for (const auto& entry : buckets)
    {
        p.clear();
        pieceColors.clear();
        for (const uint32_t t : entry.second)
        {
            p.insert(p.end(), &vertices[(size_t)t * 9], &vertices[(size_t)t * 9] + 9);
            pieceColors.insert(pieceColors.end(), &colors[(size_t)t * 12], &colors[(size_t)t * 12] + 12);
        }

        // Хамрах бөмбөлөг: хязгаарын хайрцгийн төв ба хамгийн алсын орой
        Vector3 low = { p[0], p[1], p[2] }, high = low;
        for (size_t i = 0; i < p.size(); i += 3)
        {
            low = { std::min(low.x, p[i]), std::min(low.y, p[i + 1]), std::min(low.z, p[i + 2]) };
            high = { std::max(high.x, p[i]), std::max(high.y, p[i + 1]), std::max(high.z, p[i + 2]) };
        }

        MeshPiece piece;
        piece.center = Vector3Scale(Vector3Add(low, high), 0.5f);
        piece.radius = Vector3Distance(low, high) * 0.5f;
        piece.mesh = UploadArrays(p, pieceColors);
        pieces.push_back(piece);
    }
    return pieces;
}

MeshBuilder MeshBuilder::Extract(const std::vector<std::pair<size_t, size_t>>& ranges)
{
    MeshBuilder out;
    if (ranges.empty()) return out;
    // Үлдэх гурвалжнуудыг байранд нь урагш шахна (хотын геометрийг хоёр дахин хуулахгүй)
    size_t write = 0, read = 0;
    auto keep = [&](size_t until) {
        if (until > read && write != read)
        {
            std::copy(vertices.begin() + (std::ptrdiff_t)(read * 9), vertices.begin() + (std::ptrdiff_t)(until * 9),
                      vertices.begin() + (std::ptrdiff_t)(write * 9));
            std::copy(colors.begin() + (std::ptrdiff_t)(read * 12), colors.begin() + (std::ptrdiff_t)(until * 12),
                      colors.begin() + (std::ptrdiff_t)(write * 12));
        }
        if (until > read) write += until - read;
    };
    for (const auto& range : ranges)
    {
        keep(range.first);
        out.vertices.insert(out.vertices.end(), vertices.begin() + (std::ptrdiff_t)(range.first * 9),
                            vertices.begin() + (std::ptrdiff_t)((range.first + range.second) * 9));
        out.colors.insert(out.colors.end(), colors.begin() + (std::ptrdiff_t)(range.first * 12),
                          colors.begin() + (std::ptrdiff_t)((range.first + range.second) * 12));
        read = range.first + range.second;
    }
    keep(vertices.size() / 9);
    vertices.resize(write * 9);
    colors.resize(write * 12);
    return out;
}

std::vector<MeshPiece> MeshBuilder::UploadGroups(float cellSize, const std::vector<std::pair<size_t, size_t>>& groups,
                                                 std::vector<int>& groupPiece, std::vector<int>& groupStart) const
{
    std::map<std::pair<int, int>, std::vector<uint32_t>> buckets;
    for (size_t g = 0; g < groups.size(); ++g)
    {
        if (groups[g].second == 0) continue;
        const float* v = &vertices[groups[g].first * 9];
        const float cx = (v[0] + v[3] + v[6]) / 3.0f;
        const float cz = (v[2] + v[5] + v[8]) / 3.0f;
        buckets[{ (int)std::floor(cx / cellSize), (int)std::floor(cz / cellSize) }].push_back((uint32_t)g);
    }
    groupPiece.assign(groups.size(), -1);
    groupStart.assign(groups.size(), 0);

    std::vector<MeshPiece> pieces;
    std::vector<float> p;
    std::vector<unsigned char> pieceColors;
    for (const auto& entry : buckets)
    {
        p.clear();
        pieceColors.clear();
        for (const uint32_t g : entry.second)
        {
            groupPiece[g] = (int)pieces.size();
            groupStart[g] = (int)(p.size() / 3);
            const size_t first = groups[g].first, count = groups[g].second;
            p.insert(p.end(), &vertices[first * 9], &vertices[first * 9] + count * 9);
            pieceColors.insert(pieceColors.end(), &colors[first * 12], &colors[first * 12] + count * 12);
        }
        Vector3 low = { p[0], p[1], p[2] }, high = low;
        for (size_t i = 0; i < p.size(); i += 3)
        {
            low = { std::min(low.x, p[i]), std::min(low.y, p[i + 1]), std::min(low.z, p[i + 2]) };
            high = { std::max(high.x, p[i]), std::max(high.y, p[i + 1]), std::max(high.z, p[i + 2]) };
        }
        MeshPiece piece;
        piece.center = Vector3Scale(Vector3Add(low, high), 0.5f);
        piece.radius = Vector3Distance(low, high) * 0.5f;
        piece.mesh = UploadArrays(p, pieceColors);
        pieces.push_back(piece);
    }
    return pieces;
}

ViewCone ViewCone::From(const Camera3D& camera)
{
    const float aspect = (float)ViewWidth() / (float)std::max(1, ViewHeight());
    const float tanHalf = std::tan(camera.fovy * DEG2RAD * 0.5f);
    return { camera.position, Vector3Normalize(Vector3Subtract(camera.target, camera.position)),
             std::atan(tanHalf * std::sqrt(1.0f + aspect * aspect)) };
}

bool ViewCone::Sees(Vector3 center, float radius, float maxDistance) const
{
    const Vector3 offset = Vector3Subtract(center, position);
    const float distance = Vector3Length(offset);
    if (distance > maxDistance + radius)
    {
        return false;
    }
    if (distance <= radius)
    {
        return true; // камер бөмбөлөг дотор
    }
    const float angle = std::acos(std::clamp(Vector3DotProduct(offset, forward) / distance, -1.0f, 1.0f));
    return angle <= halfAngle + std::asin(radius / distance);
}

void DrawPieces(const std::vector<MeshPiece>& pieces, const Material& material, const ViewCone& view,
                float maxDistance)
{
    for (const MeshPiece& piece : pieces)
    {
        if (view.Sees(piece.center, piece.radius, maxDistance))
        {
            DrawBuiltMesh(piece.mesh, material);
        }
    }
}

void UnloadPieces(std::vector<MeshPiece>& pieces)
{
    for (MeshPiece& piece : pieces)
    {
        UnloadBuiltMesh(piece.mesh);
    }
    pieces.clear();
}

void DrawBuiltMesh(const Mesh& mesh, const Material& material)
{
    if (mesh.vertexCount > 0)
    {
        DrawMesh(mesh, material, MatrixIdentity());
    }
}

void UnloadBuiltMesh(Mesh& mesh)
{
    if (mesh.vertexCount > 0)
    {
        UnloadMesh(mesh);
        mesh = {};
    }
}
