#pragma once

#include "Config.h"
#include "Humanoid.h"
#include "World.h"
#include "raylib.h"

// Унаа унасан үеийн суудлын хэлбэр
enum class RideStyle
{
    None,   // явган
    Bike,   // мотоцикл, гурван дугуйт: урагш бөхийж сууна
    Saddle, // морь: хөлөө алцайлгаж сууна
    Hidden  // бүхээгтэй машин: жолооч харагдахгүй
};

class Player
{
public:
    explicit Player(Vector3 spawn);

    // Унаа унасан үед Update-ийн оронд дуудна: байрлал, харах зүг нь унааг дагана.
    // ground: унааны газар дээрх байрлал, seatHip: суудал дээрх ташааны байрлал.
    void Ride(float dt, Vector3 ground, Vector3 seatHip, float heading, RideStyle style);
    // Унаанаас буух: байрлал нь exitPosition болно.
    void StopRiding(Vector3 exitPosition);
    bool IsRiding() const { return rideStyle != RideStyle::None; }
    // Явган үед бусад объект (зогсож буй унаа) түлхэж гаргахад.
    void SetHorizontalPosition(float x, float z)
    {
        position.x = x;
        position.z = z;
    }

    // cameraYaw: W товч камерын харж буй зүг рүү явуулахын тулд.
    void Update(float dt, const GameConfig& config, const World& world, float cameraYaw);
    void Draw(const GameConfig& config, bool showCollider) const;

    // Эхлэх цэгт, бүрэн амьтай буцаана.
    void Reset(float newMaxHealth);
    void Teleport(Vector3 newPosition);

    // Буудах үед биеэ камерын зүг рүү шууд эргүүлнэ.
    void AimAt(float yaw);
    Vector3 MuzzlePosition() const;
    // Буудсан мөчид дуудна: буу ухарч, гар татагдана.
    void OnFired() { recoil = 1.0f; }
    // Гартаа барих зүйл (зэвсэг солигдоход).
    void SetHeld(HeldItem item) { held = item; }
    // Хутгаар хатгах, гранат шидэх хөдөлгөөнийг эхлүүлнэ.
    void StartAction(float duration);
    float Facing() const { return facing; }

    void TakeDamage(float amount);
    void Heal(float amount);
    bool IsAlive() const { return health > 0.0f; }
    float Health() const { return health; }
    float MaxHealth() const { return maxHealth; }
    float TimeSinceHit() const { return timeSinceHit; }

    Vector3 Position() const { return position; } // хөлийн байрлал
    bool IsGrounded() const { return grounded; }
    float HorizontalSpeed() const;

private:
    Vector3 spawnPoint;
    Vector3 position;
    Vector3 velocity = { 0.0f, 0.0f, 0.0f };
    float facing = 0.0f;       // радиан, 0 = +Z
    float floorY = 0.0f;       // доорх гадаргуу (сүүдэрт)
    float walkCycle = 0.0f;    // хөл гарын савлалтын үе
    float aimTimer = 0.0f;     // >0 үед хөдөлгөөн биеийг эргүүлэхгүй
    float recoil = 0.0f;       // 1 = дөнгөж буудсан, 0 руу буурна
    float action = 0.0f;       // 1 -> 0: хатгах / шидэх хөдөлгөөний явц
    float actionDuration = 0.3f;
    HeldItem held = HeldItem::Rifle;
    RideStyle rideStyle = RideStyle::None;
    Vector3 seat = { 0.0f, 0.0f, 0.0f }; // унаа унасан үеийн ташааны байрлал

    // Биеийг зурах, сум гарах цэгийг тооцох суурь (хөлийн түвшин).
    Vector3 BodyBase() const;
    float health = 100.0f;
    float maxHealth = 100.0f;
    float timeSinceHit = 10.0f;
    bool grounded = true;
};
