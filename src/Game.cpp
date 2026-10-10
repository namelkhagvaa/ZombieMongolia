#include "Game.h"

#include "Hud.h"
#include "Humanoid.h"
#include "Minimap.h"
#include "UiFont.h"
#include "Shop.h"
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

    InitWindow(config.screenWidth, config.screenHeight, "Zombie Mongolia");
    SetTargetFPS(benchmark ? 0 : config.targetFps); // хэмжилтийн үед FPS-ийг хязгаарлахгүй
    SetExitKey(KEY_NULL); // ESC-ийг өөрсдөө боловсруулна: эхлээд нээлттэй цонхыг хаана, дараа нь л гарна
    LoadUiFont();

    // Ойрын хавтгайг холдуулснаар алсад гүний нарийвчлал сайжирна (зам, сүүдэр анивчихгүй).
    rlSetClipPlanes(0.25, 2800.0);
}

GameWindow::~GameWindow()
{
    UnloadUiFont();
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
    LoadProgress(progress, (int)Provinces().size());
    mapScreen.SelectNextTarget(progress);
}

Game::~Game()
{
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
            squad.Reset(options.soldiers, player.Position(), world);
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

        // ESC: нээлттэй цонхыг (даалгаврын журнал, газрын зураг) хаана; цонхгүй бол тоглоомоос гарна.
        if (IsKeyPressed(KEY_ESCAPE))
        {
            if (journalOpen)
            {
                CloseJournal();
            }
            else if (fullMap.IsOpen())
            {
                fullMap.Close();
                DisableCursor();
            }
            else
            {
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
            if (IsKeyPressed(KEY_Q) && !fullMap.IsOpen())
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
            else if (IsKeyPressed(KEY_M) && !journalOpen)
            {
                if (fullMap.IsOpen())
                {
                    fullMap.Close();
                    DisableCursor();
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
            if (IsKeyPressed(KEY_ENTER)) ReturnToMap();
            break;
        }

        Draw();
    }
}

void Game::HandleSettingsInput()
{
    if (IsKeyPressed(KEY_LEFT_BRACKET)) config.mouseSensitivity -= 0.0005f;
    if (IsKeyPressed(KEY_RIGHT_BRACKET)) config.mouseSensitivity += 0.0005f;
    config.mouseSensitivity = Clamp(config.mouseSensitivity, 0.0005f, 0.01f);

    if (IsKeyPressed(KEY_MINUS)) config.walkSpeed -= 0.5f;
    if (IsKeyPressed(KEY_EQUAL)) config.walkSpeed += 0.5f;
    config.walkSpeed = Clamp(config.walkSpeed, 2.0f, 15.0f);

    if (IsKeyPressed(KEY_I)) config.invertY = !config.invertY;
    if (IsKeyPressed(KEY_F3)) showDebug = !showDebug;
    if (IsKeyPressed(KEY_F1)) showHelp = !showHelp;
}

// ---------------------------------------------------------------- Газрын зураг

void Game::UpdateMap()
{
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
    arsenal.Configure(progress.weapon);
    grenades.Clear();
    horde.Clear();
    tracers.Clear();
    particles.Clear();
    fullMap.Close();
    world.Stream(player.Position(), true); // эхлэх цэгийн орчмын газрыг бүгдийг нь нэг дор бэлтгэнэ
    world.CloseAllDoors();
    SpawnVehicles();
    squad.Reset(progress.soldiers, player.Position(), world);
    camera.Snap(player.Position());

    // Даалгаврууд: хадгалагдсан явцаас үргэлжилнэ. Аль хэдийн чөлөөлсөн аймгийг дахин тоглоход шинээр эхэлнэ.
    StageSave& stageSave = progress.stages[(size_t)index];
    if (progress.liberated[index])
    {
        stageSave = StageSave{};
    }
    mission.Start(index, params, world, stageSave);
    journalOpen = false;

    DisableCursor(); // хулганыг цонхонд түгжиж, камерт ашиглана
    state = GameState::Playing;
}

HeldItem Game::HeldFor(WeaponType type) const
{
    switch (type)
    {
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
    // --- Зэвсэг сонгох: 1 хутга, 2 гар буу, 3 автомат, 4 үрлэн буу, 5 гранат ---
    for (int i = 0; i < (int)WeaponType::Count; ++i)
    {
        if (IsKeyPressed(KEY_ONE + i))
        {
            arsenal.Select((WeaponType)i);
        }
    }
    player.SetHeld(HeldFor(arsenal.Current()));

    if (IsKeyPressed(KEY_R))
    {
        arsenal.Reload();
    }

    // Автомат зэвсэг товчийг дарж байхад, бусад нь товшилт бүрд нэг удаа
    const bool autoUse = options.screenshotPath && options.weapon > 0; // туршилтын горим
    const bool trigger = autoUse || (arsenal.Spec().automatic ? IsMouseButtonDown(MOUSE_BUTTON_LEFT)
                                                              : IsMouseButtonPressed(MOUSE_BUTTON_LEFT));
    if (!trigger)
    {
        return;
    }

    player.AimAt(camera.Yaw());
    if (!arsenal.TryUse())
    {
        return;
    }

    // Онилгооны туяа: камераас дэлгэцийн төв рүү
    const Camera3D& cam = camera.Get();
    const Vector3 toTarget = Vector3Subtract(cam.target, cam.position);
    const float cameraDistance = Vector3Length(toTarget);
    const Ray aim = { cam.position, Vector3Scale(toTarget, 1.0f / cameraDistance) };

    switch (arsenal.Current())
    {
    case WeaponType::Knife: UseKnife(aim, cameraDistance); break;
    case WeaponType::Grenade: ThrowGrenade(aim); break;
    default: FireGun(aim, cameraDistance); break;
    }
}

void Game::UseKnife(Ray aim, float cameraDistance)
{
    player.StartAction(0.3f);

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
    const Vector3 muzzle = player.MuzzlePosition();

    // Тархалтыг тооцох тэнхлэгүүд (онилгооны чиглэлд перпендикуляр)
    const Vector3 side = Vector3Normalize(Vector3CrossProduct(aim.direction, { 0.0f, 1.0f, 0.0f }));
    const Vector3 up = Vector3CrossProduct(side, aim.direction);

    for (int pellet = 0; pellet < spec.pellets; ++pellet)
    {
        const float angle = (float)GetRandomValue(0, 359) * DEG2RAD;
        const float offset = spec.spread * std::sqrt((float)GetRandomValue(0, 1000) / 1000.0f);
        Vector3 direction = aim.direction;
        direction = Vector3Add(direction, Vector3Scale(side, std::cos(angle) * offset));
        direction = Vector3Add(direction, Vector3Scale(up, std::sin(angle) * offset));
        const Ray ray = { aim.position, Vector3Normalize(direction) };

        const float reach = cameraDistance + spec.range;
        const float wallDistance = world.RayDistance(ray, reach);
        const ZombieHit hit = horde.Raycast(ray, cameraDistance * 0.9f, wallDistance);

        const float distance = hit.index >= 0 ? hit.distance : wallDistance;
        const Vector3 end = Vector3Add(ray.position, Vector3Scale(ray.direction, distance));
        tracers.Add(muzzle, end, hit.headshot ? RED : YELLOW);

        if (hit.index >= 0)
        {
            // Толгойд оновол нэг сумаар устгана. Аварга зомби л үл хамаарна: толгойд гурав дахин хохирол.
            const float headshot = horde.At(hit.index).boss ? arsenal.Damage() * 3.0f : kHeadshotDamage;
            horde.Damage(hit.index, hit.headshot ? headshot : arsenal.Damage());
            particles.Blood(end, Vector3Negate(ray.direction), hit.headshot ? 14 : (spec.pellets > 1 ? 3 : 7));
        }
        else if (wallDistance < reach)
        {
            particles.Dust(end); // газар эсвэл хананд туссан
        }
    }

    // Бууны эффект: ухралт, амны гал, баруун тийш үсрэх хонгио
    const float facing = player.Facing();
    const Vector3 rightSide = { -std::cos(facing), 0.0f, std::sin(facing) };
    const Vector3 forward = { std::sin(facing), 0.0f, std::cos(facing) };
    player.OnFired();
    particles.Flash(muzzle, Vector3Normalize(Vector3Add(aim.direction, forward)));
    particles.Shell(Vector3Add(muzzle, Vector3Scale(forward, -0.4f)), rightSide);
}

void Game::ThrowGrenade(Ray aim)
{
    player.StartAction(0.45f);

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

    if (vehicle >= 0 && vehicleDistance <= doorDistance) result.vehicle = vehicle;
    else if (door >= 0) result.door = door;
    return result;
}

void Game::UpdateVehicles(float dt)
{
    const bool autoDrive = options.screenshotPath && options.ride > 0; // туршилтын горим

    // --- F: унаанд суух / буух, хаалга нээх / хаах ---
    if (IsKeyPressed(KEY_F))
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
        }
    }

    // --- Жолоодлого: W/S хий ба тоормос, A/D эргэлт, Space гар тоормос ---
    float throttle = 0.0f, steer = 0.0f;
    bool handbrake = false;
    if (garage.IsDriving())
    {
        throttle = (float)IsKeyDown(KEY_W) - (float)IsKeyDown(KEY_S);
        steer = (float)IsKeyDown(KEY_D) - (float)IsKeyDown(KEY_A);
        handbrake = IsKeyDown(KEY_SPACE);
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

void Game::UpdateMission(float dt)
{
    HandleSettingsInput();

    if (IsKeyPressed(KEY_TAB))
    {
        ReturnToMap();
        return;
    }

    UpdateVehicles(dt);
    world.UpdateDoors(dt);
    world.Stream(player.Position()); // явах тусам урд талын газрыг бага багаар үүсгэнэ

    const bool enclosed = garage.IsDriving() && SpecFor(garage.At(garage.Driven()).type).enclosed;
    const float cameraExtra = garage.IsDriving() ? SpecFor(garage.At(garage.Driven()).type).cameraExtra : 0.0f;
    const bool aiming = IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && !enclosed;
    camera.Update(dt, config, world, player.Position(), aiming, cameraExtra);
    if (aiming)
    {
        player.AimAt(camera.Yaw());
    }

    arsenal.Update(dt);
    if (!enclosed)
    {
        UpdateShooting(); // бүхээгтэй машин дотроос буудаж болохгүй
    }

    squad.Update(dt, world, player.Position(), horde, tracers, particles);
    grenades.Update(dt, world, horde, player, particles);
    particles.Update(dt, world);
    horde.Update(dt, world, player, squad);
    tracers.Update(dt);

    int kills = 0;
    horde.TakeRewards(progress.coins, kills);
    progress.kills += kills;
    missionKills += kills;

    if (!player.IsAlive())
    {
        Save();
        state = GameState::Lost;
        return;
    }

    // --- Даалгаврууд: E товчоор объекттой харилцана (унаанаас буусан үед) ---
    bool interact = IsKeyPressed(KEY_E);
    if (options.screenshotPath && options.autoPlay)
    {
        interact = mission.AutoPlay(dt, world, player, horde) || interact; // туршилтын автомат тоглолт
        if (options.stopAfter > 0 && mission.CompletedCount() >= options.stopAfter)
        {
            interact = false; // заасан тооны даалгаврын дараа зогсоно (хадгалалтыг шалгахад)
        }
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

    // Бүх даалгавар биелсэн: аймаг чөлөөлөгдөнө.
    if (mission.IsComplete())
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
        state = GameState::Won;
    }
}

void Game::CloseJournal()
{
    journalOpen = false;
    if (state == GameState::Playing)
    {
        DisableCursor();
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
    garage.Draw(view.position, config.vehicleDrawDistance);
    mission.Draw(view.position);
    squad.Draw(view.position, showDebug);
    horde.Draw(view.position, config.characterDrawDistance, showDebug);
    player.Draw(config, showDebug);
    particles.Draw();
    grenades.Draw();
    rlDrawRenderBatchActive(); // дүрүүдийг хагас тунгалаг хашаанаас өмнө зурж дуусгана
    world.DrawTransparent(view, material);
    environment.End();

    // Гэрэлтүүлэггүй: сумны мөр, амны гал
    tracers.Draw();
    particles.DrawUnlit();
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
    info.provinceName = Provinces()[provinceIndex].name;
    info.coins = progress.coins;
    info.missionKills = missionKills;
    info.ammo = arsenal.Ammo();
    info.magazine = arsenal.MagazineSize();
    info.reloading = arsenal.IsReloading();
    info.weaponSlot = (int)arsenal.Current();
    info.usesAmmo = arsenal.UsesAmmo();
    info.showMagazine = arsenal.Current() != WeaponType::Grenade;
    info.squadAlive = squad.AliveCount();
    info.squadTotal = squad.Total();
    info.showHelp = showHelp;

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
    info.prompt = mission.Prompt();
    info.notice = mission.Notice();

    if (state == GameState::Lost)
    {
        DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(MAROON, 0.5f));
        info.banner = "YOU DIED";
        info.subBanner = "Coins and finished objectives are kept.     ENTER: back to map";
    }

    // Унааны мэдээлэл: унаж яваа бол хурд ба гадаргуу, үгүй бол ойрын унааны санамж
    char rideText[96] = "";
    if (garage.IsDriving())
    {
        const Vehicle& vehicle = garage.At(garage.Driven());
        const char* surface = garage.DrivenSurface() == Surface::Asphalt ? "ASPHALT"
                            : garage.DrivenSurface() == Surface::Dirt ? "DIRT ROAD" : "STEPPE";
        std::snprintf(rideText, sizeof(rideText), "%s  %d km/h  (max here %d)  %s     F: get off",
                      SpecFor(vehicle.type).name, (int)(std::fabs(vehicle.speed) * 3.6f),
                      (int)(garage.DrivenMaxSpeed() * 3.6f), surface);
        info.rideText = rideText;
    }
    else if (state == GameState::Playing)
    {
        const Interaction target = FindInteraction();
        if (target.vehicle >= 0)
        {
            std::snprintf(rideText, sizeof(rideText), "F: get in %s", SpecFor(garage.At(target.vehicle).type).name);
            info.rideText = rideText;
        }
        else if (target.door >= 0)
        {
            info.rideText = world.IsDoorOpen(target.door) ? "F: close door" : "F: open door";
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

    DrawMinimap(world, player, camera.Yaw(), horde, squad, garage, markers);
    DrawHud(config, player, info, showDebug);

    if (journalOpen)
    {
        mission.DrawJournal();
    }
}

void Game::DrawMissionCompleted() const
{
    const int width = GetScreenWidth();
    const int height = GetScreenHeight();
    const Color gold = { 250, 214, 70, 255 };
    const Color text = { 236, 232, 220, 255 };
    auto centered = [&](const char* line, int y, int size, Color color) {
        UiTextShadow(line, (width - UiMeasure(line, size)) / 2, y, size, color);
    };

    DrawRectangle(0, 0, width, height, Fade({ 14, 40, 22, 255 }, 0.82f));
    centered("MISSION COMPLETED", 120, 64, gold);

    char line[200];
    std::snprintf(line, sizeof(line), "%s — ҮЕ %02d / %02d", mission.Stage().provinceName, provinceIndex + 1,
                  (int)Provinces().size());
    centered(line, 206, 32, text);

    std::snprintf(line, sizeof(line), "Биелүүлсэн даалгавар: %d / %d", mission.CompletedCount(), mission.ObjectiveCount());
    centered(line, 280, 26, text);

    if (lastReward > 0) std::snprintf(line, sizeof(line), "Аймаг чөлөөлсний шагнал: %d зоос", lastReward);
    else std::snprintf(line, sizeof(line), "Энэ аймаг өмнө нь чөлөөлөгдсөн (нэмэлт шагналгүй)");
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

    centered("ENTER — газрын зураг руу буцаж үргэлжлүүлэх", 520, 24, text);
}

void Game::Draw() const
{
    BeginDrawing();
    if (state == GameState::Map)
    {
        mapScreen.Draw(progress);
    }
    else
    {
        DrawMission();
    }
    EndDrawing();
}
