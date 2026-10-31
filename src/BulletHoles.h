#pragma once

#include "raylib.h"

#include <vector>

// Сумны ором: барилгын хана, машины их бие дээр үлдэх жижиг толбо (даалгаврын турш хадгалагдана).
struct BulletHole
{
    Vector3 position; // ертөнцийн (машин дээрх нь Vehicle-д машины орон нутгийн) байрлал
    Vector3 normal;   // гадаргуугийн чиглэл (гадагш)
    float size;       // диаметр, м
    float spin;       // эргэлт, радиан (бүгд ижил харагдахгүй)
    bool metal;       // төмөр (машин): будаг нь цуурч гялтганасан төмөр гарна; хана: тоосорхог цайвар цөм
};

class BulletHoles
{
public:
    // Хананд: хуучин нь хамгийн түрүүнд арилна (kMaxHoles)
    void Add(Vector3 position, Vector3 normal, bool metal);
    void Clear() { holes.clear(); next = 0; }
    // moving: энэ frame-ийн машин дээрх оромнууд (ертөнцийн байрлалаар, Garage::CollectHoles). light: орчны гэрэл.
    void Draw(const Camera3D& camera, Color light, const std::vector<BulletHole>& moving) const;
    void Load(); // текстур (цонх нээгдсэний дараа, зурахаас өмнө)
    void Unload();

    static BulletHole Make(Vector3 position, Vector3 normal, bool metal);

private:
    std::vector<BulletHole> holes;
    size_t next = 0;
    mutable Texture2D texture = {};
};
