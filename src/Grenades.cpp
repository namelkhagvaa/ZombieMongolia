#include "Grenades.h"

#include "Player.h"
#include "Weapon.h"
#include "Zombie.h"
#include "raymath.h"

#include <algorithm>

namespace
{
    const float kFuseTime = 2.2f;
    const float kGravity = 16.0f;
    const float kRadius = 0.07f;
    const float kPlayerMaxDamage = 45.0f; // өөрийн гранатад хэт ойр байвал
}

void Grenades::Throw(Vector3 from, Vector3 velocity, bool visual)
{
    Grenade grenade = { from, velocity, kFuseTime };
    grenade.visual = visual;
    grenades.push_back(grenade);
}

void Grenades::Launch(Vector3 from, Vector3 velocity, bool visual)
{
    Grenade rocket = { from, velocity, 4.0f }; // юунд ч тусахгүй бол 4 секундийн дараа агаарт дэлбэрнэ
    rocket.rocket = true;
    rocket.visual = visual;
    grenades.push_back(rocket);
}

void Grenades::Update(float dt, const World& world, ZombieHorde& horde, Player& player, Particles& particles)
{
    for (Grenade& grenade : grenades)
    {
        grenade.fuse -= dt;

        if (grenade.rocket)
        {
            // Пуужин: шулуун ниснэ. Хана, газар эсвэл зомбид тусвал тэр даруй дэлбэрнэ.
            const float speed = Vector3Length(grenade.velocity);
            const float step = speed * dt;
            const Ray ray = { grenade.position, Vector3Scale(grenade.velocity, 1.0f / std::max(speed, 0.01f)) };
            const float wall = world.RayDistance(ray, step + 0.3f);
            const ZombieHit hit = horde.Raycast(ray, 0.0f, step + 0.8f);

            float travel = step;
            bool impact = false;
            if (hit.index >= 0)
            {
                travel = std::min(travel, hit.distance);
                impact = true;
            }
            if (wall < step + 0.3f)
            {
                travel = std::min(travel, std::max(0.0f, wall - 0.1f));
                impact = true;
            }
            grenade.position = Vector3Add(grenade.position, Vector3Scale(ray.direction, travel));

            grenade.trail -= dt;
            if (grenade.trail <= 0.0f)
            {
                grenade.trail = 0.03f;
                particles.Dust(grenade.position); // утааны мөр
            }

            if (impact || grenade.fuse <= 0.0f)
            {
                grenade.fuse = 0.0f;
                if (grenade.visual)
                {
                    particles.Explosion(grenade.position);
                    particles.Explosion(Vector3Add(grenade.position, { 0.0f, 0.8f, 0.0f }));
                    continue;
                }
                horde.Explode(grenade.position, kRocketRadius, kRocketMaxDamage);
                blasts.push_back({ grenade.position, kRocketRadius, kRocketMaxDamage });
                const Vector3 chest = { player.Position().x, player.Position().y + 1.1f, player.Position().z };
                const float distance = Vector3Distance(chest, grenade.position);
                if (distance < kRocketRadius)
                {
                    player.TakeDamage(kPlayerMaxDamage * 1.6f * (1.0f - distance / kRocketRadius));
                }
                particles.Explosion(grenade.position);
                particles.Explosion(Vector3Add(grenade.position, { 0.0f, 0.8f, 0.0f })); // том дэлбэрэлт
            }
            continue;
        }

        grenade.velocity.y -= kGravity * dt;

        // Хана, машин зэрэг саадыг мөргөвөл хэвтээ хурдаа алдаж ойно.
        const float speed = Vector3Length(grenade.velocity);
        const float step = speed * dt;
        if (speed > 0.01f)
        {
            const Ray ray = { grenade.position, Vector3Scale(grenade.velocity, 1.0f / speed) };
            const float ground = world.GroundHeight(grenade.position.x, grenade.position.z);
            const bool aboveGround = grenade.position.y > ground + 0.6f; // газарт буухыг хана гэж андуурахгүй
            if (aboveGround && world.RayDistance(ray, step + 0.1f) < step + 0.1f)
            {
                grenade.velocity.x *= -0.3f;
                grenade.velocity.z *= -0.3f;
            }
        }
        grenade.position = Vector3Add(grenade.position, Vector3Scale(grenade.velocity, dt));

        // Газарт ойх
        const float ground = world.GroundHeight(grenade.position.x, grenade.position.z) + kRadius;
        if (grenade.position.y < ground)
        {
            grenade.position.y = ground;
            grenade.velocity.y = grenade.velocity.y < -2.0f ? -grenade.velocity.y * 0.35f : 0.0f;
            grenade.velocity.x *= 0.6f;
            grenade.velocity.z *= 0.6f;
        }

        if (grenade.fuse <= 0.0f && grenade.visual)
        {
            particles.Explosion(grenade.position);
        }
        else if (grenade.fuse <= 0.0f)
        {
            horde.Explode(grenade.position, kGrenadeRadius, kGrenadeMaxDamage);
            blasts.push_back({ grenade.position, kGrenadeRadius, kGrenadeMaxDamage });

            const Vector3 chest = { player.Position().x, player.Position().y + 1.1f, player.Position().z };
            const float distance = Vector3Distance(chest, grenade.position);
            if (distance < kGrenadeRadius)
            {
                player.TakeDamage(kPlayerMaxDamage * (1.0f - distance / kGrenadeRadius));
            }

            particles.Explosion(grenade.position);
        }
    }

    grenades.erase(std::remove_if(grenades.begin(), grenades.end(), [](const Grenade& g) { return g.fuse <= 0.0f; }),
                   grenades.end());
}

void Grenades::Draw() const
{
    for (const Grenade& grenade : grenades)
    {
        if (grenade.rocket)
        {
            // Пуужин: шовх хошуу, их бие, ард нь гал
            const Vector3 direction = Vector3Normalize(grenade.velocity);
            const Vector3 tail = Vector3Subtract(grenade.position, Vector3Scale(direction, 0.55f));
            DrawCylinderEx(tail, grenade.position, 0.05f, 0.05f, 6, { 70, 84, 60, 255 });
            DrawCylinderEx(grenade.position, Vector3Add(grenade.position, Vector3Scale(direction, 0.22f)), 0.075f, 0.0f, 6,
                           { 96, 104, 70, 255 });
            DrawCylinderEx(Vector3Subtract(tail, Vector3Scale(direction, 0.35f)), tail, 0.0f, 0.07f, 6, { 255, 190, 70, 255 });
            continue;
        }
        DrawSphereEx(grenade.position, kRadius, 4, 6, { 62, 78, 48, 255 });
    }
}
