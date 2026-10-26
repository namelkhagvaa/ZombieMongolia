#include "ViewCull.h"
#include "Profile.h"
#include "Game.h"
#include "City.h"
#include "Structures.h"
#include "VehicleModel.h"

#include "Account.h"
#include "Audio.h"
#include "Platform.h"
#include "Input.h"

#include "CharacterModel.h"
#include "HorseModel.h"
#include "Hud.h"
#include "Humanoid.h"
#include "Minimap.h"
#include "UiFont.h"
#include "Shop.h"
#include "Layout.h"
#include "UiStyle.h"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>

namespace
{
    const float kSoundLevels[4] = { 1.0f, 0.6f, 0.3f, 0.0f }; // ESC цэсний "Дуу" түвшнүүд

    // Компьютерийн видео карт, процессороор тохирох график (0 бага, 1 дунд, 2 өндөр). Тусдаа видео карт: өндөр,
    // хүчтэй суурилагдсан график: дунд, бусад (Intel UHD/HD г.м.): бага. 4-өөс цөөн цөмтэй процессор эсвэл хөтөч
    // (WebGL удаан): дундаас дээш биш. Хэт хүнд байвал WatchPerformance буцааж бууруулна.
    int RecommendedGraphics()
    {
        std::string gpu = GpuName();
        for (char& c : gpu) c = (char)std::tolower((unsigned char)c);
        auto has = [&](const char* word) { return gpu.find(word) != std::string::npos; };
        int tier = 0;
        if (has("geforce") || has("rtx") || has("gtx") || has("quadro") || has("radeon rx") || has("radeon pro") ||
            has("intel(r) arc") || has("intel arc") || has(" arc(tm)"))
        {
            tier = 2;
        }
        else if (has("iris xe") || has("iris(r) xe") || has("680m") || has("760m") || has("780m") || has("880m") ||
                 has("890m") || has("radeon(tm) graphics") || has("apple m"))
        {
            tier = 1;
        }
        if (has("swiftshader") || has("llvmpipe") || has("basic render") || has("microsoft basic")) tier = 0; // програмаар зурдаг
        const int cores = CpuCores();
        if (cores > 0 && cores <= 4) tier = std::min(tier, 1);
#if defined(__EMSCRIPTEN__)
        tier = std::min(tier, 1);
#endif
        TraceLog(LOG_INFO, "GAME: GPU \"%s\", %d CPU threads -> graphics %d", GpuName(), cores, tier);
        return tier;
    }
}

namespace
{
    const float kHeadshotDamage = 1.0e6f; // ямар ч зомбины амиас их
    const float kMountDistance = 2.2f;    // унааны ирмэгээс ийм зайд F дарж сууна
    const float kDoorDistance = 2.4f;     // хаалганы төвөөс ийм зайд F дарж нээж хаана
    const float kPassageDistance = 1.3f;  // шат, цонхны эхлэх цэгээс ийм зайд F дарж туулна
    // 1000 зомби устгах тутам зомбины хурд 1.5 дахин нэмэгдэнэ; тоглох боломжтой байхын тулд 3 дахинаар хязгаарлана.
    const int kKillsPerSpeedup = 1000;
    const float kDayLength = 480.0f; // бүтэн өдөр шөнө, секунд (≈5 мин өдөр, 2 мин шөнө)
    const float kZombieSpeedupStep = 1.5f;
    const float kMaxZombieSpeedMultiplier = 3.0f;
    // Даалгаврын доторх дэлгүүр: тоглолтын зоосоор (зомби алж олно), тоглолт дуусахад зоос, авсан юм алга болно.
    const int kRecruitCost = 300;         // нэг цэрэг элсүүлэх үнэ (энэ тоглолтод л)
    const int kGrenadeCost = 80;          // 4 гранат
    const int kHealCost = 100;            // багийн гурван гишүүний амийг 100% болгох
    const int kAmmoCost = 60;             // нөөц сум +kAmmoPack
    const int kAmmoPack = 500;
    const int kStartAmmo = 5000;          // тоглолт бүрийн эхний нөөц сум (багт нийтдээ)

    // Даалгаврын үеийн дэлгүүрийн бараа (цэсний дараалал = 1..7 товч).
    // (Байнгын сайжруулалт зөвхөн газрын зургийн дэлгүүрт Gold-оор.)
    enum class ShopOffer { Ammo, Grenades, Heal, Recruit, Count };

    // Цэсний мөрүүдийн байрлал: зурах ба товшилт шалгах хоёр үүнийг ашиглана.
    const float kShopPanelWidth = 620.0f, kShopRowHeight = 52.0f, kShopTop = 150.0f;
    Rectangle ShopRow(int index)
    {
        const float x = ((float)ViewWidth() - kShopPanelWidth) * 0.5f + 20.0f;
        return { x, kShopTop + (float)index * (kShopRowHeight + 6.0f), kShopPanelWidth - 40.0f, kShopRowHeight };
    }

    // Гарцын үйлдлийн нэр: [0] гарын заавар, [1] мэдрэгчтэй дэлгэцийн товчлуур.
    const char* PassageLabel(PassageKind kind, bool touch)
    {
        switch (kind)
        {
        case PassageKind::LadderUp: return touch ? "ШАТААР АВИРАХ" : "F — шатаар авирах";
        case PassageKind::LadderDown: return touch ? "ШАТААР БУУХ" : "F — шатаар буух";
        case PassageKind::WindowOut: return touch ? "ЦОНХООР ГАРАХ" : "F — цонхоор гарах";
        case PassageKind::WindowIn: return touch ? "ЦОНХООР ОРОХ" : "F — цонхоор орох";
        case PassageKind::FenceOver: return touch ? "ХАШАА ДАВАХ" : "F — хашаа давах";
        default: return touch ? "ЦОНХНООС БУУХ" : "F — цонхноос зүүгдэж буух";
        }
    }
    const float kTeamVehicleRange = 70.0f; // багийн гишүүд ийм зайн доторх хэрэгслийг сонгож сууна
    const float kTeamBoardTime = 6.0f;     // ийм хугацаанд хүрч амжаагүй гишүүн шууд сууна

    // Хулганыг цонхонд түгжинэ (камерт). Мэдрэгчтэй дэлгэцэд заагч түгжигдэхгүй.
    void LockCursor()
    {
        if (!InTouchMode())
        {
            DisableCursor();
        }
    }
}

GameWindow::GameWindow(const GameConfig& config, const LaunchOptions& options)
{
    const bool benchmark = options.screenshotPath != nullptr;

    unsigned int flags = 0;
    if (config.msaa4x || options.forceMsaa) flags |= FLAG_MSAA_4X_HINT;
    if (config.vsync && !benchmark) flags |= FLAG_VSYNC_HINT;
    // Туршилтын цонх идэвхгүй нээгдэнэ: хэрэглэгчийн ажиллаж буй програмаас гарын оролтыг булаахгүй.
    if (benchmark) flags |= FLAG_WINDOW_UNFOCUSED;
    // Туршилт анхнаасаа нуугдмал цонхонд ажиллана (дэлгэцэнд гарч хэрэглэгчийн ажилд саад болохгүй).
    // Нуугдсан цонхны дэлгэцийн buffer найдваргүй тул зураглалыг дотоод зурагт хийж, дэлгэцийн зургийг тэндээс авна.
    const bool hidden = benchmark && !options.show;
    if (hidden) flags |= FLAG_WINDOW_HIDDEN;
    SetConfigFlags(flags);

#if defined(__ANDROID__)
    InitWindow(0, 0, "Zombie Mongolia"); // утасны бүтэн дэлгэц
    InSetTouchMode(true);
    ViewInit(720);
#else
    InitWindow(config.screenWidth, config.screenHeight, "Zombie Mongolia");
    // Цонхны икон (tools\make_icons.ps1 үүсгэнэ): exe-ийн хажуу эсвэл төслийн assets хавтсаас
    const std::string installed = std::string(GetApplicationDirectory()) + "assets/window_icon.png";
    for (const char* path : { "assets/window_icon.png", "../assets/window_icon.png", installed.c_str() })
    {
        if (!FileExists(path)) continue;
        Image icon = LoadImage(path);
        ImageFormat(&icon, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        SetWindowIcon(icon);
        UnloadImage(icon);
        break;
    }
    InSetTouchMode(options.touch || WebIsTouchDevice()); // хөтөч: утас, таблет дээр мэдрэгчтэй товчлуурууд
    ViewInit(hidden && options.viewHeight <= 0 ? GetScreenHeight() : options.viewHeight);
#endif
#if defined(__EMSCRIPTEN__)
    SetTargetFPS(0); // хөтөч: кадрын хурдыг requestAnimationFrame тогтооно (FrameYield)
#else
    SetTargetFPS(benchmark ? 0 : config.targetFps); // хэмжилтийн үед FPS-ийг хязгаарлахгүй
#endif
    SetExitKey(KEY_NULL); // ESC-ийг өөрсдөө боловсруулна: эхлээд нээлттэй цонхыг хаана, дараа нь л гарна
    LoadUiFont();
    Splash::Begin(!benchmark); // туршилтын горимд эхлэлийн дэлгэцгүй

    // Ойрын хавтгайг холдуулснаар алсад гүний нарийвчлал сайжирна (зам, сүүдэр анивчихгүй).
    // Алсын хавтгай 12 км: хотын эргэн тойрны жинхэнэ уулс (World::FarLand) харагдана.
    rlSetClipPlanes(0.25, 12000.0);
}

GameWindow::~GameWindow()
{
    Audio::Shutdown();
    UnloadUiFont();
    ViewShutdown();
    CloseWindow();
}

Game::Game(const LaunchOptions& launchOptions)
    : options(launchOptions), window(config, options), world(config.worldHalfSize * kUlaanbaatarMapScale), player({ 0.0f, 0.0f, 0.0f }),
      camera(config)
{
    if (options.savePath)
    {
        SetSavePath(options.savePath); // туршилт: тусдаа хадгалалтын файл
    }
    if (options.relay)
    {
        // Туршилт: өөр relay сервер ("хост:порт"), эсвэл "off" — зөвхөн дотоод сүлжээ
        const std::string relay = options.relay;
        const size_t colon = relay.rfind(':');
        if (relay == "off") net.SetRelayServer("", 0);
        else if (colon == std::string::npos) net.SetRelayServer(relay, Net::kRelayTcpPort);
        else net.SetRelayServer(relay.substr(0, colon), (uint16_t)std::atoi(relay.c_str() + colon + 1));
    }
    SetCharacterBindPose(options.bindPose);
    Splash::Step(0.7f, "Дүрүүдийг ачаалж байна...");
    LoadCharacterModels(environment.CharacterShader()); // байхгүй бол дүрүүд кодоор үүсгэсэн дүрсээрээ зурагдана
    Splash::Step(0.84f, "Морьдыг ачаалж байна...");
    LoadHorseModel(environment.CharacterShader());      // байхгүй бол морь кодоор үүсгэсэн дүрсээрээ
    LoadVehicleModels(environment.VehicleShader());     // байхгүй бол унаанууд кодоор угсарсан хэлбэрээрээ
    LoadPropModels(environment.VehicleShader());        // assets/buu: автомат буу, үсрэх сум (байхгүй бол кодоор)
    Splash::Step(0.86f, "Дуу ачаалж байна...");
    Audio::Init();
    Splash::Step(0.94f, "Газрын зургийг бэлтгэж байна...");
    LoadProgress(progress, (int)Provinces().size());
    Audio::SetVolume(progress.sound ? kSoundLevels[std::clamp(progress.soundLevel, 0, 3)] : 0.0f);
    // Компьютерт (утас биш) видео картын хүчээр график сонгоно: тоглогч цэснээс сонгоогүй үед
    if (options.graphics < 0 && progress.graphicsAuto && !InTouchMode()) progress.graphics = RecommendedGraphics();
    environment.SetQuality(options.graphics >= 0 ? options.graphics : progress.graphics);
    if (progress.netTag < 1000 || progress.netTag > 9999)
    {
        progress.netTag = GetRandomValue(1000, 9999);
    }
    config.touchAimAssist = progress.aimAssist;
    mapScreen.SelectNextTarget(progress);
#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
    // Компьютер: хадгалсан сонголтоор бүтэн дэлгэц (туршилтын горимд цонхоор).
    if (progress.fullscreen && !options.screenshotPath && !options.touch) SetFullscreen(true);
    if (!options.screenshotPath) ApplyFrameLimit(); // (туршилт хязгааргүй)
#endif
    mapScreen.Preload(); // дэвсгэр зураг, аймгуудын дүрс: эхний кадрыг гацаахгүй
    // Данс (нэвтрэх, аймгийн эрх, төлбөр): туршилтын горимд идэвхгүй (--api заасан бол идэвхтэй)
    Account::Init(!options.screenshotPath || options.apiBase, options.apiBase ? options.apiBase : "");
    Splash::Finish();
}

Game::~Game()
{
    for (RenderTexture2D& portrait : portraits)
    {
        if (portrait.id) UnloadRenderTexture(portrait);
    }
    UnloadCharacterModels();
    UnloadHorseModel();
    UnloadVehicleModels();
    UnloadPropModels();
    Save();
}

void Game::Save() const
{
    // Туршилтын горим тоглогчийн хадгалалтыг өөрчлөхгүй (тусдаа файл заасан бол түүнд хадгална).
    if (!options.screenshotPath || options.savePath)
    {
        SaveProgress(progress);
    }
}

void Game::Run()
{
    // --- Хөгжүүлэлтийн горим: орчныг автоматаар шалгах ---
    const bool benchmark = options.screenshotPath != nullptr;
    Profile::Enable(benchmark); // туршилт: frame-ийн хэсгүүдийн хугацааг хэмжиж төгсгөлд нь хэвлэнэ
    const int warmupFrames = 30;
    int endFrame = warmupFrames + std::max(1, options.frames);
    int frame = 0;
    double measureStart = 0.0;
    if (benchmark && options.campaignMap)
    {
        // Аяны газрын зургийн дэлгэцийг шалгах: сонгосон аймгийг тодруулна.
        mapScreen.Select(std::clamp(options.province - 1, 0, (int)Provinces().size() - 1));
        if (options.characterSelect)
        {
            pendingProvince = mapScreen.Selected();
            characterChoice = progress.leaderSkin;
            characterSelectOpen = true;
        }
    }
    else if (benchmark)
    {
        StartMission(std::clamp(options.province - 1, 0, (int)Provinces().size() - 1));
        EnableCursor(); // туршилтын цонх хэрэглэгчийн хулганыг түгжихгүй
        if (options.member >= 1 && options.member <= kTeamSize)
        {
            const bool switched = SwitchMember(options.member - 1, false) || options.member - 1 == activeMember;
            TraceLog(LOG_INFO, "TEAM: switch to member %d %s, weapon %s", options.member, switched ? "ok" : "FAILED",
                     arsenal.Spec().name);
        }
        if (options.hasPose)
        {
            player.Teleport({ options.x, world.GroundHeight(options.x, options.z), options.z });
            camera.Snap(player.Position());
            world.Stream(player.Position(), true);
        }
        if (options.thirdPerson) progress.firstPerson = false;

        camera.SetView(options.yawDegrees * DEG2RAD, options.pitchDegrees * DEG2RAD);
        if (options.cameraDistance > 0.0f) camera.SetDistance(options.cameraDistance);
        if (options.ride >= 1 && options.ride <= garage.Count())
        {
            garage.Mount(options.ride - 1); // туршилт: унаад шууд урагш явна
            if (options.hasPose)
            {
                garage.Place(options.ride - 1, { options.x, world.GroundHeight(options.x, options.z), options.z },
                             (options.rideHeading > -999.0f ? options.rideHeading : options.yawDegrees) * DEG2RAD);
                if (options.crashTest >= 0 && options.crashTest < garage.Count())
                {
                    const float h = (options.rideHeading > -999.0f ? options.rideHeading : options.yawDegrees) * DEG2RAD;
                    const float tx = options.x + std::sin(h) * 25.0f, tz = options.z + std::cos(h) * 25.0f;
                    garage.Place(options.crashTest, { tx, world.GroundHeight(tx, tz), tz }, h + PI * 0.5f);
                }
            }
        }
        if (options.fullMapZoom > 0.0f)
        {
            fullMap.Open(world, player);
            fullMap.SetZoom(options.fullMapZoom);
        }
        journalOpen = options.journal;
        if (options.shop) OpenShop();
        if (options.pause) OpenPause();
        if (options.airstrike)
        {
            airstrike.Start(player.Position(), camera.Yaw()); // туршилт: бөмбөгдөлтийн кино
            state = GameState::Airstrike;
        }

        if (options.soldiers > 0)
        {
            ResetSquad(options.soldiers);
        }

        InSetTestMove({ options.moveRight, options.moveForward });
        if (options.demo)
        {
            player.SetPoseDemo(options.demoMove, options.demoPhase, options.demoAim, options.demoAction);
            player.SetPoseDemoExtra(options.demoDirection, options.demoReload);
        }
        if (options.stance == 1) player.SetStance(Stance::Crouch);
        if (options.stance == 2) player.SetStance(Stance::Prone);
        if (options.hold >= 1 && options.hold <= arsenal.SlotCount())
        {
            arsenal.Select(arsenal.SlotWeapon(options.hold - 1));
        }

        if (options.weapon >= 1 && options.weapon <= (int)WeaponType::Count)
        {
            arsenal.Select((WeaponType)(options.weapon - 1));

            // Хутганы дүрмийг төрөл бүр дээр шалгана: биед 5, толгойд 2 цохилт.
            for (int type = 0; type < 3; ++type)
            {
                int counts[2] = { 0, 0 };
                for (int head = 0; head < 2; ++head)
                {
                    horde.Spawn((ZombieType)type, { 120.0f, 0.0f, 120.0f }, params);
                    const int last = horde.Count() - 1; // сүүлд нэмсэн
                    while (horde.At(last).IsAlive() && counts[head] < 50)
                    {
                        horde.DamageFraction(last, head ? kKnifeHeadFraction : kKnifeBodyFraction);
                        ++counts[head];
                    }
                }
                TraceLog(LOG_INFO, "SELFTEST: knife on zombie type %d: body %d stabs, head %d stabs", type, counts[0],
                         counts[1]);
            }
            int coins = 0, kills = 0;
            horde.TakeRewards(coins, kills); // туршилтын зомбины шагналыг тооцохгүй
        }

        // Туршилтын зомбинууд: тоглогчийн урд талд хагас тойргоор, гурван төрлөөр.
        // Эхний хоёрыг шууд устгаж үхэх хөдөлгөөнийг, дараагийн хоёрыг шархдуулж цочрохыг шалгана.
        const float yaw = options.yawDegrees * DEG2RAD;
        for (int i = 0; i < options.zombies; ++i)
        {
            const float angle = yaw + ((float)(i % 9) - 4.0f) * 0.2f;
            const float nearest = options.weapon == 5 ? 13.0f : 4.5f; // гранатын туршилтад холоос ирнэ
            const float distance = nearest + (float)(i / 9) * 2.2f + (float)(i % 2) * 0.8f;
            const Vector3 origin = player.Position();
            Vector3 position = { origin.x + std::sin(angle) * distance, 0.0f, origin.z + std::cos(angle) * distance };
            position.y = world.GroundHeight(position.x, position.z);
            horde.Spawn((ZombieType)(i % 3), position, params);
            if (options.weapon > 0) continue; // зэвсгийн туршилтад бүгд амьд үлдэнэ
            const int last = horde.Count() - 1;
            if (i < 2) horde.Damage(last, 1.0e6f);
            else if (i < 4) horde.Damage(last, 1.0f);
        }
        for (int i = 0; i < 6 && options.zombies > 0 && options.weapon == 0; ++i)
        {
            particles.Blood({ player.Position().x, 1.4f, player.Position().z + 4.5f }, { 0.0f, 0.3f, -1.0f }, 6);
        }
    }

    while (!AppShouldClose())
    {
        if (benchmark)
        {
            if (frame == warmupFrames) measureStart = GetTime();
            // Автомат тоглолт: үе дуусмагц (эсвэл тоглогч үхвэл) удаан хүлээлгүй дуусгана.
            if (options.autoPlay && state != GameState::Playing && endFrame > frame + 20)
            {
                endFrame = frame + 20;
            }
            if (frame == endFrame)
            {
                const int measuredFrames = std::max(1, endFrame - warmupFrames);
                const double seconds = GetTime() - measureStart;
                TraceLog(LOG_INFO,
                         "BENCH: %.1f fps average (%.2f ms/frame), %d terrain chunks drawn, %d colliders, %d zombies, "
                         "%d kills, hp %d, player at (%.1f, %.1f) %s boundary, %d terrain meshes loaded",
                         (double)measuredFrames / seconds, 1000.0 * seconds / (double)measuredFrames,
                         world.DrawnChunks(), world.ObstacleCount(), horde.AliveCount(), missionKills,
                         (int)player.Health(), player.Position().x, player.Position().z,
                         world.InsideBoundary(player.Position().x, player.Position().z) ? "inside" : "OUTSIDE",
                         world.LoadedTerrainMeshes());
                TraceLog(LOG_INFO, "MISSION: stage %d, %d of %d objectives done, state %s, coins %d, squad %d",
                         provinceIndex + 1, mission.CompletedCount(), mission.ObjectiveCount(),
                         state == GameState::Won ? "WON" : state == GameState::Lost ? "LOST" : "PLAYING", progress.coins,
                         squad.Total());
                if (squad.Total() > 0)
                {
                    // Цэргүүд тоглогчтой нэг "өрөөнд" (эсвэл хамт гадаа) байгаа эсэх
                    const int playerRoom = world.InteriorAt(player.Position().x, player.Position().z);
                    int together = 0;
                    for (const Soldier& soldier : squad.All())
                    {
                        together += world.InteriorAt(soldier.mover.position.x, soldier.mover.position.z) == playerRoom;
                    }
                    TraceLog(LOG_INFO, "SQUAD: player room %d, %d of %d soldiers in the same room", playerRoom, together,
                             squad.Total());
                }
                if (garage.IsDriving())
                {
                    const Vehicle& vehicle = garage.At(garage.Driven());
                    TraceLog(LOG_INFO, "RIDE: %s speed %.1f m/s (%.0f km/h), limit here %.1f m/s, surface %d",
                             SpecFor(vehicle.type).name, vehicle.speed, vehicle.speed * 3.6f, garage.DrivenMaxSpeed(),
                             (int)garage.DrivenSurface());
                }
                TraceLog(LOG_INFO, "CLIMB: player height %.2f above ground %.2f, traversing %d, interior %d", player.Position().y,
                         world.GroundHeight(player.Position().x, player.Position().z), (int)player.IsTraversing(),
                         world.InteriorAt(player.Position().x, player.Position().z));
                if (teamVehicle >= 0)
                {
                    const Vehicle& escort = garage.At(teamVehicle);
                    int riders = 0;
                    for (const Soldier& soldier : squad.All()) riders += soldier.riding == teamVehicle;
                    TraceLog(LOG_INFO, "ESCORT: team in %s with %d riders, speed %.1f m/s, %.1f m behind the player, %s",
                             SpecFor(escort.type).name, riders, escort.speed,
                             HorizontalDistance(escort.position, player.Position()),
                             garage.Escort() == teamVehicle ? "driving" : "boarding");
                }
                Image image = ViewCapture();
                ExportImage(image, options.screenshotPath);
                UnloadImage(image);
                break;
            }
            ++frame;
        }

        // Цонх гацах үед (чирэх г.м.) том dt-ээс болж хана нэвтлэхээс сэргийлнэ.
        const float dt = std::min(GetFrameTime(), 1.0f / 30.0f) * (benchmark ? options.timeScale : 1.0f);

        // Мэдрэгчтэй дэлгэцийн товчлуурууд ба гар, хулганы оролтыг нэгтгэнэ.
        touchState = MakeTouchState();
        InBeginFrame(touchState);
        // Данс: өөр төхөөрөмж дээр нэвтэрсэн бол (нэг зэрэг нэг л төхөөрөмж) даалгаврыг зогсоож газрын зураг руу
        Account::Tick();
        {
            std::string note;
            if (Account::TakeNotice(note)) SetNotice(note, 5.0f); // (баталгаажаагүй тул олгоогүй шагнал г.м.)
        }
        if (Account::TakeKicked() && state != GameState::Map)
        {
            if (paused) ClosePause();
            ReturnToMap();
        }
        if (!benchmark || options.coopTest) UpdateNetwork(dt);

        // ESC (утсан дээр "буцах" товч): нээлттэй цонхыг (даалгаврын журнал, газрын зураг) хаана; цонхгүй бол гарна.
        if (InKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACK))
        {
            if (state == GameState::Map && !paused && Account::Back())
            {
                // данс, төлбөрийн цонх: өмнөх цонх руу буцна
            }
            else if (state == GameState::Map && mapScreen.DialogOpen())
            {
                mapScreen.CloseDialog(); // сайжруулалт авах уу? цонх
            }
            else if (codeEntryOpen)
            {
                codeEntryOpen = false; // өрөөний код оруулах цонх
            }
            else if (characterSelectOpen)
            {
                characterSelectOpen = false; // дүр сонгох цонх: газрын зураг руу буцна
            }
            else if (paused)
            {
                ClosePause();
            }
            else if (journalOpen)
            {
                CloseJournal();
            }
            else if (shopOpen)
            {
                CloseShop();
            }
            else if (fullMap.IsOpen())
            {
                fullMap.Close();
                LockCursor();
            }
            else if (benchmark)
            {
                TraceLog(LOG_INFO, "BENCH: stopped early by the ESC key at frame %d", frame);
                break;
            }
            else
            {
                OpenPause(); // тоглоомын цэс: шууд гарахгүй
            }
        }
#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
        if (IsKeyPressed(KEY_F11)) SetFullscreen(!progress.fullscreen);
#endif
        // Хөтөч: өөр tab руу шилжих, ESC зэргээр хулганы түгжээ алдагдвал тоглоомын цэсийг нээнэ
        // (эс бөгөөс хулганы сум гарч ирээд харах чиглэл удирдагдахгүй). "Үргэлжлүүлэх" нь дахин түгжинэ.
        if (WebPointerLockLost() && state == GameState::Playing && !paused && !InTouchMode()) OpenPause();
        if (paused)
        {
            UpdatePauseMenu();
            if (quitRequested) break;
            // Ганцаараа: тоглоом түр зогсоно. Хамтарсан тоглолтод нөгөө тоглогч үргэлжлүүлж байгаа тул ертөнц
            // хөдөлсөөр (snapshot, зомби), зөвхөн энэ тоглогчийн оролт хаагдана.
            if (!(net.Connected()))
            {
                Draw();
                continue;
            }
            InSetMuted(true);
        }

        switch (state)
        {
        case GameState::Map:
            UpdateMap();
            break;
        case GameState::Playing:
            // Q: даалгаврын журнал. M: бүтэн дэлгэцийн газрын зураг. Аль нэг нь нээлттэй үед тоглоом түр зогсож,
            // хулганы заагч гарч ирнэ; камер эргэхгүй, буудлага явагдахгүй.
            if (shopOpen)
            {
                UpdateShopMenu(); // дэлгүүрийн цэс: тоглоом түр зогсоно
                // Хамтарсан тоглолт: нөгөө тоглогч үргэлжлүүлж байгаа тул ертөнц хөдөлсөөр (энэ тоглогчийн оролтгүй)
                if (shopOpen && net.Connected())
                {
                    InSetMuted(true);
                    UpdateMission(dt);
                }
                break;
            }
            if (InKeyPressed(KEY_Q) && !fullMap.IsOpen())
            {
                if (journalOpen)
                {
                    CloseJournal();
                }
                else
                {
                    journalOpen = true;
                    EnableCursor();
                }
            }
            else if (InKeyPressed(KEY_M) && !journalOpen)
            {
                if (fullMap.IsOpen())
                {
                    fullMap.Close();
                    LockCursor();
                }
                else
                {
                    fullMap.Open(world, player); tutorialMapSeen = true;
                    EnableCursor();
                }
            }

            if (journalOpen)
            {
                // түр зогссон (хамтарсан тоглолтод ертөнц хөдөлсөөр, оролтгүй)
                if (net.Connected())
                {
                    InSetMuted(true);
                    UpdateMission(dt);
                }
            }
            else if (fullMap.IsOpen())
            {
                fullMap.Update(world, player);
                Vector2 point;
                bool clear = false;
                if (fullMap.TakeWaypoint(point, clear))
                {
                    if (clear) navigator.Clear();
                    else navigator.SetTarget(world, player.Position(), point); // хөтлөх замыг тооцно
                }
                if (net.Connected())
                {
                    InSetMuted(true);
                    UpdateMission(dt);
                }
            }
            else
            {
                UpdateMission(dt);
            }
            break;
        case GameState::Airstrike:
            UpdateAirstrike(dt);
            break;
        case GameState::Won:
        case GameState::Lost:
            tracers.Update(dt);
            if (InKeyPressed(KEY_ENTER)) ReturnToMap();
            break;
        }
        InSetMuted(false);

        {
            PROFILE_SCOPE("draw (total)");
            Draw();
        }
        Profile::EndFrame();
    }
    Profile::Report();
    if (benchmark && AppShouldClose())
    {
        TraceLog(LOG_INFO, "BENCH: stopped early because the window was closed at frame %d", frame);
    }
}

void Game::HandleSettingsInput()
{
    if (InKeyPressed(KEY_LEFT_BRACKET)) config.mouseSensitivity -= 0.0005f;
    if (InKeyPressed(KEY_RIGHT_BRACKET)) config.mouseSensitivity += 0.0005f;
    config.mouseSensitivity = Clamp(config.mouseSensitivity, 0.0005f, 0.01f);

    if (InKeyPressed(KEY_MINUS)) config.walkSpeed -= 0.5f;
    if (InKeyPressed(KEY_EQUAL)) config.walkSpeed += 0.5f;
    config.walkSpeed = Clamp(config.walkSpeed, 2.0f, 15.0f);

    if (InKeyPressed(KEY_I)) config.invertY = !config.invertY;
    // F1, F2, F3 нь багийн гишүүд рүү шилжих товч болсон тул тусламж, шалгах горим өөр товчинд.
    // Шалгах горим (мөргөлдөх хүрээ): Ctrl+F4. Дан F4 нь F1–F3-ын хажууд тул санамсаргүй дарагддаг байсан.
    const bool control = InKeyDown(KEY_LEFT_CONTROL) || InKeyDown(KEY_RIGHT_CONTROL);
    if (control && InKeyPressed(KEY_F4)) showDebug = !showDebug;
    if (InKeyPressed(KEY_F10)) showHelp = !showHelp; // (H нь "энд бай" тушаал)
}

// ---------------------------------------------------------------- Газрын зураг

void Game::UpdateMap()
{
    // Туршилт (--campaign --api ...): нэвтэрсэн бол сонгосон аймгийг эхлүүлэх хүсэлт (эрх нээх, төлөх цонх)
    static bool accountTestSent = false;
    static double accountTestEnter = 0.0;
    if (options.screenshotPath && options.apiBase && options.journal && accountTestSent && accountTestEnter > 0.0 &&
        GetTime() > accountTestEnter)
    {
        accountTestEnter = 0.0; // --journal: аймаг нээх цонхны үндсэн товчийг нэг удаа дарна (төлөх цонх руу)
        Account::TestEnter();
    }
    static bool cashoutOpened = false;
    if (options.cashout && !cashoutOpened && Account::SignedIn())
    {
        cashoutOpened = true; // туршилт: Gold татах цонх
        Account::OpenCashout();
    }
    if (Account::Update()) return; // нэвтрэх, аймаг нээх, төлөх цонх нээлттэй
    if (options.screenshotPath && options.apiBase && !accountTestSent && Account::SignedIn() && !options.characterSelect &&
        !options.stayOnMap)
    {
        accountTestSent = true;
        accountTestEnter = GetTime() + 1.0;
        Account::RequestStart(mapScreen.Selected(), Provinces()[(size_t)mapScreen.Selected()].name);
        return;
    }
    if (characterSelectOpen)
    {
        UpdateCharacterSelect();
        return;
    }
    if (codeEntryOpen)
    {
        UpdateCodeEntry(); // газрын зургийн товч (1-5, Enter) кодын цифртэй давхцахгүй
        return;
    }
    if (!mapScreen.DialogOpen()) UpdateCoopMap(); // сайжруулалтын цонх нээлттэй үед доорх самбар товшигдохгүй
    if (state != GameState::Map) return; // хамтарсан тоглолт эхэлсэн
    bool changed = ShopSync(progress); // серверээс авсан сайжруулалт
    const MapScreen::Result result = mapScreen.Update(progress, changed);
    if (changed)
    {
        Save();
    }
    // Эхлээд 1-р дайчны дүрийг сонгоно (сүүлд сонгосон нь урьдчилан тэмдэглэгдсэн). Дараа нь данстай үед аймгийн
    // эрхийг сервер шалгаж (үнэгүй эрх, төлбөр), нээлттэй өрөөний Gold-ыг авна: дүр сонгохоос буцвал Gold хасагдахгүй.
    if (result == MapScreen::Result::StartMission)
    {
        skinPicked = false;
        OpenCharacterSelect(mapScreen.Selected(), false);
    }
    const int approved = Account::TakeApproved();
    if (approved >= 0)
    {
        if (skinPicked)
        {
            if (progress.leaderSkin != pendingSkin)
            {
                progress.leaderSkin = pendingSkin;
                Save();
            }
            StartMission(approved);
        }
        else OpenCharacterSelect(approved, true); // дүр сонгоогүй байхад зөвшөөрөгдсөн (туршилт)
        skinPicked = false;
    }
}

void Game::OpenCharacterSelect(int province, bool approved)
{
    pendingProvince = province;
    pendingApproved = approved;
    mapScreen.Select(province);
    characterChoice = std::clamp(progress.leaderSkin, 0, kLeaderSkinCount - 1);
    characterSelectOpen = true;
}

Rectangle Game::CharacterCardRect(int skin) const
{
    // Бүх дүр нэг мөрөнд багтана (өндөр нь тогтмол: 3D дүр картандаа таарна)
    const float gap = 24.0f, height = 400.0f;
    const float width = std::min(300.0f, ((float)ViewWidth() - 64.0f - gap * (float)(kLeaderSkinCount - 1)) / (float)kLeaderSkinCount);
    const float left = ((float)ViewWidth() - (width * (float)kLeaderSkinCount + gap * (float)(kLeaderSkinCount - 1))) * 0.5f;
    return { left + (float)skin * (width + gap), 120.0f, width, height };
}

Rectangle Game::CharacterStartRect() const
{
    return { (float)ViewWidth() * 0.5f - 130.0f, 560.0f, 260.0f, 58.0f };
}

Rectangle Game::CharacterBackRect() const
{
    return { 24.0f, 24.0f, 150.0f, 50.0f };
}

void Game::UpdateCharacterSelect()
{
    if (InKeyPressed(KEY_LEFT) || InKeyPressed(KEY_A)) characterChoice = (characterChoice + kLeaderSkinCount - 1) % kLeaderSkinCount;
    if (InKeyPressed(KEY_RIGHT) || InKeyPressed(KEY_D)) characterChoice = (characterChoice + 1) % kLeaderSkinCount;
    for (int skin = 0; skin < kLeaderSkinCount && skin < 9; ++skin)
    {
        if (InKeyPressed(KEY_ONE + skin)) characterChoice = skin; // 1, 2, 3 товч
    }
    bool start = InKeyPressed(KEY_ENTER) || InKeyPressed(KEY_KP_ENTER) || InKeyPressed(KEY_SPACE);
    if (InMousePressed(MOUSE_BUTTON_LEFT))
    {
        const Vector2 pointer = InPointer();
        for (int skin = 0; skin < kLeaderSkinCount; ++skin)
        {
            if (CheckCollisionPointRec(pointer, CharacterCardRect(skin))) characterChoice = skin;
        }
        if (CheckCollisionPointRec(pointer, CharacterStartRect())) start = true;
        if (CheckCollisionPointRec(pointer, CharacterBackRect()))
        {
            characterSelectOpen = false; // газрын зураг руу буцна
            return;
        }
    }
    if (!start) return;
    // Онцгой дүр: Gold-ийг сервер аймаг эхлэхэд авна; хүрэлцэхгүй бол Gold авах цонх
    const bool premium = Account::Enabled() && !pendingApproved && Account::PremiumSkin(characterChoice);
    if (premium && Account::GoldAvailable() < Account::SkinPrice(characterChoice))
    {
        Account::OpenGoldShop();
        return;
    }
    characterSelectOpen = false;
    if (pendingApproved || !Account::Enabled())
    {
        if (progress.leaderSkin != characterChoice)
        {
            progress.leaderSkin = characterChoice;
            Save();
        }
        StartMission(pendingProvince);
        return;
    }
    // Сервер зөвшөөрмөгц (TakeApproved) энэ дүрээр эхэлнэ (онцгой дүрийг төлөгдсөний дараа л хадгална)
    skinPicked = true;
    pendingSkin = characterChoice;
    Account::RequestStart(pendingProvince, Provinces()[(size_t)pendingProvince].name, characterChoice);
}

void Game::DrawCharacterSelect() const
{
    const int width = ViewWidth(), height = ViewHeight();
    // Бүтэн дэлгэцийн дэвсгэр (доорх газрын зургийг цэвэрлэж, 3D-д гүний buffer шинээр)
    rlDrawRenderBatchActive();
    ClearBackground({ 8, 10, 14, 255 });
    DrawRectangleGradientV(0, 0, width, height, { 26, 32, 44, 255 }, { 8, 10, 14, 255 });
    const char* title = "ДҮРЭЭ СОНГО";
    Ui::TextBold(title, (width - Ui::MeasureBold(title, 30)) / 2, 50, 30, Ui::kAccent);
    const char* subtitle = "1-р дайчин энэ дүрээр тоглоно";
    Ui::Text(subtitle, (width - Ui::Measure(subtitle, 15)) / 2, 88, 15, Ui::kTextDim);

    for (int skin = 0; skin < kLeaderSkinCount; ++skin)
    {
        const Rectangle card = CharacterCardRect(skin);
        const bool selected = skin == characterChoice;
        Ui::Panel(card, selected ? Color{ 40, 32, 14, 230 } : Color{ 16, 20, 28, 220 });
        Ui::Outline(card, selected ? Ui::kAccent : Color{ 255, 255, 255, 40 }, selected ? 3.0f : 1.0f, 12.0f);
    }

    // 3D: хоёр дүр картуудынхаа дотор аажим эргэнэ (жинхэнэ загвар, буу барьсан)
    Camera3D camera = {};
    camera.position = { 0.0f, 0.85f, 8.0f }; // дүр картын дотор: хөл нь нэрийн дээр, толгой нь дээд ирмэгийн доор
    camera.target = { 0.0f, 0.65f, 0.0f };
    camera.up = { 0.0f, 1.0f, 0.0f };
    camera.fovy = 30.0f;
    camera.projection = CAMERA_PERSPECTIVE;
    environment.SetTimeOfDay(0.3f); // өдрийн гэрэл
    environment.ClearShadow();
    BeginMode3D(camera);
    rlEnableDepthTest();
    environment.Begin(camera);
    for (int skin = 0; skin < kLeaderSkinCount; ++skin)
    {
        // Картын төвийн дэлгэцийн x-ийг ертөнцийн x болгоно (камерын фокусын хавтгай дээр)
        const Rectangle card = CharacterCardRect(skin);
        const float screenX = (card.x + card.width * 0.5f) / (float)width * 2.0f - 1.0f;
        const float halfWidth = std::tan(camera.fovy * 0.5f * DEG2RAD) * camera.position.z * (float)width / (float)height;
        const float worldX = screenX * halfWidth;
        const HumanoidLook look = TeamMemberLook(0, skin);
        HumanoidPose pose = MakeSoldierPose(0.0f, 0.0f, true, (float)GetTime() + (float)skin, 0.0f, look.held);
        const float facing = (float)GetTime() * 0.5f + (skin == characterChoice ? 0.0f : 0.6f);
        DrawHumanoid({ worldX, 0.0f, 0.0f }, facing, 1.0f, pose, look, true);
    }
    environment.End();
    EndMode3D();

    for (int skin = 0; skin < kLeaderSkinCount; ++skin)
    {
        const Rectangle card = CharacterCardRect(skin);
        const char* name = LeaderSkinName(skin);
        const bool selected = skin == characterChoice;
        Ui::TextBold(name, (int)(card.x + (card.width - (float)Ui::MeasureBold(name, 24)) / 2.0f), (int)(card.y + card.height) - 44, 24,
                     selected ? Ui::kAccent : Ui::kText);
        if (Account::Enabled() && Account::PremiumSkin(skin))
        {
            // Онцгой дүр: аймаг эхлүүлэх бүрийн үнэ (картын дээд буланд)
            const char* price = TextFormat("%d Gold", Account::SkinPrice(skin));
            const float chipWidth = (float)Ui::MeasureBold(price, 16) + 20.0f;
            const Rectangle chip = { card.x + card.width - chipWidth - 10.0f, card.y + 10.0f, chipWidth, 28.0f };
            DrawRectangleRounded(chip, 0.5f, 8, { 40, 30, 8, 230 });
            DrawRectangleRoundedLinesEx(chip, 0.5f, 8, 1.5f, Ui::kAccent);
            Ui::TextBold(price, (int)chip.x + 10, (int)chip.y + 5, 16, Ui::kAccent);
        }
    }
    {
        // Газрын зураг руу буцах (утсан дээр ESC байхгүй)
        const Rectangle back = CharacterBackRect();
        const bool hover = !InTouchMode() && CheckCollisionPointRec(InPointer(), back);
        DrawRectangleRounded(back, 0.4f, 8, { 255, 255, 255, (unsigned char)(hover ? 46 : 24) });
        DrawRectangleRoundedLinesEx(back, 0.4f, 8, 1.0f, { 255, 255, 255, 80 });
        const float cy = back.y + back.height * 0.5f;
        DrawLineEx({ back.x + 20.0f, cy }, { back.x + 36.0f, cy }, 3.0f, Ui::kText);
        DrawLineEx({ back.x + 20.0f, cy }, { back.x + 28.0f, cy - 8.0f }, 3.0f, Ui::kText);
        DrawLineEx({ back.x + 20.0f, cy }, { back.x + 28.0f, cy + 8.0f }, 3.0f, Ui::kText);
        Ui::TextBold("БУЦАХ", (int)back.x + 48, (int)cy - 11, 19, Ui::kText);
    }
    const Rectangle button = CharacterStartRect();
    DrawRectangleRounded(button, 0.4f, 8, Ui::kAccent);
    const bool unaffordable = Account::Enabled() && !pendingApproved && Account::PremiumSkin(characterChoice) &&
                              Account::GoldAvailable() < Account::SkinPrice(characterChoice);
    const char* label = unaffordable ? "GOLD АВАХ" : "ЭХЛЭХ";
    Ui::TextBold(label, (int)(button.x + (button.width - (float)Ui::MeasureBold(label, 24)) / 2.0f), (int)button.y + 16, 24,
                 { 20, 16, 10, 255 });
    if (!InTouchMode())
    {
        const char* hint = "← → сонгох     ENTER эхлэх     ESC буцах";
        Ui::Text(hint, (width - Ui::Measure(hint, 14)) / 2, height - 40, 14, Ui::kTextDim);
    }
}

void Game::RenderPortraits() const
{
    if (portraitSkin == progress.leaderSkin && portraits[0].id) return;
    portraitSkin = progress.leaderSkin;
    Camera3D view = {};
    view.position = { 0.0f, 1.62f, 1.15f }; // толгой, мөр (дүр 1 м-ийн хуваарьтай, ~1.8 м өндөр)
    view.target = { 0.0f, 1.56f, 0.0f };
    view.up = { 0.0f, 1.0f, 0.0f };
    view.fovy = 30.0f;
    view.projection = CAMERA_PERSPECTIVE;
    // Өдрийн гэрэлтэй, сүүдэргүй зурна (дараа нь тоглолтын цагийг буцаана; сүүдэр энэ кадрт дахин зурагдана)
    environment.SetTimeOfDay(0.3f);
    environment.ClearShadow();
    for (int member = 0; member < 3; ++member)
    {
        if (!portraits[member].id) portraits[member] = LoadRenderTexture(112, 112);
        BeginTextureMode(portraits[member]);
        ClearBackground({ 0, 0, 0, 0 });
        BeginMode3D(view);
        rlEnableDepthTest();
        environment.Begin(view);
        const HumanoidLook look = TeamMemberLook(member, progress.leaderSkin);
        const HumanoidPose pose = MakeSoldierPose(0.0f, 0.0f, true, 0.0f, 0.0f, look.held);
        DrawHumanoid({ 0.0f, 0.0f, 0.0f }, 0.0f, 1.0f, pose, look, true);
        environment.End();
        EndMode3D();
        EndTextureMode();
    }
    environment.SetTimeOfDay(dayClock / kDayLength);
}

void Game::ReturnToMap()
{
    battleActive = hostGone = takeoverBattle = autopilot = autoFire = false;
    idleTimer = 0.0f;
    if (Account::CurrentRoomRole() != Account::RoomRole::None) Account::LeaveRoom();
    progress.coins = 0; // тоглолтын зоос тоглолт дуусахад алга болно
    if (coopGuest) LeaveCoop();
    else if (net.GetRole() == Net::Role::Host) { net.Stop(); remoteActive = false; joinRequestPending = false; }
    Save();
    horde.Clear();
    tracers.Clear();
    mapScreen.SelectNextTarget(progress);
    EnableCursor();
    state = GameState::Map;
}

// ---------------------------------------------------------------- Даалгавар

namespace
{
    // Газрын өнгө: говь шаргал элсэрхэг, хойд ойт аймаг ногоон (Structures-ийн бүс нутагтай ижил)
    float ProvinceDryness(int province)
    {
        switch (province)
        {
        case 4: case 6: case 9: case 11: case 16: case 17: case 18: return 0.9f; // говь
        case 5: case 7: case 8: case 12: case 19: return 0.0f;                    // ойт
        case 0: case 1: case 2: case 3: return 0.4f;                              // баруун
        default: return 0.25f;
        }
    }

    // Алсын уулсын өндөр: баруун (Алтай, Хангай) өндөр цастай, зүүн тал намхан
    float ProvinceMountains(int province)
    {
        switch (province)
        {
        case 0: case 1: case 2: case 3: case 4: return 1.0f;           // Алтай
        case 5: case 7: case 9: case 6: return 0.85f;                   // Хангай, Хөвсгөл
        case 8: case 10: case 12: case 13: case 14: case 15: case 19: return 0.6f;
        case 11: case 16: case 17: case 18: return 0.45f;              // говь
        default: return 0.35f;                                          // зүүн тал
        }
    }

    // Аймгийн бүс нутгаас хамаарсан цаг агаар: говьд шороон шуурга, хойд уулархаг нутагт цас олонтой.
    WeatherKind PickWeather(int province, int forced)
    {
        if (forced >= 0 && forced <= 4) return (WeatherKind)forced;
        const bool gobi = province == 4 || province == 6 || province == 11 || province == 16 || province == 17 || province == 18;
        const bool north = province == 0 || province == 1 || province == 3 || province == 5 || province == 7 || province == 12;
        const int weights[5] = { 50, 22, gobi ? 4 : 12, north ? 14 : 5, gobi ? 26 : 4 };
        int total = 0;
        for (int w : weights) total += w;
        int roll = GetRandomValue(0, total - 1);
        for (int i = 0; i < 5; ++i)
        {
            if (roll < weights[i]) return (WeatherKind)i;
            roll -= weights[i];
        }
        return WeatherKind::Clear;
    }
}

void Game::StartMission(int index, StageSave* stageOverride)
{
    const double startTime = GetTime();
    provinceIndex = index;
    params = MissionFor(index);
    ShopSync(progress);       // байнгын сайжруулалт: серверийнх
    progress.coins = 0;       // тоглолтын зоос шинээр
    ammoReserve = kStartAmmo;

    // Талбарын хязгаар: энэ аймгийн хилийн хэлбэр. Тоглогч хилээс хамгийн хол дотоод цэгт (0, 0) эхэлнэ.
    // Улаанбаатарын нутаг томсгосон (жинхэнэ хэмжээтэй нисэх буудал багтана).
    const float radius = (config.worldHalfSize - 8.0f) * (index == kUlaanbaatarProvince ? kUlaanbaatarMapScale : 1.0f);
    world.SetBoundary(MakeMissionShape(index, radius));

    // Энэ үеийн суурин, отрууд: аймаг бүрд байшингууд, зохион байгуулалт нь өөр.
    world.BuildSettlements(index);
    if (world.CityActive())
    {
        // Аймгийн жинхэнэ төв: заставыг төвлөсөн 5.2 x 5.2 км дөрвөлжин
        const float edge = kCityHalfExtent;
        Shape square;
        square.outline = { { -edge, -edge }, { edge, -edge }, { edge, edge }, { -edge, edge } };
        world.SetBoundary(square);
    }
    missionKills = 0;
    inTown = false;
    medKitKills = 0;
    lastReward = 0;
    helpedHost = false;

    player.Reset(config.playerBaseHealth + kHealthPerLevel * (float)progress.healthLevel);
    if (world.CityActive())
    {
        if (index == kUlaanbaatarProvince) player.Teleport({ 0.0f, 0.0f, -14.0f }); // Сүхбаатарын хөшөөний урд
    }

    // Баг: 1, 2-р гишүүн дайчин, 3-р гишүүн мэргэн буудагч. Тоглогч 1-р гишүүнээр эхэлнэ.
    for (int member = 0; member < kTeamSize; ++member)
    {
        // 2-р дайчин (F2) үрлэн бууны оронд пуужин харвагчтай.
        arsenals[member].Configure(progress.weapon, IsSniperMember(member) ? Loadout::Sniper
                                                    : member == 1 ? Loadout::Heavy : Loadout::Fighter);
        arsenals[member].SetReserve(&ammoReserve);
    }
    arsenal.SetReserve(&ammoReserve);
    activeMember = 0;
    dayClock = 0.08f * kDayLength; // аймаг бүр өглөөгөөр эхэлнэ
    wasNight = false;
    environment.SetWeather(PickWeather(index, options.weather));
    environment.SetDryness(ProvinceDryness(index));
    environment.SetMountainScale(ProvinceMountains(index));
    TraceLog(LOG_INFO, "MISSION: start setup %.2f s (graphics %d)", GetTime() - startTime, environment.Quality());
    if (WebHeapMegabytes() > 0) TraceLog(LOG_INFO, "MEMORY: heap %d MB after mission setup", WebHeapMegabytes());
    perfTime = -3.0f; // эхний секундүүдэд газар ачаалагддаг тул хэмжихгүй
    perfFrames = 0;
    environment.SetTimeOfDay(dayClock / kDayLength);
    aimToggled = false;
    progress.firstPerson = true; // даалгавар бүр 1-р этгээдээр (зөвхөн зэвсэг харагдана) эхэлнэ; V-ээр 3-р этгээд рүү шилжинэ
    for (bool& mark : holdMark) mark = false;
    arsenal = arsenals[0];
    player.SetLook(TeamMemberLook(0, progress.leaderSkin));
    grenades.Clear();
    horde.Clear();
    tracers.Clear();
    particles.Clear();
    fullMap.Close();
    barrels.Spawn(world, index); // аймаг бүрт өөр байрлалд
    medKits.Clear();
    events.Clear();
    eventTimer = 70.0f;
    rescueSoldiers.clear();
    world.Stream(player.Position(), true); // эхлэх цэгийн орчмын газрыг бүгдийг нь нэг дор бэлтгэнэ
    world.CloseAllDoors();
    SpawnVehicles();
    ResetSquad(progress.soldiers);
    camera.Snap(player.Position());
    WarmUpRendering();

    // Даалгаврууд: хадгалагдсан явцаас үргэлжилнэ. Аль хэдийн чөлөөлсөн аймгийг дахин тоглоход шинээр эхэлнэ.
    StageSave& stageSave = stageOverride ? *stageOverride : progress.stages[(size_t)index];
    if (progress.liberated[index] && !stageOverride)
    {
        stageSave = StageSave{};
    }
    if (options.quotaSmall >= 0)
    {
        mission.SetQuota(options.quotaSmall, std::max(0, options.quotaBig)); // туршилт
    }
    mission.Start(index, params, world, stageSave);
    navigator.Clear();
    SetMapNavigator(&navigator);
    if (options.hasWaypoint) navigator.SetTarget(world, player.Position(), { options.waypointX, options.waypointZ });
    journalOpen = false;
    shopOpen = false;

    LockCursor(); // хулганыг цонхонд түгжиж, камерт ашиглана
    state = GameState::Playing;
}

void Game::ResetSquad(int recruits)
{
    squad.Reset(recruits, player.Position(), world);
    teamVehicle = -1;
    garage.SetEscort(-1);

    const Vector3 origin = player.Position();
    for (int member = 0; member < kTeamSize; ++member)
    {
        if (member == activeMember)
        {
            continue;
        }
        // Удирдаж буй гишүүний ард, хоёр талд
        Vector3 position = { origin.x + (member == kSniperMember ? 5.5f : -5.5f), origin.y, origin.z - 4.0f };
        world.ResolveHorizontal(position, config.playerRadius, config.playerHeight, 0.0f);
        position.y = world.GroundHeight(position.x, position.z);
        squad.AddMember(member, position, player.Facing(), player.MaxHealth(), player.MaxHealth());
    }
}

bool Game::SwitchMember(int target, bool leaveDead)
{
    if (player.IsDowned() && !leaveDead) return false; // унасан: босгохыг хүлээнэ
    if (target == activeMember || target < 0 || target >= kTeamSize)
    {
        return false;
    }
    const int index = squad.FindMember(target);
    if (index < 0 || !squad.All()[(size_t)index].alive || squad.All()[(size_t)index].downed)
    {
        return false; // тэр гишүүн үхсэн
    }
    // Хоёр хүний унаа: жолооч ба буудагч суудлаа солино (унаа зогсохгүй).
    int toGunner = -1, toDriver = -1;
    if (!leaveDead && garage.IsDriving() && index == carGunner)
    {
        toGunner = garage.Driven(); // тоглогч буудагч болж, гишүүн жолоодно
    }
    else if (!leaveDead && gunnerVehicle >= 0 && index == carDriver)
    {
        toDriver = gunnerVehicle;   // тоглогч жолоогоо буцааж авна
    }
    else if (gunnerVehicle >= 0)
    {
        if (!leaveDead)
        {
            return false; // унаанаас буусны дараа л өөр гишүүн рүү шилжинэ
        }
        LeaveGunnerSeat();
    }
    else if (garage.IsDriving())
    {
        if (!leaveDead)
        {
            return false; // унаанаас буусны дараа л шилжинэ
        }
        garage.Dismount();
        player.StopRiding(player.Position());
    }

    // Тоглогчийн бие ба AI гишүүний бие байр, амь, дугаараа солилцоно.
    Soldier& body = squad.All()[(size_t)index];
    const Vector3 oldPosition = player.Position();
    const float oldFacing = player.Facing();
    const float oldHealth = player.Health(), oldMaxHealth = player.MaxHealth();

    arsenals[activeMember] = arsenal;
    player.Assume(body.mover.position, body.mover.facing, body.health, body.maxHealth);

    body.mover.position = oldPosition;
    body.mover.facing = oldFacing;
    body.health = leaveDead ? 0.0f : oldHealth;
    body.maxHealth = oldMaxHealth;
    body.alive = !leaveDead;
    body.deathTimer = 0.0f;
    body.member = activeMember;
    body.routeFrom = body.routeTo = -2;
    body.stuckTimer = 0.0f;
    body.bestDistance = 0.0f;
    body.moving = false;
    body.riding = -1;
    body.boarding = false;
    body.seatStyle = 0;
    // "Энд бай" тэмдэгтэй гишүүнийг орхивол тэр яг энэ байрнаасаа байлдана; удирдлагаа авсан гишүүн чөлөөтэй.
    body.holding = holdMark[activeMember] && !leaveDead;
    body.holdPoint = oldPosition;
    holdMark[activeMember] = false;
    holdMark[target] = false;

    activeMember = target;
    aimToggled = false; // шинэ гишүүн онилоогүй байдлаар эхэлнэ
    // Элсүүлсэн цэргүүд шинэ удирдагчийн эргэн тойронд шууд байр сонгоно (түүнийг хамгаална).
    for (Soldier& soldier : squad.All())
    {
        if (soldier.member < 0) soldier.guardTimer = 0.0f;
    }
    arsenal = arsenals[target];
    player.SetLook(TeamMemberLook(target, progress.leaderSkin));
    player.SetHeld(HeldFor(arsenal.Current()));

    if (toGunner >= 0)
    {
        // Хуучин бие (одоо AI) жолоочийн суудалд, тоглогч буудагчийн суудалд
        garage.Dismount();
        gunnerVehicle = toGunner;
        carDriver = index;
        carGunner = -1;
        body.riding = toGunner;
        driverStuck = driverReverse = 0.0f;
        SetNotice(std::string(TeamMemberName(body.member)) + " жолоодож байна — та буудна. Буцааж солих: Tab", 3.0f);
    }
    else if (toDriver >= 0)
    {
        gunnerVehicle = -1;
        carDriver = -1;
        garage.SetEscort(-1);
        garage.SetEscortInput(0.0f, 0.0f);
        garage.Mount(toDriver);
        carGunner = index;
        body.riding = toDriver;
        SetNotice("Та жолоодож байна — " + std::string(TeamMemberName(body.member)) + " буудна", 2.5f);
    }
    return true;
}

namespace
{
    constexpr int kTutorialSteps = 5;
    constexpr float kTutorialStepTime = 25.0f; // хийгээгүй ч энэ хугацааны дараа дараагийн алхам
}

const char* Game::TutorialText() const
{
    const bool touch = InTouchMode();
    switch (progress.tutorial)
    {
    case 0: return touch ? "Зүүн талын жойстикоор хөдөлнө" : "WASD — хөдлөх,  Shift — гүйх";
    case 1: return touch ? "ГАЛ товчийг дарж зомби руу буудна" : "Хулганы зүүн товч — буудах,  баруун — онилох";
    case 2: return touch ? "Зүүн дээд хөргийг товшиж өөр дайчин руу шилжинэ" : "Tab — өөр дайчин руу шилжих";
    case 3: return touch ? "Унаанд ойртоод дугуй товчоор сууна" : "Унааны дэргэд F — суух / буух";
    case 4: return touch ? "Газрын зургийг (баруун доод) товшиж даалгавар руу очно" : "M — газрын зураг: шар тэмдэг бол даалгавар";
    default: return nullptr;
    }
}

void Game::UpdateTutorial(float dt)
{
    if (progress.tutorial >= kTutorialSteps || options.screenshotPath || coopGuest) return;
    if (tutorialTimer <= 0.0f)
    {
        // Алхмын эхлэл
        tutorialFrom = player.Position();
        tutorialShots = localShots;
        tutorialMember = activeMember;
    }
    tutorialTimer += dt;
    bool done = tutorialTimer > kTutorialStepTime;
    switch (progress.tutorial)
    {
    case 0: done = done || HorizontalDistance(player.Position(), tutorialFrom) > 6.0f; break;
    case 1: done = done || (uint8_t)(localShots - tutorialShots) >= 3; break;
    case 2: done = done || activeMember != tutorialMember; break;
    case 3: done = done || garage.IsDriving() || gunnerVehicle >= 0; break;
    case 4: done = done || tutorialMapSeen; break;
    default: break;
    }
    if (!done || tutorialTimer < 1.5f) return; // (алхам бүр дор хаяж 1.5 секунд харагдана)
    ++progress.tutorial;
    tutorialTimer = 0.0f;
    if (progress.tutorial >= kTutorialSteps)
    {
        progress.tutorial = kTutorialDone;
        SetNotice("ЗААВАР ДУУСЛАА — АМЖИЛТ!", 2.5f);
    }
    Save();
}

namespace
{
    constexpr int kEventHorde = 2, kEventSurvivor = 3, kEventAirdrop = 0, kEventCrash = 1;
}

bool Game::EventSpot(float minDistance, float maxDistance, Vector3& spot) const
{
    // Тоглогчийн харж буй (эсвэл явж буй) зүгт, хилийн дотор, барилгын гадна
    const float base = garage.IsDriving() ? garage.At(garage.Driven()).heading : camera.Yaw();
    for (int attempt = 0; attempt < 12; ++attempt)
    {
        const float angle = base + (float)GetRandomValue(-45, 45) * DEG2RAD;
        const float distance = minDistance + (maxDistance - minDistance) * (float)GetRandomValue(0, 100) / 100.0f;
        const Vector3 p = { player.Position().x + std::sin(angle) * distance, 0.0f, player.Position().z + std::cos(angle) * distance };
        if (!world.InsideBoundary(p.x, p.z) || world.InteriorAt(p.x, p.z) >= 0) continue;
        spot = { p.x, world.GroundHeight(p.x, p.z), p.z };
        return true;
    }
    return false;
}

void Game::ShowWorldEvent(int kind, Vector3 position)
{
    (void)position;
    switch (kind)
    {
    case kEventAirdrop: SetNotice("НИСЭХ ОНГОЦ АЧАА ХАЯЛАА! — сум, гранат, эм (ногоон утаа)", 4.0f); break;
    case kEventCrash: SetNotice("СҮЙРСЭН МАШИН урд талд — ачааг нь ав!", 4.0f); break;
    case kEventHorde: SetNotice("УРД ЗОМБИЙН БӨӨГНӨРӨЛ! — болгоомжтой", 4.0f); break;
    case kEventSurvivor: SetNotice("АВРАХ ХҮН БАЙНА! — газрын зураг дээрх тэмдэг рүү оч", 4.0f); break;
    default: break;
    }
}

bool Game::StartWorldEvent(int kind)
{
    Vector3 spot = {};
    if (kind == kEventCrash)
    {
        // Урд байгаа, хэн ч суугаагүй, эвдрээгүй унааг сүйрүүлнэ (гал, утаа), дэргэд нь ачаа
        int best = -1;
        float bestDistance = 260.0f;
        for (int i = 0; i < garage.Count(); ++i)
        {
            const Vehicle& v = garage.At(i);
            if (v.wrecked || v.occupied || v.remote || i == garage.Driven() || i == garage.Escort() || i == gunnerVehicle ||
                v.type == VehicleType::Horse) continue;
            const float d = HorizontalDistance(v.position, player.Position());
            if (d > 90.0f && d < bestDistance)
            {
                bestDistance = d;
                best = i;
            }
        }
        if (best < 0) return false;
        garage.Damage(best, 1e6f);
        const Vehicle& v = garage.At(best);
        spot = HumanoidLocalToWorld(v.position, v.heading, { SpecFor(v.type).radius + 1.8f, 0.0f, 0.0f });
        spot.y = world.GroundHeight(spot.x, spot.z);
        const int id = nextEventId++;
        events.AddCrate((uint16_t)id, spot, 0.0f);
        for (int k = 0; k < 6; ++k)
        {
            const float a = (float)k * 1.05f;
            horde.Spawn(ZombieType::Walker, { spot.x + std::sin(a) * 9.0f, spot.y, spot.z + std::cos(a) * 9.0f }, params, 0, false);
        }
        if (remoteActive && net.Connected()) net.SendWorldEvent(kind, id, spot, 0.0f);
    }
    else
    {
        if (!EventSpot(140.0f, garage.IsDriving() ? 320.0f : 240.0f, spot)) return false;
        if (kind == kEventAirdrop)
        {
            const int id = nextEventId++;
            events.AddCrate((uint16_t)id, spot, 140.0f);
            if (remoteActive && net.Connected()) net.SendWorldEvent(kind, id, spot, 140.0f);
        }
        else if (kind == kEventHorde)
        {
            const int count = GetRandomValue(14, 18);
            for (int k = 0; k < count; ++k)
            {
                const float a = (float)GetRandomValue(0, 359) * DEG2RAD, r = (float)GetRandomValue(2, 14);
                Vector3 p = { spot.x + std::sin(a) * r, 0.0f, spot.z + std::cos(a) * r };
                p.y = world.GroundHeight(p.x, p.z);
                horde.Spawn(ZombieType::Walker, p, params, 0, false);
            }
            events.AddMarker(spot, 120.0f);
            if (remoteActive && net.Connected()) net.SendWorldEvent(kind, 0, spot, 0.0f);
        }
        else if (kind == kEventSurvivor)
        {
            squad.Add(spot, 0.0f);
            Soldier& survivor = squad.All().back();
            survivor.waitingRescue = true;
            survivor.holdPoint = spot;
            rescueSoldiers.push_back(squad.Total() - 1);
            for (int k = 0; k < 8; ++k)
            {
                const float a = (float)k * 0.785f;
                Vector3 p = { spot.x + std::sin(a) * 16.0f, 0.0f, spot.z + std::cos(a) * 16.0f };
                p.y = world.GroundHeight(p.x, p.z);
                horde.Spawn(ZombieType::Walker, p, params, 0, true);
            }
            events.AddMarker(spot, 180.0f);
            if (remoteActive && net.Connected()) net.SendWorldEvent(kind, 0, spot, 0.0f);
        }
    }
    ShowWorldEvent(kind, spot);
    TraceLog(LOG_INFO, "EVENT: kind %d at %.0f, %.0f", kind, spot.x, spot.z);
    return true;
}

void Game::UpdateDowned(float dt)
{
    player.UpdateDowned(dt);
    if (player.IsDowned() && garage.IsDriving())
    {
        garage.Dismount(); // (унаан дээр унахгүй ч хамгаалалт)
        player.StopRiding(player.Position());
    }
    // AI нөхөд: унасан тоглогч руу гүйж очиж босгоно (хост тооцно; зочны тоглогчийг ч)
    if (!coopGuest)
    {
        const bool guestDown = remoteActive && guestFigure.alive && (guestFigure.flags & Net::kDowned);
        squad.SetLeadersDowned(player.IsDowned(), guestDown);
        if (squad.TakeHostRevived())
        {
            player.Revive(0.3f);
            SetNotice("НӨХӨР ЧИНЬ БОСГОЛОО!", 2.0f);
        }
        if (squad.TakeGuestRevived() && net.Connected())
        {
            net.SendRevive();
            guestShadow = -1.0f; // босгосон: давхар тооцоо дараагийн мэдээгээр
            guestForcedDown = false;
        }
    }
    // Хүн хүнээ: нөгөө тоглогч унасан бол дэргэд нь 3 секунд зогсвол босгоно
    const RemoteFigure& other = coopGuest ? hostFigure : guestFigure;
    const bool otherDown = net.Connected() && (coopGuest || remoteActive) && other.alive && (other.flags & Net::kDowned);
    if (otherDown && player.IsAlive() && !player.IsDowned() && !garage.IsDriving() &&
        HorizontalDistance(player.Position(), other.position) < 2.2f)
    {
        humanRevive += dt;
        if (humanRevive >= 3.0f)
        {
            humanRevive = 0.0f;
            net.SendRevive();
            if (!coopGuest)
            {
                guestShadow = -1.0f;
                guestForcedDown = false;
            }
            SetNotice("НӨХРӨӨ БОСГОЛОО!", 2.0f);
        }
    }
    else
    {
        humanRevive = 0.0f;
    }
}

void Game::TakeCrate(int id)
{
    if (coopGuest && net.Connected())
    {
        // Зочин: хайрцгийг хост олгоно (хоёулаа зэрэг авбал нэг л хүнд). Хүсэлтийг нэг л удаа.
        if (std::find(requestedCrates.begin(), requestedCrates.end(), id) != requestedCrates.end()) return;
        requestedCrates.push_back(id);
        net.SendCrateTaken(id);
        return;
    }
    if (!events.RemoveCrate((uint16_t)id)) return;
    if (net.Connected() && remoteActive) net.SendCrateTaken(id);
    GiveCrateReward();
}

void Game::GiveCrateReward()
{
    // Ачаа: багийн нөөц сум, гранат, амь
    ammoReserve += 300;
    arsenal.AddGrenades(2);
    for (Arsenal& other : arsenals) other.AddGrenades(1);
    player.Heal(player.MaxHealth() * 0.4f);
    Audio::Play(Sfx::Reload, player.Position(), 0.9f);
    SetNotice("АЧАА АВЛАА: +300 сум, +2 гранат, +40% амь", 3.0f);
}

void Game::UpdateWorldEvents(float dt)
{
    events.Update(dt);
    if (options.eventKind >= 0 && missionFrame == 5 && !coopGuest) StartWorldEvent(options.eventKind); // туршилт
    // Хайрцаг газардлаа: дуунд ойрын зомби сэрнэ (хост)
    for (const Vector3& landed : events.TakeLanded())
    {
        particles.Dust(landed);
        Audio::Play(Sfx::Thud, landed, 1.0f, 0.7f);
        ChatterNearest(VoiceLine::Crate, player.Position());
        if (!coopGuest) horde.AlertAround(landed, 70.0f);
    }
    // Ойртсон хайрцгийг авна
    if (player.IsAlive() && !garage.IsDriving())
    {
        const int crate = events.CrateNear(player.Position(), 2.6f);
        if (crate >= 0) TakeCrate(crate);
    }
    if (coopGuest) return;
    // Аврах хүн: тоглогч (эсвэл зочин) 9 м-т ойртвол багт нэгдэнэ
    for (size_t i = 0; i < rescueSoldiers.size();)
    {
        const int index = rescueSoldiers[i];
        if (index < 0 || index >= squad.Total() || !squad.All()[(size_t)index].alive)
        {
            rescueSoldiers.erase(rescueSoldiers.begin() + (long)i);
            continue;
        }
        Soldier& survivor = squad.All()[(size_t)index];
        const bool reached = HorizontalDistance(player.Position(), survivor.mover.position) < 9.0f ||
                             (remoteActive && guestFigure.alive && HorizontalDistance(guestFigure.position, survivor.mover.position) < 9.0f);
        if (reached)
        {
            survivor.waitingRescue = false;
            progress.coins += 25;
            SetNotice("АВРАГДЛАА! — танай багт нэгдэв (+25 зоос)", 3.0f);
            rescueSoldiers.erase(rescueSoldiers.begin() + (long)i);
            continue;
        }
        ++i;
    }
    // Дараагийн үйл явдал
    if (state != GameState::Playing || options.screenshotPath) return;
    eventTimer -= dt;
    if (eventTimer > 0.0f) return;
    eventTimer = (float)GetRandomValue(100, 160);
    const int roll = GetRandomValue(0, 99);
    const int kind = roll < 30 ? kEventHorde : roll < 55 ? kEventAirdrop : roll < 80 ? kEventSurvivor : kEventCrash;
    if (!StartWorldEvent(kind) && kind != kEventAirdrop) StartWorldEvent(kEventAirdrop);
}

void Game::ExploderBlasts()
{
    // Бөөн зомби үхэхдээ дэлбэрнэ: ойрын зомби (гинжин дэлбэрэлт), хүн, торх гэмтэнэ. Зочин: зөвхөн харагдана.
    const float radius = 5.5f, damage = 55.0f;
    for (int round = 0; round < 4; ++round)
    {
        const std::vector<Vector3> blasts = horde.TakeExploderBlasts();
        if (blasts.empty()) break;
        for (const Vector3& blast : blasts)
        {
            particles.Explosion(blast);
            particles.Blood(blast, { 0.0f, 1.0f, 0.0f }, 24);
            Audio::Play(Sfx::Explosion, blast, 0.7f, 1.25f);
            if (coopGuest) continue;
            horde.Explode(blast, radius, 140.0f);
            barrels.Blast(blast, radius);
            const float d = Vector3Distance(Vector3Add(player.Position(), { 0.0f, 1.1f, 0.0f }), blast);
            if (player.IsAlive() && d < radius) player.TakeDamageFrom(damage * (1.0f - d / radius), blast);
            for (int k = 0; k < squad.Total(); ++k)
            {
                const Soldier& soldier = squad.All()[(size_t)k];
                const float ds = Vector3Distance(Vector3Add(soldier.mover.position, { 0.0f, 1.1f, 0.0f }), blast);
                if (soldier.alive && ds < radius) squad.Damage(k, damage * (1.0f - ds / radius));
            }
            if (remoteActive && guestFigure.alive && net.Connected())
            {
                const float dg = Vector3Distance(Vector3Add(guestFigure.position, { 0.0f, 1.1f, 0.0f }), blast);
                if (dg < radius) DamageGuest(damage * (1.0f - dg / radius));
            }
        }
    }
}

void Game::MarkHit(int kind)
{
    // Нэг буудалтад (бөөрөнцөг олон) нэг л дуу: хамгийн "хүчтэй" тэмдгийг үлдээнэ
    const bool fresh = hitMarker < 0.8f;
    if (fresh || kind > hitMarkerKind) hitMarkerKind = kind;
    hitMarker = 1.0f;
}

void Game::UpdateFeedback(float dt)
{
    hitMarker = std::max(0.0f, hitMarker - dt * 4.0f);
    damageArrow = std::max(0.0f, damageArrow - dt * 0.9f);
    // Цохиулсан: зүг (мэдэгдэж байвал), камер сэгсэрнэ
    if (player.TimeSinceHit() < dt * 1.5f && player.IsAlive())
    {
        Vector3 source = {};
        if (player.HitDirection(source))
        {
            damageFrom = source;
            damageArrow = 1.0f;
        }
        camera.Shake(0.22f);
    }
    // Амь бага: зүрхний цохилт (амь багасах тусам олон)
    const float ratio = player.MaxHealth() > 0.0f ? player.Health() / player.MaxHealth() : 1.0f;
    heartbeatTimer -= dt;
    if (player.IsAlive() && ratio < 0.3f && heartbeatTimer <= 0.0f)
    {
        heartbeatTimer = 0.55f + ratio * 2.0f;
        Audio::Play(Sfx::Heartbeat, camera.Get().position, 0.75f * (1.0f - ratio * 2.0f), 1.0f);
    }
    // Унаа мөргөхөд (бүтэн байдал огцом буурвал) сэгсэрнэ
    const int ride = garage.IsDriving() ? garage.Driven() : gunnerVehicle;
    const float health = ride >= 0 ? garage.At(ride).health : -1.0f;
    if (ride >= 0 && lastDrivenHealth > 0.0f && lastDrivenHealth - health > 4.0f)
    {
        camera.Shake(std::min(0.8f, (lastDrivenHealth - health) / 40.0f));
    }
    lastDrivenHealth = health;
}

void Game::HitVehicle(int index, float damage, Vector3 at, Vector3 direction)
{
    const Vehicle& vehicle = garage.At(index);
    const bool horse = vehicle.type == VehicleType::Horse;
    if (horse) particles.Blood(at, Vector3Negate(direction), 6);
    else particles.Flash(at, Vector3Negate(direction)); // төмөрт тусах оч
    if (vehicle.wrecked) return;
    const float amount = damage * (horse ? 1.5f : 1.0f);
    // Унааг хост тооцдог; зочны жолоодож буйг зочин. Нөгөө талынх бол хохирлыг илгээнэ.
    if (coopGuest)
    {
        if (index == garage.Driven()) garage.Damage(index, amount);
        else net.SendVehicleHit(index, amount);
    }
    else if (vehicle.remote)
    {
        if (remoteActive && net.Connected()) net.SendVehicleHit(index, amount);
    }
    else
    {
        garage.Damage(index, amount);
    }
}

void Game::UpdateVehicleCombat(float dt)
{
    (void)dt;
    // --- Эвдэрсэн машин дэлбэрнэ (морь биш): ойрын зомби, хүн, торх, унааг гэмтээнэ ---
    if (wreckSeen.size() != (size_t)garage.Count())
    {
        wreckSeen.assign((size_t)garage.Count(), 0);
        for (int i = 0; i < garage.Count(); ++i) wreckSeen[(size_t)i] = garage.At(i).wrecked ? 1 : 0;
    }
    for (int i = 0; i < garage.Count(); ++i)
    {
        const Vehicle& vehicle = garage.At(i);
        if (!vehicle.wrecked)
        {
            wreckSeen[(size_t)i] = 0; // засварласан
            continue;
        }
        if (wreckSeen[(size_t)i]) continue;
        wreckSeen[(size_t)i] = 1;
        if (vehicle.type == VehicleType::Horse) continue;
        const Vector3 blast = Vector3Add(vehicle.position, { 0.0f, 1.0f, 0.0f });
        const float radius = 7.0f;
        particles.Explosion(blast); // (ойрын унаануудыг ч UpdateVehicles гэмтээнэ)
        if (!coopGuest)
        {
            horde.Explode(blast, radius, 140.0f);
            barrels.Blast(blast, radius);
            for (int k = 0; k < squad.Total(); ++k)
            {
                const Soldier& soldier = squad.All()[(size_t)k];
                const float d = Vector3Distance(soldier.mover.position, vehicle.position);
                if (soldier.alive && d < radius) squad.Damage(k, 70.0f * (1.0f - d / radius));
            }
        }
        const float d = Vector3Distance(player.Position(), vehicle.position);
        if (player.IsAlive() && d < radius) player.TakeDamage(70.0f * (1.0f - d / radius));
    }

    // --- Тулаан: эсрэг багийн хүнийг хурдтай унаагаар дайрвал амийн 30% хасагдана (нэг дайралтанд нэг удаа) ---
    if (!battleActive || !garage.IsDriving()) return;
    const Vehicle& car = garage.At(garage.Driven());
    if (std::fabs(car.speed) < 5.0f) return;
    const double now = GetTime();
    const float reach = SpecFor(car.type).radius + 0.6f;
    auto ram = [&](Vector3 feet, int id, auto&& apply) {
        if (HorizontalDistance(feet, car.position) > reach || std::fabs(feet.y - car.position.y) > 2.5f) return;
        for (const auto& entry : rammedAt)
        {
            if (entry.first == id && now - entry.second < 1.5) return;
        }
        rammedAt.erase(std::remove_if(rammedAt.begin(), rammedAt.end(), [&](const std::pair<int, double>& e) { return e.first == id; }),
                       rammedAt.end());
        rammedAt.push_back({ id, now });
        apply();
        particles.Blood(Vector3Add(feet, { 0.0f, 1.0f, 0.0f }), { std::sin(car.heading), 0.3f, std::cos(car.heading) }, 12);
        Audio::Play(Sfx::Hit, feet, 0.8f);
        TraceLog(LOG_INFO, "BATTLE: rammed %d with a vehicle", id);
    };
    // Сөрөг хохирол = амийн дээд хэмжээний хувь (хүлээн авагч тал өөрийн дээд хэмжээгээр тооцно)
    constexpr float kRamShare = 0.3f;
    if (!coopGuest)
    {
        if (remoteActive && guestFigure.alive && !(guestFigure.flags & Net::kRiding))
        {
            ram(guestFigure.position, kGuestTarget, [&] { DamageGuest(-kRamShare); });
        }
        for (int i = 0; i < squad.Total(); ++i)
        {
            const Soldier& soldier = squad.All()[(size_t)i];
            if (soldier.alive && soldier.guestTeam && soldier.member >= 0 && soldier.riding < 0)
            {
                ram(soldier.mover.position, i, [&] { squad.DamageFraction(i, kRamShare); });
            }
        }
    }
    else
    {
        if (hostFigure.alive && !(hostFigure.flags & Net::kRiding))
        {
            ram(hostFigure.position, (int)Net::kHostPlayer, [&] { net.SendHitHuman(Net::kHostPlayer, -kRamShare); });
        }
        for (size_t i = 0; i < hostSoldiers.size() && i < hostSoldierSide.size(); ++i)
        {
            if (hostSoldierSide[i] == 1 && hostSoldiers[i].alive && hostSoldiers[i].seatStyle == 0)
            {
                ram(hostSoldiers[i].position, (int)i, [&] { net.SendHitHuman((uint32_t)i, -kRamShare); });
            }
        }
    }
}

void Game::TrackSoldierLosses()
{
    if (coopGuest) return; // зочны цэргүүдийг хост тооцож SoldierLost илгээнэ
    int own = 0, guest = 0;
    for (Soldier& soldier : squad.All())
    {
        if (!soldier.bought || soldier.alive || soldier.lossSent) continue;
        soldier.lossSent = true;
        if (soldier.guestTeam) ++guest;
        else ++own;
    }
    if (guest > 0 && remoteActive && net.Connected()) net.SendSoldierLost(guest);
    if (own > 0)
    {
        TraceLog(LOG_INFO, "SQUAD: lost %d Gold soldier(s)", own);
        if (Account::ServerShop()) Account::LoseSoldiers(own);
        else progress.soldiers = std::max(0, progress.soldiers - own);
        SetNotice(own == 1 ? "Gold-оор авсан цэрэг үхлээ — цэргийн тоо хасагдлаа" : "Gold-оор авсан цэргүүд үхлээ — цэргийн тоо хасагдлаа", 2.5f);
    }
}

void Game::LeaveGunnerSeat()
{
    if (gunnerVehicle < 0) return;
    const Vehicle& vehicle = garage.At(gunnerVehicle);
    const float side = SpecFor(vehicle.type).radius + config.playerRadius + 0.3f;
    // Тоглогч баруун талд, жолооч зүүн талд бууна.
    Vector3 exit = HumanoidLocalToWorld(vehicle.position, vehicle.heading, { -side, 0.0f, 0.0f });
    world.ResolveHorizontal(exit, config.playerRadius, config.playerHeight, config.stepHeight);
    exit.y = world.GroundHeight(exit.x, exit.z);
    player.StopRiding(exit);
    std::vector<Soldier>& all = squad.All();
    if (carDriver >= 0 && carDriver < (int)all.size())
    {
        Soldier& driver = all[(size_t)carDriver];
        Vector3 out = HumanoidLocalToWorld(vehicle.position, vehicle.heading, { side, 0.0f, 0.0f });
        world.ResolveHorizontal(out, config.playerRadius, config.playerHeight, 0.0f);
        out.y = world.GroundHeight(out.x, out.z);
        if (driver.riding >= 0) driver.mover.position = out;
        driver.riding = -1;
        driver.boarding = false;
        driver.seatStyle = 0;
        driver.hasRein = false;
    }
    gunnerVehicle = -1;
    carDriver = -1;
    garage.SetEscort(-1);
    garage.SetEscortInput(0.0f, 0.0f);
}

void Game::DriveForGunner(float dt)
{
    const Vehicle& vehicle = garage.At(gunnerVehicle);
    // Зорилго: хамгийн ойрын даалгаврын тэмдэг. Байхгүй бол зогсоно (тоглогч буудна).
    std::vector<Vector3> markers;
    mission.Markers(markers);
    float best = 1e9f;
    Vector3 goal = vehicle.position;
    for (const Vector3& marker : markers)
    {
        const float d = HorizontalDistance(marker, vehicle.position);
        if (d < best)
        {
            best = d;
            goal = marker;
        }
    }
    const float dx = goal.x - vehicle.position.x, dz = goal.z - vehicle.position.z;
    const float distance = std::sqrt(dx * dx + dz * dz);
    const float turn = std::remainder(std::atan2(dx, dz) - vehicle.heading, 2.0f * PI);
    float throttle = 0.0f, steer = std::clamp(-turn * 2.5f, -1.0f, 1.0f);
    if (distance > 18.0f) throttle = distance > 40.0f ? 0.8f : 0.45f;
    else if (vehicle.speed > 1.0f) throttle = -1.0f; // хүрлээ: зогсоно
    else steer = 0.0f;
    if (std::fabs(turn) > 1.2f && vehicle.speed > 5.0f) throttle = 0.0f;

    // Гацвал (хана, мод) хэсэг ухарч, эсрэг тийш эргэнэ.
    driverStuck = throttle > 0.0f && std::fabs(vehicle.speed) < 1.0f ? driverStuck + dt : 0.0f;
    if (driverStuck > 2.0f)
    {
        driverStuck = 0.0f;
        driverReverse = 1.4f;
    }
    if (driverReverse > 0.0f)
    {
        driverReverse -= dt;
        throttle = -0.8f;
        steer = -steer;
    }
    garage.SetEscort(gunnerVehicle);
    garage.SetEscortInput(throttle, steer);
}

HeldItem Game::HeldFor(WeaponType type) const
{
    switch (type)
    {
    case WeaponType::Sniper: return HeldItem::Sniper;
    case WeaponType::Launcher: return HeldItem::Launcher;
    case WeaponType::Knife: return HeldItem::Knife;
    case WeaponType::Pistol: return HeldItem::Pistol;
    case WeaponType::Shotgun: return HeldItem::Shotgun;
    case WeaponType::Grenade: return arsenal.Ammo() > 0 ? HeldItem::Grenade : HeldItem::None;
    case WeaponType::Rifle:
    default: return HeldItem::Rifle;
    }
}

void Game::UpdateShooting()
{
    if (player.IsDowned()) return; // унасан: буудахгүй
    if (player.IsTraversing())
    {
        return; // шат, цонхоор гарч байхдаа буудахгүй
    }

    // --- Зэвсэг сонгох. Дайчин: 1 хутга, 2 гар буу, 3 автомат, 4 үрлэн буу, 5 гранат.
    //     Мэргэн буудагч: 1 хутга, 2 гар буу, 3 дурантай буу. ---
    for (int i = 0; i < arsenal.SlotCount(); ++i)
    {
        if (InKeyPressed(KEY_ONE + i))
        {
            arsenal.Select(arsenal.SlotWeapon(i));
        }
    }
    player.SetHeld(HeldFor(arsenal.Current()));

    if (InKeyPressed(KEY_R))
    {
        arsenal.Reload();
    }

    // Автомат зэвсэг товчийг дарж байхад, бусад нь товшилт бүрд нэг удаа
    const bool autoUse = options.screenshotPath && options.weapon > 0; // туршилтын горим
    const bool trigger = autoUse || autoFire || (arsenal.Spec().automatic ? InMouseDown(MOUSE_BUTTON_LEFT)
                                                                          : InMousePressed(MOUSE_BUTTON_LEFT));
    if (!trigger)
    {
        return;
    }

    player.AimAtPoint(CrosshairTarget());
    if (!arsenal.TryUse())
    {
        return;
    }

    // Онилгооны туяа: камераас дэлгэцийн төв рүү
    const Camera3D& cam = camera.Get();
    const Vector3 toTarget = Vector3Subtract(cam.target, cam.position);
    const float targetDistance = Vector3Length(toTarget);
    const Ray aim = { cam.position, Vector3Scale(toTarget, 1.0f / targetDistance) };
    // Дуран хараагаар харахад камер нүдэн дээр байгаа тул камер, тоглогч хоёрын хоорондох зай тэг.
    const float cameraDistance = camera.IsScoped() || camera.IsFirstPerson() ? 0.0f : targetDistance;

    const Vector3 hand = Vector3Add(player.Position(), { 0.0f, 1.4f, 0.0f });
    switch (arsenal.Current())
    {
    case WeaponType::Knife: Audio::Play(Sfx::Knife, hand); UseKnife(aim, cameraDistance); break;
    case WeaponType::Grenade:
        Audio::Play(Sfx::Throw, hand);
        ThrowGrenade(aim);
        ChatterPlayer(VoiceLine::Grenade);
        break;
    case WeaponType::Launcher: Audio::Play(Sfx::Launcher, hand); FireLauncher(aim, cameraDistance); break;
    default:
        Audio::Play(arsenal.Current() == WeaponType::Pistol ? Sfx::Pistol
                    : arsenal.Current() == WeaponType::Shotgun ? Sfx::Shotgun
                    : arsenal.Current() == WeaponType::Sniper ? Sfx::Sniper : Sfx::Rifle,
                    hand, 0.9f);
        FireGun(aim, cameraDistance);
        break;
    }
}

Vector3 Game::EffectMuzzle() const
{
    // 1-р этгээдийн үед сумны мөр, амны гал дэлгэц дээрх бууны амнаас гарна.
    return camera.IsFirstPerson() ? ViewModelMuzzle(camera.Get(), player.Held(), camera.AimBlend()) : player.MuzzlePosition();
}

Vector3 Game::CrosshairTarget() const
{
    // Дэлгэцийн төвийн туяа хана, газар эсвэл зомбид тусах цэг: буу яг энэ цэг рүү чиглэнэ.
    const Camera3D& cam = camera.Get();
    const Vector3 toTarget = Vector3Subtract(cam.target, cam.position);
    const float targetDistance = Vector3Length(toTarget);
    const Ray ray = { cam.position, Vector3Scale(toTarget, 1.0f / targetDistance) };
    const float cameraDistance = camera.IsScoped() || camera.IsFirstPerson() ? 0.0f : targetDistance;
    const float wall = world.RayDistance(ray, cameraDistance + 150.0f);
    const ZombieHit hit = horde.Raycast(ray, cameraDistance * 0.9f, wall);
    // Тоглогчийн ард (камер, тоглогч хоёрын дунд) байх цэг рүү онилохгүй.
    const float distance = std::max(hit.index >= 0 ? hit.distance : wall, cameraDistance + 1.0f);
    return Vector3Add(ray.position, Vector3Scale(ray.direction, distance));
}

void Game::UseKnife(Ray aim, float cameraDistance)
{
    player.StartAction(0.38f);

    // Эхлээд онилсон зомби (толгой эсэхийг ялгана), үгүй бол урд байгаа хамгийн ойрынх.
    int target = -1;
    bool head = false;
    Vector3 point = {};

    const ZombieHit hit = horde.Raycast(aim, cameraDistance * 0.9f, cameraDistance + kKnifeReach + 1.5f);
    if (hit.index >= 0)
    {
        const Zombie& zombie = horde.At(hit.index);
        if (HorizontalDistance(player.Position(), zombie.mover.position) <= kKnifeReach + zombie.Radius())
        {
            target = hit.index;
            head = hit.headshot;
            point = Vector3Add(aim.position, Vector3Scale(aim.direction, hit.distance));
        }
    }
    if (target < 0)
    {
        target = horde.NearestInFront(player.Position(), camera.Yaw(), kKnifeReach);
        if (target >= 0)
        {
            point = horde.At(target).Chest();
        }
    }
    if (target < 0)
    {
        return; // агаарт хатгасан
    }

    // Биед 5, толгойд 2 удаа хатгахад үхнэ (зомбины төрлөөс үл хамаарна).
    horde.DamageFraction(target, head ? kKnifeHeadFraction : kKnifeBodyFraction);
    if (coopGuest)
    {
        // Зочин: хутганы хохирлыг хост тооцно (DamageFraction-тэй ижил томьёо)
        const Zombie& zombie = horde.At(target);
        const float fraction = (head ? kKnifeHeadFraction : kKnifeBodyFraction) * (zombie.boss ? 0.12f : 1.0f);
        net.SendHit(zombie.id, zombie.maxHealth * fraction + 0.01f, head);
    }
    particles.Blood(point, Vector3Negate(aim.direction), head ? 10 : 6);
}

void Game::FireGun(Ray aim, float cameraDistance)
{
    const WeaponSpec& spec = arsenal.Spec();
    const Vector3 muzzle = EffectMuzzle();

    // Тархалтыг тооцох тэнхлэгүүд (онилгооны чиглэлд перпендикуляр)
    const Vector3 side = Vector3Normalize(Vector3CrossProduct(aim.direction, { 0.0f, 1.0f, 0.0f }));
    const Vector3 up = Vector3CrossProduct(side, aim.direction);

    for (int pellet = 0; pellet < spec.pellets; ++pellet)
    {
        const float angle = (float)GetRandomValue(0, 359) * DEG2RAD;
        // Суусан, хэвтсэн үед буу тогтвортой: тархалт багасна.
        const float offset = spec.spread * player.SpreadScale() * std::sqrt((float)GetRandomValue(0, 1000) / 1000.0f);
        Vector3 direction = aim.direction;
        direction = Vector3Add(direction, Vector3Scale(side, std::cos(angle) * offset));
        direction = Vector3Add(direction, Vector3Scale(up, std::sin(angle) * offset));
        const Ray ray = { aim.position, Vector3Normalize(direction) };

        const float reach = cameraDistance + spec.range;
        const float wallDistance = world.RayDistance(ray, reach);
        const ZombieHit hit = horde.Raycast(ray, cameraDistance * 0.9f, wallDistance);

        // Бензинтэй торх зомби, хананаас өмнө байвал сум түүнд тусна.
        float barrelDistance = 0.0f;
        const int barrel = barrels.Raycast(ray, cameraDistance * 0.9f, hit.index >= 0 ? hit.distance : wallDistance, barrelDistance);
        if (barrel >= 0)
        {
            barrels.Damage(barrel, arsenal.Damage());
            if (coopGuest) net.SendBarrelHit(barrel, arsenal.Damage());
            const Vector3 at = Vector3Add(ray.position, Vector3Scale(ray.direction, barrelDistance));
            tracers.Add(muzzle, at, YELLOW);
            particles.Flash(at, Vector3Negate(ray.direction)); // оч
            continue;
        }

        // Унаа (машин, морь) зомби, хананаас өмнө байвал сум түүнд тусна. Өөрийн болон багийн унааг буудахгүй.
        float vehicleDistance = 0.0f;
        const int ownVehicle = garage.IsDriving() ? garage.Driven() : gunnerVehicle;
        const int vehicle = garage.Raycast(ray, cameraDistance * 0.9f, hit.index >= 0 ? hit.distance : wallDistance, ownVehicle,
                                           teamVehicle, vehicleDistance);

        // Тулаан: эсрэг багийн хүн зомби, хана, унаанаас өмнө байвал сум түүнд тусна
        if (battleActive)
        {
            float humanDistance = 0.0f;
            bool humanHead = false;
            const int human = RaycastHumans(ray, cameraDistance * 0.9f,
                                            vehicle >= 0 ? vehicleDistance : hit.index >= 0 ? hit.distance : wallDistance,
                                            humanDistance, humanHead);
            if (human != -1)
            {
                // Толгойд оновол их хохирол (4 дахин)
                const float damage = arsenal.Damage() * (humanHead ? 4.0f : 1.0f);
                if (coopGuest) net.SendHitHuman((uint32_t)human, damage);
                else if (human == kGuestTarget) DamageGuest(damage);
                else squad.Damage(human, damage);
                const Vector3 at = Vector3Add(ray.position, Vector3Scale(ray.direction, humanDistance));
                tracers.Add(muzzle, at, humanHead ? RED : YELLOW);
                if (pellet == 0) localShotEnd = at;
                particles.Blood(at, Vector3Negate(ray.direction), humanHead ? 12 : 6);
                if (pellet == 0) Audio::Play(Sfx::Hit, at, humanHead ? 0.6f : 0.4f);
                MarkHit(humanHead ? 1 : 0);
                continue;
            }
        }

        if (vehicle >= 0)
        {
            const Vector3 at = Vector3Add(ray.position, Vector3Scale(ray.direction, vehicleDistance));
            tracers.Add(muzzle, at, YELLOW);
            if (pellet == 0) localShotEnd = at;
            HitVehicle(vehicle, arsenal.Damage(), at, ray.direction);
            continue;
        }

        const float distance = hit.index >= 0 ? hit.distance : wallDistance;
        const Vector3 end = Vector3Add(ray.position, Vector3Scale(ray.direction, distance));
        tracers.Add(muzzle, end, hit.headshot ? RED : YELLOW);
        if (pellet == 0) localShotEnd = end; // хамтарсан тоглолт: нөгөө талд сумны мөр зурна

        if (hit.index >= 0)
        {
            // Толгойд оновол нэг сумаар устгана. Аварга зомби л үл хамаарна: толгойд гурав дахин хохирол.
            const float headshot = horde.At(hit.index).boss ? arsenal.Damage() * 3.0f : kHeadshotDamage;
            if (coopGuest) net.SendHit(horde.At(hit.index).id, hit.headshot ? headshot : arsenal.Damage(), hit.headshot);
            horde.Damage(hit.index, hit.headshot ? headshot : arsenal.Damage());
            MarkHit(horde.At(hit.index).health <= 0.0f ? 2 : hit.headshot ? 1 : 0);
            particles.Blood(end, Vector3Negate(ray.direction), hit.headshot ? 14 : (spec.pellets > 1 ? 3 : 7));
            if (pellet == 0) Audio::Play(Sfx::Hit, end, hit.headshot ? 0.6f : 0.4f);
        }
        else if (wallDistance < reach)
        {
            particles.Dust(end); // газар эсвэл хананд туссан
        }
    }

    // Буун дуунд ойр орчмын зомби сэрж, дуу гарсан зүг рүү (байшин дотор байсан ч) дайрна.
    horde.AlertAround(player.Position(), 95.0f);

    // Бууны эффект: ухралт, амны гал, баруун тийш үсрэх хонгио
    const float facing = player.Facing();
    const Vector3 rightSide = { -std::cos(facing), 0.0f, std::sin(facing) };
    const Vector3 forward = { std::sin(facing), 0.0f, std::cos(facing) };
    player.OnFired();
    ++localShots;
    {
        // Камерын ухралт: зэвсгээс хамаарна (суусан, хэвтсэн үед бага)
        float kick = 0.006f;
        switch (arsenal.Current())
        {
        case WeaponType::Pistol: kick = 0.011f; break;
        case WeaponType::Shotgun: kick = 0.03f; break;
        case WeaponType::Sniper: kick = 0.035f; break;
        default: break;
        }
        kick *= player.SpreadScale() * (InTouchMode() ? 0.6f : 1.0f);
        camera.Kick(kick * 0.35f * (float)GetRandomValue(-100, 100) / 100.0f, kick);
        if (arsenal.Current() == WeaponType::Shotgun || arsenal.Current() == WeaponType::Sniper) camera.Shake(0.12f);
    }
    particles.Flash(muzzle, Vector3Normalize(Vector3Add(aim.direction, forward)));
    particles.Shell(Vector3Add(muzzle, Vector3Scale(forward, -0.4f)), rightSide);
}

void Game::FireLauncher(Ray aim, float cameraDistance)
{
    // Пуужин мөрөн дээрх хоолойноос гарч, загалмайн зааж буй цэг рүү шулуун ниснэ.
    const float reach = cameraDistance + arsenal.Spec().range;
    const float wall = world.RayDistance(aim, reach);
    const ZombieHit hit = horde.Raycast(aim, cameraDistance * 0.9f, wall);
    const float distance = hit.index >= 0 ? hit.distance : wall;
    const Vector3 target = Vector3Add(aim.position, Vector3Scale(aim.direction, distance));

    const Vector3 muzzle = EffectMuzzle();
    const Vector3 direction = Vector3Normalize(Vector3Subtract(target, muzzle));
    grenades.Launch(muzzle, Vector3Scale(direction, kRocketSpeed));
    if (net.Connected() && (coopGuest || remoteActive)) net.SendProjectile(1, muzzle, Vector3Scale(direction, kRocketSpeed));
    horde.AlertAround(player.Position(), 130.0f);

    // Харвалтын гал урд, ард хоёр талаас; хүчтэй ухралт
    const float facing = player.Facing();
    const Vector3 backward = { -std::sin(facing), 0.0f, -std::cos(facing) };
    player.OnFired();
    particles.Flash(muzzle, direction);
    particles.Flash(Vector3Add(muzzle, Vector3Scale(backward, 1.6f)), backward);
    particles.Dust(Vector3Add(muzzle, Vector3Scale(backward, 2.2f)));
}

void Game::ThrowGrenade(Ray aim)
{
    player.StartAction(0.7f);

    // Онилсон зүг рүү, бага зэрэг дээш нум зуруулж шиднэ.
    const Vector3 from = HumanoidLocalToWorld(player.Position(), player.Facing(), HumanoidMuzzleFor(HeldItem::Grenade));
    Vector3 velocity = Vector3Scale(aim.direction, 15.0f);
    velocity.y += 5.0f;
    grenades.Throw(from, velocity);
    if (net.Connected() && (coopGuest || remoteActive)) net.SendProjectile(0, from, velocity);
}

void Game::SpawnVehicles()
{
    garage.Clear();
    sentVehiclePosition.clear(); // шинэ унаанууд: зочинд дахин илгээнэ
    sentVehicleHealth.clear();

    // Аймгийн хилийн дотор байгаа цэгүүдэд л байрлуулна.
    // Суурингууд үе бүрд өөр тул хүссэн цэг дээр барилга, хашаа таарч болно: ойролцоох чөлөөт цэгийг хайна.
    auto place = [&](VehicleType type, float x, float z, float headingDegrees) {
        const float radius = SpecFor(type).radius + 0.3f;
        for (int attempt = 0; attempt < 14; ++attempt)
        {
            // Эхлээд хүссэн цэг, дараа нь тойрог тойргоор холдоно.
            const float ring = (float)((attempt + 7) / 8) * 3.5f;
            const float angle = (float)attempt * 0.785f;
            const float px = x + std::sin(angle) * ring, pz = z + std::cos(angle) * ring;
            if (!world.InsideBoundary(px, pz) || world.InteriorAt(px, pz) >= 0)
            {
                continue;
            }

            Vector3 resolved = { px, 0.0f, pz };
            world.ResolveHorizontal(resolved, radius, 1.6f, 0.3f); // (хашаа, бусад унаатай давхцахгүй)
            if (HorizontalDistance(resolved, { px, 0.0f, pz }) < 0.05f)
            {
                garage.Add(type, { px, world.GroundHeight(px, pz), pz }, headingDegrees * DEG2RAD);
                return;
            }
        }
    };

    // Эхлэх цэгийн дэргэд төрөл бүрээс нэг: засмал замын хоёр хажууд
    place(VehicleType::Jeep, -5.0f, -7.0f, 90.0f);
    place(VehicleType::Motorcycle, -3.0f, 6.6f, 90.0f);
    place(VehicleType::Trike, -9.5f, 6.8f, 90.0f);
    place(VehicleType::Horse, 14.0f, 7.6f, 60.0f);
    place(VehicleType::Truck, 22.0f, -14.0f, 90.0f); // ачааны машин: шатахуун түгээх газрын дэргэд
    place(VehicleType::Hatchback, -12.0f, -7.0f, 90.0f); // Yaris: жийпийн ард
    place(VehicleType::Suv, -19.0f, -7.0f, 90.0f);       // X-Trail

    // Талбараар тархсан нэмэлт унаа: суурин, шатахуун түгээх газар, цэргийн пост, малчдын отор
    place(VehicleType::Trike, 37.0f, -8.6f, -90.0f);
    place(VehicleType::Jeep, -40.0f, -13.0f, 0.0f);
    place(VehicleType::Truck, -30.0f, 16.0f, -90.0f); // заставын цэргийн машин
    place(VehicleType::Motorcycle, 23.0f, 60.0f, 180.0f);
    int index = 0;
    for (const Vector2& camp : world.GetLayout().camps)
    {
        place(VehicleType::Horse, camp.x - 1.5f, camp.y - 2.0f, 40.0f * (float)index);
        if (index % 2 == 0)
        {
            place(VehicleType::Motorcycle, camp.x - 9.0f, camp.y + 9.0f, 120.0f);
        }
        ++index;
    }

    // Улаанбаатарын хот: гудамжны захаар тархсан унаж болох машинууд (Yaris, X-Trail, жийп, ачааны машин, мотоцикл)
    // ба ШТС бүрийн дэргэд нэг машин
    if (world.CityActive())
    {
        static const VehicleType kCityVehicles[] = { VehicleType::Hatchback, VehicleType::Suv, VehicleType::Jeep, VehicleType::Hatchback,
                                                     VehicleType::Suv, VehicleType::Motorcycle, VehicleType::Truck, VehicleType::Hatchback,
                                                     VehicleType::Suv, VehicleType::Jeep };
        int k = 0;
        for (const Vector3& spot : CityVehicleSpots())
        {
            place(kCityVehicles[k++ % 10], spot.x, spot.z, spot.y * RAD2DEG);
        }
        for (const CityPoi& poi : CurrentCity().pois)
        {
            if (poi.kind == CityPoiKind::Station)
            {
                place(k++ % 2 ? VehicleType::Hatchback : VehicleType::Truck, poi.position.x + 22.0f, poi.position.y - 14.0f, 90.0f);
            }
        }
    }

    // Жижиг суурин бүрд: гудамжинд жийп, Yaris, гурван дугуйт, X-Trail ээлжлэн
    index = 0;
    static const VehicleType kHamletVehicles[] = { VehicleType::Jeep, VehicleType::Hatchback, VehicleType::Trike, VehicleType::Suv };
    for (const Vector2& hamlet : world.GetLayout().hamlets)
    {
        place(kHamletVehicles[index % 4], hamlet.x - 30.0f, hamlet.y - 5.0f, 180.0f);
        ++index;
    }
}

Game::Interaction Game::FindInteraction() const
{
    Interaction result;
    const Vector3 position = player.Position();

    int vehicle = garage.Nearest(position, kMountDistance);
    if (vehicle >= 0 && garage.At(vehicle).occupied) vehicle = -1; // нөгөө тоглогч (эсвэл түүний баг) сууж яваа
    const int door = world.NearestDoor(position, kDoorDistance);

    float vehicleDistance = 1e9f, doorDistance = 1e9f;
    if (vehicle >= 0)
    {
        vehicleDistance = HorizontalDistance(position, garage.At(vehicle).position) - SpecFor(garage.At(vehicle).type).radius;
    }
    if (door >= 0)
    {
        doorDistance = world.DoorDistance(door, position);
    }

    // Шат, цонх: эхлэх цэг нь хамгийн ойр байвал (хаалга, унаанаас ойр) түүнийг сонгоно.
    const int passage = player.IsTraversing() ? -1 : world.NearestPassage(position, kPassageDistance);
    float passageDistance = 1e9f;
    if (passage >= 0)
    {
        passageDistance = HorizontalDistance(position, world.GetPassage(passage).from);
    }

    if (passage >= 0 && passageDistance < std::min(vehicleDistance, doorDistance)) result.passage = passage;
    else if (vehicle >= 0 && vehicleDistance <= doorDistance) result.vehicle = vehicle;
    else if (door >= 0) result.door = door;
    else if (player.IsAlive() && !player.IsTraversing() && !garage.IsDriving())
    {
        // Хашааны дэргэд камерын зүг рүү: давж гарна (хаалганы дэргэд бол хаалгыг нь нээнэ)
        const Vector3 look = Vector3Subtract(camera.Get().target, camera.Get().position);
        const Vector2 dir = Vector2Normalize({ look.x, look.z });
        result.fence = world.FindVault(position, dir, 1.3f, config.playerRadius, 3.0f, result.vault);
    }
    return result;
}

void Game::UpdateSounds(float dt, float throttle)
{
    const Camera3D& view = camera.Get();
    Audio::SetListener(view.position, Vector3Subtract(view.target, view.position));

    // Сум цэнэглэж эхлэх агшин
    const bool reloading = arsenal.IsReloading();
    if (reloading && !wasReloading)
    {
        Audio::Play(Sfx::Reload, Vector3Add(player.Position(), { 0.0f, 1.3f, 0.0f }), 0.8f);
        if (horde.Nearest(player.Position(), 25.0f) >= 0) ChatterPlayer(VoiceLine::Reload); // тулааны дунд л
    }
    wasReloading = reloading;

    // Зомбинууд хааяа гинших, аварга нь архирна (ойрынх нь л)
    groanTimer -= dt;
    if (groanTimer <= 0.0f && horde.Count() > 0)
    {
        groanTimer = 1.6f + 2.2f * (float)GetRandomValue(0, 100) / 100.0f; // хааяа (ядаргаагүй)
        for (int attempt = 0; attempt < 6; ++attempt)
        {
            const int index = GetRandomValue(0, horde.Count() - 1);
            const Zombie& zombie = horde.At(index);
            if (!zombie.IsAlive() || Vector3Distance(zombie.mover.position, view.position) > (zombie.boss ? 120.0f : 30.0f)) continue;
            const Vector3 head = Vector3Add(zombie.mover.position, { 0.0f, 1.6f * zombie.scale, 0.0f });
            if (zombie.boss) Audio::Play(Sfx::BossRoar, head, 1.0f, 1.0f);
            else Audio::Play(Sfx::ZombieGroan, head, 0.55f, zombie.type == ZombieType::Brute ? 0.85f : zombie.type == ZombieType::Runner ? 1.1f : 1.0f);
            break;
        }
    }

    // Хөдөлгүүр: 0 = тоглогчийн унаа, 1 = ойрын бусад хөдөлж буй (багийн) унаа
    auto engineOf = [](const Vehicle& v) {
        if (v.wrecked || SpecFor(v.type).fuelRange <= 0.0f || v.fuel <= 0.0f) return EngineSound::Off;
        switch (v.type)
        {
        case VehicleType::Motorcycle: return EngineSound::Moto;
        case VehicleType::Trike: return EngineSound::Trike;
        case VehicleType::Truck: return EngineSound::Truck;
        default: return EngineSound::Car;
        }
    };
    auto rpmOf = [](const Vehicle& v, float pedal) {
        // Хурдлахад эргэлт жигд өснө (араа солихдоо огцом унахгүй: тасархай "дүх дүх" дуу гаргахгүй)
        const float ratio = std::clamp(std::fabs(v.speed) / SpecFor(v.type).topSpeed, 0.0f, 1.0f);
        return std::clamp(0.08f + 0.8f * std::pow(ratio, 0.7f) + 0.12f * pedal, 0.0f, 1.0f);
    };
    int nearest = -1;
    float nearestDistance = 80.0f;
    for (int i = 0; i < garage.Count(); ++i)
    {
        const Vehicle& v = garage.At(i);
        // Тоглогчийн унаа ба араас дагаж яваа багийн унаа: давхар хөдөлгүүрийн дуу гаргахгүй
        if (i == garage.Driven() || i == garage.Escort() || std::fabs(v.speed) < 0.5f || engineOf(v) == EngineSound::Off) continue;
        const float d = Vector3Distance(v.position, view.position);
        if (d < nearestDistance) { nearestDistance = d; nearest = i; }
    }
    if (garage.IsDriving())
    {
        const Vehicle& v = garage.At(garage.Driven());
        const float pedal = std::fabs(throttle);
        Audio::SetEngine(0, engineOf(v), rpmOf(v, pedal), pedal, v.position);
    }
    else
    {
        Audio::SetEngine(0, EngineSound::Off, 0.0f, 0.0f, view.position);
    }
    if (nearest >= 0)
    {
        const Vehicle& v = garage.At(nearest);
        Audio::SetEngine(1, engineOf(v), rpmOf(v, 0.6f), 0.6f, v.position);
    }
    else
    {
        Audio::SetEngine(1, EngineSound::Off, 0.0f, 0.0f, view.position);
    }
    UpdateChatter(dt);
}

namespace
{
    // Үйл явдал бүрийг дахин ярих хүртэлх хугацаа (секунд): байнга давтагдаж уйтгартай болохгүй
    float ChatterDelay(VoiceLine line)
    {
        switch (line)
        {
        case VoiceLine::Spot: return 14.0f;
        case VoiceLine::Enemy: return 12.0f;
        case VoiceLine::Fire: return 5.0f;
        case VoiceLine::Reload: return 25.0f;
        case VoiceLine::Hurt: return 4.0f;
        case VoiceLine::Kill: return 12.0f;
        case VoiceLine::Exploder: return 15.0f;
        case VoiceLine::Boss: return 60.0f;
        case VoiceLine::Swarm: return 35.0f;
        case VoiceLine::Ammo: return 45.0f;
        case VoiceLine::Grenade: return 5.0f;
        case VoiceLine::Vehicle: return 30.0f;
        case VoiceLine::Crate: return 20.0f;
        case VoiceLine::Lost: return 6.0f; // олон цэрэг зэрэг үхвэл нэг л удаа
        case VoiceLine::Idle: return 120.0f;
        default: return 0.0f; // унах, босох, нөхрөө алдах, тушаал: үргэлж
        }
    }
}

bool Game::Chatter(VoiceLine line, int speaker, Vector3 position, float pitch, bool urgent)
{
    float& wait = chatterCooldown[(int)line];
    if (wait > 0.0f || (!urgent && chatterGap > 0.0f)) return false;
    if (!Audio::Say(speaker, line, Vector3Add(position, { 0.0f, 1.6f, 0.0f }), pitch, urgent)) return false;
    wait = ChatterDelay(line);
    chatterGap = 1.5f;
    return true;
}

bool Game::ChatterPlayer(VoiceLine line, bool urgent)
{
    return Chatter(line, std::clamp(activeMember, 0, 2), player.Position(), 1.0f, urgent);
}

bool Game::ChatterNearest(VoiceLine line, Vector3 position, bool urgent)
{
    const ChatterSubject* best = nullptr;
    float bestDistance = 60.0f;
    for (const ChatterSubject& s : chatterSubjects)
    {
        if (!s.alive || s.downed) continue;
        const float d = Vector3Distance(s.position, position);
        if (d < bestDistance)
        {
            bestDistance = d;
            best = &s;
        }
    }
    if (best) return Chatter(line, best->speaker, best->position, best->pitch, urgent);
    return player.IsAlive() && !player.IsDowned() && ChatterPlayer(line, urgent);
}

void Game::UpdateChatter(float dt)
{
    for (float& wait : chatterCooldown) wait = std::max(0.0f, wait - dt);
    chatterGap = std::max(0.0f, chatterGap - dt);

    // Өөрийн багийнхан (тулаанд эсрэг багийнхан ярихгүй). Хост: AI цэргүүд, зочин: хостын илгээсэн төлөв.
    // Дуу хоолой: багийн гишүүн бүр өөрийнх, элсүүлсэн цэргүүд ээлжилж (өндөр нь бага зэрэг өөр).
    chatterSubjects.clear();
    const auto voiceOf = [](int member, int index, float& pitch) {
        pitch = member >= 0 ? 1.0f : 0.93f + 0.035f * (float)(index % 4);
        return member >= 0 ? member % 3 : index % 3;
    };
    if (coopGuest)
    {
        for (size_t i = 0; i < guestView.soldiers.size(); ++i)
        {
            const Net::SoldierState& s = guestView.soldiers[i];
            if (battleActive && !s.guest) continue;
            ChatterSubject subject = { s.position, 0, 1.0f, s.alive != 0, s.alive == 2, s.aiming != 0, s.health, 0 };
            subject.speaker = voiceOf(s.member, (int)i, subject.pitch);
            if (!s.guest) subject.pitch *= 0.96f; // хостын багийнхан
            chatterSubjects.push_back(subject);
        }
    }
    else
    {
        const std::vector<Soldier>& all = squad.All();
        for (size_t i = 0; i < all.size(); ++i)
        {
            const Soldier& s = all[i];
            if (battleActive && s.guestTeam) continue;
            ChatterSubject subject = { s.mover.position, 0, 1.0f, s.alive, s.downed, s.aimHold > 0.0f, s.health, s.kills };
            subject.speaker = voiceOf(s.member, (int)i, subject.pitch);
            if (s.guestTeam) subject.pitch *= 0.96f; // зочны багийнхан
            chatterSubjects.push_back(subject);
        }
    }

    // Өөрчлөлтүүд (баг өөрчлөгдсөн кадрт харьцуулахгүй: индексүүд шилжсэн)
    bool fighting = false;
    if (chatterMemo.size() != chatterSubjects.size())
    {
        chatterMemo.assign(chatterSubjects.size(), {});
        for (size_t i = 0; i < chatterSubjects.size(); ++i)
        {
            const ChatterSubject& s = chatterSubjects[i];
            chatterMemo[i] = { s.alive, s.downed, false, s.health, s.kills, 11.0f }; // анх бай олоход "зомби!"
        }
    }
    for (size_t i = 0; i < chatterSubjects.size(); ++i)
    {
        const ChatterSubject& s = chatterSubjects[i];
        ChatterMemo& memo = chatterMemo[i];
        fighting = fighting || (s.alive && s.aiming);
        if (memo.alive && !s.alive)
        {
            ChatterNearest(VoiceLine::Lost, s.position, true); // нөхрөө алдлаа
        }
        else if (s.alive && s.downed && !memo.downed)
        {
            Chatter(VoiceLine::Downed, s.speaker, s.position, s.pitch, true);
        }
        else if (s.alive && !s.downed && memo.downed)
        {
            Chatter(VoiceLine::Revived, s.speaker, s.position, s.pitch, true);
        }
        else if (s.alive && !s.downed)
        {
            if (s.health < memo.health - 4.0f) Chatter(VoiceLine::Hurt, s.speaker, s.position, s.pitch);
            // Удаан тайван байгаад бай олсон: хэсэг хугацаанд хэлэхийг оролдоно (өөр хүн ярьж байж магадгүй).
            // Ойрхон зомби байхгүй бол хүн (тулаан).
            if (s.aiming && !memo.aiming && memo.calm > 10.0f) memo.spot = 2.5f;
            if (memo.spot > 0.0f)
            {
                memo.spot -= dt;
                const bool human = battleActive && horde.Nearest(s.position, 45.0f) < 0;
                if (Chatter(human ? VoiceLine::Enemy : VoiceLine::Spot, s.speaker, s.position, s.pitch)) memo.spot = 0.0f;
            }
            if (s.kills != memo.kills && GetRandomValue(0, 99) < 40) Chatter(VoiceLine::Kill, s.speaker, s.position, s.pitch);
        }
        memo.calm = s.aiming ? 0.0f : memo.calm + dt;
        memo.alive = s.alive;
        memo.downed = s.downed;
        memo.aiming = s.aiming;
        memo.health = s.health;
        memo.kills = s.kills;
    }

    // Тоглогчийн дүр: унах, босох, нөөц сум дуусах
    const bool playerDowned = player.IsDowned();
    if (playerDowned && !chatterPlayerDowned) ChatterPlayer(VoiceLine::Downed, true);
    if (!playerDowned && chatterPlayerDowned && player.IsAlive()) ChatterPlayer(VoiceLine::Revived, true);
    chatterPlayerDowned = playerDowned;
    if (!player.IsAlive() || playerDowned) return;
    if (ammoReserve < 100 && !chatterLowAmmo) ChatterPlayer(VoiceLine::Ammo);
    chatterLowAmmo = ammoReserve < 100;
    // Тоглогч унаанд суувал багийнхан нь "явцгаая"
    const bool driving = garage.IsDriving();
    if (driving && !chatterDriving && !chatterSubjects.empty()) ChatterNearest(VoiceLine::Vehicle, player.Position());
    chatterDriving = driving;

    // Ойрын зомбиуд (хагас секунд тутамд): тэсрэгч, аварга, бүслэлт, удаан тайван
    chatterScan -= dt;
    if (chatterScan > 0.0f) return;
    chatterScan = 0.5f;
    const Vector3 at = player.Position();
    int close = 0;
    int exploder = -1, boss = -1;
    for (int i = 0; i < horde.Count(); ++i)
    {
        const Zombie& z = horde.At(i);
        if (!z.IsAlive()) continue;
        const float d = Vector3Distance(z.mover.position, at);
        if (d < 18.0f) ++close;
        if (z.exploder && d < 14.0f) exploder = i;
        if (z.boss && d < 70.0f) boss = i;
    }
    if (exploder >= 0) ChatterNearest(VoiceLine::Exploder, horde.At(exploder).mover.position, true);
    else if (boss >= 0) ChatterNearest(VoiceLine::Boss, horde.At(boss).mover.position);
    else if (close >= 12) ChatterNearest(VoiceLine::Swarm, at);
    chatterIdle = fighting || close > 0 || driving ? 0.0f : chatterIdle + 0.5f;
    if (chatterIdle > 60.0f && ChatterNearest(VoiceLine::Idle, at)) chatterIdle = 0.0f;
}

void Game::UpdateVehicles(float dt)
{
    const bool autoDrive = options.screenshotPath && options.ride > 0; // туршилтын горим

    // --- F: унаанд суух / буух, хаалга нээх / хаах ---
    ++missionFrame;
    const bool testUse = options.screenshotPath && options.useFrame > 0 && missionFrame == options.useFrame;
    if (InKeyPressed(KEY_F) || testUse)
    {
        if (garage.IsDriving())
        {
            // Унааны зүүн талд бууна.
            const Vehicle& vehicle = garage.At(garage.Driven());
            const float side = SpecFor(vehicle.type).radius + config.playerRadius + 0.3f;
            Vector3 exit = HumanoidLocalToWorld(vehicle.position, vehicle.heading, { side, 0.0f, 0.0f });
            world.ResolveHorizontal(exit, config.playerRadius, config.playerHeight, config.stepHeight);
            exit.y = world.GroundHeight(exit.x, exit.z);
            garage.Dismount();
            player.StopRiding(exit);
        }
        else if (gunnerVehicle >= 0)
        {
            LeaveGunnerSeat();
        }
        else
        {
            // Унаа, хаалга хоёулаа ойр байвал ойрынх нь сонгогдоно.
            const Interaction target = FindInteraction();
            if (target.vehicle >= 0)
            {
                garage.ClearRemote(target.vehicle); // одооноос өөрөө жолоодно (сүлжээний байрлалыг дагахгүй)
                garage.Mount(target.vehicle);
            }
            else if (target.door >= 0)
            {
                world.SetDoorOpen(target.door, !world.IsDoorOpen(target.door));
                if (coopGuest)
                {
                    net.SendDoor(target.door, world.IsDoorOpen(target.door));
                    pendingDoor = target.door;
                    pendingDoorTimer = 1.0f;
                }
            }
            else if (target.passage >= 0)
            {
                player.StartTraverse(world.GetPassage(target.passage));
            }
            else if (target.fence)
            {
                player.StartTraverse(target.vault);
            }
        }
    }

    // --- Жолоодлого: W/S хий ба тоормос, A/D эргэлт, Space гар тоормос ---
    float throttle = 0.0f, steer = 0.0f;
    bool handbrake = false;
    if (garage.IsDriving())
    {
        const Vector2 axis = InMoveAxis(); // гар (WASD) эсвэл мэдрэгчтэй дэлгэцийн хүрд
        throttle = axis.y;
        steer = axis.x;
        handbrake = InKeyDown(KEY_SPACE);
        if (autoDrive) throttle = 1.0f;
        if (autoDrive && options.driftFrame != 0 && missionFrame >= std::abs(options.driftFrame))
        {
            handbrake = options.driftFrame > 0; // сөрөг: тоормосгүй, зөвхөн эргэнэ (шарвах ёсгүй)
            steer = 1.0f;
        }
    }
    // Хашаа: хурдтай дайрсан машин хашааны хэсгийг нураагаад гарна (мотоцикл, морь нураахгүй). Нөгөө тоглогчийн
    // унааг ч хурдаар нь тооцно: хоёр талд ижилхэн нурна.
    for (int i = 0; i < garage.Count(); ++i)
    {
        const Vehicle& vehicle = garage.At(i);
        const VehicleSpec& spec = SpecFor(vehicle.type);
        const float speed = std::fabs(vehicle.speed);
        if (spec.radius < 1.0f || speed < 2.5f) continue;
        const float sign = vehicle.speed > 0.0f ? 1.0f : -1.0f;
        const Vector2 dir = { std::sin(vehicle.heading) * sign, std::cos(vehicle.heading) * sign };
        const int hits = world.BreakFences(vehicle.position, spec.radius * 0.6f + 0.45f + speed * dt * 2.0f, dir, speed);
        if (hits <= 0) continue;
        garage.Slow(i, std::pow(0.85f, (float)hits));
        Audio::Play(Sfx::Crash, vehicle.position, std::min(1.0f, speed / 10.0f), 0.8f);
    }
    garage.Update(dt, world, horde, throttle, steer, handbrake, garage.IsDriving() && (InSprint() || autoDrive));
    for (int i = 0; i < garage.Count(); ++i)
    {
        const Vehicle& vehicle = garage.At(i);
        if (vehicle.remote || std::fabs(vehicle.speed) < 0.3f) continue; // нөгөө тоглогчийнх: өөрөө нээж илгээнэ
        const int door = world.NearestDoor(vehicle.position, SpecFor(vehicle.type).radius + 2.6f);
        if (door < 0 || !world.IsGate(door) || world.IsDoorOpen(door)) continue;
        const Vector3 center = world.DoorCenter(door);
        const float ahead = (std::sin(vehicle.heading) * (center.x - vehicle.position.x) +
                             std::cos(vehicle.heading) * (center.z - vehicle.position.z)) * (vehicle.speed > 0.0f ? 1.0f : -1.0f);
        if (ahead <= 0.0f) continue; // хаалга руу явж байгаа үед л
        world.SetDoorOpen(door, true);
        if (coopGuest)
        {
            net.SendDoor(door, true);
            pendingDoor = door;
            pendingDoorTimer = 1.0f;
        }
    }
    // Хост: зочны унаа мөргөж устгасан зомбины зоосыг зочинд илгээнэ (зочин "+N" ба зоосоо авна).
    remoteRewardBuffer.clear();
    horde.TakeRemoteRewards(remoteRewardBuffer);
    for (const ZombieHorde::KillReward& reward : remoteRewardBuffer)
    {
        if (remoteActive && net.Connected()) net.SendKillReward(reward.coins, reward.position);
    }
    UpdateSounds(dt, throttle);
    UpdateVehicleCombat(dt);

    // Дэлбэрэлт (гранат, торх, пуужин) ойрын унааг гэмтээнэ
    {
        std::vector<Vector3> blasts;
        particles.TakeExplosions(blasts);
        for (const Vector3& blast : blasts)
        {
            const float near = Vector3Distance(blast, camera.Get().position);
            if (near < 45.0f) camera.Shake(0.9f * (1.0f - near / 45.0f));
            for (int i = 0; i < garage.Count(); ++i)
            {
                const float distance = Vector3Distance(blast, garage.At(i).position);
                if (distance < 8.0f) garage.Damage(i, 70.0f * (1.0f - distance / 8.0f));
            }
        }
    }

    // Унаж яваа хэрэгсэл эвдэрвэл (морь үхвэл) буулгана; бензин дуусвал сануулна.
    if (garage.IsDriving())
    {
        const Vehicle& vehicle = garage.At(garage.Driven());
        if (vehicle.wrecked)
        {
            const float side = SpecFor(vehicle.type).radius + config.playerRadius + 0.3f;
            Vector3 exit = HumanoidLocalToWorld(vehicle.position, vehicle.heading, { side, 0.0f, 0.0f });
            world.ResolveHorizontal(exit, config.playerRadius, config.playerHeight, config.stepHeight);
            exit.y = world.GroundHeight(exit.x, exit.z);
            garage.Dismount();
            player.StopRiding(exit);
            SetNotice(vehicle.type == VehicleType::Horse ? "Морь үхлээ!" : std::string(SpecFor(vehicle.type).name) + " эвдэрлээ!", 3.0f);
        }
        else if (SpecFor(vehicle.type).fuelRange > 0.0f && vehicle.fuel <= 0.0f && !fuelWarned)
        {
            fuelWarned = true;
            SetNotice("Бензин дууслаа! Шатахуун түгээх газарт очиж бензин хийнэ үү", 4.0f);
        }
        else if (vehicle.fuel > 0.02f)
        {
            fuelWarned = false;
        }
    }

    // Шатахуун түгээх газар: түгээгүүрийн дэргэд зогсоход бензин хийж, засварлана (зоосоор).
    if (garage.IsDriving())
    {
        const int index = garage.Driven();
        const Vehicle& vehicle = garage.At(index);
        const float dx = vehicle.position.x - 30.0f, dz = vehicle.position.z + 11.5f; // түгээгүүрүүд (Structures: GasStation)
        if (dx * dx + dz * dz < 9.0f * 9.0f && std::fabs(vehicle.speed) < 1.0f && progress.coins > 0)
        {
            const float fuel = garage.Refuel(index, 0.12f * dt);              // ~8 секундэд дүүрнэ
            const float fixed = garage.Repair(index, SpecFor(vehicle.type).maxHealth * 0.08f * dt);
            fuelCoinDebt += fuel * 60.0f + fixed * 0.5f;                      // дүүрэн сав 60, засвар 1 зоос / 2 нэгж
            if (fuelCoinDebt >= 1.0f)
            {
                const int pay = std::min(progress.coins, (int)fuelCoinDebt);
                progress.coins -= pay;
                fuelCoinDebt -= (float)pay;
            }
        }
    }

    // Шарвалт: хажуу тийш гулсаж байвал арын дугуйнаас утаа (засмал) эсвэл шороо босож, дугуй чахирна
    driftTimer += dt;
    if (driftTimer >= 0.05f)
    {
        driftTimer = 0.0f;
        for (int i = 0; i < garage.Count(); ++i)
        {
            const Vehicle& vehicle = garage.At(i);
            const float slide = std::fabs(vehicle.slip);
            if (slide < 2.2f || vehicle.type == VehicleType::Horse) continue;
            if (Vector3Distance(vehicle.position, camera.Get().position) > 120.0f) continue;
            const bool asphalt = world.SurfaceAt(vehicle.position.x, vehicle.position.z) == Surface::Asphalt;
            const float back = vehicle.type == VehicleType::Truck ? -2.4f : -1.2f;
            for (float side : { -0.85f, 0.85f })
            {
                const Vector3 wheel = HumanoidLocalToWorld(vehicle.position, vehicle.heading, { side, 0.25f, back });
                particles.Kickup(wheel, { 0.0f, 0.3f, 0.0f }, asphalt ? Color{ 205, 205, 200, 255 } : Color{ 176, 156, 120, 255 });
            }
            screechTimer -= 0.05f;
            if (asphalt && screechTimer <= 0.0f)
            {
                screechTimer = 0.3f;
                Audio::Play(Sfx::Screech, vehicle.position, std::min(1.0f, slide / 8.0f), 0.9f + 0.02f * slide);
            }
        }
    }

    // Гэмтсэн унаанаас утаа, эвдэрснээс хар утаа ба гал
    smokeTimer += dt;
    if (smokeTimer >= 0.12f)
    {
        smokeTimer = 0.0f;
        for (int i = 0; i < garage.Count(); ++i)
        {
            const Vehicle& vehicle = garage.At(i);
            if (vehicle.type == VehicleType::Horse) continue;
            const float condition = vehicle.health / SpecFor(vehicle.type).maxHealth;
            if (condition > 0.35f || Vector3Distance(vehicle.position, camera.Get().position) > 150.0f) continue;
            const Vector3 engine = HumanoidLocalToWorld(vehicle.position, vehicle.heading,
                                                        { 0.0f, vehicle.type == VehicleType::Truck ? 2.0f : 1.2f,
                                                          vehicle.type == VehicleType::Truck ? 2.6f : 1.0f });
            const Vector2 wind = environment.Wind();
            const unsigned char grey = vehicle.wrecked ? 40 : (unsigned char)(70 + condition * 200.0f);
            particles.Kickup(engine, { wind.x * 0.8f, 1.6f, wind.y * 0.8f }, { grey, grey, grey, 255 });
            if (vehicle.wrecked && GetRandomValue(0, 2) == 0)
            {
                particles.Kickup(engine, { 0.0f, 2.2f, 0.0f }, { 240, 120, 40, 255 }); // гал
            }
        }
    }

    // Шороон зам, хээр талаар хурдтай явахад унаа, морины ард тоос босно (бороо, цастай үед үгүй).
    dustTimer += dt;
    if (dustTimer >= 0.07f)
    {
        dustTimer = 0.0f;
        const WeatherKind weatherNow = environment.Weather();
        if (weatherNow != WeatherKind::Rain && weatherNow != WeatherKind::Snow)
        {
            for (int i = 0; i < garage.Count(); ++i)
            {
                const Vehicle& vehicle = garage.At(i);
                const float speed = std::fabs(vehicle.speed);
                if (speed < 4.0f || Vector3Distance(vehicle.position, camera.Get().position) > 120.0f) continue;
                if (world.SurfaceAt(vehicle.position.x, vehicle.position.z) == Surface::Asphalt) continue;
                const float back = vehicle.speed > 0.0f ? -1.0f : 1.0f;
                const Vector3 at = { vehicle.position.x + std::sin(vehicle.heading) * back * 1.4f,
                                     vehicle.position.y + 0.3f,
                                     vehicle.position.z + std::cos(vehicle.heading) * back * 1.4f };
                const Vector2 wind = environment.Wind();
                const Color dust = vehicle.type == VehicleType::Horse ? Color{ 170, 152, 120, 255 } : Color{ 182, 162, 128, 255 };
                particles.Kickup(at, { wind.x * 0.6f, 0.0f, wind.y * 0.6f }, dust);
            }
        }
    }

    if (garage.IsDriving())
    {
        const Vehicle& vehicle = garage.At(garage.Driven());
        const VehicleSpec& spec = SpecFor(vehicle.type);
        const RideStyle style = spec.enclosed ? RideStyle::Hidden
                              : vehicle.type == VehicleType::Horse ? RideStyle::Saddle : RideStyle::Bike;
        bool reins = false;
        const Vector3 rein = garage.ReinWorld(garage.Driven(), reins);
        player.Ride(dt, vehicle.position, garage.SeatPosition(), vehicle.heading, style, rein, reins);
    }
    else if (gunnerVehicle >= 0)
    {
        const Vehicle& vehicle = garage.At(gunnerVehicle);
        if (vehicle.wrecked)
        {
            LeaveGunnerSeat();
            SetNotice(vehicle.type == VehicleType::Horse ? "Морь үхлээ!" : std::string(SpecFor(vehicle.type).name) + " эвдэрлээ!", 3.0f);
        }
        else
        {
            const RideStyle style = SpecFor(vehicle.type).enclosed ? RideStyle::Hidden
                                  : vehicle.type == VehicleType::Horse ? RideStyle::Saddle : RideStyle::Bike;
            player.Ride(dt, vehicle.position, garage.SeatWorld(gunnerVehicle, true), vehicle.heading, style);
        }
    }
    else
    {
        player.Update(dt, config, world, camera.Yaw());

        // Зогсож буй унаа дундуур гарахгүй.
        Vector3 position = player.Position();
        garage.PushOut(position, config.playerRadius);
        player.SetHorizontalPosition(position.x, position.z);
    }
}

void Game::UpdateTeamRiding(float dt)
{
    std::vector<Soldier>& all = squad.All();

    // Үхсэн гишүүн унаанаас унана.
    for (Soldier& soldier : all)
    {
        if (!soldier.alive && (soldier.riding >= 0 || soldier.boarding))
        {
            soldier.riding = -1;
            soldier.boarding = false;
            soldier.seatStyle = 0;
        }
    }

    // Тоглогч буудагчийн суудалд: AI жолооч жолоодно, бусад гишүүд бууж явганаар дагана (дагах унаа ганцхан).
    if (gunnerVehicle >= 0)
    {
        const bool driverOk = carDriver >= 0 && carDriver < (int)all.size() && all[(size_t)carDriver].alive &&
                              !garage.At(gunnerVehicle).wrecked;
        if (!driverOk)
        {
            LeaveGunnerSeat();
            SetNotice("Жолооч унаснаас унаанаас буулаа", 2.5f);
            return;
        }
        int slot = 0;
        for (size_t i = 0; i < all.size(); ++i)
        {
            Soldier& soldier = all[i];
            if ((int)i == carDriver || soldier.riding < 0) continue;
            const Vehicle& other = garage.At(soldier.riding);
            const float side = SpecFor(other.type).radius + 0.9f;
            Vector3 exit = HumanoidLocalToWorld(other.position, other.heading, { -side, 0.0f, slot++ == 0 ? 0.9f : -0.9f });
            world.ResolveHorizontal(exit, config.playerRadius, config.playerHeight, 0.0f);
            exit.y = world.GroundHeight(exit.x, exit.z);
            soldier.mover.position = exit;
            soldier.riding = -1;
            soldier.boarding = false;
            soldier.seatStyle = 0;
        }
        teamVehicle = -1;
        const Vehicle& vehicle = garage.At(gunnerVehicle);
        Soldier& driver = all[(size_t)carDriver];
        driver.riding = gunnerVehicle;
        driver.boarding = false;
        driver.mover.position = vehicle.position;
        driver.mover.facing = vehicle.heading;
        driver.seatHeading = vehicle.heading;
        driver.seat = garage.SeatWorld(gunnerVehicle, false);
        bool reins = false;
        driver.rein = garage.ReinWorld(gunnerVehicle, reins);
        driver.hasRein = reins;
        driver.seatStyle = SpecFor(vehicle.type).enclosed ? 3 : vehicle.type == VehicleType::Horse ? 2 : 1;
        DriveForGunner(dt);
        return;
    }

    // Тоглогч жолоодож байна: хамгийн ойрын нэг гишүүн тоглогчийн унаанд буудагчаар сууна.
    if (garage.IsDriving() && !coopGuest)
    {
        const int driven = garage.Driven();
        if (carGunner >= 0 && (carGunner >= (int)all.size() || !all[(size_t)carGunner].alive ||
                               all[(size_t)carGunner].riding != driven))
        {
            carGunner = -1;
        }
        if (carGunner < 0)
        {
            float best = 15.0f;
            for (size_t i = 0; i < all.size(); ++i)
            {
                const Soldier& soldier = all[i];
                if (!soldier.alive || soldier.member < 0 || soldier.guestTeam || soldier.holding || soldier.riding >= 0) continue;
                const float d = HorizontalDistance(soldier.mover.position, garage.At(driven).position);
                if (d < best)
                {
                    best = d;
                    carGunner = (int)i;
                }
            }
            if (carGunner >= 0)
            {
                SetNotice(std::string(TeamMemberName(all[(size_t)carGunner].member)) + " буудагчаар суулаа. Жолоо солих: Tab", 3.0f);
            }
        }
        if (carGunner >= 0)
        {
            const Vehicle& vehicle = garage.At(driven);
            Soldier& gunner = all[(size_t)carGunner];
            gunner.riding = driven;
            gunner.boarding = false;
            gunner.mover.position = vehicle.position;
            gunner.mover.facing = vehicle.heading;
            gunner.seatHeading = vehicle.heading;
            gunner.seat = garage.SeatWorld(driven, true);
            gunner.hasRein = false;
            gunner.seatStyle = SpecFor(vehicle.type).enclosed ? 5
                             : vehicle.type == VehicleType::Horse ? 2
                             : vehicle.type == VehicleType::Trike ? 4 : 1;
        }
    }
    else
    {
        carGunner = -1;
    }

    const bool teamWrecked = teamVehicle >= 0 && garage.At(teamVehicle).wrecked;
    if (!garage.IsDriving() || teamWrecked)
    {
        // Тоглогч буусан (эсвэл багийн унаа эвдэрсэн): гишүүд хэрэгслийнхээ хажууд бууна.
        int slot = 0;
        for (Soldier& soldier : all)
        {
            if (soldier.riding >= 0)
            {
                const Vehicle& vehicle = garage.At(soldier.riding);
                const float side = SpecFor(vehicle.type).radius + 0.9f;
                Vector3 exit = HumanoidLocalToWorld(vehicle.position, vehicle.heading,
                                                    { -side, 0.0f, slot == 0 ? 0.9f : -0.9f });
                world.ResolveHorizontal(exit, config.playerRadius, config.playerHeight, 0.0f);
                exit.y = world.GroundHeight(exit.x, exit.z);
                soldier.mover.position = exit;
                ++slot;
            }
            soldier.riding = -1;
            soldier.boarding = false;
            soldier.seatStyle = 0;
        }
        teamVehicle = -1;
        garage.SetEscort(-1);
        return;
    }

    // --- Тоглогч унаа унаж яваа ---
    if (teamVehicle < 0)
    {
        // Амьд гишүүдэд хамгийн ойр, сул хэрэгслийг сонгоно (хоёулаа нэг хэрэгсэлд сууна).
        const Soldier* reference = nullptr;
        for (const Soldier& soldier : all)
        {
            if (soldier.alive && soldier.member >= 0 && !soldier.guestTeam && !soldier.holding && !reference &&
                &soldier - all.data() != carGunner)
            {
                reference = &soldier;
            }
        }
        if (!reference)
        {
            return;
        }

        float best = kTeamVehicleRange;
        for (int i = 0; i < garage.Count(); ++i)
        {
            const float distance = HorizontalDistance(reference->mover.position, garage.At(i).position);
            if (i != garage.Driven() && !garage.At(i).occupied && !garage.At(i).wrecked && distance < best)
            {
                best = distance;
                teamVehicle = i;
            }
        }
        if (teamVehicle < 0)
        {
            return; // ойролцоо хэрэгсэл алга: урьдын адил явганаар дагана
        }
        teamBoardTimer = 0.0f;
        escortStuck = 0.0f;
    }

    const Vehicle& vehicle = garage.At(teamVehicle);
    const VehicleSpec& spec = SpecFor(vehicle.type);
    teamBoardTimer += dt;

    int aboard = 0, waiting = 0;
    for (Soldier& soldier : all)
    {
        if (!soldier.alive || soldier.member < 0 || soldier.holding || soldier.guestTeam || &soldier - all.data() == carGunner)
        {
            continue; // "энд бай" тушаалтай гишүүн байрандаа үлдэнэ; зочны багийнхан зочноо дагана; буудагч тоглогчтой
        }
        if (soldier.riding < 0)
        {
            soldier.boardPoint = vehicle.position;
            const bool reached = HorizontalDistance(soldier.mover.position, vehicle.position) < spec.radius + 1.3f;
            if (reached || teamBoardTimer > kTeamBoardTime)
            {
                soldier.riding = teamVehicle;
                soldier.boarding = false;
            }
            else
            {
                soldier.boarding = true;
                ++waiting;
            }
        }
        if (soldier.riding >= 0)
        {
            // Эхний гишүүн жолооч, дараагийнх нь зорчигч: сундлах, жолоочийн эсрэг талын суудал, тэвш.
            const bool passenger = aboard > 0;
            soldier.mover.position = vehicle.position;
            soldier.mover.facing = vehicle.heading;
            soldier.seatHeading = vehicle.heading;
            soldier.seat = garage.SeatWorld(teamVehicle, passenger);
            // Жолооч л жолоо барина (сундлагч, тэвшинд суугч барихгүй).
            bool reins = false;
            soldier.rein = garage.ReinWorld(teamVehicle, reins);
            soldier.hasRein = reins && !passenger;
            soldier.seatStyle = spec.enclosed ? 3
                              : vehicle.type == VehicleType::Horse ? 2
                              : vehicle.type == VehicleType::Trike && passenger ? 4 : 1;
            ++aboard;
        }
    }

    if (aboard == 0 && waiting == 0)
    {
        teamVehicle = -1; // гишүүд бүгд үхсэн
        garage.SetEscort(-1);
        return;
    }
    if (waiting > 0)
    {
        garage.SetEscort(-1); // бүгд суутал хүлээнэ
        return;
    }

    // --- Жолоодлого: тоглогчийн хэрэгслийг зай барин дагана ---
    garage.SetEscort(teamVehicle);
    const Vehicle& lead = garage.At(garage.Driven());
    const float dx = lead.position.x - vehicle.position.x, dz = lead.position.z - vehicle.position.z;
    const float distance = std::sqrt(dx * dx + dz * dz);
    const float gap = SpecFor(lead.type).radius + spec.radius + 5.0f;
    const float turn = std::remainder(std::atan2(dx, dz) - vehicle.heading, 2.0f * PI);

    float throttle = 0.0f;
    if (distance > gap + 6.0f) throttle = 1.0f;
    else if (distance > gap) throttle = lead.speed > vehicle.speed ? 0.6f : 0.0f;
    else if (vehicle.speed > 1.0f) throttle = -1.0f; // хэт ойртсон: тоормослоно
    if (std::fabs(turn) > 1.2f && vehicle.speed > 5.0f)
    {
        throttle = 0.0f; // огцом эргэхийн өмнө хурдаа хасна
    }
    garage.SetEscortInput(throttle, std::clamp(-turn * 2.5f, -1.0f, 1.0f));

    // Гацсан (хашаа, байшин мөргөсөн) эсвэл хэт хоцорсон бол тоглогчийн хэрэгслийн ард шилжинэ:
    // гишүүд зам хайдаггүй тул энэ нь баталгаа болно.
    escortStuck = distance > gap + 6.0f && std::fabs(vehicle.speed) < 1.0f ? escortStuck + dt : 0.0f;
    if (escortStuck > 3.0f || distance > 110.0f)
    {
        escortStuck = 0.0f;
        Vector3 place = HumanoidLocalToWorld(lead.position, lead.heading, { 0.0f, 0.0f, -(gap + 2.0f) });
        world.ResolveHorizontal(place, spec.radius, 1.6f, 0.3f);
        place.y = world.GroundHeight(place.x, place.z);
        garage.Place(teamVehicle, place, lead.heading);
    }
}

void Game::AimAssist(float dt)
{
    const Camera3D& view = camera.Get();
    const Vector3 forward = Vector3Normalize(Vector3Subtract(view.target, view.position));
    const float cone = std::cos(26.0f * DEG2RAD); // загалмайгаас ийм өнцгийн доторх зомбийг л сонгоно

    // Загалмайд хамгийн ойр (өнцгөөр), харагдаж буй зомби; ойрынх нь бага зэрэг давуу.
    int best = -1;
    float bestScore = 1e9f;
    Vector3 bestDirection = forward;
    for (int i = 0; i < horde.Count(); ++i)
    {
        const Zombie& zombie = horde.At(i);
        if (!zombie.IsAlive())
        {
            continue;
        }
        const Vector3 toZombie = Vector3Subtract(zombie.Chest(), view.position);
        const float distance = Vector3Length(toZombie);
        if (distance < 2.0f || distance > 75.0f)
        {
            continue;
        }
        const Vector3 direction = Vector3Scale(toZombie, 1.0f / distance);
        const float alignment = Vector3DotProduct(forward, direction);
        if (alignment < cone)
        {
            continue;
        }
        const float score = std::acos(std::min(1.0f, alignment)) + distance * 0.003f;
        if (score < bestScore && world.RayDistance({ view.position, direction }, distance) >= distance - 0.6f)
        {
            best = i;
            bestScore = score;
            bestDirection = direction;
        }
    }
    if (best < 0)
    {
        return;
    }

    // Камерын эргэлт: yaw нь +Z-ээс, pitch эерэг үед доош харна.
    const float wantedYaw = std::atan2(bestDirection.x, bestDirection.z);
    const float wantedPitch = -std::asin(std::clamp(bestDirection.y, -1.0f, 1.0f));
    const float pull = 1.0f - std::exp(-7.0f * dt);
    camera.AddView(std::remainder(wantedYaw - camera.Yaw(), 2.0f * PI) * pull, (wantedPitch - camera.Pitch()) * pull);
}

void Game::WarmUpRendering()
{
    const double started = GetTime();
    RenderTexture2D target = LoadRenderTexture(256, 256);
    if (target.id == 0) return;
    Camera3D view = {};
    view.position = { 0.0f, 3.0f, -9.0f };
    view.target = { 0.0f, 1.0f, 0.0f };
    view.up = { 0.0f, 1.0f, 0.0f };
    view.fovy = 50.0f;
    view.projection = CAMERA_PERSPECTIVE;
    BeginTextureMode(target);
    ClearBackground(BLACK);
    BeginMode3D(view);
    environment.Begin(view);
    // Бүх унаа (байрлалаас үл хамааран), морь, газар
    garage.Draw(view.position, 1e9f, &environment.PropMaterial());
    world.Draw(view, environment.GetMaterial(), environment.TerrainMaterial(), environment.DetailMaterial(), config.detailDistance,
               environment.ShadowsEnabled());
    // Дүр бүр: ойрын ба холын (хөнгөн) хувилбар, барих зэвсэг бүр
    const HeldItem items[] = { HeldItem::Rifle, HeldItem::Sniper, HeldItem::Pistol, HeldItem::Shotgun, HeldItem::Knife,
                               HeldItem::Grenade, HeldItem::Launcher };
    for (int model = 0; model < kCharacterCount; ++model)
    {
        HumanoidLook look = TeamMemberLook(0, 0);
        look.model = model;
        for (const bool light : { false, true })
        {
            look.lowDetail = light;
            look.held = items[model % 7];
            HumanoidPose pose = MakeSoldierPose(0.0f, 0.0f, true, 0.0f, 0.0f, look.held);
            DrawHumanoid({ 0.0f, 0.0f, 0.0f }, 0.0f, 1.0f, pose, look, true);
        }
    }
    for (const HeldItem item : items)
    {
        HumanoidLook look = TeamMemberLook(0, 0);
        look.model = -1; // кодоор үүсгэсэн дүрс ба зэвсгийн загварууд
        look.held = item;
        HumanoidPose pose = MakeSoldierPose(0.0f, 0.0f, true, 0.0f, 0.0f, item);
        DrawHumanoid({ 0.0f, 0.0f, 0.0f }, 0.0f, 1.0f, pose, look, true);
    }
    environment.End();
    EndMode3D();
    EndTextureMode();
    rlDrawRenderBatchActive();
    UnloadRenderTexture(target);
    TraceLog(LOG_INFO, "MISSION: rendering warm-up %.0f ms", (GetTime() - started) * 1000.0);
}

void Game::UpdateRenderScale(float dt)
{
    if (options.renderScale > 0.0f)
    {
        renderScale = std::clamp(options.renderScale, 0.5f, 1.0f); // туршилт: тогтмол
        return;
    }
    if (options.screenshotPath) return;
    scaleTime += std::min(dt, 0.25f);
    ++scaleFrames;
    if (scaleTime < 1.5f) return;
    const float fps = (float)scaleFrames / scaleTime;
    scaleTime = 0.0f;
    scaleFrames = 0;
    // Зорилт: утсанд 30, компьютерт 45 FPS. Бага бол нягтралыг 10%-аар бууруулж (55% хүртэл), хангалттай их бол
    // аажим (5%) өсгөнө: хэлбэлзэхгүй.
    const float target = InTouchMode() ? 30.0f : 45.0f;
    const float before = renderScale;
    if (fps < target - 3.0f) renderScale = std::max(0.55f, renderScale - 0.1f);
    else if (fps > target + 10.0f) renderScale = std::min(1.0f, renderScale + 0.05f);
    if (renderScale != before) TraceLog(LOG_INFO, "PERF: %.1f fps, render scale %.0f%%", fps, renderScale * 100.0f);
}

void Game::WatchPerformance(float dt)
{
    // Удаан компьютер: 4 секундын дундаж 20 FPS-ээс бага бол графикийг нэг шат бууруулна (сүүдэр, эффект).
    if (options.screenshotPath || environment.Quality() == 0) return;
    perfTime += std::min(dt, 0.5f);
    ++perfFrames;
    if (perfTime < 4.0f) return;
    const float fps = (float)perfFrames / perfTime;
    perfTime = 0.0f;
    perfFrames = 0;
    if (fps >= 20.0f || renderScale > 0.56f) return; // эхлээд нягтралыг бууруулна (UpdateRenderScale)
    progress.graphics = environment.Quality() - 1;
    progress.graphicsAuto = false; // энэ төхөөрөмжид хэт хүнд байсан: дахин автоматаар өсгөхгүй
    environment.SetQuality(progress.graphics);
    Save();
    TraceLog(LOG_WARNING, "PERF: %.1f fps, graphics lowered to %d", fps, progress.graphics);
    SetNotice(progress.graphics == 1 ? "Тоглоом удаан байна: график ДУНД болголоо" : "Тоглоом удаан байна: график БАГА болголоо", 4.0f);
}

void Game::AdvanceDay(float dt)
{
    dayClock = std::fmod(dayClock + dt, kDayLength);
    if (options.dayTime >= 0.0f) dayClock = options.dayTime * kDayLength; // туршилт
    environment.Update(dt);
    UpdateRenderScale(dt);
    WatchPerformance(dt);
    environment.SetTimeOfDay(dayClock / kDayLength);
    const bool isNight = environment.Night() > 0.5f;
    if (isNight != wasNight && state == GameState::Playing)
    {
        recruitNotice = isNight ? "ШӨНӨ БОЛЛОО — болгоомжтой" : "ҮҮР ЦАЙЛАА";
        recruitNoticeTimer = 2.5f;
    }
    wasNight = isNight;
}

void Game::UpdateMission(float dt)
{
    squad.SetLeaderSkins(progress.leaderSkin, guestFigure.skin); // 1-р дайчны дүр (өөрийн ба зочны баг)
    navigator.Update(world, player.Position(), dt);
    if (navigator.TakeArrived()) SetNotice("ОЧИХ ГАЗАРТАА ХҮРЛЭЭ", 2.5f);
    HandleSettingsInput();
    AdvanceDay(dt);

    // Ухрах (газрын зураг руу): утасны "УХРАХ" товч. Компьютер дээр тоглоомын цэсний "Газрын зураг".
    if (InTouchMode() && InKeyPressed(KEY_END))
    {
        ReturnToMap();
        return;
    }

    UpdateCoopMission(dt);
    UpdateBattle(dt);
    if (state != GameState::Playing) return; // тулаан дуусч газрын зураг руу буцсан (эсвэл шинэ хост болсон)
    UpdateVehicles(dt);
    UpdateVehicleBlockers(); // зогсож буй унаа: явган хүн, зомби дундуур нь гарахгүй
    UpdateTeamRiding(dt);
    world.UpdateDoors(dt);
    {
        PROFILE_SCOPE("world stream");
        world.Stream(player.Position()); // явах тусам урд талын газрыг бага багаар үүсгэнэ
    }

    const bool enclosed = garage.IsDriving() && SpecFor(garage.At(garage.Driven()).type).enclosed;
    const float cameraExtra = garage.IsDriving() ? SpecFor(garage.At(garage.Driven()).type).cameraExtra : 0.0f;
    // Зочин: өөрийн AI гишүүд рүү шилжих, "энд бай" (хост тооцоолдог тул хүсэлт илгээнэ)
    if (coopGuest) GuestTeamInput();
    // H: "Энд бай" — удирдаж буй гишүүнийг тэмдэглэнэ; өөр гишүүн рүү (Tab) шилжихэд тэр байрандаа үлдэж байлдана.
    if (InKeyPressed(KEY_H) && !coopGuest && !garage.IsDriving() && gunnerVehicle < 0)
    {
        holdMark[activeMember] = !holdMark[activeMember];
        if (holdMark[activeMember]) ChatterPlayer(VoiceLine::Hold, true);
        SetNotice(holdMark[activeMember] ? "ЭНД БАЙНА — өөр гишүүн рүү шилжинэ үү (Tab)" : "ЭНД БАЙХ ТУШААЛ ЦУЦЛАГДЛАА", 2.5f);
    }
    // G: бүгд над руу (байлдахаа түр больж гүйж ирнэ). B: загалмайн доорх зомби руу бүгд буудна.
    if (InKeyPressed(KEY_G) && !coopGuest)
    {
        squad.Regroup(6.0f);
        for (bool& mark : holdMark) mark = false;
        ChatterNearest(VoiceLine::Follow, player.Position(), true);
        SetNotice("БҮГД НАДАД ИР!", 1.8f);
    }
    if (InKeyPressed(KEY_B) && !coopGuest)
    {
        const Camera3D& cam = camera.Get();
        const Vector3 toTarget = Vector3Subtract(cam.target, cam.position);
        const float length = Vector3Length(toTarget);
        const Ray ray = { cam.position, Vector3Scale(toTarget, 1.0f / length) };
        const float wall = world.RayDistance(ray, length + 200.0f);
        ZombieHit hit = horde.Raycast(ray, length * 0.9f, wall);
        int target = hit.index;
        if (target < 0) target = horde.NearestInFront(player.Position(), camera.Yaw(), 40.0f); // тэр зүгийн ойрынх
        if (target >= 0)
        {
            squad.Focus(horde.At(target).id, 12.0f);
            ChatterNearest(VoiceLine::Fire, player.Position(), true);
            SetNotice("БАЙГ ОНИЛ: бүгд тэр зомби руу!", 1.8f);
        }
        else
        {
            SetNotice("Загалмайн доор зомби алга", 1.5f);
        }
    }
    // Tab: дараагийн амьд гишүүн рүү шилжинэ (F1-F3 нь хөтчийн тусламж, хайлттай давхцдаг тул хэрэглэхгүй).
    const bool testSwap = options.screenshotPath && options.swapFrame > 0 && missionFrame == options.swapFrame;
    if ((InKeyPressed(KEY_TAB) || testSwap) && !coopGuest)
    {
        for (int step = 1; step < kTeamSize; ++step)
        {
            if (SwitchMember((activeMember + step) % kTeamSize, false)) break;
        }
    }
    // Утас: багийн картыг товшиход тэр гишүүн рүү (картууд дотооддоо F1-F3 виртуал товч илгээдэг).
    for (int member = 0; member < kTeamSize && InTouchMode() && !coopGuest; ++member)
    {
        if (InKeyPressed(KEY_F1 + member)) SwitchMember(member, false);
    }

    const bool forceAim = options.screenshotPath && options.scope; // туршилтын горим
    // Онилгоо: баруун товч дарахад асч, дахин дарахад унтарна (утасны "ОНИЛОХ" товч ч мөн адил).
    if (!InTouchMode() && !paused && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) aimToggled = !aimToggled;
    if (enclosed || player.IsTraversing()) aimToggled = false;
    const bool aimInput = InTouchMode() ? InMouseDown(MOUSE_BUTTON_RIGHT) : aimToggled;
    const bool aiming = (aimInput || forceAim) && !enclosed;
    // Мэргэн буудагч дурантай буугаараа шагайхад дуран хараа гарна (явган үед).
    const bool scoped = aiming && arsenal.Current() == WeaponType::Sniper && !garage.IsDriving();
    const bool firearm = arsenal.Current() != WeaponType::Knife && arsenal.Current() != WeaponType::Grenade;
    // Автомат онилгоог асаах / унтраах (утасны "АВТО ОНИЛГОО" товч): сонголт хадгалагдана.
    if (InTouchMode() && (InKeyPressed(KEY_T)))
    {
        config.touchAimAssist = !config.touchAimAssist;
        progress.aimAssist = config.touchAimAssist;
        recruitNotice = config.touchAimAssist ? "АВТО ОНИЛГОО АСЛАА" : "АВТО ОНИЛГОО УНТАРЛАА — өөрөө онилно";
        recruitNoticeTimer = 1.8f;
        Save();
    }
    if (config.touchAimAssist && InTouchMode() && !enclosed && firearm && (aiming || InMouseDown(MOUSE_BUTTON_LEFT)))
    {
        AimAssist(dt);
    }
    // --- Аймгийн төв: орох үед нэр, статусыг харуулна ---
    {
        const bool nowInTown = world.CityActive() || InTown(player.Position().x, player.Position().z);
        if (nowInTown && !inTown && state == GameState::Playing)
        {
            bool city = false;
            const char* town = TownName(provinceIndex, city);
            SetNotice(city ? std::string(town) + " ХОТ"
                           : std::string(town) + "  •  " + Provinces()[(size_t)provinceIndex].name + " аймгийн төв", 3.0f);
        }
        inTown = nowInTown;
    }

    // Камерын горим: V (утсан дээр "КАМЕР" товч) — 1-р / 3-р этгээд. Унаа, шат, цонхонд 3-р этгээд.
    if (InKeyPressed(KEY_V))
    {
        progress.firstPerson = !progress.firstPerson;
        recruitNotice = progress.firstPerson ? "КАМЕР: 1-Р ЭТГЭЭД" : "КАМЕР: 3-Р ЭТГЭЭД";
        recruitNoticeTimer = 1.4f;
        Save();
    }
    camera.SetFirstPerson(progress.firstPerson && !garage.IsDriving() && gunnerVehicle < 0 && !player.IsTraversing());
    camera.SetTraversing(player.IsTraversing());
    if (player.IsTraversing())
    {
        camera.Guide(player.TraverseFacing(), 0.03f, dt); // бараг хэвтээ: цонхны нүхээр харна
    }
    // Жолоодож байхад камер унааны төвийг, унааны өндөрт тааруулж харна (суудлыг тойрвол урт машины дотор орно)
    Vector3 cameraFocus = player.Position();
    float cameraDrop = player.CameraDrop();
    if (garage.IsDriving() || gunnerVehicle >= 0)
    {
        const Vehicle& vehicle = garage.At(garage.IsDriving() ? garage.Driven() : gunnerVehicle);
        float height = 2.0f;
        switch (vehicle.type)
        {
        case VehicleType::Truck: height = 3.4f; break;
        case VehicleType::Jeep: height = 2.3f; break;
        case VehicleType::Suv: height = 2.2f; break;
        case VehicleType::Horse: height = 2.4f; break;
        default: height = 1.9f; break;
        }
        cameraFocus = vehicle.position;
        cameraDrop = config.cameraTargetHeight - height;
    }
    camera.SetRiding(garage.IsDriving());
    camera.Update(dt, config, world, cameraFocus, aiming, cameraExtra, scoped, cameraDrop);
    if (aiming)
    {
        player.AimAtPoint(CrosshairTarget());
    }

    arsenal.Update(dt);
    player.SetReload(arsenal.ReloadProgress());
    if (!enclosed)
    {
        UpdateShooting(); // бүхээгтэй машин дотроос буудаж болохгүй
    }

    {
        PROFILE_SCOPE("soldiers update");
        squad.Update(dt, world, player.Position(), horde, tracers, particles);
    }
    TrackSoldierLosses();
    UpdateWorldEvents(dt);
    UpdateDowned(dt);
    UpdateTutorial(dt);
    UpdateFeedback(dt);
    // Тулаан: AI дайчдын эсрэг баг руу оносон сум
    for (const HumanHit& hit : squad.TakeHumanHits())
    {
        TraceLog(LOG_INFO, "BATTLE: AI hit %d for %.0f", hit.id, hit.damage);
        if (hit.id == kHostTarget) player.TakeDamage(hit.damage);
        else if (hit.id == kGuestTarget) DamageGuest(hit.damage);
        else if (hit.id >= 0 && hit.id < squad.Total()) squad.Damage(hit.id, hit.damage);
    }
    grenades.Update(dt, world, horde, player, particles);
    // Бензинтэй торх: гранат, пуужингийн дэлбэрэлт, хурдтай унаа дэлбэлнэ; тоглогч дундуур нь гарахгүй.
    for (const Grenades::BlastEvent& blast : grenades.TakeBlasts())
    {
        barrels.Blast(blast.position, blast.radius);
        if (coopGuest) net.SendBlast(blast.position, blast.radius, blast.damage); // хост тооцно
    }
    if (garage.IsDriving())
    {
        const Vehicle& driven = garage.At(garage.Driven());
        barrels.Ram(driven.position, SpecFor(driven.type).radius, std::fabs(driven.speed));
    }
    if (!coopGuest && guestVehicle >= 0 && guestVehicle < garage.Count())
    {
        const Vehicle& remote = garage.At(guestVehicle); // зочны унаа (хост дэлбэлж, зочинд мэдэгдэнэ)
        barrels.Ram(remote.position, SpecFor(remote.type).radius, std::fabs(remote.speed));
    }
    barrels.Update(dt, horde, player, squad, particles);
    if (!garage.IsDriving() && gunnerVehicle < 0)
    {
        Vector3 position = player.Position();
        barrels.PushOut(position, config.playerRadius);
        player.Nudge(position.x, position.z);
    }
    particles.Update(dt, world);
    precipitation.Update(dt, camera.Get().position, environment.Weather(), environment.Wind());
    if (!coopGuest)
    {
        PROFILE_SCOPE("zombies update");
        horde.Update(dt, world, player, squad); // зочин: зомбинууд хостоос (UpdateCoopMission)
    }
    ExploderBlasts();
    SendHostSnapshot(dt);
    if (options.screenshotPath && options.autoPlay)
    {
        player.Heal(1.0e6f); // автомат туршилтын дүр үхэхгүй: даалгаврын логикийг л шалгана
    }
    tracers.Update(dt);

    int kills = 0;
    horde.TakeRewards(progress.coins, kills);
    progress.kills += kills;
    missionKills += kills;

    // Зомбины хурд: тухайн аймгийн үед устгасан тоо (хадгалагдана) 1000 тутамд 1.5 дахин
    if (!coopGuest)
    {
        const int stageKills = mission.KilledSmall() + mission.KilledBig();
        const int level = stageKills / kKillsPerSpeedup;
        const float multiplier = std::min(kMaxZombieSpeedMultiplier, std::pow(kZombieSpeedupStep, (float)level));
        if (multiplier > horde.SpeedMultiplier() + 0.01f && state == GameState::Playing && missionFrame > 2)
        {
            recruitNotice = multiplier >= kMaxZombieSpeedMultiplier ? "ЗОМБИНУУД ХАМГИЙН ХУРДАН БОЛЛОО!" : "ЗОМБИНУУД ХУРДСАЛАА!";
            recruitNoticeTimer = 2.5f;
        }
        horde.SetSpeedMultiplier(multiplier);
    }

    // "+N" зоос: шинэ устгалтуудыг нэмж, хуучныг (1.4 сек) арилгана
    killRewardBuffer.clear();
    horde.TakeKillRewards(killRewardBuffer);
    for (const ZombieHorde::KillReward& reward : killRewardBuffer)
    {
        coinPopups.push_back({ reward.position, reward.coins, 0.0f });
        // 100 устгалт тутам сүүлчийн зомбины байрнаас эмнэлгийн хайрцаг унана
        if (++medKitKills % MedKits::kKillsPerKit == 0)
        {
            medKits.Drop(world, reward.position);
            recruitNotice = "ЭМНЭЛЭГ УНАЛАА! — газрын зураг дээрх улаан загалмай";
            recruitNoticeTimer = 3.0f;
        }
    }
    Vector3 pickedKit = {};
    if (medKits.Update(dt, player, squad, &pickedKit))
    {
        if (coopGuest) net.SendPickup(pickedKit);
        recruitNotice = "АМЬ 100% БОЛЛОО";
        recruitNoticeTimer = 2.0f;
    }
    for (CoinPopup& popup : coinPopups) popup.age += dt;
    coinPopups.erase(std::remove_if(coinPopups.begin(), coinPopups.end(), [](const CoinPopup& p) { return p.age > 1.4f; }),
                     coinPopups.end());

    if (coopGuest)
    {
        // Зочин: даалгавар, шагнал, ялалтыг хост тооцно (UpdateCoopMission, UpdateNetwork). Дэлгүүр өөрийн зоосоор.
        recruitNoticeTimer = std::max(0.0f, recruitNoticeTimer - dt);
        inShop = player.IsAlive() && !garage.IsDriving() && !player.IsTraversing() &&
                 world.InSafeRoom(player.Position().x, player.Position().z);
        if (inShop && InKeyPressed(KEY_E)) OpenShop();
        return;
    }

    if (!player.IsAlive())
    {
        // Удирдаж байсан гишүүн үхсэн: амьд үлдсэн гишүүн рүү шилжинэ. Гурвуулаа үхвэл даалгавар бүтэлгүйтнэ.
        bool switched = false;
        for (int member = 0; member < kTeamSize && !switched; ++member)
        {
            switched = SwitchMember(member, true);
        }
        if (!switched)
        {
            Save();
            if (battleActive && takeoverBattle)
            {
                // Тасарсан хостын AI баг ялав (энэ тоглогч серверт зочин): хост ялсанд мэдэгдэнэ
                TraceLog(LOG_INFO, "BATTLE: absent host's AI team won");
                Account::ReportBattle(false);
                takeoverBattle = false;
                battleActive = false;
            }
            else if (battleActive)
            {
                // Тулаан: хостын үндсэн 3 дайчин унав, зочин ялна (шинэ хост болно)
                TraceLog(LOG_INFO, "BATTLE: host team down, guest won");
                net.SendBattleOver(true);
                Account::ReportBattle(true);
                battleActive = false;
            }
            state = GameState::Lost;
            return;
        }
    }

    // --- Даалгаврууд: E товчоор объекттой харилцана (унаанаас буусан үед) ---
    bool interact = InKeyPressed(KEY_E);
    if (options.screenshotPath && options.autoPlay)
    {
        interact = mission.AutoPlay(dt, world, player, horde) || interact; // туршилтын автомат тоглолт
        if (options.stopAfter > 0 && mission.CompletedCount() >= options.stopAfter)
        {
            interact = false; // заасан тооны даалгаврын дараа зогсоно (хадгалалтыг шалгахад)
        }
    }
    // --- Дэлгүүр: дотор нь орж E дарахад цэс нээгдэнэ (цэрэг, зэвсэг, гранат, амь зоосоор) ---
    recruitNoticeTimer = std::max(0.0f, recruitNoticeTimer - dt);
    inShop = !garage.IsDriving() && !player.IsTraversing() && world.InSafeRoom(player.Position().x, player.Position().z);
    if (inShop && interact)
    {
        interact = false; // даалгаврын объекттой давхар харилцахгүй
        OpenShop();
        return;
    }

    const bool autoStopped = options.autoPlay && options.stopAfter > 0 && mission.CompletedCount() >= options.stopAfter;
    if (!autoStopped)
    {
        mission.Update(dt, world, player, horde, squad, interact, !garage.IsDriving() && gunnerVehicle < 0);
    }

    mission.TakeReward(); // даалгаврын зоосны шагнал байхгүй (Gold зөвхөн серверийн дүрмээр)
    if (mission.TakeDirty())
    {
        Save(); // даалгаврын явц шууд хадгалагдана
    }

    // Бүх даалгавар биелж, үеийн нөөц (2500 жижиг, 500 том зомби) устгагдсан: аймаг чөлөөлөгдөнө.
    // Холбоогоор онгоц дуудаж үлдсэн зомбийг бөмбөгдөх богино кино явж, дуусахад ялалтын дэлгэц гарна.
    // (Автомат туршилт нөөцийг заагаагүй бол зөвхөн даалгаврын логикийг шалгана.)
    const bool quotaRequired = !(options.screenshotPath && options.autoPlay) || options.quotaSmall >= 0;
    if (mission.IsComplete() && (mission.QuotaMet() || !quotaRequired))
    {
        lastReward = 0; // аймаг чөлөөлөхөд зоос өгөхгүй
        progress.liberated[provinceIndex] = true;
        progress.stages[(size_t)provinceIndex] = StageSave{}; // дараагийн удаа шинээр эхэлнэ
        Save();
        journalOpen = false;
        shopOpen = false;
        fullMap.Close();
        airstrike.Start(player.Position(), camera.Yaw());
        if (net.Connected() && remoteActive) net.SendAirstrike(player.Position(), camera.Yaw()); // зочин ч үзнэ
        state = GameState::Airstrike;
    }
}

void Game::UpdateAirstrike(float dt)
{
    if (coopGuest)
    {
        // Зочин: зомбинуудын үхлийг хостын snapshot-оос авна; кино дуусахад хост MissionWon илгээнэ.
        player.Heal(1.0e6f);
        airstrike.Update(dt, world, horde, particles);
        UpdateCoopMission(dt);
        particles.Update(dt, world);
        tracers.Update(dt);
        return;
    }
    UpdateCoopMission(dt); // хост: зочны дүрийг хөдөлгөсөөр

    // Кино: багийнхан үхэхгүй (амийг дүүргэнэ), зомби, цэргүүд хөдөлсөөр (үхэх хөдөлгөөн харагдана).
    player.Heal(1.0e6f);
    for (Soldier& soldier : squad.All())
    {
        if (soldier.alive) soldier.health = soldier.maxHealth;
    }
    airstrike.Update(dt, world, horde, particles);
    horde.Update(dt, world, player, squad);
    squad.Update(dt, world, player.Position(), horde, tracers, particles);
    particles.Update(dt, world);
    tracers.Update(dt);
    int kills = 0;
    horde.TakeRewards(progress.coins, kills);
    progress.kills += kills;
    missionKills += kills;
    killRewardBuffer.clear();
    horde.TakeKillRewards(killRewardBuffer); // кинонд зоосны бичиг харуулахгүй
    SendHostSnapshot(dt);

    if (airstrike.Finished() || InKeyPressed(KEY_ENTER) || (InTouchMode() && InMousePressed(MOUSE_BUTTON_LEFT)))
    {
        if (!airstrike.Finished()) airstrike.Skip();
        airstrike.Update(0.0f, world, horde, particles); // үлдсэн зомбийг устгана
        airstrike.Stop();
        Save();
        if (Account::CurrentRoomRole() == Account::RoomRole::Host) Account::CompleteRoom(); // шагнал, өрөө хаагдана
        if (net.Connected() && remoteActive) net.SendMissionWon(params.reward);
        if (remoteActive && joinCoop && Account::Enabled()) SetNotice(TextFormat("ХАМТ ДАВЛАА! Хоёулаа +%d Gold", Account::CoopReward()), 5.0f);
        state = GameState::Won;
    }
}

// ---------------------------------------------------------------- Тоглоомын цэс (ESC)
namespace
{
    enum class PauseItem { Resume, Graphics, Sound, Fullscreen, FrameRate, Map, Quit };

    // Компьютерийн FPS хязгаар (Progress::frameLimit): 0 = дэлгэцийн давтамж (VSync), хязгааргүй = 0 FPS
    const int kFrameLimits[5] = { -1, 60, 120, 144, 0 };


    // Одоогийн төлөвт харагдах мөрүүд (газрын зураг дээр "Газрын зураг руу буцах" байхгүй; утсан дээр бүтэн дэлгэц байхгүй).
    int PauseItems(bool playing, PauseItem* out)
    {
        int count = 0;
        out[count++] = PauseItem::Resume;
        out[count++] = PauseItem::Graphics;
        out[count++] = PauseItem::Sound;
#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
        if (!InTouchMode()) out[count++] = PauseItem::Fullscreen;
        if (!InTouchMode()) out[count++] = PauseItem::FrameRate;
#endif
        if (playing) out[count++] = PauseItem::Map;
#if !defined(__EMSCRIPTEN__) // хөтөч дээр "гарах" хэрэггүй: tab-аа хаана
        out[count++] = PauseItem::Quit;
#endif
        return count;
    }

    Rectangle PauseRow(int index, int count)
    {
        const float width = 420.0f, height = 60.0f, gap = 12.0f;
        const float total = (float)count * height + (float)(count - 1) * gap;
        const float top = ((float)ViewHeight() - total) * 0.5f + 30.0f;
        return { ((float)ViewWidth() - width) * 0.5f, top + (float)index * (height + gap), width, height };
    }
}

void Game::OpenPause()
{
    paused = true;
    EnableCursor();
}

void Game::ClosePause()
{
    paused = false;
    if (state == GameState::Playing && !journalOpen && !shopOpen && !fullMap.IsOpen()) LockCursor();
}

void Game::SetFullscreen(bool enabled)
{
#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
    // Хүрээгүй бүтэн цонх (alt-tab хурдан). UI 1280x720-д зориулсан тул бүтэн дэлгэцэд 720 өндөртэй
    // дотоод зургаар зурж, дэлгэцэнд сунгана (газрын зураг, HUD-ийн байрлал ижил хэвээр).
    if (enabled != IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE)) ToggleBorderlessWindowed();
    ViewShutdown();
    // Өргөн нь 1280-аас багагүй (16:10, 4:3 дэлгэцэд газрын зургийн баруун самбар тасрахгүй).
    const int fitHeight = (int)std::ceil(1280.0 * GetScreenHeight() / std::max(1, GetScreenWidth()));
    ViewInit(enabled ? std::max(720, fitHeight) : options.viewHeight);
#endif
    progress.fullscreen = enabled;
    Save();
}

void Game::UpdatePauseMenu()
{
    PauseItem items[8];
    const int count = PauseItems(state == GameState::Playing, items);
    int chosen = -1;
    for (int i = 0; i < count; ++i)
    {
        if (InKeyPressed(KEY_ONE + i) || (InMousePressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(InPointer(), PauseRow(i, count))))
        {
            chosen = i;
        }
    }
    if (chosen < 0) return;

    switch (items[chosen])
    {
    case PauseItem::Resume: ClosePause(); break;
    case PauseItem::Graphics:
        progress.graphicsAuto = false; // тоглогчийн сонголт: автоматаар солихгүй
        progress.graphics = (progress.graphics + 2) % 3; // өндөр -> дунд -> бага -> өндөр
        environment.SetQuality(progress.graphics);
        Save();
        break;
    case PauseItem::Sound:
        // 100% -> 60% -> 30% -> үгүй -> 100%
        if (!progress.sound) { progress.sound = true; progress.soundLevel = 0; }
        else if (progress.soundLevel >= 2) progress.sound = false;
        else ++progress.soundLevel;
        Audio::SetVolume(progress.sound ? kSoundLevels[progress.soundLevel] : 0.0f);
        Save();
        break;
    case PauseItem::Fullscreen: SetFullscreen(!progress.fullscreen); break;
    case PauseItem::FrameRate:
        progress.frameLimit = (progress.frameLimit + 1) % 5;
        ApplyFrameLimit();
        Save();
        break;
    case PauseItem::Map:
        paused = false;
        ReturnToMap();
        break;
    case PauseItem::Quit:
        Save();
        quitRequested = true;
        break;
    }
}

void Game::UpdateVehicleBlockers()
{
    // Явган хүн, зомби унааны дундуур гарахгүй: унаа бүр урт тэнхлэгийнхээ дагуух капсул. Хурдан явж буй унаа
    // (мөргөж дайрах тооцоотой) ба тоглогчийн жолоодож буй нь орохгүй.
    std::vector<VehicleBlocker> blockers;
    blockers.reserve((size_t)garage.Count());
    for (int i = 0; i < garage.Count(); ++i)
    {
        const Vehicle& v = garage.At(i);
        if (std::fabs(v.speed) > 3.0f || (garage.IsDriving() && i == garage.Driven())) continue;
        const float radius = SpecFor(v.type).radius;
        const Vector2 along = { std::sin(v.heading) * radius * 0.45f, std::cos(v.heading) * radius * 0.45f };
        const Vector2 center = { v.position.x, v.position.z };
        blockers.push_back({ Vector2Subtract(center, along), Vector2Add(center, along), radius * 0.5f, v.position.y - 0.3f,
                             v.position.y + 1.7f });
    }
    world.SetVehicleBlockers(std::move(blockers));
}

void Game::ApplyFrameLimit()
{
#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
    // Дэлгэцийн давтамжаар: VSync (тасархай зураггүй, 144 Hz дэлгэц дээр 144). Бусад: VSync унтраад тогтсон
    // хязгаар (120, 144) эсвэл хязгааргүй (хүчтэй компьютерт хамгийн бага хоцрогдол).
    const int index = std::clamp(progress.frameLimit, 0, 4);
    if (index == 0)
    {
        SetWindowState(FLAG_VSYNC_HINT);
        const int refresh = GetMonitorRefreshRate(GetCurrentMonitor());
        SetTargetFPS(refresh > 0 ? refresh : config.targetFps);
    }
    else
    {
        ClearWindowState(FLAG_VSYNC_HINT);
        SetTargetFPS(kFrameLimits[index]);
    }
    TraceLog(LOG_INFO, "GAME: frame limit %d (vsync %s)", index == 0 ? GetMonitorRefreshRate(GetCurrentMonitor()) : kFrameLimits[index],
             index == 0 ? "on" : "off");
#endif
}

void Game::DrawPauseMenu() const
{
    const int width = ViewWidth(), height = ViewHeight();
    DrawRectangle(0, 0, width, height, Fade(BLACK, 0.55f));

    PauseItem items[8];
    const int count = PauseItems(state == GameState::Playing, items);
    const Rectangle first = PauseRow(0, count), last = PauseRow(count - 1, count);
    const Rectangle panel = { first.x - 30.0f, first.y - 90.0f, first.width + 60.0f, last.y + last.height - first.y + 150.0f };
    Ui::PanelAccent(panel, Ui::kAccent);
    const char* title = "ЦЭС";
    const int titleX = (int)(panel.x + (panel.width - (float)Ui::MeasureBold(title, 30)) * 0.5f);
    Ui::DrawIcon(Ui::Icon::Pause, { (float)titleX - 40.0f, panel.y + 26.0f, 32.0f, 32.0f });
    Ui::TextBold(title, titleX, (int)panel.y + 26, 30, Ui::kText);

    const Vector2 pointer = InPointer();
    for (int i = 0; i < count; ++i)
    {
        const Rectangle row = PauseRow(i, count);
        const char* label = "";
        switch (items[i])
        {
        case PauseItem::Resume: label = "Үргэлжлүүлэх"; break;
        case PauseItem::Sound:
            label = !progress.sound ? "Дуу: ҮГҮЙ" : progress.soundLevel == 0 ? "Дуу: 100%" : progress.soundLevel == 1 ? "Дуу: 60%" : "Дуу: 30%";
            break;
        case PauseItem::Graphics:
            label = progress.graphics == 2 ? "График: ӨНДӨР" : progress.graphics == 1 ? "График: ДУНД" : "График: БАГА";
            break;
        case PauseItem::Fullscreen: label = progress.fullscreen ? "Бүтэн дэлгэц: ТИЙМ" : "Бүтэн дэлгэц: ҮГҮЙ"; break;
        case PauseItem::FrameRate:
            label = progress.frameLimit == 0 ? TextFormat("FPS: ДЭЛГЭЦЭЭР (%d)", GetMonitorRefreshRate(GetCurrentMonitor()))
                  : progress.frameLimit == 4 ? "FPS: ХЯЗГААРГҮЙ"
                                             : TextFormat("FPS: %d", kFrameLimits[progress.frameLimit]);
            break;
        case PauseItem::Map: label = "Газрын зураг руу буцах"; break;
        case PauseItem::Quit: label = "Тоглоомоос гарах"; break;
        }
        const bool hover = !InTouchMode() && CheckCollisionPointRec(pointer, row);
        const bool danger = items[i] == PauseItem::Quit;
        Ui::Panel(row, hover ? Fade(danger ? Ui::kDanger : Ui::kAccent, 0.25f) : Ui::kPanelSoft, 12.0f);
        if (!InTouchMode()) Ui::KeyBadge(TextFormat("%d", i + 1), (int)row.x + 16, (int)row.y + 17, 16, Ui::kTextDim);
        // Мөрийн баруун талд үйлдлийн икон
        Ui::Icon icon = Ui::Icon::Play;
        switch (items[i])
        {
        case PauseItem::Resume: icon = Ui::Icon::Play; break;
        case PauseItem::Graphics: icon = Ui::Icon::Settings; break;
        case PauseItem::Sound: icon = Ui::Icon::Sound; break;
        case PauseItem::Fullscreen: icon = Ui::Icon::Fullscreen; break;
        case PauseItem::FrameRate: icon = Ui::Icon::Settings; break;
        case PauseItem::Map: icon = Ui::Icon::Back; break;
        case PauseItem::Quit: icon = Ui::Icon::Close; break;
        }
        Ui::DrawIcon(icon, { row.x + row.width - 44.0f, row.y + (row.height - 24.0f) * 0.5f, 24.0f, 24.0f },
                     hover ? WHITE : Fade(WHITE, 0.75f));
        Ui::TextBold(label, (int)(row.x + (row.width - (float)Ui::MeasureBold(label, 22)) * 0.5f), (int)row.y + 17, 22,
                     danger ? Ui::kDanger : Ui::kText);
    }
    const char* help = InTouchMode() ? "Мөрийг товшино уу" : "ESC — үргэлжлүүлэх    F11 — бүтэн дэлгэц";
    Ui::Text(help, (int)(panel.x + (panel.width - (float)Ui::Measure(help, 15)) * 0.5f), (int)(last.y + last.height) + 22, 15,
             Ui::kTextDim);
}


void Game::CloseJournal()
{
    journalOpen = false;
    if (state == GameState::Playing)
    {
        LockCursor();
    }
}

// ---------------------------------------------------------------- Зураглал

void Game::RenderShadows() const
{
    const bool cinematic = state == GameState::Airstrike;
    const Camera3D view = cinematic ? airstrike.GetCamera() : camera.Get();
    // Сүүдрийн хүрээний төв: камерын урд талд (харагдах хэсэг их хэмжээгээр багтана)
    Vector3 forward = Vector3Subtract(view.target, view.position);
    forward.y = 0.0f;
    forward = Vector3Length(forward) > 0.001f ? Vector3Normalize(forward) : Vector3{ 0.0f, 0.0f, 1.0f };
    const float radius = environment.ShadowRadius();
    Vector3 center = Vector3Add(view.position, Vector3Scale(forward, radius * 0.45f));
    center.y = world.GroundHeight(center.x, center.z);
    if (!environment.BeginShadowPass(center)) return;

    world.DrawShadowCasters(center, radius * 1.1f, environment.DepthMaterial());
    if (environment.Quality() < 2)
    {
        environment.EndShadowPass(); // "дунд" график: зөвхөн барилга, мод, газрын сүүдэр (хүмүүс, унаа үгүй)
        return;
    }

    garage.Draw(center, radius * 1.4f, &environment.DepthMaterial(), true);
    SetPropDepthMaterial(&environment.DepthMaterial()); // хүмүүсийн барьсан бууны загвар
    squad.Draw(center, false);
    horde.Draw(view.position, 12.0f, false); // зомби олон тул зөвхөн хамгийн ойрынх нь сүүдэр тусгана
    if (player.IsAlive()) player.Draw(config, false); // 1-р этгээдийн үед ч өөрийн сүүдэр харагдана
    barrels.Draw(center);
    if (remoteActive && !coopGuest) DrawRemoteFigure(guestFigure, TeamMemberLook(guestFigure.member, guestFigure.skin));
    if (coopGuest && guestPlaced)
    {
        DrawRemoteFigure(hostFigure, TeamMemberLook(hostFigure.member, hostFigure.skin));
        for (const RemoteFigure& soldier : hostSoldiers) DrawRemoteFigure(soldier, TeamMemberLook(soldier.member, soldier.skin));
    }
    SetPropDepthMaterial(nullptr);
    environment.EndShadowPass();
}

void Game::DrawMissionScene() const
{
    const bool cinematic = state == GameState::Airstrike;
    const Camera3D strikeView = airstrike.GetCamera();
    const Camera3D& view = cinematic ? strikeView : camera.Get();
    const Material& material = environment.GetMaterial();

    environment.DrawSky(view); // 2D: тэнгэр, үүл, нар

    BeginMode3D(view);
    ViewCull::Begin(view); // камерын ард байгаа дүрс, унааг зурахгүй

    // Гэрэлтүүлэг + манантай хэсэг
    environment.Begin(view);
    if (world.HasFarLand()) environment.DrawFarLand(world.FarLand(), view); // жинхэнэ уулс, гол (10 км)
    else environment.DrawMountains(view.position);
    {
        PROFILE_SCOPE("draw world");
        world.Draw(view, material, environment.TerrainMaterial(), environment.DetailMaterial(), config.detailDistance,
                   environment.ShadowsEnabled());
        world.DrawDoors(view.position);
    }
    {
        PROFILE_SCOPE("draw vehicles");
        garage.Draw(view.position, config.vehicleDrawDistance, &environment.PropMaterial());
    }
    mission.Draw(view.position);
    {
        PROFILE_SCOPE("draw soldiers");
        squad.Draw(view.position, showDebug);
    }
    {
        PROFILE_SCOPE("draw zombies");
        horde.Draw(view.position, config.characterDrawDistance, showDebug);
    }
    if (cinematic || (!camera.IsScoped() && !camera.IsFirstPerson()))
    {
        player.Draw(config, showDebug); // дуран хараа, 1-р этгээдийн үед өөрийн бие харагдахгүй
    }
    airstrike.Draw();
    barrels.Draw(view.position);
    medKits.Draw(view.position);
    events.Draw(view.position);
    if (remoteActive && !coopGuest) DrawRemoteFigure(guestFigure, TeamMemberLook(guestFigure.member, guestFigure.skin));
    if (coopGuest && guestPlaced)
    {
        DrawRemoteFigure(hostFigure, TeamMemberLook(hostFigure.member, hostFigure.skin));
        for (const RemoteFigure& soldier : hostSoldiers) DrawRemoteFigure(soldier, TeamMemberLook(soldier.member, soldier.skin));
    }
    particles.Draw();
    grenades.Draw();
    particles.DrawPuffs();
    rlDrawRenderBatchActive(); // дүрүүдийг хагас тунгалаг хашаанаас өмнө зурж дуусгана
    world.DrawTransparent(view, material);
    if (!cinematic && camera.IsFirstPerson() && player.IsAlive())
    {
        DrawViewModel(view, player.Held(), player.Recoil(), player.WalkPhase(), player.HorizontalSpeed() / config.walkSpeed,
                      player.ReloadProgress(), player.ActionProgress(), camera.AimBlend());
    }
    environment.End();

    // Бороо, цас, шороо (гэрэлтүүлэггүй, шөнө бүдэг). Барилга дотор байхад зурахгүй.
    if (world.InteriorAt(view.position.x, view.position.z) < 0)
    {
        precipitation.Draw(view, 1.0f - environment.Night() * 0.75f);
    }

    // Гэрэлтүүлэггүй: сумны мөр, амны гал
    tracers.Draw();
    particles.DrawUnlit();
    barrels.DrawUnlit(view.position);
    medKits.DrawUnlit(view.position);
    events.DrawUnlit(view.position);
    if (state == GameState::Playing)
    {
        mission.DrawBeacons(world, view.position);
        if (!camera.IsScoped() && !fullMap.IsOpen()) navigator.Draw3D(view, player.Position()); // дээд талын 3D сум
    }
    if (showDebug)
    {
        world.DrawColliders(view.position);
    }
    ViewCull::End();
    EndMode3D();
}

void Game::DrawMission() const
{
    const bool cinematic = state == GameState::Airstrike;

    HudInfo info;
    info.provinceName = Provinces()[provinceIndex].name; // монгол нэр (Regions.cpp)
    info.coins = progress.coins;
    {
        // Өдөр шөнийн заагч: шөнө 0.63-0.955 (Environment::SetTimeOfDay-тэй ижил шилжилтийн дунд цэг)
        const float t = dayClock / kDayLength, nightStart = 0.63f, nightEnd = 0.955f;
        info.night = t >= nightStart && t < nightEnd;
        const float start = info.night ? nightStart : (t >= nightEnd ? nightEnd : nightEnd - 1.0f);
        const float length = info.night ? nightEnd - nightStart : 1.0f - (nightEnd - nightStart);
        info.phaseProgress = std::clamp((t - start) / length, 0.0f, 1.0f);
        info.phaseLeft = (1.0f - info.phaseProgress) * length * kDayLength;
    }
    info.missionKills = missionKills;
    info.ammo = arsenal.Ammo();
    info.magazine = arsenal.MagazineSize();
    info.reserve = ammoReserve;
    info.reloading = arsenal.IsReloading();
    static const char* const fighterSlots[] = { "1 ХУТГА", "2 ГАР БУУ", "3 АВТОМАТ", "4 ҮРЛЭН БУУ", "5 ГРАНАТ" };
    static const char* const sniperSlots[] = { "1 ХУТГА", "2 ГАР БУУ", "3 ДУРАНТ БУУ" };
    static const char* const heavySlots[] = { "1 ХУТГА", "2 ГАР БУУ", "3 АВТОМАТ", "4 ПУУЖИН", "5 ГРАНАТ" };
    info.weaponSlot = arsenal.CurrentSlot();
    info.slotNames = IsSniperMember(activeMember) ? sniperSlots : activeMember == 1 ? heavySlots : fighterSlots;
    info.slotCount = arsenal.SlotCount();
    for (int i = 0; i < info.slotCount && i < 8; ++i) info.slotWeapons[i] = (int)arsenal.SlotWeapon(i);
    info.scoped = camera.IsScoped() && state == GameState::Playing;
    info.hitMarker = hitMarker;
    if (state == GameState::Playing && !options.screenshotPath && !coopGuest && progress.tutorial < kTutorialSteps)
    {
        info.tutorial = TutorialText();
        info.tutorialStep = progress.tutorial;
        info.tutorialSteps = kTutorialSteps;
    }
    info.hitMarkerKind = hitMarkerKind;
    if (damageArrow > 0.0f)
    {
        // Цохиулсан зүг: газрын зурагтай ижил (дэлгэцийн дээш = камерын харж буй зүг)
        const float dx = damageFrom.x - player.Position().x, dz = damageFrom.z - player.Position().z, yaw = camera.Yaw();
        const float right = -dx * std::cos(yaw) + dz * std::sin(yaw), up = dx * std::sin(yaw) + dz * std::cos(yaw);
        info.damageArrow = damageArrow;
        info.damageAngle = std::atan2(right, up);
    }
    info.usesAmmo = arsenal.UsesAmmo() && arsenal.Current() != WeaponType::Grenade; // гранат хязгааргүй: тоо харуулахгүй
    info.showMagazine = arsenal.Current() != WeaponType::Grenade;
    info.showHelp = showHelp;
    info.touch = InTouchMode();
    for (int i = 0; i < 3; ++i) info.portraits[i] = portraits[i].texture;
    info.stance = player.GetStance() == Stance::Crouch ? "СУУСАН" : player.GetStance() == Stance::Prone ? "ХЭВТСЭН" : nullptr;

    // Баг: гурван гишүүний амь; элсүүлсэн цэргүүдийг тусад нь тоолно.
    for (int member = 0; member < kTeamSize; ++member)
    {
        HudInfo::TeamEntry& entry = info.team[member];
        entry.name = TeamMemberName(member);
        entry.active = member == activeMember;
        if (entry.active)
        {
            entry.health = (int)std::ceil(player.Health());
            entry.alive = player.IsAlive();
        }
        else
        {
            const int index = squad.FindMember(member);
            entry.alive = index >= 0 && squad.All()[(size_t)index].alive;
            entry.health = entry.alive ? (int)std::ceil(squad.All()[(size_t)index].health) : 0;
            if (entry.alive && squad.All()[(size_t)index].holding) entry.tag = "БАЙРАНДАА";
        }
        if (entry.active && holdMark[member]) entry.tag = "ЭНД БАЙНА";
    }
    for (const Soldier& soldier : squad.All())
    {
        if (soldier.member < 0)
        {
            ++info.squadTotal;
            info.squadAlive += soldier.alive;
        }
    }

    // Одоо хийх даалгавар: нэр, явц, хүртэлх зай
    char objectiveText[200] = "";
    char objectiveDetail[120] = "";
    const int focus = mission.FocusObjective();
    if (focus >= 0)
    {
        std::snprintf(objectiveText, sizeof(objectiveText), "%d / %d  %s", focus + 1, mission.ObjectiveCount(),
                      mission.Stage().objectives[(size_t)focus].title);
        const std::string progressText = mission.ProgressText(focus);
        const int meters = (int)mission.DistanceTo(focus, player.Position());
        if (progressText.empty()) std::snprintf(objectiveDetail, sizeof(objectiveDetail), "%d м", meters);
        else std::snprintf(objectiveDetail, sizeof(objectiveDetail), "%s    %d м", progressText.c_str(), meters);
        info.objective = objectiveText;
        info.objectiveDetail = objectiveDetail;
    }
    else if (!mission.QuotaMet())
    {
        // Даалгаврууд биелсэн ч зомби үлдсэн
        info.objective = "Үлдсэн зомбийг устга";
    }
    info.hordeSmall = mission.QuotaSmall() - mission.KilledSmall();
    info.hordeBig = mission.QuotaBig() - mission.KilledBig();
    info.zombieSpeed = horde.SpeedMultiplier();
    if (!coopGuest && mission.GuardedObjective() >= 0)
    {
        info.guardSmall = mission.PoolLeft(mission.GuardedObjective(), false);
        info.guardBig = mission.PoolLeft(mission.GuardedObjective(), true);
    }
    if (coopGuest)
    {
        // Зочин: даалгавар, зомбины тоог хостоос
        info.objective = guestView.objective.empty() ? "Хосттой холбогдож байна..." : guestView.objective.c_str();
        info.objectiveDetail = guestView.detail.empty() ? nullptr : guestView.detail.c_str();
        info.hordeSmall = guestView.hordeSmall;
        info.hordeBig = guestView.hordeBig;
        info.zombieSpeed = guestView.zombieSpeed;
        static std::string selfName;
        selfName = PlayerName();
        // Зочин: өөрөө ба өөрийн хоёр дайчин (хост тооцоолдог), доор нь хостын чип
        info.teamCount = 1;
        info.team[0] = { selfName.c_str(), (int)std::ceil(player.Health()), player.IsAlive(), true };
        info.portraits[0] = portraits[std::clamp(activeMember, 0, 2)].texture;
        info.teamKeys[0] = "Та";
        for (const Net::SoldierState& s : guestView.soldiers)
        {
            if (!s.guest || info.teamCount >= 3 || s.member < 0) continue;
            info.team[info.teamCount] = { TeamMemberName(s.member), (int)std::ceil(s.health), s.alive != 0, false };
            info.portraits[info.teamCount] = portraits[std::clamp((int)s.member, 0, 2)].texture;
            info.teamKeys[info.teamCount] = info.teamCount == 1 ? "2" : "3";
            ++info.teamCount;
        }
        info.squadTotal = 0;
        info.partnerName = hostName.empty() ? "Хост" : hostName.c_str();
        info.partnerHealth = (guestView.host.flags & Net::kAlive) != 0 ? -1 : 0;
    }
    else if (remoteActive)
    {
        info.partnerName = joinRequester.c_str(); // хост: нэгдсэн тоглогч
        info.partnerHealth = guestFigure.alive ? -1 : 0;
    }
    info.prompt = mission.Prompt();
    info.notice = mission.Notice();
    static char recruitPrompt[96];
    if (inShop && !shopOpen && state == GameState::Playing)
    {
        std::snprintf(recruitPrompt, sizeof(recruitPrompt), "%sДэлгүүр — сум, гранат, амь, цэрэг (%d зоос)",
                      InTouchMode() ? "" : "E — ", progress.coins);
        info.prompt = recruitPrompt;
    }
    if (recruitNoticeTimer > 0.0f && recruitNotice)
    {
        info.notice = recruitNotice;
    }

    static char downText[96];
    if (state == GameState::Playing && player.IsDowned())
    {
        // Унасан: нөхөд босгохыг хүлээнэ
        const float revive = squad.HostReviveProgress();
        info.banner = "УНАЛАА!";
        if (revive > 0.0f) std::snprintf(downText, sizeof(downText), "Нөхөр чинь босгож байна... %d%%", (int)(revive / 3.0f * 100.0f));
        else std::snprintf(downText, sizeof(downText), "Нөхдөө хүлээ — %d сек дотор босгохгүй бол үхнэ", (int)std::ceil(player.DownTimeLeft()));
        info.subBanner = downText;
    }
    else if (state == GameState::Playing && humanRevive > 0.0f)
    {
        std::snprintf(downText, sizeof(downText), "Нөхрөө босгож байна... %d%%", (int)(humanRevive / 3.0f * 100.0f));
        info.subBanner = downText;
    }
    if (state == GameState::Lost)
    {
        DrawRectangle(0, 0, ViewWidth(), ViewHeight(), Fade(MAROON, 0.5f));
        info.banner = "БАГ ЯЛАГДЛАА";
        info.subBanner = InTouchMode() ? "Зоос болон биелүүлсэн даалгаврууд хадгалагдана."
                                       : "Зоос болон биелүүлсэн даалгаврууд хадгалагдана.     ENTER — газрын зураг руу буцах";
    }

    // Унааны мэдээлэл: унаж яваа бол хурд ба гадаргуу, үгүй бол ойрын унааны санамж
    char rideText[256] = ""; // UTF-8 кирилл үсэг 2 байт
    if (garage.IsDriving())
    {
        const Vehicle& vehicle = garage.At(garage.Driven());
        const char* surface = garage.DrivenSurface() == Surface::Asphalt ? "ЗАСМАЛ ЗАМ"
                            : garage.DrivenSurface() == Surface::Dirt ? "ШОРООН ЗАМ" : "ХЭЭР ТАЛ";
        const bool horse = vehicle.type == VehicleType::Horse;
        const int condition = (int)std::ceil(100.0f * vehicle.health / SpecFor(vehicle.type).maxHealth);
        char status[64];
        if (horse) std::snprintf(status, sizeof(status), "Амь %d%%", condition);
        else std::snprintf(status, sizeof(status), "Бензин %d%%   Бүтэн %d%%", (int)std::ceil(vehicle.fuel * 100.0f), condition);
        std::snprintf(rideText, sizeof(rideText),
                      InTouchMode() ? "%s  %d км/ц  %s   %s%s"
                                    : "%s  %d км/ц  %s   %s%s     F — буух",
                      SpecFor(vehicle.type).name, (int)(std::sqrt(vehicle.speed * vehicle.speed + vehicle.slip * vehicle.slip) * 3.6f), surface, status,
                      horse ? (InTouchMode() ? "     Жойстикийг бүрэн түлхэж давхина" : "     Shift — давхих") : "");
        info.rideText = rideText;
    }
    else if (gunnerVehicle >= 0)
    {
        const Vehicle& vehicle = garage.At(gunnerVehicle);
        const int condition = (int)std::ceil(100.0f * vehicle.health / SpecFor(vehicle.type).maxHealth);
        std::snprintf(rideText, sizeof(rideText), InTouchMode() ? "%s  буудагч   Бүтэн %d%%" : "%s  буудагч   Бүтэн %d%%     Tab — жолоо авах   F — буух",
                      SpecFor(vehicle.type).name, condition);
        info.rideText = rideText;
    }
    else if (state == GameState::Playing && !InTouchMode())
    {
        const Interaction target = FindInteraction();
        if (target.vehicle >= 0)
        {
            std::snprintf(rideText, sizeof(rideText), "F — унах: %s", SpecFor(garage.At(target.vehicle).type).name);
            info.rideText = rideText;
        }
        else if (target.door >= 0)
        {
            info.rideText = world.IsDoorOpen(target.door) ? "F — хаалга хаах" : "F — хаалга нээх";
        }
        else if (target.passage >= 0)
        {
            info.rideText = PassageLabel(world.GetPassage(target.passage).kind, false);
        }
        else if (target.fence)
        {
            info.rideText = PassageLabel(PassageKind::FenceOver, false);
        }
    }

    std::vector<Vector3> markers;
    mission.Markers(markers);
    events.Markers(markers); // гэнэтийн үйл явдал: ачаа, зомбийн бөөгнөрөл, аврах хүн

    // Хамтарсан тоглолт: газрын зураг дээр өөрийн ба эсрэг багийг ялгаж харуулна (тулаангүй үед нөгөө тоглогч
    // хамт тоглогч). Хост: зочин ба түүний баг; зочин: хост ба түүний баг (snapshot).
    std::vector<MapPerson> people;
    const bool together = coopGuest || remoteActive || squad.HasGuestTeam();
    if (together)
    {
        const bool versus = battleActive || hostGone;
        if (!coopGuest)
        {
            for (const Soldier& soldier : squad.All())
            {
                if (soldier.alive) people.push_back({ soldier.mover.position, versus && soldier.guestTeam, false });
            }
            if (remoteActive && guestFigure.alive) people.push_back({ guestFigure.position, versus, true });
        }
        else
        {
            for (size_t i = 0; i < hostSoldiers.size() && i < hostSoldierSide.size(); ++i)
            {
                if (!hostSoldiers[i].alive) continue;
                const bool enemyTeam = hostSoldierSide[i] == 1 || hostSoldierSide[i] == 3;
                people.push_back({ hostSoldiers[i].position, versus && enemyTeam, false });
            }
            if (hostFigure.alive && !hostGone) people.push_back({ hostFigure.position, versus, true });
        }
    }
    SetMapPeople(together ? &people : nullptr);
    struct ClearPeople { ~ClearPeople() { SetMapPeople(nullptr); } } clearPeople; // (people энэ функцээс гарахад устна)

    if (fullMap.IsOpen())
    {
        fullMap.Draw(world, player, horde, squad, garage, markers, medKits.Positions(), info.provinceName);
        return;
    }

    if (state == GameState::Won)
    {
        DrawMissionCompleted();
        return;
    }
    if (cinematic)
    {
        airstrike.DrawOverlay(); // кинонд HUD харуулахгүй
        return;
    }

    DrawCoinPopups();
    DrawMinimap(world, player, camera.Yaw(), horde, squad, garage, markers, medKits.Positions());
    {
        PROFILE_SCOPE("draw hud");
        DrawHud(config, player, info, showDebug);
    }
    navigator.DrawHud(player.Position(), camera.Yaw());
    DrawCoopOverlay();

    if (journalOpen)
    {
        mission.DrawJournal();
    }
    if (shopOpen)
    {
        DrawShopMenu();
    }
}

// ---------------------------------------------------------------- Дэлгүүр (даалгаврын үеийн)
namespace
{
    const char* ShopOfferName(ShopOffer offer)
    {
        switch (offer)
        {
        case ShopOffer::Ammo: return "Сум +500";
        case ShopOffer::Grenades: return "Гранат +4";
        case ShopOffer::Heal: return "Амь 100% (бүх баг)";
        case ShopOffer::Recruit: return "Цэрэг элсүүлэх (энэ тоглолтод)";
        default: return "";
        }
    }

    int ShopOfferCost(ShopOffer offer)
    {
        switch (offer)
        {
        case ShopOffer::Ammo: return kAmmoCost;
        case ShopOffer::Grenades: return kGrenadeCost;
        case ShopOffer::Heal: return kHealCost;
        case ShopOffer::Recruit: return kRecruitCost;
        default: return 0;
        }
    }
}

void Game::OpenShop()
{
    shopOpen = true;
    shopMessage = nullptr;
    EnableCursor();
}

void Game::CloseShop()
{
    shopOpen = false;
    LockCursor();
}

void Game::UpdateShopMenu()
{
    if (InKeyPressed(KEY_E))
    {
        CloseShop();
        return;
    }
    for (int i = 0; i < (int)ShopOffer::Count; ++i)
    {
        if (InKeyPressed(KEY_ONE + i) || (InMousePressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(InPointer(), ShopRow(i))))
        {
            BuyShopOffer(i);
            break;
        }
    }
}

void Game::BuyShopOffer(int index)
{
    const ShopOffer offer = (ShopOffer)index;
    const int cost = ShopOfferCost(offer);
    if (progress.coins < cost)
    {
        shopMessage = "ЗООС ХҮРЭЛЦЭХГҮЙ";
        return;
    }
    switch (offer)
    {
    case ShopOffer::Recruit:
    {
        if (coopGuest)
        {
            net.SendGuestRecruit(1); // хост зочны дэргэд үүсгэж, зочныг дагуулна
            shopMessage = "ШИНЭ ЦЭРЭГ БАГТ НЭГДЛЭЭ";
            break;
        }
        // Дэлгүүрийн дотор, хаалганы дэргэд гарч ирээд тоглогчийг дагана.
        const float angle = (float)(squad.Total() % 8) * 0.785f;
        Vector3 spot = { kShopCenter.x + std::sin(angle) * 2.0f, 0.0f, kShopCenter.y + 1.2f + std::cos(angle) * 0.8f };
        world.ResolveHorizontal(spot, 0.4f, 1.8f, 0.0f);
        spot.y = world.GroundHeight(spot.x, spot.z);
        squad.Add(spot, 0.0f);
        shopMessage = "ШИНЭ ЦЭРЭГ БАГТ НЭГДЛЭЭ";
        break;
    }
    case ShopOffer::Ammo:
        ammoReserve += kAmmoPack;
        shopMessage = "СУМ НЭМЭГДЛЭЭ";
        break;
    case ShopOffer::Grenades:
        for (Arsenal& memberArsenal : arsenals) memberArsenal.AddGrenades(4);
        arsenal.AddGrenades(4);
        shopMessage = "ГРАНАТ НЭМЭГДЛЭЭ";
        break;
    case ShopOffer::Heal:
    {
        bool hurt = player.Health() < player.MaxHealth();
        for (const Soldier& soldier : squad.All())
        {
            hurt = hurt || (soldier.member >= 0 && soldier.alive && soldier.health < soldier.maxHealth);
        }
        if (coopGuest)
        {
            // Зочны AI гишүүд хост дээр: snapshot-ын амиар шалгаж, хостод дүүргүүлнэ
            for (const Net::SoldierState& s : guestView.soldiers)
            {
                hurt = hurt || (s.guest && s.member >= 0 && s.alive && s.health < player.MaxHealth() - 0.5f);
            }
            if (hurt) net.SendGuestHeal();
        }
        if (!hurt)
        {
            shopMessage = "Багийн амь аль хэдийн дүүрэн";
            return;
        }
        player.Heal(player.MaxHealth());
        for (Soldier& soldier : squad.All())
        {
            if (soldier.member >= 0 && soldier.alive) soldier.health = soldier.maxHealth;
        }
        shopMessage = "АМЬ 100% БОЛЛОО";
        break;
    }
    default:
        return;
    }
    progress.coins -= cost;
    Save();
}

void Game::DrawShopMenu() const
{
    const int width = ViewWidth(), height = ViewHeight();
    DrawRectangle(0, 0, width, height, Fade(BLACK, 0.45f));

    const int count = (int)ShopOffer::Count;
    const Rectangle first = ShopRow(0), last = ShopRow(count - 1);
    const Rectangle panel = { first.x - 20.0f, kShopTop - 96.0f, kShopPanelWidth, last.y + last.height - kShopTop + 172.0f };
    Ui::PanelAccent(panel, Ui::kAccent);
    Ui::TextBold("ДЭЛГҮҮР", (int)panel.x + 20, (int)panel.y + 18, 30, Ui::kText);
    Ui::Text("Зомби алж олсон зоосоор: тоглолт дуусахад зоос, авсан юм алга болно", (int)panel.x + 20, (int)panel.y + 56, 16,
             Ui::kTextDim);
    Ui::Chip(TextFormat("%d", progress.coins), (int)(panel.x + panel.width) - 20, (int)panel.y + 22, Ui::kAccent, Ui::kAccent, true);

    const Vector2 pointer = InPointer();
    for (int i = 0; i < count; ++i)
    {
        const ShopOffer offer = (ShopOffer)i;
        const Rectangle row = ShopRow(i);

        // Үнэ, боломжтой эсэх
        const bool maxed = false;
        const int cost = ShopOfferCost(offer);
        const bool affordable = progress.coins >= cost;

        const bool hover = !InTouchMode() && CheckCollisionPointRec(pointer, row);
        Ui::Panel(row, hover && affordable ? Fade(Ui::kAccent, 0.22f) : Ui::kPanelSoft, 10.0f);
        Ui::KeyBadge(TextFormat("%d", i + 1), (int)row.x + 12, (int)row.y + 13, 16, affordable ? Ui::kAccent : Ui::kTextDim);
        Ui::TextBold(ShopOfferName(offer), (int)row.x + 54, (int)row.y + 14, 20, Ui::kText);
        const char* price = maxed ? "MAX" : TextFormat("%d", cost);
        const int priceWidth = Ui::MeasureBold(price, 22);
        Ui::TextBold(price, (int)(row.x + row.width) - 18 - priceWidth, (int)row.y + 13, 22,
                     maxed ? Ui::kGood : affordable ? Ui::kAccent : Ui::kDanger);
        if (!maxed) Ui::Coin({ row.x + row.width - 34.0f - (float)priceWidth, row.y + row.height * 0.5f }, 9.0f);
    }

    const int footerY = (int)(last.y + last.height) + 16;
    if (shopMessage)
    {
        Ui::TextBold(shopMessage, (int)panel.x + 20, footerY, 20, Ui::kAccent);
    }
    const char* help = InTouchMode() ? "Мөрийг товшиж авна" : "1–4 эсвэл товшиж авна    E / ESC — хаах";
    Ui::Text(help, (int)panel.x + 20, footerY + 30, 16, Ui::kTextDim);
}

void Game::DrawMissionCompleted() const
{
    const int width = ViewWidth();
    const int height = ViewHeight();
    const Color gold = { 250, 214, 70, 255 };
    const Color text = { 236, 232, 220, 255 };
    auto centered = [&](const char* line, int y, int size, Color color) {
        UiTextShadow(line, (width - UiMeasure(line, size)) / 2, y, size, color);
    };

    DrawRectangle(0, 0, width, height, Fade({ 14, 40, 22, 255 }, 0.82f));
    Ui::DrawIcon(Ui::Icon::MissionComplete, { (float)width * 0.5f - 36.0f, 44.0f, 72.0f, 72.0f });
    centered("ДААЛГАВАР АМЖИЛТТАЙ БИЕЛЛЭЭ", 126, 52, gold);

    char line[200];
    std::snprintf(line, sizeof(line), "%s — ҮЕ %02d / %02d", mission.Stage().provinceName, provinceIndex + 1,
                  (int)Provinces().size());
    centered(line, 206, 32, text);

    std::snprintf(line, sizeof(line), "Биелүүлсэн даалгавар: %d / %d", mission.CompletedCount(), mission.ObjectiveCount());
    centered(line, 280, 26, text);

    if (helpedHost)
    {
        std::snprintf(line, sizeof(line), "Та %s-д тусалж аймгийг чөлөөллөө", hostName.empty() ? "хост" : hostName.c_str());
    }
    else std::snprintf(line, sizeof(line), "Ганцаарчилсан тоглолт: Gold өөрчлөгдөхгүй");
    centered(line, 318, 26, gold);
    if (lastReward > 0)
    {
        Ui::DrawIcon(Ui::Icon::Reward, { (float)(width - UiMeasure(line, 26)) * 0.5f - 38.0f, 316.0f, 30.0f, 30.0f });
    }

    if (Account::ServerShop())
    {
        std::snprintf(line, sizeof(line), "Gold: %d", Account::Gold());
        centered(line, 356, 26, text);
    }

    // Дараагийн нээгдэх үе (зочинд: өөрийн аймаг нээгдээгүй тул)
    const int next = provinceIndex + 1;
    if (helpedHost)
    {
        centered("Энэ аймагт өөрөө тоглох бол газрын зургаас нээнэ үү", 430, 24, { 150, 220, 150, 255 });
    }
    else if (next < (int)Provinces().size())
    {
        std::snprintf(line, sizeof(line), "Дараагийн үе нээгдлээ: %s (%02d / %02d)", StageFor(next).provinceName, next + 1,
                      (int)Provinces().size());
        centered(line, 430, 28, { 150, 220, 150, 255 });
    }
    else
    {
        centered("Монгол улс бүрэн чөлөөлөгдлөө!", 430, 30, { 150, 220, 150, 255 });
    }

    centered(InTouchMode() ? "Товшиж газрын зураг руу буцна уу" : "ENTER — газрын зураг руу буцах", 520, 24, text);
}

TouchState Game::MakeTouchState() const
{
    static const char* const fighterWeapons[] = { "ХУТГА", "ГАР БУУ", "АВТОМАТ", "ҮРЛЭН БУУ", "ГРАНАТ" };
    static const char* const sniperWeapons[] = { "ХУТГА", "ГАР БУУ", "ДУРАНТ БУУ" };
    static const char* const heavyWeapons[] = { "ХУТГА", "ГАР БУУ", "АВТОМАТ", "ПУУЖИН", "ГРАНАТ" };

    TouchState touch;
    touch.minimapSize = MinimapSize();
    if (paused)
    {
        touch.screen = TouchScreen::Pause;
        return touch;
    }
    if (state == GameState::Map)
    {
        // Данс, төлбөрийн цонх: газрын зургийн товчлуургүй (цонх өөрөө товшилтыг уншина)
        touch.screen = Account::Blocking() || characterSelectOpen || mapScreen.DialogOpen() ? TouchScreen::Pause : TouchScreen::CampaignMap;
        return touch;
    }
    if (state != GameState::Playing)
    {
        touch.screen = TouchScreen::Result;
        return touch;
    }
    touch.screen = journalOpen ? TouchScreen::Journal
                 : shopOpen    ? TouchScreen::Shop
                 : fullMap.IsOpen() ? TouchScreen::FullMap : TouchScreen::Gameplay;

    touch.weaponCount = arsenal.SlotCount();
    touch.weaponNames = IsSniperMember(activeMember) ? sniperWeapons : activeMember == 1 ? heavyWeapons : fighterWeapons;
    touch.weaponCurrent = arsenal.CurrentSlot();
    for (int i = 0; i < touch.weaponCount && i < 8; ++i) touch.weaponTypes[i] = (int)arsenal.SlotWeapon(i);
    touch.sniper = arsenal.Current() == WeaponType::Sniper;
    touch.driving = garage.IsDriving();
    touch.aimAssist = config.touchAimAssist;
    touch.firstPerson = progress.firstPerson;
    touch.joinRequest = joinRequestPending && !coopGuest;
    touch.showHold = !garage.IsDriving() && gunnerVehicle < 0;
    touch.showOrders = !coopGuest;
    touch.holdMarked = holdMark[activeMember];
    touch.stance = player.GetStance() == Stance::Crouch ? 1 : player.GetStance() == Stance::Prone ? 2 : 0;

    for (int i = 0; i < 3; ++i) touch.teamTiles[i] = HudTeamTile(i);
    for (int member = 0; member < kTeamSize; ++member)
    {
        TouchState::Member& entry = touch.team[member];
        entry.name = TeamMemberName(member);
        entry.active = member == activeMember;
        if (entry.active)
        {
            entry.health = (int)std::ceil(player.Health());
            entry.alive = player.IsAlive();
        }
        else
        {
            const int index = squad.FindMember(member);
            entry.alive = index >= 0 && squad.All()[(size_t)index].alive;
            entry.health = entry.alive ? (int)std::ceil(squad.All()[(size_t)index].health) : 0;
        }
    }

    // Нөхцөлт үйлдлүүд: унаа / хаалга (F), даалгаврын объект (E)
    if (garage.IsDriving() || gunnerVehicle >= 0)
    {
        touch.useLabel = garage.At(garage.IsDriving() ? garage.Driven() : gunnerVehicle).type == VehicleType::Horse ? "МОРИНООС БУУХ"
                                                                                                                  : "УНААНААС БУУХ";
        touch.useVehicle = true;
        touch.useVehicleKind = (int)garage.At(garage.IsDriving() ? garage.Driven() : gunnerVehicle).type;
    }
    else
    {
        const Interaction interaction = FindInteraction();
        if (interaction.vehicle >= 0)
        {
            const VehicleType type = garage.At(interaction.vehicle).type;
            touch.useLabel = type == VehicleType::Horse ? "МОРЬ УНАХ" : "УНААНД СУУХ";
            touch.useVehicle = true;
            touch.useVehicleKind = (int)type;
        }
        else if (interaction.door >= 0) touch.useLabel = world.IsDoorOpen(interaction.door) ? "ХААЛГА ХААХ" : "ХААЛГА НЭЭХ";
        else if (interaction.passage >= 0) touch.useLabel = PassageLabel(world.GetPassage(interaction.passage).kind, true);
        else if (interaction.fence) touch.useLabel = PassageLabel(PassageKind::FenceOver, true);
    }
    if (mission.Prompt())
    {
        touch.actLabel = "ҮЙЛДЭЛ ХИЙХ";
    }
    if (inShop)
    {
        touch.actLabel = "ДЭЛГҮҮР";
    }
    return touch;
}

void Game::DrawCoinPopups() const
{
    const Camera3D& view = camera.Get();
    const Vector3 forward = Vector3Normalize(Vector3Subtract(view.target, view.position));
    for (const CoinPopup& popup : coinPopups)
    {
        // Дээшээ хөвж, сүүлийн хэсэгт бүдгэрнэ; эхэндээ бага зэрэг томорч "үсэрнэ".
        const Vector3 world = { popup.position.x, popup.position.y + popup.age * 1.1f, popup.position.z };
        const Vector3 toPopup = Vector3Subtract(world, view.position);
        const float distance = Vector3Length(toPopup);
        if (Vector3DotProduct(toPopup, forward) < 0.5f || distance > 90.0f) continue; // камерын ард эсвэл хол
        Vector2 screen = ViewWorldToScreen(world, view);
        // Эхэндээ томорч "үсрээд" (overshoot), дээшээ удааширч хөвж, сүүлдээ бүдгэрнэ.
        const float t = std::min(1.0f, popup.age / 0.22f);
        const float pop = t < 1.0f ? 0.5f + 0.75f * t - 0.25f * t * t * 3.0f * (1.0f - t) : 1.0f;
        const float alpha = popup.age < 1.0f ? 1.0f : std::max(0.0f, 1.0f - (popup.age - 1.0f) / 0.4f);
        screen.y -= 26.0f * (1.0f - std::exp(-popup.age * 2.5f));
        const int size = (int)(std::clamp(30.0f - distance * 0.2f, 18.0f, 30.0f) * std::max(0.5f, pop));
        const bool big = popup.coins >= 20; // том зомби, аварга: илүү тод
        const char* text = TextFormat("+%d", popup.coins);
        const int textWidth = Ui::MeasureBold(text, size);
        const float coinRadius = (float)size * 0.46f;
        const float padX = (float)size * 0.32f, gap = (float)size * 0.22f;
        const float width = padX * 2.0f + coinRadius * 2.0f + gap + (float)textWidth;
        const float height = (float)size * 1.36f;
        const Rectangle pill = { screen.x - width * 0.5f, screen.y - height * 0.5f, width, height };
        DrawRectangleRounded(pill, 1.0f, 12, Fade({ 14, 12, 8, 255 }, 0.62f * alpha));
        DrawRectangleRoundedLinesEx(pill, 1.0f, 12, big ? 2.0f : 1.2f, Fade(Ui::kAccent, (big ? 0.9f : 0.55f) * alpha));
        Ui::Coin({ pill.x + padX + coinRadius, screen.y }, coinRadius, alpha);
        const int textX = (int)(pill.x + padX + coinRadius * 2.0f + gap);
        const int textY = (int)(screen.y - (float)size * 0.56f);
        Ui::TextBold(text, textX + 1, textY + 2, size, Fade(BLACK, 0.6f * alpha));
        Ui::TextBold(text, textX, textY, size, Fade(big ? Color{ 255, 214, 90, 255 } : Color{ 255, 226, 120, 255 }, alpha));
    }
}

void Game::Draw() const
{
    BeginDrawing();
    const bool mission = state != GameState::Map;
    bool post = false;
    if (mission)
    {
        if (InTouchMode()) RenderPortraits(); // (сүүдрээс өмнө: сүүдрийг арилгадаг)
        // 3D-г HUD-аас тусад нь текстурт зурж, өнгө засан (bloom, vignette) дэлгэцэнд гаргана.
        {
            PROFILE_SCOPE("draw shadows");
            RenderShadows();
        }
        const bool lowRes = renderScale < 0.999f;
        const int sceneWidth = lowRes ? std::max(160, (int)((float)ViewWidth() * renderScale)) : ViewWidth();
        const int sceneHeight = lowRes ? std::max(90, (int)((float)ViewHeight() * renderScale)) : ViewHeight();
        post = environment.BeginScene(sceneWidth, sceneHeight, lowRes);
        if (post)
        {
            DrawMissionScene();
            environment.EndScene();
        }
    }
    ViewBegin();
    if (!mission)
    {
        mapScreen.Draw(progress);
        DrawCoopMap();
        mapScreen.DrawConfirm(progress);
        if (codeEntryOpen) DrawCodeEntry();
        if (characterSelectOpen) DrawCharacterSelect();
        Account::Draw();
        // Хувилбар: хөрвүүлсэн огноо, цаг (аль хувилбар ажиллаж байгааг ялгахад)
        const char* build = "Хувилбар: " __DATE__ " " __TIME__;
        UiText(build, ViewWidth() - UiMeasure(build, 14) - 12, ViewHeight() - 22, 14, Fade(WHITE, 0.55f));
    }
    else
    {
        if (post) environment.DrawScene();
        else DrawMissionScene();
        DrawMission();
    }
    if (paused) DrawPauseMenu();
    InDrawTouch(touchState);
    ViewEnd();
    EndDrawing();
    FrameYield(); // хөтөч: зургийг дэлгэцэнд гаргана
}


// ---------------------------------------------------------------- Хамтарсан тоглолт (Wi-Fi)

namespace
{
    const float kCoopSendInterval = 1.0f / 20.0f; // snapshot, дүрийн байдлын давтамж
    const float kCoopViewRadius = 150.0f;          // зочинд илгээх зомбины хүрээ
    const float kGuestRespawnTime = 5.0f;
}

void Game::SetNotice(const std::string& text, float seconds)
{
    noticeText = text;
    recruitNotice = noticeText.c_str();
    recruitNoticeTimer = seconds;
}

std::string Game::PlayerName() const
{
    const std::string name = Account::Name(); // бүртгэлтэй нэр
    if (!name.empty()) return name;
    return std::string("Тоглогч ") + std::to_string(progress.netTag);
}

void Game::UpdateRemoteFigure(RemoteFigure& figure, const Net::AvatarState& state, float dt, bool snap)
{
    // Хурд: өмнөх мэдээнээс хойш явсан зай (дараагийн мэдээ хүртэл урагш таамаглана)
    // Хугацаа: илгээсэн цагуудын зөрүү (ирэх хугацааны савлагаа хурдыг огцом өөрчилдөггүй); хуучин бол ирсэн цаг
    const float sentGap = figure.sentAt >= 0.0f && state.sentAt > 0.0f ? state.sentAt - figure.sentAt : figure.sinceUpdate;
    if (state.sentAt > 0.0f && figure.sentAt >= 0.0f && sentGap <= 0.0f && !snap) return; // хуучин (дарааллаас хоцорсон) мэдээ
    figure.sentAt = state.sentAt;
    // Жигд хөдөлгөөний буфер (хамгийн сүүлийн 8 мэдээ)
    if (snap)
    {
        figure.sampleCount = 0;
        figure.renderTime = -1.0f;
    }
    if (state.sentAt > 0.0f)
    {
        if (figure.sampleCount == 8)
        {
            for (int i = 1; i < 8; ++i) figure.samples[i - 1] = figure.samples[i];
            --figure.sampleCount;
        }
        figure.samples[figure.sampleCount++] = { state.sentAt, state.position, state.facing };
    }
    if (!snap && sentGap > 0.01f && sentGap < 0.5f && (state.flags & Net::kAlive))
    {
        Vector3 velocity = Vector3Scale(Vector3Subtract(state.position, figure.target), 1.0f / sentGap);
        velocity.y = 0.0f;
        const float speed = Vector3Length(velocity);
        if (speed > 40.0f) velocity = Vector3Scale(velocity, 40.0f / speed);
        figure.velocity = Vector3Lerp(figure.velocity, velocity, 0.6f);
    }
    else
    {
        figure.velocity = { 0.0f, 0.0f, 0.0f };
    }
    figure.sinceUpdate = 0.0f;
    figure.target = state.position;
    figure.targetFacing = state.facing;
    figure.speed = state.speed;
    figure.aimPitch = state.aimPitch;
    figure.held = state.held;
    figure.member = state.member;
    figure.skin = state.leaderSkin;
    figure.flags = state.flags;
    figure.alive = (state.flags & Net::kAlive) != 0;
    figure.aiming = (state.flags & Net::kAiming) != 0;
    figure.vehicle = (state.flags & Net::kRiding) ? state.vehicle : -1;
    if (snap || Vector3Distance(figure.position, figure.target) > 6.0f)
    {
        figure.position = figure.target;
        figure.synced = false; // үсэрсэн (өөр цэрэг байж магадгүй): хуучин сумыг зурахгүй
        // Буфер: зөвхөн энэ мэдээ (өмнөхийн хооронд шилжүүлэхгүй)
        if (figure.sampleCount > 0)
        {
            figure.samples[0] = figure.samples[figure.sampleCount - 1];
            figure.sampleCount = 1;
        }
        figure.renderTime = -1.0f;
    }
    ShowRemoteShots(figure, state.shots, state.shotEnd);
    (void)dt;
}

void Game::ShowRemoteShots(RemoteFigure& figure, uint8_t shots, Vector3 shotEnd)
{
    // Шинэ сум бүрд: амнаас туссан цэг рүү сумны мөр, амны гал; дүр нь ухарч, буугаа онилно.
    const int fresh = figure.synced ? (uint8_t)(shots - figure.shots) : 0;
    figure.shots = shots;
    figure.synced = true;
    if (fresh <= 0 || !figure.alive) return;

    figure.facing = figure.targetFacing; // буудах зүг рүү шууд эргэнэ
    const Vector3 muzzle = HumanoidLocalToWorld(figure.position, figure.facing, HumanoidMuzzleFor((HeldItem)figure.held));
    const Vector3 direction = Vector3Normalize(Vector3Subtract(shotEnd, muzzle));
    for (int i = 0; i < std::min(fresh, 3); ++i)
    {
        tracers.Add(muzzle, shotEnd, (HeldItem)figure.held == HeldItem::Sniper ? RAYWHITE : YELLOW);
        Audio::Play((HeldItem)figure.held == HeldItem::Sniper ? Sfx::Sniper : (HeldItem)figure.held == HeldItem::Pistol ? Sfx::Pistol
                    : (HeldItem)figure.held == HeldItem::Shotgun ? Sfx::Shotgun : Sfx::Rifle, muzzle, 0.8f);
    }
    particles.Flash(muzzle, direction);
    figure.recoil = 1.0f;
    figure.aimTimer = 1.2f;
}

void Game::AdvanceRemoteFigure(RemoteFigure& figure, float dt) const
{
    figure.sinceUpdate += dt;
    const int n = figure.sampleCount;
    if (n >= 2)
    {
        // Жигд хөдөлгөөн: илгээгчийн цагаар 0.1 с хоцроож, тэр мөчийн хоёр мэдээний хооронд шилжүүлнэ (сүлжээний
        // савлагаа харагдахгүй). Цаг хэт зөрвөл шууд тааруулна, бага зөрөөг аажим.
        constexpr float kDelay = 0.1f;
        const float latest = figure.samples[n - 1].t, wanted = latest - kDelay;
        if (figure.renderTime < 0.0f || std::fabs(figure.renderTime - wanted) > 0.5f) figure.renderTime = wanted;
        else figure.renderTime += dt + (wanted - figure.renderTime) * std::min(1.0f, 1.5f * dt);
        const float t = figure.renderTime;
        Vector3 position = figure.samples[0].position;
        float facing = figure.samples[0].facing;
        if (t >= latest)
        {
            // Дараагийн мэдээ хоцорсон: сүүлийн хурдаар 0.15 с хүртэл урагш
            const RemoteFigure::Sample& a = figure.samples[n - 2];
            const RemoteFigure::Sample& b = figure.samples[n - 1];
            const float gap = std::max(0.01f, b.t - a.t);
            const float ahead = std::min(t - latest, 0.15f);
            position = Vector3Add(b.position, Vector3Scale(Vector3Subtract(b.position, a.position), ahead / gap));
            facing = b.facing;
        }
        else
        {
            for (int i = 0; i + 1 < n; ++i)
            {
                const RemoteFigure::Sample& a = figure.samples[i];
                const RemoteFigure::Sample& b = figure.samples[i + 1];
                if (t < a.t || t > b.t) continue;
                const float k = (t - a.t) / std::max(0.001f, b.t - a.t);
                position = Vector3Lerp(a.position, b.position, k);
                facing = a.facing + std::remainder(b.facing - a.facing, 2.0f * PI) * k;
                break;
            }
        }
        figure.position = position;
        figure.facing += std::remainder(facing - figure.facing, 2.0f * PI) * std::min(1.0f, 20.0f * dt);
    }
    else
    {
        // Хуучин хувилбартай тоглогч (илгээсэн цаггүй): урагш таамаглана
        const Vector3 predicted = Vector3Add(figure.target, Vector3Scale(figure.velocity, std::min(figure.sinceUpdate, 0.25f)));
        figure.position = Vector3Lerp(figure.position, predicted, std::min(1.0f, 15.0f * dt));
        figure.facing += std::remainder(figure.targetFacing - figure.facing, 2.0f * PI) * std::min(1.0f, 12.0f * dt);
    }
    figure.walk += dt * HumanoidGaitRate(figure.speed);
    figure.recoil = std::max(0.0f, figure.recoil - dt * 9.0f);
    figure.aimTimer = std::max(0.0f, figure.aimTimer - dt);
}

void Game::DrawRemoteFigure(const RemoteFigure& figure, const HumanoidLook& baseLook) const
{
    HumanoidLook look = baseLook;
    look.held = (HeldItem)figure.held;
    HumanoidPose pose;
    // Унаан дээр: суудал дээр сууна (бүхээгтэй унаанд харагдахгүй).
    int seatStyle = figure.seatStyle;
    Vector3 seat = Vector3Add(figure.position, figure.seatOffset);
    float facing = figure.facing;
    float seatHeading = facing;
    if (figure.alive && figure.vehicle >= 0 && figure.vehicle < garage.Count())
    {
        const Vehicle& vehicle = garage.At(figure.vehicle);
        seatStyle = SpecFor(vehicle.type).enclosed ? 3 : vehicle.type == VehicleType::Horse ? 2 : 1;
        seat = garage.SeatWorld(figure.vehicle, (figure.flags & Net::kPassenger) != 0);
        seatHeading = vehicle.heading;
        // Онилж буудаж байвал бие тэр зүг рүү (±120 градус), үгүй бол унааны зүгт.
        const bool aiming = figure.aiming || figure.aimTimer > 0.0f;
        facing = aiming ? vehicle.heading + std::clamp(std::remainder(figure.facing - vehicle.heading, 2.0f * PI), -2.1f, 2.1f)
                        : vehicle.heading;
    }
    if (figure.alive && seatStyle > 0)
    {
        if (seatStyle == 3 || seatStyle == 5) return;
        pose = MakeSoldierPose(0.0f, 0.0f, true, (float)GetTime(), figure.recoil, look.held);
        const bool saddle = seatStyle == 2, bed = seatStyle == 4;
        pose.hip[0] = pose.hip[1] = saddle ? 34.0f : bed ? 82.0f : 74.0f;
        pose.knee[0] = pose.knee[1] = saddle ? 46.0f : bed ? 60.0f : 84.0f;
        pose.legSpread = saddle ? 24.0f : bed ? 6.0f : 13.0f;
        pose.lean += saddle ? 4.0f : bed ? 0.0f : 14.0f;
        pose.bob = 0.0f;
        pose.seated = true;
        pose.aim = figure.aiming || figure.aimTimer > 0.0f ? 1.0f : 0.0f;
        pose.seatTwist = std::remainder(seatHeading - facing, 2.0f * PI) * RAD2DEG;
        if (figure.vehicle >= 0 && figure.vehicle < garage.Count() && !(figure.flags & Net::kPassenger))
        {
            // Нөгөө тоглогч жолоодож байна: зүүн гар жолоо барина.
            bool reins = false;
            const Vector3 rein = garage.ReinWorld(figure.vehicle, reins);
            if (reins)
            {
                const Vector3 d = Vector3Subtract(rein, { seat.x, seat.y - 0.9f, seat.z });
                const float sn = std::sin(facing), cs = std::cos(facing);
                pose.rein = 1.0f;
                pose.reinPoint = { d.x * cs - d.z * sn, d.y - pose.bob, d.x * sn + d.z * cs };
            }
        }
        DrawHumanoid({ seat.x, seat.y - 0.9f, seat.z }, facing, 1.0f, pose, look, true);
        return;
    }
    if (!figure.alive)
    {
        pose = MakeSoldierPose(0.0f, 0.0f, true, (float)GetTime(), 0.0f, look.held);
        pose.fall = 1.0f;
    }
    else
    {
        pose = MakeSoldierPose(figure.walk, figure.speed / config.walkSpeed, true, (float)GetTime(), figure.recoil, look.held);
        if (figure.flags & Net::kDowned) pose.fall = 0.8f; // унасан: хэвтэнэ
        pose.speed = figure.speed;
        pose.aim = figure.aiming || figure.aimTimer > 0.0f ? 1.0f : 0.0f;
        pose.aimPitch = figure.aimPitch * RAD2DEG;
        pose.crouch = (figure.flags & Net::kCrouch) ? 1.0f : 0.0f;
        pose.prone = (figure.flags & Net::kProne) ? 1.0f : 0.0f;
    }
    DrawBlobShadow(figure.position, 0.5f);
    DrawHumanoid(figure.position, figure.facing, 1.0f, pose, look, true);
}

Net::AvatarState Game::LocalAvatar() const
{
    Net::AvatarState state;
    state.position = player.Position();
    state.facing = player.Facing();
    state.aimPitch = player.AimPitch();
    state.speed = player.HorizontalSpeed();
    state.held = (uint8_t)player.Held();
    state.member = (uint8_t)activeMember;
    state.leaderSkin = (uint8_t)progress.leaderSkin; // нөгөө тоглогч энэ дүрээр харна
    state.sentAt = (float)GetTime();
    state.flags = (uint8_t)((player.IsAlive() ? Net::kAlive : 0) | (player.GetStance() == Stance::Crouch ? Net::kCrouch : 0) |
                            (player.GetStance() == Stance::Prone ? Net::kProne : 0) |
                            (player.Recoil() > 0.5f ? Net::kFired : 0) | (player.IsRiding() ? Net::kRiding : 0) |
                            (player.IsDowned() ? Net::kDowned : 0) |
                            ((InTouchMode() ? InMouseDown(MOUSE_BUTTON_RIGHT) : aimToggled) ? Net::kAiming : 0));
    state.health = player.Health();
    state.shots = localShots;
    state.shotEnd = localShotEnd;
    if (garage.IsDriving())
    {
        const Vehicle& vehicle = garage.At(garage.Driven());
        state.vehicle = (int8_t)garage.Driven();
        state.vehiclePosition = vehicle.position;
        state.vehicleHeading = vehicle.heading;
        state.vehicleSpeed = vehicle.speed;
        state.vehicleSteer = vehicle.steer;
        state.vehicleHealth = vehicle.wrecked ? 0.0f : vehicle.health;
    }
    else if (gunnerVehicle >= 0)
    {
        state.vehicle = (int8_t)gunnerVehicle; // унааг AI жолооч жолоодно (хост тооцно): зөвхөн суудал
        state.flags |= Net::kPassenger;
    }
    return state;
}

void Game::StartCoopGuest(int province)
{
    guestStage = StageSave{}; // хостын ахиц StageSync-ээр ирнэ
    guestSetupSent = false;
    guestSwitchTimer = 0.0f;
    StartMission(province, &guestStage);
    coopGuest = true;
    guestPlaced = false;
    guestRespawnTimer = 0.0f;
    squad.Reset(0, player.Position(), world); // зочин ганцаараа: AI цэрэггүй
    horde.Clear();
    horde.SetPuppet(true);
    hostFigure = RemoteFigure{};
    hostSoldiers.clear();
    hostSoldierSide.clear();
    guestView = Net::Snapshot{};
    battleActive = (Account::CurrentRoomRole() == Account::RoomRole::Guest && !Account::RoomCoop()) || options.battleTest;
    hostGone = false;
    if (battleActive) TraceLog(LOG_INFO, "BATTLE: joined as guest");
    SetNotice(battleActive ? "ТУЛААН! Хостын үндсэн 3 дайчныг ал"
              : Account::RoomCoop() ? TextFormat("ХАМТ ТОГЛОЛТ: үеийг хамт давбал хоёулаа %d Gold", Account::CoopReward())
                                    : "ХАМТАРСАН ТОГЛОЛТ ЭХЭЛЛЭЭ", 3.5f);
}

void Game::LeaveCoop(bool leaveRoom)
{
    if (leaveRoom && Account::CurrentRoomRole() == Account::RoomRole::Guest) Account::LeaveRoom(); // барьцаа суллагдана
    net.Stop();
    coopGuest = false;
    remoteActive = false;
    joinRequestPending = false;
    waitingForReply = false;
    horde.SetPuppet(false);
    horde.SetRemoteTarget(false, {});
}

void Game::UpdateNetwork(float dt)
{
    if (!net.Available()) return;
    // Нэвтэрсний дараа бүртгэлтэй нэр хайлтын жагсаалт, нэгдэх хүсэлт, хариуд харагдана
    if (net.LocalName() != PlayerName()) net.SetName(PlayerName());

    // Газрын зураг дээр: зочин болох гэж ойролцоох тоглолтыг хайна. Даалгаврын үед: хост болж хүсэлт хүлээнэ.
    if (state == GameState::Map)
    {
        if (net.GetRole() == Net::Role::Host) net.Stop();
        if (net.GetRole() != Net::Role::Guest || waitingForReply) net.Discover(dt);
        if (waitingForReply)
        {
            waitTimer -= dt;
            if (waitTimer <= 0.0f)
            {
                LeaveCoop();
                coopStatus = "Хариу ирсэнгүй";
            }
        }
    }
    else if (state == GameState::Playing && !coopGuest)
    {
        // Frame бүрд: дотоод сүлжээ, relay өрөө хоёуланг нээлттэй байлгана (сервер хүрэхгүй бол дахин оролдоно).
        if (net.GetRole() != Net::Role::Host) net.StopDiscovery();
        if (!Account::Enabled())
        {
            net.SetTicket(""); // туршилтын горим: дотоод сүлжээ, кодоор
            net.StartHosting(provinceIndex);
        }
        else if (Account::CurrentRoomRole() == Account::RoomRole::Host)
        {
            net.SetTicket(Account::RoomTicket()); // нээлттэй өрөө: зочин урилгагүй орно
            net.StartHosting(provinceIndex);
        }
        else if (net.GetRole() == Net::Role::Host)
        {
            net.Stop(); // ганцаарчилсан: хэн ч орж ирэхгүй
        }
    }

    netEvents.clear();
    net.Poll(netEvents);
    if (net.Reconnecting() && (coopGuest || remoteActive) && state != GameState::Map)
    {
        // Нөгөө тал тасарсан / чимээгүй: тоглолт хэвээр, буцаж ирэхийг хүлээнэ (Net: kResumeGrace)
        SetNotice(TextFormat("Холболт тасарлаа — хүлээж байна (%d с)", net.ReconnectSecondsLeft()), 1.2f);
    }
    for (const Net::Event& event : netEvents)
    {
        switch (event.type)
        {
        // ---- Хост ----
        case Net::Msg::JoinRequest:
            if (event.version != Net::kProtocolVersion)
            {
                // Өөр хувилбартай тоглогч: өгөгдөл таарахгүй тул нэгдүүлэхгүй
                net.AnswerJoin(false, provinceIndex);
                SetNotice(event.text + ": тоглоомын хувилбар өөр — хоёулаа шинэчилнэ үү", 3.5f);
            }
            else if (state == GameState::Playing && !coopGuest && !remoteActive)
            {
                joinRequestPending = true;
                joinRequester = event.text;
                joinCoop = event.coop;
                // Нээлттэй өрөө: асуулгүй шууд (газрын зураг, журнал, дэлгүүр, цэс нээлттэй байсан ч)
                // (найзын кодоор орсон бол хост Y / ✓ дарж зөвшөөрнө: код таасан танихгүй хүн шууд орохгүй)
                if ((Account::CurrentRoomRole() == Account::RoomRole::Host && !event.coop) || (options.screenshotPath && options.coopTest))
                {
                    AcceptGuest();
                }
            }
            else
            {
                net.AnswerJoin(false, provinceIndex);
            }
            break;
        case Net::Msg::GuestState:
            guestHealth = event.avatar.health;
            if (battleActive)
            {
                // Тулаан: зочны мэдэгдсэн амь хостын тооцооноос их бол (хохирлоо хасаагүй) хостынхоор. ~3 секунд (20/с)
                // дараалан илүү мэдэгдвэл хуурамч: тулаанд унасанд тооцно. (Сүлжээний хоцролтоос болох зөрүүнд тэсвэртэй.)
                if (guestShadow < 0.0f) guestShadow = event.avatar.health;
                if (event.avatar.health <= guestShadow + 0.25f * guestMemberHealth)
                {
                    guestShadow = std::min(guestShadow, event.avatar.health);
                    guestSuspicion = 0;
                }
                else
                {
                    guestHealth = guestShadow;
                    if (++guestSuspicion > 60 && !guestForcedDown)
                    {
                        guestForcedDown = true;
                        TraceLog(LOG_INFO, "BATTLE: guest reports %.0f hp, host counts %.0f: treated as down", event.avatar.health,
                                 guestShadow);
                    }
                }
            }
            if (!remoteActive) UpdateRemoteFigure(guestFigure, event.avatar, dt, true);
            else UpdateRemoteFigure(guestFigure, event.avatar, dt, false);
            remoteActive = true; // буудлагыг UpdateRemoteFigure тоолуураар харуулна
            {
                // Зочны жолоодож буй унааг энд хөдөлгөнө (хост, түүний баг сууж чадахгүй).
                const int vehicle = event.avatar.vehicle;
                const int next = vehicle >= 0 && vehicle < garage.Count() && vehicle != garage.Driven() ? vehicle : -1;
                if (guestVehicle >= 0 && guestVehicle != next) garage.ClearRemote(guestVehicle);
                guestVehicle = next;
                if (guestVehicle >= 0)
                {
                    garage.SetRemote(guestVehicle, event.avatar.vehiclePosition, event.avatar.vehicleHeading,
                                     event.avatar.vehicleSpeed, event.avatar.vehicleSteer, true);
                    garage.SyncHealth(guestVehicle, event.avatar.vehicleHealth); // зочин өөрийн унааг тооцно
                }
            }
            break;
        case Net::Msg::GuestDoor:
            if (!coopGuest && (int)event.id < world.DoorCount()) world.SetDoorOpen((int)event.id, event.accepted);
            break;
        case Net::Msg::GuestPickup:
            // Хайрцаг байсан бол (хост түрүүлж аваагүй) зочны амь дүүрнэ: давхар тооцоог шинэчилнэ
            if (!coopGuest && medKits.RemoveNear(event.position, 2.5f)) guestShadow = -1.0f;
            break;
        case Net::Msg::Projectile:
            // Нөгөө тоглогчийн гранат, пуужин: зөвхөн харагдана (хохирлыг шидсэн тал / хост тооцно).
            if (event.id == 1) grenades.Launch(event.position, event.velocity, true);
            else grenades.Throw(event.position, event.velocity, true);
            break;
        case Net::Msg::AirstrikeStart:
            if (coopGuest && state == GameState::Playing)
            {
                journalOpen = false;
                shopOpen = false;
                fullMap.Close();
                airstrike.Start(event.position, event.radius);
                airstrike.SetSkippable(false);
                state = GameState::Airstrike;
            }
            break;
        case Net::Msg::GuestHit:
        {
            int coins = 0;
            Vector3 at = {};
            // Толгойд: нэг сумаар (зомбины төрлөөс үл хамаарна); биед: хамгийн хүчтэй зэвсгийн дээд түвшнээс их биш
            float amount = event.amount;
            if (!AllowGuestHit(amount, event.headshot ? kHeadshotDamage : 600.0f)) break;
            if (horde.DamageById(event.id, amount, coins, at))
            {
                net.SendKillReward(coins, at);
            }
            break;
        }
        case Net::Msg::GuestBarrel:
        {
            float amount = event.amount;
            if (AllowGuestHit(amount, 600.0f)) barrels.Damage((int)event.id, amount);
            break;
        }
        case Net::Msg::GuestBlast:
        {
            // Гранат, пуужин: хохирол, радиус нь тэдгээрийн дээд хэмжээ; хэт олон дэлбэрэлтийг хаяна
            if (guestBlastTokens < 1.0f) break;
            guestBlastTokens -= 1.0f;
            const float radius = std::clamp(event.radius, 0.0f, std::max(kGrenadeRadius, kRocketRadius));
            const float amount = std::clamp(event.amount, 0.0f, std::max(kGrenadeMaxDamage, kRocketMaxDamage));
            horde.Explode(event.position, radius, amount);
            barrels.Blast(event.position, radius);
            particles.Explosion(event.position);
            break;
        }

        // ---- Зочин ----
        case Net::Msg::JoinReply:
            waitingForReply = false;
            if (event.version != Net::kProtocolVersion)
            {
                LeaveCoop();
                coopStatus = "Хостын тоглоомын хувилбар өөр — хоёр төхөөрөмждөө шинэ хувилбар суулгана уу";
            }
            else if (event.accepted)
            {
                hostName = event.text;
                StartCoopGuest(event.province);
            }
            else
            {
                LeaveCoop();
                coopStatus = "Хүсэлтийг татгалзлаа";
            }
            break;
        case Net::Msg::GuestDamage:
            // Сөрөг: амийн дээд хэмжээний хувь (унаагаар дайрсан)
            if (coopGuest) player.TakeDamage(event.amount < 0.0f ? -event.amount * player.MaxHealth() : event.amount);
            break;
        case Net::Msg::VehicleHit:
        {
            // Нөгөө тал манай тооцдог унаанд оносон (хост: зочны мэдэгдлийг хязгаарлана)
            float amount = event.amount;
            if (!coopGuest && !AllowGuestHit(amount, 1000.0f)) break;
            if ((int)event.id < garage.Count() && amount > 0.0f && amount < 5000.0f) garage.Damage((int)event.id, amount);
            break;
        }
        case Net::Msg::WorldEvent:
            if (coopGuest)
            {
                if (event.province <= 1) events.AddCrate((uint16_t)event.id, event.position, event.amount);
                else events.AddMarker(event.position, 120.0f);
                ShowWorldEvent(event.province, event.position);
            }
            break;
        case Net::Msg::Revive:
        {
            // Нөгөө тал (эсвэл хостын AI дайчин) дэргэд байгаа үед л: хүссэн үедээ өөрийгөө босгохоос
            bool near = HorizontalDistance(player.Position(), (coopGuest ? hostFigure : guestFigure).position) < 4.0f;
            for (const RemoteFigure& soldier : hostSoldiers)
            {
                near = near || (coopGuest && HorizontalDistance(player.Position(), soldier.position) < 4.0f);
            }
            if (!near) break;
            if (!coopGuest) guestShadow = -1.0f; // (хост: зочныг манай дайчин босгосныг доор тооцно)
            if (player.IsDowned())
            {
                player.Revive(0.3f);
                SetNotice("НӨХӨР ЧИНЬ БОСГОЛОО!", 2.0f);
            }
            break;
        }
        case Net::Msg::CrateTaken:
            if (!coopGuest)
            {
                // Хост: зочин хайрцаг авахыг хүслээ. Хост түрүүлж аваагүй бол зочных.
                if (events.RemoveCrate((uint16_t)event.id))
                {
                    net.SendCrateGrant((int)event.id);
                    guestShadow = -1.0f; // (зочны амь 40% нэмэгдэнэ)
                }
            }
            else
            {
                events.RemoveCrate((uint16_t)event.id); // хост түрүүлж авсан
                requestedCrates.erase(std::remove(requestedCrates.begin(), requestedCrates.end(), (int)event.id),
                                      requestedCrates.end());
            }
            break;
        case Net::Msg::CrateGrant:
            // Зочин: хост хайрцгийг надад олголоо
            if (coopGuest && events.RemoveCrate((uint16_t)event.id)) GiveCrateReward();
            requestedCrates.erase(std::remove(requestedCrates.begin(), requestedCrates.end(), (int)event.id), requestedCrates.end());
            break;
        case Net::Msg::SoldierLost:
            // Зочин: хост дээр манай Gold-ийн цэрэг үхэв
            if (coopGuest && event.id > 0)
            {
                if (Account::ServerShop()) Account::LoseSoldiers((int)event.id);
                else progress.soldiers = std::max(0, progress.soldiers - (int)event.id);
                SetNotice("Gold-оор авсан цэрэг үхлээ — цэргийн тоо хасагдлаа", 2.5f);
            }
            break;
        case Net::Msg::GuestHitHuman:
            // Хост: зочин манай тоглогч эсвэл багийн дайчинд оносон (элсүүлсэн цэрэгт тусахгүй)
            if (!coopGuest && battleActive)
            {
                // Хамгийн хүчтэй сум (дурантай буу, дээд түвшин, толгойд 4 дахин): ~2040. Сөрөг: унаагаар (дээд хэмжээний хувь)
                float amount = event.amount;
                if (!AllowGuestHit(amount, 2100.0f)) break;
                if (amount < 0.0f) amount = std::max(amount, -0.5f);
                const bool share = amount < 0.0f;
                if (event.id == Net::kHostPlayer) player.TakeDamage(share ? -amount * player.MaxHealth() : amount);
                else if ((int)event.id < squad.Total())
                {
                    const Soldier& target = squad.All()[(size_t)event.id];
                    if (target.alive && target.member >= 0 && !target.guestTeam)
                    {
                        if (share) squad.DamageFraction((int)event.id, std::min(1.0f, -amount));
                        else squad.Damage((int)event.id, amount);
                    }
                }
            }
            break;
        case Net::Msg::GuestTakeover:
            // Зочин: хост зөвшөөрлөө: AI гишүүний байранд (солих эсвэл унасан) — хостынхтой адил шилжилт
            if (coopGuest && (int)event.id < kTeamSize)
            {
                const int member = (int)event.id;
                const bool fell = !player.IsAlive();
                arsenals[activeMember] = arsenal;
                activeMember = member;
                arsenal = arsenals[member];
                player.Assume(event.position, player.Facing(), std::max(1.0f, event.amount), player.MaxHealth());
                player.SetLook(TeamMemberLook(member, progress.leaderSkin));
                player.SetHeld(HeldFor(arsenal.Current()));
                camera.Snap(event.position);
                aimToggled = false;
                guestSwitchTimer = 0.0f;
                if (fell) SetNotice("Та дараагийн дайчныхаа байранд орлоо", 2.5f);
                TraceLog(LOG_INFO, "COOP: guest now controls member %d (%.0f hp)", member, event.amount);
            }
            break;
        case Net::Msg::StageSync:
            // Зочин: өрөөний ахиц (журнал, газрын зургийн тэмдэглэгээ хосттой ижил)
            if (coopGuest)
            {
                StageSave stage;
                if (ParseStageText(event.text, stage))
                {
                    if (stage.done != guestStage.done) TraceLog(LOG_INFO, "COOP: room progress %u from host", stage.done);
                    mission.SyncFrom(stage);
                }
            }
            break;
        case Net::Msg::GuestSwitch:
            // Хост: зочин өөрийн AI гишүүн рүү шилжих хүсэлт
            if (!coopGuest && remoteActive && guestFigure.alive)
            {
                const std::vector<Soldier>& soldiers = squad.All();
                for (size_t i = 0; i < soldiers.size(); ++i)
                {
                    if (soldiers[i].guestTeam && soldiers[i].alive && soldiers[i].member == (int)event.id)
                    {
                        GuestTakeOver((int)i, false, event.accepted);
                        break;
                    }
                }
            }
            break;
        case Net::Msg::GuestRecruit:
            // Хост: зочны элсүүлсэн цэрэг: зочны дэргэд гарч, зочныг дагаж зомбитой байлдана. Зочин дэлгүүрт (хостын
            // дэлгэц дээр) байгаа үед, нэг нэгээр, зочны багт 8-аас ихгүй (хуурамчаар олноор нэмэхээс).
            if (!coopGuest && remoteActive && guestRecruitCooldown <= 0.0f &&
                world.InSafeRoom(guestFigure.position.x, guestFigure.position.z))
            {
                int recruits = 0;
                for (const Soldier& soldier : squad.All()) recruits += soldier.alive && soldier.guestTeam && soldier.member < 0 ? 1 : 0;
                guestRecruitCooldown = 0.4f;
                for (int k = 0; k < std::min((int)event.id, std::max(0, std::min(1, 8 - recruits))); ++k)
                {
                    const float angle = (float)(squad.Total() % 8) * 0.785f;
                    Vector3 spot = { guestFigure.position.x + std::sin(angle) * 2.5f, 0.0f,
                                     guestFigure.position.z + std::cos(angle) * 2.5f };
                    world.ResolveHorizontal(spot, 0.4f, 1.8f, 0.0f);
                    spot.y = world.GroundHeight(spot.x, spot.z);
                    squad.Add(spot, guestFigure.facing);
                    squad.All().back().guestTeam = true;
                }
            }
            break;
        case Net::Msg::GuestHeal:
            // Дэлгүүрт байгаа үед, 5 секундэд нэг удаа
            if (!coopGuest && guestHealCooldown <= 0.0f && world.InSafeRoom(guestFigure.position.x, guestFigure.position.z))
            {
                guestHealCooldown = 5.0f;
                guestShadow = -1.0f;
                for (Soldier& soldier : squad.All())
                {
                    if (soldier.guestTeam && soldier.alive) soldier.health = soldier.maxHealth;
                }
            }
            break;
        case Net::Msg::GuestSetup:
            // Хост: зочны гишүүдийн амь (Gold-ийн сайжруулалт), Gold-оор авсан цэргүүд
            if (!coopGuest && !guestSetup.pending)
            {
                // Серверээс зочны жинхэнэ цэрэг, амийн түвшин ирэхийг хүлээнэ (ApplyGuestSetup, UpdateCoopMission)
                guestSetup.pending = true;
                guestSetup.health = event.amount;
                guestSetup.soldiers = (int)event.id;
                guestSetup.waited = 0.0f;
            }
            break;
        case Net::Msg::BattleOver:
            // Зочин: хост тулааны үр дүнг тооцлоо; серверт өөрөө мэдэгдэж баталгаажуулна
            if (coopGuest && battleActive)
            {
                // Хостын мэдэгдлийг шууд давтахгүй: өөрийн дэлгэц дээрх байдлаар шалгаж серверт мэдэгдэнэ (зөрвөл хүчингүй)
                const bool won = event.id == 1;
                TraceLog(LOG_INFO, "BATTLE: host says guest %s", won ? "won" : "lost");
                verdictPending = true;
                verdictHostSaysGuest = won;
                verdictTimer = 0.0f;
                battleActive = false;
                SetNotice(won ? "ТУЛААНД ЯЛЛАА! Хост болж байна..." : "ТУЛААНД ЯЛАГДЛАА", 4.0f);
            }
            break;
        case Net::Msg::KillReward:
            if (coopGuest)
            {
                progress.coins += (int)event.amount;
                progress.kills += 1;
                missionKills += 1;
                coinPopups.push_back({ event.position, (int)event.amount, 0.0f });
            }
            break;
        case Net::Msg::BarrelExploded:
            if (coopGuest) barrels.Trigger((int)event.id);
            break;
        case Net::Msg::MissionWon:
            if (coopGuest && (state == GameState::Playing || state == GameState::Airstrike))
            {
                airstrike.Stop();
                // Зочин зөвхөн хостод тусална: аймаг зочны хадгалалтад чөлөөлөгдөхгүй, шагнал авахгүй (устгасан
                // зомбины зоос л үлдэнэ). Өөрөө тоглох бол аймгийг өөрийн эрхээр нээнэ.
                lastReward = 0;
                helpedHost = true;
                Save();
                state = GameState::Won;
                // Хамт тоглолт: өрөөнөөс гарсан гэж мэдэгдэхгүй (хост дуусгахад сервер хоёуланд нь шагнал өгч өрөөг хаана)
                const bool coop = Account::RoomCoop();
                if (coop) SetNotice(TextFormat("ХАМТ ДАВЛАА! Хоёулаа +%d Gold", Account::CoopReward()), 5.0f);
                LeaveCoop(!coop);
            }
            break;

        case Net::Msg::Leave:
            if (coopGuest && battleActive)
            {
                // Тулааны үеэр хост гарсан (тасарсан): энэ төхөөрөмж ертөнцийг үргэлжлүүлж хостын багийг AI тулалдуулна
                TakeOverBattle();
            }
            else if (coopGuest)
            {
                LeaveCoop();
                coopStatus = "Хост тоглолтоос гарлаа";
                if (state == GameState::Playing) ReturnToMap();
            }
            else if (remoteActive || joinRequestPending)
            {
                if (battleActive && guestFigure.alive)
                {
                    // Тулааны үеэр зочин гарсан (тасарсан): түүний дүр 1 минут хөдлөхгүй, дараа нь AI тулалдана
                    squad.AddGuestFighter(guestFigure.member, guestFigure.position, guestFigure.facing,
                                          std::max(1.0f, guestHealth), 60.0f);
                    SetNotice(joinRequester + " тасарлаа — 1 минутын дараа түүний багийг AI удирдана", 4.0f);
                }
                else
                {
                    SetNotice(joinRequester + " тоглолтоос гарлаа", 2.5f);
                }
                remoteActive = false;
                joinRequestPending = false;
                guestFigure = RemoteFigure{};
                horde.SetRemoteTarget(false, {});
            }
            else if (waitingForReply)
            {
                LeaveCoop();
                coopStatus = event.text.empty() ? "Холбогдож чадсангүй" : event.text;
            }
            break;
        default:
            break;
        }
    }
}

// ---------------------------------------------------------------- Тулаан (нээлттэй өрөө)

void Game::DamageGuest(float amount)
{
    // Сөрөг: амийн дээд хэмжээний хувь (унаагаар дайрсан)
    net.SendGuestDamage(amount);
    if (battleActive && guestShadow >= 0.0f)
    {
        // Зочны тоглоомтой ижил томьёо (Player::TakeDamage): багийн бат бөхөд хуваана; 0 хүрвэл "унасан" (1 амьтай)
        guestShadow -= (amount < 0.0f ? -amount * guestMemberHealth : amount) / kTeamToughness;
        guestShadow = std::max(guestShadow, 1.0f);
    }
}

bool Game::AllowGuestHit(float& amount, float cap)
{
    // Хамгийн хурдан зэвсгийн давтамжаас (үрлэн буу 8 үрэл, автомат ~10/с) их бол хуурамч мэдэгдэл
    if (guestHitTokens < 1.0f) return false;
    guestHitTokens -= 1.0f;
    if (!(amount == amount)) return false; // NaN
    amount = std::clamp(amount, -cap, cap);
    return true;
}

void Game::ApplyGuestSetup()
{
    // Зочны мэдэгдсэн амь, Gold-ийн цэргийг серверийн мэдээгээр хязгаарлана (тоглоомоо өөрчилж нэмэхээс)
    const int serverSoldiers = Account::RoomGuestSoldiers(), serverLevel = Account::RoomGuestHealthLevel();
    const float maxHealth = config.playerBaseHealth +
                            kHealthPerLevel * (float)(serverLevel >= 0 ? serverLevel : ShopMaxLevel(ShopItem::Health));
    guestMemberHealth = std::clamp(guestSetup.health, 50.0f, maxHealth);
    for (Soldier& soldier : squad.All())
    {
        if (soldier.guestTeam && soldier.member >= 0) soldier.health = soldier.maxHealth = guestMemberHealth;
    }
    int soldiers = std::clamp(guestSetup.soldiers, 0, kMaxSoldiers);
    if (serverSoldiers >= 0) soldiers = std::min(soldiers, serverSoldiers);
    for (int k = 0; k < soldiers; ++k)
    {
        const float angle = (float)k * 1.3f;
        Vector3 spot = { guestFigure.position.x + std::sin(angle) * 3.0f, 0.0f, guestFigure.position.z + std::cos(angle) * 3.0f };
        world.ResolveHorizontal(spot, 0.4f, 1.8f, 0.0f);
        spot.y = world.GroundHeight(spot.x, spot.z);
        squad.Add(spot, guestFigure.facing);
        squad.All().back().guestTeam = true;
        squad.All().back().bought = true; // үхвэл зочинд SoldierLost
    }
    guestSetup = PendingGuestSetup{};
}

int Game::GuestViewVerdict() const
{
    // Зочны дэлгэц дээрх байдал: хостын баг (хост + түүний 2 дайчин), зочны баг (өөрөө + 2 дайчин, хост тооцдог)
    int hostAlive = (guestView.host.flags & Net::kAlive) ? 1 : 0;
    int guestAlive = player.IsAlive() ? 1 : 0;
    for (const Net::SoldierState& soldier : guestView.soldiers)
    {
        if (!soldier.alive || soldier.member < 0) continue;
        if (soldier.guest) ++guestAlive;
        else ++hostAlive;
    }
    if (hostAlive == 0 && guestAlive > 0) return 1;
    if (guestAlive == 0 && hostAlive > 0) return 0;
    return -1;
}

int Game::TeamAlive(bool hostTeam) const
{
    // Үндсэн 3 дайчин: удирдаж буй тоглогч + багийн гишүүд (элсүүлсэн цэрэг тооцогдохгүй)
    int alive = 0;
    if (hostTeam) alive += player.IsAlive() ? 1 : 0;
    else alive += remoteActive && guestFigure.alive && !guestForcedDown ? 1 : 0;
    for (const Soldier& soldier : squad.All())
    {
        if (soldier.alive && soldier.member >= 0 && soldier.guestTeam != hostTeam) ++alive;
    }
    return alive;
}

int Game::RaycastHumans(Ray ray, float minDistance, float maxDistance, float& distance, bool& head) const
{
    // Бие (цээж) ба толгойг бөмбөлгөөр ойролцоолно
    int best = -1;
    distance = maxDistance;
    auto test = [&](Vector3 feet, int code) {
        const struct { Vector3 center; float radius; bool head; } parts[] = {
            { Vector3Add(feet, { 0.0f, 1.62f, 0.0f }), 0.2f, true },
            { Vector3Add(feet, { 0.0f, 1.05f, 0.0f }), 0.42f, false },
        };
        for (const auto& part : parts)
        {
            const RayCollision hit = GetRayCollisionSphere(ray, part.center, part.radius);
            if (hit.hit && hit.distance > minDistance && hit.distance < distance)
            {
                distance = hit.distance;
                head = part.head;
                best = code;
            }
        }
    };
    if (!coopGuest)
    {
        if (remoteActive && guestFigure.alive) test(guestFigure.position, kGuestTarget);
        for (int i = 0; i < squad.Total(); ++i)
        {
            const Soldier& soldier = squad.All()[(size_t)i];
            if (soldier.alive && soldier.guestTeam && soldier.member >= 0) test(soldier.mover.position, i);
        }
    }
    else
    {
        if (hostFigure.alive) test(hostFigure.position, (int)Net::kHostPlayer);
        for (size_t i = 0; i < hostSoldiers.size() && i < hostSoldierSide.size(); ++i)
        {
            if (hostSoldierSide[i] == 1 && hostSoldiers[i].alive) test(hostSoldiers[i].position, (int)i);
        }
    }
    return best;
}

void Game::AutoFight(float dt)
{
    // Хамгийн ойрын харагдах дайсан (эсрэг багийн хүн эсвэл зомби) руу онилж буудна
    const Vector3 eye = Vector3Add(player.Position(), { 0.0f, 1.5f, 0.0f });
    Vector3 best = {};
    float bestDistance = 70.0f;
    auto consider = [&](Vector3 chest) {
        const float d = Vector3Distance(eye, chest);
        if (d < bestDistance && d > 1.0f)
        {
            const Vector3 direction = Vector3Scale(Vector3Subtract(chest, eye), 1.0f / d);
            if (world.RayDistance({ eye, direction }, d) >= d - 0.5f)
            {
                bestDistance = d;
                best = chest;
            }
        }
    };
    if (!coopGuest)
    {
        if (remoteActive && guestFigure.alive) consider(Vector3Add(guestFigure.position, { 0.0f, 1.2f, 0.0f }));
        for (const Soldier& soldier : squad.All())
        {
            if (soldier.alive && soldier.guestTeam) consider(Vector3Add(soldier.mover.position, { 0.0f, 1.2f, 0.0f }));
        }
    }
    else
    {
        if (hostFigure.alive) consider(Vector3Add(hostFigure.position, { 0.0f, 1.2f, 0.0f }));
        for (size_t i = 0; i < hostSoldiers.size() && i < hostSoldierSide.size(); ++i)
        {
            if (hostSoldierSide[i] == 1 && hostSoldiers[i].alive) consider(Vector3Add(hostSoldiers[i].position, { 0.0f, 1.2f, 0.0f }));
        }
    }
    const int zombie = horde.Nearest(player.Position(), bestDistance);
    if (zombie >= 0) consider(horde.At(zombie).Chest());
    if (bestDistance >= 70.0f) return;

    const Camera3D& view = camera.Get();
    const Vector3 direction = Vector3Normalize(Vector3Subtract(best, view.position));
    const float wantedYaw = std::atan2(direction.x, direction.z);
    const float wantedPitch = -std::asin(std::clamp(direction.y, -1.0f, 1.0f));
    const float pull = 1.0f - std::exp(-9.0f * dt);
    camera.AddView(std::remainder(wantedYaw - camera.Yaw(), 2.0f * PI) * pull, (wantedPitch - camera.Pitch()) * pull);
    const bool aligned = std::fabs(std::remainder(wantedYaw - camera.Yaw(), 2.0f * PI)) < 0.08f;
    autoFire = aligned && arsenal.UsesAmmo();
    if (arsenal.Current() == WeaponType::Knife || arsenal.Current() == WeaponType::Grenade)
    {
        arsenal.Select(arsenal.SlotWeapon(std::min(2, arsenal.SlotCount() - 1))); // галт зэвсэг
    }
}

void Game::GuestTakeOver(int index, bool leaveDead, bool hold)
{
    // Хост: зочин index дахь AI гишүүнийхээ биед орно; зочны хуучин бие тэр гишүүн болж (AI) үлдэнэ,
    // унасан бол унасан хэвээр. Хостын SwitchMember-тэй ижил солилцоо.
    Soldier& body = squad.All()[(size_t)index];
    const Vector3 position = body.mover.position;
    const float health = body.health;
    const int member = body.member;
    body.mover.position = guestFigure.position;
    body.mover.facing = guestFigure.facing;
    body.member = guestFigure.member;
    body.health = leaveDead ? 0.0f : std::max(1.0f, guestHealth);
    body.alive = !leaveDead;
    body.deathTimer = 0.0f;
    body.holding = hold && !leaveDead;
    body.holdPoint = guestFigure.position;
    body.routeFrom = body.routeTo = -2;
    body.stuckTimer = 0.0f;
    body.bestDistance = 0.0f;
    body.moving = false;
    body.riding = -1;
    body.boarding = false;
    body.seatStyle = 0;
    body.frozen = 0.0f;
    net.SendTakeover(position, health, member);
    TraceLog(LOG_INFO, "COOP: guest moved to member %d (left %s)", member, leaveDead ? "dead" : "as AI");
    // Зочин шинэ биендээ орох хүртэл (дараагийн GuestState) хуучин байрлалаар нь онохгүй
    guestFigure.position = position;
    guestFigure.target = position;
    guestFigure.member = (uint8_t)member;
    guestHealth = health;
    guestShadow = health; // шинэ бие: хостын давхар тооцоо түүний амиар
    guestForcedDown = false;
}

void Game::GuestTeamInput()
{
    // Зочин: өөрийн баг (хост дээрх AI гишүүд). Дараалал нь HUD-ын багийн карттай ижил: өөрөө, дараа нь гишүүд.
    guestSwitchTimer = std::max(0.0f, guestSwitchTimer - GetFrameTime());
    int order[3] = { activeMember, -1, -1 };
    bool alive[3] = { true, false, false };
    int count = 1;
    for (const Net::SoldierState& s : guestView.soldiers)
    {
        if (!s.guest || s.member < 0 || count >= 3) continue;
        order[count] = s.member;
        alive[count] = s.alive != 0;
        ++count;
    }
    if (InKeyPressed(KEY_H) && !garage.IsDriving())
    {
        holdMark[activeMember] = !holdMark[activeMember];
        if (holdMark[activeMember]) ChatterPlayer(VoiceLine::Hold, true);
        SetNotice(holdMark[activeMember] ? "ЭНД БАЙНА — өөр гишүүн рүү шилжинэ үү (Tab)" : "ЭНД БАЙХ ТУШААЛ ЦУЦЛАГДЛАА", 2.5f);
    }
    int target = -1;
    if (InKeyPressed(KEY_TAB))
    {
        for (int step = 1; step < count && target < 0; ++step)
        {
            if (alive[step]) target = order[step];
        }
    }
    for (int i = 1; i < count && InTouchMode(); ++i)
    {
        if (InKeyPressed(KEY_F1 + i) && alive[i]) target = order[i];
    }
    if (target >= 0 && guestSwitchTimer <= 0.0f && !garage.IsDriving() && player.IsAlive())
    {
        net.SendGuestSwitch(target, holdMark[activeMember]);
        holdMark[activeMember] = false;
        guestSwitchTimer = 1.0f;
    }
}

void Game::TakeOverBattle()
{
    // Тулааны үеэр хост тасарсан (санамсаргүй ч байж болно): шууд ялагдал биш, тулаан тулаанаараа шийдэгдэнэ.
    // Энэ төхөөрөмж ертөнцийг (хостын ахицаар) үргэлжлүүлж, хоёр багийн дайчдыг сүүлийн байрлал, амиар нь
    // байрлуулна: хостын үндсэн дайчдыг AI тулалдуулна. Серверт энэ тоглогч зочин хэвээр (LeaveRoom биш): үр дүнг
    // тулаан дуусахад мэдэгдэнэ. Хост санаатай гарсан бол сервер аль хэдийн зочныг ялсанд тооцсон.
    struct Fighter
    {
        int member;
        Vector3 position;
        float facing, health;
    };
    std::vector<Fighter> enemies, allies;
    if (hostFigure.alive) enemies.push_back({ hostFigure.member, hostFigure.position, hostFigure.facing, guestView.host.health });
    for (const Net::SoldierState& s : guestView.soldiers)
    {
        if (s.member < 0 || s.alive == 0) continue;
        const float health = s.alive == 2 ? 10.0f : s.health; // унасан: бага амьтай
        (s.guest ? allies : enemies).push_back({ s.member, s.position, s.facing, health });
    }
    const int province = Account::RoomProvince() >= 0 ? Account::RoomProvince() : provinceIndex;
    if (enemies.empty() || province < 0 || province >= (int)progress.stages.size())
    {
        // Хостын баг аль хэдийн унасан: шууд ялна (сервер 60 секунд хүлээгээд баталгаажуулна)
        net.Stop();
        hostGone = true;
        battleActive = false;
        Account::ReportBattle(true);
        SetNotice("Хост гарлаа — ялалт баталгаажиж байна (1 минут хүртэл)", 6.0f);
        return;
    }
    StageSave stage;
    const bool hasStage = ParseStageText(Account::RoomStage(), stage);
    const Vector3 me = player.Position();
    const float myHealth = player.Health();
    const int coins = progress.coins, ammo = ammoReserve, enemySkin = hostFigure.skin;

    // Хамтарсан тоглолтоос гарна (серверт өрөөнөөс гарсан гэж мэдэгдэхгүй: тулааны үр дүн хүлээгдэж байна)
    net.Stop();
    coopGuest = false;
    remoteActive = false;
    joinRequestPending = false;
    waitingForReply = false;
    horde.SetPuppet(false);
    horde.SetRemoteTarget(false, {});
    StartMission(province, hasStage ? &stage : nullptr); // өөрийн хадгалалтыг өөрчлөхгүй
    progress.coins = coins;
    ammoReserve = ammo;

    // Өөрийн баг: удирдаж буй дайчин ба AI гишүүд сүүлийн байрлалдаа (тэр үед унасан гишүүд үхсэн)
    player.Teleport(me);
    if (myHealth < player.MaxHealth()) player.TakeDamage(player.MaxHealth() - std::max(1.0f, myHealth));
    for (Soldier& soldier : squad.All())
    {
        if (soldier.member < 0 || soldier.guestTeam) continue;
        const auto ally = std::find_if(allies.begin(), allies.end(), [&](const Fighter& f) { return f.member == soldier.member; });
        if (ally == allies.end())
        {
            soldier.alive = false;
            soldier.health = 0.0f;
            continue;
        }
        soldier.mover.position = ally->position;
        soldier.mover.facing = ally->facing;
        soldier.health = std::clamp(ally->health, 1.0f, soldier.maxHealth);
    }
    // Хостын баг: AI дайчид (дайсны тал)
    for (const Fighter& enemy : enemies)
    {
        squad.AddGuestFighter(enemy.member, enemy.position, enemy.facing, std::max(1.0f, enemy.health), 0.0f);
    }
    guestFigure.skin = (uint8_t)enemySkin; // дайсны 1-р дайчны дүр (SetLeaderSkins)
    battleActive = true;
    guestTeamSpawned = true;
    takeoverBattle = true;
    takeoverTimer = 300.0f;
    takeoverCooldown = 3.0f;
    TraceLog(LOG_INFO, "BATTLE: host left, guest took over (%d enemies, %d allies)", (int)enemies.size(), (int)allies.size());
    SetNotice("Хост тасарлаа — түүний багийг AI удирдана. Тулааныг үргэлжлүүл!", 5.0f);
}

void Game::WinBattleAsHost()
{
    // Ялагчийн үхсэн дайчид сэргэнэ; зочны баг арилна, өрөө дараагийн зочныг хүлээнэ
    battleActive = false;
    for (Soldier& soldier : squad.All())
    {
        if (soldier.member >= 0 && !soldier.guestTeam && !soldier.alive)
        {
            soldier.alive = true;
            soldier.health = soldier.maxHealth;
            soldier.deathTimer = 0.0f;
        }
    }
    // Ялагдсан багийн элсүүлсэн цэргүүд ялагчид шилжинэ
    for (Soldier& soldier : squad.All())
    {
        if (soldier.guestTeam && soldier.member < 0 && soldier.alive) soldier.guestTeam = false;
    }
    squad.RemoveGuestTeam();
    guestTeamSpawned = true; // ялагдсан зочин гартал (UpdateCoopMission) дахин үүсгэхгүй
    SetNotice("ТУЛААНД ЯЛЛАА! Дайсны баг бүгд унав — дайчид тань сэргэлээ", 4.0f);
}

void Game::MigrateToHost()
{
    // Зочин ялав: өрөөний ахиц, өмнөх хостын цэргүүдийг аваад энэ төхөөрөмж хост болж үргэлжлүүлнэ
    // (өөрийн баг, тоглолтын зоос, нөөц сум хэвээр; үхсэн дайчид сэргэнэ).
    const int province = Account::RoomProvince();
    if (province < 0 || province >= (int)progress.stages.size())
    {
        ReturnToMap();
        return;
    }
    StageSave stage;
    const bool hasStage = ParseStageText(Account::RoomStage(), stage);
    const int coins = progress.coins, ammo = ammoReserve, recruits = Account::RoomRecruits();
    LeaveCoop(); // (CurrentRoomRole нь Host болсон тул өрөөнөөс гарахгүй)
    if (hasStage) progress.stages[(size_t)province] = stage;
    StartMission(province);
    progress.coins = coins;
    ammoReserve = ammo;
    for (int i = 0; i < recruits; ++i)
    {
        const float angle = (float)i * 1.1f;
        Vector3 spot = { player.Position().x + std::sin(angle) * 3.0f, 0.0f, player.Position().z + std::cos(angle) * 3.0f };
        world.ResolveHorizontal(spot, 0.4f, 1.8f, 0.0f);
        spot.y = world.GroundHeight(spot.x, spot.z);
        squad.Add(spot, angle);
    }
    Save();
    SetNotice("ТУЛААНД ЯЛЛАА! Та одоо энэ өрөөний хост — даалгаврыг үргэлжлүүлнэ", 5.0f);
}

void Game::UpdateBattle(float dt)
{
    // Хост: өрөөний ахиц, амьд элсүүлсэн цэргийн тоог серверт (зочин ялбал тэр үргэлжлүүлнэ)
    if (!coopGuest && Account::CurrentRoomRole() == Account::RoomRole::Host)
    {
        stageUploadTimer -= dt;
        if (stageUploadTimer <= 0.0f)
        {
            stageUploadTimer = 20.0f;
            int recruits = 0;
            for (const Soldier& soldier : squad.All()) recruits += soldier.alive && soldier.member < 0 ? 1 : 0;
            Account::UploadStage(StageText(progress.stages[(size_t)provinceIndex]), recruits);
        }
    }

    // Серверийн шийдвэр
    const Account::BattleResult result = Account::TakeBattleResult();
    if (result == Account::BattleResult::Won && coopGuest && Account::CurrentRoomRole() == Account::RoomRole::Host)
    {
        MigrateToHost();
        return;
    }
    if (result == Account::BattleResult::Won)
    {
        SetNotice(TextFormat("ТУЛААНЫ ЯЛАЛТ БАТАЛГААЖЛАА · %d Gold", Account::Gold()), 4.0f);
    }
    else if ((result == Account::BattleResult::Lost || result == Account::BattleResult::Void) && coopGuest)
    {
        const bool lost = result == Account::BattleResult::Lost;
        LeaveCoop();
        ReturnToMap();
        coopStatus = lost ? "Тулаанд ялагдлаа" : "Тулааны үр дүн зөрсөн: хүчингүй боллоо";
        return;
    }
    else if (result == Account::BattleResult::Void)
    {
        SetNotice("Тулааны үр дүн зөрсөн тул хүчингүй боллоо", 4.0f);
    }

    if (!battleActive)
    {
        squad.SetBattle(false, {}, {});
        autopilot = autoFire = false;
        idleTimer = 0.0f;
        return;
    }

    // 1 минут оролтгүй (санаатай зогссон, тасарсан төхөөрөмж): AI тулалдана
    idleTimer = InActivity() ? 0.0f : idleTimer + dt;
    const bool wasAuto = autopilot;
    autopilot = idleTimer >= 60.0f && player.IsAlive();
    if (autopilot && !wasAuto) SetNotice("Та 1 минут хөдлөөгүй: AI таны оронд тулалдаж байна", 4.0f);
    autoFire = false;
    if (autopilot) AutoFight(dt);

    if (coopGuest) return; // доорх нь хостын тооцоо

    // AI дайчдын бай: хоёр багийн үндсэн дайчид (элсүүлсэн цэрэг биш)
    std::vector<HumanTarget> hostSide, guestSide;
    if (player.IsAlive()) hostSide.push_back({ Vector3Add(player.Position(), { 0.0f, 1.3f, 0.0f }), kHostTarget });
    for (int i = 0; i < squad.Total(); ++i)
    {
        const Soldier& soldier = squad.All()[(size_t)i];
        if (!soldier.alive || soldier.member < 0) continue;
        (soldier.guestTeam ? guestSide : hostSide).push_back({ Vector3Add(soldier.mover.position, { 0.0f, 1.3f, 0.0f }), i });
    }
    if (remoteActive && guestFigure.alive)
    {
        guestSide.push_back({ Vector3Add(guestFigure.position, { 0.0f, 1.3f, 0.0f }), kGuestTarget });
    }
    squad.SetBattle(true, std::move(hostSide), std::move(guestSide));

    // Зочин унасан: амьд AI дайчныхаа байранд үргэлжлүүлнэ
    takeoverCooldown = std::max(0.0f, takeoverCooldown - dt);
    if (remoteActive && !guestFigure.alive && takeoverCooldown <= 0.0f)
    {
        const std::vector<Soldier>& soldiers = squad.All();
        for (size_t i = 0; i < soldiers.size(); ++i)
        {
            if (!soldiers[i].guestTeam || !soldiers[i].alive || soldiers[i].member < 0) continue;
            TraceLog(LOG_INFO, "BATTLE: guest took over a fighter (%.0f hp)", soldiers[i].health);
            GuestTakeOver((int)i, true, false);
            takeoverCooldown = 3.0f;
            break;
        }
    }

    // Зочин ирж (газрын зургаа ачаалж) багаа үүсгэтэл ялалтыг шалгахгүй; 60 секундэд ирэхгүй бол хүчингүй
    if (!guestTeamSpawned)
    {
        battleWaitTimer += dt;
        if (battleWaitTimer > 60.0f)
        {
            TraceLog(LOG_INFO, "BATTLE: guest never arrived");
            battleActive = false;
        }
        return;
    }
    battleWaitTimer = 0.0f;

    // Тасарсан хостын багийг AI тулалдуулж буй (энэ тоглогч серверт зочин): бүгд унавал эсвэл 5 минут өнгөрвөл ялна
    if (takeoverBattle)
    {
        takeoverTimer -= dt;
        if (takeoverCooldown <= 0.0f && (TeamAlive(false) == 0 || takeoverTimer <= 0.0f))
        {
            TraceLog(LOG_INFO, "BATTLE: absent host's team %s", takeoverTimer <= 0.0f ? "timed out" : "down");
            takeoverBattle = false;
            Account::ReportBattle(true);
            WinBattleAsHost();
            if (takeoverTimer <= 0.0f) SetNotice("5 минут өнгөрлөө: хост эргэж ирээгүй тул та ялна", 4.0f);
        }
        return;
    }
    // Зочны үндсэн 3 дайчин бүгд унав: хост ялна
    if (takeoverCooldown <= 0.0f && TeamAlive(false) == 0)
    {
        if (remoteActive) net.SendBattleOver(false);
        Account::ReportBattle(false);
        TraceLog(LOG_INFO, "BATTLE: host won");
        WinBattleAsHost();
    }
}

void Game::AcceptGuest()
{
    net.AnswerJoin(true, provinceIndex);
    joinRequestPending = false;
    // Сервер: зочин холбогдлоо (тулаан тоологдоно; хариунд зочны серверийн цэрэг, амь)
    if (Account::CurrentRoomRole() == Account::RoomRole::Host) Account::ConfirmGuest();
    guestShadow = -1.0f;
    guestForcedDown = false;
    guestSuspicion = 0;
    guestHitTokens = 16.0f;
    guestBlastTokens = 3.0f;
    guestSetup = PendingGuestSetup{};
    if ((Account::CurrentRoomRole() == Account::RoomRole::Host && !joinCoop) || options.battleTest)
    {
        TraceLog(LOG_INFO, "BATTLE: guest %s joined, battle started", joinRequester.c_str());
        battleActive = true;
        guestTeamSpawned = false;
        battleWaitTimer = 0.0f;
        takeoverCooldown = 3.0f;
        Account::RefreshRoom(); // тулааны дугаар
        SetNotice(joinRequester + " өрөөнд орж ирлээ — ТУЛААН! Эсрэг багийн 3 дайчныг ал", 4.0f);
    }
    else
    {
        SetNotice(joinCoop && Account::Enabled() ? joinRequester + TextFormat(" нэгдлээ — ХАМТ ТОГЛОЛТ: үеийг хамт давбал хоёулаа %d Gold",
                                                                              Account::CoopReward())
                                                 : joinRequester + " нэгдлээ", 4.0f);
    }
}

void Game::UpdateCoopMission(float dt)
{
    // ---- Хост: хүсэлтэд хариулах, зочныг бай болгох, snapshot илгээх ----
    if (!coopGuest)
    {
        if (joinRequestPending)
        {
            // Нээлттэй өрөө: зочин урилгагүй орно (сервер Gold-ийг шалгаж тасалбар өгсөн). Туршилт: автоматаар.
            const bool autoAccept = (options.screenshotPath && options.coopTest) ||
                                    (Account::CurrentRoomRole() == Account::RoomRole::Host && !joinCoop);
            if (InKeyPressed(KEY_Y) || autoAccept) AcceptGuest();
            else if (InKeyPressed(KEY_N))
            {
                net.AnswerJoin(false, provinceIndex);
                joinRequestPending = false;
            }
        }
        if (!net.Connected())
        {
            remoteActive = false;
        }
        guestHitTokens = std::min(16.0f, guestHitTokens + 14.0f * dt);
        guestBlastTokens = std::min(3.0f, guestBlastTokens + 1.0f * dt);
        guestRecruitCooldown = std::max(0.0f, guestRecruitCooldown - dt);
        guestHealCooldown = std::max(0.0f, guestHealCooldown - dt);
        if (guestSetup.pending)
        {
            guestSetup.waited += dt;
            const bool serverKnows = Account::RoomGuestSoldiers() >= 0;
            const bool noServer = Account::CurrentRoomRole() != Account::RoomRole::Host;
            if (remoteActive && (serverKnows || noServer || guestSetup.waited > 6.0f)) ApplyGuestSetup();
        }
        if (!remoteActive && guestVehicle >= 0)
        {
            garage.ClearRemote(guestVehicle); // зочин гарсан: унаа зогсож үлдэнэ
            guestVehicle = -1;
        }
        if (remoteActive)
        {
            AdvanceRemoteFigure(guestFigure, dt);
        }
        horde.SetRemoteTarget(remoteActive && guestFigure.alive && !(guestFigure.flags & Net::kRiding), guestFigure.position);
        // Зочны үлдсэн хоёр дайчин: зочныг дагаж байлдана (хост тооцоолж, snapshot-оор зочинд харагдана)
        if (remoteActive && !squad.HasGuestTeam() && !guestTeamSpawned)
        {
            squad.AddGuestTeam(guestFigure.member, guestFigure.position, world, guestMemberHealth);
            guestTeamSpawned = battleActive; // тулаанд: унасан дайчид дахин үүсэхгүй
        }
        else if (!remoteActive && squad.HasGuestTeam() && !battleActive)
        {
            squad.RemoveGuestTeam(); // (тулаанд: тасарсан зочны багийг AI тулалдуулна)
        }
        if (!remoteActive && !battleActive) guestTeamSpawned = false; // (тулааны дараа зочин гартал дахин үүсгэхгүй)
        squad.SetGuestLeader(remoteActive, guestFigure.position);
        stageSyncTimer -= dt;
        if (remoteActive && stageSyncTimer <= 0.0f)
        {
            stageSyncTimer = 2.0f;
            net.SendStageSync(StageText(progress.stages[(size_t)provinceIndex]));
        }
        return;
    }

    // ---- Зочин: хост тулаан дууслаа гэсэн бол өөрийн харж буйгаар шалгаж серверт мэдэгдэнэ ----
    if (verdictPending)
    {
        verdictTimer += dt;
        const int verdict = GuestViewVerdict();
        if (verdict >= 0)
        {
            if ((verdict == 1) != verdictHostSaysGuest) TraceLog(LOG_INFO, "BATTLE: host's result differs from what this guest sees");
            Account::ReportBattle(verdict == 1);
            verdictPending = false;
        }
        else if (verdictTimer > 4.0f)
        {
            // Энд хоёр баг амьд байсаар: хостын мэдэгдэлд итгэхгүй (сервер хүчингүй болгоно)
            TraceLog(LOG_INFO, "BATTLE: undecided on this guest, reporting no winner");
            Account::ReportBattleUndecided();
            verdictPending = false;
        }
    }

    // ---- Зочин: хостын snapshot-оор зомбинуудыг хөдөлгөж, өөрийн байдлаа илгээнэ ----
    Net::Snapshot snapshot;
    if (net.TakeSnapshot(snapshot))
    {
        guestView = snapshot;
        dayClock = snapshot.dayClock; // хосттой ижил цаг
        // Хосттой ижил цаг агаар (зочин даалгавраа эхлүүлэхдээ санамсаргүй сонгосон байж болно)
        if (snapshot.weather >= 0 && snapshot.weather <= (int)WeatherKind::Dust && snapshot.weather != (int)environment.Weather())
        {
            environment.SetWeather((WeatherKind)snapshot.weather);
            TraceLog(LOG_INFO, "COOP: weather %d from host", snapshot.weather);
        }
        horde.ApplySnapshot(snapshot.zombies, params, snapshot.host.sentAt);
        UpdateRemoteFigure(hostFigure, snapshot.host, dt, !guestPlaced);
        hostSoldiers.resize(snapshot.soldiers.size());
        hostSoldierSide.resize(snapshot.soldiers.size());
        for (size_t i = 0; i < snapshot.soldiers.size(); ++i)
        {
            const Net::SoldierState& s = snapshot.soldiers[i];
            hostSoldierSide[i] = (int8_t)(s.guest ? (s.member < 0 ? 4 : 2) : (s.member < 0 ? 3 : 1));
        }
        for (size_t i = 0; i < snapshot.soldiers.size(); ++i)
        {
            const Net::SoldierState& s = snapshot.soldiers[i];
            Net::AvatarState a;
            a.position = s.position;
            a.facing = s.facing;
            a.speed = s.speed;
            a.held = (uint8_t)(s.member == kSniperMember ? HeldItem::Sniper : HeldItem::Rifle);
            a.member = (uint8_t)std::max<int>(0, s.member);
            a.flags = (uint8_t)((s.alive ? Net::kAlive : 0) | (s.aiming ? Net::kAiming : 0) | (s.alive == 2 ? Net::kDowned : 0));
            a.shots = s.shots;
            a.shotEnd = s.shotEnd;
            a.leaderSkin = (uint8_t)(s.guest ? progress.leaderSkin : snapshot.host.leaderSkin); // зочны баг: өөрийн дүр
            a.sentAt = snapshot.host.sentAt;
            UpdateRemoteFigure(hostSoldiers[i], a, dt, false);
            hostSoldiers[i].seatStyle = s.vehicle >= 0 ? s.seatStyle : 0;
            hostSoldiers[i].seatOffset = Vector3Subtract(s.seat, s.position);
        }
        // Унаа: хост бүх унааг хөдөлгөнө, зөвхөн зочны өөрийн жолоодож буйг энд тооцно.
        for (const Net::VehicleState& v : snapshot.vehicles)
        {
            if ((int)v.index >= garage.Count() || (int)v.index == garage.Driven()) continue;
            if ((int)garage.At(v.index).type != (int)v.type) continue; // (ертөнц таарахгүй бол хөндөхгүй)
            garage.SetRemote(v.index, v.position, v.heading, v.speed, v.steer, v.occupied != 0);
            garage.SyncHealth(v.index, v.health); // хост тооцсон (зочин зөвхөн өөрийн унааг гэмтээнэ)
        }
        // Хаалга: хостынхтой ижил (сая өөрөө өөрчилсөн хаалгыг хост хүлээн авах хүртэл хөндөхгүй)
        for (int i = 0; i < std::min(snapshot.doorCount, world.DoorCount()); ++i)
        {
            if (i == pendingDoor && pendingDoorTimer > 0.0f) continue;
            const bool open = (snapshot.doors[(size_t)(i / 8)] >> (i % 8)) & 1u;
            if (open != world.IsDoorOpen(i)) world.SetDoorOpen(i, open);
        }
        medKits.Replace(snapshot.medKits);
        if (!guestSetupSent)
        {
            // Хостод: өөрийн гишүүдийн амийн дээд хэмжээ, Gold-оор авсан цэргийн тоо (хост тэдгээрийг үүсгэнэ)
            guestSetupSent = true;
            net.SendGuestSetup(player.MaxHealth(), progress.soldiers);
        }
        if (!guestPlaced)
        {
            // Эхний snapshot: хостоос ~1 км зайд (аймгийн хил дотор) гарч ирнэ; хүрэлцэхгүй бол ойртуулна
            guestPlaced = true;
            Vector3 spot = { snapshot.host.position.x + 2.5f, 0.0f, snapshot.host.position.z - 2.0f };
            const float start = (float)GetRandomValue(0, 359) * DEG2RAD;
            bool found = false;
            for (float distance = 1000.0f; distance >= 300.0f && !found; distance -= 175.0f)
            {
                for (int k = 0; k < 16 && !found; ++k)
                {
                    const float angle = start + (float)k * (2.0f * PI / 16.0f);
                    const float x = snapshot.host.position.x + std::sin(angle) * distance;
                    const float z = snapshot.host.position.z + std::cos(angle) * distance;
                    if (!world.InsideBoundary(x, z) || world.InteriorAt(x, z) >= 0) continue;
                    spot = { x, 0.0f, z };
                    found = true;
                }
            }
            TraceLog(LOG_INFO, "COOP: guest placed %.0f m from the host",
                     std::hypot(spot.x - snapshot.host.position.x, spot.z - snapshot.host.position.z));
            world.ResolveHorizontal(spot, config.playerRadius, config.playerHeight, 0.0f);
            spot.y = world.GroundHeight(spot.x, spot.z);
            player.Teleport(spot);
            camera.Snap(spot);
            world.Stream(spot, true);
        }
    }
    pendingDoorTimer = std::max(0.0f, pendingDoorTimer - dt);
    horde.UpdatePuppets(dt);
    AdvanceRemoteFigure(hostFigure, dt);
    for (RemoteFigure& figure : hostSoldiers) AdvanceRemoteFigure(figure, dt);

    // Унасан зочин: хэдэн секундын дараа хостын дэргэд амилна (тулаанд: AI дайчныхаа байранд л)
    if (!player.IsAlive() && !battleActive)
    {
        guestRespawnTimer += dt;
        if (guestRespawnTimer > kGuestRespawnTime)
        {
            guestRespawnTimer = 0.0f;
            player.Reset(player.MaxHealth());
            Vector3 spot = { hostFigure.position.x + 2.0f, 0.0f, hostFigure.position.z + 2.0f };
            world.ResolveHorizontal(spot, config.playerRadius, config.playerHeight, 0.0f);
            spot.y = world.GroundHeight(spot.x, spot.z);
            player.Teleport(spot);
            camera.Snap(spot);
        }
    }

    coopSendTimer -= dt;
    if (coopSendTimer <= 0.0f && guestPlaced)
    {
        coopSendTimer = kCoopSendInterval;
        net.SendGuestState(LocalAvatar());
    }
}

void Game::SendHostSnapshot(float dt)
{
    if (coopGuest || !net.Connected()) return; // (зочин эхний snapshot-оор байрлаж байж өөрийн байдлыг илгээнэ)

    // Дэлбэрсэн торх, зочныг цохисон хохирол: шууд
    std::vector<int> exploded;
    barrels.TakeExploded(exploded);
    for (int index : exploded) net.SendBarrelExploded(index);
    const float damage = horde.TakeRemoteDamage();
    if (damage > 0.0f) DamageGuest(damage);

    coopSendTimer -= dt;
    if (coopSendTimer > 0.0f) return;
    coopSendTimer = kCoopSendInterval;

    Net::Snapshot snapshot;
    snapshot.host = LocalAvatar();
    for (const Soldier& soldier : squad.All())
    {
        Net::SoldierState s;
        s.position = soldier.mover.position;
        s.facing = soldier.mover.facing;
        s.speed = soldier.moving ? soldier.moveAmount * 6.0f : 0.0f;
        s.member = (int8_t)soldier.member;
        s.alive = soldier.alive ? (soldier.downed ? 2 : 1) : 0; // 2: унасан
        s.aiming = soldier.aimPose > 0.5f ? 1 : 0;
        s.shots = soldier.shots;
        s.shotEnd = soldier.shotEnd;
        s.vehicle = (int8_t)soldier.riding;
        s.seatStyle = (uint8_t)soldier.seatStyle;
        s.seat = soldier.seat;
        s.guest = soldier.guestTeam ? 1 : 0;
        s.health = soldier.health;
        snapshot.soldiers.push_back(s);
    }
    // Унаа: хөдөлсөн, гэмтсэн, сууж яваа унааг л илгээнэ (хотод зуу гаруй унаа зогсож байдаг: snapshot-ыг багасгана).
    // Хоёр тал эхэндээ ижил байрлалд үүсгэдэг тул хөдлөөгүй унааг илгээх шаардлагагүй; гэхдээ snapshot
    // бүрд хэдэн унааг ээлжлэн илгээж, алдагдсан пакет, хожуу нэгдсэн зочныг нөхнө.
    sentVehiclePosition.resize((size_t)garage.Count(), { 1e9f, 0.0f, 0.0f });
    sentVehicleHealth.resize((size_t)garage.Count(), -1.0f);
    const int resendFrom = vehicleResend, resendCount = std::min(garage.Count(), 6);
    vehicleResend = garage.Count() > 0 ? (vehicleResend + resendCount) % garage.Count() : 0;
    for (int i = 0; i < garage.Count(); ++i)
    {
        const Vehicle& vehicle = garage.At(i);
        const bool occupied = i == garage.Driven() || i == teamVehicle;
        const float health = vehicle.wrecked ? 0.0f : vehicle.health;
        const bool changed = occupied || std::fabs(vehicle.speed) > 0.01f ||
                             Vector3Distance(vehicle.position, sentVehiclePosition[(size_t)i]) > 0.02f ||
                             health != sentVehicleHealth[(size_t)i];
        const bool resend = garage.Count() > 0 && (i - resendFrom + garage.Count()) % garage.Count() < resendCount;
        if (!changed && !resend) continue;
        sentVehiclePosition[(size_t)i] = vehicle.position;
        sentVehicleHealth[(size_t)i] = health;
        Net::VehicleState v;
        v.index = (uint8_t)i;
        v.type = (uint8_t)vehicle.type;
        v.position = vehicle.position;
        v.heading = vehicle.heading;
        v.speed = vehicle.speed;
        v.steer = vehicle.steer;
        v.occupied = occupied ? 1 : 0;
        v.health = health;
        snapshot.vehicles.push_back(v);
        if (snapshot.vehicles.size() >= 255) break;
    }
    snapshot.doorCount = world.DoorCount();
    snapshot.doors.assign((size_t)(snapshot.doorCount + 7) / 8, 0);
    for (int i = 0; i < snapshot.doorCount; ++i)
    {
        if (world.IsDoorOpen(i)) snapshot.doors[(size_t)(i / 8)] |= (uint8_t)(1u << (i % 8));
    }
    snapshot.medKits = medKits.Positions();
    snapshot.origin = guestFigure.position; // зомбины байрлалыг зочноос см-ээр шахна
    horde.CollectStates(guestFigure.position, kCoopViewRadius, snapshot.zombies);
    if ((++snapshotCount & 1u) != 0)
    {
        // Хол (60 м-ээс цааш) амьд зомбиг хоёр snapshot тутам нэг удаа: сүлжээний ачаалал ~хоёр дахин бага
        snapshot.zombies.erase(std::remove_if(snapshot.zombies.begin(), snapshot.zombies.end(),
                                              [&](const Net::ZombieState& z) {
                                                  return (z.flags & Net::kZombieAlive) &&
                                                         HorizontalDistance(z.position, snapshot.origin) > 60.0f;
                                              }),
                               snapshot.zombies.end());
    }
    ObjectiveTexts(snapshot.objective, snapshot.detail);
    snapshot.hordeSmall = mission.QuotaSmall() - mission.KilledSmall();
    snapshot.hordeBig = mission.QuotaBig() - mission.KilledBig();
    snapshot.zombieSpeed = horde.SpeedMultiplier();
    snapshot.dayClock = dayClock;
    snapshot.weather = (int)environment.Weather();
    net.SendSnapshot(snapshot);
}

void Game::ObjectiveTexts(std::string& objective, std::string& detail) const
{
    objective.clear();
    detail.clear();
    const int focus = mission.FocusObjective();
    if (focus >= 0)
    {
        objective = TextFormat("%d / %d  %s", focus + 1, mission.ObjectiveCount(), mission.Stage().objectives[(size_t)focus].title);
        const std::string progressText = mission.ProgressText(focus);
        const int meters = (int)mission.DistanceTo(focus, player.Position());
        detail = progressText.empty() ? TextFormat("%d м", meters) : TextFormat("%s    %d м", progressText.c_str(), meters);
    }
    else if (!mission.QuotaMet())
    {
        objective = "Үлдсэн зомбийг устга";
    }
}

void Game::DrawCoopOverlay() const
{
    const int width = ViewWidth();
    if (battleActive || hostGone)
    {
        // Тулаан: хоёр багийн амьд үндсэн дайчид
        int own = 0, enemy = 0;
        if (!coopGuest)
        {
            own = TeamAlive(true);
            enemy = TeamAlive(false);
        }
        else
        {
            own = player.IsAlive() ? 1 : 0;
            enemy = hostFigure.alive && !hostGone ? 1 : 0;
            for (size_t i = 0; i < hostSoldiers.size() && i < hostSoldierSide.size(); ++i)
            {
                if (!hostSoldiers[i].alive) continue;
                if (hostSoldierSide[i] == 2) ++own;
                if (hostSoldierSide[i] == 1) ++enemy;
            }
        }
        const char* text = hostGone ? "ТУЛААН · хост гарлаа, ялалт баталгаажиж байна"
                                    : TextFormat("ТУЛААН  ·  Танай баг %d  ·  Дайсан %d%s", own, enemy, autopilot ? "  ·  AI" : "");
        const int textWidth = Ui::MeasureBold(text, 15);
        const Rectangle chip = { (float)(width - textWidth - 32) / 2.0f, 54.0f, (float)textWidth + 32.0f, 30.0f };
        DrawRectangleRounded(chip, 0.5f, 8, Fade({ 60, 14, 12, 255 }, 0.85f));
        DrawRectangleRoundedLinesEx(chip, 0.5f, 8, 1.5f, Fade(Ui::kDanger, 0.8f));
        Ui::TextBold(text, (int)chip.x + 16, (int)chip.y + 6, 15, Ui::kText);
    }
    // Хост: нэгдэх хүсэлт
    if (joinRequestPending && !coopGuest)
    {
        const char* line = TextFormat("%s тантай хамт тоглохыг хүсэж байна", joinRequester.c_str());
        const int textWidth = Ui::MeasureBold(line, 20);
        const int cardWidth = std::max(460, textWidth + 60);
        const Rectangle card = { (float)(width - cardWidth) / 2.0f, 70.0f, (float)cardWidth, InTouchMode() ? 70.0f : 110.0f };
        Ui::PanelAccent(card, Ui::kInfo, { 12, 18, 28, 235 });
        Ui::TextBold(line, (int)card.x + 30, (int)card.y + 16, 20, Ui::kText);
        if (!InTouchMode())
        {
            int x = (int)card.x + 30;
            const int y = (int)card.y + 62;
            x += Ui::KeyBadge("Y", x, y, 16, Ui::kGood) + 8;
            Ui::TextBold("ЗӨВШӨӨРӨХ", x, y + 3, 17, Ui::kGood);
            x += Ui::MeasureBold("ЗӨВШӨӨРӨХ", 17) + 40;
            x += Ui::KeyBadge("N", x, y, 16, Ui::kDanger) + 8;
            Ui::TextBold("ТАТГАЛЗАХ", x, y + 3, 17, Ui::kDanger);
        }
    }

    // Хост найздаа хэлэх өрөөний код (зочин нэгдтэл). Хөтөч дээр сервертэй холбогдоогүй бол шалтгааныг харуулна
    // (компьютер, утсанд дотоод сүлжээгээр тоглож болох тул сервер хүрэхгүйг харуулахгүй).
    const bool openRoom = Account::CurrentRoomRole() == Account::RoomRole::Host;
    const bool webHosting = net.UsesRoomCodes() && !coopGuest && state == GameState::Playing && !openRoom;
    if (openRoom && !coopGuest && state == GameState::Playing && !net.Connected())
    {
        // Найзын код (үргэлж), нээлттэй (тулаан) эсэх: Gold хүрэлцээгүй бол шалтгаан
        const char* label = "НАЙЗЫН КОД";
        const char* code = Account::RoomCode().c_str();
        const char* note = Account::RoomPvp() ? "·  нээлттэй өрөө: өөр тоглогч орж ирж магадгүй"
                                              : TextFormat("·  тулаанд %d Gold хэрэгтэй (танд %d)", Account::RoomEntry(), Account::GoldAvailable());
        const int labelWidth = Ui::Measure(label, 13), codeWidth = Ui::MeasureBold(code, 20), noteWidth = Ui::Measure(note, 13);
        const int chipWidth = labelWidth + codeWidth + noteWidth + 48;
        const Rectangle chip = { (float)(width - chipWidth) / 2.0f, 54.0f, (float)chipWidth, 30.0f };
        DrawRectangleRounded(chip, 0.5f, 8, Fade({ 12, 18, 28, 255 }, 0.78f));
        Ui::Text(label, (int)chip.x + 14, (int)chip.y + 9, 13, Ui::kTextDim);
        Ui::TextBold(code, (int)chip.x + 22 + labelWidth, (int)chip.y + 4, 20, Ui::kGood);
        Ui::Text(note, (int)chip.x + 34 + labelWidth + codeWidth, (int)chip.y + 9, 13, Account::RoomPvp() ? Ui::kAccent : Ui::kTextDim);
    }
    if (webHosting && net.RoomCode() == 0 && !net.HasLan())
    {
        const char* text = net.GetRole() == Net::Role::Host ? "ХАМТ ТОГЛОХ: сервертэй холбогдож байна..."
                                                             : "ХАМТ ТОГЛОХ: сервертэй холбогдож чадсангүй, дахин оролдож байна";
        const int textWidth = Ui::Measure(text, 13);
        const Rectangle chip = { (float)(width - textWidth - 28) / 2.0f, 54.0f, (float)textWidth + 28.0f, 26.0f };
        DrawRectangleRounded(chip, 0.5f, 8, Fade({ 12, 18, 28, 255 }, 0.78f));
        Ui::Text(text, (int)chip.x + 14, (int)chip.y + 6, 13, net.GetRole() == Net::Role::Host ? Ui::kTextDim : Ui::kAccent);
    }
    else if (webHosting && net.GetRole() == Net::Role::Host && !net.Connected() && !joinRequestPending)
    {
        const char* label = "ХАМТ ТОГЛОХ КОД";
        const std::string codeText = std::to_string(net.RoomCode());
        const char* code = codeText.c_str();
        const int labelWidth = Ui::Measure(label, 13), codeWidth = Ui::MeasureBold(code, 20);
        const int chipWidth = labelWidth + codeWidth + 36;
        const Rectangle chip = { (float)(width - chipWidth) / 2.0f, 54.0f, (float)chipWidth, 30.0f };
        DrawRectangleRounded(chip, 0.5f, 8, Fade({ 12, 18, 28, 255 }, 0.78f));
        Ui::Text(label, (int)chip.x + 14, (int)chip.y + 9, 13, Ui::kTextDim);
        Ui::TextBold(code, (int)chip.x + 22 + labelWidth, (int)chip.y + 4, 20, Ui::kInfo);
    }

    // Нөгөө тоглогчийн нэр: толгой дээр
    const Camera3D& view = camera.Get();
    auto nameTag = [&](const RemoteFigure& figure, const std::string& name) {
        const Vector3 head = { figure.position.x, figure.position.y + 2.25f, figure.position.z };
        const Vector3 forward = Vector3Subtract(view.target, view.position);
        if (Vector3DotProduct(Vector3Subtract(head, view.position), forward) <= 0.0f) return;
        if (Vector3Distance(head, view.position) > 120.0f) return;
        const Vector2 p = ViewWorldToScreen(head, view);
        const int w = Ui::MeasureBold(name.c_str(), 15) + 20;
        DrawRectangleRounded({ p.x - (float)w / 2.0f, p.y - 13.0f, (float)w, 24.0f }, 1.0f, 8, Fade({ 12, 18, 28, 255 }, 0.7f));
        Ui::TextBold(name.c_str(), (int)p.x - w / 2 + 10, (int)p.y - 10, 15, Ui::kInfo);
    };
    if (remoteActive && !coopGuest) nameTag(guestFigure, joinRequester);
    if (coopGuest && guestPlaced) nameTag(hostFigure, hostName.empty() ? std::string("Хост") : hostName);

    if (coopGuest && !player.IsAlive())
    {
        const char* text = TextFormat("Та унасан — %d сек дараа хостын дэргэд амилна",
                                      (int)std::ceil(kGuestRespawnTime - guestRespawnTimer));
        const int w = Ui::MeasureBold(text, 22);
        Ui::Panel({ (float)(width - w) / 2.0f - 20.0f, 200.0f, (float)w + 40.0f, 48.0f });
        Ui::TextBold(text, (width - w) / 2, 212, 22, Ui::kDanger);
    }
}

// Газрын зургийн дэлгэц: ойролцоох тоглолтууд, нэгдэх товч
void Game::UpdateCoopMap()
{
    if (!net.Available()) return;
    const std::vector<Net::FoundGame>& found = net.Found();
    const bool click = InMousePressed(MOUSE_BUTTON_LEFT);
    const Vector2 pointer = InPointer();
    if (Account::TakeJoinedRoom())
    {
        // Сервер өрөөг сонгож тасалбар өглөө: хостод шууд нэгдэнэ (хост асуулгүй хүлээн авна). Зочин онцгой
        // дүрийн төлбөр төлөөгүй тул энгийн дүрээр орно.
        if (Account::PremiumSkin(progress.leaderSkin))
        {
            progress.leaderSkin = 0;
            Save();
        }
        net.SetName(PlayerName());
        net.SetJoinCoop(Account::RoomCoop()); // найзын кодоор: хамт тоглолт (хост тулаан эхлүүлэхгүй)
        net.RequestJoinTicket(Account::RoomTicket());
        waitingForReply = true;
        waitTimer = 30.0f;
        coopStatus.clear();
        return;
    }
    if (Account::ServerShop() && !waitingForReply)
    {
        // Данстай: найзын кодоор хамт (үнэгүй), эсвэл нээлттэй өрөөнд тулаан (Gold-ийн дүрмийг сервер шалгана)
        std::string note;
        if (Account::TakeSearchNote(note)) coopStatus = note;
        if ((click && CheckCollisionPointRec(pointer, FriendButtonRect())) || InKeyPressed(KEY_K))
        {
            if (Account::SearchingRoom()) Account::CancelRoomSearch();
            OpenCodeEntry();
            return;
        }
        if ((click && CheckCollisionPointRec(pointer, CodeButtonRect())) || InKeyPressed(KEY_J))
        {
            if (Account::SearchingRoom()) Account::CancelRoomSearch();
            else if (Account::GoldAvailable() >= Account::RoomEntry())
            {
                coopStatus.clear();
                Account::JoinRoom(); // сул өрөө олдтол хайна
            }
            else coopStatus = TextFormat("Тулаанд %d Gold хэрэгтэй (танд %d). Найзтайгаа кодоор үнэгүй тоглоорой",
                                         Account::RoomEntry(), Account::GoldAvailable());
        }
        return;
    }
    if (waitingForReply)
    {
        if ((click && CheckCollisionPointRec(pointer, CoopButtonRect(0))) || InKeyPressed(KEY_J))
        {
            LeaveCoop();
            coopStatus = "Цуцаллаа";
        }
        return;
    }
    if (options.joinCode > 0 && net.UsesRoomCodes())
    {
        // Туршилт: кодын цонхоор оруулсан мэт шууд нэгдэнэ (нэг удаа)
        codeEntry = std::to_string(options.joinCode);
        options.joinCode = 0;
        codeEntryOpen = true;
        ForceCodeSubmit();
        return;
    }
    // Интернэт: найзынхаа өрөөний кодыг оруулж нэгдэнэ (J нь дотоод сүлжээнд олдсон тоглолтгүй үед).
    if (net.UsesRoomCodes() &&
        ((click && CheckCollisionPointRec(pointer, CodeButtonRect())) || (InKeyPressed(KEY_J) && found.empty())))
    {
        OpenCodeEntry();
        return;
    }
    if (!net.HasLan()) return;
    const bool autoJoin = options.screenshotPath && options.coopTest; // туршилт: олдсон тоглолт руу шууд нэгдэнэ
    const int lanRows = net.UsesRoomCodes() ? 1 : 2; // доод мөрөнд "КОДООР НЭГДЭХ"
    for (int i = 0; i < (int)found.size() && i < lanRows; ++i)
    {
        if ((click && CheckCollisionPointRec(pointer, CoopButtonRect(i))) || (i == 0 && (InKeyPressed(KEY_J) || autoJoin)))
        {
            net.SetName(PlayerName());
            net.RequestJoin(found[(size_t)i]);
            waitingForReply = true;
            waitTimer = 30.0f;
            coopStatus.clear();
            return;
        }
    }
}

Rectangle Game::CoopButtonRect(int row) const
{
    // Дотоод сүлжээнд олдсон тоглолтуудын "НЭГДЭХ" (0 нь хүлээж байхад "ЦУЦЛАХ")
    return { 790.0f, 634.0f + 30.0f * (float)row, 120.0f, 26.0f };
}

Rectangle Game::CodeButtonRect() const
{
    // Дотоод сүлжээтэй бол доод мөрөнд, хөтөч дээр дээд мөрөнд. Данстай: доод мөрөнд "ТУЛААН ХАЙХ"
    return { 750.0f, net.HasLan() || Account::ServerShop() ? 664.0f : 634.0f, 160.0f, 26.0f };
}

Rectangle Game::FriendButtonRect() const
{
    // Данстай: дээд мөрөнд найзын кодоор нэгдэх
    return { 750.0f, 634.0f, 160.0f, 26.0f };
}

void Game::OpenCodeEntry()
{
    codeEntryOpen = true;
    codeEntry.clear();
    coopStatus.clear();
}

Rectangle Game::CodeKeyRect(int key) const
{
    // 3 x 4 тоон товчлуур: 1 2 3 / 4 5 6 / 7 8 9 / ← 0 OK
    const float size = 64.0f, gap = 10.0f;
    const float left = (float)ViewWidth() * 0.5f - (size * 3.0f + gap * 2.0f) * 0.5f;
    const float top = (float)ViewHeight() * 0.5f - 120.0f;
    return { left + (float)(key % 3) * (size + gap), top + (float)(key / 3) * (size + gap), size, size };
}

namespace
{
    // Кодын цонхны хүрээ: товчлуурууд дээр гарчиг, цифрийн нүднүүд
    Rectangle CodePanel(Rectangle first, Rectangle last)
    {
        return { first.x - 40.0f, first.y - 110.0f, last.x + last.width - first.x + 80.0f, last.y + last.height - first.y + 150.0f };
    }
}

void Game::UpdateCodeEntry()
{
    auto addDigit = [&](int digit) {
        if ((int)codeEntry.size() < CodeLength()) codeEntry.push_back((char)('0' + digit));
    };
    bool submit = InKeyPressed(KEY_ENTER) || InKeyPressed(KEY_KP_ENTER);
    for (int d = 0; d <= 9; ++d)
    {
        if (InKeyPressed(KEY_ZERO + d) || InKeyPressed(KEY_KP_0 + d)) addDigit(d);
    }
    if (InKeyPressed(KEY_BACKSPACE) && !codeEntry.empty()) codeEntry.pop_back();
    if (InMousePressed(MOUSE_BUTTON_LEFT))
    {
        const Vector2 pointer = InPointer();
        for (int key = 0; key < 12; ++key)
        {
            if (!CheckCollisionPointRec(pointer, CodeKeyRect(key))) continue;
            if (key < 9) addDigit(key + 1);
            else if (key == 9 && !codeEntry.empty()) codeEntry.pop_back();
            else if (key == 10) addDigit(0);
            else if (key == 11) submit = true;
        }
        // Цонхны гадна товшвол хаана
        if (!CheckCollisionPointRec(pointer, CodePanel(CodeKeyRect(0), CodeKeyRect(11)))) codeEntryOpen = false;
    }
    if (!submit || !codeEntryOpen) return;
    ForceCodeSubmit();
}

int Game::CodeLength() const
{
    return Account::ServerShop() ? 6 : 4; // сервер 6 оронтой найзын код өгнө (4 оронтойг таах амархан байсан)
}

void Game::ForceCodeSubmit()
{
    if ((int)codeEntry.size() != CodeLength())
    {
        coopStatus = TextFormat("Код %d оронтой байна", CodeLength());
        return;
    }
    if (Account::ServerShop())
    {
        // Данстай: сервер кодоор найзын өрөөг олж тасалбар өгнө (UpdateCoopMap: TakeJoinedRoom)
        codeEntryOpen = false;
        coopStatus = "Найзын өрөөг хайж байна...";
        Account::JoinCoop(codeEntry);
        return;
    }
    net.SetJoinCoop(false);
    Net::FoundGame room;
    room.name = "Өрөө " + codeEntry;
    room.address = (uint32_t)std::atoi(codeEntry.c_str());
    room.relay = true;
    codeEntryOpen = false;
    net.SetName(PlayerName());
    net.RequestJoin(room);
    waitingForReply = true;
    waitTimer = 30.0f;
    coopStatus.clear();
}

void Game::DrawCodeEntry() const
{
    const Rectangle panel = CodePanel(CodeKeyRect(0), CodeKeyRect(11));
    DrawRectangle(0, 0, ViewWidth(), ViewHeight(), { 0, 0, 0, 140 });
    Ui::PanelAccent(panel, Ui::kInfo, { 10, 13, 19, 240 });
    const char* title = "НАЙЗЫН ӨРӨӨНИЙ КОД";
    Ui::TextBold(title, (int)(panel.x + (panel.width - (float)Ui::MeasureBold(title, 17)) / 2.0f), (int)panel.y + 16, 17, Ui::kInfo);
    // Цифрийн нүд (данстай: 6, relay-ийн хуучин код: 4)
    const int digits = CodeLength();
    const float box = digits > 4 ? 38.0f : 44.0f, boxGap = digits > 4 ? 7.0f : 10.0f;
    const float boxLeft = panel.x + (panel.width - (box * (float)digits + boxGap * (float)(digits - 1))) / 2.0f;
    for (int i = 0; i < digits; ++i)
    {
        const Rectangle cell = { boxLeft + (float)i * (box + boxGap), panel.y + 48.0f, box, 48.0f };
        DrawRectangleRounded(cell, 0.25f, 6, { 255, 255, 255, 18 });
        Ui::Outline(cell, i == (int)codeEntry.size() ? Ui::kAccent : Color{ 255, 255, 255, 60 }, 1.5f, 6.0f);
        if (i < (int)codeEntry.size())
        {
            const char digit[2] = { codeEntry[(size_t)i], 0 };
            Ui::TextBold(digit, (int)(cell.x + (cell.width - (float)Ui::MeasureBold(digit, 28)) / 2.0f), (int)cell.y + 8, 28, Ui::kText);
        }
    }
    static const char* const labels[12] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "←", "0", "OK" };
    for (int key = 0; key < 12; ++key)
    {
        const Rectangle r = CodeKeyRect(key);
        const bool ok = key == 11;
        DrawRectangleRounded(r, 0.3f, 8, ok ? Fade(Ui::kInfo, (int)codeEntry.size() == digits ? 0.9f : 0.35f) : Color{ 255, 255, 255, 26 });
        Ui::TextBold(labels[key], (int)(r.x + (r.width - (float)Ui::MeasureBold(labels[key], 24)) / 2.0f), (int)(r.y + 18.0f), 24,
                     ok ? Color{ 10, 20, 30, 255 } : Ui::kText);
    }
    if (!coopStatus.empty())
    {
        Ui::Text(coopStatus.c_str(), (int)(panel.x + (panel.width - (float)Ui::Measure(coopStatus.c_str(), 13)) / 2.0f),
                 (int)(panel.y + panel.height) - 22, 13, Ui::kAccent);
    }
}

void Game::DrawCoopMap() const
{
    if (!net.Available()) return;
    const Rectangle panel = { 430.0f, 598.0f, 500.0f, 114.0f };
    Ui::PanelAccent(panel, Ui::kInfo, { 10, 13, 19, 205 });
    Ui::Outline(panel, { 96, 196, 236, 70 }, 1.0f, 10.0f);
    const char* title = Account::ServerShop()                ? "ХАМТ ТОГЛОХ · ТУЛААН  (интернэт)"
                        : net.HasLan() && net.UsesRoomCodes() ? "ХАМТ ТОГЛОХ  (сүлжээ, интернэт)"
                        : net.UsesRoomCodes()                ? "ХАМТ ТОГЛОХ  (интернэт)"
                                                             : "ХАМТ ТОГЛОХ  (дотоод сүлжээ)";
    Ui::TextBold(title, (int)panel.x + 18, (int)panel.y + 8, 15, Ui::kInfo);
    const std::string self = PlayerName();
    Ui::Text(self.c_str(), (int)(panel.x + panel.width) - 16 - Ui::Measure(self.c_str(), 13), (int)panel.y + 10, 13,
             Ui::kTextDim);

    const std::vector<Net::FoundGame>& found = net.Found();
    if (Account::ServerShop() && !waitingForReply)
    {
        // 1-р мөр: найзын кодоор хамт (үнэгүй). 2-р мөр: өөр тоглогчийн нээлттэй өрөөнд тулаан (Gold хэрэгтэй).
        const bool enough = Account::GoldAvailable() >= Account::RoomEntry();
        const bool searching = Account::SearchingRoom();
        Ui::Text("Найзтайгаа: кодоор нь нэгдэж хамт давна", (int)panel.x + 18, 632, 15, Ui::kText);
        Ui::Text(TextFormat("Үнэгүй · хамт давбал хоёулаа %d Gold", Account::CoopReward()), (int)panel.x + 18, 650, 12, Ui::kTextDim);
        {
            const Rectangle friendButton = FriendButtonRect();
            DrawRectangleRounded(friendButton, 0.5f, 6, Fade(Ui::kGood, 0.85f));
            const char* friendLabel = "КОДООР НЭГДЭХ";
            Ui::TextBold(friendLabel, (int)(friendButton.x + (friendButton.width - (float)Ui::MeasureBold(friendLabel, 13)) / 2.0f),
                         (int)friendButton.y + 5, 13, { 10, 20, 30, 255 });
        }
        if (searching)
        {
            // Сул өрөө хайж байна: эргэдэг нум
            const float spin = (float)GetTime() * 300.0f;
            const Vector2 spinner = { panel.x + 30.0f, 677.0f };
            DrawRing(spinner, 8.0f, 11.0f, 0.0f, 360.0f, 24, { 255, 255, 255, 30 });
            DrawRing(spinner, 8.0f, 11.0f, spin, spin + 100.0f, 12, Ui::kInfo);
            const float dots = std::fmod((float)GetTime(), 1.5f);
            Ui::Text(TextFormat("Тулаанд өрөө хайж байна%s", dots < 0.5f ? "." : dots < 1.0f ? ".." : "..."), (int)panel.x + 52, 668, 15,
                     Ui::kText);
        }
        else
        {
            Ui::Text("Тулаан: өөр тоглогчийн өрөөнд орно", (int)panel.x + 18, 666, 15, Ui::kText);
            Ui::Text(enough ? "Орох: 20 Gold · ялагдвал 50 Gold"
                            : TextFormat("Тулаанд %d Gold хэрэгтэй (танд %d)", Account::RoomEntry(), Account::GoldAvailable()),
                     (int)panel.x + 18, 684, 12, enough ? Ui::kTextDim : Ui::kDanger);
        }
        const Rectangle button = CodeButtonRect();
        const char* label = searching ? "ЦУЦЛАХ" : "ТУЛААН ХАЙХ";
        DrawRectangleRounded(button, 0.5f, 6, Fade(searching ? Ui::kDanger : enough ? Ui::kInfo : Ui::kTextDim, 0.85f));
        Ui::TextBold(label, (int)(button.x + (button.width - (float)Ui::MeasureBold(label, 13)) / 2.0f), (int)button.y + 5, 13,
                     { 10, 20, 30, 255 });
        if (!coopStatus.empty() && !codeEntryOpen)
        {
            Ui::Text(coopStatus.c_str(), (int)panel.x + 18, (int)(panel.y + panel.height) + 4, 13, Ui::kAccent);
        }
        return;
    }
    if (waitingForReply)
    {
        Ui::Text("Хүсэлт илгээлээ, хариу хүлээж байна...", (int)panel.x + 18, 638, 15, Ui::kText);
        const Rectangle button = CoopButtonRect(0);
        DrawRectangleRounded(button, 0.5f, 6, Fade(Ui::kDanger, 0.8f));
        Ui::TextBold("ЦУЦЛАХ", (int)(button.x + (button.width - (float)Ui::MeasureBold("ЦУЦЛАХ", 13)) / 2.0f), (int)button.y + 5, 13,
                     Ui::kText);
    }
    else if (!net.HasLan())
    {
        // Хөтөч: зөвхөн интернэт
        Ui::Text("Найзынхаа өрөөний кодоор нэгдэнэ", (int)panel.x + 18, 636, 15, Ui::kText);
        Ui::Text("Нөгөө хүн аймгаа эхлүүлэхэд код нь гарна", (int)panel.x + 18, 660, 13, Ui::kTextDim);
    }
    else
    {
        const bool withCode = net.UsesRoomCodes();
        if (found.empty())
        {
            // Хайж байгааг илтгэх эргэдэг нум
            const float spin = (float)GetTime() * 300.0f;
            const Vector2 spinner = { panel.x + 30.0f, 655.0f };
            DrawRing(spinner, 8.0f, 11.0f, 0.0f, 360.0f, 24, { 255, 255, 255, 30 });
            DrawRing(spinner, 8.0f, 11.0f, spin, spin + 100.0f, 12, Ui::kInfo);
            const float dots = std::fmod((float)GetTime(), 1.5f);
            Ui::Text(TextFormat("Ойролцоо тоглож буй хүн хайж байна%s", dots < 0.5f ? "." : dots < 1.0f ? ".." : "..."),
                     (int)panel.x + 52, 636, 15, Ui::kText);
            if (!withCode) Ui::Text("Нөгөө хүн аймгаа эхлүүлсэн, нэг сүлжээнд байх ёстой", (int)panel.x + 52, 660, 13, Ui::kTextDim);
        }
        if (withCode) Ui::Text("Интернэтээр: найзын кодоор", (int)panel.x + 18, 668, 15, Ui::kTextDim);
        for (int i = 0; i < (int)found.size() && i < (withCode ? 1 : 2); ++i)
        {
            const Net::FoundGame& game = found[(size_t)i];
            const int y = 636 + 30 * i;
            const char* province = game.province >= 0 && game.province < (int)Provinces().size() ? Provinces()[(size_t)game.province].name : "?";
            Ui::Text(TextFormat("%s  —  %s", game.name.c_str(), province), (int)panel.x + 18, y + 2, 15, Ui::kText);
            const Rectangle button = CoopButtonRect(i);
            DrawRectangleRounded(button, 0.5f, 6, Fade(Ui::kInfo, 0.85f));
            Ui::TextBold("НЭГДЭХ", (int)(button.x + (button.width - (float)Ui::MeasureBold("НЭГДЭХ", 13)) / 2.0f), (int)button.y + 5, 13,
                         { 10, 20, 30, 255 });
        }
    }
    if (!waitingForReply && net.UsesRoomCodes())
    {
        const Rectangle button = CodeButtonRect();
        DrawRectangleRounded(button, 0.5f, 6, Fade(Ui::kInfo, 0.85f));
        Ui::TextBold("КОДООР НЭГДЭХ", (int)(button.x + (button.width - (float)Ui::MeasureBold("КОДООР НЭГДЭХ", 13)) / 2.0f),
                     (int)button.y + 5, 13, { 10, 20, 30, 255 });
    }
    if (!coopStatus.empty() && !codeEntryOpen)
    {
        Ui::Text(coopStatus.c_str(), (int)panel.x + 18, (int)(panel.y + panel.height) - 22, 13, Ui::kAccent);
    }
}

#if defined(__ANDROID__)
// raylib 5.5 нь OpenGL ES 3.0 горимд glDrawBuffersEXT-ийг дууддаг боловч ES 3.0-ийн санд энэ нь
// glDrawBuffers нэртэй байдаг: холбоосын алдаанаас сэргийлж дамжуулагч функц өгнө.
#include <GLES3/gl3.h>
extern "C" void glDrawBuffersEXT(GLsizei count, const GLenum* buffers)
{
    glDrawBuffers(count, buffers);
}
#endif
