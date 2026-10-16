#include "Zombie.h"

#include "Audio.h"
#include "Humanoid.h"
#include "Player.h"
#include "CharacterModel.h"
#include "Soldier.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>

namespace
{
    const float kClimbHeight = 1.05f; // зомби ийм өндөр хайрцаг дээр авирч гарна
    const float kAttackInterval = 1.0f;
    const float kAttackAnimTime = 0.35f;
    const float kStaggerTime = 0.28f;
    const float kFallTime = 0.55f;   // унаж дуусах хугацаа
    const float kCorpseTime = 4.5f;  // цогцос алга болох хүртэлх хугацаа
    const float kSinkTime = 1.0f;    // төгсгөлд газарт шингэх хугацаа
    const float kSpeedScale = 0.8f;  // бүх зомбины үндсэн хурдыг 20% удаашруулна
    const float kModelDistance = 60.0f;   // үүнээс хол зомбийг загварын хөнгөн хувилбараар зурна
    // Зэрэг харагдах зомби олон тул зөвхөн хамгийн ойрын хэдийг нь олон гурвалжинтай загвараар зурна
    // (утсан дээр цөөн). Бусад нь хөнгөн дүрсээр зурагдана.
#if defined(__ANDROID__)
    const int kModelBudget = 8;
    const float kSimpleDistance = 70.0f;  // үүнээс хол кодоор үүсгэсэн хамгийн хөнгөн дүрсээр (утас сул)
#else
    const int kModelBudget = 16;
    const float kSimpleDistance = 110.0f; // үүнээс хол (манан дунд) кодоор үүсгэсэн хамгийн хөнгөн дүрсээр
#endif
    const float kAggroDistance = 60.0f; // ийм зайд бай орж ирэхэд зомби сэрэмжилж хөөнө
    // Бүх зомбины амийг 300%-иар нэмэгдүүлнэ (4 дахин). Толгойд оносон сум аваргаас бусдыг нэг сумаар устгасан хэвээр.
    const float kZombieHealthMultiplier = 4.0f;

    struct TypeStats
    {
        float health;
        float speed;
        float damageScale;
        float scale;
        float bulk;
        float cadence; // алхааны давтамжийн үржүүлэгч
        int reward;
    };

    TypeStats StatsFor(ZombieType type)
    {
        switch (type)
        {
        case ZombieType::Runner: return { 35.0f, 4.6f, 0.7f, 0.92f, 0.9f, 1.5f, 8 };
        case ZombieType::Brute: return { 220.0f, 1.9f, 2.0f, 1.32f, 1.28f, 3.2f, 20 }; // хэмнэл: том алхам гулсахгүй
        case ZombieType::Walker:
        default: return { 80.0f, 2.6f, 1.0f, 1.0f, 1.0f, 2.1f, 5 }; // 80 амь: анхны буугаар биед 4 сум
        }
    }

    float Random(float low, float high)
    {
        return low + (high - low) * (float)GetRandomValue(0, 1000) / 1000.0f;
    }

    Color Pick(const Color* palette, int count)
    {
        return palette[GetRandomValue(0, count - 1)];
    }

    // Зомби бүрд өөр хувцас, арьс, гэмтэл оноож, бүгд ижил харагдахаас сэргийлнэ.
    void RandomizeAppearance(Zombie& zombie)
    {
        static const Color shirts[] = {
            { 96, 100, 108, 255 }, { 124, 96, 76, 255 }, { 78, 96, 112, 255 }, { 132, 124, 104, 255 },
            { 112, 70, 64, 255 },  { 86, 98, 78, 255 },  { 150, 144, 132, 255 }, { 70, 68, 82, 255 },
        };
        static const Color pants[] = {
            { 58, 60, 70, 255 }, { 74, 64, 52, 255 }, { 50, 58, 52, 255 }, { 84, 82, 80, 255 }, { 44, 46, 60, 255 },
        };
        static const Color skins[] = {
            { 124, 150, 104, 255 }, { 142, 152, 120, 255 }, { 108, 132, 110, 255 }, { 150, 146, 124, 255 },
        };

        zombie.shirt = Pick(shirts, 8);
        zombie.pants = Pick(pants, 5);
        zombie.skin = Pick(skins, 4);
        static const Color hairs[] = { { 30, 26, 24, 255 }, { 62, 52, 44, 255 }, { 120, 116, 108, 255 } };
        zombie.hair = Pick(hairs, 3);
        zombie.hairStyle = GetRandomValue(0, 2);
        zombie.missingArm = GetRandomValue(0, 99) < 16;
        zombie.bloody = GetRandomValue(0, 99) < 55;
        zombie.openRibs = GetRandomValue(0, 99) < 35;
        zombie.tornLeg = GetRandomValue(0, 99) < 40;
        zombie.oneEye = GetRandomValue(0, 99) < 30;
        zombie.headWound = GetRandomValue(0, 99) < 35;
        zombie.shirtless = zombie.type == ZombieType::Runner && GetRandomValue(0, 99) < 60;
        if (zombie.type == ZombieType::Brute) zombie.hairStyle = 0; // том зомби халзан
        zombie.headRoll = Random(-22.0f, 22.0f);
        zombie.limp = GetRandomValue(0, 99) < 60 ? Random(0.3f, 0.8f) : 0.0f;
        zombie.armStyle = GetRandomValue(0, 2);
        zombie.scale *= Random(0.94f, 1.07f);
        zombie.walkCycle = Random(0.0f, 6.28f); // бүгд нэг хэмнэлээр алхахгүй

        if (zombie.type == ZombieType::Brute)
        {
            zombie.skin = { 96, 118, 92, 255 };
            zombie.limp = 0.0f;
        }
        else if (zombie.type == ZombieType::Runner)
        {
            zombie.skin = { 158, 162, 118, 255 };
            zombie.limp = 0.0f;
        }
    }

    HumanoidLook LookFor(const Zombie& zombie)
    {
        HumanoidLook look;
        look.kind = HumanoidKind::Zombie;
        look.pants = zombie.pants;
        look.boots = { 34, 30, 28, 255 };
        look.body = zombie.shirt;
        look.skin = zombie.skin;
        look.belt = zombie.pants;
        look.hair = zombie.hair;
        look.hairStyle = zombie.hairStyle;
        look.missingLeftArm = zombie.missingArm;
        look.bloody = zombie.bloody;
        look.shirtless = zombie.shirtless;
        look.openRibs = zombie.openRibs && !zombie.shirtless;
        look.tornLeg = zombie.tornLeg;
        look.oneEye = zombie.oneEye;
        look.headWound = zombie.headWound;
        look.bulk = StatsFor(zombie.type).bulk * (zombie.boss ? 1.15f : 1.0f);

        // Гаднаас оруулсан загвар (байвал). Ижил төрлийн зомбинууд нэг загвартай тул өнгийг нь бага зэрэг
        // өөрчилж ялгана; аварга нь улаавтар.
        look.model = zombie.boss ? kCharacterZombieBoss
                   : zombie.type == ZombieType::Runner ? kCharacterZombieRunner
                   : zombie.type == ZombieType::Brute ? kCharacterZombieBrute : kCharacterZombieWalker;
        look.modelTint = zombie.boss ? Color{ 255, 255, 255, 255 } // аварга өөрийн загвартай: өнгөө хэвээр
                                     : Color{ (unsigned char)(200 + zombie.skin.r % 56), (unsigned char)(200 + zombie.skin.g % 56),
                                              (unsigned char)(200 + zombie.shirt.b % 56), 255 };
        if (HasCharacterModel(look.model))
        {
            look.bulk = 1.0f + (look.bulk - 1.0f) * 0.4f; // загвар өөрөө бүдүүн, нарийн хэлбэртэй
        }
        return look;
    }

    // Өмнөх frame-ээс хойш явсан зайгаар бодит хурдыг хэмжиж (зөөлрүүлж), алхааны үеийг тэр хурдтай таарах
    // давтамжаар урагшлуулна: хөл газарт тулж, гулсахгүй.
    void AdvanceGait(Zombie& zombie, float dt)
    {
        const Vector3 pos = zombie.mover.position;
        const float moved = dt > 0.0f ? HorizontalDistance(pos, zombie.lastPosition) / dt : 0.0f;
        zombie.lastPosition = pos;
        const float measured = moved > 15.0f ? 0.0f : moved; // үсрэлт (snapshot, цонх давах) тооцохгүй
        zombie.gaitSpeed += (measured - zombie.gaitSpeed) * std::min(1.0f, 8.0f * dt);
        if (zombie.gaitSpeed > 0.25f && zombie.IsAlive())
        {
            zombie.walkCycle += dt * HumanoidGaitRate(zombie.gaitSpeed / zombie.scale);
        }
    }

    HumanoidPose PoseFor(const Zombie& zombie)
    {
        HumanoidPose pose;
        pose.headRoll = zombie.headRoll;

        // --- Үхсэн: өвдөг нугарч, гар суларч, нуруугаараа унана ---
        if (!zombie.IsAlive())
        {
            const float t = std::min(1.0f, zombie.deathTimer / kFallTime);
            const float eased = t * t; // хурдсан унана
            pose.fall = eased;
            const float buckle = std::sin(t * PI);
            pose.knee[0] = 50.0f * buckle + 12.0f * eased;
            pose.knee[1] = 35.0f * buckle + 4.0f * eased;
            pose.hip[0] = 25.0f * buckle;
            pose.shoulder[0] = -35.0f * eased;
            pose.shoulder[1] = 150.0f * eased; // нэг гар толгой дээгүүр
            pose.shoulderYaw[0] = -30.0f * eased;
            pose.elbow[0] = 25.0f;
            pose.elbow[1] = 30.0f;
            pose.headPitch = -25.0f * eased;
            pose.jaw = 30.0f;
            return pose;
        }

        const float cycle = zombie.walkCycle;
        const float s = std::sin(cycle);
        // Явж байхад хөл ээлжлэн газарт тулж гишгэнэ (доорх hip/knee нь зогсож байх үед л).
        pose.plantWalk = zombie.gaitSpeed > 0.25f && !zombie.airborne && zombie.climbTimer <= 0.0f;
        pose.stepLift = zombie.type == ZombieType::Brute || zombie.boss ? 1.5f : zombie.type == ZombieType::Runner ? 1.4f : 2.2f;
        pose.walkPhase = cycle;
        pose.speed = zombie.gaitSpeed / zombie.scale; // загварын орон зайд (DrawHumanoid scale-аар томруулна)
        pose.jaw = 14.0f + 8.0f * std::sin(cycle * 0.5f); // эрүү нь сул унжиж хөдөлнө
        auto kneeLift = [&](int side) { return std::max(0.0f, std::cos(cycle + (float)side * PI)); };

        switch (zombie.type)
        {
        case ZombieType::Runner:
            // Урагш бөхийж, гараа ардаа чирч гүйнэ
            pose.hip[0] = s * 46.0f;
            pose.hip[1] = -s * 46.0f;
            pose.knee[0] = 15.0f + 62.0f * kneeLift(0);
            pose.knee[1] = 15.0f + 62.0f * kneeLift(1);
            pose.lean = 30.0f;
            pose.roll = s * 4.0f;
            pose.bob = -0.05f * std::fabs(s);
            pose.headPitch = -22.0f;
            pose.shoulder[0] = -38.0f - s * 24.0f;
            pose.shoulder[1] = -38.0f + s * 24.0f;
            pose.shoulderYaw[0] = pose.shoulderYaw[1] = -14.0f;
            pose.elbow[0] = pose.elbow[1] = 34.0f;
            break;

        case ZombieType::Brute:
            // Хүнд, том алхаа: хөлөө өндөр өргөж газар дэлсэн гишгэнэ, бие нь гишгэсэн хөл рүүгээ найгаж,
            // гишгэх бүрд доош суунэ. Урт гараа хөлийнхөө эсрэг савлана (горилла маягийн).
            pose.hip[0] = s * 32.0f;
            pose.hip[1] = -s * 32.0f;
            pose.knee[0] = 12.0f + 48.0f * kneeLift(0);
            pose.knee[1] = 12.0f + 48.0f * kneeLift(1);
            pose.lean = 14.0f + 3.0f * std::fabs(s);
            pose.roll = s * 8.0f;
            pose.bob = -0.07f * (1.0f - std::fabs(s)); // хоёр хөл газарт: доош суунэ
            pose.headPitch = 10.0f;
            pose.shoulder[0] = 18.0f - s * 30.0f;
            pose.shoulder[1] = 18.0f + s * 30.0f;
            pose.shoulderYaw[0] = pose.shoulderYaw[1] = -18.0f; // гараа биеэсээ зайтай барина
            pose.elbow[0] = 30.0f + 12.0f * std::max(0.0f, s);
            pose.elbow[1] = 30.0f + 12.0f * std::max(0.0f, -s);
            break;

        case ZombieType::Walker:
        default:
            // Доголж, найгаж алхана: баруун хөлөө чирнэ
            pose.hip[0] = s * 22.0f * (1.0f - 0.65f * zombie.limp);
            pose.hip[1] = -s * 22.0f;
            pose.knee[0] = 8.0f + 28.0f * kneeLift(0) * (1.0f - 0.8f * zombie.limp);
            pose.knee[1] = 8.0f + 28.0f * kneeLift(1);
            pose.lean = 16.0f + 4.0f * std::sin(cycle * 2.0f);
            pose.roll = s * (5.0f + 6.0f * zombie.limp);
            pose.bob = -0.03f * std::fabs(s) - 0.04f * zombie.limp * std::max(0.0f, s);
            pose.headPitch = 18.0f;

            if (zombie.armStyle == 0)
            {
                // Хоёр гараа урагш сунгасан
                pose.shoulder[0] = 78.0f + s * 6.0f;
                pose.shoulder[1] = 74.0f - s * 6.0f;
                pose.elbow[0] = pose.elbow[1] = 12.0f;
            }
            else if (zombie.armStyle == 1)
            {
                // Нэг гар урагш, нөгөө нь унжсан
                pose.shoulder[0] = 82.0f + s * 5.0f;
                pose.elbow[0] = 10.0f;
                pose.shoulder[1] = 12.0f - s * 12.0f;
                pose.elbow[1] = 22.0f;
            }
            else
            {
                // Гараа доогуур, шүүрэх гэж буй мэт
                pose.shoulder[0] = 44.0f + s * 8.0f;
                pose.shoulder[1] = 50.0f - s * 8.0f;
                pose.elbow[0] = pose.elbow[1] = 42.0f;
            }
            break;
        }

        // --- Цохилт: гараа дээрээс доош савна ---
        if (zombie.attackAnim > 0.0f)
        {
            const float k = zombie.attackAnim / kAttackAnimTime; // 1 -> 0
            pose.shoulder[0] = 40.0f + 115.0f * k;
            pose.shoulder[1] = 40.0f + 115.0f * k;
            pose.shoulderYaw[0] = pose.shoulderYaw[1] = 10.0f;
            pose.elbow[0] = pose.elbow[1] = 20.0f;
            pose.lean += 14.0f * (1.0f - k);
            pose.jaw = 34.0f; // хазах гэж амаа ангайна
        }

        // --- Суманд оногдсон: биеэрээ хойш цочно ---
        if (zombie.stagger > 0.0f)
        {
            const float k = zombie.stagger / kStaggerTime;
            pose.lean -= 30.0f * k;
            pose.headPitch -= 22.0f * k;
            pose.shoulder[0] -= 35.0f * k;
            pose.shoulder[1] -= 35.0f * k;
        }

        return pose;
    }
}

Vector3 Zombie::Chest() const
{
    return { mover.position.x, mover.position.y + Height() * 0.65f, mover.position.z };
}

void ZombieHorde::Clear()
{
    zombies.clear();
    pendingCoins = 0;
    pendingKills = 0;
    pendingSmallKills = 0;
    pendingBigKills = 0;
}

void ZombieHorde::AlertAround(Vector3 center, float radius)
{
    for (Zombie& zombie : zombies)
    {
        if (zombie.IsAlive() && !zombie.alerted && HorizontalDistance(zombie.mover.position, center) < radius)
        {
            zombie.alerted = true;
        }
    }
}

void ZombieHorde::TakeKillTypes(int& small, int& big)
{
    small += pendingSmallKills;
    big += pendingBigKills;
    pendingSmallKills = 0;
    pendingBigKills = 0;
}

void ZombieHorde::CountAlive(int& small, int& big) const
{
    for (const Zombie& zombie : zombies)
    {
        if (zombie.IsAlive() && !zombie.boss)
        {
            ++(zombie.type == ZombieType::Brute ? big : small);
        }
    }
}

void ZombieHorde::RemoveFar(Vector3 center, float distance, bool ambientOnly)
{
    zombies.erase(std::remove_if(zombies.begin(), zombies.end(),
                                 [&](const Zombie& z) {
                                     return z.IsAlive() && !z.boss && (!ambientOnly || z.zone == 0) &&
                                            HorizontalDistance(z.mover.position, center) > distance;
                                 }),
                  zombies.end());
}

int ZombieHorde::AliveInZone(int zone) const
{
    int count = 0;
    for (const Zombie& zombie : zombies)
    {
        count += zombie.IsAlive() && zombie.zone == zone;
    }
    return count;
}

void ZombieHorde::DamageZone(int zone, float amount, int maxTargets)
{
    for (int i = 0; i < (int)zombies.size() && maxTargets > 0; ++i)
    {
        if (zombies[i].IsAlive() && zombies[i].zone == zone)
        {
            Damage(i, amount);
            --maxTargets;
        }
    }
}

void ZombieHorde::SpawnBoss(Vector3 position, const MissionParams& mission, int zone)
{
    Spawn(ZombieType::Brute, position, mission, zone, false);

    Zombie& boss = zombies.back();
    boss.boss = true;
    boss.health *= 7.0f;
    boss.maxHealth = boss.health;
    boss.damage *= 1.4f;
    boss.speed += 0.9f;
    boss.scale = 1.75f;
    boss.reward = 150;
    boss.skin = { 132, 52, 46, 255 };
    boss.shirt = { 44, 40, 44, 255 };
    boss.bloody = true;
    boss.hairStyle = 0;
    boss.missingArm = false;
}

void ZombieHorde::Spawn(ZombieType type, Vector3 position, const MissionParams& mission, int zone, bool alerted)
{
    const TypeStats stats = StatsFor(type);

    Zombie zombie;
    zombie.zone = zone;
    zombie.alerted = alerted;
    zombie.mover.position = position;
    zombie.lastPosition = position;
    zombie.mover.detourSign = GetRandomValue(0, 1) == 0 ? -1.0f : 1.0f;
    zombie.type = type;
    zombie.health = stats.health * mission.healthScale * kZombieHealthMultiplier;
    zombie.id = ++nextId;
    zombie.maxHealth = zombie.health;
    zombie.speed = (stats.speed + mission.speedBonus) * kSpeedScale;
    zombie.damage = mission.zombieDamage * stats.damageScale;
    zombie.scale = stats.scale;
    zombie.reward = stats.reward;
    RandomizeAppearance(zombie);
    zombies.push_back(zombie);
}

void ZombieHorde::Update(float dt, World& world, Player& player, SoldierSquad& squad)
{
    // Цогцос хэсэг хугацаанд хэвтээд арилна.
    zombies.erase(std::remove_if(zombies.begin(), zombies.end(),
                                 [](const Zombie& z) { return !z.IsAlive() && z.deathTimer > kCorpseTime; }),
                  zombies.end());

    std::vector<Soldier>& soldiers = squad.All();
    navClock += dt;
    const float kClimbTime = 1.1f; // цонхоор авирч орох хугацаа

    for (Zombie& zombie : zombies)
    {
        Vector3& pos = zombie.mover.position;
        AdvanceGait(zombie, dt);

        if (!zombie.IsAlive())
        {
            zombie.deathTimer += dt;

            // Машинд мөргүүлсэн: нум зурж нисээд газарт унана, унахдаа бага зэрэг гулсана.
            if (zombie.airborne)
            {
                zombie.flight.y -= 18.0f * dt;
                pos.x += zombie.flight.x * dt;
                pos.y += zombie.flight.y * dt;
                pos.z += zombie.flight.z * dt;
                zombie.tumble += 420.0f * dt;

                const float ground = world.GroundHeight(pos.x, pos.z);
                if (pos.y <= ground && zombie.flight.y < 0.0f)
                {
                    pos.y = ground;
                    zombie.airborne = false;
                    zombie.deathTimer = std::max(zombie.deathTimer, kFallTime); // газарт хэвтсэн байрлал
                }
                world.ResolveHorizontal(pos, zombie.Radius(), zombie.Height(), kClimbHeight);
            }
            continue;
        }

        // --- Бай сонгох: хамгийн ойр амьд тоглогч эсвэл цэрэг (дэлгүүр доторх хүнийг үл тооно) ---
        const int kPlayer = -1;
        const int kNone = -2;
        int target = kNone;
        float targetDistance = 1e9f;
        Vector3 targetPosition = pos;
        // Дэлгүүрт нуугдсан хамгийн ойрын хүний өрөө (бүслэх): бай болгохгүй ч хаяад явахгүй.
        int besiege = -1;
        float besiegeDistance = 1e9f;
        auto sheltered = [&](Vector3 p) {
            const int room = world.InteriorAt(p.x, p.z);
            if (room < 0 || !world.GetInterior(room).safe) return false;
            const float distance = HorizontalDistance(pos, p);
            if (distance < besiegeDistance)
            {
                besiegeDistance = distance;
                besiege = room;
            }
            return true;
        };

        if (player.IsAlive() && !sheltered(player.Position()))
        {
            target = kPlayer;
            targetPosition = player.Position();
            targetDistance = HorizontalDistance(pos, targetPosition);
        }
        for (int i = 0; i < (int)soldiers.size(); ++i)
        {
            if (!soldiers[i].alive || sheltered(soldiers[i].mover.position)) continue;
            const float distance = HorizontalDistance(pos, soldiers[i].mover.position);
            if (distance < targetDistance)
            {
                target = i;
                targetPosition = soldiers[i].mover.position;
                targetDistance = distance;
            }
        }
        const int kRemote = -3; // хамтарсан тоглолтын зочин
        if (remoteActive && !sheltered(remotePosition))
        {
            const float distance = HorizontalDistance(pos, remotePosition);
            if (distance < targetDistance)
            {
                target = kRemote;
                targetPosition = remotePosition;
                targetDistance = distance;
            }
        }

        zombie.attackCooldown -= dt;
        zombie.attackAnim -= dt;
        zombie.stagger -= dt;

        // Бүх бай дэлгүүрт нуугдсан: дэлгүүрийг тойрон аажуухан тэнэнэ (дотогш орохгүй, цохихгүй, хаалга нүдэхгүй).
        if (target == kNone && besiege >= 0 && zombie.alerted && zombie.climbTimer <= 0.0f)
        {
            const Interior& room = world.GetInterior(besiege);
            // Одоогийн байрнаасаа барилгыг тойрон (ханыг хөндлөн гарахгүй) хажуу тийш алхана.
            const float ring = std::max(room.halfX, room.halfZ) + 3.0f + (float)(zombie.id % 5);
            const float angle = std::atan2(pos.x - room.center.x, pos.z - room.center.y) + ((zombie.id % 2) ? 0.3f : -0.3f);
            const Vector3 spot = { room.center.x + std::sin(angle) * ring, pos.y, room.center.y + std::cos(angle) * ring };
            if (HorizontalDistance(pos, spot) > 0.6f)
            {
                const float speed = zombie.speed * speedMultiplier * 0.45f;
                MoveToward(zombie.mover, spot, speed, dt, world, zombie.Radius(), zombie.Height(), kClimbHeight);
                world.KeepOutOfSafeRooms(pos, zombie.Radius());
            }
            const float floor = world.SurfaceHeightAt(pos.x, pos.z, pos.y + kClimbHeight);
            pos.y = pos.y < floor ? std::min(floor, pos.y + 4.0f * dt) : std::max(floor, pos.y - 12.0f * dt);
            continue;
        }
        if (target == kNone)
        {
            continue;
        }

        // Сэрэмжлээгүй зомби байрандаа зогсоно; бай нь ойртох эсвэл суманд оногдоход хөөж эхэлнэ.
        if (!zombie.alerted)
        {
            // Тоглогч суусан, хэвтсэн бол (баг нь түүнтэй хамт сэмхэн явна) зомби ойроос л анзаарна.
            if (targetDistance < kAggroDistance * player.Concealment())
            {
                zombie.alerted = true;
            }
            else
            {
                continue;
            }
        }

        // --- Цонхоор авирч орж яваа: тавцан дээгүүр нум зурж давна ---
        if (zombie.climbTimer > 0.0f)
        {
            zombie.climbTimer -= dt;
            const float t = 1.0f - std::max(0.0f, zombie.climbTimer) / kClimbTime;
            pos = Vector3Lerp(zombie.climbFrom, zombie.climbTo, t);
            pos.y += std::sin(t * PI) * 1.15f;
            zombie.walkCycle += dt * 8.0f;
            if (zombie.climbTimer <= 0.0f)
            {
                pos = zombie.climbTo;
            }
            continue;
        }

        // --- Зам сонгох: бай барилга дотор, эсвэл өөр давхарт байвал шууд биш, гарцаар дамжина ---
        Vector3 moveTarget = targetPosition;
        const int targetRoom = world.InteriorAt(targetPosition.x, targetPosition.z);
        const int ownRoom = world.InteriorAt(pos.x, pos.z);
        if (targetRoom >= 0 && ownRoom != targetRoom)
        {
            // Барилга руу нэвтрэх: хаалга эсвэл хамгийн ойрын 1 давхрын цонх. Сонголтыг хэдэн секунд тутам шинэчилнэ.
            const Interior& room = world.GetInterior(targetRoom);
            if (zombie.entryRoom != targetRoom || navClock - zombie.entryChosenAt > 2.5f)
            {
                zombie.entryRoom = targetRoom;
                zombie.entryChosenAt = navClock;
                zombie.entryPassage = -1;
                const Vector3 doorPoint = { room.doorOutside.x, pos.y, room.doorOutside.y };
                // Нээлттэй хаалга хамгийн хялбар зам тул давуу
                float best = HorizontalDistance(pos, doorPoint) - (world.IsDoorOpen(room.door) ? 4.0f : 0.0f);
                for (int i = 0; i < world.PassageCount(); ++i)
                {
                    const Passage& passage = world.GetPassage(i);
                    if (passage.kind != PassageKind::WindowIn || !room.Contains(passage.to.x, passage.to.z))
                    {
                        continue;
                    }
                    const float distance = HorizontalDistance(pos, passage.from);
                    if (distance < best)
                    {
                        best = distance;
                        zombie.entryPassage = i;
                    }
                }
            }

            if (zombie.entryPassage >= 0)
            {
                const Passage& passage = world.GetPassage(zombie.entryPassage);
                moveTarget = passage.from;
                if (HorizontalDistance(pos, passage.from) < 0.9f)
                {
                    zombie.climbTimer = kClimbTime;
                    zombie.climbFrom = pos;
                    zombie.climbTo = passage.to;
                    continue;
                }
            }
            else
            {
                // Хаалга: гадна талын цэг рүү очиж, хаалттай бол нүдэж эвдэнэ; онгойсон бол дотогш орно.
                const Vector3 outside = { room.doorOutside.x, pos.y, room.doorOutside.y };
                const Vector3 inside = { room.doorInside.x, pos.y, room.doorInside.y };
                const bool atDoor = HorizontalDistance(pos, outside) < 2.6f;
                moveTarget = atDoor && world.IsDoorOpen(room.door) ? inside : outside;
                if (atDoor)
                {
                    world.PoundDoors(pos, dt);
                }
            }
        }
        else if (targetRoom >= 0 && world.GetInterior(targetRoom).hasStairs && std::fabs(targetPosition.y - pos.y) > 1.2f)
        {
            // Нэг байшинд, өөр давхарт: шатаар гарна / бууна.
            const Interior& room = world.GetInterior(targetRoom);
            const Vector3 bottom = { room.stairBottom.x, pos.y, room.stairBottom.y };
            const Vector3 top = { room.stairTop.x, pos.y, room.stairTop.y };
            const float ground = world.GroundHeight(pos.x, pos.z);
            if (targetPosition.y > pos.y)
            {
                // Доод давхарт байвал эхлээд шатны эхэнд очно; шатан дээр гарсан бол дээд үзүүр рүү
                moveTarget = pos.y < ground + 0.3f && HorizontalDistance(pos, bottom) > 0.7f ? bottom : top;
            }
            else
            {
                moveTarget = pos.y > room.upperFloor - 0.3f && HorizontalDistance(pos, top) > 0.7f ? top : bottom;
            }
        }
        else
        {
            zombie.entryRoom = -1;
        }

        // --- Хөдөлгөөн: оногдсон үедээ бараг зогсоно ---
        const float reach = zombie.Radius() + 0.9f;
        const bool detour = moveTarget.x != targetPosition.x || moveTarget.z != targetPosition.z;
        if (detour || targetDistance > reach * 0.8f)
        {
            const float speed = zombie.speed * speedMultiplier * (zombie.stagger > 0.0f ? 0.2f : 1.0f);
            MoveToward(zombie.mover, moveTarget, speed, dt, world, zombie.Radius(), zombie.Height(), kClimbHeight);
            world.KeepOutOfSafeRooms(pos, zombie.Radius()); // дэлгүүрийн үүдээр орохгүй

            // Бай нь ойрхон (барилга дотор нуугдсан байж болно) үед дэргэдэх хаалттай хаалгыг нүднэ.
            if (targetDistance < 14.0f)
            {
                world.PoundDoors(pos, dt);
            }
        }

        // Намхан саад дээр авирах, ирмэгээс буух
        const float floor = world.SurfaceHeightAt(pos.x, pos.z, pos.y + kClimbHeight);
        pos.y = pos.y < floor ? std::min(floor, pos.y + 4.0f * dt) : std::max(floor, pos.y - 12.0f * dt);

        // --- Дайралт ---
        const bool inReach = targetDistance < reach && std::fabs(targetPosition.y - pos.y) < 1.6f;
        if (inReach && zombie.attackCooldown <= 0.0f && zombie.stagger <= 0.0f)
        {
            zombie.attackCooldown = kAttackInterval;
            zombie.attackAnim = kAttackAnimTime;
            Audio::Play(zombie.boss ? Sfx::BossRoar : Sfx::ZombieAttack, Vector3Add(pos, { 0.0f, 1.6f * zombie.scale, 0.0f }), 0.65f,
                        zombie.type == ZombieType::Brute ? 0.8f : 1.0f);
            if (target == kPlayer)
            {
                player.TakeDamage(zombie.damage);
            }
            else if (target == kRemote)
            {
                remoteDamage += zombie.damage;
            }
            else
            {
                squad.Damage(target, zombie.damage);
            }
        }
    }

    // --- Амьд зомбинууд бие биетэйгээ давхцахгүй ---
    for (size_t i = 0; i < zombies.size(); ++i)
    {
        if (!zombies[i].IsAlive()) continue;
        for (size_t j = i + 1; j < zombies.size(); ++j)
        {
            if (!zombies[j].IsAlive()) continue;

            Vector3& a = zombies[i].mover.position;
            Vector3& b = zombies[j].mover.position;
            const float minDistance = zombies[i].Radius() + zombies[j].Radius();
            const float distance = HorizontalDistance(a, b);
            if (distance < minDistance && distance > 1e-4f)
            {
                const float push = (minDistance - distance) * 0.5f / distance;
                const float dx = (a.x - b.x) * push;
                const float dz = (a.z - b.z) * push;
                a.x += dx;
                a.z += dz;
                b.x -= dx;
                b.z -= dz;
            }
        }
    }
}

ZombieHit ZombieHorde::Raycast(Ray ray, float minDistance, float maxDistance) const
{
    ZombieHit best;
    best.distance = maxDistance;

    for (int i = 0; i < (int)zombies.size(); ++i)
    {
        const Zombie& zombie = zombies[i];
        if (!zombie.IsAlive()) continue;

        // Оногдлын хүрээ харагдах загварт тааруулсан: том зомби мөр, гартайгаа хамт өргөн; алхахдаа
        // урагш бөхийдөг тул хүрээ харах зүг рүүгээ бага зэрэг шилжинэ (эс бөгөөс биеийг онилсон сум нэвт гарна).
        const float widen = zombie.type == ZombieType::Brute ? 1.9f : 1.2f;
        const float r = zombie.Radius() * widen;
        const float forward = 0.18f * zombie.scale;
        const Vector3 p = { zombie.mover.position.x + std::sin(zombie.mover.facing) * forward, zombie.mover.position.y,
                            zombie.mover.position.z + std::cos(zombie.mover.facing) * forward };
        const BoundingBox box = { { p.x - r, p.y, p.z - r }, { p.x + r, p.y + zombie.Height(), p.z + r } };

        const RayCollision hit = GetRayCollisionBox(ray, box);
        if (hit.hit && hit.distance >= minDistance && hit.distance < best.distance)
        {
            best.index = i;
            best.distance = hit.distance;
            // Толгой: хүзүүнээс дээш, биеийн голын хэсэгт (мөрний хажуу, гарыг толгой гэж тооцохгүй)
            const float side = std::fabs((hit.point.x - p.x) * std::cos(zombie.mover.facing) -
                                         (hit.point.z - p.z) * std::sin(zombie.mover.facing));
            best.headshot = hit.point.y > p.y + zombie.Height() * 0.8f && side < zombie.Radius() * 0.75f;
        }
    }
    return best;
}

int ZombieHorde::Nearest(Vector3 position, float maxDistance) const
{
    int best = -1;
    float bestDistance = maxDistance;
    for (int i = 0; i < (int)zombies.size(); ++i)
    {
        if (!zombies[i].IsAlive()) continue;
        const float distance = HorizontalDistance(position, zombies[i].mover.position);
        if (distance < bestDistance)
        {
            best = i;
            bestDistance = distance;
        }
    }
    return best;
}

void ZombieHorde::Damage(int index, float amount)
{
    Zombie& zombie = zombies[index];
    if (!zombie.IsAlive() || puppet) // зочин: хохирлыг хост тооцно
    {
        return;
    }

    zombie.health -= amount;
    zombie.alerted = true;
    zombie.stagger = zombie.boss ? 0.0f : kStaggerTime; // аварга цочирдохгүй
    if (!zombie.IsAlive())
    {
        zombie.health = 0.0f;
        zombie.deathTimer = 0.0f;
        Audio::Play(zombie.boss ? Sfx::BossRoar : Sfx::ZombieDie, Vector3Add(zombie.mover.position, { 0.0f, 1.4f * zombie.scale, 0.0f }),
                    0.9f, zombie.boss ? 0.7f : zombie.type == ZombieType::Brute ? 0.8f : 1.0f);
        pendingCoins += zombie.reward;
        ++pendingKills;
        if (killRewards.size() < 64)
        {
            const float top = 1.9f * zombie.scale;
            killRewards.push_back({ { zombie.mover.position.x, zombie.mover.position.y + top, zombie.mover.position.z },
                                    zombie.reward });
        }
        if (!zombie.boss)
        {
            ++(zombie.type == ZombieType::Brute ? pendingBigKills : pendingSmallKills);
        }
    }
}

void ZombieHorde::DamageFraction(int index, float fraction)
{
    if (zombies[index].boss)
    {
        fraction *= 0.12f; // аваргыг хутгаар цөөн цохилтоор алж болохгүй
    }
    // Бутархайн алдаанаас болж сүүлчийн цохилтод өчүүхэн амь үлдэхээс сэргийлж бага зэрэг нэмнэ.
    Damage(index, zombies[index].maxHealth * fraction + 0.01f);
}

int ZombieHorde::NearestInFront(Vector3 position, float facing, float reach) const
{
    const float forwardX = std::sin(facing), forwardZ = std::cos(facing);
    int best = -1;
    float bestDistance = 1e9f;

    for (int i = 0; i < (int)zombies.size(); ++i)
    {
        const Zombie& zombie = zombies[i];
        if (!zombie.IsAlive()) continue;

        const float dx = zombie.mover.position.x - position.x;
        const float dz = zombie.mover.position.z - position.z;
        const float distance = std::sqrt(dx * dx + dz * dz);
        if (distance > reach + zombie.Radius() || std::fabs(zombie.mover.position.y - position.y) > 1.6f) continue;

        // Урд талын 120 градусын дотор байх ёстой (шахам зогссон бол чиглэл хамаарахгүй)
        const bool inFront = distance < 0.6f || (dx * forwardX + dz * forwardZ) / distance > 0.5f;
        if (inFront && distance < bestDistance)
        {
            best = i;
            bestDistance = distance;
        }
    }
    return best;
}

int ZombieHorde::Explode(Vector3 center, float radius, float maxDamage)
{
    int killed = 0;
    for (int i = 0; i < (int)zombies.size(); ++i)
    {
        if (!zombies[i].IsAlive()) continue;

        const Vector3 chest = zombies[i].Chest();
        const float dx = chest.x - center.x, dy = chest.y - center.y, dz = chest.z - center.z;
        const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (distance >= radius) continue;

        Damage(i, maxDamage * (1.0f - distance / radius));
        if (!zombies[i].IsAlive()) ++killed;
    }
    return killed;
}

int ZombieHorde::Ram(Vector3 center, float radius, float damage, bool launch, Vector3 push)
{
    if (puppet) return 0; // зочин: мөргөлтийг хост тооцно (эс бөгөөс амьд зомби агаарт шидэгдэнэ)
    int touched = 0;
    for (int i = 0; i < (int)zombies.size(); ++i)
    {
        Zombie& zombie = zombies[i];
        if (!zombie.IsAlive()) continue;
        if (HorizontalDistance(center, zombie.mover.position) > radius + zombie.Radius()) continue;
        if (std::fabs(zombie.mover.position.y - center.y) > 2.0f) continue;

        ++touched;
        if (!launch || zombie.boss)
        {
            // Аварга хэт хүнд тул шидэгдэхгүй, зөвхөн гэмтэнэ.
            Damage(i, zombie.boss ? damage * 4.0f : damage);
            continue;
        }

        // Хүчтэй мөргөлт: шууд үхэж, унааны явсан зүг рүү дээш шидэгдэнэ. Хажуу тийш бага зэрэг хазайна.
        Damage(i, zombie.health + 1.0f);
        const float speed = std::sqrt(push.x * push.x + push.z * push.z);
        const float sideX = zombie.mover.position.x - center.x, sideZ = zombie.mover.position.z - center.z;
        zombie.airborne = true;
        zombie.flight = { push.x * 0.75f + sideX * 1.5f, 3.5f + speed * 0.22f, push.z * 0.75f + sideZ * 1.5f };
        zombie.tumble = 0.0f;
        zombie.mover.facing = std::atan2(-push.x, -push.z); // унаа руу харж байгаад хойшоо ниснэ
    }
    return touched;
}

void ZombieHorde::TakeRewards(int& coins, int& kills)
{
    coins += pendingCoins;
    kills += pendingKills;
    pendingCoins = 0;
    pendingKills = 0;
}

int ZombieHorde::AliveCount() const
{
    return (int)std::count_if(zombies.begin(), zombies.end(), [](const Zombie& z) { return z.IsAlive(); });
}

void ZombieHorde::Draw(Vector3 viewPosition, float maxDistance, bool showColliders) const
{
    // Загвараар зурах зайн хязгаар: хамгийн ойрын kModelBudget зомбины хамгийн холынх хүртэл.
    float modelDistance = kModelDistance;
    if ((int)zombies.size() > kModelBudget)
    {
        static std::vector<float> distances;
        distances.clear();
        for (const Zombie& zombie : zombies)
        {
            distances.push_back(HorizontalDistance(viewPosition, zombie.mover.position));
        }
        std::nth_element(distances.begin(), distances.begin() + kModelBudget, distances.end());
        modelDistance = std::min(modelDistance, distances[(size_t)kModelBudget]);
    }

    for (const Zombie& zombie : zombies)
    {
        const float viewDistance = HorizontalDistance(viewPosition, zombie.mover.position);
        if (viewDistance > maxDistance) continue;
        const bool detailed = viewDistance < kHumanoidDetailDistance;

        Vector3 position = zombie.mover.position;
        if (zombie.IsAlive())
        {
            DrawBlobShadow(position, zombie.Radius() * 1.3f);
        }
        else
        {
            // Цогцос төгсгөлд нь газарт шингэж алга болно.
            const float sink = std::max(0.0f, zombie.deathTimer - (kCorpseTime - kSinkTime)) / kSinkTime;
            position.y -= sink * 0.5f;
        }

        HumanoidPose pose = PoseFor(zombie);
        if (zombie.airborne)
        {
            // Агаарт: гар хөл нь сарвайж, бие нь эргэлдэнэ.
            pose.fall = 0.0f;
            pose.spin = zombie.tumble;
            pose.shoulder[0] = 120.0f;
            pose.shoulder[1] = 150.0f;
            pose.hip[0] = 40.0f;
            pose.hip[1] = -25.0f;
            pose.knee[0] = 50.0f;
            pose.knee[1] = 30.0f;
        }
        HumanoidLook look = LookFor(zombie);
        if (viewDistance >= kSimpleDistance)
        {
            look.model = -1; // маш хол (манан дунд жижиг): хамгийн хөнгөн кодоор үүсгэсэн дүрс
        }
        else if (viewDistance >= modelDistance)
        {
            look.lowDetail = true; // холын зомби: загварын хөнгөн хувилбар (дөрвөлжин дүрс биш)
        }
        DrawHumanoid(position, zombie.mover.facing, zombie.scale, pose, look, detailed);

        if (showColliders && zombie.IsAlive())
        {
            DrawCylinderWires(zombie.mover.position, zombie.Radius(), zombie.Radius(), zombie.Height(), 10, RED);
        }
    }
}

// ---------------------------------------------------------------- Хамтарсан тоглолт

bool ZombieHorde::DamageById(uint32_t id, float amount, int& coins, Vector3& position)
{
    for (int i = 0; i < (int)zombies.size(); ++i)
    {
        Zombie& zombie = zombies[(size_t)i];
        if (zombie.id != id || !zombie.IsAlive()) continue;
        Damage(i, amount);
        if (!zombie.IsAlive())
        {
            coins = zombie.reward;
            position = { zombie.mover.position.x, zombie.mover.position.y + 1.9f * zombie.scale, zombie.mover.position.z };
            return true;
        }
        return false;
    }
    return false;
}

void ZombieHorde::CollectStates(Vector3 around, float radius, std::vector<Net::ZombieState>& out) const
{
    // Пакет хэт томрохгүйн тулд хязгаартай. Дөнгөж үхсэн зомби (үхэх хөдөлгөөнийг зочин заавал харах ёстой)
    // эхэнд, дараа нь амьд зомбинууд ойроос нь.
    const size_t kMaxStates = 110;
    std::vector<std::pair<float, const Zombie*>> order;
    for (const Zombie& zombie : zombies)
    {
        if (!zombie.IsAlive() && zombie.deathTimer > 1.5f) continue; // хуучин цогцос: зочин өөрөө хэвтүүлнэ
        const float distance = HorizontalDistance(around, zombie.mover.position);
        if (distance > radius) continue;
        order.push_back({ zombie.IsAlive() ? distance : -1.0f, &zombie });
    }
    std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    if (order.size() > kMaxStates) order.resize(kMaxStates);

    out.clear();
    for (const auto& entry : order)
    {
        const Zombie& zombie = *entry.second;
        Net::ZombieState state;
        state.id = zombie.id;
        state.type = (uint8_t)zombie.type;
        state.flags = (uint8_t)((zombie.IsAlive() ? Net::kZombieAlive : 0) | (zombie.boss ? Net::kZombieBoss : 0) |
                                (zombie.attackAnim > 0.0f ? Net::kZombieAttack : 0));
        state.position = zombie.mover.position;
        state.facing = zombie.mover.facing;
        out.push_back(state);
    }
}

void ZombieHorde::ApplySnapshot(const std::vector<Net::ZombieState>& states, const MissionParams& mission)
{
    for (const Net::ZombieState& state : states)
    {
        Zombie* zombie = nullptr;
        for (Zombie& candidate : zombies)
        {
            if (candidate.id == state.id) { zombie = &candidate; break; }
        }
        const bool alive = (state.flags & Net::kZombieAlive) != 0;
        if (!zombie)
        {
            if (!alive) continue;
            const uint32_t keepNext = nextId;
            if (state.flags & Net::kZombieBoss) SpawnBoss(state.position, mission, 0);
            else Spawn((ZombieType)state.type, state.position, mission, 0, true);
            nextId = keepNext;
            zombie = &zombies.back();
            zombie->id = state.id;
            zombie->mover.facing = state.facing;
        }
        zombie->netTarget = state.position;
        zombie->netFacing = state.facing;
        zombie->netSeen = 0.0f;
        if (!alive && zombie->IsAlive())
        {
            zombie->health = 0.0f;
            zombie->deathTimer = 0.0f;
        }
        if ((state.flags & Net::kZombieAttack) && zombie->attackAnim <= 0.0f)
        {
            zombie->attackAnim = kAttackAnimTime;
        }
    }
}

void ZombieHorde::UpdatePuppets(float dt)
{
    zombies.erase(std::remove_if(zombies.begin(), zombies.end(),
                                 [](const Zombie& z) {
                                     return (!z.IsAlive() && z.deathTimer > kCorpseTime) || (z.IsAlive() && z.netSeen > 1.5f);
                                 }),
                  zombies.end());
    for (Zombie& zombie : zombies)
    {
        AdvanceGait(zombie, dt);
        zombie.netSeen += dt;
        zombie.attackAnim -= dt;
        if (!zombie.IsAlive())
        {
            zombie.deathTimer += dt;
            continue;
        }
        Vector3& pos = zombie.mover.position;
        const Vector3 delta = Vector3Subtract(zombie.netTarget, pos);
        const float distance = Vector3Length(delta);
        if (distance > 8.0f)
        {
            pos = zombie.netTarget; // хэт хол хоцорсон: шууд байрлуулна
        }
        else
        {
            const Vector3 step = Vector3Scale(delta, std::min(1.0f, 10.0f * dt));
            pos = Vector3Add(pos, step);
        }
        zombie.mover.facing += std::remainder(zombie.netFacing - zombie.mover.facing, 2.0f * PI) * std::min(1.0f, 10.0f * dt);
    }
}
