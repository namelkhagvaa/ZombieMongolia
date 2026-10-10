#include "Soldier.h"

#include "Humanoid.h"
#include "Zombie.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>

namespace
{
    const float kRadius = 0.4f;
    const float kHeight = 1.8f;
    const float kFollowDistance = 2.6f; // тоглогчоос байрлах зай
    const float kWalkSpeed = 6.0f;
    const float kCatchUpSpeed = 9.5f;
    const float kTeleportDistance = 45.0f;
    const float kFireRange = 20.0f;
    const float kFireInterval = 0.45f;
    const float kDamage = 14.0f;

    const float kFallTime = 0.6f;
    const float kStuckTime = 4.0f; // ийм удаан ахицгүй байвал тоглогчийн дэргэд шилжинэ

    HumanoidLook MakeLook()
    {
        HumanoidLook look;
        look.kind = HumanoidKind::Soldier;
        look.pants = { 74, 84, 56, 255 };
        look.boots = { 30, 28, 24, 255 };
        look.body = { 92, 108, 68, 255 }; // ногоон дүрэмт хувцас
        look.belt = { 60, 45, 30, 255 };
        look.hat = { 74, 86, 56, 255 };   // дуулга
        look.vest = { 62, 68, 48, 255 };
        look.skin = { 210, 164, 124, 255 };
        look.held = HeldItem::Rifle;
        return look;
    }
}

Vector3 SoldierSquad::SlotPosition(int index, Vector3 playerPosition) const
{
    const float angle = 2.0f * PI * (float)index / (float)std::max(1, Total()) + PI * 0.25f;
    return { playerPosition.x + std::sin(angle) * kFollowDistance, 0.0f,
             playerPosition.z + std::cos(angle) * kFollowDistance };
}

void SoldierSquad::Reset(int count, Vector3 around, const World& world)
{
    soldiers.assign(count, Soldier{});
    for (int i = 0; i < count; ++i)
    {
        Soldier& soldier = soldiers[i];
        soldier.health = kMaxHealth;
        soldier.mover.position = SlotPosition(i, around);
        soldier.mover.detourSign = (i % 2 == 0) ? 1.0f : -1.0f;
        world.ResolveHorizontal(soldier.mover.position, kRadius, kHeight, 0.0f);
        soldier.mover.position.y = world.GroundHeight(soldier.mover.position.x, soldier.mover.position.z);
    }
}

void SoldierSquad::Add(Vector3 position, float facing)
{
    Soldier soldier;
    soldier.health = kMaxHealth;
    soldier.mover.position = position;
    soldier.mover.facing = facing;
    soldier.mover.detourSign = (soldiers.size() % 2 == 0) ? 1.0f : -1.0f;
    soldiers.push_back(soldier);
}

void SoldierSquad::Update(float dt, World& world, Vector3 playerPosition, ZombieHorde& horde,
                          Tracers& tracers, Particles& particles)
{
    // Тоглогч аль барилгын дотор байгаа (-1 = гадаа)
    const int playerRoom = world.InteriorAt(playerPosition.x, playerPosition.z);

    for (int i = 0; i < Total(); ++i)
    {
        Soldier& soldier = soldiers[i];
        if (!soldier.alive)
        {
            soldier.deathTimer += dt;
            continue;
        }

        soldier.recoil = std::max(0.0f, soldier.recoil - dt * 9.0f);
        Vector3& pos = soldier.mover.position;

        // --- Тоглогчийг дагах ---
        Vector3 slot = SlotPosition(i, playerPosition);
        const int ownRoom = world.InteriorAt(pos.x, pos.z);

        if (playerRoom >= 0)
        {
            // Тоглогч барилга дотор: байраа өрөөнд багтаана (хана нэвт зогсохгүй).
            const Interior& room = world.GetInterior(playerRoom);
            for (int shrink = 0; shrink < 3 && !room.Contains(slot.x, slot.z); ++shrink)
            {
                slot.x = (slot.x + playerPosition.x) * 0.5f;
                slot.z = (slot.z + playerPosition.z) * 0.5f;
            }
        }

        // Тоглогч ба цэрэг өөр өөр "өрөөнд" байвал хаалгаар дамжина: эхлээд өөрийн талын хаалганы цэг,
        // дараа нь нөгөө талынх. Хаалга хаалттай бол цэрэг өөрөө онгойлгоно.
        Vector3 target = slot;
        if (ownRoom != playerRoom)
        {
            if (soldier.routeFrom != ownRoom || soldier.routeTo != playerRoom)
            {
                soldier.routeFrom = ownRoom;
                soldier.routeTo = playerRoom;
                soldier.routeStage = 0;
            }

            // Барилга дотроос гарах нь эхэлж, дараа нь (хэрэгтэй бол) өөр барилга руу орно.
            const bool leaving = ownRoom >= 0;
            const Interior& room = world.GetInterior(leaving ? ownRoom : playerRoom);
            const Vector2 first = leaving ? room.doorInside : room.doorOutside;
            const Vector2 second = leaving ? room.doorOutside : room.doorInside;

            const Vector2 waypoint = soldier.routeStage == 0 ? first : second;
            target = { waypoint.x, pos.y, waypoint.y };

            const float toWaypoint = HorizontalDistance(pos, target);
            if (toWaypoint < 2.2f && !world.IsDoorOpen(room.door))
            {
                world.SetDoorOpen(room.door, true);
            }
            if (soldier.routeStage == 0 && toWaypoint < 0.85f)
            {
                soldier.routeStage = 1;
            }
        }
        else
        {
            soldier.routeFrom = soldier.routeTo = -2; // зам дуусав
        }

        const float toSlot = HorizontalDistance(pos, slot);
        const bool onRoute = ownRoom != playerRoom;
        soldier.moving = onRoute || toSlot > 1.2f;

        // Гацсан эсэх: зорилтдоо ойртохгүй хэдэн секунд болбол (хашаа, хана тойрч чадахгүй)
        // тоглогчийн дэргэдэх байрандаа шилжинэ. Цэргүүд жинхэнэ зам хайдаггүй тул энэ нь баталгаа болно.
        const float remaining = HorizontalDistance(pos, target);
        if (!soldier.moving || remaining < soldier.bestDistance - 0.4f || remaining > soldier.bestDistance + 6.0f)
        {
            soldier.bestDistance = remaining; // ахиц гарсан (эсвэл зорилт өөрчлөгдсөн)
            soldier.stuckTimer = 0.0f;
        }
        else
        {
            soldier.stuckTimer += dt;
        }
        const bool stuck = soldier.stuckTimer > kStuckTime;

        if (toSlot > kTeleportDistance || stuck)
        {
            soldier.stuckTimer = 0.0f;
            soldier.bestDistance = 0.0f;
            soldier.routeFrom = soldier.routeTo = -2;
            pos = slot;
            pos.y = playerPosition.y;
            world.ResolveHorizontal(pos, kRadius, kHeight, 0.0f);
        }
        else if (soldier.moving)
        {
            const float speed = toSlot > 8.0f ? kCatchUpSpeed : kWalkSpeed;
            MoveToward(soldier.mover, target, speed, dt, world, kRadius, kHeight, 0.0f);
            soldier.walkCycle += dt * (4.5f + speed * 0.9f);
            soldier.moveAmount = speed / kWalkSpeed;
        }

        // Газрын гадаргууг дагана
        pos.y = world.SurfaceHeightAt(pos.x, pos.z, pos.y + 0.5f);

        // --- Ойрын, харагдаж буй зомбийг буудах ---
        soldier.fireCooldown -= dt;
        const int targetIndex = horde.Nearest(pos, kFireRange);
        if (targetIndex < 0)
        {
            continue;
        }

        const Vector3 muzzle = HumanoidLocalToWorld(pos, soldier.mover.facing, kHumanoidMuzzle);
        const Vector3 chest = horde.At(targetIndex).Chest();
        const Vector3 toTarget = Vector3Subtract(chest, muzzle);
        const float distance = Vector3Length(toTarget);
        if (distance < 0.01f)
        {
            continue;
        }

        if (!soldier.moving)
        {
            soldier.mover.facing = std::atan2(chest.x - pos.x, chest.z - pos.z);
        }

        if (soldier.fireCooldown <= 0.0f)
        {
            const Ray ray = { muzzle, Vector3Scale(toTarget, 1.0f / distance) };
            const bool clearShot = world.RayDistance(ray, distance) >= distance - 0.05f;
            if (clearShot)
            {
                soldier.fireCooldown = kFireInterval;
                soldier.recoil = 1.0f;
                horde.Damage(targetIndex, kDamage);
                tracers.Add(muzzle, chest, ORANGE);
                particles.Flash(muzzle, ray.direction);
                particles.Blood(chest, Vector3Negate(ray.direction), 4);
            }
        }
    }
}

void SoldierSquad::Damage(int index, float amount)
{
    Soldier& soldier = soldiers[index];
    soldier.health -= amount;
    if (soldier.health <= 0.0f)
    {
        soldier.health = 0.0f;
        soldier.alive = false;
    }
}

int SoldierSquad::AliveCount() const
{
    return (int)std::count_if(soldiers.begin(), soldiers.end(), [](const Soldier& s) { return s.alive; });
}

void SoldierSquad::Draw(Vector3 viewPosition, bool showColliders) const
{
    for (const Soldier& soldier : soldiers)
    {
        static const HumanoidLook look = MakeLook();
        const bool detailed = HorizontalDistance(viewPosition, soldier.mover.position) < kHumanoidDetailDistance;
        HumanoidPose pose = MakeSoldierPose(soldier.walkCycle, soldier.moving ? soldier.moveAmount : 0.0f, true,
                                            (float)GetTime() + soldier.walkCycle, soldier.recoil);

        if (!soldier.alive)
        {
            // Унасан цэрэг даалгавар дуустал газар хэвтэнэ.
            const float t = std::min(1.0f, soldier.deathTimer / kFallTime);
            pose.fall = t * t;
            pose.knee[0] = 30.0f * std::sin(t * PI) + 10.0f * t;
            DrawHumanoid(soldier.mover.position, soldier.mover.facing, 1.0f, pose, look, detailed);
            continue;
        }

        DrawBlobShadow(soldier.mover.position, kRadius * 1.3f);
        DrawHumanoid(soldier.mover.position, soldier.mover.facing, 1.0f, pose, look, detailed);

        // Амины зурвас: толгой дээрх жижиг шоо (амь багасах тусам богиносно)
        const float ratio = soldier.health / kMaxHealth;
        const Vector3 barPos = { soldier.mover.position.x, soldier.mover.position.y + 2.25f,
                                 soldier.mover.position.z };
        DrawCubeV(barPos, { 0.7f * ratio, 0.07f, 0.07f }, ratio > 0.4f ? GREEN : RED);

        if (showColliders)
        {
            DrawCylinderWires(soldier.mover.position, kRadius, kRadius, kHeight, 10, SKYBLUE);
        }
    }
}
