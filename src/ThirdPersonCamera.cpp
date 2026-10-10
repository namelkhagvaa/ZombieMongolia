#include "ThirdPersonCamera.h"

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
                               bool aiming, float extraDistance)
{
    // Унах, буухад камер огцом үсрэхгүй, аажим холдож ойртоно.
    rideDistance += (extraDistance - rideDistance) * std::min(1.0f, 4.0f * dt);

    // Онилгоо руу зөөлөн шилжинэ.
    const float blendTarget = aiming ? 1.0f : 0.0f;
    aimBlend += (blendTarget - aimBlend) * std::min(1.0f, config.aimBlendSpeed * dt);

    const float sensitivity = config.mouseSensitivity * Lerp(1.0f, config.aimSensitivityScale, aimBlend);
    const Vector2 mouse = GetMouseDelta();
    yaw -= mouse.x * sensitivity;
    pitch += mouse.y * sensitivity * (config.invertY ? -1.0f : 1.0f);
    pitch = Clamp(pitch, config.cameraMinPitch, config.cameraMaxPitch);

    distance -= GetMouseWheelMove() * config.cameraZoomStep;
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
    const Vector3 target = {
        follow.x + right.x * config.cameraShoulderOffset,
        follow.y + config.cameraTargetHeight,
        follow.z + right.z * config.cameraShoulderOffset,
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
}
