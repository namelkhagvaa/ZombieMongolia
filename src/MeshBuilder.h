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
    // Бүх оройд (хувиргалтын дараа) нэмэгдэх шилжилт: байгууламжийг газрын өндөрт өргөнө.
    void SetOffset(Vector3 value) { offset = value; }

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

    // Чулуу: хоёр давхар жигд бус цагираг, хавтгайвтар орой (найман талстаас дугуйвтар, бодит).
    void Rock(Vector3 center, Vector3 radii, Color color, Rng& rng);

    // Ирмэгүүдийг bevel хэмжээгээр налуу тайрсан хайрцаг: машины их бие, тагнаас хурц өнцөг арилгана.
    void BevelBox(Vector3 center, Vector3 size, float bevel, Color color);
    // a -> b хоёр цэгийн хоорондох (конус) хоолой: хөл, хүзүү, яндан, сэрээ, дугуйн тэнхлэг.
    void Tube(Vector3 a, Vector3 b, float radiusA, float radiusB, int sides, Color color, bool caps = true);
    // Эллипсоид: морины их бие, толгой, түлшний сав, гэрэл.
    void Ellipsoid(Vector3 center, Vector3 radii, int rings, int segments, Color color);

    // Угсарсан гурвалжнуудыг rlgl-ийн одоогийн матрицаар шууд зурна (хөдөлдөг объект: унаа, морь, дугуй).
    // Shader нь ертөнцийн байрлалаас гэрэл тооцдог тул DrawMesh-ийн хувиргалтын оронд үүнийг ашиглана.
    void DrawImmediate() const;

    int TriangleCount() const { return (int)(vertices.size() / 9); }
    bool Empty() const { return vertices.empty(); }

    // GPU руу хуулж mesh буцаана (хоосон бол vertexCount = 0). Цонх нээгдсэний дараа дуудна.
    Mesh Upload() const;

    // Гурвалжнуудыг cellSize хэмжээтэй (x, z) торын нүднүүдээр бүлэглэж, нүд бүрд тусдаа mesh үүсгэнэ.
    // Том ертөнцийн барилга, замыг ингэж хуваавал зөвхөн ойрын хэсгийг зурах боломжтой болно.
    std::vector<MeshPiece> UploadPartitioned(float cellSize) const;

    // Гурвалжны мужуудыг (first, count; өсөх дарааллаар, давхцахгүй) энэ builder-ээс тасдаж, шинэ builder-т
    // дараалуулан хийнэ.
    MeshBuilder Extract(const std::vector<std::pair<size_t, size_t>>& ranges);
    // Бүлэг (first, count гурвалжин) бүрийг бүхлээр нь нэг нүдэнд хийж хуваана; бүлэг бүрийн хэсэг ба түүн дэх
    // эхний оройг буцаана (дараа нь UpdateMeshBuffer-оор нуух).
    std::vector<MeshPiece> UploadGroups(float cellSize, const std::vector<std::pair<size_t, size_t>>& groups,
                                        std::vector<int>& groupPiece, std::vector<int>& groupStart) const;

private:
    void Vertex(Vector3 position, Color color);

    std::vector<float> vertices;
    std::vector<unsigned char> colors;
    Matrix transform = {};
    bool hasTransform = false;
    Vector3 offset = { 0.0f, 0.0f, 0.0f };
};

// vertexCount = 0 бол юу ч хийхгүй.
void DrawBuiltMesh(const Mesh& mesh, const Material& material);
void UnloadBuiltMesh(Mesh& mesh);

// Харагдаж буй, maxDistance доторх хэсгүүдийг зурна.
void DrawPieces(const std::vector<MeshPiece>& pieces, const Material& material, const ViewCone& view,
                float maxDistance);
void UnloadPieces(std::vector<MeshPiece>& pieces);
