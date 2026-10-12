#include "Game.h"

#include "Input.h"

#include "CharacterModel.h"
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
    const float kRecruitDistance = 3.2f;  // элсүүлэх цэгээс ийм зайд E дарна
    // 500 зомби устгах тутам зомбины хурд 2 дахин нэмэгдэнэ; тоглох боломжтой байхын тулд 4 дахинаар хязгаарлана.
    const int kKillsPerSpeedup = 500;
    const float kMaxZombieSpeedMultiplier = 4.0f;
    const int kRecruitCost = 300;         // нэг цэрэг элсүүлэх үнэ, зоос

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
    for (const char* path : { "assets/window_icon.png", "../assets/window_icon.png" })
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
    : options(launchOptions), window(config, options), world(config.worldHalfSize), player({ 0.0f, 0.0f, 0.0f }),
      camera(config)
{
    if (options.savePath)
    {
        SetSavePath(options.savePath); // туршилт: тусдаа хадгалалтын файл
    }
    SetCharacterBindPose(options.bindPose);
    LoadCharacterModels(environment.CharacterShader()); // байхгүй бол дүрүүд кодоор үүсгэсэн дүрсээрээ зурагдана
    LoadProgress(progress, (int)Provinces().size());
    if (progress.netTag < 1000 || progress.netTag > 9999)
    {
        progress.netTag = GetRandomValue(1000, 9999);
    }
    config.touchAimAssist = progress.aimAssist;
    mapScreen.SelectNextTarget(progress);
}

Game::~Game()
{
    UnloadCharacterModels();
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
            if (journalOpen)
            {
                CloseJournal();
            }
            else if (fullMap.IsOpen())
            {
                fullMap.Close();
                LockCursor();
            }
            else
            {
                if (benchmark) TraceLog(LOG_INFO, "BENCH: stopped early by the ESC key at frame %d", frame);
                break;
            }
        }

        switch (state)
        {
        case GameState::Map:
            UpdateMap();
            break;
        case GameState::Playing:
            // Q: даалгаврын журнал. M: бүтэн дэлгэцийн газрын зураг. Аль нэг нь нээлттэй үед тоглоом түр зогсож,
            // хулганы заагч гарч ирнэ; камер эргэхгүй, буудлага явагдахгүй.
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
        case GameState::Won:
        case GameState::Lost:
            tracers.Update(dt);
            if (InKeyPressed(KEY_ENTER)) ReturnToMap();
            break;
        }

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
    if (InKeyPressed(KEY_H)) showHelp = !showHelp;
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

void Game::StartMission(int index)
{
    provinceIndex = index;
    params = MissionFor(index);

    // Талбарын хязгаар: энэ аймгийн хилийн хэлбэр. Тоглогч хилээс хамгийн хол дотоод цэгт (0, 0) эхэлнэ.
    world.SetBoundary(MakeMissionShape(index, config.worldHalfSize - 8.0f));

    // Энэ үеийн суурин, отрууд: аймаг бүрд байшингууд, зохион байгуулалт нь өөр.
    world.BuildSettlements(index);
    missionKills = 0;
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
    arsenal = arsenals[0];
    player.SetLook(TeamMemberLook(0));
    grenades.Clear();
    horde.Clear();
    tracers.Clear();
    particles.Clear();
    fullMap.Close();
    barrels.Spawn(world, index); // аймаг бүрт өөр байрлалд
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

    activeMember = target;
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
    return camera.IsFirstPerson() ? ViewModelMuzzle(camera.Get(), player.Held()) : player.MuzzlePosition();
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

    const int vehicle = garage.Nearest(position, kMountDistance);
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
                garage.Mount(target.vehicle);
            }
            else if (target.door >= 0)
            {
                world.SetDoorOpen(target.door, !world.IsDoorOpen(target.door));
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

    if (garage.IsDriving())
    {
        const Vehicle& vehicle = garage.At(garage.Driven());
        const VehicleSpec& spec = SpecFor(vehicle.type);
        const RideStyle style = spec.enclosed ? RideStyle::Hidden
                              : vehicle.type == VehicleType::Horse ? RideStyle::Saddle : RideStyle::Bike;
        player.Ride(dt, vehicle.position, garage.SeatPosition(), vehicle.heading, style);
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
            if (soldier.alive && soldier.member >= 0 && !reference) reference = &soldier;
        }
        if (!reference)
        {
            return;
        }

        float best = kTeamVehicleRange;
        for (int i = 0; i < garage.Count(); ++i)
        {
            const float distance = HorizontalDistance(reference->mover.position, garage.At(i).position);
            if (i != garage.Driven() && distance < best)
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
        if (!soldier.alive || soldier.member < 0)
        {
            continue;
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
            soldier.seat = garage.SeatWorld(teamVehicle, passenger);
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

void Game::UpdateMission(float dt)
{
    HandleSettingsInput();

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
    // F1, F2, F3: багийн гишүүд рүү шилжинэ.
    for (int member = 0; member < kTeamSize; ++member)
    {
        if (InKeyPressed(KEY_F1 + member) && !coopGuest)
        {
            SwitchMember(member, false);
        }
    }

    const bool forceAim = options.screenshotPath && options.scope; // туршилтын горим
    const bool aiming = (InMouseDown(MOUSE_BUTTON_RIGHT) || forceAim) && !enclosed;
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
    barrels.Update(dt, horde, player, squad, particles);
    if (!garage.IsDriving())
    {
        Vector3 position = player.Position();
        barrels.PushOut(position, config.playerRadius);
        player.Nudge(position.x, position.z);
    }
    particles.Update(dt, world);
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

    // Зомбины хурд: тухайн аймгийн үед устгасан тоо (хадгалагдана) 500 тутамд 2 дахин
    if (!coopGuest)
    {
        const int stageKills = mission.KilledSmall() + mission.KilledBig();
        const int level = stageKills / kKillsPerSpeedup;
        const float multiplier = std::min(kMaxZombieSpeedMultiplier, std::pow(2.0f, (float)level));
        if (multiplier > horde.SpeedMultiplier() + 0.01f && state == GameState::Playing && missionFrame > 2)
        {
            recruitNotice = multiplier >= kMaxZombieSpeedMultiplier ? "ЗОМБИНУУД ХАМГИЙН ХУРДАН БОЛЛОО!" : "ЗОМБИНУУД 2 ДАХИН ХУРДАН БОЛЛОО!";
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
    // --- Цэрэг элсүүлэх цэг: E дарж 300 зоосоор нэг цэрэг багт нэмнэ (тоо хязгааргүй, тухайн даалгаврын турш) ---
    recruitNoticeTimer = std::max(0.0f, recruitNoticeTimer - dt);
    nearRecruitStation = !garage.IsDriving() && !player.IsTraversing() &&
                         HorizontalDistance(player.Position(), { kRecruitStation.x, 0.0f, kRecruitStation.y }) < kRecruitDistance;
    if (nearRecruitStation && interact)
    {
        interact = false; // даалгаврын объекттой давхар харилцахгүй
        if (progress.coins >= kRecruitCost)
        {
            progress.coins -= kRecruitCost;
            const float angle = (float)(squad.Total() % 8) * 0.785f;
            Vector3 spot = { kRecruitStation.x + std::sin(angle) * 2.5f, 0.0f, kRecruitStation.y + 1.5f + std::cos(angle) * 1.5f };
            world.ResolveHorizontal(spot, 0.4f, 1.8f, 0.0f);
            spot.y = world.GroundHeight(spot.x, spot.z);
            squad.Add(spot, PI);
            recruitNotice = "ШИНЭ ЦЭРЭГ БАГТ НЭГДЛЭЭ";
            Save();
        }
        else
        {
            recruitNotice = "ЗООС ХҮРЭЛЦЭХГҮЙ";
        }
        recruitNoticeTimer = 2.2f;
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

    // Бүх даалгавар биелж, үеийн зомбины нөөц бүхэлдээ устгагдсан: аймаг чөлөөлөгдөнө.
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
        if (net.Connected() && remoteActive) net.SendMissionWon(params.reward);
        state = GameState::Won;
    }
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

void Game::DrawMission() const
{
    const Camera3D& view = camera.Get();
    const Material& material = environment.GetMaterial();

    environment.DrawSky(view); // 2D: тэнгэр, нар

    BeginMode3D(view);

    // Гэрэлтүүлэг + манантай хэсэг
    environment.Begin(view);
    environment.DrawMountains(view.position);
    world.Draw(view, material, config.detailDistance);
    world.DrawDoors(view.position);
    garage.Draw(view.position, config.vehicleDrawDistance, &environment.PropMaterial());
    mission.Draw(view.position);
    squad.Draw(view.position, showDebug);
    horde.Draw(view.position, config.characterDrawDistance, showDebug);
    if (!camera.IsScoped() && !camera.IsFirstPerson())
    {
        player.Draw(config, showDebug); // дуран хараа, 1-р этгээдийн үед өөрийн бие харагдахгүй
    }
    barrels.Draw(view.position);
    if (remoteActive && !coopGuest) DrawRemoteFigure(guestFigure, TeamMemberLook(guestFigure.member));
    if (coopGuest && guestPlaced)
    {
        DrawRemoteFigure(hostFigure, TeamMemberLook(hostFigure.member));
        for (const RemoteFigure& soldier : hostSoldiers) DrawRemoteFigure(soldier, TeamMemberLook(soldier.member));
    }
    particles.Draw();
    grenades.Draw();
    rlDrawRenderBatchActive(); // дүрүүдийг хагас тунгалаг хашаанаас өмнө зурж дуусгана
    world.DrawTransparent(view, material);
    if (camera.IsFirstPerson() && player.IsAlive())
    {
        DrawViewModel(view, player.Held(), player.Recoil(), player.WalkPhase(), player.HorizontalSpeed() / config.walkSpeed,
                      player.ReloadProgress(), player.ActionProgress());
    }
    environment.End();

    // Гэрэлтүүлэггүй: сумны мөр, амны гал
    tracers.Draw();
    particles.DrawUnlit();
    barrels.DrawUnlit(view.position);
    if (state == GameState::Playing)
    {
        mission.DrawBeacons(world, view.position);
    }
    if (showDebug)
    {
        world.DrawColliders(view.position);
    }
    EndMode3D();

    HudInfo info;
    info.provinceName = Provinces()[provinceIndex].name; // монгол нэр (Regions.cpp)
    info.coins = progress.coins;
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
        }
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
        info.teamCount = 2;
        info.team[0] = { selfName.c_str(), (int)std::ceil(player.Health()), player.IsAlive(), true };
        info.team[1] = { "Хост", (int)std::ceil(guestView.host.health), (guestView.host.flags & Net::kAlive) != 0, false };
        info.teamKeys[0] = "Та";
        info.teamKeys[1] = "НЭГ";
        info.squadTotal = 0;
    }
    else if (remoteActive)
    {
        info.partnerName = joinRequester.c_str(); // хост: нэгдсэн тоглогч
        info.partnerHealth = guestFigure.alive ? -1 : 0;
    }
    info.prompt = mission.Prompt();
    info.notice = mission.Notice();
    static char recruitPrompt[96];
    if (nearRecruitStation && state == GameState::Playing)
    {
        if (progress.coins >= kRecruitCost)
            std::snprintf(recruitPrompt, sizeof(recruitPrompt), "%sЦэрэг элсүүлэх — %d зоос", InTouchMode() ? "" : "E — ", kRecruitCost);
        else
            std::snprintf(recruitPrompt, sizeof(recruitPrompt), "Цэрэг элсүүлэх: %d зоос хэрэгтэй (танд %d)", kRecruitCost, progress.coins);
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
        fullMap.Draw(world, player, horde, squad, garage, markers, info.provinceName);
        return;
    }

    if (state == GameState::Won)
    {
        DrawMissionCompleted();
        return;
    }

    DrawCoinPopups();
    DrawMinimap(world, player, camera.Yaw(), horde, squad, garage, markers);
    DrawHud(config, player, info, showDebug);
    DrawCoopOverlay();

    if (journalOpen)
    {
        mission.DrawJournal();
    }
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
    touch.screen = journalOpen ? TouchScreen::Journal : fullMap.IsOpen() ? TouchScreen::FullMap : TouchScreen::Gameplay;

    touch.weaponCount = arsenal.SlotCount();
    touch.weaponNames = IsSniperMember(activeMember) ? sniperWeapons : activeMember == 1 ? heavyWeapons : fighterWeapons;
    touch.weaponCurrent = arsenal.CurrentSlot();
    for (int i = 0; i < touch.weaponCount && i < 8; ++i) touch.weaponTypes[i] = (int)arsenal.SlotWeapon(i);
    touch.sniper = arsenal.Current() == WeaponType::Sniper;
    touch.driving = garage.IsDriving();
    touch.aimAssist = config.touchAimAssist;
    touch.firstPerson = progress.firstPerson;
    touch.joinRequest = joinRequestPending && !coopGuest;
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
    if (nearRecruitStation)
    {
        touch.actLabel = "ЦЭРЭГ ЭЛСҮҮЛЭХ";
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
    ViewBegin();
    if (state == GameState::Map)
    {
        mapScreen.Draw(progress);
        DrawCoopMap();
    }
    else
    {
        DrawMission();
    }
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
    if (snap || Vector3Distance(figure.position, figure.target) > 6.0f) figure.position = figure.target;
    (void)dt;
}

void Game::AdvanceRemoteFigure(RemoteFigure& figure, float dt) const
{
    figure.position = Vector3Lerp(figure.position, figure.target, std::min(1.0f, 12.0f * dt));
    figure.facing += std::remainder(figure.targetFacing - figure.facing, 2.0f * PI) * std::min(1.0f, 12.0f * dt);
    figure.walk += dt * HumanoidGaitRate(figure.speed);
}

void Game::DrawRemoteFigure(const RemoteFigure& figure, const HumanoidLook& baseLook) const
{
    HumanoidLook look = baseLook;
    look.held = (HeldItem)figure.held;
    HumanoidPose pose;
    if (!figure.alive)
    {
        pose = MakeSoldierPose(0.0f, 0.0f, true, (float)GetTime(), 0.0f, look.held);
        pose.fall = 1.0f;
    }
    else
    {
        pose = MakeSoldierPose(figure.walk, figure.speed / config.walkSpeed, true, (float)GetTime(), 0.0f, look.held);
        pose.speed = figure.speed;
        pose.aim = (figure.flags & Net::kAiming) ? 1.0f : 0.0f;
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
    state.aimPitch = 0.0f;
    state.speed = player.HorizontalSpeed();
    state.held = (uint8_t)player.Held();
    state.member = (uint8_t)activeMember;
    state.flags = (uint8_t)((player.IsAlive() ? Net::kAlive : 0) | (player.GetStance() == Stance::Crouch ? Net::kCrouch : 0) |
                            (player.GetStance() == Stance::Prone ? Net::kProne : 0) |
                            (player.Recoil() > 0.5f ? Net::kFired : 0) | (player.IsRiding() ? Net::kRiding : 0) |
                            (InMouseDown(MOUSE_BUTTON_RIGHT) ? Net::kAiming : 0));
    state.health = player.Health();
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
            if (state == GameState::Playing && !coopGuest && !remoteActive)
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
            remoteActive = true;
            if (event.avatar.flags & Net::kFired)
            {
                const Vector3 muzzle = HumanoidLocalToWorld(guestFigure.position, guestFigure.facing,
                                                            HumanoidMuzzleFor((HeldItem)guestFigure.held));
                particles.Flash(muzzle, { std::sin(guestFigure.facing), 0.0f, std::cos(guestFigure.facing) });
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
            if (event.accepted)
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
            if (coopGuest && state == GameState::Playing)
            {
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
        if (remoteActive)
        {
            AdvanceRemoteFigure(guestFigure, dt);
        }
        horde.SetRemoteTarget(remoteActive && guestFigure.alive && !(guestFigure.flags & Net::kRiding), guestFigure.position);
        return;
    }

    // ---- Зочин: хостын snapshot-оор зомбинуудыг хөдөлгөж, өөрийн байдлаа илгээнэ ----
    Net::Snapshot snapshot;
    if (net.TakeSnapshot(snapshot))
    {
        guestView = snapshot;
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
            a.flags = s.alive ? Net::kAlive : 0;
            UpdateRemoteFigure(hostSoldiers[i], a, dt, false);
        }
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
        if (soldier.riding >= 0) continue; // унаанд сууж яваа цэргийг харуулахгүй (энгийн байлгах)
        snapshot.soldiers.push_back(s);
    }
    horde.CollectStates(guestFigure.position, kCoopViewRadius, snapshot.zombies);
    ObjectiveTexts(snapshot.objective, snapshot.detail);
    snapshot.hordeSmall = mission.QuotaSmall() - mission.KilledSmall();
    snapshot.hordeBig = mission.QuotaBig() - mission.KilledBig();
    snapshot.zombieSpeed = horde.SpeedMultiplier();
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
    return { 790.0f, 640.0f + 30.0f * (float)row, 120.0f, 26.0f };
}

void Game::DrawCoopMap() const
{
    if (!net.Available()) return;
    const Rectangle panel = { 430.0f, 600.0f, 500.0f, 108.0f };
    Ui::PanelAccent(panel, Ui::kInfo, Ui::kPanelSoft);
    Ui::TextBold("ХАМТ ТОГЛОХ  (нэг Wi-Fi)", (int)panel.x + 18, (int)panel.y + 10, 15, Ui::kInfo);
    Ui::Text(PlayerName().c_str(), (int)(panel.x + panel.width) - 16 - Ui::Measure(PlayerName().c_str(), 13), (int)panel.y + 12,
             13, Ui::kTextDim);

    const std::vector<Net::FoundGame>& found = net.Found();
    if (waitingForReply)
    {
        Ui::Text("Хүсэлт илгээлээ, хариу хүлээж байна...", (int)panel.x + 18, 642, 15, Ui::kText);
        const Rectangle button = CoopButtonRect(0);
        DrawRectangleRounded(button, 0.5f, 6, Fade(Ui::kDanger, 0.8f));
        Ui::TextBold("ЦУЦЛАХ", (int)(button.x + (button.width - (float)Ui::MeasureBold("ЦУЦЛАХ", 13)) / 2.0f), (int)button.y + 5, 13,
                     Ui::kText);
        return;
    }
    if (found.empty())
    {
        const float dots = std::fmod((float)GetTime(), 1.5f);
        Ui::Text(TextFormat("Ойролцоо тоглож буй хүн хайж байна%s", dots < 0.5f ? "." : dots < 1.0f ? ".." : "..."),
                 (int)panel.x + 18, 642, 15, Ui::kTextDim);
        Ui::Text("Нөгөө хүн аймгаа эхлүүлсэн, нэг Wi-Fi-д байх ёстой", (int)panel.x + 18, 666, 13, Ui::kTextDim);
    }
    for (int i = 0; i < (int)found.size() && i < 2; ++i)
    {
        const Net::FoundGame& game = found[(size_t)i];
        const int y = 642 + 30 * i;
        const char* province = game.province >= 0 && game.province < (int)Provinces().size() ? Provinces()[(size_t)game.province].name : "?";
        Ui::Text(TextFormat("%s  —  %s", game.name.c_str(), province), (int)panel.x + 18, y + 2, 15, Ui::kText);
        const Rectangle button = CoopButtonRect(i);
        DrawRectangleRounded(button, 0.5f, 6, Fade(Ui::kInfo, 0.85f));
        Ui::TextBold("НЭГДЭХ", (int)(button.x + (button.width - (float)Ui::MeasureBold("НЭГДЭХ", 13)) / 2.0f), (int)button.y + 5, 13,
                     { 10, 20, 30, 255 });
    }
    if (!coopStatus.empty())
    {
        Ui::Text(coopStatus.c_str(), (int)panel.x + 18, (int)(panel.y + panel.height) - 20, 13, Ui::kAccent);
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
