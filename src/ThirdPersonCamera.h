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
    // scoped: мэргэн буудагчийн дуран хараа: камер нүдний түвшинд очиж, харах өнцөг маш нарийсна.
    void Update(float dt, const GameConfig& config, const World& world, Vector3 playerFeet, bool aiming,
                float extraDistance = 0.0f, bool scoped = false, float targetDrop = 0.0f);
    // Дуран хараагаар харж байгаа эсэх (тоглогчийн биеийг зурахгүй, дэлгэцэд хараа гарна).
    bool IsScoped() const { return scopeBlend > 0.6f; }
    // 1-р этгээд: камер нүдний түвшинд, тоглогчийн биеийг зурахгүй (гар, буу нь тусдаа зурагдана).
    void SetFirstPerson(bool active) { firstPerson = active; }
    bool IsFirstPerson() const { return firstPerson && !IsScoped(); }

    // Тоглогч шинэ газар гарч ирэхэд (даалгавар эхлэх) камерыг шууд тэнд аваачна.
    void Snap(Vector3 playerFeet);
    void SetView(float newYaw, float newPitch);
    void SetDistance(float newDistance) { distance = newDistance; }
    // Шат, цонх давах үед: камерыг өгсөн зүг рүү зөөлөн эргүүлж, ойртуулж, мөрний шилжилтгүй болгоно.
    // Ингэснээр камер давж буй замын ард байрлаж, цонхны нүхээр тоглогчийг харна (хананд наалдахгүй).
    void Guide(float targetYaw, float targetPitch, float dt);
    void SetTraversing(bool active) { traversing = active; }
    // Унаа жолоодож байна: камер унааны доогуур орж дээш харахгүй, унааны дотор орохгүй.
    void SetRiding(bool value) { riding = value; }

    const Camera3D& Get() const { return camera; }
    float AimBlend() const { return aimBlend; } // 0 = ердийн, 1 = бүрэн онилсон
    float Yaw() const { return yaw; }
    float Pitch() const { return pitch; }
    // Буудах ухралт: хараа дээш (pitch, радиан) ба хажуу тийш (yaw) үсэрч, дээш үсэрсний ихэнх нь аажим буцна.
    void Kick(float yawDelta, float pitchDelta)
    {
        yaw += yawDelta;
        pitch -= pitchDelta;
        kickReturn += pitchDelta * 0.7f;
    }
    // Сэгсрэлт (дэлбэрэлт, мөргөлдөөн, цохиулах): 0..1, аажим унтарна. Онох чиглэлд нөлөөлөхгүй (камер бүхэлдээ шилжинэ).
    void Shake(float amount) { shake = std::min(1.0f, shake + amount); }
    // Харах чиглэлийг гаднаас бага зэрэг эргүүлнэ (онилгооны туслах); хязгаарыг Update барина.
    void AddView(float yawDelta, float pitchDelta)
    {
        yaw += yawDelta;
        pitch += pitchDelta;
    }

private:
    Camera3D camera = {};
    Vector3 follow = { 0.0f, 0.0f, 0.0f }; // тоглогчийн байрлалын зөөлрүүлсэн утга
    bool hasFollow = false;
    float yaw = 0.0f;    // радиан, 0 = +Z рүү харна
    float pitch = 0.3f;  // эерэг = камер дээрээс харна
    float distance;
    float aimBlend = 0.0f; // 0 = ердийн, 1 = бүрэн онилсон
    float scopeBlend = 0.0f; // 0 = ердийн, 1 = дуран хараа
    float rideDistance = 0.0f; // унаа унасан үеийн нэмэлт зай (аажим өөрчлөгдөнө)
    bool traversing = false;
    bool riding = false;
    bool firstPerson = false;
    float traverseBlend = 0.0f; // 0 = ердийн, 1 = давж буй үеийн ойрын камер
    float kickReturn = 0.0f;    // ухралтаас буцах үлдсэн pitch
    float shake = 0.0f;
};
