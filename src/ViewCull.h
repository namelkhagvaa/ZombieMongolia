#pragma once

#include "raylib.h"
#include "raymath.h"

// Камерын харах конус: камерын ард (харагдах өнцгөөс гадна) байгаа дүрс, унааг зурахгүй. Зөвхөн үндсэн зурлагад
// (Begin ... End хооронд) идэвхтэй: сүүдрийн зурлагад ард байгаа зүйлийн сүүдэр ч харагдаж болох тул бүгдийг зурна.
namespace ViewCull
{
    inline bool active = false;
    inline Vector3 eye = { 0.0f, 0.0f, 0.0f }, forward = { 0.0f, 0.0f, 1.0f };

    inline void Begin(const Camera3D& camera)
    {
        eye = camera.position;
        forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
        active = true;
    }
    inline void End() { active = false; }

    // position: дүрсийн хөл (газар), radius: хэмжээ (м). Ойрын, эсвэл ~70 градусын конус дотор байвал харагдана.
    inline bool Visible(Vector3 position, float radius)
    {
        if (!active) return true;
        const Vector3 d = { position.x - eye.x, position.y + 1.0f - eye.y, position.z - eye.z };
        const float distance = Vector3Length(d);
        if (distance < radius + 4.0f) return true;
        return Vector3DotProduct(d, forward) > distance * 0.3f - radius;
    }
}
