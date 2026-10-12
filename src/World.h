#pragma once

#include "Layout.h"
#include "MeshBuilder.h"
#include "Obstacle.h"
#include "Polygon.h"
#include "Terrain.h"
#include "raylib.h"

#include <cmath>
#include <vector>

// Газрын гадаргуугийн төрөл: тээврийн хэрэгслийн хурдад нөлөөлнө.
enum class Surface
{
    Asphalt, // засмал зам
    Dirt,    // шороон зам
    Steppe   // хээр тал (зам биш)
};

// Тоглоомын талбар: толгодтой газар, байгууламжууд, тэдгээртэй мөргөлдөх асуулгууд.
//
// Ертөнц том (5.4 км) тул:
//  - газрын mesh-ийг камерын эргэн тойронд явцын дунд үүсгэнэ (Terrain::Stream),
//  - барилга, зам, сүүдрийн геометрийг бүсчилж, зөвхөн ойр, харагдаж буйг зурна,
//  - саадуудыг тороор индексжүүлж, мөргөлдөөнийг зөвхөн ойрын саадуудтай шалгана.
class World
{
public:
    // Цонх нээгдсэний дараа үүсгэнэ (GPU mesh үүсгэдэг).
    explicit World(float halfSize);
    ~World();

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    // Суурин, отор, жижиг суурингуудыг variant (аяны үеийн дугаар)-аар шинээр үүсгэнэ: үе бүрд байшингууд,
    // тэдгээрийн зохион байгуулалт өөр. Үе эхлэхэд дуудна; ижил variant-аар дахин дуудвал юу ч хийхгүй.
    void BuildSettlements(int variant);

    // Камерын ойр орчмын газрыг бэлтгэнэ. Frame бүрд дуудна; everything = true бол хэрэгтэй бүгдийг
    // нэг дор үүсгэнэ (даалгавар эхлэх, алс газар шилжих үед).
    void Stream(Vector3 viewPosition, bool everything = false);

    // Газрын өндөр (саадыг тооцохгүй).
    float GroundHeight(float x, float z) const { return terrain.HeightAt(x, z); }

    // (x, z) цэгийн гадаргуугийн төрөл.
    Surface SurfaceAt(float x, float z) const;

    // (x, z) цэгийн доорх хамгийн өндөр гадаргуу (газар эсвэл саадын орой); maxY-аас дээшхийг тооцохгүй.
    float SurfaceHeightAt(float x, float z, float maxY) const;

    // Босоо цилиндр (хөлийн байрлал feetPos) саад руу орсон бол хэвтээ чиглэлд түлхэж гаргана.
    void ResolveHorizontal(Vector3& feetPos, float radius, float height, float stepHeight) const;

    // from цэгээс dir чиглэлд саад, газарт тулахгүй явж болох зай (камерт).
    float ClipDistance(Vector3 from, Vector3 dir, float maxDistance) const;

    // Туяа саад эсвэл газарт тусах хүртэлх зай; юунд ч тусахгүй бол maxDistance (сум, харааны шугамд).
    float RayDistance(Ray ray, float maxDistance) const;

    float HalfSize() const { return halfSize; }

    // Даалгаврын талбарын хязгаар (аймгийн хилийн хэлбэр). Хэн ч үүнээс гарч, нүх рүү нь орж чадахгүй.
    // markers = true бол хил дагуух тэмдэг шонгуудыг үүсгэнэ.
    void SetBoundary(const Shape& shape, bool markers = true);
    const Shape& Boundary() const { return boundary; }
    bool InsideBoundary(float x, float z) const { return boundary.Contains({ x, z }); }
    // Газрын зурагт дүүргэхэд: хилийн доторх талбай ба нүхнүүдийн гурвалжнууд (x, z).
    const std::vector<Vector2>& BoundaryFill() const { return boundaryFill; }
    const std::vector<Vector2>& BoundaryHoleFill() const { return boundaryHoleFill; }

    // Жижиг газрын зурагт
    const std::vector<Obstacle>& Obstacles() const { return obstacles; }
    const Layout& GetLayout() const { return layout; }

    // (minX..maxX, minZ..maxZ) тэгш өнцөгттэй давхцаж болох саадуудын индексийг out-д нэмнэ (давхардаж болно).
    void ObstaclesNear(float minX, float minZ, float maxX, float maxZ, std::vector<int>& out) const;

    // --- Хаалга ба барилгын дотор тал ---
    // position-д хамгийн ойр, maxDistance доторх хаалга; -1 = байхгүй.
    int NearestDoor(Vector3 position, float maxDistance) const;
    bool IsDoorOpen(int index) const { return doors[index].open; }
    float DoorDistance(int index, Vector3 position) const
    {
        const Vector3 center = doors[index].Center();
        const float dx = center.x - position.x, dz = center.z - position.z;
        return std::sqrt(dx * dx + dz * dz);
    }
    void SetDoorOpen(int index, bool open);
    void CloseAllDoors();
    // Хаалганы эргэх хөдөлгөөнийг шинэчилнэ.
    void UpdateDoors(float dt);
    // Зомби хаалттай хаалгыг нүднэ: position-ий дэргэдэх хаалга хангалттай нүдүүлбэл эвдэрч нээгдэнэ.
    void PoundDoors(Vector3 position, float dt);
    // (x, z) цэг аль барилгын дотор байгаа; -1 = гадаа.
    int InteriorAt(float x, float z) const;
    const Interior& GetInterior(int index) const { return interiors[index]; }
    // Ойрын хаалгануудыг зурна (BeginMode3D + орчны shader дотор).
    void DrawDoors(Vector3 viewPosition) const;

    // --- Тусгай гарцууд: шат, цонх ---
    // position-д (хөлийн байрлал) хамгийн ойр эхлэлтэй гарц; -1 = байхгүй.
    int NearestPassage(Vector3 position, float maxDistance) const;
    const Passage& GetPassage(int index) const { return passages[(size_t)index]; }
    int PassageCount() const { return (int)passages.size(); }

    // Газар, байгууламж, сүүдэр. Орчны shader-тэй material дамжуулна.
    void Draw(const Camera3D& camera, const Material& material, float detailDistance) const;
    // Хагас тунгалаг хэсгүүд (торон хашаа): бусад бүх зүйлийн дараа зурна.
    void DrawTransparent(const Camera3D& camera, const Material& material) const;
    void DrawColliders(Vector3 viewPosition) const;

    int DrawnChunks() const { return terrain.LastDrawnChunks(); }
    int LoadedTerrainMeshes() const { return terrain.LoadedMeshes(); }
    int ObstacleCount() const { return (int)obstacles.size(); }

private:
    void BuildObstacleGrid();
    // Цонхны гарцын зогсох цэгүүд хашаа, тавилга зэрэг саадтай давхцвал ойрын чөлөөт газар руу шилжүүлнэ;
    // хол шилжүүлэх шаардлагатай бол (давсны дараа гацна) гарцыг хасна.
    void FitWindowPassages();
    float DoorRayDistance(Ray ray, float maxDistance) const;
    int GridCell(float value) const;

    float halfSize;
    Layout layout;
    Terrain terrain;
    std::vector<Obstacle> obstacles;
    std::vector<Interior> interiors;
    std::vector<Door> doors;
    std::vector<Passage> passages;
    size_t staticPassageCount = 0; // эхний хэсэг нь тогтмол (цамхаг), үлдсэн нь суурингийнх

    // Саадын тор: нүд бүрд тэр нүдтэй давхцах саадуудын индекс
    int gridSide = 0;
    std::vector<std::vector<int>> obstacleGrid;

    // Бүсчилсэн хөдөлгөөнгүй геометр: тогтмол хэсэг
    std::vector<MeshPiece> structures;
    std::vector<MeshPiece> shadows;
    std::vector<MeshPiece> glass;

    // Үе бүрд дахин үүсдэг суурингууд
    std::vector<MeshPiece> settlementStructures;
    std::vector<MeshPiece> settlementShadows;
    std::vector<MeshPiece> settlementGlass;
    size_t staticObstacleCount = 0; // obstacles-ийн эхний хэсэг нь тогтмол, үлдсэн нь суурингийнх
    int settlementVariant = -1;

    Shape boundary;
    std::vector<Vector2> boundaryFill;
    std::vector<Vector2> boundaryHoleFill;
    std::vector<MeshPiece> boundaryPosts; // хилийн шонгууд
    std::vector<MeshPiece> boundaryBand;  // шонгийн хоорондох хагас тунгалаг улаан зурвас
};
