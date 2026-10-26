#include "Player.h"

#include "Audio.h"
#include "Input.h"

#include "Humanoid.h"
#include "Mover.h"
#include "Team.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>

namespace
{
    // Цонх давах хөдөлгөөний түлхүүр агшин. Хөлийн байрлал:
    //   wall  - цонхны ханын төвөөс замын дагуу хэдэн метр (сөрөг = эхлэх тал, эерэг = хүрэх тал);
    //           -1 = эхлэх цэг, 1.2 = хүрэх цэг (бодит зай нь гарц бүрт өөр байж болно: VaultDistance)
    //   level - 0 = эхлэх шал, 1 = цонхны тавцан, 2 = хүрэх цэг (газар, эсвэл 2 давхраас зүүгдэх өндөр)
    //   lift  - нэмэлт өндөр, метр; turn - биеийн эргэлт, градус (180 = хана руу харсан)
    // Мөч: hip, knee [баруун, зүүн]; их биеийн бөхийлт, мөр (+ = урагш), тохой, толгой (+ = доош).
    // Гар: grip > 0 үед гар тавцан / хүрээн дээрх цэгт тулна: ханын төвөөс handWall метр, тавцангаас handUp
    //   метр дээр, биеийн төвөөс хажуу тийш handSide метр.
    struct VaultKey
    {
        float t, wall, level, lift, turn;
        float hip[2], knee[2];
        float lean, shoulder, elbow, head;
        float grip, handWall, handUp, handSide;
    };

    // 1 давхрын цонх: гараа тавцанд тавиад өвдгөө гаргаж, цонхонд бөхийн суугаад хүрээнээс түлхэж үсэрнэ.
    const VaultKey kWindowKeys[] = {
        { 0.00f, -1.00f, 0.00f, 0.00f, 0.0f, { 0.0f, 0.0f }, { 4.0f, 4.0f }, 0.0f, 8.0f, 14.0f, 0.0f, 0.0f, -0.08f, 0.0f, 0.3f },
        // Хананд ойртож, хоёр гараа тавцангийн ирмэгт тавиад үсрэхээр суугаад авна
        { 0.14f, -0.42f, 0.00f, 0.00f, 0.0f, { 24.0f, 16.0f }, { 46.0f, 34.0f }, 26.0f, 70.0f, 30.0f, -4.0f, 1.0f, -0.08f, 0.0f, 0.3f },
        // Гараараа түлхэж биеэ өргөөд баруун өвдгөө тавцан дээр тавина: гуя урагш, шилбэ тавцангийн дагуу хэвтээ
        // (бие хананаас гуяны уртаар хол, эс бөгөөс өвдөг хана нэвтэлнэ), зүүн хөл унжина
        { 0.32f, -0.46f, 0.24f, 0.00f, 0.0f, { 70.0f, 10.0f }, { 160.0f, 22.0f }, 30.0f, 30.0f, 70.0f, 10.0f, 1.0f, -0.06f, 0.0f, 0.3f },
        // Тулсан хөлөө өвдгөөр нь хойш эвхэж (хөл хана мөргөхгүй) биеэ тавцан руу өргөнө
        { 0.41f, -0.40f, 0.70f, 0.00f, 0.0f, { 74.0f, 34.0f }, { 156.0f, 112.0f }, 38.0f, 26.0f, 50.0f, 14.0f, 1.0f, -0.04f, 0.0f, 0.3f },
        // Тавцан дээр бөхийж сууна (өгзөг дотор талд, ул тавцан дээр), хоёр гараараа цонхны хүрээнээс барина
        { 0.50f, -0.28f, 1.00f, 0.00f, 0.0f, { 104.0f, 98.0f }, { 130.0f, 124.0f }, 40.0f, 26.0f, 32.0f, 22.0f, 1.0f, 0.02f, 0.72f, 0.56f },
        // Хүрээнээс түлхэж нөгөө тал руу гишгэнэ
        { 0.62f, 0.25f, 1.08f, 0.06f, 0.0f, { 72.0f, 98.0f }, { 92.0f, 122.0f }, 30.0f, 60.0f, 30.0f, 12.0f, 0.6f, 0.06f, 0.72f, 0.56f },
        // Үсрэлт: хөл газар руу сунаж, гар тэнцвэр барина
        { 0.72f, 0.55f, 1.45f, 0.10f, 0.0f, { 40.0f, 26.0f }, { 52.0f, 34.0f }, 14.0f, 58.0f, 30.0f, 6.0f, 0.0f, 0.06f, 0.72f, 0.56f },
        // Буухдаа өвдгөө нугалж цохилтоо зөөлрүүлнэ
        { 0.84f, 1.20f, 2.00f, 0.00f, 0.0f, { 72.0f, 60.0f }, { 108.0f, 96.0f }, 30.0f, 44.0f, 36.0f, 10.0f, 0.0f, 0.0f, 0.0f, 0.3f },
        // Босно
        { 1.00f, 1.20f, 2.00f, 0.00f, 0.0f, { 4.0f, 4.0f }, { 8.0f, 8.0f }, 4.0f, 10.0f, 20.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.3f },
    };

    // 2 давхрын цонх: тавцан дээр гарч суугаад хана руу эргэж, гараараа тавцангаас барьсан чигээр хөлөө
    // доош хийж суларна, гараа тэнийлгэн зүүгдэнэ (тэндээс газарт үсэрнэ).
    const VaultKey kDropKeys[] = {
        { 0.00f, -1.00f, 0.00f, 0.00f, 0.0f, { 0.0f, 0.0f }, { 4.0f, 4.0f }, 0.0f, 8.0f, 14.0f, 0.0f, 0.0f, -0.08f, 0.0f, 0.3f },
        { 0.10f, -0.42f, 0.00f, 0.00f, 0.0f, { 24.0f, 16.0f }, { 46.0f, 34.0f }, 26.0f, 70.0f, 30.0f, -4.0f, 1.0f, -0.08f, 0.0f, 0.3f },
        { 0.22f, -0.46f, 0.24f, 0.00f, 0.0f, { 70.0f, 10.0f }, { 160.0f, 22.0f }, 30.0f, 30.0f, 70.0f, 10.0f, 1.0f, -0.06f, 0.0f, 0.3f },
        { 0.28f, -0.40f, 0.70f, 0.00f, 0.0f, { 74.0f, 34.0f }, { 156.0f, 112.0f }, 38.0f, 26.0f, 50.0f, 14.0f, 1.0f, -0.04f, 0.0f, 0.3f },
        // Тавцан дээр бөхийж сууна, хүрээнээс барина; доошоо харна
        { 0.34f, -0.28f, 1.00f, 0.00f, 0.0f, { 104.0f, 98.0f }, { 130.0f, 124.0f }, 40.0f, 26.0f, 32.0f, 30.0f, 1.0f, 0.02f, 0.72f, 0.56f },
        // Хүрээг суллаж, бөхийсөн чигээрээ хана руу эргэнэ
        { 0.42f, -0.05f, 1.00f, 0.00f, 90.0f, { 104.0f, 98.0f }, { 130.0f, 124.0f }, 46.0f, 60.0f, 40.0f, 20.0f, 0.0f, 0.0f, 0.0f, 0.3f },
        // Хана руу харсан (ул тавцан дээрээ эргэнэ): хоёр гараа тавцангийн гадна ирмэгт тавина
        { 0.50f, 0.18f, 1.00f, 0.00f, 180.0f, { 104.0f, 98.0f }, { 130.0f, 124.0f }, 46.0f, 40.0f, 40.0f, 24.0f, 1.0f, 0.06f, 0.0f, 0.3f },
        // Хөлөө ар тийш тавцангаас гаргаж, тохойгоо нугалан доош суларна; өвдөг, ул хананы гадна талд тулна
        { 0.64f, 0.44f, 1.45f, 0.00f, 180.0f, { 34.0f, 26.0f }, { 66.0f, 54.0f }, 14.0f, 140.0f, 80.0f, 12.0f, 1.0f, 0.08f, 0.0f, 0.28f },
        // Гараа тэнийлгэн зүүгдэнэ, хөл унжина
        { 0.80f, 0.30f, 2.00f, 0.00f, 180.0f, { 8.0f, 2.0f }, { 18.0f, 10.0f }, -4.0f, 175.0f, 8.0f, 6.0f, 1.0f, 0.08f, 0.0f, 0.26f },
        // Доошоо харж үсрэх газраа шалгана
        { 1.00f, 0.30f, 2.00f, -0.02f, 180.0f, { 4.0f, 0.0f }, { 14.0f, 8.0f }, -6.0f, 178.0f, 4.0f, 28.0f, 1.0f, 0.08f, 0.0f, 0.26f },
    };

    template <size_t N>
    VaultKey SampleKeys(const VaultKey (&keys)[N], float t)
    {
        size_t i = 0;
        while (i + 2 < N && t > keys[i + 1].t) ++i;
        const VaultKey& a = keys[i];
        const VaultKey& b = keys[i + 1];
        float u = std::clamp((t - a.t) / (b.t - a.t), 0.0f, 1.0f);
        u = u * u * (3.0f - 2.0f * u);
        auto mix = [u](float x, float y) { return x + (y - x) * u; };
        VaultKey k;
        k.t = t;
        k.wall = mix(a.wall, b.wall);
        k.level = mix(a.level, b.level);
        k.lift = mix(a.lift, b.lift);
        k.turn = mix(a.turn, b.turn);
        for (int side = 0; side < 2; ++side)
        {
            k.hip[side] = mix(a.hip[side], b.hip[side]);
            k.knee[side] = mix(a.knee[side], b.knee[side]);
        }
        k.lean = mix(a.lean, b.lean);
        k.shoulder = mix(a.shoulder, b.shoulder);
        k.elbow = mix(a.elbow, b.elbow);
        k.head = mix(a.head, b.head);
        k.grip = mix(a.grip, b.grip);
        k.handWall = mix(a.handWall, b.handWall);
        k.handUp = mix(a.handUp, b.handUp);
        k.handSide = mix(a.handSide, b.handSide);
        return k;
    }

    bool IsWindow(PassageKind kind)
    {
        return kind == PassageKind::WindowIn || kind == PassageKind::WindowOut || kind == PassageKind::WindowDrop ||
               kind == PassageKind::FenceOver;
    }

    VaultKey SampleVault(PassageKind kind, float t)
    {
        return kind == PassageKind::WindowDrop ? SampleKeys(kDropKeys, t) : SampleKeys(kWindowKeys, t);
    }

    // Түлхүүр агшны ханаас хэмжсэн зайг тухайн цонхны бодит зайд тааруулна: эхлэх, хүрэх цэг тавилга, хашааны
    // улмаас ханаас хол байж болно (World::FitWindowPassages). Ойртох алхам эхлэх цэгээс, үсрэлт хүрэх цэг хүртэл.
    float VaultDistance(const Passage& passage, float keyWall, float total)
    {
        if (keyWall < -0.42f)
        {
            return Lerp(-passage.wall, -0.42f, (keyWall + 1.0f) / 0.58f);
        }
        if (keyWall > 0.55f && passage.kind != PassageKind::WindowDrop)
        {
            return Lerp(0.55f, total - passage.wall, (keyWall - 0.55f) / 0.65f);
        }
        return keyWall;
    }

    // Цонхны тавцангийн өндөр (Structures.cpp-ийн sillHeight = 1.05 давхрын суурнаас; 1 давхарт дотор шал
    // суурнаас 0.1 м өндөр, 2 давхарт эхлэх цэг нь давхрын суурь өөрөө).
    float VaultSill(const Passage& passage)
    {
        if (passage.kind == PassageKind::FenceOver) return passage.top + 0.05f; // хашааны орой дээгүүр
        return passage.kind == PassageKind::WindowDrop ? passage.from.y + 1.05f
                                                       : std::max(passage.from.y, passage.to.y) + 0.95f;
    }

    // Замын чиглэл (from -> to), хэвтээ нэгж вектор
    Vector3 VaultAxis(const Passage& passage)
    {
        const Vector3 axis = { passage.to.x - passage.from.x, 0.0f, passage.to.z - passage.from.z };
        const float length = Vector3Length(axis);
        return length > 0.001f ? Vector3Scale(axis, 1.0f / length) : Vector3{ std::sin(passage.facing), 0.0f, std::cos(passage.facing) };
    }

    // Ханын төв (хөлийн өндөрт биш, хэвтээ байрлал)
    Vector3 VaultWall(const Passage& passage)
    {
        return Vector3Add(passage.from, Vector3Scale(VaultAxis(passage), passage.wall));
    }

    // Биеийн харах зүг: замын чиглэл + түлхүүр агшны эргэлт
    float VaultFacing(const Passage& passage, const VaultKey& key)
    {
        const Vector3 axis = VaultAxis(passage);
        return std::atan2(axis.x, axis.z) + key.turn * DEG2RAD;
    }
}

Player::Player(Vector3 spawn) : spawnPoint(spawn), position(spawn), look(TeamMemberLook(0))
{
}

void Player::Assume(Vector3 newPosition, float newFacing, float newHealth, float newMaxHealth)
{
    position = newPosition;
    velocity = { 0.0f, 0.0f, 0.0f };
    facing = newFacing;
    health = newHealth;
    maxHealth = newMaxHealth;
    grounded = false;
    timeSinceHit = 10.0f;
    aimTimer = 0.0f;
    recoil = 0.0f;
    action = 0.0f;
    traversing = false;
    stance = Stance::Stand;
    crouchPose = pronePose = 0.0f;
}

void Player::Reset(float newMaxHealth)
{
    position = spawnPoint;
    velocity = { 0.0f, 0.0f, 0.0f };
    grounded = false;
    maxHealth = newMaxHealth;
    health = newMaxHealth;
    timeSinceHit = 10.0f;
    aimTimer = 0.0f;
    aimPose = 0.0f;
    recoil = 0.0f;
    action = 0.0f;
    traversing = false;
    stance = Stance::Stand;
    crouchPose = pronePose = 0.0f;
    rideStyle = RideStyle::None;
}

void Player::Teleport(Vector3 newPosition)
{
    position = newPosition;
    velocity = { 0.0f, 0.0f, 0.0f };
}

Vector3 Player::GunPivot() const
{
    // Буу мөрөн дээр тулж эргэнэ: суух, хэвтэхэд доошилно.
    Vector3 pivot = HumanoidMuzzleFor(held);
    pivot.y = std::max(0.22f, pivot.y - 0.42f * crouchPose - 1.0f * pronePose);
    pivot.z = 0.0f;
    return pivot;
}

void Player::AimAtPoint(Vector3 target)
{
    // Буу биеийн төвөөс баруун тийш зөрж байрладаг тул бууны тэнхлэг цэгээр дайрч гарах эргэлтийг
    // хэдэн давталтаар олно (камерын yaw-аар эргүүлбэл буу хараанаас зүүн тийш зөрнө).
    const Vector3 pivot = GunPivot();
    float yaw = facing;
    Vector3 from = HumanoidLocalToWorld(BodyBase(), yaw, pivot);
    for (int i = 0; i < 3; ++i)
    {
        const float dx = target.x - from.x, dz = target.z - from.z;
        if (dx * dx + dz * dz < 0.36f) break; // цэг хэт ойр (хөл доор): эргэлт тогтворгүй болно
        yaw = std::atan2(dx, dz);
        from = HumanoidLocalToWorld(BodyBase(), yaw, pivot);
    }

    const float horizontal = std::sqrt((target.x - from.x) * (target.x - from.x) + (target.z - from.z) * (target.z - from.z));
    const float limit = 1.1f - 0.75f * pronePose; // хэвтсэн үед буу их өргөгдөхгүй
    if (IsRiding())
    {
        // Унаан дээр зөвхөн их бие эргэнэ: унааны зүгээс ±120 градусаас хэтрэхгүй.
        const float twist = std::clamp(std::remainder(yaw - rideHeading, 2.0f * PI), -2.1f, 2.1f);
        yaw = rideHeading + twist;
    }
    facing = yaw;
    aimPitch = horizontal > 0.6f ? std::clamp(std::atan2(target.y - from.y, horizontal), -limit, limit) : 0.0f;
    aimTimer = 0.4f;
}

Vector3 Player::MuzzlePosition() const
{
    // Суух, хэвтэхэд буу доошилж, хэвтэхэд бага зэрэг урагшилна. Онилсон үед мөрөө тойрон дээш доош хазайна.
    Vector3 muzzle = GunPivot();
    const float length = HumanoidMuzzleFor(held).z + 0.35f * pronePose;
    const float pitch = aimPitch * aimPose;
    muzzle.y += length * std::sin(pitch);
    muzzle.z = length * std::cos(pitch);
    return HumanoidLocalToWorld(BodyBase(), facing, muzzle);
}

Vector3 Player::BodyBase() const
{
    // Сууж байхад ташаа суудал дээр байх тул биеийн суурь нь түүнээс ташааны өндрөөр доош.
    return IsRiding() ? Vector3{ seat.x, seat.y - 0.9f, seat.z } : position;
}

void Player::Ride(float dt, Vector3 ground, Vector3 seatHip, float heading, RideStyle style, Vector3 reinPoint, bool reins)
{
    rein = reinPoint;
    hasRein = reins;
    timeSinceHit += dt;
    aimTimer -= dt;
    aimPose += ((aimTimer > 0.0f ? 1.0f : 0.0f) - aimPose) * std::min(1.0f, 9.0f * dt);
    recoil = std::max(0.0f, recoil - dt * 9.0f);
    action = std::max(0.0f, action - dt / actionDuration);

    traversing = false;
    stance = Stance::Stand; // унаан дээр суух, хэвтэхгүй
    crouchPose = pronePose = 0.0f;

    position = ground;
    velocity = { 0.0f, 0.0f, 0.0f };
    floorY = ground.y;
    grounded = true;
    seat = seatHip;
    rideStyle = style;
    rideHeading = heading;

    // Онилж буудаагүй үед унааны явж буй зүг рүү харна.
    if (aimTimer <= 0.0f)
    {
        facing = heading;
    }
}

void Player::StopRiding(Vector3 exitPosition)
{
    rideStyle = RideStyle::None;
    position = exitPosition;
    velocity = { 0.0f, 0.0f, 0.0f };
    grounded = false;
}

void Player::StartTraverse(const Passage& passage)
{
    traversing = true;
    traverse = passage;
    traverseTime = 0.0f;
    traverseDuration = passage.kind == PassageKind::LadderUp || passage.kind == PassageKind::LadderDown ? 1.9f
                     : passage.kind == PassageKind::WindowDrop ? 2.4f
                     : passage.kind == PassageKind::FenceOver ? 1.25f : 1.4f;
    stance = Stance::Stand;
    velocity = { 0.0f, 0.0f, 0.0f };
}

void Player::StartAction(float duration)
{
    action = 1.0f;
    actionDuration = std::max(0.05f, duration);
}

void Player::TakeDamage(float amount)
{
    timeSinceHit = 0.0f;
    hitFromKnown = false; // TakeDamageFrom дахин тавина
    if (downed)
    {
        // Унасан хүнийг цохивол босгох хугацаа хурдан дуусна
        downTimer -= amount * 0.12f;
        if (downTimer <= 0.0f)
        {
            downed = false;
            health = 0.0f;
        }
        return;
    }
    health = std::max(0.0f, health - amount / kTeamToughness);
    if (health <= 0.0f && rideStyle == RideStyle::None)
    {
        downed = true; // үхэхийн оронд унана: багийнхан босгох боломжтой
        downTimer = kDownTime;
        health = 1.0f;
        stance = Stance::Prone;
    }
}

void Player::UpdateDowned(float dt)
{
    if (!downed) return;
    downTimer -= dt;
    if (downTimer <= 0.0f)
    {
        downed = false;
        health = 0.0f;
    }
}

void Player::Revive(float fraction)
{
    if (!downed) return;
    downed = false;
    health = std::max(health, maxHealth * fraction);
    stance = Stance::Stand;
}

void Player::Heal(float amount)
{
    if (downed) Revive(0.0f); // эмнэлгийн хайрцаг ч босгоно
    health = std::min(maxHealth, health + amount);
}

float Player::HorizontalSpeed() const
{
    return std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
}

void Player::Update(float dt, const GameConfig& config, const World& world, float cameraYaw)
{
    timeSinceHit += dt;
    if (downed)
    {
        // Унасан: хэвтэнэ, хөдлөхгүй (босгохыг хүлээнэ)
        stance = Stance::Prone;
        velocity = { 0.0f, 0.0f, 0.0f };
        pronePose += (1.0f - pronePose) * std::min(1.0f, 6.0f * dt);
        crouchPose = std::max(0.0f, crouchPose - dt * 4.0f);
        return;
    }
    aimTimer -= dt;
    aimPose += ((aimTimer > 0.0f ? 1.0f : 0.0f) - aimPose) * std::min(1.0f, 9.0f * dt);
    recoil = std::max(0.0f, recoil - dt * 9.0f);
    action = std::max(0.0f, action - dt / actionDuration);
    landing = std::max(0.0f, landing - dt * 2.2f);

    // --- Шат, цонхоор гарч яваа: тогтсон замаар шилжинэ, удирдлага үйлчлэхгүй ---
    if (traversing)
    {
        traverseTime += dt;
        const float t = std::min(1.0f, traverseTime / traverseDuration);
        const Vector3 from = traverse.from, to = traverse.to;
        auto smooth = [](float x) { x = std::clamp(x, 0.0f, 1.0f); return x * x * (3.0f - 2.0f * x); };

        // Зам: from -> via -> to. Шат: босоо хэсэг + тавцан руу алхам. Цонх: тавцангийн дээгүүр давна.
        Vector3 via = {};
        float split = 0.5f, moveTime = 1.0f;
        switch (traverse.kind)
        {
        case PassageKind::LadderUp: via = { from.x, to.y, from.z }; split = 0.8f; break;
        case PassageKind::LadderDown: via = { to.x, from.y, to.z }; split = 0.2f; break;
        default: break;
        }
        if (IsWindow(traverse.kind))
        {
            // Цонх: хөлийн байрлал, биеийн зүг түлхүүр агшнуудын дагуу (гараа тавих, өвдөг гаргах, суух,
            // үсрэх эсвэл эргэж зүүгдэх).
            const VaultKey key = SampleVault(traverse.kind, t);
            const float total = std::max(0.01f, Vector2Distance({ from.x, from.z }, { to.x, to.z }));
            const float along = std::clamp((traverse.wall + VaultDistance(traverse, key.wall, total)) / total, 0.0f, 1.0f);
            const float sill = VaultSill(traverse);
            const float y = key.level <= 1.0f ? Lerp(from.y, sill, key.level) : Lerp(sill, to.y, key.level - 1.0f);
            position = { Lerp(from.x, to.x, along), y + key.lift, Lerp(from.z, to.z, along) };
            facing = VaultFacing(traverse, key);
        }
        else
        {
            const float p = std::min(1.0f, t / moveTime);
            position = p < split ? Vector3Lerp(from, via, smooth(p / split)) : Vector3Lerp(via, to, smooth((p - split) / (1.0f - split)));
            facing = traverse.facing;
        }

        velocity = { 0.0f, 0.0f, 0.0f };
        grounded = false;
        floorY = world.SurfaceHeightAt(position.x, position.z, position.y + 0.3f);
        walkCycle += dt * 9.0f;
        crouchPose = pronePose = 0.0f;
        if (t >= 1.0f)
        {
            traversing = false; // цонхноос зүүгдсэн бол эндээс чөлөөтэй унана
            if (!IsWindow(traverse.kind)) position = to; // цонхны сүүлийн агшин аль хэдийн байрлалдаа
            // Аюулгүйн үүднээс: саадтай давхцаж үлдвэл (хөдөлж чадахгүй гацна) ойрын чөлөөт газар руу түлхэнэ.
            world.ResolveHorizontal(position, config.playerRadius, config.playerHeight, config.stepHeight);
        }
        return;
    }

    // --- Оролт: камерын зүгтэй харьцангуй хөдөлгөөний чиглэл ---
    // Гар (WASD) эсвэл мэдрэгчтэй дэлгэцийн хүрд: хүрдийг бага хазайлгавал удаан алхана.
    const Vector2 axis = InMoveAxis();
    const float inputForward = axis.y;
    const float inputRight = axis.x * InStrafeScale();
    const float inputStrength = std::min(1.0f, std::sqrt(inputRight * inputRight + inputForward * inputForward));

    const Vector3 forward = { std::sin(cameraYaw), 0.0f, std::cos(cameraYaw) };
    const Vector3 right = { -std::cos(cameraYaw), 0.0f, std::sin(cameraYaw) };

    Vector3 wish = Vector3Add(Vector3Scale(forward, inputForward), Vector3Scale(right, inputRight));
    const bool moving = Vector3LengthSqr(wish) > 0.0f;
    if (moving)
    {
        wish = Vector3Normalize(wish);
    }

    // --- Биеийн байрлал: C суух / босох, Z хэвтэх / босох, Space босох ---
    if (InKeyPressed(KEY_C)) stance = stance == Stance::Crouch ? Stance::Stand : Stance::Crouch;
    if (InKeyPressed(KEY_Z)) stance = stance == Stance::Prone ? Stance::Stand : Stance::Prone;
    bool jump = InKeyPressed(KEY_SPACE);
    if (jump && stance != Stance::Stand)
    {
        stance = Stance::Stand; // эхний Space зөвхөн босгоно
        jump = false;
    }
    crouchPose += ((stance == Stance::Crouch ? 1.0f : 0.0f) - crouchPose) * std::min(1.0f, 10.0f * dt);
    pronePose += ((stance == Stance::Prone ? 1.0f : 0.0f) - pronePose) * std::min(1.0f, 6.0f * dt);

    // Сууж алхах нь удаан, мөлхөх нь бүр удаан; хурдан гүйх нь зөвхөн босоо үед бүрэн хүчтэй.
    float speed = config.walkSpeed * (1.0f - 0.45f * crouchPose - 0.78f * pronePose);
    if (InSprint() && stance != Stance::Prone)
    {
        speed *= stance == Stance::Crouch ? 1.25f : config.sprintMultiplier;
    }
    speed *= inputStrength;
    speed *= 1.0f - 0.7f * landing; // буусан даруйд хурдан гүйж чадахгүй

    // --- Хэвтээ хурд: зорилтот хурд руу зөөлөн ойртоно ---
    const float control = grounded ? 1.0f : config.airControl;
    const float blend = std::min(1.0f, config.acceleration * control * dt);
    velocity.x += (wish.x * speed - velocity.x) * blend;
    velocity.z += (wish.z * speed - velocity.z) * blend;

    // --- Үсрэлт ба таталцал ---
    if (grounded && jump)
    {
        velocity.y = config.jumpSpeed;
        grounded = false;
    }
    velocity.y -= config.gravity * dt;

    // --- Хөдөлгөөн ба мөргөлдөөн ---
    const float previousY = position.y;
    const bool wasGrounded = grounded;

    position.x += velocity.x * dt;
    position.z += velocity.z * dt;
    const float height = config.playerHeight * (1.0f - 0.3f * crouchPose - 0.65f * pronePose);
    world.ResolveHorizontal(position, config.playerRadius, height, config.stepHeight);

    position.y += velocity.y * dt;

    // Өмнөх өндрөөс stepHeight-аас илүүгүй өндөр гадаргуу дээр л зогсож чадна.
    floorY = world.SurfaceHeightAt(position.x, position.z, previousY + config.stepHeight);
    // Уруудах үед газраас хөндийрөхгүй: газар дээр байсан бол жижиг зөрүүг шууд дагана.
    const bool snapDown = wasGrounded && position.y - floorY < config.stepHeight;
    if (velocity.y <= 0.0f && (position.y <= floorY || snapDown))
    {
        // Өндрөөс (~1.5 м-ээс дээш) унаж буувал хөлөө нугалж суугаад босно.
        if (!wasGrounded && velocity.y < -5.5f)
        {
            landing = std::min(1.0f, -velocity.y / 9.0f);
            Audio::Play(Sfx::Thud, position, std::min(1.0f, -velocity.y / 12.0f));
        }
        position.y = floorY;
        velocity.y = 0.0f;
        grounded = true;
    }
    else
    {
        grounded = false;
    }

    // --- Биеэ хөдөлгөөний зүг рүү эргүүлэх (онилж байхгүй үед) ---
    if (moving && aimTimer <= 0.0f)
    {
        const float target = std::atan2(wish.x, wish.z);
        const float diff = std::remainder(target - facing, 2.0f * PI); // хамгийн богино эргэлт
        facing += diff * std::min(1.0f, config.turnSpeed * dt);
    }

    // Явж буй зүг (харж буй зүгтэй харьцангуй): хажуу тийш, арагш алхахад хөл тэр зүг рүү гишгэнэ.
    if (HorizontalSpeed() > 0.4f)
    {
        const float target = std::remainder(std::atan2(velocity.x, velocity.z) - facing, 2.0f * PI);
        moveDirection += std::remainder(target - moveDirection, 2.0f * PI) * std::min(1.0f, 12.0f * dt);
    }

    if (grounded)
    {
        // Гүйхэд алхам уртсах тул давтамж хурдтай шууд пропорциональ өсөхгүй.
        walkCycle += dt * HumanoidGaitRate(HorizontalSpeed());
    }
}

void Player::Draw(const GameConfig& config, bool showCollider) const
{
    if (rideStyle == RideStyle::Hidden)
    {
        return; // бүхээг дотор
    }

    HumanoidLook look = this->look;
    look.held = held;

    if (IsRiding())
    {
        // Гарын байрлал зэвсгээс, хөл ба их биеийн байрлал суудлын хэлбэрээс хамаарна.
        HumanoidPose pose = MakeSoldierPose(0.0f, 0.0f, true, (float)GetTime(), recoil, held, action);
        const bool saddle = rideStyle == RideStyle::Saddle;
        pose.hip[0] = pose.hip[1] = saddle ? 34.0f : 74.0f;
        pose.knee[0] = pose.knee[1] = saddle ? 46.0f : 84.0f;
        pose.legSpread = saddle ? 24.0f : 13.0f;
        pose.lean += saddle ? 4.0f : 14.0f;
        pose.bob = 0.0f;
        pose.seated = true;
        pose.aim = aimPose;
        pose.aimPitch = aimPitch * RAD2DEG;
        pose.seatTwist = std::remainder(rideHeading - facing, 2.0f * PI) * RAD2DEG; // ташаа, хөл унааны зүгт
        if (hasRein)
        {
            // Зүүн гар жолоо барина: цэгийг биеийн (хөлийн) орон зайд шилжүүлнэ.
            const Vector3 d = Vector3Subtract(rein, BodyBase());
            const float sn = std::sin(facing), cs = std::cos(facing);
            pose.rein = 1.0f;
            pose.reinPoint = { d.x * cs - d.z * sn, d.y - pose.bob, d.x * sn + d.z * cs };
        }
        DrawHumanoid(BodyBase(), facing, 1.0f, pose, look);
        return;
    }

    // Сүүдэр: үсрэх үед газраас хэр өндөрт байгааг харуулна.
    DrawBlobShadow({ position.x, floorY, position.z }, config.playerRadius * 1.3f);

    if (traversing && !demo)
    {
        // Авирах, давах, зүүгдэх байрлал: гар хоосон, мөчнүүдийг шууд өнцгөөр нь тавина.
        HumanoidPose pose;
        pose.seated = true;
        pose.time = (float)GetTime();
        const float t = std::min(1.0f, traverseTime / traverseDuration);
        if (traverse.kind == PassageKind::LadderUp || traverse.kind == PassageKind::LadderDown)
        {
            // Шат: гар гишгүүрээс барина. Гишгүүр 0.35 м тутамд (Structures.cpp). Гар бүр нэг гишгүүрийг барьж
            // бие 0.7 м өгсөхөд дараагийн (2 гишгүүрийн дээрх) гишгүүр рүү шилжинэ; хоёр гар ээлжилнэ.
            const float kRung = 0.35f;
            const bool up = traverse.kind == PassageKind::LadderUp;
            const Vector3 bottom = up ? traverse.from : traverse.to;
            const Vector3 forward = { std::sin(facing), 0.0f, std::cos(facing) };
            const Vector3 bodyRight = { -std::cos(facing), 0.0f, std::sin(facing) };
            const Vector3 ladder = Vector3Add(bottom, Vector3Scale(forward, traverse.wall - 0.05f)); // гишгүүрийн урд тал
            const float railTop = std::max(traverse.from.y, traverse.to.y) + 0.75f;

            // Дээд үзүүрт тавцан руу гарах алхам (step: 0 = шатан дээр .. 1 = тавцан дээр зогссон).
            // Update-ийн замтай ижил: өгсөхөд сүүлийн 20%, буухад эхний 20% нь хэвтээ алхам.
            const float split = up ? 0.8f : 0.2f;
            const float step = up ? std::clamp((t - split) / (1.0f - split), 0.0f, 1.0f) : std::clamp(1.0f - t / split, 0.0f, 1.0f);
            auto smooth = [](float x) { x = std::clamp(x, 0.0f, 1.0f); return x * x * (3.0f - 2.0f * x); };
            // Гар: эхлээд шатны хоёр хажуугийн модны орой дээр тулж (биеэ түлхэн гаргана), дараа нь аажим суллана.
            const float toRails = smooth(step / 0.3f);
            pose.grip = 1.0f - smooth((step - 0.55f) / 0.4f);

            const float sn = std::sin(facing), cs = std::cos(facing);
            for (int side = 0; side < 2; ++side)
            {
                const float u = (position.y + (float)side * kRung) / (2.0f * kRung);
                const float cycle = std::floor(u), frac = u - cycle;
                const float held = cycle * 2.0f * kRung - (float)side * kRung + 1.75f;
                const float move = std::clamp((frac - 0.75f) / 0.25f, 0.0f, 1.0f);
                const float blend = move * move * (3.0f - 2.0f * move);
                const float y = std::min(railTop, held + 2.0f * kRung * blend);
                // Гишгүүр солих үед гар бага зэрэг хойш татагдана.
                Vector3 point = Vector3Add(ladder, Vector3Scale(forward, -0.1f * std::sin(PI * blend)));
                point = Vector3Add(point, Vector3Scale(bodyRight, side == 0 ? 0.17f : -0.17f));
                point.y = y;
                // Шатны хажуугийн модны орой (тавцангаас 0.8 м дээш)
                Vector3 rail = Vector3Add(ladder, Vector3Scale(bodyRight, side == 0 ? 0.25f : -0.25f));
                rail.y = railTop - 0.05f;
                point = Vector3Lerp(point, rail, toRails);
                const Vector3 d = Vector3Subtract(point, position);
                pose.gripPoint[side] = { d.x * cs - d.z * sn, d.y - pose.bob, d.x * sn + d.z * cs };
                // Суллах үед гар доошоо, биеийн хажууд (толгой дээгүүр өргөгдөхгүй)
                pose.shoulder[side] = Lerp(150.0f, 8.0f, step);
                pose.elbow[side] = Lerp(30.0f, 18.0f, step);
                pose.shoulderYaw[side] = 8.0f;
            }

            // Хөл: авирсан өндөртэй уялдаж ээлжлэн дээш гишгэнэ; тавцан руу гарахад нэг хөлөөрөө алхаж зогсоно.
            const float legSwing = std::sin(position.y / (2.0f * kRung) * 2.0f * PI) * (1.0f - step);
            const float stride = std::sin(PI * step);
            pose.hip[0] = Lerp(45.0f + 30.0f * legSwing, 35.0f * stride, step);
            pose.hip[1] = Lerp(45.0f - 30.0f * legSwing, -10.0f * stride, step);
            pose.knee[0] = Lerp(70.0f + 40.0f * legSwing, 45.0f * stride, step);
            pose.knee[1] = Lerp(70.0f - 40.0f * legSwing, 15.0f * stride, step);
            pose.lean = Lerp(4.0f, 16.0f * stride, step); // давж гарахдаа урагш бөхийнө
        }
        else
        {
            // Цонх: мөчний өнцөг түлхүүр агшнаас, гар тавцан / хүрээн дээрх цэгт тулна.
            const VaultKey key = SampleVault(traverse.kind, t);
            for (int side = 0; side < 2; ++side)
            {
                pose.hip[side] = key.hip[side];
                pose.knee[side] = key.knee[side];
            }
            pose.lean = key.lean;
            pose.headPitch = key.head;
            pose.shoulder[0] = pose.shoulder[1] = key.shoulder;
            pose.elbow[0] = pose.elbow[1] = key.elbow;
            pose.shoulderYaw[0] = pose.shoulderYaw[1] = 8.0f;
            pose.plantFeet = true; // хөл нугарахад бие доошилж, доод хөлийн ул тавцан / газарт хүрнэ

            // Гарын цэг (ертөнцөд) -> биеийн орон зай (хөлийн байрлал, харах зүгээр; DrawHumanoid bob-оор шилжүүлнэ)
            pose.grip = key.grip;
            const Vector3 axis = VaultAxis(traverse);
            const Vector3 wall = VaultWall(traverse);
            const Vector3 bodyRight = { -std::cos(facing), 0.0f, std::sin(facing) };
            const float sn = std::sin(facing), cs = std::cos(facing);
            for (int side = 0; side < 2; ++side)
            {
                Vector3 point = Vector3Add(wall, Vector3Scale(axis, key.handWall));
                point = Vector3Add(point, Vector3Scale(bodyRight, (side == 0 ? 1.0f : -1.0f) * key.handSide));
                point.y = VaultSill(traverse) + key.handUp;
                const Vector3 d = Vector3Subtract(point, position);
                pose.gripPoint[side] = { d.x * cs - d.z * sn, d.y - pose.bob, d.x * sn + d.z * cs };
            }
        }
        look.held = HeldItem::None;
        DrawHumanoid(position, facing, 1.0f, pose, look);
        return;
    }

    // Суух, мөлхөх үеийн удаан хурдыг тухайн байрлалынхаа "бүтэн алхаа" гэж тооцно.
    const float stanceSpeed = config.walkSpeed * (1.0f - 0.45f * crouchPose - 0.78f * pronePose);
    const float moveAmount = HorizontalSpeed() / stanceSpeed;
    HumanoidPose pose = MakeSoldierPose(walkCycle, moveAmount, grounded, (float)GetTime(), recoil, held, action);
    pose.aim = aimPose;
    pose.aimPitch = aimPitch * RAD2DEG;
    pose.speed = HorizontalSpeed();
    pose.crouch = std::max(crouchPose, 0.85f * std::sqrt(landing));
    pose.prone = pronePose;
    pose.moveDirection = moveDirection;
    pose.reload = reload;
    if (demo)
    {
        pose = MakeSoldierPose(demoPhase, demoMove, true, (float)GetTime(), 0.0f, held,
                               demoAction >= 0.0f ? 1.0f - demoAction : 0.0f);
        pose.aim = demoAim;
        pose.speed = demoMove * stanceSpeed;
        pose.crouch = stance == Stance::Crouch ? 1.0f : 0.0f;
        pose.prone = stance == Stance::Prone ? 1.0f : 0.0f;
        pose.moveDirection = demoDirection;
        pose.reload = demoReload;
    }
    DrawHumanoid(position, facing, 1.0f, pose, look);

    if (showCollider)
    {
        DrawCylinderWires(position, config.playerRadius, config.playerRadius, config.playerHeight, 12, LIME);
    }
}
