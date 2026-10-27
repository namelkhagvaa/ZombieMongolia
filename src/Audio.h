#pragma once

#include "raylib.h"

// Тоглоомын дуу: буу, зомби, дэлбэрэлт, мөргөлдөөн, морины туурай, хөдөлгүүр.
//
// Дуу бүрийг эхлэхэд кодоор нийлэгжүүлнэ (synthesis): гадны файл, лиценз хэрэггүй. assets/sounds/<нэр>.wav
// (эсвэл .ogg) файл байвал түүнийг ашиглана: жишээ нь assets/sounds/rifle.wav гэж жинхэнэ бичлэг тавьж болно.
// Дуу 3D: сонсогчоос холдох тусам намсаж, зүүн/баруун талаас сонсогдоно.
enum class Sfx
{
    Rifle,
    Pistol,
    Shotgun,
    Sniper,
    Launcher,
    Knife,
    Throw,
    Reload,
    Explosion,
    ZombieGroan,
    ZombieAttack,
    ZombieDie,
    BossRoar,
    Hit,   // сум биед тусах
    Crash, // тээврийн хэрэгсэл мөргөлдөх
    Thud,  // унах, мөргөх (зөөлөн)
    Hoof,  // морины туурай
    Screech, // дугуй чахирах (шарвалт)
    Rumble,  // дэлбэрэлтийн гүн нүргээн (дотоод давхарга)
    Marker,    // сум онов (тоглогчид: богино "тик")
    Headshot,  // толгойд онов (төмөрлөг "дин")
    Heartbeat, // амь бага: зүрхний цохилт
    Count
};

// Хөдөлгүүрийн төрөл (дууны өнгө)
enum class EngineSound
{
    Off = -1,
    Car,   // жийп
    Moto,  // мотоцикл
    Trike, // гурван дугуйт
    Truck  // ачааны машин (дизель)
};

// Цэргийн яриа (assets/sounds/soldiers/<a|b|c>_<код>_<n>.wav): гурван өөр дуу хоолой (a Дайчин 1, b Дайчин 2,
// c Мэргэн буудагч = багийн гишүүний дугаар), үйл явдал бүрд 1-2 хувилбар.
enum class VoiceLine
{
    Spot,     // зомби харсан
    Enemy,    // нөгөө багийн хүн (тулаан)
    Fire,     // гал нээх
    Reload,
    Hurt,
    Downed,
    Revived,
    Lost,     // нөхөр нь үхсэн
    Follow,   // "над руу" тушаал
    Hold,     // "энд бай" тушаал
    Kill,
    Exploder, // тэсрэгч зомби ойртсон
    Boss,
    Swarm,    // олон зомби бүсэлсэн
    Ammo,     // нөөц сум дуусч байна
    Grenade,
    Vehicle,  // унаанд суух
    Crate,    // ачааны хайрцаг газардсан
    Idle,     // удаан чимээгүй
    Count
};

namespace Audio
{
    void Init();
    void Shutdown();
    void SetEnabled(bool enabled);
    void SetVolume(float volume); // 0 = дуугүй .. 1 = бүрэн
    bool Enabled();

    // Сонсогч (камер): байрлал, урагш чиглэл
    void SetListener(Vector3 position, Vector3 forward);
    // 3D дуу. volume: 0..1+, pitch: 1 = хэвийн
    void Play(Sfx sound, Vector3 position, float volume = 1.0f, float pitch = 1.0f);

    // Хөдөлгүүр (2 дуу хоолой: 0 = тоглогчийн унаа, 1 = ойрын бусад): rpm 0..1, throttle 0..1
    void SetEngine(int voice, EngineSound kind, float rpm, float throttle, Vector3 position);

    // Цэргийн яриа: speaker 0..2 (дуу хоолой), нэг удаад нэг л хүн ярина. urgent: ярьж буйг тасалж ярина.
    // Ярьсан бол true (бичлэг байхгүй, өөр хүн ярьж байгаа, хэт хол бол false).
    bool Say(int speaker, VoiceLine line, Vector3 position, float pitch = 1.0f, bool urgent = false);
    bool HasVoice(int speaker, VoiceLine line);
}
