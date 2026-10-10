#include "Mission.h"

#include "Humanoid.h"
#include "Mover.h"
#include "Procedural.h"
#include "UiFont.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
    const float kInteractDistance = 2.8f;
    const float kReachRadius = 9.0f;
    const float kDefendRadius = 30.0f;
    const float kZoneRadius = 42.0f;
    const float kZoneWakeDistance = 130.0f; // тоглогч ийм ойртоход бүсийн зомби гарч эхэлнэ
    const float kNoticeTime = 4.5f;
    const float kDrawDistance = 160.0f;

    const Color kMarker = { 250, 214, 70, 255 };
    const Color kDoneGreen = { 96, 200, 110, 255 };

    bool IsZoneType(ObjectiveType type)
    {
        return type == ObjectiveType::ClearZone || type == ObjectiveType::DefendLocation ||
               type == ObjectiveType::DefeatBoss;
    }

    // Хүнд аймагт хурдан ба том зомби олширно.
    ZombieType PickType(int province)
    {
        const int roll = GetRandomValue(0, 99);
        if (province >= 3 && roll < 6 + province) return ZombieType::Brute;
        if (province >= 1 && roll < 25 + 2 * province) return ZombieType::Runner;
        return ZombieType::Walker;
    }

    // center-ийн эргэн тойронд, саад ба барилгын доторх биш, хилийн доторх чөлөөт цэг.
    Vector3 PlaceAround(const World& world, Vector3 center, float minRadius, float maxRadius, Rng& rng)
    {
        Vector3 best = center;
        for (int attempt = 0; attempt < 30; ++attempt)
        {
            const float angle = rng.Range(0.0f, 2.0f * PI);
            const float distance = rng.Range(minRadius, maxRadius);
            Vector3 candidate = { center.x + std::sin(angle) * distance, 0.0f, center.z + std::cos(angle) * distance };
            candidate.y = world.GroundHeight(candidate.x, candidate.z);

            Vector3 resolved = candidate;
            world.ResolveHorizontal(resolved, 0.6f, 1.8f, 0.3f);
            best = resolved;

            const bool free = HorizontalDistance(candidate, resolved) < 0.05f;
            if (free && world.InteriorAt(candidate.x, candidate.z) < 0 && world.InsideBoundary(candidate.x, candidate.z))
            {
                return candidate;
            }
        }
        best.y = world.GroundHeight(best.x, best.z);
        return best;
    }

    // Санамсаргүй (тоглоомын явцын) цэг: spawn-д.
    Vector3 RandomAround(const World& world, Vector3 center, float minRadius, float maxRadius)
    {
        Vector3 position = center;
        for (int attempt = 0; attempt < 12; ++attempt)
        {
            const float angle = (float)GetRandomValue(0, 359) * DEG2RAD;
            const float distance = minRadius + (maxRadius - minRadius) * (float)GetRandomValue(0, 1000) / 1000.0f;
            position = { center.x + std::sin(angle) * distance, 0.0f, center.z + std::cos(angle) * distance };
            if (world.InsideBoundary(position.x, position.z) && world.InteriorAt(position.x, position.z) < 0)
            {
                break;
            }
        }
        world.ResolveHorizontal(position, 0.6f, 1.8f, 0.3f);
        position.y = world.GroundHeight(position.x, position.z);
        return position;
    }

    // ------------------------------------------------------------------ Объектуудын зураглал

    void DrawGenerator(Vector3 p, bool running)
    {
        const Color body = { 70, 96, 62, 255 };
        DrawCubeV({ p.x, p.y + 0.12f, p.z }, { 1.7f, 0.12f, 1.0f }, { 50, 50, 54, 255 });       // суурь
        DrawCubeV({ p.x - 0.25f, p.y + 0.62f, p.z }, { 1.0f, 0.86f, 0.8f }, body);               // хөдөлгүүрийн хайрцаг
        DrawCubeV({ p.x + 0.52f, p.y + 0.5f, p.z }, { 0.5f, 0.6f, 0.7f }, { 150, 44, 38, 255 }); // түлшний сав
        DrawCubeV({ p.x - 0.25f, p.y + 1.25f, p.z + 0.2f }, { 0.1f, 0.5f, 0.1f }, { 40, 40, 42, 255 }); // яндан
        DrawCubeV({ p.x - 0.25f, p.y + 0.7f, p.z + 0.41f }, { 0.5f, 0.3f, 0.04f }, { 30, 32, 36, 255 }); // самбар
        // Заагч гэрэл: унтраалттай улаан, ажиллаж байвал ногоон
        DrawCubeV({ p.x - 0.1f, p.y + 0.75f, p.z + 0.44f }, { 0.1f, 0.1f, 0.04f },
                  running ? Color{ 80, 240, 90, 255 } : Color{ 230, 60, 50, 255 });
    }

    void DrawRadio(Vector3 p, bool running)
    {
        const Color wood = { 120, 96, 70, 255 };
        DrawCubeV({ p.x, p.y + 0.78f, p.z }, { 1.3f, 0.06f, 0.7f }, wood); // ширээ
        for (float dx : { -0.58f, 0.58f })
        {
            for (float dz : { -0.28f, 0.28f })
            {
                DrawCubeV({ p.x + dx, p.y + 0.38f, p.z + dz }, { 0.07f, 0.76f, 0.07f }, wood);
            }
        }
        DrawCubeV({ p.x - 0.15f, p.y + 1.02f, p.z }, { 0.7f, 0.42f, 0.4f }, { 66, 78, 56, 255 });      // радио
        DrawCubeV({ p.x - 0.15f, p.y + 1.02f, p.z + 0.21f }, { 0.5f, 0.2f, 0.02f }, { 28, 30, 32, 255 });
        DrawCubeV({ p.x + 0.12f, p.y + 2.3f, p.z - 0.15f }, { 0.03f, 2.2f, 0.03f }, { 30, 30, 30, 255 }); // антен
        DrawCubeV({ p.x - 0.4f, p.y + 1.12f, p.z + 0.22f }, { 0.08f, 0.08f, 0.03f },
                  running ? Color{ 80, 240, 90, 255 } : Color{ 230, 60, 50, 255 });
    }

    void DrawItem(Vector3 p, ItemKind kind)
    {
        switch (kind)
        {
        case ItemKind::Fuel: // улаан түлшний сав
            DrawCubeV({ p.x, p.y + 0.26f, p.z }, { 0.36f, 0.5f, 0.2f }, { 176, 44, 36, 255 });
            DrawCubeV({ p.x, p.y + 0.56f, p.z }, { 0.2f, 0.06f, 0.08f }, { 60, 60, 60, 255 });
            DrawCubeV({ p.x + 0.12f, p.y + 0.55f, p.z }, { 0.07f, 0.1f, 0.07f }, { 30, 30, 30, 255 });
            break;
        case ItemKind::Medicine: // улаан загалмайтай цагаан хайрцаг
            DrawCubeV({ p.x, p.y + 0.18f, p.z }, { 0.5f, 0.34f, 0.36f }, { 232, 230, 224, 255 });
            DrawCubeV({ p.x, p.y + 0.36f, p.z }, { 0.3f, 0.02f, 0.09f }, { 200, 40, 40, 255 });
            DrawCubeV({ p.x, p.y + 0.36f, p.z }, { 0.09f, 0.02f, 0.3f }, { 200, 40, 40, 255 });
            break;
        case ItemKind::Ammo: // ногоон сумны хайрцаг
            DrawCubeV({ p.x, p.y + 0.16f, p.z }, { 0.56f, 0.3f, 0.3f }, { 78, 92, 56, 255 });
            DrawCubeV({ p.x, p.y + 0.33f, p.z }, { 0.58f, 0.04f, 0.32f }, { 56, 66, 40, 255 });
            DrawCubeV({ p.x, p.y + 0.4f, p.z }, { 0.2f, 0.06f, 0.04f }, { 40, 40, 40, 255 });
            break;
        case ItemKind::Food: // хүнсний уут
            DrawCubeV({ p.x, p.y + 0.22f, p.z }, { 0.5f, 0.42f, 0.36f }, { 190, 168, 120, 255 });
            DrawCubeV({ p.x, p.y + 0.47f, p.z }, { 0.2f, 0.1f, 0.2f }, { 170, 148, 104, 255 });
            break;
        }
    }

    void DrawDropPoint(Vector3 p, bool delivered)
    {
        // Тавцан ба далбаа
        DrawCubeV({ p.x, p.y + 0.08f, p.z }, { 1.8f, 0.14f, 1.4f }, { 132, 106, 78, 255 });
        DrawCubeV({ p.x + 0.8f, p.y + 1.3f, p.z + 0.6f }, { 0.06f, 2.6f, 0.06f }, { 200, 196, 186, 255 });
        DrawCubeV({ p.x + 0.45f, p.y + 2.3f, p.z + 0.6f }, { 0.7f, 0.45f, 0.03f },
                  delivered ? kDoneGreen : Color{ 236, 190, 50, 255 });
        if (delivered)
        {
            DrawCubeV({ p.x - 0.2f, p.y + 0.42f, p.z }, { 0.7f, 0.54f, 0.6f }, { 150, 120, 84, 255 }); // хүргэсэн ачаа
        }
    }

    HumanoidLook SurvivorLook(SurvivorKind kind, int variant)
    {
        HumanoidLook look;
        if (kind == SurvivorKind::Soldier)
        {
            look.kind = HumanoidKind::Soldier;
            look.pants = { 74, 84, 56, 255 };
            look.boots = { 30, 28, 24, 255 };
            look.body = { 92, 108, 68, 255 };
            look.hat = { 74, 86, 56, 255 };
            look.vest = { 62, 68, 48, 255 };
            look.skin = { 210, 164, 124, 255 };
        }
        else
        {
            // Дээлтэй малчин: өнгө нь хүн бүрд өөр
            static const Color deels[] = { { 124, 70, 50, 255 }, { 56, 110, 92, 255 }, { 116, 60, 104, 255 },
                                           { 60, 84, 132, 255 } };
            look.kind = HumanoidKind::Player;
            look.pants = { 60, 54, 48, 255 };
            look.boots = { 52, 36, 26, 255 };
            look.body = deels[variant % 4];
            look.trim = { 226, 196, 90, 255 };
            look.belt = look.trim;
            look.hat = { 70, 60, 110, 255 };
            look.vest = { 110, 96, 70, 255 };
            look.skin = { 214, 168, 128, 255 };
        }
        return look;
    }

    // Олон мөрөнд хуваан бичнэ. Буцаах утга: зурсан мөрийн тоо.
    int DrawWrapped(const std::string& text, int x, int y, int size, int maxWidth, Color color)
    {
        int lines = 0;
        std::string line;
        size_t start = 0;
        while (start <= text.size())
        {
            const size_t space = text.find(' ', start);
            const std::string word = text.substr(start, space == std::string::npos ? std::string::npos : space - start);
            const std::string candidate = line.empty() ? word : line + " " + word;

            if (!line.empty() && UiMeasure(candidate.c_str(), size) > maxWidth)
            {
                UiText(line.c_str(), x, y + lines * (size + 4), size, color);
                ++lines;
                line = word;
            }
            else
            {
                line = candidate;
            }

            if (space == std::string::npos) break;
            start = space + 1;
        }
        if (!line.empty())
        {
            UiText(line.c_str(), x, y + lines * (size + 4), size, color);
            ++lines;
        }
        return lines;
    }
}

// ---------------------------------------------------------------- Эхлүүлэх

Vector3 Mission::ResolveSite(const World& world, SiteRef site) const
{
    auto ground = [&](float x, float z) { return Vector3{ x, world.GroundHeight(x, z), z }; };

    // Эхлэх цэгт ойрын N дэх, аймгийн хилийн доторх цэг
    auto nth = [&](const std::vector<Vector2>& points, int ordinal, Vector2& out) {
        std::vector<Vector2> inside;
        for (const Vector2& p : points)
        {
            if (world.InsideBoundary(p.x, p.y)) inside.push_back(p);
        }
        if (inside.empty()) return false;
        std::sort(inside.begin(), inside.end(), [](const Vector2& a, const Vector2& b) {
            return a.x * a.x + a.y * a.y < b.x * b.x + b.y * b.y;
        });
        out = inside[(size_t)std::min(ordinal, (int)inside.size() - 1)];
        return true;
    };

    Vector2 point;
    switch (site.kind)
    {
    case SiteKind::Checkpoint: return ground(-48.0f, 17.0f); // элсэн уутан хоргодох байрны хойд талын талбай
    case SiteKind::GasStation: return ground(31.0f, -21.0f);
    case SiteKind::Warehouse: return ground(27.0f, 70.0f);   // агуулахын хаалганы өмнө
    case SiteKind::Camp:
        if (nth(world.GetLayout().camps, site.ordinal, point)) return ground(point.x - 3.0f, point.y - 9.0f);
        break;
    case SiteKind::Hamlet:
        if (nth(world.GetLayout().hamlets, site.ordinal, point)) return ground(point.x - 34.0f, point.y + 8.0f);
        break;
    case SiteKind::Settlement:
        break;
    }
    return ground(26.0f, 32.0f); // төв суурингийн гудамж (олдоогүй отор, суурингийн орлуулга)
}

void Mission::Start(int provinceIndex, const MissionParams& missionParams, const World& world, StageSave& stageSave)
{
    province = provinceIndex;
    params = missionParams;
    stage = &StageFor(provinceIndex);
    save = &stageSave;

    prompt.clear();
    notice.clear();
    noticeTimer = 0.0f;
    ambientTimer = 4.0f;
    autoTimer = 0.0f;
    rewardPending = 0;
    dirty = false;

    const int count = std::min((int)stage->objectives.size(), StageSave::kMaxObjectives);
    states.assign((size_t)count, ObjectiveState{});

    for (int i = 0; i < count; ++i)
    {
        const ObjectiveDef& def = stage->objectives[(size_t)i];
        ObjectiveState& state = states[(size_t)i];
        Rng rng((uint32_t)(provinceIndex * 131 + i * 17 + 7)); // байрлал нь тоглох бүрд ижил
        const bool done = (save->done >> i) & 1u;

        state.location = ResolveSite(world, def.site);
        state.status = done ? ObjectiveStatus::Done : ObjectiveStatus::Locked;

        switch (def.type)
        {
        case ObjectiveType::ReachLocation:
            state.radius = kReachRadius;
            break;

        case ObjectiveType::InteractObject:
            state.entities.push_back({ PlaceAround(world, state.location, 2.0f, 6.0f, rng), rng.Range(0.0f, 6.28f), done });
            break;

        case ObjectiveType::RescueSurvivors:
        case ObjectiveType::CollectItems:
        {
            const bool rescue = def.type == ObjectiveType::RescueSurvivors;
            state.target = def.count;
            for (int k = 0; k < def.count; ++k)
            {
                MissionEntity entity;
                entity.position = PlaceAround(world, state.location, rescue ? 3.0f : 4.0f, rescue ? 12.0f : 18.0f, rng);
                entity.facing = rng.Range(0.0f, 6.28f);
                entity.used = done || ((save->entities[i] >> k) & 1u);
                state.progress += entity.used;
                state.entities.push_back(entity);
            }
            break;
        }

        case ObjectiveType::DeliverItems:
            state.entities.push_back({ PlaceAround(world, state.location, 2.0f, 5.0f, rng), 0.0f, done });
            break;

        case ObjectiveType::ClearZone:
            state.radius = kZoneRadius;
            state.target = std::max(4, (8 + provinceIndex) * def.count / 100);
            state.reserve = done ? 0 : state.target;
            state.progress = done ? state.target : 0;
            break;

        case ObjectiveType::DefendLocation:
            state.radius = kDefendRadius;
            state.target = (int)def.seconds;
            state.progress = done ? state.target : 0;
            break;

        case ObjectiveType::DefeatBoss:
            state.radius = 14.0f;
            state.progress = done ? 1 : 0;
            break;
        }
    }

    // Урьдчилсан нөхцөл нь биелсэн (эсвэл байхгүй) даалгавруудыг идэвхжүүлнэ.
    for (int i = 0; i < count; ++i)
    {
        const int after = stage->objectives[(size_t)i].after;
        if (states[(size_t)i].status == ObjectiveStatus::Locked &&
            (after < 0 || (after < count && states[(size_t)after].status == ObjectiveStatus::Done)))
        {
            Activate(i);
        }
    }
}

void Mission::Activate(int index)
{
    states[(size_t)index].status = ObjectiveStatus::Active;
}

void Mission::Complete(int index)
{
    ObjectiveState& state = states[(size_t)index];
    if (state.status == ObjectiveStatus::Done)
    {
        return;
    }

    const ObjectiveDef& def = stage->objectives[(size_t)index];
    state.status = ObjectiveStatus::Done;
    save->done |= 1u << index;
    if (def.type == ObjectiveType::ClearZone)
    {
        save->clearedZones |= 1u << index; // энэ бүсэд зомби дахин гарахгүй
    }

    rewardPending += def.reward;
    notice = std::string("ДААЛГАВАР БИЕЛЛЭЭ: ") + def.title;
    noticeTimer = kNoticeTime;
    dirty = true;
    TraceLog(LOG_INFO, "MISSION: stage %d objective %d (%s) completed", province + 1, index + 1, def.id);

    // Энэ даалгавраас хамаарсан дараагийн даалгавруудыг нээнэ.
    for (int j = 0; j < ObjectiveCount(); ++j)
    {
        if (states[(size_t)j].status == ObjectiveStatus::Locked && stage->objectives[(size_t)j].after == index)
        {
            Activate(j);
        }
    }
}

// ---------------------------------------------------------------- Төлөв

bool Mission::IsComplete() const
{
    for (const ObjectiveState& state : states)
    {
        if (state.status != ObjectiveStatus::Done) return false;
    }
    return !states.empty();
}

bool Mission::TakeDirty()
{
    const bool result = dirty;
    dirty = false;
    return result;
}

int Mission::TakeReward()
{
    const int result = rewardPending;
    rewardPending = 0;
    return result;
}

int Mission::CompletedCount() const
{
    int count = 0;
    for (const ObjectiveState& state : states) count += state.status == ObjectiveStatus::Done;
    return count;
}

int Mission::FocusObjective() const
{
    for (int i = 0; i < ObjectiveCount(); ++i)
    {
        if (states[(size_t)i].status == ObjectiveStatus::Active) return i;
    }
    return -1;
}

std::string Mission::ProgressText(int index) const
{
    const ObjectiveDef& def = stage->objectives[(size_t)index];
    const ObjectiveState& state = states[(size_t)index];
    char buffer[64] = "";

    switch (def.type)
    {
    case ObjectiveType::RescueSurvivors:
    case ObjectiveType::CollectItems:
        std::snprintf(buffer, sizeof(buffer), "%d / %d", state.progress, state.target);
        break;
    case ObjectiveType::ClearZone:
        std::snprintf(buffer, sizeof(buffer), "%d / %d зомби", state.progress, state.target);
        break;
    case ObjectiveType::DefendLocation:
        std::snprintf(buffer, sizeof(buffer), "%d / %d сек", state.progress, state.target);
        break;
    default:
        break;
    }
    return buffer;
}

float Mission::DistanceTo(int index, Vector3 from) const
{
    const ObjectiveState& state = states[(size_t)index];
    float best = HorizontalDistance(from, state.location);

    // Аврах хүн, цуглуулах зүйлтэй бол хамгийн ойрын үлдсэн нь
    bool any = false;
    for (const MissionEntity& entity : state.entities)
    {
        if (entity.used) continue;
        const float distance = HorizontalDistance(from, entity.position);
        best = any ? std::min(best, distance) : distance;
        any = true;
    }
    return best;
}

void Mission::Markers(std::vector<Vector3>& out) const
{
    for (const ObjectiveState& state : states)
    {
        if (state.status == ObjectiveStatus::Active) out.push_back(state.location);
    }
}

bool Mission::InClearedZone(Vector3 position) const
{
    for (int i = 0; i < ObjectiveCount(); ++i)
    {
        if (((save->clearedZones >> i) & 1u) && HorizontalDistance(position, states[(size_t)i].location) < kZoneRadius + 10.0f)
        {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------- Шинэчлэл

void Mission::UpdateZone(int index, float dt, const World& world, const Player& player, ZombieHorde& horde)
{
    const ObjectiveDef& def = stage->objectives[(size_t)index];
    ObjectiveState& state = states[(size_t)index];
    const int zone = index + 1;
    const float playerDistance = HorizontalDistance(player.Position(), state.location);
    state.spawnTimer -= dt;

    if (def.type == ObjectiveType::ClearZone)
    {
        // Бүсийн нөөц: тоглогч ойртоход дотор нь бага багаар гарч ирнэ; нөөц дуусвал дахин гарахгүй.
        const int cap = std::min(14 + province / 3, state.target);
        int alive = horde.AliveInZone(zone);
        if (playerDistance < state.radius + kZoneWakeDistance)
        {
            while (state.reserve > 0 && alive < cap && state.spawnTimer <= 0.0f)
            {
                horde.Spawn(PickType(province), RandomAround(world, state.location, 4.0f, state.radius), params, zone, false);
                --state.reserve;
                ++alive;
                state.spawnTimer = alive < cap * 2 / 3 ? 0.0f : 1.0f; // эхний хэсэг шууд, дараа нь нэг нэгээр
            }
        }

        state.progress = state.target - state.reserve - alive;
        if (state.reserve == 0 && alive == 0)
        {
            Complete(index);
        }
    }
    else if (def.type == ObjectiveType::DefendLocation)
    {
        // Хугацаа зөвхөн тоглогч бүс дотор байхад урагшилна; энэ үед зомби тасралтгүй довтолно.
        if (playerDistance < state.radius)
        {
            state.timer += dt;
            const int cap = 8 + province / 3;
            if (state.spawnTimer <= 0.0f && horde.AliveInZone(zone) < cap)
            {
                horde.Spawn(PickType(province), RandomAround(world, state.location, 42.0f, 58.0f), params, zone, true);
                state.spawnTimer = 1.6f;
            }
        }
        state.progress = std::min(state.target, (int)state.timer);
        if (state.timer >= def.seconds)
        {
            Complete(index);
        }
    }
    else // DefeatBoss
    {
        if (!state.spawned && playerDistance < 110.0f)
        {
            horde.SpawnBoss(RandomAround(world, state.location, 0.0f, 6.0f), params, zone);
            for (int k = 0; k < 3; ++k)
            {
                horde.Spawn(PickType(province), RandomAround(world, state.location, 4.0f, 12.0f), params, zone, false);
            }
            state.spawned = true;
        }
        if (state.spawned && horde.AliveInZone(zone) == 0)
        {
            state.progress = 1;
            Complete(index);
        }
    }
}

void Mission::UpdateAmbient(float dt, const World& world, const Player& player, ZombieHorde& horde)
{
    // Тэнүүчилсэн зомби: ертөнц хоосон биш байхын тулд тоглогчийн эргэн тойронд цөөн тоогоор гарна.
    // Цэвэрлэсэн бүс дотор хэзээ ч гарахгүй.
    ambientTimer -= dt;
    if (ambientTimer > 0.0f)
    {
        return;
    }
    ambientTimer = 7.0f;

    if (horde.AliveInZone(0) >= 4 + province / 5)
    {
        return;
    }
    const Vector3 position = RandomAround(world, player.Position(), 48.0f, 78.0f);
    if (!InClearedZone(position) && HorizontalDistance(position, player.Position()) > 35.0f)
    {
        horde.Spawn(PickType(province), position, params, 0, true);
    }
}

void Mission::Update(float dt, World& world, Player& player, ZombieHorde& horde, SoldierSquad& squad,
                     bool interactPressed, bool onFoot)
{
    prompt.clear();
    noticeTimer -= dt;

    // Хамгийн ойрын харилцаж болох объект (идэвхтэй даалгавруудын дундаас)
    int nearestObjective = -1, nearestEntity = -1;
    float nearestDistance = 1e9f;
    const Vector3 position = player.Position();

    for (int i = 0; i < ObjectiveCount(); ++i)
    {
        ObjectiveState& state = states[(size_t)i];
        if (state.status != ObjectiveStatus::Active)
        {
            continue;
        }
        const ObjectiveDef& def = stage->objectives[(size_t)i];

        // Байршлыг сахих зомби: даалгавар идэвхжихэд нэг удаа гарна.
        if (!state.spawned && def.guards > 0 && !IsZoneType(def.type) &&
            HorizontalDistance(position, state.location) < kZoneWakeDistance)
        {
            for (int k = 0; k < def.guards; ++k)
            {
                horde.Spawn(PickType(province), RandomAround(world, state.location, 6.0f, 18.0f), params, i + 1, false);
            }
            state.spawned = true;
        }

        switch (def.type)
        {
        case ObjectiveType::ReachLocation:
            if (HorizontalDistance(position, state.location) < state.radius)
            {
                Complete(i);
            }
            break;

        case ObjectiveType::InteractObject:
        case ObjectiveType::RescueSurvivors:
        case ObjectiveType::CollectItems:
        case ObjectiveType::DeliverItems:
            for (int k = 0; k < (int)state.entities.size(); ++k)
            {
                if (state.entities[(size_t)k].used) continue;
                const float reach = def.type == ObjectiveType::DeliverItems ? kInteractDistance + 1.0f : kInteractDistance;
                const float distance = HorizontalDistance(position, state.entities[(size_t)k].position);
                if (distance < reach && distance < nearestDistance)
                {
                    nearestDistance = distance;
                    nearestObjective = i;
                    nearestEntity = k;
                }
            }
            break;

        case ObjectiveType::ClearZone:
        case ObjectiveType::DefendLocation:
        case ObjectiveType::DefeatBoss:
            UpdateZone(i, dt, world, player, horde);
            break;
        }
    }

    UpdateAmbient(dt, world, player, horde);

    // --- E товч: ойрын объекттой харилцах ---
    if (nearestObjective < 0)
    {
        return;
    }
    if (!onFoot)
    {
        prompt = "Унаанаасаа бууж байж харилцана (F)";
        return;
    }

    const ObjectiveDef& def = stage->objectives[(size_t)nearestObjective];
    ObjectiveState& state = states[(size_t)nearestObjective];
    prompt = std::string("E \xE2\x80\x94 ") + def.action; // "E — ..."

    if (!interactPressed)
    {
        return;
    }

    MissionEntity& entity = state.entities[(size_t)nearestEntity];
    entity.used = true;
    prompt.clear();

    switch (def.type)
    {
    case ObjectiveType::RescueSurvivors:
    case ObjectiveType::CollectItems:
        save->entities[nearestObjective] |= 1u << nearestEntity;
        ++state.progress;
        dirty = true;
        if (def.type == ObjectiveType::RescueSurvivors && def.survivor == SurvivorKind::Soldier)
        {
            squad.Add(entity.position, entity.facing); // аврагдсан цэрэг багт нэгдэнэ
        }
        if (state.progress >= state.target)
        {
            Complete(nearestObjective);
        }
        break;

    default: // InteractObject, DeliverItems
        Complete(nearestObjective);
        break;
    }
}

// ---------------------------------------------------------------- Туршилтын автомат тоглолт

bool Mission::AutoPlay(float dt, const World& world, Player& player, ZombieHorde& horde)
{
    player.Heal(1000.0f); // туршилтын тоглогч үхэхгүй (даалгаврын логикийг л шалгана, тулааны хүндрэлийг биш)

    autoTimer -= dt;
    if (autoTimer > 0.0f)
    {
        return false;
    }
    autoTimer = 0.25f;

    const int focus = FocusObjective();
    if (focus < 0)
    {
        return false;
    }
    const ObjectiveDef& def = stage->objectives[(size_t)focus];
    const ObjectiveState& state = states[(size_t)focus];

    auto moveTo = [&](Vector3 target) {
        if (HorizontalDistance(player.Position(), target) > 1.2f)
        {
            player.Teleport({ target.x + 0.8f, world.GroundHeight(target.x + 0.8f, target.z), target.z });
        }
    };

    switch (def.type)
    {
    case ObjectiveType::ReachLocation:
        moveTo(state.location);
        return false;

    case ObjectiveType::InteractObject:
    case ObjectiveType::RescueSurvivors:
    case ObjectiveType::CollectItems:
    case ObjectiveType::DeliverItems:
        for (const MissionEntity& entity : state.entities)
        {
            if (!entity.used)
            {
                moveTo(entity.position);
                return true; // E дарна
            }
        }
        return false;

    default: // бүсийн төрлүүд: байршилд зогсож, бүсийн зомбийг "буудна"
        moveTo(state.location);
        horde.DamageZone(focus + 1, 1.0e6f, 2);
        return false;
    }
}

// ---------------------------------------------------------------- Зураглал

void Mission::Draw(Vector3 viewPosition) const
{
    const float time = (float)GetTime();

    for (int i = 0; i < ObjectiveCount(); ++i)
    {
        const ObjectiveDef& def = stage->objectives[(size_t)i];
        const ObjectiveState& state = states[(size_t)i];
        if (HorizontalDistance(viewPosition, state.location) > kDrawDistance)
        {
            continue;
        }
        const bool visible = state.status != ObjectiveStatus::Locked;

        for (int k = 0; k < (int)state.entities.size(); ++k)
        {
            const MissionEntity& entity = state.entities[(size_t)k];

            switch (def.type)
            {
            case ObjectiveType::InteractObject:
                // Төхөөрөмж үргэлж харагдана; гэрэл нь төлөвийг заана.
                if (def.prop == PropKind::Generator) DrawGenerator(entity.position, entity.used);
                else DrawRadio(entity.position, entity.used);
                break;

            case ObjectiveType::CollectItems:
                if (visible && !entity.used) DrawItem(entity.position, def.item);
                break;

            case ObjectiveType::DeliverItems:
                if (visible) DrawDropPoint(entity.position, entity.used);
                break;

            case ObjectiveType::RescueSurvivors:
            {
                if (!visible) break;
                // Аврагдсан цэрэг багт нэгдсэн тул энд зурагдахгүй; иргэн байрандаа зогсож үлдэнэ.
                if (entity.used && def.survivor == SurvivorKind::Soldier) break;

                HumanoidPose pose;
                if (entity.used)
                {
                    pose = MakeSoldierPose(0.0f, 0.0f, true, time + (float)k, 0.0f, HeldItem::None);
                }
                else
                {
                    // Айж бөхийсөн: өвдөг нугалж, гараараа толгойгоо хамгаална.
                    pose.hip[0] = pose.hip[1] = 62.0f;
                    pose.knee[0] = pose.knee[1] = 112.0f;
                    pose.bob = -0.36f;
                    pose.lean = 22.0f;
                    pose.shoulder[0] = pose.shoulder[1] = 150.0f;
                    pose.elbow[0] = pose.elbow[1] = 105.0f;
                    pose.shoulderYaw[0] = pose.shoulderYaw[1] = 20.0f;
                    pose.headPitch = 18.0f;
                    pose.breath = std::sin(time * 3.4f + (float)k);
                }
                DrawBlobShadow(entity.position, 0.5f);
                DrawHumanoid(entity.position, entity.facing, 1.0f, pose, SurvivorLook(def.survivor, k));
                break;
            }

            default:
                break;
            }
        }
    }
}

void Mission::DrawBeacons(const World& world, Vector3 viewPosition) const
{
    const float pulse = 0.5f + 0.5f * std::sin((float)GetTime() * 3.0f);

    for (int i = 0; i < ObjectiveCount(); ++i)
    {
        const ObjectiveDef& def = stage->objectives[(size_t)i];
        const ObjectiveState& state = states[(size_t)i];
        if (state.status != ObjectiveStatus::Active)
        {
            continue;
        }

        // Алсаас харагдах гэрлийн багана: даалгаврын байршлыг заана.
        const float distance = HorizontalDistance(viewPosition, state.location);
        const float width = std::clamp(distance * 0.012f, 0.35f, 4.0f); // хол байх тусам бүдүүн зурж харагдуулна
        DrawCubeV({ state.location.x, state.location.y + 45.0f, state.location.z }, { width, 90.0f, width },
                  Fade(kMarker, 0.22f + 0.12f * pulse));

        // Бүсийн хүрээ: газрын гадаргууг дагасан цагираг
        if (state.radius > 0.0f && distance < 220.0f)
        {
            const int segments = 48;
            Vector3 previous = {};
            for (int s = 0; s <= segments; ++s)
            {
                const float angle = 2.0f * PI * (float)s / (float)segments;
                const float x = state.location.x + std::sin(angle) * state.radius;
                const float z = state.location.z + std::cos(angle) * state.radius;
                const Vector3 point = { x, world.GroundHeight(x, z) + 0.35f, z };
                if (s > 0) DrawLine3D(previous, point, kMarker);
                previous = point;
            }
        }

        // Аврах хүн, цуглуулах зүйл, төхөөрөмж бүрийн дээр жижиг тэмдэг
        if (distance < kDrawDistance)
        {
            for (const MissionEntity& entity : state.entities)
            {
                if (entity.used) continue;
                const float height = def.type == ObjectiveType::CollectItems ? 1.3f : 2.7f;
                const float bob = 0.12f * std::sin((float)GetTime() * 4.0f + entity.position.x);
                DrawCubeV({ entity.position.x, entity.position.y + height + bob, entity.position.z }, { 0.22f, 0.22f, 0.22f },
                          kMarker);
            }
        }
    }
}

void Mission::DrawJournal() const
{
    const int screenWidth = GetScreenWidth();
    const int screenHeight = GetScreenHeight();
    const int width = 960, height = 640;
    const int left = (screenWidth - width) / 2, top = (screenHeight - height) / 2;

    const Color panel = { 24, 28, 34, 240 };
    const Color text = { 236, 232, 220, 255 };
    const Color dim = { 150, 150, 146, 255 };

    DrawRectangle(0, 0, screenWidth, screenHeight, Fade(BLACK, 0.55f));
    DrawRectangle(left, top, width, height, panel);
    DrawRectangleLinesEx({ (float)left, (float)top, (float)width, (float)height }, 2.0f, kMarker);

    int y = top + 20;
    UiText("MISSION JOURNAL", left + 28, y, 34, kMarker);
    char header[128];
    std::snprintf(header, sizeof(header), "%s \xE2\x80\x94 ҮЕ %02d / %02d", stage->provinceName, province + 1,
                  (int)Provinces().size());
    UiText(header, left + width - 28 - UiMeasure(header, 26), y + 6, 26, text);
    y += 48;

    y += (22 + 4) * DrawWrapped(std::string("Үндсэн зорилго: ") + stage->goal, left + 28, y, 22, width - 56, text);
    y += 10;
    DrawLine(left + 28, y, left + width - 28, y, Fade(kMarker, 0.5f));
    y += 12;

    const int focus = FocusObjective();
    for (int i = 0; i < ObjectiveCount(); ++i)
    {
        const ObjectiveDef& def = stage->objectives[(size_t)i];
        const ObjectiveState& state = states[(size_t)i];
        const bool done = state.status == ObjectiveStatus::Done;
        const bool locked = state.status == ObjectiveStatus::Locked;
        const int blockHeight = 104;

        // Одоо хийх ёстой даалгаврыг онцолно.
        if (i == focus)
        {
            DrawRectangle(left + 16, y - 4, width - 32, blockHeight - 6, Fade(kMarker, 0.14f));
            DrawRectangle(left + 16, y - 4, 5, blockHeight - 6, kMarker);
        }

        // Тэмдэглэгээний нүд: биелсэн бол ногоон зөв тэмдэг
        const Rectangle box = { (float)left + 34.0f, (float)y + 4.0f, 26.0f, 26.0f };
        DrawRectangleLinesEx(box, 2.0f, done ? kDoneGreen : locked ? dim : kMarker);
        if (done)
        {
            DrawLineEx({ box.x + 5.0f, box.y + 14.0f }, { box.x + 11.0f, box.y + 20.0f }, 3.0f, kDoneGreen);
            DrawLineEx({ box.x + 11.0f, box.y + 20.0f }, { box.x + 22.0f, box.y + 6.0f }, 3.0f, kDoneGreen);
        }

        char title[160];
        std::snprintf(title, sizeof(title), "%d. %s", i + 1, def.title);
        const Color titleColor = done ? kDoneGreen : locked ? dim : text;
        UiText(title, left + 74, y, 26, titleColor);

        // Төлөв: баруун талд
        const char* status = done ? "БИЕЛСЭН" : locked ? "ТҮГЖЭЭТЭЙ" : (i == focus ? "ОДОО ХИЙХ" : "ИДЭВХТЭЙ");
        UiText(status, left + width - 34 - UiMeasure(status, 18), y + 5, 18, titleColor);

        const int lines = DrawWrapped(def.description, left + 74, y + 32, 20, width - 74 - 40, locked ? dim : text);

        // Явц (тоо, хугацаатай даалгаварт) ба шагнал
        const std::string progress = ProgressText(i);
        char detail[160];
        if (locked && def.after >= 0)
        {
            std::snprintf(detail, sizeof(detail), "Эхлээд %d-р даалгаврыг биелүүл.", def.after + 1);
        }
        else if (!progress.empty())
        {
            std::snprintf(detail, sizeof(detail), "Явц: %s        Шагнал: %d зоос", progress.c_str(), def.reward);
        }
        else
        {
            std::snprintf(detail, sizeof(detail), "Шагнал: %d зоос", def.reward);
        }
        UiText(detail, left + 74, y + 34 + lines * 24, 18, done ? kDoneGreen : dim);

        y += blockHeight;
    }

    char footer[128];
    std::snprintf(footer, sizeof(footer), "Явц: %d / %d даалгавар биелсэн.", CompletedCount(), ObjectiveCount());
    UiText(footer, left + 28, top + height - 44, 24, CompletedCount() == ObjectiveCount() ? kDoneGreen : text);
    const char* close = "Q / ESC \xE2\x80\x94 хаах";
    UiText(close, left + width - 28 - UiMeasure(close, 20), top + height - 40, 20, dim);
}
