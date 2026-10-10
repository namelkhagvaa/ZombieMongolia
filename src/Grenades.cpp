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

void Grenades::Throw(Vector3 from, Vector3 velocity)
{
    grenades.push_back({ from, velocity, kFuseTime });
}

void Grenades::Update(float dt, const World& world, ZombieHorde& horde, Player& player, Particles& particles)
{
    for (Grenade& grenade : grenades)
    {
        grenade.fuse -= dt;
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

        if (grenade.fuse <= 0.0f)
        {
            horde.Explode(grenade.position, kGrenadeRadius, kGrenadeMaxDamage);

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
        DrawSphereEx(grenade.position, kRadius, 4, 6, { 62, 78, 48, 255 });
    }
}
