#pragma once

#include "Layout.h"
#include "Obstacle.h"
#include "raylib.h"

#include <vector>

// Толгодтой газрын гадаргуу: бүх ертөнцийн өндрийн тор + хэсэг (chunk) бүрийн mesh.
//
// Ертөнц 5.4 км өргөн тул бүх хэсгийн mesh-ийг нэг дор үүсгэвэл санах ой, эхлэх хугацаа хэт их болно.
// Тиймээс mesh-ийг камерын эргэн тойронд явцын дунд үүсгэж (frame бүрд цөөн тоогоор), холдсоныг чөлөөлнө.
// Хэсэг бүр гурван mesh-тэй байж болно: газар (ойроос нарийн, холоос бүдүүн) ба өвс/чулуу/бут (зөвхөн ойроос).
class Terrain
{
public:
    // Цонх нээгдсэний дараа үүсгэнэ. Өндрийн торыг бүхэлд нь тооцно (mesh үүсгэхгүй).
    explicit Terrain(const Layout& layout);
    ~Terrain();

    Terrain(const Terrain&) = delete;
    Terrain& operator=(const Terrain&) = delete;

    // Өвс, чулууг ургуулахгүй байх саадуудын жагсаалт. Саадууд байрласны дараа нэг удаа дуудна.
    void SetObstacles(const std::vector<Obstacle>* list) { obstacles = list; }

    // Саадууд өөрчлөгдсөн (суурингууд дахин үүссэн): өвс, чулууны mesh-үүдийг хаяж, дахин үүсгүүлнэ.
    void InvalidateDetails();

    // Mesh-ийн гурвалжинтай яг таарах өндөр. Ертөнцийн аль ч цэгт ажиллана.
    float HeightAt(float x, float z) const;
    float MaxHeight() const { return maxHeight; }

    // Камерын ойр орчмын хэсгүүдийн mesh-ийг үүсгэж, холдсоныг чөлөөлнө.
    // budget: энэ дуудалтад үүсгэх ажлын дээд хэмжээ (нэг frame-д бага, эхлэхэд хязгааргүй).
    void Stream(Vector3 viewPosition, int budget);

    // Камерт харагдахгүй хэсгийг алгасна; detailDistance-аас холын өвс чулууг зурахгүй.
    void Draw(const Camera3D& camera, const Material& material, float detailDistance) const;

    int LastDrawnChunks() const { return lastDrawnChunks; }
    int LoadedMeshes() const { return loadedMeshes; }

private:
    struct Chunk
    {
        Mesh ground = {};    // бүрэн нарийвчлал
        Mesh groundFar = {}; // холоос: хоёр дахин бүдүүн тор
        Mesh details = {};   // өвс, чулуу, бут
        bool hasGround = false, hasFar = false, hasDetails = false;
        Vector3 center = {};
        float radius = 0.0f;
        int cellX = 0; // хэсгийн эхний нүд
        int cellZ = 0;
    };

    float GridHeight(int ix, int iz) const { return heights[(size_t)iz * (size_t)(cells + 1) + (size_t)ix]; }

    // step: нэг дөрвөлжинд орох нүдний тоо (1 = бүрэн, 2 = бүдүүн).
    Mesh BuildGround(const Chunk& chunk, int step) const;
    Mesh BuildDetails(const Chunk& chunk) const;

    // Камерын эргэн тойрны radius зайд багтах хэсгүүдийн индексийн муж.
    void ChunkRange(Vector3 position, float radius, int& x0, int& x1, int& z0, int& z1) const;

    const Layout& layout;
    const std::vector<Obstacle>* obstacles = nullptr;

    int cells = 0;          // нэг талын нүдний тоо
    int chunksPerSide = 0;
    float cellSize = 0.0f;
    float halfExtent = 0.0f;
    float maxHeight = 0.0f;
    std::vector<float> heights;
    std::vector<Chunk> chunks;
    std::vector<int> loaded; // ядаж нэг mesh-тэй хэсгүүд
    int loadedMeshes = 0;
    mutable int lastDrawnChunks = 0;
};
