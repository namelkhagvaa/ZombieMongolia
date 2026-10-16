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

void ThirdPersonCamera::Guide(float targetYaw, float targetPitch, float dt)
{
    const float k = std::min(1.0f, 5.0f * dt);
    yaw += std::remainder(targetYaw - yaw, 2.0f * PI) * k;
    pitch += (targetPitch - pitch) * k;
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
    traverseBlend += ((traversing ? 1.0f : 0.0f) - traverseBlend) * std::min(1.0f, 4.0f * dt);

    const float sensitivity = config.mouseSensitivity * Lerp(1.0f, config.aimSensitivityScale, aimBlend) *
                              (IsScoped() ? config.scopeSensitivityScale / config.aimSensitivityScale : 1.0f);
    const Vector2 mouse = InLookDelta();
    yaw -= mouse.x * sensitivity;
    pitch += mouse.y * sensitivity * (config.invertY ? -1.0f : 1.0f);
    pitch = Clamp(pitch, riding ? 0.08f : config.cameraMinPitch, config.cameraMaxPitch);

    distance -= InWheel() * config.cameraZoomStep;
    distance = Clamp(distance, config.cameraMinDistance, config.cameraMaxDistance);

    // Зөөлөн дагалт: камер тоглогчийн байрлал руу экспоненциалаар ойртоно (frame rate-ээс хамаарахгүй).
    // Босоо чиглэлд арай удаан: толгод, шатаар явахад дэлгэц сэгсрэхгүй.
    if (!hasFollow)
    {
        Snap(playerFeet);
    }
    const float horizontal = 1.0f - std::exp(-config.cameraFollowSpeed * dt);
    // (Авирах, зүүгдэх үед бие хурдан дээш доош хөдөлдөг тул босоо дагалт ердийн хурдтай.)
    const float vertical = 1.0f - std::exp(-config.cameraFollowSpeed * (0.5f + 0.5f * traverseBlend) * dt);
    follow.x += (playerFeet.x - follow.x) * horizontal;
    follow.z += (playerFeet.z - follow.z) * horizontal;
    follow.y += (playerFeet.y - follow.y) * vertical;

    // Мөрөн дээгүүр харах: харах цэгийг камерын баруун тал руу шилжүүлнэ.
    const Vector3 right = { -std::cos(yaw), 0.0f, std::sin(yaw) };
    // Баруун талд хана ойр байвал шилжилтийг багасгана: эс бөгөөс харах цэг хананы цаана гарч,
    // камер барилгын гаднаас харж, тоглогч харагдахгүй болно (хана дагасан шатан дээр).
    // Давж байхад харах цэг цээжний өндөрт: толгойн өндрөөс харвал үсрэх үед туяа цонхны дээд хүрээнд тусна.
    const Vector3 head = { follow.x, follow.y + config.cameraTargetHeight - targetDrop - 0.55f * traverseBlend, follow.z };
    // Давж байхад шилжилтгүй: цонхны нүх нарийн тул камер тоглогчийн яг ард байна.
    const float shoulder =
        std::clamp(world.ClipDistance(head, right, config.cameraShoulderOffset + 0.35f) - 0.35f, 0.0f, config.cameraShoulderOffset) *
        (1.0f - traverseBlend);
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
    float wanted = Lerp(base, std::min(base, config.aimDistance + rideDistance), aimBlend);
    wanted = Lerp(wanted, std::min(wanted, 2.6f), traverseBlend);
    float clipped = world.ClipDistance(target, back, wanted);
    // Унаа жолоодож байхад камер унааны их бие дотор орохгүй (ард нь толгод байсан ч)
    if (riding) clipped = std::max(clipped, std::min(wanted, 2.0f + rideDistance * 0.7f));

    camera.target = target;
    camera.position = Vector3Add(target, Vector3Scale(back, clipped));

    // Газар доогуур орохгүй
    const float ground = world.GroundHeight(camera.position.x, camera.position.z);
    camera.position.y = std::max(camera.position.y, ground + 0.35f);
    camera.fovy = Lerp(config.cameraFovY, config.aimFovY, aimBlend);

    if (firstPerson && !IsScoped())
    {
        // 1-р этгээд: нүдний түвшнээс харна; онилоход (баруун товч) бага зэрэг томруулна.
        const Vector3 forward = Vector3Negate(back);
        const Vector3 eye = { playerFeet.x, playerFeet.y + 1.66f - targetDrop, playerFeet.z };
        camera.position = Vector3Add(eye, Vector3Scale(forward, 0.08f));
        camera.target = Vector3Add(camera.position, Vector3Scale(forward, 10.0f));
        camera.fovy = Lerp(config.cameraFovY + 8.0f, config.aimFovY + 6.0f, aimBlend);
    }

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
