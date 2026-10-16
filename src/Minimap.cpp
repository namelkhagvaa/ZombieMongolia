#include "Minimap.h"

#include "Input.h"
#include "UiFont.h"
#include "UiStyle.h"

#include "AirportData.h"
#include "Layout.h"
#include "Polygon.h"
#include "raylib.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>

namespace
{
    const float kMinimapRadius = 55.0f; // жижиг зургийн төвөөс ирмэг хүртэлх зай, метр

    const Color kBackground = { 52, 58, 42, 235 };     // аймгийн нутаг (жижиг зураг)
    const Color kFullBackground = { 62, 70, 50, 255 }; // аймгийн нутаг (бүтэн зураг)
    const Color kOutside = { 18, 20, 24, 235 };        // хилийн гадна тал
    const Color kFrame = { 232, 226, 208, 255 };
    const Color kAsphaltRoad = { 96, 96, 100, 255 };
    const Color kDirtRoad = { 150, 126, 92, 255 };
    const Color kBuilding = { 196, 184, 160, 255 };
    const Color kLowObstacle = { 128, 122, 104, 255 };
    const Color kFence = { 150, 156, 160, 255 };
    const Color kBoundary = { 200, 80, 60, 255 };
    const Color kZombie = { 230, 60, 50, 255 };
    const Color kSoldier = { 110, 220, 110, 255 };
    const Color kPlayer = { 250, 220, 90, 255 };
    const Color kVehicle = { 90, 190, 240, 255 };
    const Color kObjective = { 250, 214, 70, 255 };
    const Color kMedKit = { 220, 30, 36, 255 };

    // Газрын зургийн харагдац: ертөнцийн аль цэг дэлгэцийн хаана, ямар масштаб, эргэлтээр буух вэ.
    struct MapView
    {
        Rectangle area;  // дэлгэц дээрх талбай
        Vector2 origin;  // area-ийн төвд буух ертөнцийн цэг (x, z)
        float scale;     // пиксел / метр
        float yaw;       // дээд тал руу харах зүг (радиан); 0 = хойд зүг (+Z) дээшээ

        Vector2 Center() const { return { area.x + area.width * 0.5f, area.y + area.height * 0.5f }; }

        Vector2 ToScreen(float x, float z) const
        {
            const float dx = x - origin.x, dz = z - origin.y;
            const float u = -dx * std::cos(yaw) + dz * std::sin(yaw); // баруун тийш
            const float v = dx * std::sin(yaw) + dz * std::cos(yaw);  // дээш
            const Vector2 center = Center();
            return { center.x + u * scale, center.y - v * scale };
        }
    };

    // Аймгийн нутаг дэвсгэр (дүүргэлт), зам, саад, хил. Талбайн гаднах хэсгийг тайрна.
    void DrawTerrainLayers(const MapView& view, const World& world, Color inside, Color outside)
    {
        BeginScissorMode((int)view.area.x, (int)view.area.y, (int)view.area.width, (int)view.area.height);

        // Хилийн доторх нутаг цайвар, нүх (өөр засаг захиргааны нэгж) гадна талын өнгөөр
        const std::vector<Vector2>& fill = world.BoundaryFill();
        for (size_t i = 0; i + 2 < fill.size(); i += 3)
        {
            DrawTriangleAnyWinding(view.ToScreen(fill[i].x, fill[i].y), view.ToScreen(fill[i + 1].x, fill[i + 1].y),
                                   view.ToScreen(fill[i + 2].x, fill[i + 2].y), inside);
        }
        const std::vector<Vector2>& holes = world.BoundaryHoleFill();
        for (size_t i = 0; i + 2 < holes.size(); i += 3)
        {
            DrawTriangleAnyWinding(view.ToScreen(holes[i].x, holes[i].y), view.ToScreen(holes[i + 1].x, holes[i + 1].y),
                                   view.ToScreen(holes[i + 2].x, holes[i + 2].y), outside);
        }

        for (const Road& road : world.GetLayout().roads)
        {
            const Color color = road.kind == RoadKind::Asphalt ? kAsphaltRoad : kDirtRoad;
            const float thickness = std::max(2.0f, road.halfWidth * 2.0f * view.scale);
            for (size_t i = 0; i + 1 < road.points.size(); ++i)
            {
                const Vector2 a = view.ToScreen(road.points[i].x, road.points[i].y);
                const Vector2 b = view.ToScreen(road.points[i + 1].x, road.points[i + 1].y);
                DrawLineEx(a, b, thickness, color);
                DrawCircleV(b, thickness * 0.5f, color); // нугаралтын завсрыг бөглөнө
            }
        }

        // Буянт-Ухаа нисэх буудал (Улаанбаатарын үед): перрон, ХБЗ, явгалах зам, орох зам
        if (world.GetLayout().AirfieldActive())
        {
            static std::vector<Vector2> apronTriangles;
            if (apronTriangles.empty())
            {
                for (const airport::Ring& apron : airport::kAprons)
                {
                    const std::vector<Vector2> t = Triangulate(Polygon(airport::kPoints + apron.first, airport::kPoints + apron.first + apron.count));
                    apronTriangles.insert(apronTriangles.end(), t.begin(), t.end());
                }
                Vector2 corners[4];
                AirportParking(corners);
                for (int i : { 0, 1, 2, 0, 2, 3 }) apronTriangles.push_back(corners[i]);
            }
            const Color apronColor = { 128, 128, 124, 255 };
            for (size_t i = 0; i + 2 < apronTriangles.size(); i += 3)
            {
                DrawTriangleAnyWinding(view.ToScreen(apronTriangles[i].x, apronTriangles[i].y),
                                       view.ToScreen(apronTriangles[i + 1].x, apronTriangles[i + 1].y),
                                       view.ToScreen(apronTriangles[i + 2].x, apronTriangles[i + 2].y), apronColor);
            }
            auto polyline = [&](const Vector2* points, int count, float halfWidth, Color color) {
                const float thickness = std::max(1.5f, halfWidth * 2.0f * view.scale);
                for (int i = 0; i + 1 < count; ++i)
                {
                    const Vector2 b = view.ToScreen(points[i + 1].x, points[i + 1].y);
                    DrawLineEx(view.ToScreen(points[i].x, points[i].y), b, thickness, color);
                    DrawCircleV(b, thickness * 0.5f, color);
                }
            };
            for (const airport::Strip& strip : airport::kStrips)
            {
                if (strip.kind == 2) polyline(airport::kPoints + strip.first, strip.count, strip.halfWidth, { 100, 100, 102, 255 });
            }
            for (const airport::Strip& strip : airport::kStrips)
            {
                if (strip.kind != 2)
                {
                    polyline(airport::kPoints + strip.first, strip.count, strip.halfWidth,
                             strip.kind == 0 ? Color{ 66, 68, 72, 255 } : Color{ 134, 120, 98, 255 });
                }
            }
            const Road& access = AirportAccessRoad();
            polyline(access.points.data(), (int)access.points.size(), access.halfWidth, kAsphaltRoad);
        }

        // Саадууд: барилга цайвар, намхан саад бараан, торон хашаа саарал
        const float reach = std::max(view.area.width, view.area.height) * 0.75f / view.scale;
        for (const Obstacle& o : world.Obstacles())
        {
            if (std::fabs(o.center.x - view.origin.x) > reach || std::fabs(o.center.z - view.origin.y) > reach) continue;

            const Vector2 p = view.ToScreen(o.center.x, o.center.z);
            const float w = std::max(1.5f, o.size.x * view.scale);
            const float h = std::max(1.5f, o.size.z * view.scale);
            const Color color = !o.blocksRays ? kFence : (o.size.y > 2.4f ? kBuilding : kLowObstacle);
            DrawRectanglePro({ p.x, p.y, w, h }, { w * 0.5f, h * 0.5f }, view.yaw * RAD2DEG, color);
        }

        // Аймгийн хил ба дотор нь орж болохгүй нутгийн хил
        auto drawRing = [&](const Polygon& ring) {
            for (size_t i = 0; i < ring.size(); ++i)
            {
                const Vector2 a = ring[i];
                const Vector2 b = ring[(i + 1) % ring.size()];
                DrawLineEx(view.ToScreen(a.x, a.y), view.ToScreen(b.x, b.y), 2.5f, kBoundary);
            }
        };
        drawRing(world.Boundary().outline);
        for (const Polygon& hole : world.Boundary().holes)
        {
            drawRing(hole);
        }

        EndScissorMode();
    }

    // Цэргүүд, зомбинууд, тоглогч. clampToEdge: талбайгаас гарсан цэгийг ирмэг дээр наах эсэх.
    // Унаанууд: цэнхэр дөрвөлжин (унаж яваа нь тоглогчийн тэмдгээр харагдана).
    void DrawVehicles(const MapView& view, const Garage& garage, float markerScale)
    {
        const float left = view.area.x, right = view.area.x + view.area.width;
        const float top = view.area.y, bottom = view.area.y + view.area.height;
        for (int i = 0; i < garage.Count(); ++i)
        {
            if (i == garage.Driven()) continue;
            const Vehicle& vehicle = garage.At(i);
            const Vector2 p = view.ToScreen(vehicle.position.x, vehicle.position.z);
            if (p.x < left + 4.0f || p.x > right - 4.0f || p.y < top + 4.0f || p.y > bottom - 4.0f) continue;

            const float icon = 15.0f * markerScale;
            const Ui::Icon picture = vehicle.type == VehicleType::Horse ? Ui::Icon::MapHorse : Ui::Icon::MapVehicle;
            if (Ui::DrawIcon(picture, { p.x - icon * 0.5f, p.y - icon * 0.5f, icon, icon })) continue;
            const float size = 6.0f * markerScale;
            DrawRectangleV({ p.x - size * 0.5f - 1.0f, p.y - size * 0.5f - 1.0f }, { size + 2.0f, size + 2.0f }, BLACK);
            DrawRectangleV({ p.x - size * 0.5f, p.y - size * 0.5f }, { size, size }, kVehicle);
        }
    }

    // Идэвхтэй даалгаврын байршил: шар ромбо. Хүрээнээс гарсан бол ирмэг дээр наагдаж зүгийг заана.
    void DrawObjectives(const MapView& view, const std::vector<Vector3>& markers, float markerScale)
    {
        const Vector2 center = view.Center();
        const float limitX = view.area.width * 0.5f - 8.0f;
        const float limitY = view.area.height * 0.5f - 8.0f;
        for (const Vector3& marker : markers)
        {
            Vector2 p = view.ToScreen(marker.x, marker.z);
            const float dx = p.x - center.x, dy = p.y - center.y;
            const float overflow = std::max(std::fabs(dx) / limitX, std::fabs(dy) / limitY);
            if (overflow > 1.0f)
            {
                p = { center.x + dx / overflow, center.y + dy / overflow };
            }
            const float icon = 16.0f * markerScale;
            DrawCircleV(p, icon * 0.55f, Fade(BLACK, 0.45f));
            if (Ui::DrawIcon(Ui::Icon::MapMission, { p.x - icon * 0.5f, p.y - icon * 0.5f, icon, icon })) continue;
            const float r = 6.5f * markerScale;
            DrawPoly(p, 4, r + 2.0f, 0.0f, BLACK);
            DrawPoly(p, 4, r, 0.0f, kObjective);
        }
    }

    // Эмнэлгийн хайрцаг: лугшдаг цагаан дөрвөлжин дээр улаан загалмай. Хүрээнээс гарсан бол ирмэг дээр наагдана.
    void DrawMedKits(const MapView& view, const std::vector<Vector3>& medKits, float markerScale)
    {
        const Vector2 center = view.Center();
        const float limitX = view.area.width * 0.5f - 10.0f;
        const float limitY = view.area.height * 0.5f - 10.0f;
        const float pulse = 0.5f + 0.5f * std::sin((float)GetTime() * 5.0f);
        for (const Vector3& kit : medKits)
        {
            Vector2 p = view.ToScreen(kit.x, kit.z);
            const float dx = p.x - center.x, dy = p.y - center.y;
            const float overflow = std::max(std::fabs(dx) / limitX, std::fabs(dy) / limitY);
            if (overflow > 1.0f)
            {
                p = { center.x + dx / overflow, center.y + dy / overflow };
            }
            const float half = (6.0f + 1.5f * pulse) * markerScale;
            DrawCircleV(p, half * 2.0f, Fade(kMedKit, 0.25f * pulse));
            if (Ui::DrawIcon(Ui::Icon::MapSupply, { p.x - half * 1.3f, p.y - half * 1.3f, half * 2.6f, half * 2.6f })) continue;
            DrawRectangleV({ p.x - half - 1.5f, p.y - half - 1.5f }, { half * 2.0f + 3.0f, half * 2.0f + 3.0f }, BLACK);
            DrawRectangleV({ p.x - half, p.y - half }, { half * 2.0f, half * 2.0f }, RAYWHITE);
            const float arm = half * 0.8f, thick = half * 0.32f;
            DrawRectangleV({ p.x - arm, p.y - thick }, { arm * 2.0f, thick * 2.0f }, kMedKit);
            DrawRectangleV({ p.x - thick, p.y - arm }, { thick * 2.0f, arm * 2.0f }, kMedKit);
        }
    }

    void DrawMarkers(const MapView& view, const Player& player, const ZombieHorde& horde, const SoldierSquad& squad,
                     bool clampToEdge, float markerScale)
    {
        const Vector2 center = view.Center();
        const float limitX = view.area.width * 0.5f - 5.0f;
        const float limitY = view.area.height * 0.5f - 5.0f;

        // Буцаах утга: зурах эсэх. outside: ирмэг дээр наагдсан эсэх.
        auto place = [&](Vector2& p, bool& outside) {
            const float dx = p.x - center.x, dy = p.y - center.y;
            const float overflow = std::max(std::fabs(dx) / limitX, std::fabs(dy) / limitY);
            outside = overflow > 1.0f;
            if (!outside) return true;
            if (!clampToEdge) return false;
            p = { center.x + dx / overflow, center.y + dy / overflow };
            return true;
        };

        for (const Soldier& soldier : squad.All())
        {
            if (!soldier.alive) continue;
            Vector2 p = view.ToScreen(soldier.mover.position.x, soldier.mover.position.z);
            bool outside = false;
            if (!place(p, outside)) continue;
            const float icon = 9.0f * markerScale;
            if (Ui::DrawIcon(Ui::Icon::MapAlly, { p.x - icon * 0.5f, p.y - icon * 0.5f, icon, icon })) continue;
            DrawCircleV(p, 3.5f * markerScale, BLACK);
            DrawCircleV(p, 2.5f * markerScale, kSoldier);
        }

        for (int i = 0; i < horde.Count(); ++i)
        {
            const Zombie& zombie = horde.At(i);
            if (!zombie.IsAlive()) continue;
            Vector2 p = view.ToScreen(zombie.mover.position.x, zombie.mover.position.z);
            bool outside = false;
            if (!place(p, outside)) continue;
            const float radius = (zombie.type == ZombieType::Brute ? 4.0f : 3.0f) * markerScale;
            const bool brute = zombie.type == ZombieType::Brute;
            const float icon = radius * 2.6f;
            if (Ui::DrawIcon(brute ? Ui::Icon::MapElite : Ui::Icon::MapZombie, { p.x - icon * 0.5f, p.y - icon * 0.5f, icon, icon },
                             outside ? Fade(WHITE, 0.55f) : WHITE))
            {
                continue;
            }
            DrawCircleV(p, radius, outside ? Fade(kZombie, 0.55f) : kZombie);
        }

        // Тоглогч: биеийн харж буй зүг рүү заасан сум
        Vector2 p = view.ToScreen(player.Position().x, player.Position().z);
        bool outside = false;
        if (place(p, outside))
        {
            const float relative = player.Facing() - view.yaw;
            const float dirX = -std::sin(relative), dirY = -std::cos(relative);
            const float size = markerScale;
            const float icon = 15.0f * size;
            const float degrees = std::atan2(dirX, -dirY) * RAD2DEG; // зураг дээш заасан: харах зүг рүү эргүүлнэ
            DrawCircleV(p, icon * 0.42f, Fade(BLACK, 0.4f));
            if (Ui::DrawIcon(Ui::Icon::MapPlayer, { p.x - icon * 0.5f, p.y - icon * 0.5f, icon, icon }, WHITE, degrees)) return;
            const Vector2 tip = { p.x + dirX * 8.0f * size, p.y + dirY * 8.0f * size };
            const Vector2 backLeft = { p.x - dirX * 5.0f * size + dirY * 5.0f * size,
                                       p.y - dirY * 5.0f * size - dirX * 5.0f * size };
            const Vector2 backRight = { p.x - dirX * 5.0f * size - dirY * 5.0f * size,
                                        p.y - dirY * 5.0f * size + dirX * 5.0f * size };
            DrawTriangle(tip, backLeft, backRight, kPlayer);
            DrawTriangle(tip, backRight, backLeft, kPlayer); // эргэлтийн чиглэлээс үл хамааран харагдуулна
            DrawCircleV(p, 2.0f * size, BLACK);
        }
    }

    void DrawLegendEntry(int x, int y, Color color, const char* text)
    {
        DrawCircle(x + 7, y + 9, 6.0f, color);
        UiText(text, x + 20, y, 18, kFrame);
    }
}

// ---------------------------------------------------------------- Жижиг зураг

void DrawMinimap(const World& world, const Player& player, float cameraYaw, const ZombieHorde& horde,
                 const SoldierSquad& squad, const Garage& garage, const std::vector<Vector3>& markers,
                 const std::vector<Vector3>& medKits)
{
    const float size = (float)kMinimapSize;
    MapView view;
    // Мэдрэгчтэй дэлгэцэд баруун доод буланд тулааны товчлуурууд байдаг тул газрын зураг баруун дээд буланд.
    view.area = { (float)(ViewWidth() - kMinimapSize - kMinimapMargin),
                  InTouchMode() ? (float)kMinimapMargin : (float)(ViewHeight() - kMinimapSize - kMinimapMargin), size, size };
    view.origin = { player.Position().x, player.Position().z };
    view.scale = size * 0.5f / kMinimapRadius;
    view.yaw = cameraYaw;

    // Шил карт дотор: эргэн тойронд 6 px зай, нимгэн хүрээ
    Ui::Panel({ view.area.x - 6.0f, view.area.y - 6.0f, view.area.width + 12.0f, view.area.height + 12.0f }, Ui::kPanel, 12.0f);
    DrawRectangleRec(view.area, kOutside);
    DrawTerrainLayers(view, world, kBackground, kOutside);
    DrawVehicles(view, garage, 1.0f);
    DrawObjectives(view, markers, 1.0f);
    DrawMarkers(view, player, horde, squad, true, 1.0f);
    DrawMedKits(view, medKits, 1.0f);
    DrawRectangleLinesEx(view.area, 1.0f, Ui::kBorder);

    // Хойд зүгийн (+Z) тэмдэг хүрээн дээр
    const Vector2 center = view.Center();
    const float northX = std::sin(cameraYaw), northY = -std::cos(cameraYaw);
    const float reach = std::max(std::fabs(northX), std::fabs(northY));
    const Vector2 north = { center.x + northX / reach * (size * 0.5f - 9.0f),
                            center.y + northY / reach * (size * 0.5f - 9.0f) };
    DrawCircleV(north, 10.0f, Ui::kAccent);
    const int northWidth = Ui::MeasureBold("Х", 14);
    Ui::TextBold("Х", (int)north.x - northWidth / 2, (int)north.y - 8, 14, { 20, 16, 8, 255 });
}

// ---------------------------------------------------------------- Бүтэн дэлгэцийн зураг

namespace
{
    Rectangle FullMapArea()
    {
        const float margin = 60.0f;
        return { margin, margin + 10.0f, (float)ViewWidth() - margin * 2.0f,
                 (float)ViewHeight() - margin * 2.0f - 20.0f };
    }
}

float FullMap::BaseScale(const World& world) const
{
    // 100%: аймгийн хил бүтнээрээ, захаасаа бага зэрэг зайтай багтана.
    const Rectangle area = FullMapArea();
    const Rectangle bounds = world.Boundary().Bounds();
    return 0.94f * std::min(area.width / std::max(1.0f, bounds.width), area.height / std::max(1.0f, bounds.height));
}

void FullMap::Open(const World& world)
{
    // Аймгийн хилийн хэлбэр бүтнээрээ харагдахаар төвлөрүүлж, 100%-иас эхэлнэ.
    const Rectangle bounds = world.Boundary().Bounds();
    open = true;
    zoom = kMinZoom;
    focus = { bounds.x + bounds.width * 0.5f, bounds.y + bounds.height * 0.5f };
}

void FullMap::SetZoom(float newZoom)
{
    zoom = std::clamp(newZoom, kMinZoom, kMaxZoom);
}

void FullMap::Update(const World& world, const Player& player)
{
    const Rectangle area = FullMapArea();
    const Vector2 center = { area.x + area.width * 0.5f, area.y + area.height * 0.5f };
    const Vector2 mouse = InPointer();

    // Хойд зүг дээшээ харах үед дэлгэцийн баруун тал нь ертөнцийн -X, доод тал нь -Z тул
    // дэлгэцийн шилжилтийг ертөнцийн шилжилт болгоход тэмдэг нь урвуу.
    const float wheel = InWheel();
    if (wheel != 0.0f)
    {
        // Хулганы доорх цэг байрандаа үлдэхээр томруулна.
        const float before = BaseScale(world) * zoom;
        const Vector2 anchor = { focus.x - (mouse.x - center.x) / before, focus.y - (mouse.y - center.y) / before };
        SetZoom(zoom * std::pow(1.2f, wheel));
        const float after = BaseScale(world) * zoom;
        focus = { anchor.x + (mouse.x - center.x) / after, anchor.y + (mouse.y - center.y) / after };
    }

    if (InMouseDown(MOUSE_BUTTON_LEFT))
    {
        const Vector2 delta = InLookDelta();
        const float scale = BaseScale(world) * zoom;
        focus.x += delta.x / scale;
        focus.y += delta.y / scale;
    }

    if (InKeyPressed(KEY_C))
    {
        focus = { player.Position().x, player.Position().z };
    }

    const Rectangle bounds = world.Boundary().Bounds();
    focus.x = Clamp(focus.x, bounds.x, bounds.x + bounds.width);
    focus.y = Clamp(focus.y, bounds.y, bounds.y + bounds.height);
}

void FullMap::Draw(const World& world, const Player& player, const ZombieHorde& horde, const SoldierSquad& squad,
                   const Garage& garage, const std::vector<Vector3>& markers, const std::vector<Vector3>& medKits,
                   const char* title) const
{
    const int width = ViewWidth();
    const int height = ViewHeight();

    MapView view;
    view.area = FullMapArea();
    view.origin = focus;
    view.scale = BaseScale(world) * zoom;
    view.yaw = 0.0f;

    DrawRectangle(0, 0, width, height, Fade({ 6, 8, 12, 255 }, 0.85f));
    Ui::Panel({ view.area.x - 8.0f, view.area.y - 8.0f, view.area.width + 16.0f, view.area.height + 16.0f }, Ui::kPanel, 14.0f);
    DrawRectangleRec(view.area, kOutside);
    DrawTerrainLayers(view, world, kFullBackground, kOutside);

    // Газрын нэрс
    BeginScissorMode((int)view.area.x, (int)view.area.y, (int)view.area.width, (int)view.area.height);
    const struct { float x, z; const char* name; } places[] = {
        { 28.0f, 34.0f, "СУУРИН" },  { -52.0f, 2.0f, "ЗАСТАВ" }, { 38.0f, -13.0f, "ШАТАХУУН ТҮГЭЭХ ГАЗАР" },
        { 10.0f, 70.0f, "АГУУЛАХ" }, { kTownCenter.x, kTownCenter.y, "АЙМГИЙН ТӨВ" }, { kShopCenter.x, kShopCenter.y + 5.5f, "ДЭЛГҮҮР" },
        { airport::kTerminal.x, airport::kTerminal.y - 60.0f, world.GetLayout().AirfieldActive() ? "БУЯНТ-УХАА НИСЭХ БУУДАЛ" : nullptr },
    };
    for (const auto& place : places)
    {
        if (place.name == nullptr) continue;
        const Vector2 p = view.ToScreen(place.x, place.z);
        const int textWidth = UiMeasure(place.name, 16);
        UiText(place.name, (int)p.x - textWidth / 2 + 1, (int)p.y - 7, 16, Fade(BLACK, 0.8f));
        UiText(place.name, (int)p.x - textWidth / 2, (int)p.y - 8, 16, kFrame);
    }
    if (world.GetLayout().AirfieldActive())
    {
        // ODbL: нисэх буудлын зураг OpenStreetMap-ийн өгөгдлөөр
        UiText("Нисэх буудлын зураг: (c) OpenStreetMap contributors", (int)view.area.x + 10,
               (int)(view.area.y + view.area.height) - 22, 14, Fade(kFrame, 0.75f));
    }
    EndScissorMode();

    DrawVehicles(view, garage, 1.3f);
    DrawObjectives(view, markers, 1.5f);
    DrawMarkers(view, player, horde, squad, true, 1.5f);
    DrawMedKits(view, medKits, 1.5f);
    DrawRectangleLinesEx(view.area, 1.0f, Ui::kBorder);

    // Гарчиг, томруулалт, тайлбар
    Ui::TextBold("ГАЗРЫН ЗУРАГ", (int)view.area.x, 18, 28, Ui::kText);
    Ui::Text(title, (int)view.area.x + Ui::MeasureBold("ГАЗРЫН ЗУРАГ", 28) + 16, 26, 20, Ui::kAccent);
    const char* zoomText = TextFormat("%d%%", (int)std::round(zoom * 100.0f));
    Ui::Chip(zoomText, width - (int)view.area.x, 18, Ui::kText, Ui::kInfo, true);
    UiText("Х", width / 2 - 7, (int)view.area.y + 6, 22, kFrame);

    const int legendY = height - 40;
    int x = (int)view.area.x;
    DrawLegendEntry(x, legendY, kPlayer, "та");
    DrawLegendEntry(x + 60, legendY, kZombie, "зомби");
    DrawLegendEntry(x + 150, legendY, kSoldier, "цэрэг");
    DrawLegendEntry(x + 240, legendY, kVehicle, "унаа");
    DrawLegendEntry(x + 320, legendY, kObjective, "даалгавар");
    DrawLegendEntry(x + 450, legendY, kMedKit, "эмнэлэг");
    const char* help = "Хулганы дугуй — томруулах (100–1000%)    Чирэх — зөөх    C — өөр дээрээ төвлөрүүлэх    M — хаах";
    UiText(help, width - (int)view.area.x - UiMeasure(help, 18), legendY, 18, kFrame);
}
