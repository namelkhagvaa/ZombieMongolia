#pragma once

#include "World.h"
#include "raylib.h"

#include <cstdint>
#include <vector>

// Газрын зураг дээр тэмдэглэсэн газар руу хөтлөх зам (GPS шиг): засмал замыг илүүд үзэж, шороон замаар,
// эсвэл хээр талаар; барилга, саадаар нэвтрэхгүй. 8 м-ийн тор дээр A* хайлт.
// Зам нь газрын зураг, жижиг зураг дээр шугамаар, газар дээр сумаар, дэлгэцийн дээд хэсэгт чиглэлийн сумаар харагдана.
class Navigator
{
public:
    // Очих газар тэмдэглэнэ (тухайн даруйд нь зам тооцно).
    void SetTarget(const World& world, Vector3 from, Vector2 target);
    void Clear();
    bool Active() const { return active; }
    Vector2 Target() const { return target; }

    // Тоглогчийн байрлалаар замыг богиносгож, замаас хол холдвол дахин тооцно. Хүрвэл тэмдэглэгээ арилна.
    void Update(const World& world, Vector3 player, float dt);
    // Хүрсэн бол нэг удаа true (мэдэгдэл харуулахад)
    bool TakeArrived();

    // Тоглогчоос очих газар хүртэлх зам (x, z)
    const std::vector<Vector2>& Route() const { return route; }
    float RemainingDistance() const;

    // Газар дээрх сумнууд (BeginMode3D дотор)
    void Draw3D(const World& world, Vector3 player) const;
    // Дэлгэцийн дээд хэсэгт: дараагийн эргэлт рүү чиглэсэн сум ба үлдсэн зай
    void DrawHud(Vector3 player, float cameraYaw) const;

private:
    void BuildGrid(const World& world);
    bool Plan(const World& world, Vector2 from);
    int Cell(float v) const;

    bool active = false;
    bool arrived = false;
    Vector2 target = { 0.0f, 0.0f };
    std::vector<Vector2> route;
    float replanTimer = 0.0f;

    // Зардлын тор: 0 = нэвтрэхгүй, бусад нь алхамын жин (засмал 10, шороон 13, хээр 30, ус 90)
    static constexpr float kCell = 8.0f;
    std::vector<uint8_t> cost;
    int side = 0;
    float half = 0.0f;
    int gridProvince = -100;
    size_t gridObstacles = 0;
    Rectangle gridBounds = { 0, 0, 0, 0 };
};
