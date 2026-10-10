#pragma once

#include "Layout.h"

// Тоглоомын бүх тохируулж болох утгууд нэг дор.
// Тоглож байх үед зарим нь товчоор өөрчлөгдөнө (Game.cpp-г үз).
struct GameConfig
{
    // Цонх
    int screenWidth = 1280;
    int screenHeight = 720;
    int targetFps = 60;

    // Тоглогч
    float walkSpeed = 6.0f;          // м/с
    float sprintMultiplier = 1.6f;   // Shift дарахад
    float jumpSpeed = 7.5f;          // үсрэлтийн анхны хурд, м/с
    float gravity = 22.0f;           // м/с^2
    float acceleration = 12.0f;      // хурдаа хэр түргэн авах
    float airControl = 0.35f;        // агаарт байх үеийн удирдлагын хувь
    float turnSpeed = 12.0f;         // биеэ хөдөлгөөний зүг рүү эргүүлэх хурд
    float playerRadius = 0.4f;
    float playerHeight = 1.8f;
    float stepHeight = 0.3f;         // үсрэлгүй гишгэж гарах өндөр
    float playerBaseHealth = 100.0f;
    float waveHeal = 25.0f;          // давалгаа дуусахад нөхөгдөх амь

    // Камер
    float mouseSensitivity = 0.003f; // радиан / пиксел
    bool invertY = false;
    float cameraDistance = 6.0f;
    float cameraMinDistance = 2.5f;
    float cameraMaxDistance = 16.0f;
    float cameraFollowSpeed = 18.0f; // их байх тусам камер тоглогчийг чанга дагана
    float cameraZoomStep = 0.8f;
    float cameraMinPitch = -0.35f;   // радиан
    float cameraMaxPitch = 1.35f;
    float cameraTargetHeight = 1.6f; // тоглогчийн хөлөөс дээш харах цэг
    float cameraShoulderOffset = 0.8f; // харах цэгийг баруун тийш шилжүүлнэ (онилгоо дүрээр халхлагдахгүй)
    float cameraFovY = 60.0f;

    // Онилох (хулганын баруун товч)
    float aimFovY = 35.0f;             // ойртуулсан харах өнцөг
    float aimDistance = 2.2f;          // камер мөрөнд ойртоно
    float aimSensitivityScale = 0.55f; // онилох үед хулгана удааширна
    float aimBlendSpeed = 12.0f;       // онилгоо руу шилжих хурд

    // Ертөнц
    float worldHalfSize = kPlayableHalfExtent; // тоглогч явж болох талбайн хагас хэмжээ (4.3 x 4.3 км)

    // График: сул компьютерт зориулсан тохиргоо
    bool msaa4x = false;            // ирмэг толийлгох; сул GPU дээр FPS унагадаг
    bool vsync = true;
    float detailDistance = 70.0f;   // өвс, чулуу, бутыг энэ зайнаас цааш зурахгүй
    float characterDrawDistance = 170.0f;
    float vehicleDrawDistance = 260.0f;
};
