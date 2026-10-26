#include "WorldEvents.h"

#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>

namespace
{
    const float kFallSpeed = 7.0f;       // м/с (шүхэртэй)
    const float kDrawDistance = 260.0f;
    const Color kWood = { 120, 86, 48, 255 }, kStrap = { 40, 46, 30, 255 };
    const Color kCanopyA = { 236, 120, 40, 255 }, kCanopyB = { 245, 240, 230, 255 };
}

void WorldEvents::Clear()
{
    crates.clear();
    markers.clear();
    landed.clear();
}

void WorldEvents::AddCrate(uint16_t id, Vector3 ground, float altitude)
{
    RemoveCrate(id);
    crates.push_back({ id, ground, std::max(0.0f, altitude) });
}

bool WorldEvents::RemoveCrate(uint16_t id)
{
    const size_t before = crates.size();
    crates.erase(std::remove_if(crates.begin(), crates.end(), [&](const Crate& c) { return c.id == id; }), crates.end());
    return crates.size() != before;
}

void WorldEvents::AddMarker(Vector3 position, float seconds)
{
    markers.push_back({ position, seconds });
}

void WorldEvents::Update(float dt)
{
    time += dt;
    for (Crate& crate : crates)
    {
        if (crate.altitude <= 0.0f) continue;
        crate.altitude = std::max(0.0f, crate.altitude - kFallSpeed * dt);
        if (crate.altitude <= 0.0f) landed.push_back(crate.ground);
    }
    for (Marker& marker : markers) marker.timeLeft -= dt;
    markers.erase(std::remove_if(markers.begin(), markers.end(), [](const Marker& m) { return m.timeLeft <= 0.0f; }), markers.end());
}

int WorldEvents::CrateNear(Vector3 position, float radius) const
{
    for (const Crate& crate : crates)
    {
        if (crate.altitude > 0.0f) continue;
        const float dx = crate.ground.x - position.x, dz = crate.ground.z - position.z;
        if (dx * dx + dz * dz < radius * radius && std::fabs(crate.ground.y - position.y) < 3.0f) return crate.id;
    }
    return -1;
}

std::vector<Vector3> WorldEvents::TakeLanded()
{
    std::vector<Vector3> out;
    out.swap(landed);
    return out;
}

void WorldEvents::Markers(std::vector<Vector3>& out) const
{
    for (const Crate& crate : crates) out.push_back(crate.ground);
    for (const Marker& marker : markers) out.push_back(marker.position);
}

void WorldEvents::Draw(Vector3 viewPosition) const
{
    for (const Crate& crate : crates)
    {
        const Vector3 at = { crate.ground.x, crate.ground.y + crate.altitude, crate.ground.z };
        if (Vector2Distance({ at.x, at.z }, { viewPosition.x, viewPosition.z }) > kDrawDistance) continue;
        const float sway = crate.altitude > 0.0f ? 6.0f * std::sin(time * 1.3f + (float)crate.id) : 0.0f;
        rlPushMatrix();
        rlTranslatef(at.x, at.y, at.z);
        rlRotatef(sway, 0.0f, 0.0f, 1.0f);
        // Модон хайрцаг, оосор
        DrawCube({ 0.0f, 0.45f, 0.0f }, 1.2f, 0.9f, 0.9f, kWood);
        DrawCubeWires({ 0.0f, 0.45f, 0.0f }, 1.2f, 0.9f, 0.9f, { 70, 50, 28, 255 });
        DrawCube({ 0.0f, 0.45f, 0.0f }, 1.22f, 0.12f, 0.92f, kStrap);
        DrawCube({ 0.0f, 0.45f, 0.0f }, 0.14f, 0.92f, 0.92f, kStrap);
        if (crate.altitude > 0.0f)
        {
            // Шүхэр: хавтгайрсан бөмбөрцөг (улбар шар, цагаан судал), олс
            const Vector3 canopy = { 0.0f, 6.5f, 0.0f };
            rlPushMatrix();
            rlTranslatef(canopy.x, canopy.y, canopy.z);
            rlScalef(1.0f, 0.45f, 1.0f);
            DrawSphereEx({ 0.0f, 0.0f, 0.0f }, 3.2f, 8, 12, kCanopyA);
            DrawSphereWires({ 0.0f, 0.0f, 0.0f }, 3.25f, 8, 6, kCanopyB);
            rlPopMatrix();
            for (const float x : { -0.6f, 0.6f })
            {
                for (const float z : { -0.45f, 0.45f })
                {
                    DrawLine3D({ x, 0.9f, z }, { x * 4.0f, canopy.y - 0.6f, z * 5.0f }, { 230, 230, 220, 255 });
                }
            }
        }
        rlPopMatrix();
    }
}

void WorldEvents::DrawUnlit(Vector3 viewPosition) const
{
    // Газардсан хайрцаг: алсаас харагдах ногоон утаа (гэрлийн багана)
    const float pulse = 0.5f + 0.5f * std::sin(time * 3.0f);
    for (const Crate& crate : crates)
    {
        if (crate.altitude > 0.0f) continue;
        const float distance = Vector2Distance({ crate.ground.x, crate.ground.z }, { viewPosition.x, viewPosition.z });
        const float width = std::clamp(distance * 0.012f, 0.35f, 4.0f);
        DrawCubeV({ crate.ground.x, crate.ground.y + 45.0f, crate.ground.z }, { width, 90.0f, width },
                  Fade({ 90, 230, 110, 255 }, 0.22f + 0.15f * pulse));
    }
}
