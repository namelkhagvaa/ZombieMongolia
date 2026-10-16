#include "Game.h"

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
#include <cstdio>

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
    const int kRecruitCost = 300;         // нэг цэрэг элсүүлэх үнэ, зоос
    const int kGrenadeCost = 80;          // дэлгүүр: 4 гранат
    const int kHealCost = 100;            // дэлгүүр: багийн гурван гишүүний амийг 100% болгох

    // Даалгаврын үеийн дэлгүүрийн бараа (цэсний дараалал = 1..7 товч).
    enum class ShopOffer { Recruit, Damage, FireRate, Magazine, Grenades, Heal, MaxHealth, Count };

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
    InSetTouchMode(options.touch);
    ViewInit(options.viewHeight);
#endif
    SetTargetFPS(benchmark ? 0 : config.targetFps); // хэмжилтийн үед FPS-ийг хязгаарлахгүй
    SetExitKey(KEY_NULL); // ESC-ийг өөрсдөө боловсруулна: эхлээд нээлттэй цонхыг хаана, дараа нь л гарна
    LoadUiFont();

    // Ойрын хавтгайг холдуулснаар алсад гүний нарийвчлал сайжирна (зам, сүүдэр анивчихгүй).
    rlSetClipPlanes(0.25, 2800.0);
}

GameWindow::~GameWindow()
{
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
    SetCharacterBindPose(options.bindPose);
    LoadCharacterModels(environment.CharacterShader()); // байхгүй бол дүрүүд кодоор үүсгэсэн дүрсээрээ зурагдана
    LoadHorseModel(environment.CharacterShader());      // байхгүй бол морь кодоор үүсгэсэн дүрсээрээ
    LoadProgress(progress, (int)Provinces().size());
    environment.SetQuality(options.graphics >= 0 ? options.graphics : progress.graphics);
    if (progress.netTag < 1000 || progress.netTag > 9999)
    {
        progress.netTag = GetRandomValue(1000, 9999);
    }
    config.touchAimAssist = progress.aimAssist;
    mapScreen.SelectNextTarget(progress);
#if !defined(__ANDROID__)
    // Компьютер: хадгалсан сонголтоор бүтэн дэлгэц (туршилтын горимд цонхоор).
    if (progress.fullscreen && !options.screenshotPath && !options.touch) SetFullscreen(true);
#endif
}

Game::~Game()
{
    UnloadCharacterModels();
    UnloadHorseModel();
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
    const int warmupFrames = 30;
    int endFrame = warmupFrames + std::max(1, options.frames);
    int frame = 0;
    double measureStart = 0.0;
    if (benchmark && options.campaignMap)
    {
        // Аяны газрын зургийн дэлгэцийг шалгах: сонгосон аймгийг тодруулна.
        mapScreen.Select(std::clamp(options.province - 1, 0, (int)Provinces().size() - 1));
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
        camera.SetView(options.yawDegrees * DEG2RAD, options.pitchDegrees * DEG2RAD);
        if (options.cameraDistance > 0.0f) camera.SetDistance(options.cameraDistance);
        if (options.ride >= 1 && options.ride <= garage.Count())
        {
            garage.Mount(options.ride - 1); // туршилт: унаад шууд урагш явна
            if (options.hasPose)
            {
                garage.Place(options.ride - 1, { options.x, world.GroundHeight(options.x, options.z), options.z },
                             options.yawDegrees * DEG2RAD);
            }
        }
        if (options.fullMapZoom > 0.0f)
        {
            fullMap.Open(world);
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

    while (!WindowShouldClose())
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
                // LoadImageFromScreen нь DPI масштабтай дэлгэц дээр буруу хэмжээ уншдаг тул шууд уншина.
                Image image = { rlReadScreenPixels(GetRenderWidth(), GetRenderHeight()), GetRenderWidth(),
                                GetRenderHeight(), 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
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
        if (!benchmark || options.coopTest) UpdateNetwork(dt);

        // ESC (утсан дээр "буцах" товч): нээлттэй цонхыг (даалгаврын журнал, газрын зураг) хаана; цонхгүй бол гарна.
        if (InKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACK))
        {
            if (paused)
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
#if !defined(__ANDROID__)
        if (IsKeyPressed(KEY_F11)) SetFullscreen(!progress.fullscreen);
#endif
        if (paused)
        {
            UpdatePauseMenu();
            if (quitRequested) break;
            // Ганцаараа: тоглоом түр зогсоно. Хамтарсан тоглолтод нөгөө тоглогч үргэлжлүүлж байгаа тул ертөнц
            // хөдөлсөөр (snapshot, зомби), зөвхөн энэ тоглогчийн оролт хаагдана.
            if (!(net.Connected() && (coopGuest || remoteActive)))
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
                    fullMap.Open(world);
                    EnableCursor();
                }
            }

            if (journalOpen)
            {
                // түр зогссон
            }
            else if (fullMap.IsOpen())
            {
                fullMap.Update(world, player);
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

        Draw();
    }
    if (benchmark && WindowShouldClose())
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
    if (InKeyPressed(KEY_F4)) showDebug = !showDebug;
    if (InKeyPressed(KEY_F10)) showHelp = !showHelp; // (H нь "энд бай" тушаал)
}

// ---------------------------------------------------------------- Газрын зураг

void Game::UpdateMap()
{
    UpdateCoopMap();
    if (state != GameState::Map) return; // хамтарсан тоглолт эхэлсэн
    bool changed = false;
    const MapScreen::Result result = mapScreen.Update(progress, changed);
    if (changed)
    {
        Save();
    }
    if (result == MapScreen::Result::StartMission)
    {
        StartMission(mapScreen.Selected());
    }
}

void Game::ReturnToMap()
{
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

void Game::StartMission(int index)
{
    provinceIndex = index;
    params = MissionFor(index);

    // Талбарын хязгаар: энэ аймгийн хилийн хэлбэр. Тоглогч хилээс хамгийн хол дотоод цэгт (0, 0) эхэлнэ.
    // Улаанбаатарын нутаг томсгосон (жинхэнэ хэмжээтэй нисэх буудал багтана).
    const float radius = (config.worldHalfSize - 8.0f) * (index == kUlaanbaatarProvince ? kUlaanbaatarMapScale : 1.0f);
    world.SetBoundary(MakeMissionShape(index, radius));

    // Энэ үеийн суурин, отрууд: аймаг бүрд байшингууд, зохион байгуулалт нь өөр.
    world.BuildSettlements(index);
    missionKills = 0;
    inTown = false;
    medKitKills = 0;
    lastReward = 0;

    player.Reset(config.playerBaseHealth + kHealthPerLevel * (float)progress.healthLevel);

    // Баг: 1, 2-р гишүүн дайчин, 3-р гишүүн мэргэн буудагч. Тоглогч 1-р гишүүнээр эхэлнэ.
    for (int member = 0; member < kTeamSize; ++member)
    {
        // 2-р дайчин (F2) үрлэн бууны оронд пуужин харвагчтай.
        arsenals[member].Configure(progress.weapon, IsSniperMember(member) ? Loadout::Sniper
                                                    : member == 1 ? Loadout::Heavy : Loadout::Fighter);
    }
    activeMember = 0;
    dayClock = 0.08f * kDayLength; // аймаг бүр өглөөгөөр эхэлнэ
    wasNight = false;
    environment.SetWeather(PickWeather(index, options.weather));
    environment.SetTimeOfDay(dayClock / kDayLength);
    aimToggled = false;
    progress.firstPerson = true; // даалгавар бүр 1-р этгээдээр (зөвхөн зэвсэг харагдана) эхэлнэ; V-ээр 3-р этгээд рүү шилжинэ
    for (bool& mark : holdMark) mark = false;
    arsenal = arsenals[0];
    player.SetLook(TeamMemberLook(0));
    grenades.Clear();
    horde.Clear();
    tracers.Clear();
    particles.Clear();
    fullMap.Close();
    barrels.Spawn(world, index); // аймаг бүрт өөр байрлалд
    medKits.Clear();
    world.Stream(player.Position(), true); // эхлэх цэгийн орчмын газрыг бүгдийг нь нэг дор бэлтгэнэ
    world.CloseAllDoors();
    SpawnVehicles();
    ResetSquad(progress.soldiers);
    camera.Snap(player.Position());

    // Даалгаврууд: хадгалагдсан явцаас үргэлжилнэ. Аль хэдийн чөлөөлсөн аймгийг дахин тоглоход шинээр эхэлнэ.
    StageSave& stageSave = progress.stages[(size_t)index];
    if (progress.liberated[index])
    {
        stageSave = StageSave{};
    }
    if (options.quotaSmall >= 0)
    {
        mission.SetQuota(options.quotaSmall, std::max(0, options.quotaBig)); // туршилт
    }
    mission.Start(index, params, world, stageSave);
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
    if (target == activeMember || target < 0 || target >= kTeamSize)
    {
        return false;
    }
    const int index = squad.FindMember(target);
    if (index < 0 || !squad.All()[(size_t)index].alive)
    {
        return false; // тэр гишүүн үхсэн
    }
    if (garage.IsDriving())
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
    player.SetLook(TeamMemberLook(target));
    player.SetHeld(HeldFor(arsenal.Current()));
    return true;
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
    const bool trigger = autoUse || (arsenal.Spec().automatic ? InMouseDown(MOUSE_BUTTON_LEFT)
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

    switch (arsenal.Current())
    {
    case WeaponType::Knife: UseKnife(aim, cameraDistance); break;
    case WeaponType::Grenade: ThrowGrenade(aim); break;
    case WeaponType::Launcher: FireLauncher(aim, cameraDistance); break;
    default: FireGun(aim, cameraDistance); break;
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
            particles.Blood(end, Vector3Negate(ray.direction), hit.headshot ? 14 : (spec.pellets > 1 ? 3 : 7));
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
            world.ResolveHorizontal(resolved, radius, 1.6f, 0.3f);
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

    // Талбараар тархсан нэмэлт унаа: суурин, шатахуун түгээх газар, цэргийн пост, малчдын отор
    place(VehicleType::Trike, 37.0f, -8.6f, -90.0f);
    place(VehicleType::Jeep, -40.0f, -13.0f, 0.0f);
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

    // Жижиг суурин бүрд: гудамжинд жийп эсвэл гурван дугуйт
    index = 0;
    for (const Vector2& hamlet : world.GetLayout().hamlets)
    {
        place(index % 2 == 0 ? VehicleType::Jeep : VehicleType::Trike, hamlet.x - 30.0f, hamlet.y - 5.0f, 180.0f);
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
    return result;
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
    }
    garage.Update(dt, world, horde, throttle, steer, handbrake);

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

    if (!garage.IsDriving())
    {
        // Тоглогч буусан: гишүүд ч хэрэгслийнхээ хажууд бууна. Хэрэгсэл нь аяндаа зогсоно.
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
            if (soldier.alive && soldier.member >= 0 && !soldier.guestTeam && !soldier.holding && !reference) reference = &soldier;
        }
        if (!reference)
        {
            return;
        }

        float best = kTeamVehicleRange;
        for (int i = 0; i < garage.Count(); ++i)
        {
            const float distance = HorizontalDistance(reference->mover.position, garage.At(i).position);
            if (i != garage.Driven() && !garage.At(i).occupied && distance < best)
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
        if (!soldier.alive || soldier.member < 0 || soldier.holding || soldier.guestTeam)
        {
            continue; // "энд бай" тушаалтай гишүүн байрандаа үлдэнэ; зочны багийнхан зочноо дагана
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

void Game::AdvanceDay(float dt)
{
    dayClock = std::fmod(dayClock + dt, kDayLength);
    if (options.dayTime >= 0.0f) dayClock = options.dayTime * kDayLength; // туршилт
    environment.Update(dt);
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
    HandleSettingsInput();
    AdvanceDay(dt);

    if (InKeyPressed(KEY_TAB))
    {
        ReturnToMap();
        return;
    }

    UpdateCoopMission(dt);
    UpdateVehicles(dt);
    UpdateTeamRiding(dt);
    world.UpdateDoors(dt);
    world.Stream(player.Position()); // явах тусам урд талын газрыг бага багаар үүсгэнэ

    const bool enclosed = garage.IsDriving() && SpecFor(garage.At(garage.Driven()).type).enclosed;
    const float cameraExtra = garage.IsDriving() ? SpecFor(garage.At(garage.Driven()).type).cameraExtra : 0.0f;
    // H: "Энд бай" — удирдаж буй гишүүнийг тэмдэглэнэ; өөр гишүүн рүү (F1-F3) шилжихэд тэр байрандаа үлдэж байлдана.
    if (InKeyPressed(KEY_H) && !coopGuest && !garage.IsDriving())
    {
        holdMark[activeMember] = !holdMark[activeMember];
        SetNotice(holdMark[activeMember] ? "ЭНД БАЙНА — өөр гишүүн рүү шилжинэ үү (F1-F3)" : "ЭНД БАЙХ ТУШААЛ ЦУЦЛАГДЛАА", 2.5f);
    }
    // F1, F2, F3: багийн гишүүд рүү шилжинэ.
    for (int member = 0; member < kTeamSize; ++member)
    {
        if (InKeyPressed(KEY_F1 + member) && !coopGuest)
        {
            SwitchMember(member, false);
        }
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
        const bool nowInTown = InTown(player.Position().x, player.Position().z);
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
    camera.SetFirstPerson(progress.firstPerson && !garage.IsDriving() && !player.IsTraversing());
    camera.SetTraversing(player.IsTraversing());
    if (player.IsTraversing())
    {
        camera.Guide(player.TraverseFacing(), 0.03f, dt); // бараг хэвтээ: цонхны нүхээр харна
    }
    camera.Update(dt, config, world, player.Position(), aiming, cameraExtra, scoped, player.CameraDrop());
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

    squad.Update(dt, world, player.Position(), horde, tracers, particles);
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
    if (!garage.IsDriving())
    {
        Vector3 position = player.Position();
        barrels.PushOut(position, config.playerRadius);
        player.Nudge(position.x, position.z);
    }
    particles.Update(dt, world);
    precipitation.Update(dt, camera.Get().position, environment.Weather(), environment.Wind());
    if (!coopGuest) horde.Update(dt, world, player, squad); // зочин: зомбинууд хостоос (UpdateCoopMission)
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
        // Зочин: даалгавар, шагнал, ялалтыг хост тооцно (UpdateCoopMission, UpdateNetwork)
        recruitNoticeTimer = std::max(0.0f, recruitNoticeTimer - dt);
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
        mission.Update(dt, world, player, horde, squad, interact, !garage.IsDriving());
    }

    progress.coins += mission.TakeReward();
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
        lastReward = 0;
        if (!progress.liberated[provinceIndex])
        {
            progress.liberated[provinceIndex] = true;
            lastReward = params.reward;
            progress.coins += lastReward;
        }
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
        if (net.Connected() && remoteActive) net.SendMissionWon(params.reward);
        state = GameState::Won;
    }
}

// ---------------------------------------------------------------- Тоглоомын цэс (ESC)
namespace
{
    enum class PauseItem { Resume, Graphics, Fullscreen, Map, Quit };

    // Одоогийн төлөвт харагдах мөрүүд (газрын зураг дээр "Газрын зураг руу буцах" байхгүй; утсан дээр бүтэн дэлгэц байхгүй).
    int PauseItems(bool playing, PauseItem* out)
    {
        int count = 0;
        out[count++] = PauseItem::Resume;
        out[count++] = PauseItem::Graphics;
#if !defined(__ANDROID__)
        if (!InTouchMode()) out[count++] = PauseItem::Fullscreen;
#endif
        if (playing) out[count++] = PauseItem::Map;
        out[count++] = PauseItem::Quit;
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
#if !defined(__ANDROID__)
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
    PauseItem items[5];
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
        progress.graphics = (progress.graphics + 2) % 3; // өндөр -> дунд -> бага -> өндөр
        environment.SetQuality(progress.graphics);
        Save();
        break;
    case PauseItem::Fullscreen: SetFullscreen(!progress.fullscreen); break;
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

void Game::DrawPauseMenu() const
{
    const int width = ViewWidth(), height = ViewHeight();
    DrawRectangle(0, 0, width, height, Fade(BLACK, 0.55f));

    PauseItem items[5];
    const int count = PauseItems(state == GameState::Playing, items);
    const Rectangle first = PauseRow(0, count), last = PauseRow(count - 1, count);
    const Rectangle panel = { first.x - 30.0f, first.y - 90.0f, first.width + 60.0f, last.y + last.height - first.y + 150.0f };
    Ui::PanelAccent(panel, Ui::kAccent);
    const char* title = "ЦЭС";
    Ui::TextBold(title, (int)(panel.x + (panel.width - (float)Ui::MeasureBold(title, 30)) * 0.5f), (int)panel.y + 26, 30, Ui::kText);

    const Vector2 pointer = InPointer();
    for (int i = 0; i < count; ++i)
    {
        const Rectangle row = PauseRow(i, count);
        const char* label = "";
        switch (items[i])
        {
        case PauseItem::Resume: label = "Үргэлжлүүлэх"; break;
        case PauseItem::Graphics:
            label = progress.graphics == 2 ? "График: ӨНДӨР" : progress.graphics == 1 ? "График: ДУНД" : "График: БАГА";
            break;
        case PauseItem::Fullscreen: label = progress.fullscreen ? "Бүтэн дэлгэц: ТИЙМ" : "Бүтэн дэлгэц: ҮГҮЙ"; break;
        case PauseItem::Map: label = "Газрын зураг руу буцах"; break;
        case PauseItem::Quit: label = "Тоглоомоос гарах"; break;
        }
        const bool hover = !InTouchMode() && CheckCollisionPointRec(pointer, row);
        const bool danger = items[i] == PauseItem::Quit;
        Ui::Panel(row, hover ? Fade(danger ? Ui::kDanger : Ui::kAccent, 0.25f) : Ui::kPanelSoft, 12.0f);
        if (!InTouchMode()) Ui::KeyBadge(TextFormat("%d", i + 1), (int)row.x + 16, (int)row.y + 17, 16, Ui::kTextDim);
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

    world.DrawShadowCasters(center, radius * 1.5f, environment.DepthMaterial());

    garage.Draw(center, radius * 1.4f, &environment.DepthMaterial());
    squad.Draw(center, false);
    horde.Draw(center, radius, false);
    if (player.IsAlive()) player.Draw(config, false); // 1-р этгээдийн үед ч өөрийн сүүдэр харагдана
    barrels.Draw(center);
    if (remoteActive && !coopGuest) DrawRemoteFigure(guestFigure, TeamMemberLook(guestFigure.member));
    if (coopGuest && guestPlaced)
    {
        DrawRemoteFigure(hostFigure, TeamMemberLook(hostFigure.member));
        for (const RemoteFigure& soldier : hostSoldiers) DrawRemoteFigure(soldier, TeamMemberLook(soldier.member));
    }
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

    // Гэрэлтүүлэг + манантай хэсэг
    environment.Begin(view);
    environment.DrawMountains(view.position);
    world.Draw(view, material, environment.TerrainMaterial(), environment.DetailMaterial(), config.detailDistance,
               environment.ShadowsEnabled());
    world.DrawDoors(view.position);
    garage.Draw(view.position, config.vehicleDrawDistance, &environment.PropMaterial());
    mission.Draw(view.position);
    squad.Draw(view.position, showDebug);
    horde.Draw(view.position, config.characterDrawDistance, showDebug);
    if (cinematic || (!camera.IsScoped() && !camera.IsFirstPerson()))
    {
        player.Draw(config, showDebug); // дуран хараа, 1-р этгээдийн үед өөрийн бие харагдахгүй
    }
    airstrike.Draw();
    barrels.Draw(view.position);
    medKits.Draw(view.position);
    if (remoteActive && !coopGuest) DrawRemoteFigure(guestFigure, TeamMemberLook(guestFigure.member));
    if (coopGuest && guestPlaced)
    {
        DrawRemoteFigure(hostFigure, TeamMemberLook(hostFigure.member));
        for (const RemoteFigure& soldier : hostSoldiers) DrawRemoteFigure(soldier, TeamMemberLook(soldier.member));
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
    if (state == GameState::Playing)
    {
        mission.DrawBeacons(world, view.position);
    }
    if (showDebug)
    {
        world.DrawColliders(view.position);
    }
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
    info.reloading = arsenal.IsReloading();
    static const char* const fighterSlots[] = { "1 ХУТГА", "2 ГАР БУУ", "3 АВТОМАТ", "4 ҮРЛЭН БУУ", "5 ГРАНАТ" };
    static const char* const sniperSlots[] = { "1 ХУТГА", "2 ГАР БУУ", "3 ДУРАНТ БУУ" };
    static const char* const heavySlots[] = { "1 ХУТГА", "2 ГАР БУУ", "3 АВТОМАТ", "4 ПУУЖИН", "5 ГРАНАТ" };
    info.weaponSlot = arsenal.CurrentSlot();
    info.slotNames = IsSniperMember(activeMember) ? sniperSlots : activeMember == 1 ? heavySlots : fighterSlots;
    info.slotCount = arsenal.SlotCount();
    for (int i = 0; i < info.slotCount && i < 8; ++i) info.slotWeapons[i] = (int)arsenal.SlotWeapon(i);
    info.scoped = camera.IsScoped() && state == GameState::Playing;
    info.usesAmmo = arsenal.UsesAmmo() && arsenal.Current() != WeaponType::Grenade; // гранат хязгааргүй: тоо харуулахгүй
    info.showMagazine = arsenal.Current() != WeaponType::Grenade;
    info.showHelp = showHelp;
    info.touch = InTouchMode();
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
        info.teamKeys[0] = "Та";
        for (const Net::SoldierState& s : guestView.soldiers)
        {
            if (!s.guest || info.teamCount >= 3 || s.member < 0) continue;
            info.team[info.teamCount] = { TeamMemberName(s.member), (int)std::ceil(s.health), s.alive != 0, false };
            info.teamKeys[info.teamCount] = info.teamCount == 1 ? "2" : "3";
            ++info.teamCount;
        }
        info.squadTotal = 0;
        info.partnerName = "Хост";
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
        std::snprintf(recruitPrompt, sizeof(recruitPrompt), "%sДэлгүүр — цэрэг, зэвсэг, амь (%d зоос)",
                      InTouchMode() ? "" : "E — ", progress.coins);
        info.prompt = recruitPrompt;
    }
    if (recruitNoticeTimer > 0.0f && recruitNotice)
    {
        info.notice = recruitNotice;
    }

    if (state == GameState::Lost)
    {
        DrawRectangle(0, 0, ViewWidth(), ViewHeight(), Fade(MAROON, 0.5f));
        info.banner = "БАГ ЯЛАГДЛАА";
        info.subBanner = InTouchMode() ? "Зоос болон биелүүлсэн даалгаврууд хадгалагдана."
                                       : "Зоос болон биелүүлсэн даалгаврууд хадгалагдана.     ENTER — газрын зураг руу буцах";
    }

    // Унааны мэдээлэл: унаж яваа бол хурд ба гадаргуу, үгүй бол ойрын унааны санамж
    char rideText[96] = "";
    if (garage.IsDriving())
    {
        const Vehicle& vehicle = garage.At(garage.Driven());
        const char* surface = garage.DrivenSurface() == Surface::Asphalt ? "ЗАСМАЛ ЗАМ"
                            : garage.DrivenSurface() == Surface::Dirt ? "ШОРООН ЗАМ" : "ХЭЭР ТАЛ";
        std::snprintf(rideText, sizeof(rideText), InTouchMode() ? "%s  %d км/ц  (энд дээд тал нь %d)  %s" : "%s  %d км/ц  (энд дээд тал нь %d)  %s     F — буух",
                      SpecFor(vehicle.type).name, (int)(std::fabs(vehicle.speed) * 3.6f),
                      (int)(garage.DrivenMaxSpeed() * 3.6f), surface);
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
    }

    std::vector<Vector3> markers;
    mission.Markers(markers);

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
    DrawHud(config, player, info, showDebug);
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
        case ShopOffer::Recruit: return "Цэрэг элсүүлэх";
        case ShopOffer::Damage: return "Зэвсгийн хүч";
        case ShopOffer::FireRate: return "Буудах хурд";
        case ShopOffer::Magazine: return "Сумны багтаамж";
        case ShopOffer::Grenades: return "Гранат +4";
        case ShopOffer::Heal: return "Амь 100% (бүх баг)";
        case ShopOffer::MaxHealth: return "Амийн дээд хэмжээ";
        default: return "";
        }
    }

    // Зэвсэг, амийн сайжруулалт газрын зургийн дэлгүүртэй нэг түвшинг хуваалцана (Shop.h).
    bool OfferUpgrade(ShopOffer offer, ShopItem& item)
    {
        switch (offer)
        {
        case ShopOffer::Damage: item = ShopItem::Damage; return true;
        case ShopOffer::FireRate: item = ShopItem::FireRate; return true;
        case ShopOffer::Magazine: item = ShopItem::Magazine; return true;
        case ShopOffer::MaxHealth: item = ShopItem::Health; return true;
        default: return false;
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
    ShopItem item = ShopItem::Count;
    if (OfferUpgrade(offer, item))
    {
        if (ShopLevel(progress, item) >= ShopMaxLevel(item))
        {
            shopMessage = "Хамгийн дээд түвшинд хүрсэн";
            return;
        }
        if (!ShopBuy(progress, item))
        {
            shopMessage = "ЗООС ХҮРЭЛЦЭХГҮЙ";
            return;
        }
        if (item == ShopItem::Health)
        {
            // Удирдаж буй ба дагаж буй гишүүдийн амийн дээд хэмжээ нэмэгдэнэ.
            const float newMax = config.playerBaseHealth + kHealthPerLevel * (float)progress.healthLevel;
            const float gain = newMax - player.MaxHealth();
            player.SetMaxHealth(newMax);
            for (Soldier& soldier : squad.All())
            {
                if (soldier.member < 0) continue;
                soldier.maxHealth = newMax;
                if (soldier.alive) soldier.health += gain;
            }
            shopMessage = "АМИЙН ДЭЭД ХЭМЖЭЭ НЭМЭГДЛЭЭ";
        }
        else
        {
            // Багийн гурван гишүүний зэвсэгт шууд үйлчилнэ (сум хөндөхгүй).
            for (Arsenal& memberArsenal : arsenals) memberArsenal.SetLevels(progress.weapon);
            arsenal.SetLevels(progress.weapon);
            shopMessage = "ЗЭВСЭГ САЙЖИРЛАА";
        }
        Save();
        return;
    }

    if (offer == ShopOffer::Recruit && coopGuest)
    {
        shopMessage = "Зочин цэрэг элсүүлэх боломжгүй";
        return;
    }
    const int cost = offer == ShopOffer::Recruit ? kRecruitCost : offer == ShopOffer::Grenades ? kGrenadeCost : kHealCost;
    if (progress.coins < cost)
    {
        shopMessage = "ЗООС ХҮРЭЛЦЭХГҮЙ";
        return;
    }
    switch (offer)
    {
    case ShopOffer::Recruit:
    {
        // Дэлгүүрийн дотор, хаалганы дэргэд гарч ирээд тоглогчийг дагана.
        const float angle = (float)(squad.Total() % 8) * 0.785f;
        Vector3 spot = { kShopCenter.x + std::sin(angle) * 2.0f, 0.0f, kShopCenter.y + 1.2f + std::cos(angle) * 0.8f };
        world.ResolveHorizontal(spot, 0.4f, 1.8f, 0.0f);
        spot.y = world.GroundHeight(spot.x, spot.z);
        squad.Add(spot, 0.0f);
        shopMessage = "ШИНЭ ЦЭРЭГ БАГТ НЭГДЛЭЭ";
        break;
    }
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
    Ui::Text("Цуглуулсан зоосоороо худалдаж ав", (int)panel.x + 20, (int)panel.y + 56, 16, Ui::kTextDim);
    Ui::Chip(TextFormat("%d", progress.coins), (int)(panel.x + panel.width) - 20, (int)panel.y + 22, Ui::kAccent, Ui::kAccent, true);

    const Vector2 pointer = InPointer();
    for (int i = 0; i < count; ++i)
    {
        const ShopOffer offer = (ShopOffer)i;
        const Rectangle row = ShopRow(i);

        // Үнэ, түвшин, боломжтой эсэх
        ShopItem item = ShopItem::Count;
        const bool upgrade = OfferUpgrade(offer, item);
        const int level = upgrade ? ShopLevel(progress, item) : 0;
        const int maxLevel = upgrade ? ShopMaxLevel(item) : 0;
        const bool maxed = upgrade && level >= maxLevel;
        const int cost = upgrade ? ShopCost(progress, item)
                       : offer == ShopOffer::Recruit ? kRecruitCost : offer == ShopOffer::Grenades ? kGrenadeCost : kHealCost;
        const bool affordable = !maxed && progress.coins >= cost;

        const bool hover = !InTouchMode() && CheckCollisionPointRec(pointer, row);
        Ui::Panel(row, hover && affordable ? Fade(Ui::kAccent, 0.22f) : Ui::kPanelSoft, 10.0f);
        Ui::KeyBadge(TextFormat("%d", i + 1), (int)row.x + 12, (int)row.y + 13, 16, affordable ? Ui::kAccent : Ui::kTextDim);
        Ui::TextBold(ShopOfferName(offer), (int)row.x + 54, (int)row.y + (upgrade ? 6 : 14), 20, maxed ? Ui::kTextDim : Ui::kText);
        if (upgrade)
        {
            for (int k = 0; k < maxLevel; ++k)
            {
                DrawRectangleRounded({ row.x + 54.0f + (float)k * 16.0f, row.y + 34.0f, 12.0f, 7.0f }, 1.0f, 4,
                                     k < level ? Ui::kAccent : Color{ 255, 255, 255, 40 });
            }
        }
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
    const char* help = InTouchMode() ? "Мөрийг товшиж авна" : "1–7 эсвэл товшиж авна    E / ESC — хаах";
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
    centered("ДААЛГАВАР АМЖИЛТТАЙ БИЕЛЛЭЭ", 126, 52, gold);

    char line[200];
    std::snprintf(line, sizeof(line), "%s — ҮЕ %02d / %02d", mission.Stage().provinceName, provinceIndex + 1,
                  (int)Provinces().size());
    centered(line, 206, 32, text);

    std::snprintf(line, sizeof(line), "Биелүүлсэн даалгавар: %d / %d", mission.CompletedCount(), mission.ObjectiveCount());
    centered(line, 280, 26, text);

    if (lastReward > 0) std::snprintf(line, sizeof(line), "Аймаг чөлөөлсний шагнал: %d зоос", lastReward);
    else std::snprintf(line, sizeof(line), "Энэ аймаг өмнө нь чөлөөлөгдсөн тул нэмэлт шагнал олгохгүй");
    centered(line, 318, 26, gold);

    std::snprintf(line, sizeof(line), "Нийт зоос: %d", progress.coins);
    centered(line, 356, 26, text);

    // Дараагийн нээгдэх үе
    const int next = provinceIndex + 1;
    if (next < (int)Provinces().size())
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
    touch.minimapSize = kMinimapSize;
    if (paused)
    {
        touch.screen = TouchScreen::Pause;
        return touch;
    }
    if (state == GameState::Map)
    {
        touch.screen = TouchScreen::CampaignMap;
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
    touch.showHold = !coopGuest && !garage.IsDriving();
    touch.holdMarked = holdMark[activeMember];
    touch.stance = player.GetStance() == Stance::Crouch ? 1 : player.GetStance() == Stance::Prone ? 2 : 0;

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
    if (garage.IsDriving())
    {
        touch.useLabel = "УНААНААС БУУХ";
    }
    else
    {
        const Interaction interaction = FindInteraction();
        if (interaction.vehicle >= 0) touch.useLabel = "УНААНД СУУХ";
        else if (interaction.door >= 0) touch.useLabel = world.IsDoorOpen(interaction.door) ? "ХААЛГА ХААХ" : "ХААЛГА НЭЭХ";
        else if (interaction.passage >= 0) touch.useLabel = PassageLabel(world.GetPassage(interaction.passage).kind, true);
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
        // 3D-г HUD-аас тусад нь текстурт зурж, өнгө засан (bloom, vignette) дэлгэцэнд гаргана.
        RenderShadows();
        post = environment.BeginScene(ViewWidth(), ViewHeight());
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
}


// ---------------------------------------------------------------- Хамтарсан тоглолт (Wi-Fi)

namespace
{
    const float kCoopSendInterval = 1.0f / 15.0f; // snapshot, дүрийн байдлын давтамж
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
    return std::string("Тоглогч ") + std::to_string(progress.netTag);
}

void Game::UpdateRemoteFigure(RemoteFigure& figure, const Net::AvatarState& state, float dt, bool snap)
{
    figure.target = state.position;
    figure.targetFacing = state.facing;
    figure.speed = state.speed;
    figure.aimPitch = state.aimPitch;
    figure.held = state.held;
    figure.member = state.member;
    figure.flags = state.flags;
    figure.alive = (state.flags & Net::kAlive) != 0;
    figure.aiming = (state.flags & Net::kAiming) != 0;
    figure.vehicle = (state.flags & Net::kRiding) ? state.vehicle : -1;
    if (snap || Vector3Distance(figure.position, figure.target) > 6.0f)
    {
        figure.position = figure.target;
        figure.synced = false; // үсэрсэн (өөр цэрэг байж магадгүй): хуучин сумыг зурахгүй
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
    }
    particles.Flash(muzzle, direction);
    figure.recoil = 1.0f;
    figure.aimTimer = 1.2f;
}

void Game::AdvanceRemoteFigure(RemoteFigure& figure, float dt) const
{
    figure.position = Vector3Lerp(figure.position, figure.target, std::min(1.0f, 12.0f * dt));
    figure.facing += std::remainder(figure.targetFacing - figure.facing, 2.0f * PI) * std::min(1.0f, 12.0f * dt);
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
        seat = garage.SeatWorld(figure.vehicle, false);
        seatHeading = vehicle.heading;
        // Онилж буудаж байвал бие тэр зүг рүү (±120 градус), үгүй бол унааны зүгт.
        const bool aiming = figure.aiming || figure.aimTimer > 0.0f;
        facing = aiming ? vehicle.heading + std::clamp(std::remainder(figure.facing - vehicle.heading, 2.0f * PI), -2.1f, 2.1f)
                        : vehicle.heading;
    }
    if (figure.alive && seatStyle > 0)
    {
        if (seatStyle == 3) return;
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
        if (figure.vehicle >= 0 && figure.vehicle < garage.Count())
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
    state.flags = (uint8_t)((player.IsAlive() ? Net::kAlive : 0) | (player.GetStance() == Stance::Crouch ? Net::kCrouch : 0) |
                            (player.GetStance() == Stance::Prone ? Net::kProne : 0) |
                            (player.Recoil() > 0.5f ? Net::kFired : 0) | (player.IsRiding() ? Net::kRiding : 0) |
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
    }
    return state;
}

void Game::StartCoopGuest(int province)
{
    StartMission(province);
    coopGuest = true;
    guestPlaced = false;
    guestRespawnTimer = 0.0f;
    squad.Reset(0, player.Position(), world); // зочин ганцаараа: AI цэрэггүй
    horde.Clear();
    horde.SetPuppet(true);
    hostFigure = RemoteFigure{};
    hostSoldiers.clear();
    guestView = Net::Snapshot{};
    SetNotice("ХАМТАРСАН ТОГЛОЛТ ЭХЭЛЛЭЭ", 2.5f);
}

void Game::LeaveCoop()
{
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
    if (net.LocalName().empty()) net.SetName(PlayerName());

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
    else if (state == GameState::Playing && !coopGuest && net.GetRole() != Net::Role::Host)
    {
        net.StopDiscovery();
        net.StartHosting(provinceIndex);
    }

    netEvents.clear();
    net.Poll(netEvents);
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
            }
            else
            {
                net.AnswerJoin(false, provinceIndex);
            }
            break;
        case Net::Msg::GuestState:
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
                }
            }
            break;
        case Net::Msg::GuestDoor:
            if (!coopGuest && (int)event.id < world.DoorCount()) world.SetDoorOpen((int)event.id, event.accepted);
            break;
        case Net::Msg::GuestPickup:
            if (!coopGuest) medKits.RemoveNear(event.position, 2.5f);
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
            if (horde.DamageById(event.id, event.amount, coins, at))
            {
                net.SendKillReward(coins, at);
            }
            break;
        }
        case Net::Msg::GuestBarrel:
            barrels.Damage((int)event.id, event.amount);
            break;
        case Net::Msg::GuestBlast:
            horde.Explode(event.position, event.radius, event.amount);
            barrels.Blast(event.position, event.radius);
            particles.Explosion(event.position);
            break;

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
                StartCoopGuest(event.province);
            }
            else
            {
                LeaveCoop();
                coopStatus = "Хүсэлтийг татгалзлаа";
            }
            break;
        case Net::Msg::GuestDamage:
            if (coopGuest) player.TakeDamage(event.amount);
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
                lastReward = 0;
                if (!progress.liberated[provinceIndex])
                {
                    progress.liberated[provinceIndex] = true;
                    lastReward = (int)event.amount;
                    progress.coins += lastReward;
                }
                Save();
                state = GameState::Won;
                LeaveCoop();
            }
            break;

        case Net::Msg::Leave:
            if (coopGuest)
            {
                LeaveCoop();
                coopStatus = "Хост тоглолтоос гарлаа";
                if (state == GameState::Playing) ReturnToMap();
            }
            else if (remoteActive || joinRequestPending)
            {
                SetNotice(joinRequester + " тоглолтоос гарлаа", 2.5f);
                remoteActive = false;
                joinRequestPending = false;
                guestFigure = RemoteFigure{};
                horde.SetRemoteTarget(false, {});
            }
            else if (waitingForReply)
            {
                LeaveCoop();
                coopStatus = "Холбогдож чадсангүй";
            }
            break;
        default:
            break;
        }
    }
}

void Game::UpdateCoopMission(float dt)
{
    // ---- Хост: хүсэлтэд хариулах, зочныг бай болгох, snapshot илгээх ----
    if (!coopGuest)
    {
        if (joinRequestPending)
        {
            const bool autoAccept = options.screenshotPath && options.coopTest; // туршилт: автоматаар зөвшөөрнө
            if (InKeyPressed(KEY_Y) || autoAccept)
            {
                net.AnswerJoin(true, provinceIndex);
                joinRequestPending = false;
                SetNotice(joinRequester + " нэгдлээ", 2.5f);
            }
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
        if (remoteActive && !squad.HasGuestTeam())
        {
            squad.AddGuestTeam(guestFigure.member, guestFigure.position, world);
        }
        else if (!remoteActive && squad.HasGuestTeam())
        {
            squad.RemoveGuestTeam();
        }
        squad.SetGuestLeader(remoteActive, guestFigure.position);
        return;
    }

    // ---- Зочин: хостын snapshot-оор зомбинуудыг хөдөлгөж, өөрийн байдлаа илгээнэ ----
    Net::Snapshot snapshot;
    if (net.TakeSnapshot(snapshot))
    {
        guestView = snapshot;
        dayClock = snapshot.dayClock; // хосттой ижил цаг
        horde.ApplySnapshot(snapshot.zombies, params);
        UpdateRemoteFigure(hostFigure, snapshot.host, dt, !guestPlaced);
        hostSoldiers.resize(snapshot.soldiers.size());
        for (size_t i = 0; i < snapshot.soldiers.size(); ++i)
        {
            const Net::SoldierState& s = snapshot.soldiers[i];
            Net::AvatarState a;
            a.position = s.position;
            a.facing = s.facing;
            a.speed = s.speed;
            a.held = (uint8_t)(s.member == kSniperMember ? HeldItem::Sniper : HeldItem::Rifle);
            a.member = (uint8_t)std::max<int>(0, s.member);
            a.flags = (uint8_t)((s.alive ? Net::kAlive : 0) | (s.aiming ? Net::kAiming : 0));
            a.shots = s.shots;
            a.shotEnd = s.shotEnd;
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
        }
        // Хаалга: хостынхтой ижил (сая өөрөө өөрчилсөн хаалгыг хост хүлээн авах хүртэл хөндөхгүй)
        for (int i = 0; i < std::min(snapshot.doorCount, world.DoorCount()); ++i)
        {
            if (i == pendingDoor && pendingDoorTimer > 0.0f) continue;
            const bool open = (snapshot.doors[(size_t)(i / 8)] >> (i % 8)) & 1u;
            if (open != world.IsDoorOpen(i)) world.SetDoorOpen(i, open);
        }
        medKits.Replace(snapshot.medKits);
        if (!guestPlaced)
        {
            // Эхний snapshot: хостын хажууд гарч ирнэ
            guestPlaced = true;
            Vector3 spot = { snapshot.host.position.x + 2.5f, 0.0f, snapshot.host.position.z - 2.0f };
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

    // Унасан зочин: хэдэн секундын дараа хостын дэргэд амилна
    if (!player.IsAlive())
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
    if (coopSendTimer <= 0.0f)
    {
        coopSendTimer = kCoopSendInterval;
        net.SendGuestState(LocalAvatar());
    }
}

void Game::SendHostSnapshot(float dt)
{
    if (coopGuest || !net.Connected() || !remoteActive) return;

    // Дэлбэрсэн торх, зочныг цохисон хохирол: шууд
    std::vector<int> exploded;
    barrels.TakeExploded(exploded);
    for (int index : exploded) net.SendBarrelExploded(index);
    const float damage = horde.TakeRemoteDamage();
    if (damage > 0.0f) net.SendGuestDamage(damage);

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
        s.alive = soldier.alive ? 1 : 0;
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
    for (int i = 0; i < garage.Count(); ++i)
    {
        const Vehicle& vehicle = garage.At(i);
        Net::VehicleState v;
        v.index = (uint8_t)i;
        v.type = (uint8_t)vehicle.type;
        v.position = vehicle.position;
        v.heading = vehicle.heading;
        v.speed = vehicle.speed;
        v.steer = vehicle.steer;
        v.occupied = (i == garage.Driven() || i == teamVehicle) ? 1 : 0;
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
    horde.CollectStates(guestFigure.position, kCoopViewRadius, snapshot.zombies);
    ObjectiveTexts(snapshot.objective, snapshot.detail);
    snapshot.hordeSmall = mission.QuotaSmall() - mission.KilledSmall();
    snapshot.hordeBig = mission.QuotaBig() - mission.KilledBig();
    snapshot.zombieSpeed = horde.SpeedMultiplier();
    snapshot.dayClock = dayClock;
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
    if (coopGuest && guestPlaced) nameTag(hostFigure, "Хост");

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
    if (waitingForReply)
    {
        if ((click && CheckCollisionPointRec(pointer, CoopButtonRect(0))) || InKeyPressed(KEY_J))
        {
            LeaveCoop();
            coopStatus = "Цуцаллаа";
        }
        return;
    }
    const bool autoJoin = options.screenshotPath && options.coopTest; // туршилт: олдсон тоглолт руу шууд нэгдэнэ
    for (int i = 0; i < (int)found.size() && i < 2; ++i)
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
    // Олдсон тоглолтуудын "НЭГДЭХ" (0 нь хүлээж байхад "ЦУЦЛАХ")
    return { 790.0f, 628.0f + 30.0f * (float)row, 120.0f, 26.0f };
}

void Game::DrawCoopMap() const
{
    if (!net.Available()) return;
    const Rectangle panel = { 430.0f, 592.0f, 500.0f, 122.0f };
    Ui::PanelAccent(panel, Ui::kInfo, Ui::kPanelSoft);
    Ui::TextBold("ХАМТ ТОГЛОХ  (дотоод сүлжээ)", (int)panel.x + 18, (int)panel.y + 8, 15, Ui::kInfo);
    const std::string self = PlayerName();
    Ui::Text(self.c_str(), (int)(panel.x + panel.width) - 16 - Ui::Measure(self.c_str(), 13), (int)panel.y + 10, 13,
             Ui::kTextDim);

    const std::vector<Net::FoundGame>& found = net.Found();
    if (waitingForReply)
    {
        Ui::Text("Хүсэлт илгээлээ, хариу хүлээж байна...", (int)panel.x + 18, 630, 15, Ui::kText);
        const Rectangle button = CoopButtonRect(0);
        DrawRectangleRounded(button, 0.5f, 6, Fade(Ui::kDanger, 0.8f));
        Ui::TextBold("ЦУЦЛАХ", (int)(button.x + (button.width - (float)Ui::MeasureBold("ЦУЦЛАХ", 13)) / 2.0f), (int)button.y + 5, 13,
                     Ui::kText);
    }
    else
    {
        if (found.empty())
        {
            const float dots = std::fmod((float)GetTime(), 1.5f);
            Ui::Text(TextFormat("Ойролцоо тоглож буй хүн хайж байна%s", dots < 0.5f ? "." : dots < 1.0f ? ".." : "..."),
                     (int)panel.x + 18, 630, 15, Ui::kTextDim);
            Ui::Text("Нөгөө хүн аймгаа эхлүүлсэн, нэг сүлжээнд байх ёстой", (int)panel.x + 18, 654, 13, Ui::kTextDim);
        }
        for (int i = 0; i < (int)found.size() && i < 2; ++i)
        {
            const Net::FoundGame& game = found[(size_t)i];
            const int y = 630 + 30 * i;
            const char* province = game.province >= 0 && game.province < (int)Provinces().size() ? Provinces()[(size_t)game.province].name : "?";
            Ui::Text(TextFormat("%s  —  %s", game.name.c_str(), province), (int)panel.x + 18, y + 2, 15, Ui::kText);
            const Rectangle button = CoopButtonRect(i);
            DrawRectangleRounded(button, 0.5f, 6, Fade(Ui::kInfo, 0.85f));
            Ui::TextBold("НЭГДЭХ", (int)(button.x + (button.width - (float)Ui::MeasureBold("НЭГДЭХ", 13)) / 2.0f), (int)button.y + 5, 13,
                         { 10, 20, 30, 255 });
        }
    }
    if (!coopStatus.empty())
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
