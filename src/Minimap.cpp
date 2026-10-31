#include "Minimap.h"

#include "Input.h"
#include "UiFont.h"
#include "UiStyle.h"

#include "AirportData.h"
#include "City.h"
#include "Navigation.h"
#include "Layout.h"
#include "Polygon.h"
#include "raylib.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>

namespace
{
    const Navigator* navigation = nullptr;
}

void SetMapNavigator(const Navigator* navigator)
{
    navigation = navigator;
}

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
    const Color kEnemy = { 255, 150, 30, 255 };   // эсрэг багийн хүн (зомбины улаанаас ялгарна)
    const Color kPartner = { 90, 175, 255, 255 }; // хамтран тоглож буй нөгөө тоглогч
    const std::vector<MapPerson>* mapPeople = nullptr;
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

    // Улаанбаатарын хотын газрын зураг: барилга, цэцэрлэг, усны гурвалжнууд (нэг удаа үүсгэнэ).
    struct CityMapShape
    {
        float minX, minZ, maxX, maxZ;
        size_t first, count; // triangles доторх оройн муж
        Color color;
    };
    struct CityMapCache
    {
        std::vector<Vector2> triangles;
        std::vector<CityMapShape> areas, buildings;
    };

    // Идэвхтэй хотын (аймгийн) зураг: аймаг солигдоход дахин үүсгэнэ.
    const CityMapCache& CityMap()
    {
        static CityMapCache cache;
        static int province = -2;
        if (province != CurrentCity().province)
        {
            province = CurrentCity().province;
            cache = [] {
            CityMapCache c;
            const CityData& city = CurrentCity();
            auto add = [&](std::vector<CityMapShape>& list, const std::vector<Vector2>& outline, Color color) {
                CityMapShape shape = { 1e9f, 1e9f, -1e9f, -1e9f, c.triangles.size(), 0, color };
                for (const Vector2& p : outline)
                {
                    shape.minX = std::min(shape.minX, p.x); shape.maxX = std::max(shape.maxX, p.x);
                    shape.minZ = std::min(shape.minZ, p.y); shape.maxZ = std::max(shape.maxZ, p.y);
                }
                const std::vector<Vector2> t = Triangulate(outline);
                c.triangles.insert(c.triangles.end(), t.begin(), t.end());
                shape.count = t.size();
                list.push_back(shape);
            };
            static const Color kAreaColors[] = { { 70, 104, 132, 255 }, { 70, 100, 56, 255 }, { 84, 112, 64, 255 },
                                                 { 56, 84, 46, 255 },   { 86, 96, 70, 255 },  { 112, 110, 104, 255 } };
            for (const CityArea& area : city.areas) add(c.areas, area.outline, kAreaColors[(int)area.kind]);
            for (const CityBuilding& building : city.buildings)
            {
                const Color color = building.type == CityBuildingType::Ger ? Color{ 220, 214, 198, 255 }
                                  : building.height > 20.0f                ? Color{ 214, 202, 176, 255 }
                                                                           : kBuilding;
                add(c.buildings, building.outline, color);
            }
            return c;
            }();
        }
        return cache;
    }

    void DrawCityShapes(const MapView& view, const CityMapCache& map, const std::vector<CityMapShape>& shapes, float reach)
    {
        for (const CityMapShape& shape : shapes)
        {
            if (shape.maxX < view.origin.x - reach || shape.minX > view.origin.x + reach || shape.maxZ < view.origin.y - reach ||
                shape.minZ > view.origin.y + reach)
            {
                continue;
            }
            if ((shape.maxX - shape.minX) * view.scale < 0.6f && (shape.maxZ - shape.minZ) * view.scale < 0.6f) continue; // пикселээс жижиг
            for (size_t i = shape.first; i + 2 < shape.first + shape.count; i += 3)
            {
                DrawTriangleAnyWinding(view.ToScreen(map.triangles[i].x, map.triangles[i].y),
                                       view.ToScreen(map.triangles[i + 1].x, map.triangles[i + 1].y),
                                       view.ToScreen(map.triangles[i + 2].x, map.triangles[i + 2].y), shape.color);
            }
        }
    }

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

        const float reach = std::max(view.area.width, view.area.height) * 0.75f / view.scale;
        const bool city = world.CityActive();
        if (city)
        {
            DrawCityShapes(view, CityMap(), CityMap().areas, reach);
        }
        for (const Road& road : world.GetLayout().roads)
        {
            const Color color = road.kind == RoadKind::Asphalt ? kAsphaltRoad : kDirtRoad;
            const float thickness = std::max(city ? 1.0f : 2.0f, road.halfWidth * 2.0f * view.scale);
            for (size_t i = 0; i + 1 < road.points.size(); ++i)
            {
                const Vector2 pa = road.points[i], pb = road.points[i + 1];
                if (std::max(pa.x, pb.x) < view.origin.x - reach || std::min(pa.x, pb.x) > view.origin.x + reach ||
                    std::max(pa.y, pb.y) < view.origin.y - reach || std::min(pa.y, pb.y) > view.origin.y + reach)
                {
                    continue;
                }
                const Vector2 a = view.ToScreen(pa.x, pa.y);
                const Vector2 b = view.ToScreen(pb.x, pb.y);
                DrawLineEx(a, b, thickness, color);
                if (thickness > 2.5f) DrawCircleV(b, thickness * 0.5f, color); // нугаралтын завсрыг бөглөнө
            }
        }
        if (city)
        {
            DrawCityShapes(view, CityMap(), CityMap().buildings, reach);
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

        // Саадууд: барилга цайвар, намхан саад бараан, торон хашаа саарал (хотод OSM-ийн барилгыг дээр зурсан)
        const std::vector<Obstacle>& obstacles = world.Obstacles();
        for (size_t k = city ? world.StaticObstacleCount() : 0; k < obstacles.size(); ++k)
        {
            const Obstacle& o = obstacles[k];
            if (std::fabs(o.center.x - view.origin.x) > reach || std::fabs(o.center.z - view.origin.y) > reach) continue;

            const Vector2 p = view.ToScreen(o.center.x, o.center.z);
            const float w = std::max(1.5f, o.size.x * view.scale);
            const float h = std::max(1.5f, o.size.z * view.scale);
            const Color color = !o.blocksRays ? kFence : (o.size.y > 2.4f ? kBuilding : kLowObstacle);
            DrawRectanglePro({ p.x, p.y, w, h }, { w * 0.5f, h * 0.5f }, view.yaw * RAD2DEG, color);
        }

        // Очих газар (зам зурахгүй: сум зөвхөн зүгийг заана)
        if (navigation && navigation->Active())
        {
            const Vector2 t = view.ToScreen(navigation->Target().x, navigation->Target().y);
            DrawCircleV(t, 8.0f, Fade(BLACK, 0.6f));
            DrawCircleV(t, 6.0f, { 255, 196, 40, 255 });
            DrawCircleV(t, 2.5f, BLACK);
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

            // Бүтэн зураг дээр (markerScale > 1) олон унаа газрын зургийг дүүргэдэг тул жижиг, бүдэг
            const bool crowded = markerScale > 1.2f;
            const float icon = (crowded ? 11.0f : 15.0f) * markerScale;
            const Ui::Icon picture = vehicle.type == VehicleType::Horse ? Ui::Icon::MapHorse : Ui::Icon::MapVehicle;
            if (Ui::DrawIcon(picture, { p.x - icon * 0.5f, p.y - icon * 0.5f, icon, icon }, crowded ? Fade(WHITE, 0.7f) : WHITE)) continue;
            const float size = 6.0f * markerScale;
            DrawRectangleV({ p.x - size * 0.5f - 1.0f, p.y - size * 0.5f - 1.0f }, { size + 2.0f, size + 2.0f }, BLACK);
            DrawRectangleV({ p.x - size * 0.5f, p.y - size * 0.5f }, { size, size }, kVehicle);
        }
    }

    const char* DistanceText(float meters)
    {
        return meters < 1000.0f ? TextFormat("%d м", (int)std::round(meters / 10.0f) * 10) : TextFormat("%.1f км", meters / 1000.0f);
    }

    // Идэвхтэй даалгаврууд: төрлийн тэмдэг (туг, хүн, хайрцаг, бамбай ...), дугаартай; одоо хийх ёстой нь лугшдаг
    // цагирагтай. Хүрээнээс гарсан бол ирмэг дээр наагдаж зүгийг заана.
    // from: (бүтэн зураг) тоглогчийн байрлал: тэмдгийн хажууд нэр, зай бичнэ. selected: чиглэл заагдаж буй цэг.
    void DrawObjectives(const MapView& view, const std::vector<MapMarker>& markers, float markerScale, const Vector3* from = nullptr,
                        const Vector2* selected = nullptr)
    {
        const Vector2 center = view.Center();
        const float limitX = view.area.width * 0.5f - 12.0f * markerScale;
        const float limitY = view.area.height * 0.5f - 12.0f * markerScale;
        // Үйл явдлууд эхэнд, даалгаврууд дээр нь (давхцвал даалгавар харагдана)
        for (int pass = 0; pass < 2; ++pass)
        {
            for (const MapMarker& marker : markers)
            {
                if ((marker.objective >= 0) != (pass == 1)) continue;
                Vector2 p = view.ToScreen(marker.position.x, marker.position.z);
                const float dx = p.x - center.x, dy = p.y - center.y;
                const float overflow = std::max(std::fabs(dx) / limitX, std::fabs(dy) / limitY);
                const bool edge = overflow > 1.0f;
                if (edge) p = { center.x + dx / overflow, center.y + dy / overflow };
                const float radius = (marker.objective >= 0 ? 10.0f : 8.0f) * markerScale;
                const Color color = MarkerColor(marker.kind);
                const bool chosen = selected && Vector2Distance(*selected, { marker.position.x, marker.position.z }) < 3.0f;
                if (marker.focus || chosen)
                {
                    const float pulse = std::fmod((float)GetTime() * 0.9f, 1.0f);
                    DrawRing(p, radius * (1.0f + 0.7f * pulse), radius * (1.15f + 0.7f * pulse), 0.0f, 360.0f, 32,
                             Fade(chosen ? WHITE : color, 0.85f * (1.0f - pulse)));
                }
                if (edge)
                {
                    // Ирмэг дээр: гадагш заасан жижиг гурвалжин
                    const float length = std::sqrt(dx * dx + dy * dy);
                    const Vector2 dir = { dx / length, dy / length }, side = { -dir.y, dir.x };
                    const Vector2 tip = { p.x + dir.x * radius * 1.6f, p.y + dir.y * radius * 1.6f };
                    const Vector2 b = { p.x + dir.x * radius * 0.9f + side.x * radius * 0.6f, p.y + dir.y * radius * 0.9f + side.y * radius * 0.6f };
                    const Vector2 c = { p.x + dir.x * radius * 0.9f - side.x * radius * 0.6f, p.y + dir.y * radius * 0.9f - side.y * radius * 0.6f };
                    const float cross = (b.x - tip.x) * (c.y - tip.y) - (b.y - tip.y) * (c.x - tip.x);
                    if (cross < 0.0f) DrawTriangle(tip, b, c, color);
                    else DrawTriangle(tip, c, b, color);
                }
                DrawMarkerBadge(marker.kind, p, radius, 1.0f, marker.objective >= 0 && markerScale > 1.2f ? marker.objective + 1 : 0);
                if (from && !edge)
                {
                    // Бүтэн зураг: баруун талд нь нэр ба зай ("Эмнэлэгт оч · 240 м")
                    const float meters = std::sqrt((marker.position.x - from->x) * (marker.position.x - from->x) +
                                                   (marker.position.z - from->z) * (marker.position.z - from->z));
                    const char* text = TextFormat("%s  ·  %s", marker.title, DistanceText(meters));
                    const int w = Ui::MeasureBold(text, 15);
                    const Rectangle pill = { p.x + radius + 6.0f, p.y - 11.0f, (float)w + 16.0f, 22.0f };
                    Ui::Fill(pill, Fade({ 20, 30, 43, 255 }, chosen ? 0.95f : 0.8f));
                    if (chosen) Ui::Stroke(pill, 1.5f, WHITE);
                    Ui::TextBold(text, (int)(pill.x + 8.0f), (int)pill.y + 3, 15, marker.objective >= 0 ? color : Ui::kText);
                }
            }
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

    // Дэлгүүрийн тэмдэг: алтан дугуй дээр цүнхний дүрс (газрын зураг, тайлбарт)
    void DrawShopBadge(Vector2 p, float r)
    {
        DrawCircleV(p, r + 2.0f, { 20, 14, 4, 230 });
        DrawCircleV(p, r, { 242, 178, 48, 255 });
        // Цүнх: бие ба бариул
        const Color ink = { 40, 26, 6, 255 };
        const Rectangle bag = { p.x - r * 0.5f, p.y - r * 0.2f, r, r * 0.7f };
        Ui::Fill(bag, ink);
        DrawRing({ p.x, bag.y }, r * 0.24f, r * 0.36f, 180.0f, 360.0f, 12, ink);
    }

    // Баавгайн тэмдэг: хүрэн цэг, цагаан хүрээтэй (уурласан бол улбар хүрээ)
    void DrawBearBadge(Vector2 p, float r, bool alerted)
    {
        DrawCircleV(p, r + 1.6f, alerted ? Color{ 255, 140, 40, 255 } : Color{ 236, 228, 214, 255 });
        DrawCircleV(p, r, Color{ 120, 78, 44, 255 });
        DrawCircleV({ p.x - r * 0.45f, p.y - r * 0.55f }, r * 0.32f, Color{ 120, 78, 44, 255 }); // чих
        DrawCircleV({ p.x + r * 0.45f, p.y - r * 0.55f }, r * 0.32f, Color{ 120, 78, 44, 255 });
    }

    // Дэлгүүр (сум, гранат, амь, цэрэг): алтан дугуй дээр цүнхний дүрс. Жижиг зурагт хүрээнээс гарвал ирмэг дээр
    // наагдаж зүгийг заана, бүтэн зураг дээр том.
    void DrawShop(const MapView& view, float markerScale, bool clampToEdge)
    {
        Vector2 p = view.ToScreen(kShopCenter.x, kShopCenter.y);
        const Vector2 center = view.Center();
        const float limitX = view.area.width * 0.5f - 11.0f, limitY = view.area.height * 0.5f - 11.0f;
        const float dx = p.x - center.x, dy = p.y - center.y;
        const float overflow = std::max(std::fabs(dx) / limitX, std::fabs(dy) / limitY);
        if (overflow > 1.0f)
        {
            if (!clampToEdge) return;
            p = { center.x + dx / overflow, center.y + dy / overflow };
        }
        DrawShopBadge(p, 8.5f * markerScale);
    }

    // Застав (бааз): Structures.cpp-ийн BuildCheckpoint-ийн төв, бүтэн зургийн "ЗАСТАВ" бичвэртэй ижил цэг.
    constexpr Vector2 kOutpost = { -52.0f, 2.0f };

    // Тогтмол газрууд: харуулын цамхаг (шатаар авирах цэг), застав. Талбайгаас гарсан бол зурахгүй.
    // Икон байхгүй бол юу ч зурахгүй (зам, барилга нь өөрөө харагдана).
    void DrawLandmarks(const MapView& view, const World& world, float markerScale, bool showBase)
    {
        const float left = view.area.x, right = view.area.x + view.area.width;
        const float top = view.area.y, bottom = view.area.y + view.area.height;
        auto inside = [&](Vector2 p, float half) {
            return p.x > left + half && p.x < right - half && p.y > top + half && p.y < bottom - half;
        };
        const float tower = 17.0f * markerScale;
        for (int i = 0; i < world.PassageCount(); ++i)
        {
            const Passage& passage = world.GetPassage(i);
            if (passage.kind != PassageKind::LadderUp) continue;
            const Vector2 p = view.ToScreen(passage.to.x, passage.to.z);
            if (!inside(p, tower * 0.5f)) continue;
            Ui::DrawIcon(Ui::Icon::MapTower, { p.x - tower * 0.5f, p.y - tower * 0.5f, tower, tower });
        }
        if (showBase)
        {
            const float base = 15.0f * markerScale;
            const Vector2 p = view.ToScreen(kOutpost.x, kOutpost.y);
            if (inside(p, base * 0.5f))
            {
                DrawCircleV(p, base * 0.6f, Fade(BLACK, 0.4f));
                Ui::DrawIcon(Ui::Icon::MapBase, { p.x - base * 0.5f, p.y - base * 0.5f, base, base });
            }
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

        if (mapPeople)
        {
            // Хамтарсан тоглолт: өөрийн ба эсрэг баг өөр өнгөөр, нөгөө тоглогч томоор (ирмэг дээр ч харагдана)
            for (const MapPerson& person : *mapPeople)
            {
                Vector2 p = view.ToScreen(person.position.x, person.position.z);
                bool outside = false;
                if (!place(p, outside)) continue;
                const Color color = person.enemy ? kEnemy : person.human ? kPartner : kSoldier;
                const float radius = (person.human ? 4.6f : 2.8f) * markerScale;
                const Color fill = outside ? Fade(color, 0.7f) : color;
                if (person.enemy && person.human)
                {
                    // Эсрэг тоглогч: лугшдаг цагираг (хаана байгааг шууд олно)
                    const float pulse = std::fmod((float)GetTime() * 1.2f, 1.0f);
                    DrawRing(p, radius * (1.4f + 1.6f * pulse), radius * (1.7f + 1.6f * pulse), 0.0f, 360.0f, 32,
                             Fade(kEnemy, 0.9f * (1.0f - pulse)));
                }
                if (person.enemy)
                {
                    // Эсрэг баг: ромбо (өнгө ялгадаггүй хүнд ч ялгарна)
                    DrawPoly(p, 4, radius * 1.35f + 1.5f * markerScale, 0.0f, person.human ? WHITE : BLACK);
                    DrawPoly(p, 4, radius * 1.35f, 0.0f, fill);
                }
                else
                {
                    DrawCircleV(p, radius + 1.5f * markerScale, person.human ? WHITE : BLACK);
                    DrawCircleV(p, radius, fill);
                }
            }
        }
        else
        {
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
        }

        for (int i = 0; i < horde.Count(); ++i)
        {
            const Zombie& zombie = horde.At(i);
            if (!zombie.IsAlive()) continue;
            Vector2 p = view.ToScreen(zombie.mover.position.x, zombie.mover.position.z);
            bool outside = false;
            if (!place(p, outside)) continue;
            if (zombie.IsBear())
            {
                // Баавгай: хүрэн цэг (уурласан бол улбар хүрээтэй); газрын зургийн ирмэгт гаргахгүй
                if (outside) continue;
                DrawBearBadge(p, 3.4f * markerScale, zombie.alerted);
                continue;
            }
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
    }

    // Тоглогчийн тэмдэг: цагаан хүрээтэй бараан дугуй дээр харж буй зүг (dirX, dirY: дэлгэцийн чиглэл) рүү
    // заасан шар сум (навигацийн газрын зураг шиг; "анхааруулга" гурвалжин дүрс шиг биш)
    void DrawPlayerBadge(Vector2 p, float radius, float dirX, float dirY)
    {
        DrawCircleV(p, radius, WHITE);
        DrawCircleV(p, radius * 0.84f, { 24, 28, 36, 255 });
        const float s = radius * 0.62f;
        const Vector2 tip = { p.x + dirX * s, p.y + dirY * s };
        const Vector2 left = { p.x - dirX * s * 0.7f + dirY * s * 0.75f, p.y - dirY * s * 0.7f - dirX * s * 0.75f };
        const Vector2 right = { p.x - dirX * s * 0.7f - dirY * s * 0.75f, p.y - dirY * s * 0.7f + dirX * s * 0.75f };
        const Vector2 notch = { p.x - dirX * s * 0.25f, p.y - dirY * s * 0.25f };
        // Сумны хоёр хагас (эргэлтийн чиглэлээс үл хамааран харагдахаар хоёр талаас нь)
        DrawTriangle(tip, left, notch, kPlayer);
        DrawTriangle(tip, notch, left, kPlayer);
        DrawTriangle(tip, notch, right, kPlayer);
        DrawTriangle(tip, right, notch, kPlayer);
    }

    // Тоглогч: бусад бүх тэмдгийн дээр (даалгавар, эмнэлэгтэй давхцаж дарагдахгүй). Лугшдаг гэрэлт хүрээ,
    // цагаан хүрээтэй дугуй дээр харж буй зүг рүү заасан сум.
    void DrawPlayer(const MapView& view, const Player& player, float markerScale)
    {
        Vector2 p = view.ToScreen(player.Position().x, player.Position().z);
        const Vector2 center = view.Center();
        const float limitX = view.area.width * 0.5f - 5.0f, limitY = view.area.height * 0.5f - 5.0f;
        const float overflow = std::max(std::fabs(p.x - center.x) / limitX, std::fabs(p.y - center.y) / limitY);
        if (overflow > 1.0f) p = { center.x + (p.x - center.x) / overflow, center.y + (p.y - center.y) / overflow };

        const float relative = player.Facing() - view.yaw;
        const float pulse = std::fmod((float)GetTime() * 0.8f, 1.0f);
        const float icon = 15.0f * markerScale;
        DrawCircleV(p, icon * (0.6f + 0.7f * pulse), Fade(kPlayer, 0.35f * (1.0f - pulse)));
        DrawPlayerBadge(p, icon * 0.56f, -std::sin(relative), -std::cos(relative));
    }

    // Газрын нэр: бараан бөөрөнхий шошго дээр (газрын зургийн өнгөнөөс үл хамааран уншигдана)
    void DrawPlaceLabel(Vector2 p, const char* name, Color color)
    {
        const int w = Ui::MeasureBold(name, 15);
        const Rectangle pill = { p.x - (float)w * 0.5f - 9.0f, p.y - 11.0f, (float)w + 18.0f, 22.0f };
        Ui::Fill(pill, { 16, 18, 22, 210 });
        Ui::Stroke(pill, 1.0f, Fade(color, 0.35f));
        Ui::TextBold(name, (int)(pill.x + 9.0f), (int)pill.y + 3, 15, color);
    }

    // Тайлбарын нэг мөр: газрын зураг дээрхтэй ижил дүрс + нэр. Дараагийн мөрийн x-ийг буцаана.
    int DrawLegendItem(int x, int y, Ui::Icon icon, Color fallback, const char* text)
    {
        const Rectangle box = { (float)x, (float)y - 1.0f, 20.0f, 20.0f };
        if (!Ui::DrawIcon(icon, box)) DrawCircle(x + 10, y + 9, 6.0f, fallback);
        UiText(text, x + 25, y, 17, kFrame);
        return x + 25 + UiMeasure(text, 17) + 20;
    }

    // Тайлбарын мөр, икон биш өнгөт тэмдэгтэй (хамтрагч: дугуй, дайсан: ромбо; газрын зураг дээрхтэй ижил)
    int DrawLegendShape(int x, int y, Color color, bool diamond, const char* text)
    {
        const Vector2 c = { (float)x + 10.0f, (float)y + 9.0f };
        if (diamond)
        {
            DrawPoly(c, 4, 8.0f, 0.0f, BLACK);
            DrawPoly(c, 4, 6.5f, 0.0f, color);
        }
        else
        {
            DrawCircleV(c, 7.5f, WHITE);
            DrawCircleV(c, 6.0f, color);
        }
        UiText(text, x + 25, y, 17, kFrame);
        return x + 25 + UiMeasure(text, 17) + 20;
    }
}

// ---------------------------------------------------------------- Жижиг зураг

void SetMapPeople(const std::vector<MapPerson>* people)
{
    mapPeople = people;
}

int MinimapSize()
{
    return InTouchMode() ? kMinimapTouchSize : kMinimapSize;
}

void DrawMinimap(const World& world, const Player& player, float cameraYaw, const ZombieHorde& horde,
                 const SoldierSquad& squad, const Garage& garage, const std::vector<MapMarker>& markers,
                 const std::vector<Vector3>& medKits)
{
    const int side = MinimapSize();
    const float size = (float)side;
    MapView view;
    // Мэдрэгчтэй дэлгэцэд баруун доод буланд тулааны товчлуурууд байдаг тул газрын зураг баруун дээд буланд.
    view.area = { (float)(ViewWidth() - side - kMinimapMargin),
                  InTouchMode() ? (float)kMinimapMargin : (float)(ViewHeight() - side - kMinimapMargin), size, size };
    view.origin = { player.Position().x, player.Position().z };
    view.scale = size * 0.5f / kMinimapRadius;
    view.yaw = cameraYaw;

    // Шил карт дотор: эргэн тойронд 6 px зай, нимгэн хүрээ
    Ui::Panel({ view.area.x - 6.0f, view.area.y - 6.0f, view.area.width + 12.0f, view.area.height + 12.0f }, Ui::kPanel, 12.0f);
    DrawRectangleRec(view.area, kOutside);
    DrawTerrainLayers(view, world, kBackground, kOutside);
    DrawLandmarks(view, world, 1.0f, true);
    DrawVehicles(view, garage, 1.0f);
    DrawShop(view, 1.0f, true);
    DrawObjectives(view, markers, 1.0f);
    DrawMarkers(view, player, horde, squad, true, 1.0f);
    DrawMedKits(view, medKits, 1.0f);
    DrawPlayer(view, player, 1.0f);
    DrawRectangleLinesEx(view.area, 1.0f, Ui::kBorder);

    // Хойд зүгийн (+Z) тэмдэг хүрээн дээр
    const Vector2 center = view.Center();
    const float northX = std::sin(cameraYaw), northY = -std::cos(cameraYaw);
    const float reach = std::max(std::fabs(northX), std::fabs(northY));
    const Vector2 north = { center.x + northX / reach * (size * 0.5f - 9.0f),
                            center.y + northY / reach * (size * 0.5f - 9.0f) };
    // Улаан гурвалжин гадагш заана (шар дугуй дээрх "Х" үсэг даалгаврын шар "X" тэмдэгтэй андуурагддаг байв)
    {
        const Vector2 out = { northX, northY }; // хүрээ рүү
        const Vector2 side = { -northY, northX };
        // Нарийн, урт сум (тэгш талт гурвалжин аль зүг рүү заасан нь ялгагддаггүй)
        const Vector2 tip = { north.x + out.x * 8.0f, north.y + out.y * 8.0f };
        const Vector2 a = { north.x - out.x * 6.0f + side.x * 4.0f, north.y - out.y * 6.0f + side.y * 4.0f };
        const Vector2 b = { north.x - out.x * 6.0f - side.x * 4.0f, north.y - out.y * 6.0f - side.y * 4.0f };
        DrawCircleV(north, 10.0f, { 16, 18, 22, 220 });
        DrawTriangle(tip, a, b, { 235, 70, 60, 255 });
        DrawTriangle(tip, b, a, { 235, 70, 60, 255 });
    }

    // Луужин: зүүн дээд буланд, зүү нь хойд зүг рүү заана (зураг камертай хамт эргэдэг)
    const float compass = 30.0f;
    const Rectangle compassBox = { view.area.x + 4.0f, view.area.y + 4.0f, compass, compass };
    DrawCircleV({ compassBox.x + compass * 0.5f, compassBox.y + compass * 0.5f }, compass * 0.5f, Fade(BLACK, 0.45f));
    Ui::DrawIcon(Ui::Icon::Compass, compassBox, WHITE, cameraYaw * RAD2DEG);
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

    // Зүүн дээд буланд даалгаврын жагсаалт: мөр бүр дээр товшвол тэр даалгавар руу чиглэл заана
    constexpr int kListRows = 7;
    Rectangle ObjectiveList(int rows)
    {
        const Rectangle area = FullMapArea();
        const float width = std::min(360.0f, area.width * 0.42f);
        return { area.x + 10.0f, area.y + 10.0f, width, 36.0f + 40.0f * (float)rows + 8.0f };
    }
    Rectangle ObjectiveRow(int index, int rows)
    {
        const Rectangle list = ObjectiveList(rows);
        return { list.x + 6.0f, list.y + 36.0f + 40.0f * (float)index, list.width - 12.0f, 36.0f };
    }
}

float FullMap::BaseScale(const World& world) const
{
    // 100%: аймгийн хил бүтнээрээ, захаасаа бага зэрэг зайтай багтана.
    const Rectangle area = FullMapArea();
    const Rectangle bounds = world.Boundary().Bounds();
    return 0.94f * std::min(area.width / std::max(1.0f, bounds.width), area.height / std::max(1.0f, bounds.height));
}

void FullMap::Open(const World& world, const Player& player)
{
    // Тоглогчийн эргэн тойрон ойроос: дэлгэцийн өргөнд ~kOpenSpan метр багтана
    open = true;
    SetZoom(FullMapArea().width / (kOpenSpan * BaseScale(world)));
    focus = { player.Position().x, player.Position().z };
}

bool FullMap::TakeWaypoint(Vector2& point, bool& clear)
{
    clear = clearWaypoint;
    point = waypoint;
    const bool any = hasWaypoint || clearWaypoint;
    hasWaypoint = clearWaypoint = false;
    return any;
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
    const float pinch = InPinch();
    if (wheel != 0.0f || pinch != 1.0f)
    {
        // Хулганы (чимхэлтийн голын) доорх цэг байрандаа үлдэхээр томруулна.
        const Vector2 at = pinch != 1.0f ? InPinchCenter() : mouse;
        const float before = BaseScale(world) * zoom;
        const Vector2 anchor = { focus.x - (at.x - center.x) / before, focus.y - (at.y - center.y) / before };
        SetZoom(zoom * std::pow(1.2f, wheel) * pinch);
        const float after = BaseScale(world) * zoom;
        focus = { anchor.x + (at.x - center.x) / after, anchor.y + (at.y - center.y) / after };
    }
    if (InPinching()) dragged += 100.0f; // чимхэлт дууссаны дараа очих газар тэмдэглэгдэхгүй

    if (InMouseDown(MOUSE_BUTTON_LEFT))
    {
        const Vector2 delta = InLookDelta();
        const float scale = BaseScale(world) * zoom;
        focus.x += delta.x / scale;
        focus.y += delta.y / scale;
        if (!pressing)
        {
            pressing = true;
            pressAt = mouse;
            dragged = 0.0f;
        }
        dragged += std::fabs(delta.x) + std::fabs(delta.y);
    }
    else if (pressing)
    {
        // Чирэлгүй товшилт: очих газар тэмдэглэнэ (одоо байгаа тэмдэглэгээн дээр товшвол арилна)
        pressing = false;
        const int rows = std::min((int)markers.size(), kListRows);
        int row = -1;
        for (int i = 0; i < rows && dragged < 8.0f; ++i)
        {
            if (CheckCollisionPointRec(pressAt, ObjectiveRow(i, rows))) row = i;
        }
        // Газрын зураг дээрх даалгаврын тэмдэг (товшилтын 22 пикселийн дотор)
        int picked = row;
        if (picked < 0 && dragged < 8.0f && CheckCollisionPointRec(pressAt, area) &&
            !(rows > 0 && CheckCollisionPointRec(pressAt, ObjectiveList(rows))))
        {
            const float scale = BaseScale(world) * zoom;
            float best = 22.0f;
            for (int i = 0; i < (int)markers.size(); ++i)
            {
                const Vector2 p = { center.x - (markers[(size_t)i].position.x - focus.x) * scale,
                                    center.y - (markers[(size_t)i].position.z - focus.y) * scale };
                const float d = Vector2Distance(p, pressAt);
                if (d < best)
                {
                    best = d;
                    picked = i;
                }
            }
        }
        if (picked >= 0)
        {
            // Даалгавар: аль хэдийн чиглэл заасан бол арилгана, үгүй бол яг байршил руу чиглэл
            const Vector2 target = { markers[(size_t)picked].position.x, markers[(size_t)picked].position.z };
            if (navigation && navigation->Active() && Vector2Distance(target, navigation->Target()) < 3.0f) clearWaypoint = true;
            else
            {
                hasWaypoint = true;
                waypoint = target;
            }
        }
        else if (dragged < 8.0f && CheckCollisionPointRec(pressAt, area) && !(rows > 0 && CheckCollisionPointRec(pressAt, ObjectiveList(rows))))
        {
            const float scale = BaseScale(world) * zoom;
            const Vector2 point = { focus.x - (pressAt.x - center.x) / scale, focus.y - (pressAt.y - center.y) / scale };
            if (navigation && navigation->Active() && Vector2Distance(point, navigation->Target()) * scale < 14.0f)
            {
                clearWaypoint = true;
            }
            else if (world.InsideBoundary(point.x, point.y))
            {
                hasWaypoint = true;
                waypoint = point;
            }
        }
    }
    if (InMousePressed(MOUSE_BUTTON_RIGHT) || InKeyPressed(KEY_X)) clearWaypoint = true;

    if (InKeyPressed(KEY_C))
    {
        focus = { player.Position().x, player.Position().z };
    }

    const Rectangle bounds = world.Boundary().Bounds();
    focus.x = Clamp(focus.x, bounds.x, bounds.x + bounds.width);
    focus.y = Clamp(focus.y, bounds.y, bounds.y + bounds.height);
}

void FullMap::Draw(const World& world, const Player& player, const ZombieHorde& horde, const SoldierSquad& squad,
                   const Garage& garage, const std::vector<Vector3>& medKits, const char* title) const
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
    DrawLandmarks(view, world, 1.4f, false); // заставыг доор нь нэрийнх нь хажууд зурна

    // Газрын нэрс
    BeginScissorMode((int)view.area.x, (int)view.area.y, (int)view.area.width, (int)view.area.height);
    DrawShop(view, 1.5f, false);
    struct Place { float x, z; const char* name; };
    const CityData& cityData = CurrentCity();
    const Vector2 townSite = CityTownSite(cityData), gasSite = CityGasSite(cityData), storeSite = CityWarehouseSite(cityData);
    static const Place capitalPlaces[] = {
        { 0.0f, 60.0f, "СҮХБААТАРЫН ТАЛБАЙ" }, { 96.0f, -1857.0f, "ТӨВ ЦЭНГЭЛДЭХ" }, { 166.0f, 221.0f, "ҮНДЭСНИЙ МУЗЕЙ" },
        { 590.0f, 147.0f, "ЗАНАБАЗАРЫН МУЗЕЙ" }, { 793.0f, -2385.0f, "БОГД ХААНЫ ОРДОН" }, { -360.0f, -974.0f, "ҮНДЭСНИЙ ЦЭЦЭРЛЭГТ ХҮРЭЭЛЭН" },
    };
    bool isCity = false;
    static std::string townLabel;
    townLabel = TownName(cityData.province, isCity);
    std::vector<Place> cityPlaces = {
        { kOutpost.x, kOutpost.y, "ЗАСТАВ" }, { kShopCenter.x, kShopCenter.y + 5.5f, "ДЭЛГҮҮР" },
        { townSite.x, townSite.y - 40.0f, cityData.province == kUlaanbaatarProvince ? "ЧОЙЖИН ЛАМЫН СҮМ" : townLabel.c_str() },
        { gasSite.x, gasSite.y - 14.0f, "ШТС" },
    };
    if (storeSite.x != townSite.x || storeSite.y != townSite.y) cityPlaces.push_back({ storeSite.x - 17.0f, storeSite.y + 10.0f, "АГУУЛАХ" });
    if (cityData.province == kUlaanbaatarProvince) cityPlaces.insert(cityPlaces.end(), std::begin(capitalPlaces), std::end(capitalPlaces));
    for (size_t i = 0; i < cityData.mountains.size(); ++i)
    {
        cityPlaces.push_back({ cityData.mountains[i].peak.x, cityData.mountains[i].peak.y, cityData.mountainNames[i].c_str() });
    }
    const Place places[] = {
        { 28.0f, 34.0f, "СУУРИН" },  { kOutpost.x, kOutpost.y, "ЗАСТАВ" }, { 38.0f, -13.0f, "ШАТАХУУН ТҮГЭЭХ ГАЗАР" },
        { 10.0f, 70.0f, "АГУУЛАХ" }, { kTownCenter.x, kTownCenter.y, "АЙМГИЙН ТӨВ" }, { kShopCenter.x, kShopCenter.y + 5.5f, "ДЭЛГҮҮР" },
        { airport::kTerminal.x, airport::kTerminal.y - 60.0f, world.GetLayout().AirfieldActive() ? "БУЯНТ-УХАА НИСЭХ БУУДАЛ" : nullptr },
    };
    const bool city = world.CityActive();
    std::vector<Place> shown = city ? cityPlaces : std::vector<Place>(std::begin(places), std::end(places));
    if (city)
    {
        bool first = true; // эхнийх нь (даалгаврын ШТС) cityPlaces-д бий
        for (const CityPoi& poi : cityData.pois)
        {
            if (poi.kind != CityPoiKind::Station) continue;
            if (!first) shown.push_back({ poi.position.x + 31.0f, poi.position.y - 35.0f, "ШТС" });
            first = false;
        }
    }
    for (const Place& place : shown)
    {
        if (place.name == nullptr) continue;
        const Vector2 p = view.ToScreen(place.x, place.z);
        const bool shop = place.x == kShopCenter.x && place.z == kShopCenter.y + 5.5f;
        DrawPlaceLabel(p, place.name, shop ? Color{ 255, 205, 90, 255 } : kFrame);
        if (place.x == kOutpost.x && place.z == kOutpost.y)
        {
            // Застав (бааз): шошгын зүүн талд икон
            const float textWidth = (float)Ui::MeasureBold(place.name, 15);
            Ui::DrawIcon(Ui::Icon::MapBase, { p.x - textWidth * 0.5f - 34.0f, p.y - 11.0f, 22.0f, 22.0f });
        }
    }
    if (city)
    {
        // ODbL: Улаанбаатарын гудамж, барилга OpenStreetMap-ийн өгөгдлөөр
        UiText("Газрын зураг: (c) OpenStreetMap contributors (ODbL)  ·  Өндөр: AWS Terrain Tiles (SRTM, Mapzen)", (int)view.area.x + 10,
               (int)(view.area.y + view.area.height) - 22, 14, Fade(kFrame, 0.75f));
    }
    else if (world.GetLayout().AirfieldActive())
    {
        // ODbL: нисэх буудлын зураг OpenStreetMap-ийн өгөгдлөөр
        UiText("Нисэх буудлын зураг: (c) OpenStreetMap contributors", (int)view.area.x + 10,
               (int)(view.area.y + view.area.height) - 22, 14, Fade(kFrame, 0.75f));
    }
    EndScissorMode();

    DrawVehicles(view, garage, 1.3f);
    const Vector3 from = player.Position();
    const Vector2 target = navigation && navigation->Active() ? navigation->Target() : Vector2{ 1e9f, 1e9f };
    DrawMarkers(view, player, horde, squad, true, 1.5f);
    DrawMedKits(view, medKits, 1.5f);
    DrawPlayer(view, player, 1.5f);
    DrawObjectives(view, markers, 1.4f, &from, &target);
    DrawRectangleLinesEx(view.area, 1.0f, Ui::kBorder);

    // Зүүн дээд: даалгаврын жагсаалт (товшвол чиглэл заана)
    const int rows = std::min((int)markers.size(), kListRows);
    if (rows > 0)
    {
        const Rectangle list = ObjectiveList(rows);
        Ui::Fill(list, { 20, 30, 43, 225 });
        Ui::Stroke(list, 1.0f, Fade(kFrame, 0.3f));
        Ui::TextBold("ДААЛГАВРУУД", (int)list.x + 14, (int)list.y + 10, 15, kObjective);
        const char* tip = "дарж чиглэл авах";
        Ui::Text(tip, (int)(list.x + list.width) - 14 - Ui::Measure(tip, 13), (int)list.y + 12, 13, Fade(kFrame, 0.6f));
        const Vector2 mouse = InPointer();
        for (int i = 0; i < rows; ++i)
        {
            const MapMarker& marker = markers[(size_t)i];
            const Rectangle r = ObjectiveRow(i, rows);
            const bool chosen = Vector2Distance(target, { marker.position.x, marker.position.z }) < 3.0f;
            const bool hover = !InTouchMode() && CheckCollisionPointRec(mouse, r);
            if (chosen || hover || marker.focus)
            {
                Ui::Fill(r, Fade(MarkerColor(marker.kind), chosen ? 0.28f : hover ? 0.16f : 0.08f));
            }
            if (chosen) Ui::Stroke(r, 1.5f, WHITE);
            DrawMarkerBadge(marker.kind, { r.x + 18.0f, r.y + r.height * 0.5f }, 13.0f);
            const float meters = Vector2Distance({ from.x, from.z }, { marker.position.x, marker.position.z });
            const char* right = DistanceText(meters);
            const int rightWidth = Ui::MeasureBold(right, 15);
            Ui::TextBold(right, (int)(r.x + r.width) - 10 - rightWidth, (int)r.y + 10, 15, chosen ? WHITE : Fade(kFrame, 0.8f));
            // Нэр: багтахгүй бол таслана
            std::string name = marker.objective >= 0 ? TextFormat("%d. %s", marker.objective + 1, marker.title) : marker.title;
            const int maxWidth = (int)r.width - 44 - rightWidth - 16;
            while (Ui::MeasureBold(name.c_str(), 16) > maxWidth && name.size() > 4)
            {
                if (name.size() >= 3 && name.compare(name.size() - 3, 3, "\xE2\x80\xA6") == 0) name.erase(name.size() - 3);
                name.pop_back();
                while (!name.empty() && ((unsigned char)name.back() & 0xC0) == 0x80) name.pop_back();
                if (!name.empty() && ((unsigned char)name.back() & 0xC0) == 0xC0) name.pop_back(); // (олон байтын эхлэл)
                name += "\xE2\x80\xA6";
            }
            Ui::TextBold(name.c_str(), (int)r.x + 40, (int)r.y + 9, 16, marker.objective >= 0 ? Ui::kText : MarkerColor(marker.kind));
        }
    }

    // Хойд зүг: газрын зургийн баруун дээд буланд луужин ("Х" үсэг ганцаараа ойлгомжгүй байв)
    {
        const Vector2 c = { view.area.x + view.area.width - 34.0f, view.area.y + 34.0f };
        DrawCircleV(c, 24.0f, { 16, 18, 22, 215 });
        DrawRing(c, 22.0f, 24.0f, 0.0f, 360.0f, 32, Fade(kFrame, 0.35f));
        DrawTriangle({ c.x, c.y - 17.0f }, { c.x - 6.0f, c.y }, { c.x + 6.0f, c.y }, { 230, 70, 60, 255 });
        DrawTriangle({ c.x, c.y + 17.0f }, { c.x + 6.0f, c.y }, { c.x - 6.0f, c.y }, Fade(kFrame, 0.8f));
        const int w = Ui::MeasureBold("ХОЙД", 12);
        Ui::TextBold("ХОЙД", (int)c.x - w / 2, (int)c.y + 26, 12, kFrame);
    }

    // Гарчиг, томруулалт, удирдлага (дээд мөрөнд), тайлбар (доод мөрөнд)
    Ui::TextDisplay("ГАЗРЫН ЗУРАГ", (int)view.area.x, 18, 28, Ui::kText, 0.08f);
    Ui::Text(title, (int)view.area.x + Ui::MeasureDisplay("ГАЗРЫН ЗУРАГ", 28, 0.08f) + 16, 26, 20, Ui::kAccent);
    const char* zoomText = TextFormat("%d%%", (int)std::round(zoom * 100.0f));
    Ui::Chip(zoomText, width - (int)view.area.x, 18, Ui::kText, Ui::kInfo, true);
    const char* help = InTouchMode() ? "Даалгавар дээр товш: чиглэл   Чир: зөөх   Чимх: томруулах"
                                     : "Даалгавар дээр дар: чиглэл   X: арилгах   Дугуй: томруулах   Чир: зөөх   C: өөр дээрээ   M: хаах";
    const int zoomWidth = UiMeasure(zoomText, 18) + 40;
    UiText(help, width - (int)view.area.x - zoomWidth - 16 - UiMeasure(help, 15), 28, 15, Fade(kFrame, 0.7f));

    // Тайлбар: газрын зураг дээрх яг тэр дүрсүүд (өнгөт цэг биш); байгаа зүйлсээ л
    bool enemies = false, bears = false, horses = false, towers = false;
    if (mapPeople)
    {
        for (const MapPerson& person : *mapPeople) enemies = enemies || person.enemy;
    }
    for (int i = 0; i < horde.Count(); ++i) bears = bears || (horde.At(i).IsAlive() && horde.At(i).IsBear());
    for (int i = 0; i < garage.Count(); ++i) horses = horses || garage.At(i).type == VehicleType::Horse;
    for (int i = 0; i < world.PassageCount(); ++i) towers = towers || world.GetPassage(i).kind == PassageKind::LadderUp;

    const int legendY = height - 36;
    const Rectangle bar = { view.area.x, (float)legendY - 9.0f, view.area.width, 36.0f };
    Ui::Fill(bar, { 16, 18, 22, 200 });
    int x = (int)view.area.x + 14;
    {
        // Та: тоглогчийн тэмдэг (газрын зураг дээрхтэй ижил сум)
        DrawPlayerBadge({ (float)x + 10.0f, (float)legendY + 9.0f }, 10.0f, 0.0f, -1.0f);
        UiText("Та", x + 25, legendY, 17, kFrame);
        x += 25 + UiMeasure("Та", 17) + 20;
    }
    {
        // Даалгавар: төрлийн тэмдэг (туг, хүн, хайрцаг ...); дарвал чиглэл заана
        DrawMarkerBadge(MarkerKind::Reach, { (float)x + 10.0f, (float)legendY + 9.0f }, 10.0f);
        UiText("Даалгавар", x + 25, legendY, 17, kFrame);
        x += 25 + UiMeasure("Даалгавар", 17) + 20;
    }
    x = DrawLegendItem(x, legendY, Ui::Icon::MapZombie, kZombie, "Зомби");
    x = DrawLegendItem(x, legendY, Ui::Icon::MapElite, kZombie, "Том зомби");
    if (bears)
    {
        DrawBearBadge({ (float)x + 10.0f, (float)legendY + 10.0f }, 6.0f, false);
        UiText("Баавгай", x + 25, legendY, 17, kFrame);
        x += 25 + UiMeasure("Баавгай", 17) + 20;
    }
    if (mapPeople) x = DrawLegendShape(x, legendY, kPartner, false, "Хамтрагч");
    x = DrawLegendItem(x, legendY, Ui::Icon::MapAlly, kSoldier, "Цэрэг");
    if (enemies) x = DrawLegendShape(x, legendY, kEnemy, true, "Дайсан");
    x = DrawLegendItem(x, legendY, Ui::Icon::MapVehicle, kVehicle, "Машин");
    if (horses) x = DrawLegendItem(x, legendY, Ui::Icon::MapHorse, kVehicle, "Морь");
    x = DrawLegendItem(x, legendY, Ui::Icon::MapSupply, kMedKit, "Эм");
    DrawShopBadge({ (float)x + 10.0f, (float)legendY + 9.0f }, 8.0f);
    UiText("Дэлгүүр", x + 25, legendY, 17, kFrame);
    x += 25 + UiMeasure("Дэлгүүр", 17) + 20;
    if (towers) x = DrawLegendItem(x, legendY, Ui::Icon::MapTower, kFrame, "Цамхаг");
}
