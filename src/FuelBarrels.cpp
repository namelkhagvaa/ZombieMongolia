#include "FuelBarrels.h"

#include "VehicleModel.h"

#include "Layout.h"
#include "Player.h"
#include "Procedural.h"
#include "Soldier.h"
#include "Zombie.h"
#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>

namespace
{
    const float kRadius = 0.33f;
    const float kHeight = 0.95f;
    const float kBlastRadius = 7.0f;       // дэлбэрэлтийн хүрээ
    const float kZombieMaxDamage = 1200.0f; // голд нь байгаа том зомбийг ч устгана
    const float kPlayerMaxDamage = 70.0f;
    const float kSoldierMaxDamage = 60.0f;
    const float kChainRadius = 5.5f;       // хөрш торхыг дэлбэлэх зай
    const float kBurnTime = 7.0f;
    const float kDrawDistance = 140.0f;

    const Color kPaint = { 176, 34, 30, 255 };
    const Color kBand = { 236, 196, 40, 255 };
    const Color kSteel = { 60, 58, 56, 255 };
}

void FuelBarrels::Spawn(const World& world, int seed)
{
    barrels.clear();
    exploded.clear();
    Rng rng((uint32_t)(9100 + seed * 131));

    auto tryPlace = [&](float x, float z) {
        if (!world.InsideBoundary(x, z) || world.InteriorAt(x, z) >= 0) return false;
        Vector3 free = { x, 0.0f, z };
        world.ResolveHorizontal(free, kRadius + 0.15f, kHeight, 0.0f);
        if (HorizontalDistance(free, { x, 0.0f, z }) > 0.05f) return false; // хана, хашаа, машинд давхцана
        for (const Barrel& other : barrels)
        {
            if (HorizontalDistance(other.position, { x, 0.0f, z }) < 0.75f) return false;
        }
        Barrel barrel;
        barrel.position = { x, world.GroundHeight(x, z), z };
        barrel.tilt = rng.Range(-4.0f, 4.0f);
        barrels.push_back(barrel);
        return true;
    };
    // Бөөгнөрөл: нэг цэгийн орчимд 2-4 торх
    auto cluster = [&](float x, float z, int count) {
        for (int i = 0, placed = 0; i < count * 6 && placed < count; ++i)
        {
            placed += tryPlace(x + rng.Range(-1.6f, 1.6f), z + rng.Range(-1.6f, 1.6f));
        }
    };

    // Шатахуун түгээх газар, застав, агуулахын орчим (даалгаврын цэгүүдээс зайтай)
    cluster(25.0f, -15.5f, 3);
    cluster(48.0f, -18.5f, 3);
    cluster(-40.0f, -11.0f, 2);
    cluster(-61.0f, 0.5f, 2);
    cluster(19.0f, 63.0f, 3);

    // Замын хажуу, суурин орчим: санамсаргүй цэгүүдээс замд ойрыг (4-28 м) сонгоно
    const Layout& layout = world.GetLayout();
    int placed = 0;
    for (int attempt = 0; attempt < 900 && placed < 46; ++attempt)
    {
        const float x = rng.Range(-420.0f, 420.0f), z = rng.Range(-420.0f, 420.0f);
        const float road = layout.RoadDistance(x, z);
        if (road < 4.0f || road > 28.0f) continue;
        const int before = (int)barrels.size();
        cluster(x, z, rng.Int(1, 3));
        placed += (int)barrels.size() > before;
    }
}

int FuelBarrels::Raycast(Ray ray, float minDistance, float maxDistance, float& distance) const
{
    int best = -1;
    distance = maxDistance;
    for (int i = 0; i < (int)barrels.size(); ++i)
    {
        const Barrel& barrel = barrels[(size_t)i];
        if (barrel.exploded) continue;
        const Vector3 p = barrel.position;
        const BoundingBox box = { { p.x - kRadius, p.y, p.z - kRadius }, { p.x + kRadius, p.y + kHeight, p.z + kRadius } };
        const RayCollision hit = GetRayCollisionBox(ray, box);
        if (hit.hit && hit.distance >= minDistance && hit.distance < distance)
        {
            distance = hit.distance;
            best = i;
        }
    }
    return best;
}

void FuelBarrels::Damage(int index, float amount)
{
    if (index < 0 || index >= (int)barrels.size()) return;
    Barrel& barrel = barrels[(size_t)index];
    if (barrel.exploded) return;
    barrel.health -= amount;
    if (barrel.health <= 0.0f && barrel.fuse < 0.0f)
    {
        barrel.fuse = 0.05f; // бараг шууд
    }
}

void FuelBarrels::Blast(Vector3 center, float radius)
{
    for (Barrel& barrel : barrels)
    {
        if (barrel.exploded || barrel.fuse >= 0.0f) continue;
        if (Vector3Distance(center, { barrel.position.x, barrel.position.y + 0.5f, barrel.position.z }) < radius + kRadius)
        {
            barrel.fuse = 0.1f + 0.05f * (float)GetRandomValue(0, 4);
        }
    }
}

void FuelBarrels::Ram(Vector3 center, float radius, float speed)
{
    if (speed < 4.0f) return;
    for (Barrel& barrel : barrels)
    {
        if (!barrel.exploded && barrel.fuse < 0.0f && HorizontalDistance(center, barrel.position) < radius + kRadius)
        {
            barrel.fuse = 0.0f;
        }
    }
}

void FuelBarrels::PushOut(Vector3& position, float radius) const
{
    for (const Barrel& barrel : barrels)
    {
        if (barrel.exploded) continue;
        if (position.y > barrel.position.y + kHeight - 0.1f) continue; // дээр нь зогсож байна
        const float dx = position.x - barrel.position.x, dz = position.z - barrel.position.z;
        const float distance = std::sqrt(dx * dx + dz * dz);
        const float minimum = radius + kRadius;
        if (distance < minimum && distance > 0.0001f)
        {
            position.x = barrel.position.x + dx / distance * minimum;
            position.z = barrel.position.z + dz / distance * minimum;
        }
    }
}

void FuelBarrels::Explode(Barrel& barrel, ZombieHorde& horde, Player& player, SoldierSquad& squad, Particles& particles)
{
    barrel.exploded = true;
    barrel.burn = kBurnTime;
    exploded.push_back((int)(&barrel - barrels.data()));
    const Vector3 center = { barrel.position.x, barrel.position.y + 0.5f, barrel.position.z };

    horde.Explode(center, kBlastRadius, kZombieMaxDamage);
    horde.AlertAround(center, 90.0f);

    const Vector3 chest = { player.Position().x, player.Position().y + 1.1f, player.Position().z };
    const float playerDistance = Vector3Distance(chest, center);
    if (playerDistance < kBlastRadius)
    {
        player.TakeDamage(kPlayerMaxDamage * (1.0f - playerDistance / kBlastRadius));
    }
    for (int i = 0; i < squad.Total(); ++i)
    {
        const Soldier& soldier = squad.All()[(size_t)i];
        if (!soldier.alive || soldier.riding >= 0) continue;
        const float distance = Vector3Distance({ soldier.mover.position.x, soldier.mover.position.y + 1.1f, soldier.mover.position.z }, center);
        if (distance < kBlastRadius)
        {
            squad.Damage(i, kSoldierMaxDamage * (1.0f - distance / kBlastRadius), "торхны дэлбэрэлт");
        }
    }

    // Том, галтай дэлбэрэлт: хоёр давхар, дээр нь хар утаа
    particles.Explosion(center);
    particles.Explosion(Vector3Add(center, { 0.0f, 1.2f, 0.0f }));
    for (int i = 0; i < 4; ++i)
    {
        particles.Flash(Vector3Add(center, { 0.0f, 0.4f * (float)i, 0.0f }), { 0.0f, 1.0f, 0.0f });
    }

    // Гинжин дэлбэрэлт
    for (Barrel& other : barrels)
    {
        if (&other == &barrel || other.exploded || other.fuse >= 0.0f) continue;
        const float distance = HorizontalDistance(other.position, barrel.position);
        if (distance < kChainRadius)
        {
            other.fuse = 0.15f + distance * 0.06f;
        }
    }
}

void FuelBarrels::Update(float dt, ZombieHorde& horde, Player& player, SoldierSquad& squad, Particles& particles)
{
    for (Barrel& barrel : barrels)
    {
        if (barrel.exploded)
        {
            if (barrel.burn > 0.0f)
            {
                // Шатаж буй гал: тасралтгүй гарах жижиг дөл (сүүлдээ багасна), дээрээс нь бараан утаа
                barrel.burn -= dt;
                const float strength = std::min(1.0f, barrel.burn / 2.0f);
                barrel.flame -= dt;
                while (barrel.flame <= 0.0f)
                {
                    barrel.flame += 0.035f;
                    const float angle = (float)GetRandomValue(0, 628) / 100.0f, r = (float)GetRandomValue(0, 20) / 100.0f;
                    const Vector3 at = { barrel.position.x + std::cos(angle) * r, barrel.position.y + 0.3f, barrel.position.z + std::sin(angle) * r };
                    particles.Flame(at, { std::cos(angle) * 0.3f, 0.6f + (float)GetRandomValue(0, 60) / 100.0f, std::sin(angle) * 0.3f },
                                    (0.2f + (float)GetRandomValue(0, 15) / 100.0f) * (0.4f + 0.6f * strength),
                                    0.4f + (float)GetRandomValue(0, 30) / 100.0f);
                }
                barrel.smoke -= dt;
                if (barrel.smoke <= 0.0f)
                {
                    barrel.smoke = 0.3f;
                    const unsigned char grey = (unsigned char)GetRandomValue(30, 55);
                    particles.Smoke({ barrel.position.x, barrel.position.y + 1.3f, barrel.position.z }, 0.5f + 0.3f * strength,
                                    { grey, grey, grey, 255 });
                }
            }
            continue;
        }
        if (barrel.fuse >= 0.0f)
        {
            barrel.fuse -= dt;
            if (barrel.fuse <= 0.0f)
            {
                Explode(barrel, horde, player, squad, particles);
            }
        }
    }
}

void FuelBarrels::Draw(Vector3 viewPosition) const
{
    for (const Barrel& barrel : barrels)
    {
        if (HorizontalDistance(viewPosition, barrel.position) > kDrawDistance) continue;
        const Vector3 p = barrel.position;
        if (barrel.exploded)
        {
            // Хагарсан, хар болсон үлдэгдэл, доороо түлэгдсэн газар
            DrawCylinder({ p.x, p.y + 0.01f, p.z }, 1.4f, 1.4f, 0.02f, 10, { 30, 26, 24, 255 });
            DrawCylinderEx({ p.x, p.y, p.z }, { p.x + 0.05f, p.y + 0.32f, p.z }, kRadius, kRadius * 1.15f, 8, { 40, 36, 34, 255 });
            continue;
        }
        if (HasSceneryModel(SceneryModelId::FuelBarrel) && HorizontalDistance(viewPosition, p) < 70.0f)
        {
            // Текстуртай загвар (assets/pooshig/barrel_fuel): өндөр 1 м -> kHeight
            rlPushMatrix();
            rlTranslatef(p.x, p.y, p.z);
            rlRotatef(barrel.tilt, 0.0f, 0.0f, 1.0f);
            rlRotatef(p.x * 37.0f + p.z * 11.0f, 0.0f, 1.0f, 0.0f); // тэмдэг нь өөр өөр зүг рүү
            rlScalef(kHeight, kHeight, kHeight);
            DrawSceneryModel(SceneryModelId::FuelBarrel);
            rlPopMatrix();
            continue;
        }
        rlPushMatrix();
        rlTranslatef(p.x, p.y, p.z);
        rlRotatef(barrel.tilt, 0.0f, 0.0f, 1.0f);
        // Улаан их бие, хоёр хавирга, шар тууз, дээд таг, бөглөө
        DrawCylinderEx({ 0.0f, 0.0f, 0.0f }, { 0.0f, kHeight, 0.0f }, kRadius, kRadius, 12, kPaint);
        for (float y : { 0.3f, 0.64f })
        {
            DrawCylinderEx({ 0.0f, y, 0.0f }, { 0.0f, y + 0.04f, 0.0f }, kRadius + 0.015f, kRadius + 0.015f, 12, Fade(kPaint, 1.0f));
        }
        DrawCylinderEx({ 0.0f, 0.42f, 0.0f }, { 0.0f, 0.56f, 0.0f }, kRadius + 0.008f, kRadius + 0.008f, 12, kBand);
        DrawCylinderEx({ 0.0f, kHeight, 0.0f }, { 0.0f, kHeight + 0.02f, 0.0f }, kRadius + 0.01f, kRadius - 0.02f, 12, kSteel);
        DrawCylinderEx({ 0.18f, kHeight, 0.0f }, { 0.18f, kHeight + 0.05f, 0.0f }, 0.04f, 0.04f, 6, kSteel);
        // Галын тэмдэг (тууз дээр): хар гурвалжин дөл
        DrawCube({ 0.0f, 0.49f, kRadius + 0.012f }, 0.1f, 0.1f, 0.01f, { 24, 20, 18, 255 });
        rlPopMatrix();
    }
}

