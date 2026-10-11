#include "Player.h"

#include "Input.h"

#include "Humanoid.h"
#include "Team.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>

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

void Player::AimAt(float yaw)
{
    facing = yaw;
    aimTimer = 0.4f;
}

Vector3 Player::MuzzlePosition() const
{
    // Суух, хэвтэхэд буу доошилж, хэвтэхэд бага зэрэг урагшилна.
    Vector3 muzzle = HumanoidMuzzleFor(held);
    muzzle.y = std::max(0.22f, muzzle.y - 0.42f * crouchPose - 1.0f * pronePose);
    muzzle.z += 0.35f * pronePose;
    return HumanoidLocalToWorld(BodyBase(), facing, muzzle);
}

Vector3 Player::BodyBase() const
{
    // Сууж байхад ташаа суудал дээр байх тул биеийн суурь нь түүнээс ташааны өндрөөр доош.
    return IsRiding() ? Vector3{ seat.x, seat.y - 0.9f, seat.z } : position;
}

void Player::Ride(float dt, Vector3 ground, Vector3 seatHip, float heading, RideStyle style)
{
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
                     : passage.kind == PassageKind::WindowDrop ? 1.25f : 0.85f;
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
    health = std::max(0.0f, health - amount);
    timeSinceHit = 0.0f;
}

void Player::Heal(float amount)
{
    health = std::min(maxHealth, health + amount);
}

float Player::HorizontalSpeed() const
{
    return std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
}

void Player::Update(float dt, const GameConfig& config, const World& world, float cameraYaw)
{
    timeSinceHit += dt;
    aimTimer -= dt;
    aimPose += ((aimTimer > 0.0f ? 1.0f : 0.0f) - aimPose) * std::min(1.0f, 9.0f * dt);
    recoil = std::max(0.0f, recoil - dt * 9.0f);
    action = std::max(0.0f, action - dt / actionDuration);

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
        case PassageKind::WindowDrop:
            via = { (from.x + to.x) * 0.5f, from.y + 1.0f, (from.z + to.z) * 0.5f };
            moveTime = 0.6f; // үлдсэн хугацаанд тавцангаас зүүгдэж байна
            break;
        default: via = { (from.x + to.x) * 0.5f, std::max(from.y, to.y) + 1.15f, (from.z + to.z) * 0.5f }; break;
        }
        const float p = std::min(1.0f, t / moveTime);
        position = p < split ? Vector3Lerp(from, via, smooth(p / split)) : Vector3Lerp(via, to, smooth((p - split) / (1.0f - split)));

        facing = traverse.facing;
        velocity = { 0.0f, 0.0f, 0.0f };
        grounded = false;
        floorY = world.SurfaceHeightAt(position.x, position.z, position.y + 0.3f);
        walkCycle += dt * 9.0f;
        crouchPose = pronePose = 0.0f;
        if (t >= 1.0f)
        {
            traversing = false; // цонхноос зүүгдсэн бол эндээс чөлөөтэй унана
            position = to;
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
        const float speedNow = HorizontalSpeed();
        walkCycle += dt * (speedNow > 0.1f ? 4.5f + speedNow * 0.9f : 0.0f);
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
        const float swing = std::sin(walkCycle);
        if (traverse.kind == PassageKind::LadderUp || traverse.kind == PassageKind::LadderDown)
        {
            // Шат: гар хөл ээлжлэн дээш доош
            pose.shoulder[0] = 140.0f + 28.0f * swing;
            pose.shoulder[1] = 140.0f - 28.0f * swing;
            pose.elbow[0] = 50.0f - 30.0f * swing;
            pose.elbow[1] = 50.0f + 30.0f * swing;
            pose.hip[0] = 38.0f - 30.0f * swing;
            pose.hip[1] = 38.0f + 30.0f * swing;
            pose.knee[0] = 60.0f - 38.0f * swing;
            pose.knee[1] = 60.0f + 38.0f * swing;
            pose.lean = 6.0f;
        }
        else if (traverse.kind == PassageKind::WindowDrop && t > 0.6f)
        {
            // Тавцангаас зүүгдсэн: гар дээшээ сунасан, хөл унжсан
            pose.shoulder[0] = pose.shoulder[1] = 176.0f;
            pose.elbow[0] = pose.elbow[1] = 6.0f;
            pose.knee[0] = 10.0f;
            pose.knee[1] = 18.0f;
        }
        else
        {
            // Цонхны тавцан давах: өвдгөө эвхэж, гараараа тулна
            const float tuck = std::sin(std::min(1.0f, t / (traverse.kind == PassageKind::WindowDrop ? 0.6f : 1.0f)) * PI);
            pose.hip[0] = 78.0f * tuck;
            pose.hip[1] = 52.0f * tuck;
            pose.knee[0] = 96.0f * tuck;
            pose.knee[1] = 70.0f * tuck;
            pose.lean = 34.0f * tuck;
            pose.shoulder[0] = pose.shoulder[1] = 40.0f + 30.0f * tuck;
            pose.elbow[0] = pose.elbow[1] = 40.0f;
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
    pose.crouch = crouchPose;
    pose.prone = pronePose;
    pose.moveDirection = moveDirection;
    pose.reload = reload;
    if (demo)
    {
        pose = MakeSoldierPose(demoPhase, demoMove, true, (float)GetTime(), 0.0f, held,
                               demoAction >= 0.0f ? 1.0f - demoAction : 0.0f);
        pose.aim = demoAim;
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
