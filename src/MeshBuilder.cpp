#include "MeshBuilder.h"

#include "Input.h"

#include "raymath.h"

#include <algorithm>
#include <cmath>
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

void MeshBuilder::Vertex(Vector3 position, Color color)
{
    if (hasTransform)
    {
        position = Vector3Transform(position, transform);
    }
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
        return mesh;
    }
}

Mesh MeshBuilder::Upload() const
{
    return UploadArrays(vertices, colors);
}

std::vector<MeshPiece> MeshBuilder::UploadPartitioned(float cellSize) const
{
    struct Bucket
    {
        std::vector<float> positions;
        std::vector<unsigned char> vertexColors;
    };
    std::map<std::pair<int, int>, Bucket> buckets;

    // Гурвалжин бүрийг хүндийн төвийнх нь нүдэнд хийнэ.
    const size_t triangleCount = vertices.size() / 9;
    for (size_t t = 0; t < triangleCount; ++t)
    {
        const float* v = &vertices[t * 9];
        const float cx = (v[0] + v[3] + v[6]) / 3.0f;
        const float cz = (v[2] + v[5] + v[8]) / 3.0f;
        Bucket& bucket = buckets[{ (int)std::floor(cx / cellSize), (int)std::floor(cz / cellSize) }];
        bucket.positions.insert(bucket.positions.end(), v, v + 9);
        bucket.vertexColors.insert(bucket.vertexColors.end(), &colors[t * 12], &colors[t * 12] + 12);
    }

    std::vector<MeshPiece> pieces;
    for (const auto& entry : buckets)
    {
        const std::vector<float>& p = entry.second.positions;

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
        piece.mesh = UploadArrays(p, entry.second.vertexColors);
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
