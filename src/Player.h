#pragma once

#include "Config.h"
#include "Humanoid.h"
#include "World.h"
#include "raylib.h"

#include <algorithm>

// Унаа унасан үеийн суудлын хэлбэр
enum class RideStyle
{
    None,   // явган
    Bike,   // мотоцикл, гурван дугуйт: урагш бөхийж сууна
    Saddle, // морь: хөлөө алцайлгаж сууна
    Hidden  // бүхээгтэй машин: жолооч харагдахгүй
};

// Биеийн байрлал: C суух, Z хэвтэх, Space босох.
enum class Stance
{
    Stand,
    Crouch, // удаан, жижиг бай, оновч сайн
    Prone   // маш удаан (мөлхөнө), хамгийн жижиг бай, хамгийн сайн оновч
};

class Player
{
public:
    explicit Player(Vector3 spawn);

    // Унаа унасан үед Update-ийн оронд дуудна: байрлал, харах зүг нь унааг дагана.
    // ground: унааны газар дээрх байрлал, seatHip: суудал дээрх ташааны байрлал.
    // rein: зүүн гарын барих цэг (жолоо, хазаар; ертөнцөд), hasRein = false бол гар чөлөөтэй.
    void Ride(float dt, Vector3 ground, Vector3 seatHip, float heading, RideStyle style, Vector3 rein = {},
              bool hasRein = false);
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
    // Амийн дээд хэмжээг өөрчилнө (нэмэгдсэн хэмжээгээр амь нэмэгдэнэ).
    void SetMaxHealth(float newMaxHealth)
    {
        health = std::max(1.0f, health + newMaxHealth - maxHealth);
        maxHealth = newMaxHealth;
    }
    void Teleport(Vector3 newPosition);
    // Хурдыг хөндөхгүйгээр хэвтээ байрлалыг засна (саадаас түлхэх).
    void Nudge(float x, float z) { position.x = x; position.z = z; }
    // Багийн өөр гишүүн рүү шилжих: түүний байрлал, харах зүг, амийг авна.
    void Assume(Vector3 newPosition, float newFacing, float newHealth, float newMaxHealth);
    // Гадаад төрх (удирдаж буй гишүүнээс хамаарна).
    void SetLook(const HumanoidLook& newLook) { look = newLook; }
    // Туршилт: дүрийг тогтсон байрлалд зурна (хөдөлгөөний үе шатыг дэлгэцийн зургаар шалгахад).
    void SetPoseDemo(float move, float phase, float aim, float actionProgress)
    {
        demo = true;
        demoMove = move;
        demoPhase = phase;
        demoAim = aim;
        demoAction = actionProgress;
    }
    void SetPoseDemoExtra(float direction, float reloadProgress)
    {
        demoDirection = direction;
        demoReload = reloadProgress;
    }

    // Буудах үед биеэ эргүүлж, бууг дээш доош хазайлгаж ертөнцийн энэ цэг (хараа) рүү чиглүүлнэ.
    void AimAtPoint(Vector3 target);
    Vector3 MuzzlePosition() const;
    // Буудсан мөчид дуудна: буу ухарч, гар татагдана.
    void OnFired() { recoil = 1.0f; }
    // Гартаа барих зүйл (зэвсэг солигдоход).
    void SetHeld(HeldItem item) { held = item; }
    // Хутгаар хатгах, гранат шидэх хөдөлгөөнийг эхлүүлнэ.
    void StartAction(float duration);
    float Facing() const { return facing; }
    // 1-р этгээдийн гар, бууг зурахад
    HeldItem Held() const { return held; }
    float Recoil() const { return recoil; }
    float AimPitch() const { return aimPitch; }
    float WalkPhase() const { return walkCycle; }
    float ReloadProgress() const { return reload; }
    float ActionProgress() const { return action; }

    // Шат, цонх зэрэг гарцыг туулж эхэлнэ: дуустал удирдлага түр зогсоно.
    void StartTraverse(const Passage& passage);
    bool IsTraversing() const { return traversing; }
    // Бүсэлхийнээс гүн усанд сэлж буй: хэвтээ хөвнө, буу барихгүй, буудахгүй
    bool IsSwimming() const { return swimming; }
    // Давж буй замын зүг: камер үүний ард байрлана (2 давхраас буухад гадна талаас цонх руу харна).
    float TraverseFacing() const { return traverse.facing; }

    Stance GetStance() const { return stance; }
    void SetStance(Stance newStance) { stance = newStance; }
    // Суух, хэвтэхэд зомби анзаарах зай багасна (1 = босоо).
    float Concealment() const { return 1.0f - 0.4f * crouchPose - 0.65f * pronePose; }
    // Сумны тархалтын үржүүлэгч: суусан, хэвтсэн үед буу тогтвортой.
    float SpreadScale() const { return 1.0f - 0.4f * crouchPose - 0.65f * pronePose; }
    // Камерын харах цэг хэдэн метр доошлох.
    float CameraDrop() const { return 0.5f * crouchPose + 1.2f * pronePose; }
    // Цэнэглэлтийн явц (гарын хөдөлгөөнд); -1 = цэнэглээгүй.
    void SetReload(float progress) { reload = progress; }

    void TakeDamage(float amount);
    // Хаанаас цохиулсныг тэмдэглэнэ (HUD-ийн чиглэлийн сум)
    void TakeDamageFrom(float amount, Vector3 source)
    {
        TakeDamage(amount);
        hitFrom = source;
        hitFromKnown = true;
    }
    bool HitDirection(Vector3& source) const
    {
        source = hitFrom;
        return hitFromKnown;
    }
    void Heal(float amount); // унасан бол босгоно
    bool IsAlive() const { return health > 0.0f; }
    // Унасан: амь дууссан ч kDownTime секунд босгохыг хүлээнэ (хөдлөх, буудах боломжгүй, хэвтэнэ). Энэ хооронд
    // цохиулбал хугацаа хурдан дуусна. Хугацаа дуусвал үхнэ (health = 0).
    static constexpr float kDownTime = 20.0f;
    // Дараагийн унахад хүлээх хугацаа (тулаанд kPvpDownTime)
    void SetDownDuration(float seconds) { downDuration = seconds; }
    bool IsDowned() const { return downed; }
    float DownTimeLeft() const { return downTimer; }
    void UpdateDowned(float dt);
    void Revive(float fraction); // амийн дээд хэмжээний fraction-тай босно
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
    float aimPose = 0.0f;      // 0 = зэвсгээ бэлэн барьсан .. 1 = онилсон (зөөлөн шилжинэ)
    float aimPitch = 0.0f;     // онилгооны босоо өнцөг, радиан (+ = дээш)
    Stance stance = Stance::Stand;
    float crouchPose = 0.0f;   // 0 = босоо .. 1 = суусан (зөөлөн шилжинэ)
    float pronePose = 0.0f;    // 0 = босоо .. 1 = хэвтсэн
    float moveDirection = 0.0f; // явж буй зүг, харж буй зүгтэй харьцангуй
    bool traversing = false;    // шат, цонхоор гарч яваа
    bool swimming = false;      // гүн усанд сэлж буй
    Passage traverse = {};
    float traverseTime = 0.0f;
    float traverseDuration = 1.0f;
    float reload = -1.0f;
    float recoil = 0.0f;       // 1 = дөнгөж буудсан, 0 руу буурна
    float landing = 0.0f;      // өндрөөс буусны дараа 1 -> 0: өвдгөө нугалж цохилтоо зөөлрүүлнэ
    float action = 0.0f;       // 1 -> 0: хатгах / шидэх хөдөлгөөний явц
    float actionDuration = 0.3f;
    HeldItem held = HeldItem::Rifle;
    HumanoidLook look;
    bool demo = false;
    float demoMove = 0.0f, demoPhase = 0.0f, demoAim = 0.0f, demoAction = -1.0f;
    float demoDirection = 0.0f, demoReload = -1.0f;
    RideStyle rideStyle = RideStyle::None;
    Vector3 seat = { 0.0f, 0.0f, 0.0f }; // унаа унасан үеийн ташааны байрлал
    float rideHeading = 0.0f;            // унааны зүг (бие онилсон зүг рүү эргэхэд ташаа, хөл энэ зүгт)
    Vector3 rein = { 0.0f, 0.0f, 0.0f };  // жолоо, хазаарын цэг (зүүн гар барина)
    bool hasRein = false;

    // Биеийг зурах, сум гарах цэгийг тооцох суурь (хөлийн түвшин).
    Vector3 BodyBase() const;
    // Бууны эргэх цэг (мөр), биеийн орон зайд.
    Vector3 GunPivot() const;
    float health = 100.0f;
    float maxHealth = 100.0f;
    float timeSinceHit = 10.0f;
    bool downed = false;
    float downTimer = 0.0f;
    float downDuration = kDownTime;
    Vector3 hitFrom = { 0.0f, 0.0f, 0.0f };
    bool hitFromKnown = false;
    bool grounded = true;
};
