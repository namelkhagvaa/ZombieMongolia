#pragma once

#include "Campaign.h"
#include "Config.h"
#include "Environment.h"
#include "Grenades.h"
#include "Minimap.h"
#include "Mission.h"
#include "Particles.h"
#include "Player.h"
#include "Progress.h"
#include "Soldier.h"
#include "ThirdPersonCamera.h"
#include "Tracers.h"
#include "Vehicle.h"
#include "Weapon.h"
#include "World.h"
#include "Zombie.h"

enum class GameState
{
    Map,     // газрын зураг + дэлгүүр
    Playing, // даалгавар
    Won,     // аймаг чөлөөлөгдсөн
    Lost     // тоглогч үхсэн
};

// Командын мөрийн сонголтууд. Хөгжүүлэлтийн үед орчныг шалгахад ашиглана:
//   ZombieMongolia.exe --screenshot shot.png [x z yawDeg pitchDeg] [--msaa]
// нь шууд даалгавар эхлүүлж, FPS хэмжээд, дэлгэцийн зураг хадгалаад гарна.
struct LaunchOptions
{
    const char* screenshotPath = nullptr;
    bool hasPose = false;
    float x = 0.0f, z = 0.0f, yawDegrees = 0.0f, pitchDegrees = 17.0f;
    bool forceMsaa = false;
    int frames = 120; // хэмжих frame-ийн тоо (--frames N)
    int zombies = 0;  // тоглогчийн урд шууд гаргах зомбины тоо (--zombies N)
    int soldiers = 0; // туршилтад багийн цэргийн тоо (--soldiers N)
    float cameraDistance = 0.0f; // >0 бол камерын зай (--zoom D)
    int weapon = 0;        // 1..5 бол тэр зэвсгийг сонгож, тасралтгүй хэрэглэнэ (--weapon N)
    float fullMapZoom = 0.0f; // >0 бол бүтэн газрын зургийг энэ томруулалтаар нээнэ (--fullmap Z)
    int province = 1;         // аль аймгийн даалгаврыг эхлүүлэх, аяны дугаараар (--province N)
    bool campaignMap = false; // даалгавар эхлүүлэхгүй, аяны газрын зургийг харуулна (--campaign)
    int ride = 0;             // 1..4: жийп, мотоцикл, гурван дугуйт, морийг унаж шууд урагш явна (--ride N)
    float timeScale = 1.0f;   // туршилтад тоглоомын цагийг хурдасгана (--timescale X)
    bool journal = false;     // даалгаврын журналыг нээлттэй эхлүүлнэ (--journal)
    bool autoPlay = false;    // даалгавруудыг автоматаар гүйцэтгэнэ (--autoplay)
    int stopAfter = 0;        // автомат тоглолт энэ тооны даалгаврын дараа зогсоно (--stop-after N)
    const char* savePath = nullptr; // тусдаа хадгалалтын файл; заасан бол туршилт ч хадгална (--save FILE)
};

// Цонхыг бусад гишүүдээс өмнө нээж, хамгийн сүүлд хаана (mesh, shader нь цонхноос хамаардаг).
struct GameWindow
{
    GameWindow(const GameConfig& config, const LaunchOptions& options);
    ~GameWindow();
};

// Цонх, үндсэн давталт, дэд системүүдийг холбоно.
class Game
{
public:
    explicit Game(const LaunchOptions& options);
    ~Game();

    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;

    void Run();

private:
    void HandleSettingsInput();
    void Save() const;

    void UpdateMap();
    void UpdateMission(float dt);
    // F товчоор харилцах зүйл: ойрын унаа эсвэл хаалга (аль нь ч байхгүй бол хоёулаа -1).
    struct Interaction
    {
        int vehicle = -1;
        int door = -1;
    };
    Interaction FindInteraction() const;

    void SpawnVehicles();
    void UpdateVehicles(float dt);
    void UpdateShooting();
    void UseKnife(Ray aim, float cameraDistance);
    void FireGun(Ray aim, float cameraDistance);
    void ThrowGrenade(Ray aim);
    HeldItem HeldFor(WeaponType type) const;

    void StartMission(int provinceIndex);
    void ReturnToMap();
    void CloseJournal();

    void Draw() const;
    void DrawMission() const;
    void DrawMissionCompleted() const;

    GameConfig config;
    LaunchOptions options;
    GameWindow window; // доорх гишүүдээс өмнө байх ёстой
    Environment environment;
    World world;
    Player player;
    ThirdPersonCamera camera;
    Arsenal arsenal;
    Grenades grenades;
    Garage garage;
    ZombieHorde horde;
    SoldierSquad squad;
    Tracers tracers;
    Particles particles;

    Progress progress;
    MapScreen mapScreen;
    FullMap fullMap;
    GameState state = GameState::Map;

    // Одоогийн үеийн төлөв
    int provinceIndex = 0;
    MissionParams params = {}; // үеийн хүндрэл (зомбины амь, хохирол, шагнал)
    Mission mission;           // үеийн даалгаврууд
    bool journalOpen = false;  // Q: даалгаврын журнал нээлттэй (тоглоом түр зогссон)
    int missionKills = 0;
    int lastReward = 0;

    bool showDebug = false;
    bool showHelp = true;
};
