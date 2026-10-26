#pragma once

#include "raylib.h"

#include <cstdint>
#include <vector>

// Тоглолтын явцын гэнэтийн үйл явдлын харагдах хэсэг: ачааны хайрцаг (нисэх онгоцноос шүхэртэй унах эсвэл сүйрсэн
// машины дэргэд), газрын зургийн түр тэмдэг (зомбийн бөөгнөрөл, аврах хүн). Үйл явдлыг хост сонгож (Game),
// зочинд мэдээлнэ; хайрцгийг хоёр тал аль нь түрүүлж авсан нь авна.
class WorldEvents
{
public:
    void Clear();
    // altitude > 0: шүхэртэй унаж буй (газардтал авч болохгүй)
    void AddCrate(uint16_t id, Vector3 ground, float altitude);
    bool RemoveCrate(uint16_t id);
    void AddMarker(Vector3 position, float seconds);
    void Update(float dt);
    // Газардсан хайрцаг position-оос radius дотор байвал түүний дугаар (-1 = байхгүй)
    int CrateNear(Vector3 position, float radius) const;
    // Сая газардсан хайрцгууд (хост: ойрын зомбиг сэрээнэ)
    std::vector<Vector3> TakeLanded();
    // Газрын зурагт: хайрцаг, түр тэмдэг
    void Markers(std::vector<Vector3>& out) const;

    void Draw(Vector3 viewPosition) const;
    void DrawUnlit(Vector3 viewPosition) const;

private:
    struct Crate
    {
        uint16_t id;
        Vector3 ground;
        float altitude;
    };
    struct Marker
    {
        Vector3 position;
        float timeLeft;
    };
    std::vector<Crate> crates;
    std::vector<Marker> markers;
    std::vector<Vector3> landed;
    float time = 0.0f;
};
