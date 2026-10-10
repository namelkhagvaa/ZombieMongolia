#pragma once

#include "Procedural.h"
#include "raylib.h"

#include <vector>

// Том mesh-ийн нэг бүсэд ноогдох хэсэг: зөвхөн камерт ойр, харагдаж байгаа хэсгүүдийг зурна.
struct MeshPiece
{
    Mesh mesh = {};
    Vector3 center = {};
    float radius = 0.0f;
};

// Камерын харах конус: бөмбөлөг харагдах эсэхийг хурдан шалгана.
struct ViewCone
{
    Vector3 position;
    Vector3 forward;
    float halfAngle; // дэлгэцийн булан хүртэлх өнцөг, радиан

    static ViewCone From(const Camera3D& camera);
    // maxDistance-аас хол эсвэл конусын гадна бол false.
    bool Sees(Vector3 center, float radius, float maxDistance) const;
};

// Олон энгийн дүрсийг нэг mesh-д нэгтгэнэ: нэг удаа үүсгээд, нэг draw call-оор зурна.
// Оройн өнгө ашиглана; гэрэлтүүлгийг shader нь гадаргуугийн налуугаас өөрөө тооцдог тул normal хэрэггүй.
class MeshBuilder
{
public:
    // Дараа нэмэгдэх бүх оройд хэрэглэгдэх хувиргалт (хазайлгах, эргүүлэх).
    void SetTransform(const Matrix& matrix);
    void ResetTransform();

    void Triangle(Vector3 a, Vector3 b, Vector3 c, Color color);
    void Triangle(Vector3 a, Vector3 b, Vector3 c, Color ca, Color cb, Color cc);
    void Quad(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color color); // оройнууд тойрох дарааллаар

    void Box(Vector3 center, Vector3 size, Color color);

    // base: суурийн төв. radiusTop = 0 бол конус.
    void Cylinder(Vector3 base, float radiusBottom, float radiusTop, float height, int sides, Color color,
                  bool cap = true);

    // Хоёр налуу дээвэр. baseCenter: дээврийн суурийн төв, width = X, depth = Z.
    void GableRoof(Vector3 baseCenter, float width, float depth, float height, bool ridgeAlongX, Color color);

    // Жигд бус найман талст: чулуу, бут.
    void Blob(Vector3 center, Vector3 radii, Color color, Rng& rng, float colorJitter = 0.08f);

    int TriangleCount() const { return (int)(vertices.size() / 9); }
    bool Empty() const { return vertices.empty(); }

    // GPU руу хуулж mesh буцаана (хоосон бол vertexCount = 0). Цонх нээгдсэний дараа дуудна.
    Mesh Upload() const;

    // Гурвалжнуудыг cellSize хэмжээтэй (x, z) торын нүднүүдээр бүлэглэж, нүд бүрд тусдаа mesh үүсгэнэ.
    // Том ертөнцийн барилга, замыг ингэж хуваавал зөвхөн ойрын хэсгийг зурах боломжтой болно.
    std::vector<MeshPiece> UploadPartitioned(float cellSize) const;

private:
    void Vertex(Vector3 position, Color color);

    std::vector<float> vertices;
    std::vector<unsigned char> colors;
    Matrix transform = {};
    bool hasTransform = false;
};

// vertexCount = 0 бол юу ч хийхгүй.
void DrawBuiltMesh(const Mesh& mesh, const Material& material);
void UnloadBuiltMesh(Mesh& mesh);

// Харагдаж буй, maxDistance доторх хэсгүүдийг зурна.
void DrawPieces(const std::vector<MeshPiece>& pieces, const Material& material, const ViewCone& view,
                float maxDistance);
void UnloadPieces(std::vector<MeshPiece>& pieces);
