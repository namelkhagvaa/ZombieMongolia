#include "Soldier.h"

#include "Humanoid.h"
#include "Team.h"
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

    // Багийн гишүүд жирийн цэргээс илүү: дайчин хурдан бууддаг, мэргэн буудагч холоос ховор боловч хүчтэй.
    const float kFighterRange = 26.0f, kFighterInterval = 0.28f, kFighterDamage = 18.0f;
    const float kSniperRange = 70.0f, kSniperInterval = 1.4f, kSniperDamage = 150.0f;
    const float kGuardRadius = 16.0f; // удирдаж буй гишүүнд ийм ойртсон зомбийг түрүүлж буудна

    // Багийн гишүүд удирдаж буй гишүүнээс нэлээн зайтай явж, зомби ойртоход өөрсдөө байр сонгон байлдана.
    const float kTeamSpacing = 9.0f;      // дагахдаа барих зай
    const float kTeamStartFollow = 6.5f;  // байрнаасаа ийм холдсон үед л хөдөлж эхэлнэ
    const float kTeamLeash = 42.0f;       // удирдаж буй гишүүнээс ийм холдвол тулаанаа орхиж гүйцнэ
    const float kFighterEngage = 30.0f, kFighterNear = 6.5f, kFighterFar = 17.0f;
    const float kSniperEngage = 62.0f, kSniperNear = 17.0f, kSniperFar = 50.0f;

    float Random01()
    {
        return (float)GetRandomValue(0, 1000) / 1000.0f;
    }

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

Vector3 SoldierSquad::SlotPosition(int index, Vector3 playerPosition, float radius) const
{
    const float angle = 2.0f * PI * (float)index / (float)std::max(1, Total()) + PI * 0.25f;
    return { playerPosition.x + std::sin(angle) * radius, 0.0f, playerPosition.z + std::cos(angle) * radius };
}

void SoldierSquad::Reset(int count, Vector3 around, const World& world)
{
    soldiers.assign(count, Soldier{});
    for (int i = 0; i < count; ++i)
    {
        Soldier& soldier = soldiers[i];
        soldier.health = kMaxHealth;
        soldier.mover.position = SlotPosition(i, around, kFollowDistance);
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

void SoldierSquad::AddMember(int member, Vector3 position, float facing, float health, float maxHealth)
{
    Add(position, facing);
    Soldier& soldier = soldiers.back();
    soldier.member = member;
    soldier.health = health;
    soldier.maxHealth = maxHealth;
}

int SoldierSquad::FindMember(int member) const
{
    for (int i = 0; i < Total(); ++i)
    {
        if (soldiers[i].member == member) return i;
    }
    return -1;
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
        soldier.aimHold -= dt;
        soldier.aimPose += ((soldier.aimHold > 0.0f ? 1.0f : 0.0f) - soldier.aimPose) * std::min(1.0f, 8.0f * dt);
        Vector3& pos = soldier.mover.position;

        const bool teammate = soldier.member >= 0;
        const bool sniper = IsSniperMember(soldier.member);
        const int ownRoom = world.InteriorAt(pos.x, pos.z);

        // --- Багийн гишүүн: зомби ойртвол тоглогчийг дагахаа больж, өөрөө байр сонгон байлдана ---
        int enemyIndex = -1;
        const bool mounted = soldier.riding >= 0 || soldier.boarding;
        if (teammate && !mounted && HorizontalDistance(pos, playerPosition) < kTeamLeash)
        {
            enemyIndex = horde.Nearest(pos, sniper ? kSniperEngage : kFighterEngage);
        }
        soldier.fighting = enemyIndex >= 0;

        if (soldier.riding >= 0)
        {
            // Унаанд сууж яваа: байрлал, харах зүгийг Game унаанаас нь авч тавьдаг.
            soldier.moving = false;
            soldier.routeFrom = soldier.routeTo = -2;
            soldier.stuckTimer = 0.0f;
            soldier.bestDistance = 0.0f;
        }
        else if (soldier.boarding)
        {
            // Суух хэрэгсэл рүүгээ гүйнэ.
            MoveToward(soldier.mover, soldier.boardPoint, kCatchUpSpeed, dt, world, kRadius, kHeight, 0.0f);
            soldier.walkCycle += dt * (4.5f + kCatchUpSpeed * 0.9f);
            soldier.moveAmount = kCatchUpSpeed / kWalkSpeed;
            soldier.moving = true;
            soldier.routeFrom = soldier.routeTo = -2;
            soldier.stuckTimer = 0.0f;
            soldier.bestDistance = 0.0f;
        }
        else if (soldier.fighting)
        {
            const Vector3 enemy = horde.At(enemyIndex).mover.position;
            const float enemyDistance = HorizontalDistance(pos, enemy);
            const float nearLimit = sniper ? kSniperNear : kFighterNear;
            const float farLimit = sniper ? kSniperFar : kFighterFar;
            const Vector3 away = enemyDistance > 0.01f
                                     ? Vector3{ (pos.x - enemy.x) / enemyDistance, 0.0f, (pos.z - enemy.z) / enemyDistance }
                                     : Vector3{ 1.0f, 0.0f, 0.0f };
            const Vector3 lateral = { -away.z, 0.0f, away.x };

            Vector3 point = pos;
            float speed = kWalkSpeed;
            bool move = false;
            soldier.tacticTimer -= dt;

            if (enemyDistance < nearLimit)
            {
                // Хэт ойртсон: зайгаа авч ухарна (бага зэрэг хажуу тийш, нэг шугамаар гүйхгүй).
                point = { pos.x + away.x * 4.0f + lateral.x * soldier.mover.detourSign * 1.5f, pos.y,
                          pos.z + away.z * 4.0f + lateral.z * soldier.mover.detourSign * 1.5f };
                speed = kCatchUpSpeed;
                move = true;
                soldier.shifting = false;
            }
            else if (enemyDistance > farLimit)
            {
                // Хол байна: буудах зайд хүртэл ойртоно.
                point = enemy;
                move = true;
                soldier.shifting = false;
            }
            else
            {
                // Тохиромжтой зайд: зогсож буудна, хэдэн секунд тутам хажуу тийш шинэ байр сонгоно.
                if (soldier.tacticTimer <= 0.0f)
                {
                    soldier.tacticTimer = 1.6f + Random01() * 1.8f;
                    const float side = (Random01() < 0.5f ? -1.0f : 1.0f) * (2.5f + Random01() * 2.5f);
                    const float depth = Random01() * 3.0f - 1.5f;
                    soldier.tacticPoint = { pos.x + lateral.x * side + away.x * depth, pos.y,
                                            pos.z + lateral.z * side + away.z * depth };
                    soldier.shifting = Random01() < 0.75f;
                }
                if (soldier.shifting && HorizontalDistance(pos, soldier.tacticPoint) < 0.6f)
                {
                    soldier.shifting = false;
                }
                point = soldier.tacticPoint;
                move = soldier.shifting;
            }

            // Аймгийн хилээс гарахгүй.
            if (move && !world.InsideBoundary(point.x, point.z))
            {
                move = false;
                soldier.shifting = false;
            }

            soldier.moving = move;
            if (move)
            {
                MoveToward(soldier.mover, point, speed, dt, world, kRadius, kHeight, 0.0f);
                soldier.walkCycle += dt * (4.5f + speed * 0.9f);
                soldier.moveAmount = speed / kWalkSpeed;
            }

            // Дагах замын төлөвийг цэвэрлэнэ: тулаан дуусмагц шинээр эхэлнэ.
            soldier.routeFrom = soldier.routeTo = -2;
            soldier.stuckTimer = 0.0f;
            soldier.bestDistance = 0.0f;
        }
        else
        {
            // --- Тоглогчийг дагах ---
            // Багийн гишүүд нэлээн зайтай явж, байрнаасаа хангалттай холдсон үед л хөдөлнө.
            Vector3 slot = SlotPosition(i, playerPosition, teammate ? kTeamSpacing : kFollowDistance);

            if (playerRoom >= 0)
            {
                // Тоглогч барилга дотор: байраа өрөөнд багтаана (хана нэвт зогсохгүй).
                const Interior& room = world.GetInterior(playerRoom);
                for (int shrink = 0; shrink < 6 && !room.Contains(slot.x, slot.z); ++shrink)
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
            const float startDistance = teammate && playerRoom < 0 && !soldier.moving ? kTeamStartFollow : 1.2f;
            soldier.moving = onRoute || toSlot > startDistance;

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
        }

        // Газрын гадаргууг дагана
        if (soldier.riding < 0)
        {
            pos.y = world.SurfaceHeightAt(pos.x, pos.z, pos.y + 0.5f);
        }
        else if (soldier.seatStyle == 3)
        {
            continue; // бүхээг дотроос буудахгүй
        }

        // --- Ойрын, харагдаж буй зомбийг буудах ---
        soldier.fireCooldown -= dt;
        const float fireRange = sniper ? kSniperRange : teammate ? kFighterRange : kFireRange;
        const float fireInterval = sniper ? kSniperInterval : teammate ? kFighterInterval : kFireInterval;
        const float damage = sniper ? kSniperDamage : teammate ? kFighterDamage : kDamage;

        // Багийн гишүүд удирдаж буй гишүүнээ хамгаална: түүнд хамгийн ойр зомбийг түрүүлж буудна.
        int targetIndex = -1;
        if (teammate)
        {
            targetIndex = horde.Nearest(playerPosition, kGuardRadius);
            if (targetIndex >= 0 && HorizontalDistance(pos, horde.At(targetIndex).mover.position) > fireRange)
            {
                targetIndex = -1;
            }
        }
        if (targetIndex < 0)
        {
            targetIndex = horde.Nearest(pos, fireRange);
        }
        if (targetIndex < 0)
        {
            continue;
        }

        const Vector3 muzzle = HumanoidLocalToWorld(pos, soldier.mover.facing,
                                                    HumanoidMuzzleFor(sniper ? HeldItem::Sniper : HeldItem::Rifle));
        const Vector3 chest = horde.At(targetIndex).Chest();
        const Vector3 toTarget = Vector3Subtract(chest, muzzle);
        const float distance = Vector3Length(toTarget);
        if (distance < 0.01f)
        {
            continue;
        }

        // Байлдаж байхдаа хажуу тийш шилжих, ухрах үедээ ч бай руугаа харсан хэвээр байна.
        if (!soldier.moving || soldier.fighting)
        {
            soldier.mover.facing = std::atan2(chest.x - pos.x, chest.z - pos.z);
        }

        if (soldier.fireCooldown <= 0.0f)
        {
            const Ray ray = { muzzle, Vector3Scale(toTarget, 1.0f / distance) };
            const bool clearShot = world.RayDistance(ray, distance) >= distance - 0.05f;
            // Бай халхлагдсан хэвээр байвал удахгүй өөр байр сонгоно.
            soldier.blockedTime = clearShot ? 0.0f : soldier.blockedTime + dt;
            if (soldier.blockedTime > 0.7f)
            {
                soldier.blockedTime = 0.0f;
                soldier.tacticTimer = 0.0f;
            }
            if (clearShot)
            {
                soldier.fireCooldown = fireInterval;
                soldier.recoil = 1.0f;
                soldier.aimHold = 1.4f;
                horde.Damage(targetIndex, damage);
                horde.AlertAround(pos, 70.0f); // буун дуунд зомби сэрнэ
                tracers.Add(muzzle, chest, sniper ? RAYWHITE : ORANGE);
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
        static const HumanoidLook recruitLook = MakeLook();
        static const HumanoidLook memberLooks[kTeamSize] = { TeamMemberLook(0), TeamMemberLook(1), TeamMemberLook(2) };
        const HumanoidLook& look = soldier.member >= 0 ? memberLooks[soldier.member] : recruitLook;
        const bool detailed = HorizontalDistance(viewPosition, soldier.mover.position) < kHumanoidDetailDistance;
        HumanoidPose pose = MakeSoldierPose(soldier.walkCycle, soldier.moving ? soldier.moveAmount : 0.0f, true,
                                            (float)GetTime() + soldier.walkCycle, soldier.recoil, look.held);
        pose.aim = soldier.aimPose;

        if (soldier.alive && soldier.riding >= 0)
        {
            if (soldier.seatStyle == 3)
            {
                continue; // бүхээг дотор: харагдахгүй
            }
            // Суудлын хэлбэрээс хамаарсан хөл, их биеийн байрлал (гар нь буугаа барьсан хэвээр).
            pose = MakeSoldierPose(0.0f, 0.0f, true, (float)GetTime() + soldier.walkCycle, soldier.recoil, look.held);
            const bool saddle = soldier.seatStyle == 2, bed = soldier.seatStyle == 4;
            pose.hip[0] = pose.hip[1] = saddle ? 34.0f : bed ? 82.0f : 74.0f;
            pose.knee[0] = pose.knee[1] = saddle ? 46.0f : bed ? 60.0f : 84.0f;
            pose.legSpread = saddle ? 24.0f : bed ? 6.0f : 13.0f;
            pose.lean += saddle ? 4.0f : bed ? 0.0f : 14.0f;
            pose.bob = 0.0f;
            pose.seated = true;
            pose.aim = soldier.aimPose;
            DrawHumanoid({ soldier.seat.x, soldier.seat.y - 0.9f, soldier.seat.z }, soldier.mover.facing, 1.0f, pose, look,
                         detailed);
            continue;
        }

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
        const float ratio = soldier.health / soldier.maxHealth;
        const Vector3 barPos = { soldier.mover.position.x, soldier.mover.position.y + 2.25f,
                                 soldier.mover.position.z };
        DrawCubeV(barPos, { 0.7f * ratio, 0.07f, 0.07f }, ratio > 0.4f ? GREEN : RED);

        if (showColliders)
        {
            DrawCylinderWires(soldier.mover.position, kRadius, kRadius, kHeight, 10, SKYBLUE);
        }
    }
}
