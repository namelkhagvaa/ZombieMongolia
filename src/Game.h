#pragma once

#include "Campaign.h"
#include "Config.h"
#include "Environment.h"
#include "FuelBarrels.h"
#include "Grenades.h"
#include "Input.h"
#include "Minimap.h"
#include "Mission.h"
#include "Net.h"
#include "Particles.h"
#include "Player.h"
#include "Progress.h"
#include "Soldier.h"
#include "Team.h"
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
    int member = 0;           // 1..3 бол тэр багийн гишүүн рүү шилжиж эхэлнэ (--member N)
    bool demo = false;        // дүрийн байрлалыг тогтооно (--demo move phase aim action)
    float demoMove = 0.0f, demoPhase = 0.0f, demoAim = 0.0f, demoAction = -1.0f;
    float demoDirection = 0.0f, demoReload = -1.0f; // явах зүг (радиан), цэнэглэлтийн явц (--demo-extra D R)
    int useFrame = 0;         // >0 бол энэ дугаартай frame-д F дарсан мэт үйлдэнэ (--use-frame N)
    float moveForward = 0.0f, moveRight = 0.0f; // тоглогчийг тогтмол чиглэлд алхуулна (--move F R)
    int quotaSmall = -1, quotaBig = -1; // >= 0 бол үеийн зомбины нөөцийг энэ тоогоор солино (--quota S B)
    bool bindPose = false;    // загваруудыг анхны зогсолтоор нь зурна (--bindpose)
    bool touch = false;       // мэдрэгчтэй дэлгэцийн товчлууруудыг компьютер дээр туршина; хулгана нэг хурууг орлоно (--touch)
    int viewHeight = 0;       // >0 бол тоглоомыг ийм өндөртэй дотоод зурагт зураад сунгана (--view-height N)
    int stance = 0;           // 1 = суусан, 2 = хэвтсэн байдлаар эхэлнэ (--stance N)
    int hold = 0;             // 1..5 бол тэр товчны зэвсгийг буудалгүйгээр барина (--hold N)
    bool coopTest = false;    // туршилтын горимд ч сүлжээг ажиллуулна (--coop)
    bool scope = false;       // тасралтгүй онилно: мэргэн буудагчийн дуран харааг шалгахад (--scope)
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
        int passage = -1; // шат, цонх
    };
    Interaction FindInteraction() const;

    void SpawnVehicles();
    void UpdateVehicles(float dt);
    void UpdateShooting();
    // Дэлгэцийн төвийн хараа туссан ертөнцийн цэг.
    Vector3 CrosshairTarget() const;
    Vector3 EffectMuzzle() const;
    void UseKnife(Ray aim, float cameraDistance);
    void FireGun(Ray aim, float cameraDistance);
    void ThrowGrenade(Ray aim);
    void FireLauncher(Ray aim, float cameraDistance);
    HeldItem HeldFor(WeaponType type) const;

    // Баг: элсүүлсэн цэргүүд + тоглогчийн удирдаагүй хоёр гишүүнийг тоглогчийн дэргэд байрлуулна.
    void ResetSquad(int recruits);
    // target гишүүн рүү шилжинэ (амьд, өөр гишүүн бол). leaveDead: одоогийн гишүүн үхсэн тул цогцос нь үлдэнэ.
    bool SwitchMember(int target, bool leaveDead);
    // Тоглогч унаа унасан үед багийн гишүүд ойрын хэрэгсэлд сууж дагана; буухад тэд ч бууна.
    void UpdateTeamRiding(float dt);
    // Мэдрэгчтэй дэлгэц: буудаж / онилж байх үед харааг загалмайд ойр, харагдаж буй зомби руу зөөлөн татна.
    void AimAssist(float dt);

    void StartMission(int provinceIndex);
    void ReturnToMap();
    void CloseJournal();

    // Мэдрэгчтэй дэлгэцийн товчлууруудад хэрэгтэй төлөв (аль дэлгэц нээлттэй, зэвсэг, баг, боломжит үйлдлүүд).
    TouchState MakeTouchState() const;

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
    Arsenal arsenal;                // удирдаж буй гишүүний зэвсэг
    Arsenal arsenals[kTeamSize];    // гишүүн бүрийн зэвсэг, сум (шилжихэд хадгалагдана)
    int activeMember = 0;
    int missionFrame = 0;        // даалгаврын шинэчлэлтийн тоолуур (туршилтын командуудад)
    int teamVehicle = -1;        // багийн гишүүдийн сонгосон хэрэгсэл (-1 = байхгүй)
    float teamBoardTimer = 0.0f; // суух гэж эхэлснээс хойших хугацаа
    float escortStuck = 0.0f;    // дагаж буй хэрэгсэл гацсан хугацаа
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
    bool nearRecruitStation = false; // элсүүлэх цэгийн дэргэд (E дарж цэрэг авна)
    // Зомби устгахад дээр нь хөвөх "+N" зоос
    struct CoinPopup
    {
        Vector3 position;
        int coins;
        float age;
    };
    std::vector<CoinPopup> coinPopups;
    FuelBarrels barrels; // бензинтэй торхнууд

    // --- Хамтарсан тоглолт (Wi-Fi) ---
    struct RemoteFigure
    {
        Vector3 position = { 0.0f, 0.0f, 0.0f }, target = { 0.0f, 0.0f, 0.0f };
        float facing = 0.0f, targetFacing = 0.0f, speed = 0.0f, aimPitch = 0.0f, walk = 0.0f;
        uint8_t held = 0, member = 0, flags = 0;
        bool alive = true;
    };
    Net::Session net;
    std::vector<Net::Event> netEvents;
    bool coopGuest = false;         // өөр хүний тоглолтод зочноор нэгдсэн
    bool guestPlaced = false;       // зочин: хостын дэргэд гарч ирсэн
    float guestRespawnTimer = 0.0f;
    bool joinRequestPending = false; // хост: хариу өгөөгүй хүсэлт
    std::string joinRequester;
    bool remoteActive = false;      // хост: зочин холбогдсон
    RemoteFigure guestFigure;       // хост дээрх зочин
    RemoteFigure hostFigure;        // зочин дээрх хост
    std::vector<RemoteFigure> hostSoldiers;
    Net::Snapshot guestView;        // зочин: хостын сүүлийн мэдээлэл (даалгаврын бичиг г.м.)
    float coopSendTimer = 0.0f;
    bool waitingForReply = false;   // зочин: хүсэлт илгээгээд хүлээж байна
    float waitTimer = 0.0f;
    std::string coopStatus;         // газрын зураг: сүүлийн мэдээ ("Татгалзлаа" г.м.)
    std::string noticeText;

    void SetNotice(const std::string& text, float seconds);
    std::string PlayerName() const;
    void UpdateNetwork(float dt);
    void UpdateCoopMission(float dt);
    void SendHostSnapshot(float dt);
    void StartCoopGuest(int province);
    void LeaveCoop();
    void UpdateCoopMap();
    void DrawCoopMap() const;
    void DrawCoopOverlay() const;
    Rectangle CoopButtonRect(int row) const;
    void ObjectiveTexts(std::string& objective, std::string& detail) const;
    Net::AvatarState LocalAvatar() const;
    void UpdateRemoteFigure(RemoteFigure& figure, const Net::AvatarState& state, float dt, bool snap);
    void AdvanceRemoteFigure(RemoteFigure& figure, float dt) const;
    void DrawRemoteFigure(const RemoteFigure& figure, const HumanoidLook& look) const;
    std::vector<ZombieHorde::KillReward> killRewardBuffer;
    void DrawCoinPopups() const;
    float recruitNoticeTimer = 0.0f;
    const char* recruitNotice = nullptr;
    int lastReward = 0;

    bool showDebug = false;
    bool showHelp = false; // H: товчнуудын тайлбар (анхнаасаа нуугдсан: дэлгэц цэвэрхэн)
    TouchState touchState;
};
