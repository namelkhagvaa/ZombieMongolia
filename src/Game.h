#pragma once

#include "Campaign.h"
#include "Config.h"
#include "Environment.h"
#include "Airstrike.h"
#include "FuelBarrels.h"
#include "MedKits.h"
#include "Grenades.h"
#include "Input.h"
#include "Minimap.h"
#include "Mission.h"
#include "Navigation.h"
#include "Net.h"
#include "Particles.h"
#include "Weather.h"
#include "Player.h"
#include "Progress.h"
#include "Soldier.h"
#include "Team.h"
#include "ThirdPersonCamera.h"
#include "Tracers.h"
#include "Vehicle.h"
#include "Weapon.h"
#include "World.h"
#include "Splash.h"
#include "Zombie.h"

enum class GameState
{
    Map,     // газрын зураг + дэлгүүр
    Playing, // даалгавар
    Airstrike, // даалгавар биелсэн: онгоц үлдсэн зомбийг бөмбөгдөх богино кино (дараа нь Won)
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
    int weather = -1; // туршилт: цаг агаарыг тогтоох (--weather 0..4)
    int graphics = -1; // туршилт: графикийн түвшин (--graphics 0..2)
    int driftFrame = 0; // туршилт: энэ frame-ээс гар тоормос татаж баруун тийш эргэнэ (--drift N)
    int crashTest = -1; // туршилт: унаж буй хэрэгслийн урд 25 м-т энэ дугаартай хэрэгслийг хөндлөн тавина
    float rideHeading = -1000.0f; // туршилт: унааны чиглэл (градус), камераас тусдаа (--ride-heading)
    int zombies = 0;  // тоглогчийн урд шууд гаргах зомбины тоо (--zombies N)
    int soldiers = 0; // туршилтад багийн цэргийн тоо (--soldiers N)
    float cameraDistance = 0.0f; // >0 бол камерын зай (--zoom D)
    int weapon = 0;        // 1..5 бол тэр зэвсгийг сонгож, тасралтгүй хэрэглэнэ (--weapon N)
    float fullMapZoom = 0.0f; // >0 бол бүтэн газрын зургийг энэ томруулалтаар нээнэ (--fullmap Z)
    int province = 1;         // аль аймгийн даалгаврыг эхлүүлэх, аяны дугаараар (--province N)
    bool hasWaypoint = false; // туршилт: очих газрыг эхэнд нь тэмдэглэнэ (--waypoint X Z)
    float waypointX = 0.0f, waypointZ = 0.0f;
    bool campaignMap = false; // даалгавар эхлүүлэхгүй, аяны газрын зургийг харуулна (--campaign)
    int ride = 0;             // 1..4: жийп, мотоцикл, гурван дугуйт, морийг унаж шууд урагш явна (--ride N)
    float timeScale = 1.0f;   // туршилтад тоглоомын цагийг хурдасгана (--timescale X)
    bool journal = false;     // даалгаврын журналыг нээлттэй эхлүүлнэ (--journal)
    bool shop = false;        // дэлгүүрийн цэсийг нээлттэй эхлүүлнэ (--shop)
    bool characterSelect = false; // газрын зураг дээр дүр сонгох цонхыг нээнэ (--campaign --select)
    bool stayOnMap = false;       // --campaign --api: нэвтэрсний дараа аймаг эхлүүлэхгүй (дэлгүүр, Gold шалгах)
    bool airstrike = false;   // бөмбөгдөлтийн киног шууд эхлүүлнэ (--airstrike)
    float dayTime = -1.0f;    // туршилт: өдрийн цагийг 0..1 тогтооно (--daytime t)
    bool pause = false;       // тоглоомын цэсийг нээлттэй эхлүүлнэ (--pause)
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
    bool battleTest = false;  // --coop-той: данс, сервергүйгээр тулаан (хоёр баг бие бие рүүгээ буудна) (--battle)
    bool scope = false;       // тасралтгүй онилно: мэргэн буудагчийн дуран харааг шалгахад (--scope)
    bool autoPlay = false;    // даалгавруудыг автоматаар гүйцэтгэнэ (--autoplay)
    int stopAfter = 0;        // автомат тоглолт энэ тооны даалгаврын дараа зогсоно (--stop-after N)
    const char* savePath = nullptr; // тусдаа хадгалалтын файл; заасан бол туршилт ч хадгална (--save FILE)
    const char* apiBase = nullptr;  // данс, төлбөрийн сервер (--api http://127.0.0.1:8010/api/); хоосон бол анхдагч
    const char* relay = nullptr;    // интернэтийн relay сервер "хост:порт" эсвэл "off" (--relay 127.0.0.1:8091)
    int joinCode = 0;               // газрын зураг дээрээс энэ өрөөнд шууд нэгдэнэ (--join 1234, --campaign --coop-тай)
    bool show = false;              // туршилтын цонхыг дэлгэцэнд харуулна; үгүй бол нуугдмал ажиллана (--show)
    bool thirdPerson = false;       // туршилтыг 3-р этгээдийн камераар эхлүүлнэ: дүрийн бие харагдана (--third)
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

    // stageOverride: зочин даалгаврын явцыг өөрийн хадгалалт биш, хостын (өрөөний) ахицаар харуулна.
    void StartMission(int provinceIndex, StageSave* stageOverride = nullptr);
    void ReturnToMap();
    void CloseJournal();
    void OpenPause();
    void ClosePause();
    void UpdatePauseMenu();
    void DrawPauseMenu() const;
    void SetFullscreen(bool enabled);
    void UpdateAirstrike(float dt);
    void OpenShop();
    void CloseShop();
    void UpdateShopMenu();
    void BuyShopOffer(int index);
    void DrawShopMenu() const;

    // Мэдрэгчтэй дэлгэцийн товчлууруудад хэрэгтэй төлөв (аль дэлгэц нээлттэй, зэвсэг, баг, боломжит үйлдлүүд).
    TouchState MakeTouchState() const;

    void Draw() const;
    void DrawMission() const;      // HUD ба 2D давхарга (3D зургийн дараа)
    void DrawMissionScene() const; // тэнгэр + 3D ертөнц
    void RenderShadows() const;    // нарны сүүдрийн зураг (frame бүрд, 3D-ээс өмнө)
    void WatchPerformance(float dt); // удаан бол графикийг автоматаар бууруулна
    void UpdateSounds(float dt, float throttle); // сонсогч, хөдөлгүүр, зомбины гинших дуу
    void DrawMissionCompleted() const;

    GameConfig config;
    LaunchOptions options;
    GameWindow window; // доорх гишүүдээс өмнө байх ёстой
    SplashMark splashShaders{ 0.03f, "Графикийг бэлтгэж байна..." };
    mutable Environment environment; // зурах үед сүүдэр, дэлгэцийн эффектийн текстурт бичнэ
    SplashMark splashWorld{ 0.2f, "Газар нутгийг бэлтгэж байна..." };
    World world;
    Player player;
    ThirdPersonCamera camera;
    Arsenal arsenal;                // удирдаж буй гишүүний зэвсэг
    Arsenal arsenals[kTeamSize];    // гишүүн бүрийн зэвсэг, сум (шилжихэд хадгалагдана)
    int ammoReserve = 0;            // багийн нөөц сум (тоглолт бүр 5000-аар эхэлнэ, дэлгүүрээс тоглолтын зоосоор)

    // ---- Тулаан (нээлттэй өрөө): эсрэг багийн үндсэн 3 дайчныг алсан баг ялна, зомби хоёуланд нь дайрна ----
    bool battleActive = false;      // хост: зочин орж ирсэн; зочин: тасалбараар өрөөнд орсон
    bool hostGone = false;          // зочин: хост тасарсан (сервер 60 секунд хүлээгээд зочныг ялсанд тооцно)
    bool guestTeamSpawned = false;  // хост: энэ тулаанд зочны AI дайчдыг нэг л удаа үүсгэнэ
    float takeoverCooldown = 0.0f;  // хост: зочин AI дайчныхаа байранд орсны дараа ялалтыг шалгахгүй хугацаа
    float battleWaitTimer = 0.0f;   // хост: зочин нэгдээд ирэхийг хүлээсэн хугацаа
    float guestHealth = 0.0f;       // хост: зочны сүүлд мэдэгдсэн амь (тасарвал AI дайчин болно)
    float idleTimer = 0.0f;         // тулаанд оролтгүй өнгөрсөн хугацаа: 60 секундээс AI удирдана
    bool autopilot = false, autoFire = false;
    float stageUploadTimer = 0.0f;  // хост: өрөөний ахицыг серверт илгээх
    std::vector<int8_t> hostSoldierSide; // зочин: snapshot-ын цэрэг бүр: 1 хостын дайчин, 2 өөрийн баг, 0 элсүүлсэн
    // ---- Зочин хосттой адил: өөрийн AI гишүүд рүү шилжих, дэлгүүр, журнал (хост баталгаажуулна) ----
    StageSave guestStage;           // зочин: хостын өрөөний ахиц (StageSync), өөрийн хадгалалтыг хөндөхгүй
    float guestSwitchTimer = 0.0f;  // зочин: шилжих хүсэлт илгээсний дараа давхар илгээхгүй
    float stageSyncTimer = 0.0f;    // хост: өрөөний ахицыг зочинд илгээх
    float guestMemberHealth = 100.0f; // хост: зочны гишүүдийн амийн дээд хэмжээ (GuestSetup)
    bool guestSetupSent = false;    // зочин: нэгдсэний дараа тохиргоогоо илгээсэн
    void GuestTakeOver(int index, bool leaveDead, bool hold); // хост: зочныг index дахь AI гишүүний байранд
    void GuestTeamInput();          // зочин: Tab / F1-F3 / "энд бай"
    void AcceptGuest();             // хост: нэгдэх хүсэлтийг хүлээн авна (нээлттэй өрөөнд асуулгүй, тулаан эхэлнэ)
    void UpdateBattle(float dt);
    void AutoFight(float dt);
    void MigrateToHost();
    void WinBattleAsHost();
    int TeamAlive(bool hostTeam) const;
    // Сум тулаанд оролцогч хүнд тусав уу: хост буудвал kGuestTarget эсвэл squad-ийн индекс (зочны баг),
    // зочин буудвал Net::kHostPlayer эсвэл hostSoldiers-ийн индекс; тусаагүй бол -1.
    int RaycastHumans(Ray ray, float minDistance, float maxDistance, float& distance, bool& head) const;
    int activeMember = 0;
    int missionFrame = 0;        // даалгаврын шинэчлэлтийн тоолуур (туршилтын командуудад)
    int teamVehicle = -1;        // багийн гишүүдийн сонгосон хэрэгсэл (-1 = байхгүй)
    Navigator navigator;         // газрын зураг дээр тэмдэглэсэн газар руу хөтлөх зам
    // Хамтарсан тоглолт: зочинд сүүлд илгээсэн унааны байрлал, эрүүл мэнд (хөдлөөгүйг дахин илгээхгүй)
    std::vector<Vector3> sentVehiclePosition;
    std::vector<float> sentVehicleHealth;
    int vehicleResend = 0; // хөдлөөгүй унааг ч ээлжлэн илгээнэ (алдагдсан пакетыг нөхнө)
    float teamBoardTimer = 0.0f; // суух гэж эхэлснээс хойших хугацаа
    float escortStuck = 0.0f;    // дагаж буй хэрэгсэл гацсан хугацаа
    Grenades grenades;
    SplashMark splashVehicles{ 0.68f, "Тээврийн хэрэгслүүдийг бэлтгэж байна..." };
    Garage garage;
    ZombieHorde horde;
    SoldierSquad squad;
    Tracers tracers;
    Particles particles;
    Precipitation precipitation; // бороо, цас, шороон шуурга
    float dustTimer = 0.0f;
    float smokeTimer = 0.0f;
    float groanTimer = 0.0f;
    float driftTimer = 0.0f;
    float screechTimer = 0.0f;
    bool wasReloading = false;
    float perfTime = 0.0f;   // график автоматаар бууруулах: хэмжилтийн хугацаа ба frame-ийн тоо
    int perfFrames = 0;
    float fuelCoinDebt = 0.0f;   // шатахуун түгээх газарт хийсэн бензин, засварын төлбөр (бүхэл зоос болтол)
    bool fuelWarned = false;

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
    int medKitKills = 0; // эмнэлгийн хайрцаг унахад тоолох устгалт
    bool inShop = false;   // дэлгүүр дотор (E дарж цэс нээнэ)
    float dayClock = 0.0f;   // өдөр шөнийн мөчлөг, секунд (kDayLength-ээр эргэнэ)
    bool wasNight = false;
    void AdvanceDay(float dt);
    bool paused = false;   // ESC: тоглоомын цэс (үргэлжлүүлэх, бүтэн дэлгэц, газрын зураг, гарах)
    bool quitRequested = false;
    bool aimToggled = false; // компьютер: баруун товчоор онилгоог асааж / унтраана (дахин дарахад болино)
    // H ("Энд бай"): удирдаж буй гишүүнийг тэмдэглэнэ; өөр гишүүн рүү шилжихэд тэр байрандаа үлдэж байлдана.
    bool holdMark[3] = { false, false, false };
    bool shopOpen = false; // дэлгүүрийн цэс нээлттэй (тоглоом түр зогсоно)
    const char* shopMessage = nullptr; // цэсний доод мөрөнд: сүүлийн худалдан авалтын үр дүн
    // Зомби устгахад дээр нь хөвөх "+N" зоос
    struct CoinPopup
    {
        Vector3 position;
        int coins;
        float age;
    };
    std::vector<CoinPopup> coinPopups;
    FuelBarrels barrels; // бензинтэй торхнууд
    MedKits medKits;     // 100 устгалт тутам унах эмнэлгийн хайрцаг
    Airstrike airstrike; // үе давахад онгоцны бөмбөгдөлтийн кино

    // --- Хамтарсан тоглолт (Wi-Fi) ---
    struct RemoteFigure
    {
        Vector3 position = { 0.0f, 0.0f, 0.0f }, target = { 0.0f, 0.0f, 0.0f };
        float facing = 0.0f, targetFacing = 0.0f, speed = 0.0f, aimPitch = 0.0f, walk = 0.0f;
        uint8_t held = 0, member = 0, flags = 0;
        uint8_t skin = 0;         // тэр тоглогчийн 1-р дайчны дүр
        bool alive = true;
        uint8_t shots = 0;        // сүүлд харуулсан буудлагын тоолуур
        bool synced = false;      // тоолуурыг анх авсан эсэх (эхний мэдээнд хуучин сумыг зурахгүй)
        float recoil = 0.0f;      // 1 = дөнгөж буудсан
        float aimTimer = 0.0f;    // >0 үед буугаа онилсон байрлалтай (буудсаны дараа хэсэг хугацаа)
        bool aiming = false;      // нөгөө тал онилж байна
        int vehicle = -1;         // тоглогч: жолоодож буй унаа (-1 = явган)
        uint8_t seatStyle = 0;    // цэрэг: унаан дээрх суудал (3 = бүхээг, харагдахгүй)
        Vector3 seatOffset = { 0.0f, 0.0f, 0.0f }; // цэрэг: байрлалаас суудал хүртэл
        Vector3 velocity = { 0.0f, 0.0f, 0.0f };   // хоёр мэдээнээс тооцсон хурд (хоцролтыг нөхөж урагш таамаглана)
        float sinceUpdate = 0.0f;                   // сүүлийн мэдээнээс хойш
    };
    int guestVehicle = -1;        // хост: зочны жолоодож буй унаа
    int pendingDoor = -1;         // зочин: сая өөрөө нээсэн/хаасан хаалга (хост хүлээн авах хүртэл snapshot-оор дарахгүй)
    float pendingDoorTimer = 0.0f;
    uint8_t localShots = 0;       // энэ тоглогчийн буудсан сумны тоолуур (хамтарсан тоглолтод илгээнэ)
    Vector3 localShotEnd = { 0.0f, 0.0f, 0.0f };
    Net::Session net;
    std::vector<Net::Event> netEvents;
    bool coopGuest = false;         // өөр хүний тоглолтод зочноор нэгдсэн
    bool guestPlaced = false;       // зочин: хостын дэргэд гарч ирсэн
    float guestRespawnTimer = 0.0f;
    std::vector<ZombieHorde::KillReward> remoteRewardBuffer; // хост: зочны унаа устгасан зомбины зоос
    bool joinRequestPending = false; // хост: хариу өгөөгүй хүсэлт
    std::string joinRequester;
    std::string hostName;           // зочин: нэгдсэн тоглолтын хостын (бүртгэлтэй) нэр
    bool helpedHost = false;        // зочноор хостод тусалж аймгийг давсан (өөрийн аймаг нээгдэхгүй)
    bool remoteActive = false;      // хост: зочин холбогдсон
    RemoteFigure guestFigure;       // хост дээрх зочин
    RemoteFigure hostFigure;        // зочин дээрх хост
    std::vector<RemoteFigure> hostSoldiers;
    Net::Snapshot guestView;        // зочин: хостын сүүлийн мэдээлэл (даалгаврын бичиг г.м.)
    float coopSendTimer = 0.0f;
    bool waitingForReply = false;   // зочин: хүсэлт илгээгээд хүлээж байна
    bool characterSelectOpen = false; // даалгавар эхлэхийн өмнө 1-р дайчны дүр сонгох цонх
    int characterChoice = 0;
    int pendingProvince = -1;         // сонгосны дараа эхлэх аймаг
    bool codeEntryOpen = false;     // өрөөний код оруулах цонх нээлттэй
    std::string codeEntry;          // оруулсан цифрүүд
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
    Rectangle CodeButtonRect() const; // "КОДООР НЭГДЭХ" (интернэт)
    // Өрөөний код оруулах цонх (тоон товчлуур; гарны тоо ч болно)
    void UpdateCharacterSelect();
    void DrawCharacterSelect() const;
    // Мэдрэгчтэй дэлгэцийн багийн картын хөргүүд: гишүүн бүрийн толгой, цээж (удирдагчийн дүр солигдоход дахин)
    void RenderPortraits() const;
    mutable RenderTexture2D portraits[3] = {};
    mutable int portraitSkin = -1;
    Rectangle CharacterCardRect(int skin) const;
    Rectangle CharacterStartRect() const;
    void OpenCodeEntry();
    void UpdateCodeEntry();
    void DrawCodeEntry() const;
    void ForceCodeSubmit(); // оруулсан кодоор нэгдэх хүсэлт илгээнэ
    Rectangle CodeKeyRect(int key) const; // 0..11: 1-9, ←, 0, OK
    void ObjectiveTexts(std::string& objective, std::string& detail) const;
    Net::AvatarState LocalAvatar() const;
    void UpdateRemoteFigure(RemoteFigure& figure, const Net::AvatarState& state, float dt, bool snap);
    void ShowRemoteShots(RemoteFigure& figure, uint8_t shots, Vector3 shotEnd);
    void AdvanceRemoteFigure(RemoteFigure& figure, float dt) const;
    void DrawRemoteFigure(const RemoteFigure& figure, const HumanoidLook& look) const;
    std::vector<ZombieHorde::KillReward> killRewardBuffer;
    void DrawCoinPopups() const;
    bool inTown = false;             // тоглогч аймгийн төвд байгаа (орох үед нэрийг харуулна)
    float recruitNoticeTimer = 0.0f;
    const char* recruitNotice = nullptr;
    int lastReward = 0;

    bool showDebug = false;
    bool showHelp = false; // F10: товчнуудын тайлбар (анхнаасаа нуугдсан: дэлгэц цэвэрхэн)
    TouchState touchState;
};
