#include "ThirdPersonCamera.h"

#include "Input.h"

#include "raymath.h"

#include <algorithm>
#include <cmath>

ThirdPersonCamera::ThirdPersonCamera(const GameConfig& config) : distance(config.cameraDistance)
{
    camera.up = { 0.0f, 1.0f, 0.0f };
    camera.fovy = config.cameraFovY;
    camera.projection = CAMERA_PERSPECTIVE;
}

void ThirdPersonCamera::Snap(Vector3 playerFeet)
{
    follow = playerFeet;
    hasFollow = true;
}

void ThirdPersonCamera::SetView(float newYaw, float newPitch)
{
    yaw = newYaw;
    pitch = newPitch;
}

void ThirdPersonCamera::Update(float dt, const GameConfig& config, const World& world, Vector3 playerFeet,
                               bool aiming, float extraDistance, bool scoped, float targetDrop)
{
    // Унах, буухад камер огцом үсрэхгүй, аажим холдож ойртоно.
    rideDistance += (extraDistance - rideDistance) * std::min(1.0f, 4.0f * dt);

    // Онилгоо руу зөөлөн шилжинэ.
    const float blendTarget = aiming ? 1.0f : 0.0f;
    aimBlend += (blendTarget - aimBlend) * std::min(1.0f, config.aimBlendSpeed * dt);

    scopeBlend += ((scoped ? 1.0f : 0.0f) - scopeBlend) * std::min(1.0f, config.aimBlendSpeed * dt);

    const float sensitivity = config.mouseSensitivity * Lerp(1.0f, config.aimSensitivityScale, aimBlend) *
                              (IsScoped() ? config.scopeSensitivityScale / config.aimSensitivityScale : 1.0f);
    const Vector2 mouse = InLookDelta();
    yaw -= mouse.x * sensitivity;
    pitch += mouse.y * sensitivity * (config.invertY ? -1.0f : 1.0f);
    pitch = Clamp(pitch, config.cameraMinPitch, config.cameraMaxPitch);

    distance -= InWheel() * config.cameraZoomStep;
    distance = Clamp(distance, config.cameraMinDistance, config.cameraMaxDistance);

    // Зөөлөн дагалт: камер тоглогчийн байрлал руу экспоненциалаар ойртоно (frame rate-ээс хамаарахгүй).
    // Босоо чиглэлд арай удаан: толгод, шатаар явахад дэлгэц сэгсрэхгүй.
    if (!hasFollow)
    {
        Snap(playerFeet);
    }
    const float horizontal = 1.0f - std::exp(-config.cameraFollowSpeed * dt);
    const float vertical = 1.0f - std::exp(-config.cameraFollowSpeed * 0.5f * dt);
    follow.x += (playerFeet.x - follow.x) * horizontal;
    follow.z += (playerFeet.z - follow.z) * horizontal;
    follow.y += (playerFeet.y - follow.y) * vertical;

    // Мөрөн дээгүүр харах: харах цэгийг камерын баруун тал руу шилжүүлнэ.
    const Vector3 right = { -std::cos(yaw), 0.0f, std::sin(yaw) };
    // Баруун талд хана ойр байвал шилжилтийг багасгана: эс бөгөөс харах цэг хананы цаана гарч,
    // камер барилгын гаднаас харж, тоглогч харагдахгүй болно (хана дагасан шатан дээр).
    const Vector3 head = { follow.x, follow.y + config.cameraTargetHeight - targetDrop, follow.z };
    const float shoulder =
        std::clamp(world.ClipDistance(head, right, config.cameraShoulderOffset + 0.35f) - 0.35f, 0.0f, config.cameraShoulderOffset);
    const Vector3 target = {
        follow.x + right.x * shoulder,
        head.y, // суух, хэвтэхэд камер дагаж доошилно
        follow.z + right.z * shoulder,
    };

    // Харах цэгээс камер руу чиглэсэн нэгж вектор
    const Vector3 back = {
        -std::sin(yaw) * std::cos(pitch),
        std::sin(pitch),
        -std::cos(yaw) * std::cos(pitch),
    };

    // Хана, толгод камер тоглогч хоёрын дунд орохоос сэргийлж зайг богиносгоно.
    const float base = distance + rideDistance;
    const float wanted = Lerp(base, std::min(base, config.aimDistance + rideDistance), aimBlend);
    const float clipped = world.ClipDistance(target, back, wanted);

    camera.target = target;
    camera.position = Vector3Add(target, Vector3Scale(back, clipped));

    // Газар доогуур орохгүй
    const float ground = world.GroundHeight(camera.position.x, camera.position.z);
    camera.position.y = std::max(camera.position.y, ground + 0.35f);
    camera.fovy = Lerp(config.cameraFovY, config.aimFovY, aimBlend);

    if (IsScoped())
    {
        // Дуран хараа: нүдний түвшнээс шууд урагш харна (тоглогчийн биеийг зурахгүй).
        const Vector3 forward = Vector3Negate(back);
        const Vector3 eye = { playerFeet.x, playerFeet.y + 1.72f - targetDrop, playerFeet.z };
        camera.position = Vector3Add(eye, Vector3Scale(forward, 0.3f));
        camera.target = Vector3Add(camera.position, Vector3Scale(forward, 10.0f));
        camera.fovy = config.scopeFovY;
    }
}
