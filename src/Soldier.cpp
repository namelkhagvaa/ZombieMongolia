#include "Player.h"
#include "Navigation.h"
#include "ViewCull.h"
#include "Soldier.h"

#include "Audio.h"
#include "CharacterModel.h"
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
    const float kVaultTime = 1.0f; // хашаа давах хугацаа
    const float kFollowDistance = 2.6f; // тоглогчоос байрлах зай
    const float kWalkSpeed = 6.0f;
    const float kCatchUpSpeed = 9.5f;
    const float kSprintSpeed = 12.0f;    // хол хоцорсон үед: тоглогчийн гүйлтээс (9.6 м/с) хурдан тул заавал гүйцнэ
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

    // Элсүүлсэн цэргүүд: удирдагчийн эргэн тойронд харуул хийнэ (наалдаж дагахгүй).
    const float kGuardMin = 6.0f, kGuardMax = 12.0f; // харуулын байрны зай удирдагчаас
    const float kGuardRelocate = 10.0f;  // удирдагч байр сонгосон цэгээсээ ийм холдвол шинэ байр сонгоно
    const float kGuardStroll = 2.2f;     // байр солихдоо алхах хурд, м/с (ойрхон байвал)
    const float kRecruitEngage = 22.0f, kRecruitNear = 5.0f, kRecruitFar = 14.0f;
    const float kRecruitProtect = 24.0f; // удирдаж буй гишүүнд ийм ойртсон зомби руу элсүүлсэн цэрэг дайрна
    const float kRecruitLeash = 26.0f;   // удирдагчаас ийм холдвол тулаанаа орхино

    float Random01()
    {
        return (float)GetRandomValue(0, 1000) / 1000.0f;
    }

    const float kFallTime = 0.6f;
    const float kStuckTime = 4.0f; // ийм удаан ахицгүй байвал тойрох зүгээ сольж оролдоно

    // Хурдыг гэнэт солихгүй: алхаа / гүйлт / хурдан гүйлтийн босгон дээр (жишээ нь тоглогч 9.6 м/с гүйхэд
    // 25 м-ийн босго) хурд frame бүр солигдож, хөл чичрэх мэт харагддаг байсан.
    float EaseSpeed(Soldier& soldier, float target, float dt)
    {
        soldier.speed += (target - soldier.speed) * std::min(1.0f, 4.0f * dt);
        return soldier.speed;
    }

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
        look.model = kCharacterFighter1; // assetsighter1 загвар (байхгүй бол кодоор үүсгэсэн дүрс)
        return look;
    }
}

Vector3 SoldierSquad::SlotPosition(int index, Vector3 playerPosition, float radius) const
{
    const float angle = 2.0f * PI * (float)index / (float)std::max(1, Total()) + PI * 0.25f;
    return { playerPosition.x + std::sin(angle) * radius, 0.0f, playerPosition.z + std::cos(angle) * radius };
}

void SoldierSquad::ChooseGuardPost(int index, Vector3 playerPosition, const World& world)
{
    // Цэрэг бүр өөрийн хэсгийг (тойргийн тэнцүү хэсэг) хамгаална: хамт бөөгнөрөхгүй, бүх талыг ажиглана.
    Soldier& soldier = soldiers[(size_t)index];
    const float sector = 2.0f * PI / (float)std::max(1, Total());
    soldier.guardAnchor = playerPosition;
    soldier.guardTimer = 7.0f + 7.0f * Random01();
    // Чөлөөтэй байр олдохгүй бол (давчуу газар) удирдагчийн ойрхон байрандаа зогсоно.
    soldier.guardPost = SlotPosition(index, playerPosition, kFollowDistance);
    soldier.guardWatch = std::atan2(soldier.guardPost.x - playerPosition.x, soldier.guardPost.z - playerPosition.z);
    for (int attempt = 0; attempt < 6; ++attempt)
    {
        const float angle = sector * ((float)index + 0.2f + 0.6f * Random01()) + PI * 0.25f;
        const float radius = kGuardMin + (kGuardMax - kGuardMin) * Random01();
        Vector3 post = { playerPosition.x + std::sin(angle) * radius, playerPosition.y,
                         playerPosition.z + std::cos(angle) * radius };
        if (!world.InsideBoundary(post.x, post.z))
        {
            continue;
        }
        Vector3 free = post;
        world.ResolveHorizontal(free, kRadius, kHeight, 0.3f);
        if (HorizontalDistance(free, post) > 0.5f || world.InteriorAt(free.x, free.z) >= 0)
        {
            continue; // хана, хашаа, барилга дотор биш
        }
        soldier.guardPost = free;
        soldier.guardWatch = angle; // удирдагчаас гадагш ажиглана
        break;
    }
}

void SoldierSquad::Reset(int count, Vector3 around, const World& world)
{
    soldiers.assign(count, Soldier{});
    for (int i = 0; i < count; ++i)
    {
        Soldier& soldier = soldiers[i];
        soldier.id = NewId();
        soldier.health = kMaxHealth;
        soldier.bought = true; // даалгаврын эхний цэргүүд = Gold-оор авсан
        soldier.mover.position = SlotPosition(i, around, kFollowDistance);
        soldier.mover.detourSign = (i % 2 == 0) ? 1.0f : -1.0f;
        world.ResolveHorizontal(soldier.mover.position, kRadius, kHeight, 0.0f);
        soldier.mover.position.y = world.GroundHeight(soldier.mover.position.x, soldier.mover.position.z);
    }
}

void SoldierSquad::Add(Vector3 position, float facing)
{
    Soldier soldier;
    soldier.id = NewId();
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
        if (soldiers[i].member == member && !soldiers[i].guestTeam) return i;
    }
    return -1;
}

void SoldierSquad::AddGuestTeam(int controlledMember, Vector3 around, const World& world, float health)
{
    // Зөвхөн үндсэн гишүүдийг солино: зочны Gold-оор авсан, элсүүлсэн цэргүүд (member < 0) хэвээр
    soldiers.erase(std::remove_if(soldiers.begin(), soldiers.end(), [](const Soldier& s) { return s.guestTeam && s.member >= 0; }),
                   soldiers.end());
    for (int member = 0; member < kTeamSize; ++member)
    {
        if (member == controlledMember) continue;
        Vector3 position = { around.x + (member == kSniperMember ? 3.5f : -3.5f), around.y, around.z - 3.0f };
        world.ResolveHorizontal(position, kRadius, kHeight, 0.0f);
        position.y = world.GroundHeight(position.x, position.z);
        Add(position, 0.0f);
        Soldier& soldier = soldiers.back();
        soldier.member = member;
        soldier.guestTeam = true;
        soldier.health = soldier.maxHealth = health;
    }
}

void SoldierSquad::RemoveGuestTeam()
{
    soldiers.erase(std::remove_if(soldiers.begin(), soldiers.end(), [](const Soldier& s) { return s.guestTeam; }), soldiers.end());
    guestLeaderActive = false;
}

int SoldierSquad::FindById(uint16_t id) const
{
    if (id == 0) return -1;
    for (int i = 0; i < Total(); ++i)
    {
        if (soldiers[i].id == id) return i;
    }
    return -1;
}

bool SoldierSquad::HasGuestTeam() const
{
    return std::any_of(soldiers.begin(), soldiers.end(), [](const Soldier& s) { return s.guestTeam; });
}

bool SoldierSquad::HasGuestMembers() const
{
    return std::any_of(soldiers.begin(), soldiers.end(), [](const Soldier& s) { return s.guestTeam && s.member >= 0; });
}

void SoldierSquad::Update(float dt, World& world, Vector3 hostPosition, ZombieHorde& horde,
                          Tracers& tracers, Particles& particles)
{
    // Хостын тоглогч ба (хамтарсан тоглолтод) зочин аль барилгын дотор байгаа (-1 = гадаа)
    const int hostRoom = world.InteriorAt(hostPosition.x, hostPosition.z);
    focusTimer = std::max(0.0f, focusTimer - dt);
    UpdateRevives(dt, hostPosition);
    const int guestRoom = guestLeaderActive ? world.InteriorAt(guestLeader.x, guestLeader.z) : -1;

    for (int i = 0; i < Total(); ++i)
    {
        Soldier& soldier = soldiers[i];
        soldier.sinceHit += dt;
        // Дагах удирдагч: хостын тоглогч, эсвэл зочны багийнхны хувьд зочин
        const bool followGuest = soldier.guestTeam && guestLeaderActive;
        const Vector3 playerPosition = followGuest ? guestLeader : hostPosition;
        const int playerRoom = followGuest ? guestRoom : hostRoom;
        if (!soldier.alive)
        {
            soldier.deathTimer += dt;
            continue;
        }

        if (soldier.frozen > 0.0f)
        {
            soldier.frozen -= dt; // тасарсан тоглогч: AI нэг минутын дараа удирдана
            soldier.moving = false;
            continue;
        }
        if (soldier.downed)
        {
            // Унасан: хэвтэнэ, хугацаа дуусвал үхнэ
            soldier.moving = false;
            soldier.downTimer -= dt;
            if (soldier.downTimer <= 0.0f)
            {
                soldier.downed = false;
                soldier.health = 0.0f;
                soldier.alive = false;
                soldier.deathTimer = kFallTime; // аль хэдийн хэвтэж байна
            }
            continue;
        }
        // Хашаа давж буй: гараа тавьж, орой дээгүүр нум зурж цаад талдаа буна
        if (soldier.vaultTime >= 0.0f)
        {
            soldier.vaultTime += dt;
            const float t = std::min(1.0f, soldier.vaultTime / kVaultTime);
            const float smooth = t * t * (3.0f - 2.0f * t);
            const Passage& v = soldier.vault;
            Vector3 at = Vector3Lerp(v.from, v.to, smooth);
            at.y += std::sin(t * PI) * std::max(0.4f, v.top + 0.15f - std::max(v.from.y, v.to.y));
            soldier.mover.position = at;
            soldier.mover.facing = v.facing;
            soldier.moving = true;
            soldier.moveAmount = 1.0f;
            if (t >= 1.0f)
            {
                soldier.vaultTime = -1.0f;
                soldier.mover.position = v.to;
                soldier.path.clear();
            }
            continue;
        }
        // Өмнөх кадрт хашаанд тулсан (явах ёстойгоосоо хамаагүй бага явсан): тэр зүгт давж болох хашаа байвал давна
        if (soldier.riding < 0 && soldier.mover.wishStep > 0.02f && soldier.mover.moved < soldier.mover.wishStep * 0.4f)
        {
            Passage vault;
            if (world.FindVault(soldier.mover.position, soldier.mover.wishDir, 0.9f, kRadius, 3.0f, vault))
            {
                soldier.vault = vault;
                soldier.vaultTime = 0.0f;
                soldier.mover.wishStep = 0.0f;
                continue;
            }
        }
        soldier.mover.wishStep = 0.0f;
        if (soldier.reviveTarget != -2)
        {
            // Унасан нөхөр рүүгээ гүйнэ (UpdateRevives 1.5 м-т ирэхэд босгоно)
            const Vector3 goal = soldier.reviveTarget == -1 ? hostPosition
                               : soldier.reviveTarget == -3 ? guestLeader
                                                            : soldiers[(size_t)soldier.reviveTarget].mover.position;
            Vector3& here = soldier.mover.position;
            if (HorizontalDistance(here, goal) > 1.3f)
            {
                const float eased = EaseSpeed(soldier, kCatchUpSpeed, dt);
                MoveToward(soldier.mover, goal, eased, dt, world, kRadius, kHeight, 0.0f);
                soldier.walkCycle += dt * HumanoidGaitRate(eased);
                soldier.moveAmount = eased / kWalkSpeed;
                soldier.moving = true;
                here.y = world.SurfaceHeightAt(here.x, here.z, here.y + 0.5f);
            }
            else
            {
                soldier.moving = false;
                soldier.mover.facing = std::atan2(goal.x - here.x, goal.z - here.z);
            }
            continue;
        }
        if (!soldier.moving) soldier.speed = 0.0f; // зогссон: дараа нь тайван хурдалж эхэлнэ
        {
            const float step = HorizontalDistance(playerPosition, soldier.lastLeader);
            const float pace = step < 3.0f && dt > 0.0f ? step / dt : 0.0f; // үсэрсэн (teleport) бол тооцохгүй
            soldier.leaderSpeed += (pace - soldier.leaderSpeed) * std::min(1.0f, 3.0f * dt);
            soldier.lastLeader = playerPosition;
        }
        soldier.recoil = std::max(0.0f, soldier.recoil - dt * 9.0f);
        soldier.aimHold -= dt;
        soldier.aimPose += ((soldier.aimHold > 0.0f ? 1.0f : 0.0f) - soldier.aimPose) * std::min(1.0f, 8.0f * dt);
        Vector3& pos = soldier.mover.position;

        const bool teammate = soldier.member >= 0;
        const bool sniper = IsSniperMember(soldier.member);
        const int ownRoom = world.InteriorAt(pos.x, pos.z);

        // --- Багийн гишүүн: зомби ойртвол тоглогчийг дагахаа больж, өөрөө байр сонгон байлдана ---
        // --- Элсүүлсэн цэрэг ч мөн адил: харуулын байрнаасаа зомби руу бие даан байр сонгон байлдана ---
        int enemyIndex = -1;
        const bool mounted = soldier.riding >= 0 || soldier.boarding;
        const bool holding = (soldier.holding && teammate && !mounted) || soldier.waitingRescue;
        soldier.regroupTimer = std::max(0.0f, soldier.regroupTimer - dt);
        if (!mounted && !holding && soldier.regroupTimer <= 0.0f &&
            HorizontalDistance(pos, playerPosition) < (teammate ? kTeamLeash : kRecruitLeash))
        {
            // Элсүүлсэн цэрэг удирдаж буй гишүүнээ хамгаална: түүнд ойртсон зомбийг түрүүлж,
            // дараа нь өөрийнхөө дэргэдэхийг.
            const float keep = soldier.fighting ? 1.3f : 1.0f; // байлдаж эхэлсэн бол заагийн дээр чичигнэхгүй
            if (!teammate) enemyIndex = horde.Nearest(playerPosition, kRecruitProtect * keep);
            if (enemyIndex < 0)
            {
                enemyIndex = horde.Nearest(pos, (sniper ? kSniperEngage : teammate ? kFighterEngage : kRecruitEngage) * keep);
            }
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
        else if (holding)
        {
            // "Энд бай": байрандаа зогсоно (түлхэгдсэн бол буцаж ирнэ), доорх буудлага эндээс явагдана.
            const float off = HorizontalDistance(pos, soldier.holdPoint);
            soldier.moving = off > 0.8f;
            if (soldier.moving)
            {
                const float speed = off > 6.0f ? kCatchUpSpeed : kWalkSpeed;
                const float eased = EaseSpeed(soldier, speed, dt);
                MoveToward(soldier.mover, soldier.holdPoint, eased, dt, world, kRadius, kHeight, 0.0f);
                soldier.walkCycle += dt * HumanoidGaitRate(eased);
                soldier.moveAmount = eased / kWalkSpeed;
            }
            soldier.routeFrom = soldier.routeTo = -2;
            soldier.stuckTimer = 0.0f;
            soldier.bestDistance = 0.0f;
        }
        else if (soldier.boarding)
        {
            // Суух хэрэгсэл рүүгээ гүйнэ.
            const float eased = EaseSpeed(soldier, kCatchUpSpeed, dt);
            MoveToward(soldier.mover, soldier.boardPoint, eased, dt, world, kRadius, kHeight, 0.0f);
            soldier.walkCycle += dt * HumanoidGaitRate(eased);
            soldier.moveAmount = eased / kWalkSpeed;
            soldier.moving = true;
            soldier.routeFrom = soldier.routeTo = -2;
            soldier.stuckTimer = 0.0f;
            soldier.bestDistance = 0.0f;
        }
        else if (soldier.fighting)
        {
            const Vector3 enemy = horde.At(enemyIndex).mover.position;
            const float enemyDistance = HorizontalDistance(pos, enemy);
            const float nearLimit = sniper ? kSniperNear : teammate ? kFighterNear : kRecruitNear;
            const float farLimit = sniper ? kSniperFar : teammate ? kFighterFar : kRecruitFar;
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
                const float eased = EaseSpeed(soldier, speed, dt);
                MoveToward(soldier.mover, point, eased, dt, world, kRadius, kHeight, 0.0f);
                soldier.walkCycle += dt * HumanoidGaitRate(eased);
                soldier.moveAmount = eased / kWalkSpeed;
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
            // Элсүүлсэн цэргүүд гадаа удирдагчийн эргэн тойронд харуулын байр эзэлнэ: хэдэн секунд тутам,
            // эсвэл удирдагч холдоход шинэ байр сонгоно. Барилга дотор удирдагчийн дэргэд байна.
            const bool guarding = !teammate && playerRoom < 0;
            if (guarding)
            {
                soldier.guardTimer -= dt;
                if (soldier.guardTimer <= 0.0f || HorizontalDistance(soldier.guardAnchor, playerPosition) > kGuardRelocate)
                {
                    ChooseGuardPost(i, playerPosition, world);
                }
            }
            Vector3 slot = guarding ? soldier.guardPost : SlotPosition(i, playerPosition, teammate ? kTeamSpacing : kFollowDistance);

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

            // Гадаа: зорилт руу шууд шугам байшин, ханаар хаагдвал нарийн торон дээр зам хайж тойрно
            soldier.pathCheck -= dt;
            soldier.pathAge += dt;
            const bool outdoors = ownRoom < 0 && playerRoom < 0;
            if (!soldier.path.empty() &&
                (!outdoors || soldier.pathAge > 20.0f || Vector2Distance(soldier.path.back(), { target.x, target.z }) > 8.0f))
            {
                soldier.path.clear(); // зорилт холдсон, хуучирсан: дахин шалгана
            }
            if (outdoors && soldier.path.empty() && soldier.pathCheck <= 0.0f && HorizontalDistance(pos, target) > 5.0f)
            {
                soldier.pathCheck = 1.2f + 0.3f * (float)(i % 4); // цэргүүд нэг frame-д бөөнөөрөө тооцохгүй
                const Vector3 eye = { pos.x, pos.y + 1.1f, pos.z };
                const Vector3 toward = { target.x - pos.x, 0.0f, target.z - pos.z };
                const float length = std::sqrt(toward.x * toward.x + toward.z * toward.z);
                const Ray ray = { eye, { toward.x / length, 0.0f, toward.z / length } };
                if (world.RayDistance(ray, length) < length - 1.0f)
                {
                    std::vector<Vector2> points;
                    if (FindLocalPath(world, { pos.x, pos.z }, { target.x, target.z }, kRadius, points))
                    {
                        soldier.path = points;
                        soldier.pathIndex = 0;
                        soldier.pathAge = 0.0f;
                    }
                    else
                    {
                        soldier.pathCheck = 4.0f; // зам алга: хэсэг хүлээгээд дахин
                    }
                }
            }
            if (!soldier.path.empty())
            {
                while (soldier.pathIndex + 1 < soldier.path.size() &&
                       HorizontalDistance(pos, { soldier.path[soldier.pathIndex].x, pos.y, soldier.path[soldier.pathIndex].y }) < 1.4f)
                {
                    ++soldier.pathIndex;
                }
                const Vector2 waypoint = soldier.path[soldier.pathIndex];
                if (soldier.pathIndex + 1 == soldier.path.size() && HorizontalDistance(pos, { waypoint.x, pos.y, waypoint.y }) < 1.4f)
                {
                    soldier.path.clear();
                }
                else
                {
                    target = { waypoint.x, pos.y, waypoint.y };
                }
            }

            const float toSlot = HorizontalDistance(pos, slot);
            const bool onRoute = ownRoom != playerRoom;
            const float startDistance = teammate && playerRoom < 0 && !soldier.moving ? kTeamStartFollow
                                      : guarding && !soldier.moving                    ? 1.5f
                                                                                       : guarding ? 0.5f : 1.2f;
            // Удирдагч явж байхад байрандаа хүрээд зогсохгүй, түүнтэй ижил хурдаар үргэлжилнэ (явж-зогсож чичигнэхгүй)
            soldier.moving = onRoute || toSlot > startDistance || (soldier.moving && !guarding && soldier.leaderSpeed > 0.8f);

            // Гацсан эсэх: зорилтдоо ойртохгүй хэдэн секунд болбол (хашаа, хана тойрч чадахгүй)
            // тойрох зүгээ сольж оролдоно (доор). Цэргүүд жинхэнэ зам хайдаггүй тул энэ нь баталгаа болно.
            const float remaining = HorizontalDistance(pos, target);
            const bool progressed = remaining < soldier.bestDistance - 0.4f;
            if (!soldier.moving || progressed) soldier.stuckCount = 0;
            if (!soldier.moving || progressed || remaining > soldier.bestDistance + 6.0f)
            {
                soldier.bestDistance = remaining; // ахиц гарсан (эсвэл зорилт өөрчлөгдсөн)
                soldier.stuckTimer = 0.0f;
            }
            else
            {
                soldier.stuckTimer += dt;
            }
            const bool stuck = soldier.stuckTimer > kStuckTime;

            // Тоглогчийн дэргэд гэнэт гарч ирэхгүй: заавал араас нь гүйж гүйцнэ.
            // Гацвал эхлээд нөгөө талаар нь удаан тойрно. Гурав дахь удаа ч гацвал (хашаанд бүрэн хаагдсан)
            // тоглогчийн дэргэд биш, 12-25 м цаана, түүн рүү чиглэсэн шугам дээр тавьж, тэндээс гүйлгэнэ.
            if (stuck)
            {
                soldier.stuckTimer = 0.0f;
                soldier.bestDistance = remaining;
                ++soldier.stuckCount;
                std::vector<Vector2> points;
                if (outdoors && soldier.path.empty() && FindLocalPath(world, { pos.x, pos.z }, { slot.x, slot.z }, kRadius, points))
                {
                    // Гацсан (хашаа г.м., сум нэвтэрдэг тул шугамаар илрээгүй): тойрох зам
                    soldier.path = points;
                    soldier.pathIndex = 0;
                    soldier.pathAge = 0.0f;
                    soldier.stuckCount = 0;
                }
                else if (soldier.stuckCount < 3)
                {
                    soldier.mover.detourSign = -soldier.mover.detourSign;
                    soldier.mover.detourTimer = 2.5f;
                }
                else
                {
                    soldier.stuckCount = 0;
                    soldier.routeFrom = soldier.routeTo = -2;
                    const float away = std::max(0.01f, HorizontalDistance(pos, playerPosition));
                    const float back = std::clamp(away * 0.5f, 12.0f, 25.0f);
                    Vector3 spot = { playerPosition.x + (pos.x - playerPosition.x) / away * back, playerPosition.y,
                                     playerPosition.z + (pos.z - playerPosition.z) / away * back };
                    world.ResolveHorizontal(spot, kRadius, kHeight, 0.0f);
                    if (world.InteriorAt(spot.x, spot.z) < 0 && world.InsideBoundary(spot.x, spot.z))
                    {
                        spot.y = world.GroundHeight(spot.x, spot.z);
                        pos = spot;
                    }
                }
            }
            if (soldier.moving)
            {
                // Харуулын байр солихдоо ойрхон бол тайван алхана, хол бол гүйнэ; хол хоцорсон бол бүр хурдан.
                const float catchUp = toSlot > 25.0f ? kSprintSpeed : kCatchUpSpeed;
                float speed = guarding ? (toSlot > 20.0f ? catchUp : toSlot > 7.0f ? kWalkSpeed : kGuardStroll)
                                       : (toSlot > 8.0f ? catchUp : std::max(kWalkSpeed, soldier.leaderSpeed * 1.1f));
                if (!guarding && !onRoute && toSlot < 3.0f && soldier.leaderSpeed > 0.5f)
                {
                    speed = std::min(speed, soldier.leaderSpeed + toSlot); // байрандаа: удирдагчийн хурдаар
                }
                const float eased = EaseSpeed(soldier, speed, dt);
                MoveToward(soldier.mover, target, eased, dt, world, kRadius, kHeight, 0.0f);
                soldier.walkCycle += dt * HumanoidGaitRate(eased);
                soldier.moveAmount = eased / kWalkSpeed;
            }
            else if (guarding)
            {
                // Байрандаа: гадагш харж, эргэн тойрноо аажим ажиглана.
                const float scan = soldier.guardWatch + 0.7f * std::sin((float)GetTime() * 0.35f + (float)i * 1.7f);
                soldier.mover.facing += std::remainder(scan - soldier.mover.facing, 2.0f * PI) * std::min(1.0f, 2.5f * dt);
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
        if (!holding)
        {
            // Удирдаж буй гишүүнд ойртсон зомбийг түрүүлж буудна (байрандаа үлдсэн гишүүн өөрийнхөө орчныг).
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
        // Тушаал: "байг онил" — тэр зомби буудах зайд байвал бүгд түүн рүү
        if (focusTimer > 0.0f && !soldier.guestTeam)
        {
            const int focus = horde.FindId(focusId);
            if (focus >= 0 && HorizontalDistance(pos, horde.At(focus).mover.position) < fireRange * 1.4f) targetIndex = focus;
        }
        // Тулаан: багийн гишүүн эсрэг багийн хамгийн ойрын хүнийг (буудах зайд) зомбиос түрүүлнэ.
        // Элсүүлсэн цэргүүд зөвхөн зомбитой.
        const HumanTarget* human = nullptr;
        if (battle && teammate)
        {
            float best = fireRange;
            for (const HumanTarget& candidate : soldier.guestTeam ? hostTargets : guestTargets)
            {
                const float distance = Vector3Distance(pos, candidate.chest);
                if (distance < best)
                {
                    best = distance;
                    human = &candidate;
                }
            }
        }
        if (targetIndex < 0 && !human)
        {
            continue;
        }

        const Vector3 muzzle = HumanoidLocalToWorld(pos, soldier.mover.facing,
                                                    HumanoidMuzzleFor(sniper ? HeldItem::Sniper : HeldItem::Rifle));
        const Vector3 chest = human ? human->chest : horde.At(targetIndex).Chest();
        const Vector3 toTarget = Vector3Subtract(chest, muzzle);
        const float distance = Vector3Length(toTarget);
        if (distance < 0.01f)
        {
            continue;
        }

        // Байлдаж байхдаа хажуу тийш шилжих, ухрах үедээ ч бай руугаа харсан хэвээр байна.
        if (!soldier.moving || soldier.fighting)
        {
            // Бай руу зөөлөн эргэнэ (шууд эргэвэл явах, буудах зүг хооронд чичигнэдэг)
            const float want = std::atan2(chest.x - pos.x, chest.z - pos.z);
            soldier.mover.facing += std::remainder(want - soldier.mover.facing, 2.0f * PI) * std::min(1.0f, 12.0f * dt);
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
                // Хүнд бүх сум тусахгүй (тоглогч зайлж, халхлах боломжтой)
                // (мэргэн буудагчийн зомбид зориулсан хохирол хүнийг нэг сумаар унагаахгүй)
                if (human && GetRandomValue(0, 99) < 45) humanHits.push_back({ human->id, std::min(damage, 40.0f) });
                else if (!human)
                {
                    const bool wasAlive = horde.At(targetIndex).IsAlive();
                    horde.Damage(targetIndex, damage);
                    if (wasAlive && !horde.At(targetIndex).IsAlive()) ++soldier.kills;
                }
                horde.AlertAround(pos, 70.0f); // буун дуунд зомби сэрнэ
                tracers.Add(muzzle, chest, sniper ? RAYWHITE : ORANGE);
                Audio::Play(sniper ? Sfx::Sniper : Sfx::Rifle, muzzle, 0.5f);
                ++soldier.shots;
                soldier.shotEnd = chest;
                particles.Flash(muzzle, ray.direction);
                particles.Blood(chest, Vector3Negate(ray.direction), 4);
            }
        }
    }
}

void SoldierSquad::Damage(int index, float amount)
{
    Soldier& soldier = soldiers[index];
    if (!soldier.alive) return;
    soldier.sinceHit = 0.0f;
    if (soldier.downed)
    {
        soldier.downTimer -= amount * 0.12f; // унасан хүнийг цохивол хурдан үхнэ
        if (soldier.downTimer <= 0.0f)
        {
            soldier.downed = false;
            soldier.health = 0.0f;
            soldier.alive = false;
        }
        return;
    }
    // Элсүүлсэн жирийн цэрэг багийн гишүүдээс сул: хохирлыг 1.5 дахин авна.
    soldier.health -= soldier.member < 0 ? amount * 1.5f : amount / kTeamToughness;
    if (soldier.health <= 0.0f)
    {
        soldier.health = 0.0f;
        if (soldier.member >= 0 && soldier.riding < 0)
        {
            // Багийн гишүүн: үхэхийн оронд унана (нөхөд нь босгож болно)
            soldier.downed = true;
            soldier.downTimer = battle ? kPvpDownTime : Player::kDownTime;
            soldier.reviveProgress = 0.0f;
            soldier.health = 1.0f;
            soldier.moving = false;
            soldier.fighting = false;
            soldier.path.clear();
            return;
        }
        soldier.alive = false;
    }
}

void SoldierSquad::UpdateRevives(float dt, Vector3 hostPosition)
{
    // Унасан хүн бүрд (гишүүд, хост, зочин) тэр багийн хамгийн ойрын эрүүл гишүүнийг илгээнэ
    for (Soldier& soldier : soldiers) soldier.reviveTarget = -2;
    struct Need
    {
        int id;        // squad индекс, -1 хост, -3 зочин
        bool guestTeam;
        Vector3 position;
    };
    std::vector<Need> needs;
    for (int i = 0; i < Total(); ++i)
    {
        const Soldier& s = soldiers[(size_t)i];
        if (s.alive && s.downed) needs.push_back({ i, s.guestTeam, s.mover.position });
    }
    if (hostDowned) needs.push_back({ -1, false, hostPosition });
    if (guestDowned && guestLeaderActive) needs.push_back({ -3, true, guestLeader });
    for (const Need& need : needs)
    {
        int best = -1;
        float bestDistance = 70.0f;
        for (int i = 0; i < Total(); ++i)
        {
            const Soldier& s = soldiers[(size_t)i];
            if (i == need.id || !s.alive || s.downed || s.member < 0 || s.guestTeam != need.guestTeam || s.riding >= 0 ||
                s.reviveTarget != -2 || s.frozen > 0.0f) continue;
            const float d = HorizontalDistance(s.mover.position, need.position);
            if (d < bestDistance)
            {
                bestDistance = d;
                best = i;
            }
        }
        if (best < 0) continue;
        Soldier& helper = soldiers[(size_t)best];
        helper.reviveTarget = need.id;
        if (bestDistance > 1.5f) continue; // очиж байна (Update хөдөлгөнө)
        // Дэргэд нь: 3 секундэд (тулаанд 5) босгоно. Тулаанд босгогч буудуулбал дахин эхэлнэ.
        float& progress = need.id == -1 ? hostRevive : need.id == -3 ? guestRevive : soldiers[(size_t)need.id].reviveProgress;
        if (battle && helper.sinceHit < 0.6f)
        {
            progress = 0.0f;
            continue;
        }
        progress += dt;
        if (progress < (battle ? kPvpReviveTime : 3.0f)) continue;
        progress = 0.0f;
        if (need.id == -1) hostRevived = true;
        else if (need.id == -3) guestRevived = true;
        else
        {
            Soldier& target = soldiers[(size_t)need.id];
            target.downed = false;
            target.health = target.maxHealth * 0.3f;
        }
    }
}

void SoldierSquad::Regroup(float seconds)
{
    for (Soldier& soldier : soldiers)
    {
        if (soldier.guestTeam || !soldier.alive) continue;
        soldier.regroupTimer = seconds;
        soldier.holding = false;
        soldier.fighting = false;
        soldier.path.clear();
    }
}

void SoldierSquad::DamageFraction(int index, float fraction)
{
    Soldier& soldier = soldiers[index];
    Damage(index, soldier.maxHealth * fraction * (soldier.member < 0 ? 1.0f / 1.5f : kTeamToughness));
}

void SoldierSquad::AddGuestFighter(int member, Vector3 position, float facing, float health, float frozen)
{
    Soldier soldier;
    soldier.id = NewId();
    soldier.mover.position = position;
    soldier.mover.facing = facing;
    soldier.member = member;
    soldier.guestTeam = true;
    soldier.maxHealth = std::max(health, kMaxHealth);
    soldier.health = health;
    soldier.frozen = frozen;
    soldiers.push_back(soldier);
}

int SoldierSquad::AliveCount() const
{
    return (int)std::count_if(soldiers.begin(), soldiers.end(), [](const Soldier& s) { return s.alive; });
}

void SoldierSquad::Draw(Vector3 viewPosition, bool showColliders) const
{
    for (const Soldier& soldier : soldiers)
    {
        if (!ViewCull::Visible(soldier.mover.position, 1.5f)) continue; // камерын ард
        static const HumanoidLook recruitLook = MakeLook();
        const HumanoidLook memberLook =
            soldier.member >= 0 ? TeamMemberLook(soldier.member, soldier.guestTeam ? guestLeaderSkin : leaderSkin) : recruitLook;
        const HumanoidLook& look = memberLook;
        const bool detailed = HorizontalDistance(viewPosition, soldier.mover.position) < kHumanoidDetailDistance;
        HumanoidPose pose = MakeSoldierPose(soldier.walkCycle, soldier.moving ? soldier.moveAmount : 0.0f, true,
                                            (float)GetTime() + soldier.walkCycle, soldier.recoil, look.held);
        pose.aim = soldier.aimPose;
        pose.speed = soldier.moving ? soldier.moveAmount * kWalkSpeed : 0.0f;

        if (soldier.alive && soldier.riding >= 0)
        {
            if (soldier.seatStyle == 3 || soldier.seatStyle == 5)
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
            // Буудахдаа бие бай руу эргэнэ (±120 градус), ташаа хөл нь унааны зүгт.
            const float twist = std::clamp(std::remainder(soldier.mover.facing - soldier.seatHeading, 2.0f * PI), -2.1f, 2.1f);
            pose.seatTwist = -twist * RAD2DEG;
            const Vector3 base = { soldier.seat.x, soldier.seat.y - 0.9f, soldier.seat.z };
            const float facing = soldier.seatHeading + twist;
            if (soldier.hasRein)
            {
                const Vector3 d = Vector3Subtract(soldier.rein, base);
                const float sn = std::sin(facing), cs = std::cos(facing);
                pose.rein = 1.0f;
                pose.reinPoint = { d.x * cs - d.z * sn, d.y - pose.bob, d.x * sn + d.z * cs };
            }
            DrawHumanoid(base, facing, 1.0f, pose, look, detailed);
            continue;
        }

        if (soldier.alive && soldier.downed)
        {
            // Унасан (босгохыг хүлээж буй): нуруугаараа хэвтэж, бага зэрэг хөдөлнө
            ApplyDownedPose(pose, (float)GetTime() + soldier.walkCycle);
            DrawHumanoid(soldier.mover.position, soldier.mover.facing, 1.0f, pose, look, detailed);
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
