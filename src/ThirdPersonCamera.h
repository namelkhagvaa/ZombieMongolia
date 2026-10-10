#pragma once

#include "Config.h"
#include "World.h"
#include "raylib.h"

// Тоглогчийг тойрон эргэх (orbit) гуравдагч этгээдийн камер.
class ThirdPersonCamera
{
public:
    explicit ThirdPersonCamera(const GameConfig& config);

    // Хулганын оролтыг уншиж, тоглогчийн хөлийн байрлалыг зөөлөн дагана.
    // aiming: онилж байгаа үед камер ойртож, харах өнцөг нарийсна.
    // extraDistance: унаа унасан үед камерыг холдуулах нэмэлт зай.
    void Update(float dt, const GameConfig& config, const World& world, Vector3 playerFeet, bool aiming,
                float extraDistance = 0.0f);

    // Тоглогч шинэ газар гарч ирэхэд (даалгавар эхлэх) камерыг шууд тэнд аваачна.
    void Snap(Vector3 playerFeet);
    void SetView(float newYaw, float newPitch);
    void SetDistance(float newDistance) { distance = newDistance; }

    const Camera3D& Get() const { return camera; }
    float Yaw() const { return yaw; }

private:
    Camera3D camera = {};
    Vector3 follow = { 0.0f, 0.0f, 0.0f }; // тоглогчийн байрлалын зөөлрүүлсэн утга
    bool hasFollow = false;
    float yaw = 0.0f;    // радиан, 0 = +Z рүү харна
    float pitch = 0.3f;  // эерэг = камер дээрээс харна
    float distance;
    float aimBlend = 0.0f; // 0 = ердийн, 1 = бүрэн онилсон
    float rideDistance = 0.0f; // унаа унасан үеийн нэмэлт зай (аажим өөрчлөгдөнө)
};
