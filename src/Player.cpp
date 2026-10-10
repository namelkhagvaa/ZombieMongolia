#include "Player.h"

#include "Humanoid.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>

namespace
{
    HumanoidLook MakeLook()
    {
        HumanoidLook look;
        look.kind = HumanoidKind::Player;
        look.pants = { 58, 54, 48, 255 };
        look.boots = { 52, 36, 26, 255 };   // Монгол гутал
        look.body = { 40, 70, 150, 255 };   // хөх дээл
        look.trim = { 230, 150, 30, 255 };  // шар бүс, эмжээр
        look.belt = look.trim;
        look.hat = { 160, 34, 34, 255 };    // улаан оройтой малгай
        look.vest = { 122, 104, 70, 255 };  // савхин сумны хантааз
        look.skin = { 214, 168, 128, 255 };
        return look;
    }
}

Player::Player(Vector3 spawn) : spawnPoint(spawn), position(spawn)
{
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
    recoil = 0.0f;
    action = 0.0f;
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
    return HumanoidLocalToWorld(BodyBase(), facing, HumanoidMuzzleFor(held));
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
    recoil = std::max(0.0f, recoil - dt * 9.0f);
    action = std::max(0.0f, action - dt / actionDuration);

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
    recoil = std::max(0.0f, recoil - dt * 9.0f);
    action = std::max(0.0f, action - dt / actionDuration);

    // --- Оролт: камерын зүгтэй харьцангуй хөдөлгөөний чиглэл ---
    const float inputForward = (float)IsKeyDown(KEY_W) - (float)IsKeyDown(KEY_S);
    const float inputRight = (float)IsKeyDown(KEY_D) - (float)IsKeyDown(KEY_A);

    const Vector3 forward = { std::sin(cameraYaw), 0.0f, std::cos(cameraYaw) };
    const Vector3 right = { -std::cos(cameraYaw), 0.0f, std::sin(cameraYaw) };

    Vector3 wish = Vector3Add(Vector3Scale(forward, inputForward), Vector3Scale(right, inputRight));
    const bool moving = Vector3LengthSqr(wish) > 0.0f;
    if (moving)
    {
        wish = Vector3Normalize(wish);
    }

    float speed = config.walkSpeed;
    if (IsKeyDown(KEY_LEFT_SHIFT))
    {
        speed *= config.sprintMultiplier;
    }

    // --- Хэвтээ хурд: зорилтот хурд руу зөөлөн ойртоно ---
    const float control = grounded ? 1.0f : config.airControl;
    const float blend = std::min(1.0f, config.acceleration * control * dt);
    velocity.x += (wish.x * speed - velocity.x) * blend;
    velocity.z += (wish.z * speed - velocity.z) * blend;

    // --- Үсрэлт ба таталцал ---
    if (grounded && IsKeyPressed(KEY_SPACE))
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
    world.ResolveHorizontal(position, config.playerRadius, config.playerHeight, config.stepHeight);

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

    static const HumanoidLook baseLook = MakeLook();
    HumanoidLook look = baseLook;
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
        DrawHumanoid(BodyBase(), facing, 1.0f, pose, look);
        return;
    }

    // Сүүдэр: үсрэх үед газраас хэр өндөрт байгааг харуулна.
    DrawBlobShadow({ position.x, floorY, position.z }, config.playerRadius * 1.3f);

    const float moveAmount = HorizontalSpeed() / config.walkSpeed;
    const HumanoidPose pose = MakeSoldierPose(walkCycle, moveAmount, grounded, (float)GetTime(), recoil, held, action);
    DrawHumanoid(position, facing, 1.0f, pose, look);

    if (showCollider)
    {
        DrawCylinderWires(position, config.playerRadius, config.playerRadius, config.playerHeight, 12, LIME);
    }
}
