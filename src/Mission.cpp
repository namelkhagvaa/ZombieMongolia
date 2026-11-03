#include "Mission.h"

#include "City.h"

#include "Layout.h"

#include "Input.h"

#include "CharacterModel.h"

#include "Humanoid.h"
#include "Mover.h"
#include "Procedural.h"
#include "UiFont.h"
#include "UiStyle.h"
#include "VehicleModel.h"
#include "rlgl.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace
{
    const float kInteractDistance = 2.8f;
    const float kReachRadius = 9.0f;
    const float kDefendRadius = 30.0f;
    const int kZoneScale = 3;              // бүс, аваргын бараа бологчдын тоог үржүүлнэ

    // Даалгаврын хамгаалагч зомби (Mission::UpdateGuards)
    const float kGuardWake = 170.0f;       // баг ийм ойртоход даалгаврын хамгаалагчид гарч ирнэ
    const float kGuardAttack = 100.0f;     // ийм ойртоход дайрна
    const float kGuardInterval = 0.4f;     // шинэ хамгаалагч гарах хугацаа, сек
    const int kGuardCap = 24;              // нэг даалгаврын газарт зэрэг амьд хамгаалагч (+ аймгийн дугаарын тал)
    const float kRecycleDistance = 230.0f; // багаас ийм хол хоцорсон хамгаалагч нөөцөд буцна
    static_assert(kHordePools == StageSave::kMaxObjectives, "pool count must match objectives");

    const float kZoneRadius = 42.0f;
    const float kZoneWakeDistance = 130.0f; // тоглогч ийм ойртоход бүсийн зомби гарч эхэлнэ
    const float kNoticeTime = 4.5f;
    const float kBannerTime = 3.8f; // шинэ / биелсэн даалгаврын баннер
    const float kDrawDistance = 160.0f;

    const Color kMarker = { 250, 214, 70, 255 };
    const Color kDoneGreen = { 96, 200, 110, 255 };

    bool IsZoneType(ObjectiveType type)
    {
        return type == ObjectiveType::ClearZone || type == ObjectiveType::DefendLocation ||
               type == ObjectiveType::DefeatBoss;
    }

    // center-ийн эргэн тойронд, саад ба барилгын доторх биш, хилийн доторх чөлөөт цэг.
    Vector3 PlaceAround(const World& world, Vector3 center, float minRadius, float maxRadius, Rng& rng)
    {
        Vector3 best = center;
        for (int attempt = 0; attempt < 60; ++attempt)
        {
            // Олдохгүй бол радиусыг аажим томсгоно (том барилгын төвөөс гадагш гарна)
            const float grow = 1.0f + (float)(attempt / 15) * 0.8f;
            const float angle = rng.Range(0.0f, 2.0f * PI);
            const float distance = rng.Range(minRadius, maxRadius * grow);
            Vector3 candidate = { center.x + std::sin(angle) * distance, 0.0f, center.z + std::cos(angle) * distance };
            candidate.y = world.GroundHeight(candidate.x, candidate.z);

            Vector3 resolved = candidate;
            world.ResolveHorizontal(resolved, 0.6f, 1.8f, 0.3f);
            best = resolved;

            // Дээвэр, барилгын орой доор биш (хана нь мөргөлдөөнтэй, дотор нь хоосон барилгад хүн, хайрцаг
            // дотор нь гарч, тоглогч хүрч чадахгүй байв)
            const bool covered = world.SurfaceHeightAt(candidate.x, candidate.z, 1.0e5f) > candidate.y + 1.5f ||
                                 world.RoofHeight(candidate.x, candidate.z) > candidate.y + 1.5f;
            const bool free = HorizontalDistance(candidate, resolved) < 0.05f && !covered;
            if (free && world.InteriorAt(candidate.x, candidate.z) < 0 && world.InsideBoundary(candidate.x, candidate.z))
            {
                return candidate;
            }
        }
        best.y = world.GroundHeight(best.x, best.z);
        return best;
    }

    // Санамсаргүй (тоглоомын явцын) цэг: spawn-д. Барилга, саад дотор гарахгүй (хотод барилга шигүү).
    Vector3 RandomAround(const World& world, Vector3 center, float minRadius, float maxRadius)
    {
        Vector3 position = center;
        for (int attempt = 0; attempt < 12; ++attempt)
        {
            const float angle = (float)GetRandomValue(0, 359) * DEG2RAD;
            const float distance = minRadius + (maxRadius - minRadius) * (float)GetRandomValue(0, 1000) / 1000.0f;
            position = { center.x + std::sin(angle) * distance, 0.0f, center.z + std::cos(angle) * distance };
            if (!world.InsideBoundary(position.x, position.z) || world.InteriorAt(position.x, position.z) >= 0)
            {
                continue;
            }
            Vector3 resolved = position;
            world.ResolveHorizontal(resolved, 0.6f, 1.8f, 0.3f);
            if (HorizontalDistance(resolved, position) < 0.05f || attempt == 11)
            {
                break;
            }
        }
        world.ResolveHorizontal(position, 0.6f, 1.8f, 0.3f);
        position.y = world.GroundHeight(position.x, position.z);
        return position;
    }

    // ------------------------------------------------------------------ Объектуудын зураглал

    // Даалгаврын зүйлийн загвар (assets/mission): p дээр height өндөртэй, yaw эргэлттэй. Загваргүй бол false.
    bool DrawPropModel3(SceneryModelId id, Vector3 p, float height, float yaw = 0.0f)
    {
        if (!HasSceneryModel(id)) return false;
        rlPushMatrix();
        rlTranslatef(p.x, p.y, p.z);
        rlRotatef(yaw, 0.0f, 1.0f, 0.0f);
        rlScalef(height, height, height);
        DrawSceneryModel(id);
        rlPopMatrix();
        return true;
    }

    // Асаалттай эсэхийн гэрэл (загвар дээр): улаан / ногоон
    void StatusLight(Vector3 at, bool running)
    {
        DrawCubeV(at, { 0.07f, 0.07f, 0.07f }, running ? Color{ 80, 240, 90, 255 } : Color{ 230, 60, 50, 255 });
    }

    void DrawGenerator(Vector3 p, bool running)
    {
        if (DrawPropModel3(SceneryModelId::MissionGenerator, p, 0.8f))
        {
            StatusLight({ p.x, p.y + 0.86f, p.z }, running);
            return;
        }
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
        if (DrawPropModel3(SceneryModelId::MissionRadio, { p.x - 0.1f, p.y + 0.81f, p.z }, 0.6f))
        {
            StatusLight({ p.x + 0.35f, p.y + 0.86f, p.z + 0.2f }, running);
            return;
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
            if (DrawPropModel3(SceneryModelId::MissionMedicine, p, 0.3f)) break;
            DrawCubeV({ p.x, p.y + 0.18f, p.z }, { 0.5f, 0.34f, 0.36f }, { 232, 230, 224, 255 });
            DrawCubeV({ p.x, p.y + 0.36f, p.z }, { 0.3f, 0.02f, 0.09f }, { 200, 40, 40, 255 });
            DrawCubeV({ p.x, p.y + 0.36f, p.z }, { 0.09f, 0.02f, 0.3f }, { 200, 40, 40, 255 });
            break;
        case ItemKind::Ammo: // ногоон сумны хайрцаг
            if (DrawPropModel3(SceneryModelId::MissionAmmo, p, 0.26f)) break;
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
        if (DrawPropModel3(SceneryModelId::MissionDropPoint, p, 2.1f))
        {
            if (delivered) DrawCubeV({ p.x - 0.3f, p.y + 0.5f, p.z }, { 0.7f, 0.54f, 0.6f }, { 150, 120, 84, 255 }); // хүргэсэн ачаа
            return;
        }
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

    // Аврах иргэний загвар даалгавраас: эмч нар, сурагчид (хүү, охин ээлжлэн), лам нар; бусад нь малчин
    // (эрэгтэй, эмэгтэй ээлжлэн; эмнэлгийн өвчтөн, малчид ч орно)
    int SurvivorModel(const ObjectiveDef& def, int variant)
    {
        const std::string id = def.id;
        const int herder = variant % 2 == 0 ? kCharacterSurvivorMan : kCharacterSurvivorWoman;
        // Загвар байхгүй бол (жишээ нь ясгүй эсвэл яс нь буруу тул бэлтгээгүй) ойролцоо загвар
        auto pick = [&](int wanted, int fallback) { return HasCharacterModel(wanted) ? wanted : fallback; };
        if (id.rfind("rescue_doctors", 0) == 0)
        {
            return variant % 2 == 0 ? pick(kCharacterDoctorMan, herder) : pick(kCharacterDoctorWoman, pick(kCharacterDoctorMan, herder));
        }
        if (id.rfind("rescue_students", 0) == 0)
        {
            return variant % 2 == 0 ? pick(kCharacterStudentBoy, herder) : pick(kCharacterStudentGirl, herder);
        }
        if (id.rfind("rescue_monks", 0) == 0) return pick(kCharacterMonk, herder);
        return herder;
    }

    HumanoidLook SurvivorLook(const ObjectiveDef& def, int variant)
    {
        const SurvivorKind kind = def.survivor;
        HumanoidLook look;
        if (kind == SurvivorKind::Soldier)
        {
            look.kind = HumanoidKind::Soldier;
            // Багийн цэргүүдийн загвар (кодоор угсарсан хайрцган дүрс бусдаас ялгардаг, гар даллах нь ажилладаггүй байв)
            look.model = variant % 2 ? kCharacterFighter1 : kCharacterFighter2;
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
            look.model = SurvivorModel(def, variant);
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

namespace
{
    // Хотын жинхэнэ газрын цэгүүд (POI эсвэл тухайн төрлийн барилгын төв), хотын төвөөс ойроос нь эрэмбэлсэн.
    std::vector<Vector2> CityPlaces(const CityData& city, SiteKind kind)
    {
        std::vector<Vector2> found;
        auto pois = [&](CityPoiKind poiKind) {
            for (const CityPoi& poi : city.pois)
            {
                if (poi.kind == poiKind) found.push_back(poi.position);
            }
        };
        auto buildings = [&](CityBuildingType type, float minArea) {
            for (const CityBuilding& b : city.buildings)
            {
                if (b.type != type || b.outline.size() < 3) continue;
                float area = 0.0f;
                Vector2 center = { 0.0f, 0.0f };
                for (size_t i = 0, j = b.outline.size() - 1; i < b.outline.size(); j = i++)
                {
                    area += b.outline[j].x * b.outline[i].y - b.outline[i].x * b.outline[j].y;
                    center = Vector2Add(center, b.outline[i]);
                }
                if (std::fabs(area) * 0.5f >= minArea) found.push_back(Vector2Scale(center, 1.0f / (float)b.outline.size()));
            }
        };
        switch (kind)
        {
        case SiteKind::Hospital: pois(CityPoiKind::Hospital); break;
        case SiteKind::Police: pois(CityPoiKind::Police); break;
        case SiteKind::Government: pois(CityPoiKind::Government); break;
        case SiteKind::Museum: pois(CityPoiKind::Museum); break;
        case SiteKind::Stadium:
            pois(CityPoiKind::Stadium);
            if (found.empty())
            {
                for (const CityArea& area : city.areas)
                {
                    if (area.kind != CityAreaKind::Pitch || area.outline.empty()) continue;
                    Vector2 c = { 0.0f, 0.0f };
                    for (const Vector2& p : area.outline) c = Vector2Add(c, p);
                    found.push_back(Vector2Scale(c, 1.0f / (float)area.outline.size()));
                }
            }
            break;
        case SiteKind::Market: pois(CityPoiKind::Square); break;
        case SiteKind::School: buildings(CityBuildingType::School, 300.0f); break;
        case SiteKind::Temple: buildings(CityBuildingType::Religious, 60.0f); break;
        default: break;
        }
        const Vector2 town = CityTownSite(city);
        std::sort(found.begin(), found.end(), [&](Vector2 a, Vector2 b) { return Vector2Distance(a, town) < Vector2Distance(b, town); });
        // Бие биеэсээ 40 м-ээс ойр давхардсан цэгүүдийг нэг болгоно (нэг эмнэлгийн хэд хэдэн тэмдэглэгээ)
        std::vector<Vector2> unique;
        for (const Vector2& p : found)
        {
            bool near = false;
            for (const Vector2& q : unique) near = near || Vector2Distance(p, q) < 40.0f;
            if (!near) unique.push_back(p);
        }
        return unique;
    }

    // Байхгүй үед орлох газар (дарааллаар)
    const SiteKind* PlaceFallbacks(SiteKind kind)
    {
        static const SiteKind hospital[] = { SiteKind::School, SiteKind::Government, SiteKind::Settlement };
        static const SiteKind police[] = { SiteKind::Government, SiteKind::Market, SiteKind::Settlement };
        static const SiteKind government[] = { SiteKind::Market, SiteKind::Museum, SiteKind::Settlement };
        static const SiteKind museum[] = { SiteKind::Temple, SiteKind::Government, SiteKind::Settlement };
        static const SiteKind stadium[] = { SiteKind::School, SiteKind::Market, SiteKind::Settlement };
        static const SiteKind market[] = { SiteKind::Government, SiteKind::Museum, SiteKind::Settlement };
        static const SiteKind school[] = { SiteKind::Stadium, SiteKind::Hospital, SiteKind::Settlement };
        static const SiteKind temple[] = { SiteKind::Museum, SiteKind::Market, SiteKind::Settlement };
        static const SiteKind none[] = { SiteKind::Settlement };
        switch (kind)
        {
        case SiteKind::Hospital: return hospital;
        case SiteKind::Police: return police;
        case SiteKind::Government: return government;
        case SiteKind::Museum: return museum;
        case SiteKind::Stadium: return stadium;
        case SiteKind::Market: return market;
        case SiteKind::School: return school;
        case SiteKind::Temple: return temple;
        default: return none;
        }
    }

    bool IsCityPlace(SiteKind kind) { return kind >= SiteKind::Hospital; }

    // place-ийн ойрын, барилга, саадын гадна талын задгай газар (даалгаврын цэг)
    Vector2 FreeSpotNear(const World& world, Vector2 place)
    {
        for (float radius = 0.0f; radius <= 70.0f; radius += 5.0f)
        {
            const int steps = radius == 0.0f ? 1 : 12;
            for (int k = 0; k < steps; ++k)
            {
                const float a = (float)k * 2.0f * PI / (float)steps;
                Vector3 c = { place.x + std::cos(a) * radius, 0.0f, place.y + std::sin(a) * radius };
                c.y = world.GroundHeight(c.x, c.z);
                const Vector3 before = c;
                world.ResolveHorizontal(c, 1.2f, 1.8f, 0.3f);
                if (HorizontalDistance(c, before) < 0.05f && world.InteriorAt(c.x, c.z) < 0 && world.InsideBoundary(c.x, c.z))
                {
                    return { c.x, c.z };
                }
            }
        }
        return place;
    }
}

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
    if (world.CityActive())
    {
        // Аймгийн жинхэнэ газрууд (City.h): застав тоглоомын (0, 0)-ийн дэргэд, хот айл, суурин нь layout-д
        const CityData& city = CurrentCity();
        if (IsCityPlace(site.kind))
        {
            // Жинхэнэ газар, байхгүй бол орлох газар (эцэст нь хотын төв)
            SiteKind kind = site.kind;
            for (int attempt = 0; attempt < 4 && kind != SiteKind::Settlement; ++attempt)
            {
                const std::vector<Vector2> places = CityPlaces(city, kind);
                if (!places.empty())
                {
                    const Vector2 p = FreeSpotNear(world, places[(size_t)std::min(site.ordinal, (int)places.size() - 1)]);
                    return ground(p.x, p.y);
                }
                kind = PlaceFallbacks(site.kind)[attempt < 3 ? attempt : 2];
            }
            const Vector2 p = CityTownSite(city);
            return ground(p.x, p.y);
        }
        switch (site.kind)
        {
        case SiteKind::Checkpoint: return ground(-48.0f, 17.0f);
        case SiteKind::GasStation: { const Vector2 p = CityGasSite(city); return ground(p.x, p.y); }
        case SiteKind::Warehouse: { const Vector2 p = CityWarehouseSite(city); return ground(p.x, p.y); }
        case SiteKind::Camp:
        case SiteKind::Hamlet: break; // доорх ердийн дүрмээр
        default: { const Vector2 p = CityTownSite(city); return ground(p.x, p.y); }
        }
        if (site.kind == SiteKind::Camp && nth(world.GetLayout().camps, site.ordinal, point)) return ground(point.x - 3.0f, point.y - 9.0f);
        if (site.kind == SiteKind::Hamlet && nth(world.GetLayout().hamlets, site.ordinal, point)) return ground(point.x - 34.0f, point.y + 8.0f);
        const Vector2 p = CityTownSite(city);
        return ground(p.x, p.y);
    }
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
    default: // хотын газар (зохиомол газрын зурагт): төв суурин
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
    guardTimer = 1.0f;
    autoTimer = 0.0f;
    rewardPending = 0;
    dirty = false;
    banners.clear();
    bannerSound = 0;
    briefingTimer = briefingEnabled ? 12.0f : 0.0f;
    syncedOnce = false;
    hintObjective = -1;
    hintShown = 0;

    guardedObjective = -1;
    for (auto& alive : alivePool) alive[0] = alive[1] = 0;
    // Хамгаалагчгүй хуучин хадгалалт: аль хэдийн устгасан зомбийг даалгавруудад тэнцүү хуваана
    const int pools = std::max(1, std::min((int)stage->objectives.size(), StageSave::kMaxObjectives));
    for (int big = 0; big < 2; ++big)
    {
        int counted = 0;
        for (const auto& kills : save->guardKills) counted += kills[big];
        int missing = (big ? save->killedBig : save->killedSmall) - counted;
        for (int p = 0; missing > 0; p = (p + 1) % pools, --missing) ++save->guardKills[p][big];
    }

    const int count = std::min((int)stage->objectives.size(), StageSave::kMaxObjectives);
    states.assign((size_t)count, ObjectiveState{});

    for (int i = 0; i < count; ++i)
    {
        const ObjectiveDef& def = stage->objectives[(size_t)i];
        ObjectiveState& state = states[(size_t)i];
        Rng rng((uint32_t)(provinceIndex * 131 + i * 17 + 7)); // байрлал нь тоглох бүрд ижил
        const bool done = (save->done >> i) & 1u;

        state.location = ResolveSite(world, def.site);
        TraceLog(LOG_INFO, "MISSION: objective %d %s at (%.0f, %.0f)", i + 1, def.id, state.location.x, state.location.z);
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
                entity.used = done || (i < StageSave::kEntitySlots && ((save->entities[i] >> k) & 1u));
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
            state.target = std::max(4, (8 + provinceIndex) * def.count / 100) * kZoneScale;
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

void Mission::SyncFrom(const StageSave& host)
{
    if (!save || !stage) return;
    *save = host;
    const int count = ObjectiveCount();
    for (int i = 0; i < count; ++i)
    {
        ObjectiveState& state = states[(size_t)i];
        const bool done = (host.done >> i) & 1u;
        if (i < StageSave::kEntitySlots)
        {
            for (size_t k = 0; k < state.entities.size() && k < 32; ++k)
            {
                if ((host.entities[i] >> k) & 1u) state.entities[k].used = true;
            }
        }
        if (done && state.status != ObjectiveStatus::Done)
        {
            if (syncedOnce) PushBanner(i, true);
            state.status = ObjectiveStatus::Done;
            for (MissionEntity& entity : state.entities) entity.used = true;
            state.progress = std::max(state.progress, state.target);
            state.reserve = 0;
        }
        else if (!done && !state.entities.empty() && state.target > 0)
        {
            int used = 0;
            for (const MissionEntity& entity : state.entities) used += entity.used ? 1 : 0;
            state.progress = used;
        }
    }
    for (int i = 0; i < count; ++i)
    {
        const int after = stage->objectives[(size_t)i].after;
        if (states[(size_t)i].status == ObjectiveStatus::Locked &&
            (after < 0 || (after < count && states[(size_t)after].status == ObjectiveStatus::Done)))
        {
            Activate(i);
            if (syncedOnce) PushBanner(i, false);
        }
    }
    syncedOnce = true;
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
    PushBanner(index, true);
    dirty = true;
    TraceLog(LOG_INFO, "MISSION: stage %d objective %d (%s) completed", province + 1, index + 1, def.id);

    // Энэ даалгавраас хамаарсан дараагийн даалгавруудыг нээнэ.
    for (int j = 0; j < ObjectiveCount(); ++j)
    {
        if (states[(size_t)j].status == ObjectiveStatus::Locked && stage->objectives[(size_t)j].after == index)
        {
            Activate(j);
            PushBanner(j, false);
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

int Mission::ZoneRemaining(int index) const
{
    const ObjectiveDef& def = stage->objectives[(size_t)index];
    if (def.type != ObjectiveType::ClearZone) return -1;
    const ObjectiveState& state = states[(size_t)index];
    const int near = index < (int)zoneNear.size() ? zoneNear[(size_t)index] : 0;
    return state.status == ObjectiveStatus::Done ? 0 : state.reserve + near;
}

std::string Mission::HowTo(int index) const
{
    const ObjectiveDef& def = stage->objectives[(size_t)index];
    const ObjectiveState& state = states[(size_t)index];
    const bool touch = InTouchMode();
    const char* press = touch ? "үйлдлийн товчийг дар" : "E дар";
    switch (def.type)
    {
    case ObjectiveType::ReachLocation: return "Тэмдэгтэй газарт оч (газрын зураг дээр дарвал чиглэл заана)";
    case ObjectiveType::InteractObject: return std::string("Төхөөрөмжийн дэргэд очиж ") + press;
    case ObjectiveType::RescueSurvivors:
        return TextFormat("Гараа даллаж буй хүмүүсийн дэргэд очиж %s  (%d / %d)", press, state.progress, state.target);
    case ObjectiveType::CollectItems:
        return TextFormat("Хайрцгуудын дэргэд очиж %s  (%d / %d)", press, state.progress, state.target);
    case ObjectiveType::DeliverItems: return std::string("Хүргэх цэгт очиж ") + press;
    case ObjectiveType::ClearZone:
        return state.reserve > 0 ? TextFormat("Бүсийн зомбийг устга: үлдсэн %d (шинээр гарсаар байна)", ZoneRemaining(index))
                                 : TextFormat("Сүүлчийн %d зомбийг устга (дэлгэц дээр улаан тэмдэгтэй)", ZoneRemaining(index));
    case ObjectiveType::DefendLocation:
        return !partners.empty()
                   ? TextFormat("Бүс дотроо тэс: %d сек үлдлээ (олуулаа бүсэд байвал 1.5x хурдан)", std::max(0, state.target - state.progress))
                   : TextFormat("Бүс дотроо байж тэс: %d секунд үлдлээ", std::max(0, state.target - state.progress));
    case ObjectiveType::DefeatBoss: return "Аварга зомбийг ал (бусад зомби хамаагүй)";
    }
    return def.description;
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
        std::snprintf(buffer, sizeof(buffer), "Үлдсэн %d зомби", std::max(0, ZoneRemaining(index)));
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
    const bool complete = IsComplete();
    for (int i = 0; i < ObjectiveCount(); ++i)
    {
        const ObjectiveState& state = states[(size_t)i];
        // Бүх даалгавар биелсний дараа: хамгаалагч зомби үлдсэн газрууд (үеийг дуусгахын тулд устгана)
        if (state.status == ObjectiveStatus::Active || (complete && PoolLeft(i, false) + PoolLeft(i, true) > 0))
        {
            out.push_back(state.location);
        }
    }
}

void Mission::MapMarkers(std::vector<MapMarker>& out) const
{
    const bool complete = IsComplete();
    const int focus = FocusObjective();
    for (int i = 0; i < ObjectiveCount(); ++i)
    {
        const ObjectiveDef& def = stage->objectives[(size_t)i];
        const ObjectiveState& state = states[(size_t)i];
        if (state.status == ObjectiveStatus::Active)
        {
            out.push_back({ state.location, KindOf(def.type), i, def.title, i == focus });
        }
        else if (complete && PoolLeft(i, false) + PoolLeft(i, true) > 0)
        {
            out.push_back({ state.location, MarkerKind::Clear, i, "Үлдсэн зомбийг устга", false });
        }
    }
}

float Mission::ProgressFraction(int index) const
{
    const ObjectiveState& state = states[(size_t)index];
    switch (stage->objectives[(size_t)index].type)
    {
    case ObjectiveType::RescueSurvivors:
    case ObjectiveType::CollectItems:
    case ObjectiveType::ClearZone:
    case ObjectiveType::DefendLocation:
        return state.target > 0 ? std::clamp((float)state.progress / (float)state.target, 0.0f, 1.0f) : -1.0f;
    case ObjectiveType::DefeatBoss:
        return state.status == ObjectiveStatus::Done ? 1.0f : -1.0f;
    default:
        return -1.0f;
    }
}

int Mission::TakeBannerSound()
{
    return std::exchange(bannerSound, 0);
}

void Mission::PushBanner(int objective, bool completed)
{
    if (banners.size() > 6) banners.erase(banners.begin()); // (олон зэрэг: хамгийн хуучныг алгасна)
    banners.push_back({ objective, completed, 0.0f });
    if (banners.size() == 1 && !BriefingOpen()) bannerSound = completed ? 2 : 1;
}

void Mission::UpdatePresentation(float dt, Vector3 player)
{
    if (!stage) return;
    if (briefingTimer > 0.0f)
    {
        briefingTimer = std::max(0.0f, briefingTimer - dt);
        if (briefingTimer <= 0.0f && !banners.empty()) bannerSound = banners.front().completed ? 2 : 1;
    }
    else if (!banners.empty())
    {
        // Хоцорсон: аль хэдийн биелсэн даалгаврын "шинэ даалгавар" баннер хэрэггүй
        while (!banners.empty() && !banners.front().completed && banners.front().age == 0.0f && banners.front().objective >= 0 &&
               banners.front().objective < ObjectiveCount() && states[(size_t)banners.front().objective].status == ObjectiveStatus::Done)
        {
            banners.erase(banners.begin());
        }
        if (banners.empty()) return;
        banners.front().age += dt * (banners.size() > 2 ? 2.0f : 1.0f); // (олон хуримтлагдвал хурдан)
        if (banners.front().age >= kBannerTime)
        {
            banners.erase(banners.begin());
            if (!banners.empty()) bannerSound = banners.front().completed ? 2 : 1;
        }
    }
    UpdateHint(dt, player);
}

void Mission::UpdateHint(float dt, Vector3 player)
{
    const int focus = FocusObjective();
    if (focus != hintObjective)
    {
        hintObjective = focus;
        hintTimer = 0.0f;
        hintBest = 1e9f;
        hintProgress = -1;
    }
    if (focus < 0 || focus >= 32 || ((hintShown >> focus) & 1u) || BriefingOpen()) return;
    const float distance = DistanceTo(focus, player);
    const int progress = states[(size_t)focus].progress;
    // Ойртож (20 м-ээс их), эсвэл явц гарч байвал гацаагүй
    if (distance < hintBest - 20.0f || progress != hintProgress)
    {
        hintBest = std::min(hintBest, distance);
        hintProgress = progress;
        hintTimer = 0.0f;
        return;
    }
    hintTimer += dt;
    if (hintTimer < 75.0f) return;
    hintShown |= 1u << focus;
    if (distance > 60.0f)
    {
        notice = InTouchMode() ? "Зөвлөгөө: газрын зураг дээрх даалгаврын тэмдэг дээр дарвал чиглэлийг сумаар заана"
                               : "Зөвлөгөө: газрын зургийг (M) нээж даалгаврын тэмдэг дээр дарвал чиглэлийг сумаар заана";
    }
    else
    {
        notice = TextFormat("Зөвлөгөө: %s — дэлгэц дээрх тэмдэгтэй зүйлсийг хай", stage->objectives[(size_t)focus].title);
    }
    noticeTimer = 7.0f;
}

namespace
{
    // Нэг мөрөнд багтахгүй бол "…"-аар таслана (UTF-8 тэмдэгтийн дундуур биш)
    std::string FitLine(std::string line, int size, int maxWidth, bool bold)
    {
        auto measure = [&](const std::string& s) { return bold ? Ui::MeasureBold(s.c_str(), size) : Ui::Measure(s.c_str(), size); };
        if (measure(line) <= maxWidth) return line;
        while (!line.empty() && measure(line + "\xE2\x80\xA6") > maxWidth)
        {
            line.pop_back();
            while (!line.empty() && ((unsigned char)line.back() & 0xC0) == 0x80) line.pop_back();
        }
        return line + "\xE2\x80\xA6";
    }

    // Ногоон зөв тэмдэг (дугуйн дотор)
    void DrawCheckBadge(Vector2 c, float r, float alpha)
    {
        const Color green = { 110, 220, 120, 255 };
        DrawCircleV({ c.x + 1.0f, c.y + 2.0f }, r + 2.0f, Fade(BLACK, 0.35f * alpha));
        DrawCircleV(c, r, Fade(green, alpha));
        const float k = r / 20.0f;
        DrawLineEx({ c.x - 9.0f * k, c.y + 1.0f * k }, { c.x - 2.5f * k, c.y + 8.0f * k }, 4.5f * k, Fade({ 18, 22, 30, 255 }, alpha));
        DrawLineEx({ c.x - 2.5f * k, c.y + 8.0f * k }, { c.x + 10.0f * k, c.y - 7.0f * k }, 4.5f * k, Fade({ 18, 22, 30, 255 }, alpha));
    }
}

void Mission::DrawBanner() const
{
    const Banner* banner = CurrentBanner();
    if (!banner || banner->objective < 0 || banner->objective >= ObjectiveCount()) return;
    const ObjectiveDef& def = stage->objectives[(size_t)banner->objective];
    const MarkerKind kind = KindOf(def.type);
    const Color accent = banner->completed ? Color{ 110, 220, 120, 255 } : MarkerColor(kind);

    // Гарч ирэх (0.25 с), алга болох (0.45 с): тунгалаг ба бага зэрэг дээрээс доош гулсана
    const float in = std::min(1.0f, banner->age / 0.25f);
    const float out = std::min(1.0f, (kBannerTime - banner->age) / 0.45f);
    const float alpha = std::clamp(std::min(in, out), 0.0f, 1.0f);
    const float slide = (1.0f - in) * -18.0f;

    const int screenWidth = ViewWidth(), screenHeight = ViewHeight();
    const float width = std::min(640.0f, (float)screenWidth - 40.0f), height = 96.0f;
    const Rectangle card = { ((float)screenWidth - width) * 0.5f, (float)screenHeight * 0.25f + slide, width, height };
    Ui::Panel(card, Fade({ 20, 30, 43, 255 }, 0.9f * alpha), 14.0f);
    Ui::Outline(card, Fade(accent, 0.75f * alpha), 2.0f, 14.0f);
    Ui::Fill({ card.x, card.y + 10.0f, 5.0f, card.height - 20.0f }, Fade(accent, alpha));

    const Vector2 badge = { card.x + 52.0f, card.y + card.height * 0.5f };
    if (banner->completed) DrawCheckBadge(badge, 28.0f, alpha);
    else DrawMarkerBadge(kind, badge, 28.0f, alpha, banner->objective + 1);

    const int textX = (int)card.x + 98, textWidth = (int)card.width - 98 - 20;
    char label[96];
    if (banner->completed) std::snprintf(label, sizeof(label), "ДААЛГАВАР БИЕЛЛЭЭ  ·  %d / %d", CompletedCount(), ObjectiveCount());
    else std::snprintf(label, sizeof(label), "ШИНЭ ДААЛГАВАР  ·  %s", MarkerName(kind));
    Ui::TextBold(label, textX, (int)card.y + 12, 15, Fade(accent, alpha));
    Ui::TextBold(FitLine(def.title, 26, textWidth, true).c_str(), textX, (int)card.y + 32, 26, Fade(Ui::kText, alpha));
    // Биелсэн бол: дараагийн нээгдсэн даалгаврын нэр (дараалалд байгаа), эсвэл бүгд биелсэн
    std::string second = def.description;
    if (banner->completed)
    {
        second = IsComplete() ? "Бүх даалгавар биеллээ! Үлдсэн зомбийг устгаад аймгийг чөлөөл." : "Сайн байна! Үргэлжлүүл.";
        for (size_t k = 1; k < banners.size(); ++k)
        {
            if (banners[k].completed || banners[k].objective < 0 || banners[k].objective >= ObjectiveCount()) continue;
            second = std::string("Дараагийнх: ") + stage->objectives[(size_t)banners[k].objective].title;
            break;
        }
    }
    Ui::Text(FitLine(second, 17, textWidth, false).c_str(), textX, (int)card.y + 66, 17, Fade(Ui::kTextDim, alpha));
}

void Mission::DrawBriefing() const
{
    if (!BriefingOpen() || !stage) return;
    const float alpha = std::clamp(briefingTimer / 0.35f, 0.0f, 1.0f);
    const int screenWidth = ViewWidth(), screenHeight = ViewHeight();
    DrawRectangle(0, 0, screenWidth, screenHeight, Fade({ 6, 8, 12, 255 }, 0.55f * alpha));

    const int count = ObjectiveCount();
    const float row = 34.0f;
    const float width = std::min(720.0f, (float)screenWidth - 32.0f);
    const int goalLines = 2;
    const float height = std::min((float)screenHeight - 24.0f, 150.0f + 24.0f * (float)goalLines + row * (float)count + 64.0f);
    const Rectangle card = { ((float)screenWidth - width) * 0.5f, ((float)screenHeight - height) * 0.5f, width, height };
    Ui::Panel(card, Fade({ 20, 30, 43, 255 }, 0.95f * alpha), 18.0f);
    Ui::Outline(card, Fade(kMarker, 0.6f * alpha), 2.0f, 18.0f);

    const int left = (int)card.x + 28;
    int y = (int)card.y + 20;
    Ui::TextBold(TextFormat("АЙМАГ %d / %d", province + 1, (int)Provinces().size()), left, y, 15, Fade(kMarker, alpha));
    y += 22;
    Ui::TextDisplay(stage->provinceName, left, y, 42, Fade(Ui::kText, alpha), 0.06f);
    y += 50;
    y += 24 * DrawWrapped(stage->goal, left, y, 20, (int)card.width - 56, Fade(Ui::kTextDim, alpha));
    y += 10;
    DrawLine(left, y, (int)(card.x + card.width) - 28, y, Fade(kMarker, 0.4f * alpha));
    y += 12;
    Ui::TextDisplay("ДААЛГАВРУУД", left, y, 15, Fade(Ui::kTextDim, alpha), 0.14f);
    y += 24;

    for (int i = 0; i < count && (float)y + row < card.y + card.height - 48.0f; ++i)
    {
        const ObjectiveDef& def = stage->objectives[(size_t)i];
        const ObjectiveState& state = states[(size_t)i];
        const bool done = state.status == ObjectiveStatus::Done;
        const bool locked = state.status == ObjectiveStatus::Locked;
        const float rowAlpha = alpha * (locked ? 0.55f : 1.0f);
        if (done) DrawCheckBadge({ (float)left + 13.0f, (float)y + 12.0f }, 13.0f, rowAlpha);
        else DrawMarkerBadge(KindOf(def.type), { (float)left + 13.0f, (float)y + 12.0f }, 13.0f, rowAlpha);
        const Color color = done ? Color{ 110, 220, 120, 255 } : locked ? Ui::kTextDim : Ui::kText;
        Ui::TextBold(TextFormat("%d. %s", i + 1, def.title), left + 36, y + 2, 19, Fade(color, rowAlpha));
        const char* state_text = done ? "биелсэн" : locked ? "дараа нь" : "одоо";
        const int w = Ui::Measure(state_text, 15);
        Ui::Text(state_text, (int)(card.x + card.width) - 28 - w, y + 5, 15, Fade(color, rowAlpha));
        y += (int)row;
    }

    // Доод мөр: хаах заавар ба автоматаар хаагдах хүртэлх зурвас
    const char* hint = InTouchMode() ? "Дэлгэц дээр товшиж эхлэх" : "Дурын товч дарж эхлэх   ·   Q: даалгаврын журнал   ·   M: газрын зураг";
    const int hintWidth = Ui::Measure(hint, 16);
    const int bottom = (int)(card.y + card.height) - 34;
    Ui::Text(hint, (int)(card.x + (card.width - (float)hintWidth) * 0.5f), bottom, 16, Fade(Ui::kTextDim, alpha));
    const float left_frac = std::clamp(briefingTimer / 12.0f, 0.0f, 1.0f);
    Ui::Fill({ card.x + 28.0f, card.y + card.height - 10.0f, (card.width - 56.0f) * left_frac, 3.0f }, Fade(kMarker, 0.7f * alpha));
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
    // Хамтарсан тоглолт: аль нэг тоглогч бүсэд байхад л хангалттай
    const float hostDistance = HorizontalDistance(player.Position(), state.location);
    int playersInside = hostDistance < state.radius ? 1 : 0;
    float playerDistance = hostDistance;
    for (const Vector3& partner : partners)
    {
        const float distance = HorizontalDistance(partner, state.location);
        playerDistance = std::min(playerDistance, distance);
        if (distance < state.radius) ++playersInside;
    }
    state.spawnTimer -= dt;

    if (def.type == ObjectiveType::ClearZone)
    {
        // Бүсийн нөөц: тоглогч ойртоход дотор нь бага багаар гарч ирнэ; нөөц дуусвал дахин гарахгүй.
        const int cap = std::min(30 + province / 2, state.target);
        int alive = horde.AliveInZone(zone);
        if (playerDistance < state.radius + kZoneWakeDistance)
        {
            while (state.reserve > 0 && alive < cap && state.spawnTimer <= 0.0f)
            {
                if (!SpawnFromPool(horde, RandomAround(world, state.location, 4.0f, state.radius), zone, false, index))
                {
                    state.reserve = 0; // үеийн нөөц дууссан: энэ бүсэд дахин гарахгүй
                    break;
                }
                --state.reserve;
                ++alive;
                state.spawnTimer = alive < cap * 2 / 3 ? 0.0f : 1.0f; // эхний хэсэг шууд, дараа нь нэг нэгээр
            }
        }

        // Дуусах нөхцөл: нөөц дууссан, бүсийн ойролцоох (радиус + 40 м) зомби бүгд устсан. Хол тэнүүчилж явсан,
        // гацсан нэг зомби даалгаврыг дуусгахгүй байлгаж байв (тэд үеийн тоонд хэвээр тоологдоно).
        const int near = horde.AliveInZoneNear(zone, state.location, state.radius + 40.0f);
        if ((int)zoneNear.size() <= index) zoneNear.resize((size_t)index + 1, 0);
        zoneNear[(size_t)index] = near;
        state.progress = std::max(0, state.target - state.reserve - near);
        if (state.reserve == 0 && near == 0)
        {
            Complete(index);
        }
    }
    else if (def.type == ObjectiveType::DefendLocation)
    {
        // Хугацаа зөвхөн тоглогч бүс дотор байхад урагшилна; энэ үед зомби тасралтгүй довтолно.
        if (playerDistance < state.radius)
        {
            // Хоёр ба түүнээс олон тоглогч бүсэд: хугацаа 1.5 дахин хурдан, довтолгоо ч хүчтэй
            const bool both = playersInside >= 2;
            state.timer += dt * (both ? 1.5f : 1.0f);
            const int cap = (22 + province / 2) * (both ? 4 : 3) / 3;
            if (state.spawnTimer <= 0.0f && horde.AliveInZone(zone) < cap)
            {
                SpawnFromPool(horde, RandomAround(world, state.location, 42.0f, 58.0f), zone, true, index);
                state.spawnTimer = 0.55f;
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
            TraceLog(LOG_INFO, "MISSION: boss spawned for objective %d", index + 1);
            for (int k = 0; k < 3 * kZoneScale; ++k)
            {
                SpawnFromPool(horde, RandomAround(world, state.location, 4.0f, 16.0f), zone, false, index);
            }
            state.spawned = true;
        }
        // Аварга өөрөө үхэхэд биелнэ (дагалдах зомби хөөж хол явсан, гацсан ч хамаагүй)
        if (state.spawned && !horde.BossAliveInZone(zone))
        {
            state.progress = 1;
            Complete(index);
        }
    }
}

void Mission::UpdateGuards(float dt, const World& world, const Player& player, ZombieHorde& horde, const SoldierSquad& squad)
{
    // Даалгаврын газрын хамгаалагчид: баг (тоглогч, цэрэг) ойртоход тухайн даалгаврын нөөцөөс гарч ирнэ,
    // бүр ойртоход дайрна. Бусад газар тайван. Цэвэрлэх, хамгаалах бүс (идэвхтэй үед) өөрөө гаргана (UpdateZone).
    guardTimer -= dt;
    if (guardTimer > 0.0f)
    {
        return;
    }
    guardTimer = kGuardInterval;

    // Багаас хол хоцорсон хамгаалагчид нөөцөд буцна: баг дахин ирэхэд тэнд гарч ирнэ.
    // Бүх даалгавар биелсний дараа үлдсэн бүх зомбид хамаарна, эс бөгөөс алсад үлдсэн нь үеийг дуусгахад саад болно.
    std::vector<Vector3> anchors = { player.Position() };
    anchors.insert(anchors.end(), partners.begin(), partners.end());
    for (const Soldier& soldier : squad.All())
    {
        if (soldier.alive) anchors.push_back(soldier.mover.position);
    }
    horde.RemoveFar(anchors, kRecycleDistance, !IsComplete());

    const int cap = kGuardCap + province / 2;
    for (int i = 0; i < ObjectiveCount(); ++i)
    {
        const ObjectiveState& state = states[(size_t)i];
        const int alive = alivePool[i][0] + alivePool[i][1];
        if (PoolLeft(i, false) + PoolLeft(i, true) - alive <= 0) continue; // нөөцөөс гараагүй зомби үлдээгүй
        if (IsZoneType(stage->objectives[(size_t)i].type) && state.status == ObjectiveStatus::Active) continue;
        float nearest = 1e9f;
        for (const Vector3& anchor : anchors) nearest = std::min(nearest, HorizontalDistance(anchor, state.location));
        if (nearest > kGuardWake) continue;

        // Ойртсон баг руу дайрна; холоос бол байрандаа хамгаална
        const bool attack = nearest < kGuardAttack;
        if (attack) horde.AlertAround(state.location, kGuardAttack);
        const int batch = alive < cap / 2 ? 3 : 1;
        for (int k = 0; k < batch && alive + k < cap; ++k)
        {
            const Vector3 position = RandomAround(world, state.location, 8.0f, 60.0f);
            if (InClearedZone(position) || HorizontalDistance(position, player.Position()) < 25.0f) continue;
            if (!SpawnFromPool(horde, position, 0, attack, i)) break;
        }
    }
}

int Mission::PoolQuota(int objective, bool big) const
{
    const int pools = std::max(1, ObjectiveCount());
    const int total = big ? quotaBig : quotaSmall;
    return total / pools + (objective == 0 ? total % pools : 0);
}

int Mission::PoolLeft(int objective, bool big) const
{
    if (!save || objective < 0 || objective >= ObjectiveCount()) return 0;
    const int quota = PoolQuota(objective, big);
    return std::max(0, quota - std::min(save->guardKills[objective][big ? 1 : 0], quota));
}

bool Mission::SpawnFromPool(ZombieHorde& horde, Vector3 position, int zone, bool alerted, int pool)
{
    // Үлдсэн нөөц = даалгаврын нөөц - устгагдсан - одоо амьд байгаа (мөн үеийн нийт нөөцөөс хэтрэхгүй).
    if (pool < 0 || pool >= ObjectiveCount()) return false;
    const int smallLeft = std::min(quotaSmall - save->killedSmall - aliveSmall, PoolLeft(pool, false) - alivePool[pool][0]);
    const int bigLeft = std::min(quotaBig - save->killedBig - aliveBig, PoolLeft(pool, true) - alivePool[pool][1]);
    if (smallLeft <= 0 && bigLeft <= 0)
    {
        return false;
    }

    // Том зомби нь үлдсэн нөөцийн харьцаагаар (ойролцоогоор арав тутмын нэг); жижгүүдийн нэг хэсэг нь гүйгч.
    ZombieType type = ZombieType::Walker;
    if (bigLeft > 0 && (smallLeft <= 0 || GetRandomValue(0, smallLeft + bigLeft - 1) < bigLeft))
    {
        type = ZombieType::Brute;
        ++aliveBig;
        ++alivePool[pool][1];
    }
    else
    {
        type = GetRandomValue(0, 99) < 30 + 2 * province ? ZombieType::Runner : ZombieType::Walker;
        ++aliveSmall;
        ++alivePool[pool][0];
    }
    horde.Spawn(type, position, params, zone, alerted, pool);
    return true;
}

void Mission::Update(float dt, World& world, Player& player, ZombieHorde& horde, SoldierSquad& squad,
                     bool interactPressed, bool onFoot)
{
    prompt.clear();
    noticeTimer -= dt;

    // Үеийн нөөцийн тооцоо: устгагдсаныг бүртгэж, амьд байгааг тоолно. 25 зомби тутамд хадгална.
    int small = 0, big = 0;
    horde.TakeKillTypes(small, big);
    if (small + big > 0)
    {
        const int before = (save->killedSmall + save->killedBig) / 25;
        save->killedSmall += small;
        save->killedBig += big;
        if ((save->killedSmall + save->killedBig) / 25 != before || QuotaMet())
        {
            dirty = true;
        }
    }
    horde.TakePoolKills(save->guardKills);
    aliveSmall = aliveBig = 0;
    horde.CountAlive(aliveSmall, aliveBig);
    horde.CountAliveByPool(alivePool);

    // Хамгаалалттай даалгаврын газарт орход: хэдэн зомби хамгаалж байгааг мэдэгдэнэ
    int guarded = -1;
    float guardedDistance = kGuardWake;
    for (int i = 0; i < ObjectiveCount(); ++i)
    {
        const float distance = HorizontalDistance(player.Position(), states[(size_t)i].location);
        if (distance < guardedDistance && PoolLeft(i, false) + PoolLeft(i, true) > 0)
        {
            guardedDistance = distance;
            guarded = i;
        }
    }
    if (guarded != guardedObjective)
    {
        if (guarded >= 0)
        {
            // (Хамгаалагчдын тоо нь аймгийн нөөцийн хувь: даалгаврыг биелүүлэхэд бүгдийг нь устгах шаардлагагүй.
            // Хуучин бичвэр бүгдийг нь алах ёстой мэт ойлгогдож байв.)
            const ObjectiveDef& guardedDef = stage->objectives[(size_t)guarded];
            const ObjectiveStatus status = states[(size_t)guarded].status;
            const int around = PoolLeft(guarded, false) + PoolLeft(guarded, true);
            if (status == ObjectiveStatus::Active && guardedDef.type == ObjectiveType::ClearZone)
            {
                notice = TextFormat("%s: устгах ёстой %d зомби үлдлээ", guardedDef.title, std::max(0, ZoneRemaining(guarded)));
            }
            else if (status == ObjectiveStatus::Done)
            {
                notice = TextFormat("Энд %d зомби тэнүүчилж байна (даалгаварт заавал устгах шаардлагагүй)", around);
            }
            else
            {
                notice = TextFormat("%s: ойролцоо %d зомби хамгаалж байна", guardedDef.title, around);
            }
            noticeTimer = kNoticeTime;
        }
        guardedObjective = guarded;
    }

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

        switch (def.type)
        {
        case ObjectiveType::ReachLocation:
        {
            bool arrived = HorizontalDistance(position, state.location) < state.radius;
            for (const Vector3& partner : partners)
            {
                arrived = arrived || HorizontalDistance(partner, state.location) < state.radius;
            }
            if (arrived) Complete(i);
        }
            break;

        case ObjectiveType::InteractObject:
        case ObjectiveType::RescueSurvivors:
        case ObjectiveType::CollectItems:
        case ObjectiveType::DeliverItems:
            for (int k = 0; k < (int)state.entities.size(); ++k)
            {
                if (state.entities[(size_t)k].used) continue;
                const float reach = def.type == ObjectiveType::DeliverItems ? kInteractDistance + 1.0f
                                  : def.type == ObjectiveType::RescueSurvivors ? kInteractDistance + 0.7f : kInteractDistance;
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

    UpdateGuards(dt, world, player, horde, squad);

    // --- E товч: ойрын объекттой харилцах ---
    if (nearestObjective < 0)
    {
        return;
    }
    if (!onFoot)
    {
        prompt = InTouchMode() ? "Эхлээд унаанаасаа бууна уу" : "Эхлээд унаанаасаа бууна уу (F)";
        return;
    }

    const ObjectiveDef& def = stage->objectives[(size_t)nearestObjective];
    ObjectiveState& state = states[(size_t)nearestObjective];
    // Гараар: "E — Радио станц асаах"; мэдрэгчтэй дэлгэцэд товчлуур нь тусдаа тул зөвхөн үйлдлийн нэр.
    prompt = InTouchMode() ? std::string(def.action) : std::string("E \xE2\x80\x94 ") + def.action;

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
        if (nearestObjective < StageSave::kEntitySlots) save->entities[nearestObjective] |= 1u << nearestEntity;
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
                else if (HorizontalDistance(viewPosition, entity.position) < 45.0f)
                {
                    // Тусламж дуудаж гараа даллана (баруун гар дээш, тохой нь найгана): алсаас анзаарагдана
                    pose.seated = true;
                    pose.shoulder[0] = 165.0f;
                    pose.elbow[0] = 35.0f + 30.0f * std::sin(time * 7.0f + (float)k);
                    pose.shoulderYaw[0] = 25.0f;
                    pose.shoulder[1] = 15.0f;
                    pose.elbow[1] = 20.0f;
                    pose.headPitch = -8.0f;
                    pose.breath = std::sin(time * 3.4f + (float)k);
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
                    pose.seated = true; // өнцгүүдийг шууд хэрэглэнэ (загварын алхааны найруулагч оролцохгүй)
                    pose.breath = std::sin(time * 3.4f + (float)k);
                }
                DrawBlobShadow(entity.position, 0.5f);
                DrawHumanoid(entity.position, entity.facing, 1.0f, pose, SurvivorLook(def, k));
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

        // Алсаас харагдах нарийн гэрлийн багана (төрлийн өнгөөр): ойртоход (40 м) алга болно, дэлгэц дээрх тэмдэг заана
        const float distance = HorizontalDistance(viewPosition, state.location);
        const Color beam = MarkerColor(KindOf(def.type));
        if (distance > 40.0f)
        {
            const float width = std::clamp(distance * 0.005f, 0.25f, 2.2f); // хол байх тусам бүдүүн (харагдахуйц)
            const float fade = std::min(1.0f, (distance - 40.0f) / 60.0f);
            DrawCubeV({ state.location.x, state.location.y + 30.0f, state.location.z }, { width, 60.0f, width },
                      Fade(beam, (0.16f + 0.08f * pulse) * fade));
            DrawCubeV({ state.location.x, state.location.y + 90.0f, state.location.z }, { width * 0.7f, 60.0f, width * 0.7f },
                      Fade(beam, (0.06f + 0.04f * pulse) * fade));
        }

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
                if (s > 0) DrawLine3D(previous, point, beam);
                previous = point;
            }
        }

        // (Аврах хүн, цуглуулах зүйл, төхөөрөмж бүрийн тэмдгийг Game дэлгэц дээр 2D-ээр зурна)
    }
}

void Mission::DrawJournal() const
{
    const int screenWidth = ViewWidth();
    const int screenHeight = ViewHeight();
    // 5-аас олон даалгавартай бол цонх өндөрсөнө (дэлгэцэд багтах хэмжээгээр).
    const int width = 960, height = std::min(screenHeight - 20, 640 + 96 * std::max(0, ObjectiveCount() - 4));
    const int left = (screenWidth - width) / 2, top = (screenHeight - height) / 2;

    const Color text = Ui::kText;
    const Color dim = Ui::kTextDim;

    DrawRectangle(0, 0, screenWidth, screenHeight, Fade({ 6, 8, 12, 255 }, 0.7f));
    Ui::Panel({ (float)left, (float)top, (float)width, (float)height }, { 20, 30, 43, 235 }, 18.0f);

    int y = top + 20;
    Ui::TextDisplay("ДААЛГАВРЫН ЖУРНАЛ", left + 28, y, 30, text, 0.06f);
    DrawRectangle(left + 28, y + 40, 60, 3, kMarker);
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
    // Олон даалгавартай бол мөр бүр нам: гарчиг + нэг мөр тайлбар (багтахгүй бол "…"), явц гарчгийн хажууд
    const int available = top + height - 56 - y;
    const int blockHeight = std::clamp(available / std::max(1, ObjectiveCount()), 50, ObjectiveCount() > 4 ? 92 : 104);
    const bool compact = blockHeight < 84;
    auto fit = [](std::string line, int size, int maxWidth) {
        if (UiMeasure(line.c_str(), size) <= maxWidth) return line;
        while (!line.empty() && UiMeasure((line + "\xE2\x80\xA6").c_str(), size) > maxWidth)
        {
            line.pop_back();
            while (!line.empty() && ((unsigned char)line.back() & 0xC0) == 0x80) line.pop_back(); // UTF-8 тэмдэгтийн дунд биш
        }
        return line + "\xE2\x80\xA6";
    };
    for (int i = 0; i < ObjectiveCount(); ++i)
    {
        const ObjectiveDef& def = stage->objectives[(size_t)i];
        const ObjectiveState& state = states[(size_t)i];
        const bool done = state.status == ObjectiveStatus::Done;
        const bool locked = state.status == ObjectiveStatus::Locked;

        // Одоо хийх ёстой даалгаврыг онцолно.
        if (i == focus)
        {
            Ui::PanelAccent({ (float)left + 16.0f, (float)y - 6.0f, (float)width - 32.0f, (float)blockHeight - 4.0f }, kMarker,
                            Fade(kMarker, 0.12f));
        }

        // Тэмдэглэгээний нүд: биелсэн бол ногоон зөв тэмдэг
        const float boxSize = compact ? 22.0f : 26.0f;
        const Rectangle box = { (float)left + 34.0f, (float)y + 2.0f, boxSize, boxSize };
        Ui::Outline(box, done ? kDoneGreen : locked ? dim : kMarker, 2.0f, 6.0f);
        if (done)
        {
            const float k = boxSize / 26.0f;
            DrawLineEx({ box.x + 5.0f * k, box.y + 14.0f * k }, { box.x + 11.0f * k, box.y + 20.0f * k }, 3.0f, kDoneGreen);
            DrawLineEx({ box.x + 11.0f * k, box.y + 20.0f * k }, { box.x + 22.0f * k, box.y + 6.0f * k }, 3.0f, kDoneGreen);
        }

        char title[160];
        std::snprintf(title, sizeof(title), "%d. %s", i + 1, def.title);
        const Color titleColor = done ? kDoneGreen : locked ? dim : text;
        DrawMarkerBadge(KindOf(def.type), { (float)left + 88.0f, (float)y + (compact ? 11.0f : 13.0f) }, compact ? 11.0f : 13.0f,
                        locked ? 0.5f : 1.0f);
        Ui::TextBold(title, left + 106, y, compact ? 20 : 24, titleColor);

        // Төлөв: баруун талд
        const char* status = done ? "БИЕЛСЭН" : locked ? "ТҮГЖЭЭТЭЙ" : (i == focus ? "ОДОО ХИЙХ" : "ИДЭВХТЭЙ");
        Ui::Chip(status, left + width - 34, y - 2, titleColor, done ? kDoneGreen : locked ? dim : kMarker, true);

        const std::string progress = ProgressText(i);
        if (compact)
        {
            // Нэг мөр: тайлбар, баруун талд нь явц эсвэл шагнал
            char right[96];
            if (locked && def.after >= 0) std::snprintf(right, sizeof(right), "%d-р даалгаврын дараа", def.after + 1);
            else if (!progress.empty()) std::snprintf(right, sizeof(right), "Явц: %s", progress.c_str());
            else right[0] = '\0';
            const int rightWidth = UiMeasure(right, 16);
            UiText(right, left + width - 34 - rightWidth, y + 28, 16, done ? kDoneGreen : dim);
            UiText(fit(def.description, 16, width - 106 - 60 - rightWidth).c_str(), left + 106, y + 28, 16, locked ? dim : text);
            y += blockHeight;
            continue;
        }
        const int lines = DrawWrapped(def.description, left + 106, y + 32, 20, width - 106 - 40, locked ? dim : text);

        // Явц (тоо, хугацаатай даалгаварт) ба шагнал
        char detail[160];
        if (locked && def.after >= 0)
        {
            std::snprintf(detail, sizeof(detail), "Эхлээд %d-р даалгаврыг биелүүл.", def.after + 1);
        }
        else if (!progress.empty())
        {
            std::snprintf(detail, sizeof(detail), "Явц: %s", progress.c_str());
        }
        else
        {
            detail[0] = '\0';
        }
        UiText(detail, left + 106, y + 34 + lines * 24, 18, done ? kDoneGreen : dim);

        y += blockHeight;
    }

    char footer[128];
    std::snprintf(footer, sizeof(footer), "Явц: %d / %d даалгавар биелсэн.", CompletedCount(), ObjectiveCount());
    UiText(footer, left + 28, top + height - 44, 24, CompletedCount() == ObjectiveCount() ? kDoneGreen : text);
    const char* close = InTouchMode() ? "" : "Q / ESC \xE2\x80\x94 хаах";
    UiText(close, left + width - 28 - UiMeasure(close, 20), top + height - 40, 20, dim);
}
