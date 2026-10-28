#include "ViewCull.h"
#include "Zombie.h"

#include "Audio.h"
#include "Bear.h"
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
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
    const int kModelBudget = 8;
    const float kSimpleDistance = 70.0f;  // үүнээс хол кодоор үүсгэсэн хамгийн хөнгөн дүрсээр (утас сул)
#else
    const int kModelBudget = 16;
    const float kSimpleDistance = 85.0f;  // үүнээс хол (манан дунд) кодоор үүсгэсэн хамгийн хөнгөн дүрсээр
#endif
    const float kAggroDistance = 60.0f; // ийм зайд бай орж ирэхэд зомби сэрэмжилж хөөнө
    // Баавгай: ийм ойр хүн / зомби ирвэл дайрна; уурласан үед хөөх зай; тэнүүчлэх хурд, хүрээ
    const float kBearAggro = 9.0f;
    const float kBearChase = 40.0f;
    const float kBearRageChase = 150.0f;
    const float kBearWalk = 1.1f;
    const float kBearRoam = 35.0f;
    const float kBearAttackInterval = 1.3f;
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
        // Баавгай: том зомбиос 2 дахин хүчтэй (амь, цохилт), дайрахдаа хурдан гүйнэ
        case ZombieType::Bear: return { 440.0f, 7.5f, 4.0f, 1.45f, 1.0f, 1.0f, 40 }; // том биетэй (урт ~2.9 м)
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

    // Онцлогийн гадаад төрх: бөөн нь хавдсан ногоон, орилогч цонхигор туранхай
    void ApplyTraitLook(Zombie& zombie)
    {
        if (zombie.exploder)
        {
            zombie.skin = { 118, 150, 64, 255 };
            zombie.shirtless = true;
            zombie.openRibs = false;
        }
        else if (zombie.screamer)
        {
            zombie.skin = { 214, 206, 196, 255 };
            zombie.hairStyle = 2;
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
        look.bulk = StatsFor(zombie.type).bulk * (zombie.boss ? 1.15f : zombie.exploder ? 1.4f : zombie.screamer ? 0.88f : 1.0f);

        // Гаднаас оруулсан загвар (байвал). Ижил төрлийн зомбинууд нэг загвартай тул өнгийг нь бага зэрэг
        // өөрчилж ялгана; аварга нь улаавтар.
        look.model = zombie.boss ? kCharacterZombieBoss
                   : zombie.type == ZombieType::Runner ? kCharacterZombieRunner
                   : zombie.type == ZombieType::Brute ? kCharacterZombieBrute : kCharacterZombieWalker;
        look.modelTint = zombie.boss ? Color{ 255, 255, 255, 255 } // аварга өөрийн загвартай: өнгөө хэвээр
                                     : Color{ (unsigned char)(200 + zombie.skin.r % 56), (unsigned char)(200 + zombie.skin.g % 56),
                                              (unsigned char)(200 + zombie.shirt.b % 56), 255 };
        if (zombie.exploder) look.modelTint = { 150, 225, 95, 255 };       // хорт ногоон
        else if (zombie.screamer) look.modelTint = { 255, 246, 236, 255 }; // цонхигор
        if (HasCharacterModel(look.model))
        {
            look.bulk = 1.0f + (look.bulk - 1.0f) * (zombie.exploder ? 0.8f : 0.4f); // загвар өөрөө бүдүүн, нарийн хэлбэртэй
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
    netClock = netLatest = -1.0f; // (шинэ хост: өөр цаг)
    pendingCoins = 0;
    pendingKills = 0;
    pendingSmallKills = 0;
    pendingBigKills = 0;
    for (auto& kills : pendingPoolKills) kills[0] = kills[1] = 0;
}

void ZombieHorde::AlertAround(Vector3 center, float radius)
{
    for (Zombie& zombie : zombies)
    {
        if (zombie.IsAlive() && !zombie.alerted && !zombie.IsBear() && HorizontalDistance(zombie.mover.position, center) < radius)
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

void ZombieHorde::TakePoolKills(int kills[kHordePools][2])
{
    for (int s = 0; s < kHordePools; ++s)
    {
        kills[s][0] += pendingPoolKills[s][0];
        kills[s][1] += pendingPoolKills[s][1];
        pendingPoolKills[s][0] = pendingPoolKills[s][1] = 0;
    }
}

void ZombieHorde::CountAlive(int& small, int& big) const
{
    for (const Zombie& zombie : zombies)
    {
        if (zombie.IsAlive() && !zombie.boss && !zombie.IsBear())
        {
            ++(zombie.type == ZombieType::Brute ? big : small);
        }
    }
}

void ZombieHorde::CountAliveByPool(int alive[kHordePools][2]) const
{
    for (int s = 0; s < kHordePools; ++s) alive[s][0] = alive[s][1] = 0;
    for (const Zombie& zombie : zombies)
    {
        if (zombie.IsAlive() && !zombie.boss && zombie.pool >= 0 && zombie.pool < kHordePools)
        {
            ++alive[zombie.pool][zombie.type == ZombieType::Brute ? 1 : 0];
        }
    }
}

void ZombieHorde::RemoveFar(const std::vector<Vector3>& centers, float distance, bool ambientOnly)
{
    zombies.erase(std::remove_if(zombies.begin(), zombies.end(),
                                 [&](const Zombie& z) {
                                     if (!z.IsAlive() || z.boss || z.IsBear() || (ambientOnly && z.zone != 0)) return false;
                                     for (const Vector3& center : centers)
                                     {
                                         if (HorizontalDistance(z.mover.position, center) <= distance) return false;
                                     }
                                     return true;
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

void ZombieHorde::SpawnBear(Vector3 position, const MissionParams& mission)
{
    Spawn(ZombieType::Bear, position, mission, -1, false, -1);
    Zombie& bear = zombies.back();
    bear.scale = 1.3f + (float)GetRandomValue(0, 30) / 100.0f; // (зочин: үндсэн 1.45)
    bear.home = position;
    bear.wanderGoal = position;
    bear.wanderTimer = (float)GetRandomValue(0, 60) / 10.0f;
    bear.mover.facing = (float)GetRandomValue(0, 359) * DEG2RAD;
}

void ZombieHorde::Spawn(ZombieType type, Vector3 position, const MissionParams& mission, int zone, bool alerted, int pool)
{
    const TypeStats stats = StatsFor(type);

    Zombie zombie;
    zombie.zone = zone;
    zombie.pool = pool;
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
    if (type == ZombieType::Walker)
    {
        const int roll = GetRandomValue(0, 99);
        zombie.exploder = roll < 6;
        zombie.screamer = roll >= 6 && roll < 11;
        if (zombie.exploder)
        {
            zombie.health *= 0.8f;
            zombie.maxHealth = zombie.health;
            zombie.damage *= 0.5f; // аюул нь дэлбэрэлт
            zombie.scale *= 1.08f;
            zombie.reward += 3;
        }
        else if (zombie.screamer)
        {
            zombie.speed *= 1.12f;
            zombie.reward += 2;
        }
        ApplyTraitLook(zombie);
    }
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

        if (zombie.IsBear())
        {
            UpdateBear(zombie, dt, world, player, squad);
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

        // Орилогч: бай руугаа хөөж эхэлмэгц (ойрхон бол) хашгирч, холын зомбиудыг дуудна (нэг удаа)
        zombie.screamTimer = std::max(0.0f, zombie.screamTimer - dt);
        if (zombie.screamer && !zombie.screamed && zombie.alerted && targetDistance < 35.0f)
        {
            zombie.screamed = true;
            zombie.screamTimer = 1.2f;
            zombie.stagger = 1.0f; // орилох зуураа зогсоно
            Audio::Play(Sfx::BossRoar, Vector3Add(pos, { 0.0f, 1.6f, 0.0f }), 1.0f, 1.75f);
            AlertAround(pos, 140.0f);
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
            const float arc = zombie.climbTop > 0.0f ? std::max(1.15f, zombie.climbTop + 0.1f - std::max(zombie.climbFrom.y, zombie.climbTo.y))
                                                     : 1.15f;
            pos.y += std::sin(t * PI) * arc;
            zombie.walkCycle += dt * 8.0f;
            if (zombie.climbTimer <= 0.0f)
            {
                pos = zombie.climbTo;
                zombie.climbTop = 0.0f;
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
            // Хашаанд тулсан (байдаа хөөж явахад): удаан авирч давна (дундуур нь нэвтрэхгүй)
            Passage over;
            if (!zombie.boss && zombie.mover.moved < zombie.mover.wishStep * 0.4f &&
                world.FindVault(pos, zombie.mover.wishDir, zombie.Radius() + 0.6f, zombie.Radius(), 3.0f, over))
            {
                zombie.climbTimer = kClimbTime * 1.4f;
                zombie.climbFrom = pos;
                zombie.climbTo = over.to;
                zombie.climbTop = over.top;
            }

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
                player.TakeDamageFrom(zombie.damage, pos);
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

void ZombieHorde::UpdateBear(Zombie& bear, float dt, World& world, Player& player, SoldierSquad& squad)
{
    Vector3& pos = bear.mover.position;
    bear.attackCooldown -= dt;
    bear.attackAnim -= dt;
    bear.stagger -= dt;
    bear.rage = std::max(0.0f, bear.rage - dt);

    // Хамгийн ойрын амьд биет: тоглогч, цэрэг, зочин, зомби (дэлгүүрт нуугдсан хүнийг үл тооно)
    const int kPlayer = -1, kNone = -2, kRemote = -3, kZombie = -4;
    int target = kNone, prey = -1;
    float best = 1e9f;
    Vector3 targetPosition = pos;
    auto sheltered = [&](Vector3 p) {
        const int room = world.InteriorAt(p.x, p.z);
        return room >= 0 && world.GetInterior(room).safe;
    };
    auto consider = [&](int who, Vector3 p, int index) {
        const float distance = HorizontalDistance(pos, p);
        if (distance < best)
        {
            best = distance;
            target = who;
            targetPosition = p;
            prey = index;
        }
    };
    if (player.IsAlive() && !sheltered(player.Position())) consider(kPlayer, player.Position(), -1);
    std::vector<Soldier>& soldiers = squad.All();
    for (int i = 0; i < (int)soldiers.size(); ++i)
    {
        if (soldiers[(size_t)i].alive && !sheltered(soldiers[(size_t)i].mover.position)) consider(i, soldiers[(size_t)i].mover.position, -1);
    }
    if (remoteActive && !sheltered(remotePosition)) consider(kRemote, remotePosition, -1);
    for (int i = 0; i < (int)zombies.size(); ++i)
    {
        const Zombie& other = zombies[(size_t)i];
        if (other.IsAlive() && !other.IsBear()) consider(kZombie, other.mover.position, i);
    }

    // Хэт ойртвол уурлана; уурласан баавгай байгаа холдвол (эсвэл хэн ч үгүй бол) хэдэн секундын дараа тайвширна
    if (target != kNone && best < kBearAggro * bear.scale) bear.alerted = true;
    const float chase = bear.rage > 0.0f ? kBearRageChase : kBearChase;
    if (bear.alerted && (target == kNone || best > chase))
    {
        bear.calmTimer += dt;
        if (bear.calmTimer > 5.0f)
        {
            bear.alerted = false;
            bear.calmTimer = 0.0f;
            bear.home = pos; // энд үлдэж тэнүүчилнэ
            bear.wanderTimer = 0.0f;
        }
    }
    else bear.calmTimer = 0.0f;

    if (bear.alerted && target != kNone && best <= chase)
    {
        const float reach = bear.Radius() + (prey >= 0 ? zombies[(size_t)prey].Radius() : 0.4f) + 0.5f;
        if (best > reach * 0.85f)
        {
            const float speed = bear.speed * (bear.stagger > 0.0f ? 0.3f : 1.0f);
            MoveToward(bear.mover, targetPosition, speed, dt, world, bear.Radius(), bear.Height(), kClimbHeight);
            world.KeepOutOfSafeRooms(pos, bear.Radius());
        }
        else
        {
            bear.mover.facing = std::atan2(targetPosition.x - pos.x, targetPosition.z - pos.z);
        }
        // Савраараа шүүрэх: хүн, зомби аль алинд нь
        if (best < reach && std::fabs(targetPosition.y - pos.y) < 1.6f && bear.attackCooldown <= 0.0f && bear.stagger <= 0.0f)
        {
            bear.attackCooldown = kBearAttackInterval;
            bear.attackAnim = kAttackAnimTime;
            Audio::Play(Sfx::BossRoar, Vector3Add(pos, { 0.0f, 1.2f, 0.0f }), 0.8f, 0.6f);
            if (target == kPlayer) player.TakeDamageFrom(bear.damage, pos);
            else if (target == kRemote) remoteDamage += bear.damage;
            else if (target == kZombie)
            {
                noCredit = true;
                Damage(prey, std::max(160.0f, bear.damage * 8.0f));
                noCredit = false;
            }
            else squad.Damage(target, bear.damage);
        }
    }
    else
    {
        // Тайван: төвийнхөө орчимд аажуухан тэнүүчилж, хааяа зогсож амарна (гүн ус руу орохгүй)
        bear.wanderTimer -= dt;
        if (bear.wanderTimer <= 0.0f)
        {
            bear.wanderTimer = 6.0f + (float)GetRandomValue(0, 80) / 10.0f;
            bear.wanderGoal = pos;
            if (GetRandomValue(0, 99) >= 35)
            {
                const float angle = (float)GetRandomValue(0, 359) * DEG2RAD;
                const float radius = (float)GetRandomValue(5, (int)kBearRoam);
                const Vector3 goal = { bear.home.x + std::sin(angle) * radius, pos.y, bear.home.z + std::cos(angle) * radius };
                if (world.WaterDepth(goal.x, goal.z) < 0.3f) bear.wanderGoal = goal;
            }
        }
        if (HorizontalDistance(pos, bear.wanderGoal) > 1.0f)
        {
            MoveToward(bear.mover, bear.wanderGoal, kBearWalk, dt, world, bear.Radius(), bear.Height(), kClimbHeight);
            world.KeepOutOfSafeRooms(pos, bear.Radius());
            if (bear.mover.moved < bear.mover.wishStep * 0.3f) bear.wanderGoal = pos; // тулсан: өөр зүг сонгоно
        }
    }

    const float floor = world.SurfaceHeightAt(pos.x, pos.z, pos.y + kClimbHeight);
    pos.y = pos.y < floor ? std::min(floor, pos.y + 4.0f * dt) : std::max(floor, pos.y - 12.0f * dt);
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
        const float widen = zombie.IsBear() ? 1.15f : zombie.type == ZombieType::Brute ? 1.9f : 1.2f;
        const float r = zombie.Radius() * widen;
        const float forward = zombie.IsBear() ? 0.0f : 0.18f * zombie.scale;
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
            best.headshot = !zombie.IsBear() && hit.point.y > p.y + zombie.Height() * 0.8f && side < zombie.Radius() * 0.75f;
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
        if (!zombies[i].IsAlive() || (zombies[i].IsBear() && !zombies[i].alerted)) continue;
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
    if (!zombie.IsAlive())
    {
        return;
    }
    if (puppet)
    {
        // Зочин: хохирол, шагналыг хост тооцно; хоцролтгүй харагдуулахын тулд үхлийг урьдчилан үзүүлнэ
        // (хост 1.5 секундын дотор батлахгүй бол буцаж амилна). Аварга зомби хостынхоор л.
        if (zombie.boss) return;
        zombie.health -= amount;
        zombie.stagger = kStaggerTime;
        if (zombie.health <= 0.0f)
        {
            zombie.health = 0.0f;
            zombie.deathTimer = 0.0f;
            zombie.predicted = 1.5f;
            if (zombie.exploder) exploderBlasts.push_back(zombie.Chest()); // (зочин: зөвхөн харагдана)
            Audio::Play(Sfx::ZombieDie, Vector3Add(zombie.mover.position, { 0.0f, 1.4f * zombie.scale, 0.0f }), 0.9f,
                        zombie.type == ZombieType::Brute ? 0.8f : 1.0f);
        }
        return;
    }

    zombie.health -= amount;
    zombie.alerted = true;
    zombie.stagger = zombie.boss ? 0.0f : zombie.IsBear() ? kStaggerTime * 0.4f : kStaggerTime; // аварга цочирдохгүй
    if (zombie.IsBear()) zombie.rage = 10.0f; // суманд оногдсон баавгай хол хөөнө
    if (!zombie.IsAlive())
    {
        zombie.health = 0.0f;
        zombie.deathTimer = 0.0f;
        if (zombie.exploder) exploderBlasts.push_back(zombie.Chest());
        Audio::Play(zombie.boss ? Sfx::BossRoar : Sfx::ZombieDie, Vector3Add(zombie.mover.position, { 0.0f, 1.4f * zombie.scale, 0.0f }),
                    0.9f, zombie.boss ? 0.7f : zombie.type == ZombieType::Brute ? 0.8f : 1.0f);
        const float top = 1.9f * zombie.scale;
        const KillReward reward = { { zombie.mover.position.x, zombie.mover.position.y + top, zombie.mover.position.z },
                                    zombie.reward };
        if (noCredit)
        {
            // баавгай алсан: зоос хэнд ч очихгүй
        }
        else if (creditRemote)
        {
            if (remoteRewards.size() < 64) remoteRewards.push_back(reward); // зочны унаа мөргөсөн
        }
        else
        {
            pendingCoins += zombie.reward;
            if (!zombie.IsBear()) ++pendingKills; // баавгай устгасан тоонд орохгүй
            if (killRewards.size() < 64) killRewards.push_back(reward);
        }
        if (zombie.IsBear()) return; // үеийн тоололд орохгүй
        if (!zombie.boss)
        {
            ++(zombie.type == ZombieType::Brute ? pendingBigKills : pendingSmallKills);
            if (zombie.pool >= 0 && zombie.pool < kHordePools) ++pendingPoolKills[zombie.pool][zombie.type == ZombieType::Brute ? 1 : 0];
        }
    }
}

void ZombieHorde::DamageFraction(int index, float fraction)
{
    if (zombies[index].boss)
    {
        fraction *= 0.12f; // аваргыг хутгаар цөөн цохилтоор алж болохгүй
    }
    else if (zombies[index].IsBear())
    {
        fraction *= 0.25f;
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

int ZombieHorde::FindId(uint32_t id) const
{
    for (int i = 0; i < (int)zombies.size(); ++i)
    {
        if (zombies[(size_t)i].id == id && zombies[(size_t)i].IsAlive()) return i;
    }
    return -1;
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
        if (!launch || zombie.boss || zombie.IsBear())
        {
            // Аварга, баавгай хэт хүнд тул шидэгдэхгүй, зөвхөн гэмтэнэ.
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
        if (viewDistance > maxDistance || !ViewCull::Visible(zombie.mover.position, 2.0f * zombie.scale)) continue;
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

        if (zombie.IsBear())
        {
            // Үхсэн баавгай хажуу тийшээ хөмрөөд хэвтэнэ
            const float dead = zombie.IsAlive() ? 0.0f : std::min(1.0f, zombie.deathTimer / 1.4f); // (баавгай удаан, хүндээр унана)
            const float attack = zombie.attackAnim > 0.0f ? zombie.attackAnim / kAttackAnimTime : 0.0f;
            DrawBear(position, zombie.mover.facing, zombie.scale, zombie.walkCycle, zombie.gaitSpeed, attack, dead,
                     viewDistance < kSimpleDistance, (float)(zombie.id % 97));
            if (showColliders && zombie.IsAlive())
            {
                DrawCylinderWires(zombie.mover.position, zombie.Radius(), zombie.Radius(), zombie.Height(), 10, RED);
            }
            continue;
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
        state.type = (uint8_t)((uint8_t)zombie.type | (zombie.exploder ? Net::kZombieExploder : 0) |
                               (zombie.screamer ? Net::kZombieScreamer : 0) | (zombie.screamTimer > 0.0f ? Net::kZombieScreaming : 0));
        state.flags = (uint8_t)((zombie.IsAlive() ? Net::kZombieAlive : 0) | (zombie.boss ? Net::kZombieBoss : 0) |
                                (zombie.attackAnim > 0.0f ? Net::kZombieAttack : 0) |
                                (zombie.airborne ? Net::kZombieAirborne : 0));
        if (zombie.attackAnim > 0.0f)
        {
            const int phase = std::clamp((int)std::lround(zombie.attackAnim / kAttackAnimTime * 15.0f), 1, 15);
            state.flags = (uint8_t)(state.flags | (phase << Net::kZombieAttackShift));
        }
        state.position = zombie.mover.position;
        state.facing = zombie.mover.facing;
        out.push_back(state);
    }
}

void ZombieHorde::ApplySnapshot(const std::vector<Net::ZombieState>& states, const MissionParams& mission, float sentAt)
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
            else Spawn((ZombieType)(state.type & 15), state.position, mission, 0, true);
            nextId = keepNext;
            zombie = &zombies.back();
            zombie->id = state.id;
            // Онцлогийг хостынхоор (энд санамсаргүй сонгогдсоныг дарна)
            zombie->exploder = (state.type & Net::kZombieExploder) != 0;
            zombie->screamer = (state.type & Net::kZombieScreamer) != 0;
            const float keepScale = zombie->scale;
            RandomizeAppearance(*zombie); // (хост дээрх шиг онцлоггүй гадаад төрхөөс эхэлнэ)
            zombie->scale = keepScale;
            ApplyTraitLook(*zombie);
            zombie->mover.facing = state.facing;
        }
        // Хурд: өмнөх snapshot-оос хойш явсан зай (snapshot хоорондын хугацаанд урагш таамаглахад)
        // Хугацаа: хостын илгээсэн цагуудын зөрүү (ирэх хугацааны савлагаа нөлөөлөхгүй); хуучин хост бол ирсэн цаг
        const float age = sentAt > 0.0f && zombie->netSent > 0.0f ? sentAt - zombie->netSent : zombie->netSeen;
        // Жигд хөдөлгөөн: өмнөх мэдээ (хүрэлцэхгүй хуучин бол буфергүй)
        zombie->netPrevSent = zombie->netSent > 0.0f && age > 0.0f && age < 0.6f ? zombie->netSent : -1.0f;
        zombie->netPrevTarget = zombie->netTarget;
        zombie->netSent = sentAt;
        if (sentAt > netLatest) netLatest = sentAt;
        if (age > 0.01f && age < 0.5f && alive)
        {
            Vector3 velocity = Vector3Scale(Vector3Subtract(state.position, zombie->netTarget), 1.0f / age);
            velocity.y = 0.0f;
            const float speed = Vector3Length(velocity);
            if (speed > 10.0f) velocity = Vector3Scale(velocity, 10.0f / speed);
            zombie->netVelocity = Vector3Lerp(zombie->netVelocity, velocity, 0.6f);
        }
        else
        {
            zombie->netVelocity = { 0.0f, 0.0f, 0.0f };
        }
        zombie->netTarget = state.position;
        zombie->netFacing = state.facing;
        zombie->netSeen = 0.0f;
        if (!alive && zombie->IsAlive())
        {
            zombie->health = 0.0f;
            zombie->deathTimer = 0.0f;
            if (zombie->exploder) exploderBlasts.push_back(zombie->Chest()); // зочин: дэлбэрэлт харагдана (хохирлыг хост)
        }
        if ((state.type & Net::kZombieScreaming) && zombie->screamTimer <= 0.0f && !zombie->screamed)
        {
            zombie->screamed = true;
            zombie->screamTimer = 1.2f;
            Audio::Play(Sfx::BossRoar, Vector3Add(zombie->mover.position, { 0.0f, 1.6f, 0.0f }), 1.0f, 1.75f);
        }
        else if (alive && !zombie->IsAlive() && zombie->predicted <= 0.0f)
        {
            // Урьдчилан үхүүлсэн ч хост амьд гэж үзэв (бусад хохирол тооцогдоогүй г.м.): буцааж амилуулна
            zombie->health = std::max(1.0f, zombie->maxHealth * 0.25f);
            zombie->deathTimer = 0.0f;
        }
        if (!alive) zombie->predicted = 0.0f;
        // Унаанд мөргүүлж нисэж буй: зочин хостын байрлалыг дагаж эргэлдүүлж зурна, газардмагц хэвтэнэ.
        const bool airborne = !alive && (state.flags & Net::kZombieAirborne) != 0;
        if (zombie->airborne && !airborne) zombie->deathTimer = std::max(zombie->deathTimer, kFallTime);
        zombie->airborne = airborne;
        // Шинэ цохилт: хостын үлдсэн хугацаагаар эхлүүлнэ (хоцролтгүй, давхар цохихгүй)
        const bool attacking = (state.flags & Net::kZombieAttack) != 0;
        if (attacking && !zombie->netAttack)
        {
            const int phase = (state.flags >> Net::kZombieAttackShift) & 15;
            zombie->attackAnim = phase > 0 ? kAttackAnimTime * (float)phase / 15.0f : kAttackAnimTime;
            zombie->netVelocity = { 0.0f, 0.0f, 0.0f };
        }
        zombie->netAttack = attacking;
    }
}

void ZombieHorde::UpdatePuppets(float dt)
{
    // Хостын цаг (0.1 с хоцроосон): мэдээний хооронд жигд шилжүүлэх мөч
    if (netLatest > 0.0f)
    {
        const float wanted = netLatest - 0.1f;
        if (netClock < 0.0f || std::fabs(netClock - wanted) > 0.5f) netClock = wanted;
        else netClock += dt + (wanted - netClock) * std::min(1.0f, 1.5f * dt);
    }
    for (Zombie& zombie : zombies) zombie.predicted = std::max(0.0f, zombie.predicted - dt);
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
        Vector3& pos = zombie.mover.position;
        if (!zombie.IsAlive())
        {
            zombie.deathTimer += dt;
            if (zombie.airborne)
            {
                pos = Vector3Add(pos, Vector3Scale(Vector3Subtract(zombie.netTarget, pos), std::min(1.0f, 12.0f * dt)));
                zombie.tumble += 420.0f * dt;
            }
            continue;
        }
        // Хостын цагаар 0.1 с хоцроосон мөчид хоёр мэдээний хооронд (жигд); хоцорсон бол хурдаар урагш, хуучин хост бол
        // сүүлийн snapshot-оос хурдаар таамаглана (0.25 с-ээс илүүг таамаглахгүй).
        Vector3 predicted = Vector3Add(zombie.netTarget, Vector3Scale(zombie.netVelocity, std::min(zombie.netSeen, 0.25f)));
        if (netClock > 0.0f && zombie.netPrevSent > 0.0f && zombie.netSent > zombie.netPrevSent)
        {
            const float t = netClock;
            if (t <= zombie.netPrevSent) predicted = zombie.netPrevTarget;
            else if (t <= zombie.netSent)
            {
                predicted = Vector3Lerp(zombie.netPrevTarget, zombie.netTarget, (t - zombie.netPrevSent) / (zombie.netSent - zombie.netPrevSent));
            }
            else predicted = Vector3Add(zombie.netTarget, Vector3Scale(zombie.netVelocity, std::min(t - zombie.netSent, 0.15f)));
            pos.x = predicted.x;
            pos.z = predicted.z;
            pos.y += (predicted.y - pos.y) * std::min(1.0f, 15.0f * dt);
            zombie.mover.facing += std::remainder(zombie.netFacing - zombie.mover.facing, 2.0f * PI) * std::min(1.0f, 10.0f * dt);
            continue;
        }
        const Vector3 delta = Vector3Subtract(predicted, pos);
        const float distance = Vector3Length(delta);
        if (distance > 6.0f)
        {
            pos = predicted; // хэт хол хоцорсон: шууд байрлуулна
        }
        else
        {
            pos = Vector3Add(pos, Vector3Scale(delta, std::min(1.0f, 15.0f * dt)));
        }
        zombie.mover.facing += std::remainder(zombie.netFacing - zombie.mover.facing, 2.0f * PI) * std::min(1.0f, 10.0f * dt);
    }
}
