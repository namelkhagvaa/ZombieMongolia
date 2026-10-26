#include "Structures.h"

#include "AirportData.h"
#include "City.h"
#include "Polygon.h"

#include "Procedural.h"
#include "Layout.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
    // --- Өнгөний палитр: бүдгэрсэн, тоос шороонд дарагдсан ---
    const Color kAsphalt = { 60, 60, 64, 255 };
    const Color kAsphaltPatch = { 78, 76, 76, 255 };
    const Color kGravel = { 124, 112, 94, 255 };
    const Color kRoadPaint = { 196, 190, 160, 255 };
    const Color kDirtRoad = { 138, 114, 84, 255 };
    const Color kDirtRut = { 108, 88, 66, 255 };
    const Color kPothole = { 92, 78, 62, 255 };

    const Color kWood = { 130, 106, 80, 255 };
    const Color kWoodGrey = { 124, 118, 108, 255 };
    const Color kConcrete = { 150, 148, 140, 255 };
    const Color kBrick = { 138, 78, 62, 255 };
    const Color kWindow = { 34, 40, 48, 255 };
    const Color kWindowHole = { 14, 15, 17, 255 }; // шилгүй цонхны нүх (цул барилга: дотор нь харанхуй)
    const Color kWindowFrame = { 206, 200, 186, 255 };
    const Color kDoor = { 96, 66, 46, 255 };
    const Color kRust = { 128, 76, 50, 255 };
    const Color kMetal = { 110, 114, 118, 255 };
    const Color kRubber = { 28, 28, 30, 255 };
    const Color kGlassDark = { 40, 50, 58, 255 };
    const Color kGerFelt = { 226, 220, 204, 255 };
    const Color kGerRoof = { 204, 196, 176, 255 };
    const Color kGerTrim = { 46, 78, 140, 255 };      // дээврийн хаяавчийн хөх хээ
    const Color kGerTrimLight = { 214, 206, 186, 255 };
    const Color kGerRope = { 150, 124, 92, 255 };
    const Color kGerDoor = { 186, 96, 40, 255 };
    const Color kSandbag = { 150, 136, 100, 255 };
    const Color kOlive = { 84, 92, 60, 255 };
    const Color kCanvas = { 104, 106, 76, 255 };
    const Color kShadow = { 10, 12, 22, 96 };
    const Color kChainLink = { 170, 176, 180, 70 };
    const Color kBoulder = { 122, 118, 110, 255 };

    // Сүүдрийн хэвтээ шилжилт, 1 м өндөр тутамд (нарны чиглэлээс).
    const float kShadowX = -kSunDirection.x / kSunDirection.y;
    const float kShadowZ = -kSunDirection.z / kSunDirection.y;

    // Газар дээрх давхаргуудын өндөр (хоорондоо анивчихгүйн тулд бага зэрэг зөрүүтэй).
    const float kYShoulder = 0.03f;
    const float kYDirt = 0.045f;
    const float kYAsphalt = 0.06f;
    const float kYMarking = 0.085f;
    const float kYShadow = 0.11f;

    // Барилга, машиныг хүний биетэй (толгойн орой ~1.95 м) харьцаатай болгох үржүүлэгчид.
    // Байрлуулах код хуучин жижиг хэмжээгээр бичигдсэн тул хэмжээг барих функц дотор томруулна.
    const float kHouseScale = 1.25f;
    const float kGerScale = 1.3f;
    const float kCarScale = 1.18f;

    // Аймгийн бүс нутаг: байшин, шатахуун түгээх газар, заставын хэв маягийг тодорхойлно.
    enum class Region
    {
        West,   // баруун аймгууд: өнгөлөг будаг, хээтэй тууз
        Gobi,   // говь: хавтгай дээвэртэй шавар, тоосгон байшин
        Forest, // хойд ойт аймгууд: дүнзэн байшин, эгц дээвэр
        City,   // аж үйлдвэрийн хот: саарал, хоёр давхар
        East    // зүүн тал нутаг
    };

    // Аймгийн дугаар (баруунаас зүүн тийш, CampaignData.cpp-ийн дараалал) -> бүс
    Region RegionFor(int variant)
    {
        switch (variant)
        {
        case 0: case 1: case 2: case 3: return Region::West;      // Баян-Өлгий, Увс, Ховд, Завхан
        case 4: case 6: case 9: case 11: case 16: case 17: case 18: return Region::Gobi; // Говь-Алтай .. Дорноговь
        case 5: case 7: case 8: case 12: case 19: return Region::Forest; // Хөвсгөл, Архангай, Булган, Сэлэнгэ, Хэнтий
        case 10: case 13: case 14: case 15: return Region::City;  // Орхон, Дархан-Уул, Төв, Улаанбаатар
        default: return Region::East;                            // Сүхбаатар, Дорнод
        }
    }

    struct Scene
    {
        const Layout& layout;
        const Terrain& terrain;
        std::vector<Obstacle>& obstacles;
        MeshBuilder& solid;
        MeshBuilder& shadows;
        MeshBuilder& glass;
        std::vector<Interior>& interiors;
        std::vector<Door>& doors;
        std::vector<Passage>& passages;
        std::vector<WallSegment>& walls;
        std::vector<FencePanel>& panels;
        Rng rng{ 20240u };
        int variant = 0;               // аймгийн дугаар
        Region region = Region::West;  // аймгийн бүс нутаг

        void Collider(Vector3 center, Vector3 size, bool blocksRays = true, bool walkable = false)
        {
            obstacles.push_back({ center, size, blocksRays, walkable });
        }

        // Нурдаг хашааны хэсэг: BeginPanel ба EndPanel хооронд solid-д нэмсэн гурвалжнууд тухайн хэсгийнх.
        size_t panelFirst = 0;
        void BeginPanel() { panelFirst = (size_t)solid.TriangleCount(); }
        void EndPanel(Vector2 a, Vector2 b, float height, Color color, float breakSpeed, int wall, int obstacle)
        {
            FencePanel panel;
            panel.a = a;
            panel.b = b;
            panel.height = height;
            panel.color = color;
            panel.breakSpeed = breakSpeed;
            panel.wall = wall;
            panel.obstacle = obstacle;
            panel.first = panelFirst;
            panel.count = (size_t)solid.TriangleCount() - panelFirst;
            if (panel.count > 0) panels.push_back(panel);
        }

        // Нээж хаадаг хаалга ба түүний ард байгаа дотор талыг бүртгэнэ.
        void Entrance(Door door, Interior interior)
        {
            interior.door = (int)doors.size();
            doors.push_back(door);
            interiors.push_back(interior);
        }

        // [x0,x1] x [z0,z1] суурьтай, hLow..hHigh өндөрт орших биеийн газар дээрх сүүдэр.
        // Нар -X, -Z талд байгаа тул сүүдэр +X, +Z руу сунасан зургаан өнцөгт болно.
        void Shadow(float x0, float z0, float x1, float z1, float hLow, float hHigh)
        {
            const float ox = hLow * kShadowX, oz = hLow * kShadowZ;
            const float dx = (hHigh - hLow) * kShadowX, dz = (hHigh - hLow) * kShadowZ;
            x0 += ox; x1 += ox; z0 += oz; z1 += oz;

            const Vector2 outline[6] = {
                { x0, z0 }, { x1, z0 }, { x1 + dx, z0 + dz }, { x1 + dx, z1 + dz }, { x0 + dx, z1 + dz }, { x0, z1 },
            };
            Vector3 points[6];
            for (int i = 0; i < 6; ++i)
            {
                points[i] = { outline[i].x, terrain.HeightAt(outline[i].x, outline[i].y) + kYShadow, outline[i].y };
            }
            for (int i = 1; i < 5; ++i)
            {
                shadows.Triangle(points[0], points[i], points[i + 1], kShadow);
            }
        }

        // Хайрцаг + мөргөлдөөн + сүүдэр. center.y нь хайрцгийн төв.
        void Block(Vector3 center, Vector3 size, Color color)
        {
            solid.Box(center, size, color);
            Collider(center, size);
            Shadow(center.x - size.x * 0.5f, center.z - size.z * 0.5f, center.x + size.x * 0.5f,
                   center.z + size.z * 0.5f, 0.0f, center.y + size.y * 0.5f);
        }
    };

    // Тэгш бүсэд (газрын жинхэнэ өндөртэй хотод level өндөрт) байгууламж барина: байгууламжийн код газрыг 0
    // түвшинд гэж үздэг тул MeshBuilder-ийн шилжилт, газрын bias-аар өргөж, мөргөлдөөн, хаалга, гарцыг шилжүүлнэ.
    template <typename Build>
    void AtLevel(Scene& s, float level, Build build)
    {
        if (level == 0.0f)
        {
            build();
            return;
        }
        const size_t obstacles = s.obstacles.size(), passages = s.passages.size();
        const size_t doors = s.doors.size(), interiors = s.interiors.size();
        const Vector3 lift = { 0.0f, level, 0.0f }, none = { 0.0f, 0.0f, 0.0f };
        s.solid.SetOffset(lift);
        s.shadows.SetOffset(lift);
        s.glass.SetOffset(lift);
        s.terrain.SetBuildBias(level);
        build();
        s.terrain.SetBuildBias(0.0f);
        s.solid.SetOffset(none);
        s.shadows.SetOffset(none);
        s.glass.SetOffset(none);
        for (size_t i = obstacles; i < s.obstacles.size(); ++i) s.obstacles[i].center.y += level;
        for (size_t i = passages; i < s.passages.size(); ++i)
        {
            s.passages[i].from.y += level;
            s.passages[i].to.y += level;
        }
        for (size_t i = doors; i < s.doors.size(); ++i) s.doors[i].hinge.y += level;
        for (size_t i = interiors; i < s.interiors.size(); ++i) s.interiors[i].upperFloor += level;
    }

    // ------------------------------------------------------------------ Зам

    std::vector<Vector2> Resample(const std::vector<Vector2>& points, float spacing)
    {
        std::vector<Vector2> result;
        for (size_t i = 0; i + 1 < points.size(); ++i)
        {
            const Vector2 a = points[i];
            const Vector2 b = points[i + 1];
            const float length = Vector2Distance(a, b);
            const int steps = std::max(1, (int)std::ceil(length / spacing));
            for (int s = 0; s < steps; ++s)
            {
                result.push_back(Vector2Lerp(a, b, (float)s / (float)steps));
            }
        }
        result.push_back(points.back());
        return result;
    }

    void BuildRoad(Scene& s, const Road& road)
    {
        const std::vector<Vector2> path = Resample(road.points, 3.0f);
        const int count = (int)path.size();
        const bool asphalt = road.kind == RoadKind::Asphalt;

        // Цэг бүрийн хажуу тийш чиглэсэн нэгж вектор
        std::vector<Vector2> side(count);
        for (int i = 0; i < count; ++i)
        {
            const Vector2 tangent = Vector2Normalize(Vector2Subtract(path[std::min(i + 1, count - 1)], path[std::max(i - 1, 0)]));
            side[i] = { -tangent.y, tangent.x };
        }

        // Тэнхлэгээс offset зайд, газраас lift өндөрт орших цэг
        auto point = [&](int i, float offset, float lift) {
            const float x = path[i].x + side[i].x * offset;
            const float z = path[i].y + side[i].y * offset;
            return Vector3{ x, s.terrain.HeightAt(x, z) + lift, z };
        };
        auto strip = [&](int i, float from, float to, float lift, Color color) {
            s.solid.Quad(point(i, from, lift), point(i + 1, from, lift), point(i + 1, to, lift), point(i, to, lift), color);
        };

        const float hw = road.halfWidth;
        for (int i = 0; i + 1 < count; ++i)
        {
            if (asphalt)
            {
                strip(i, -hw - 0.9f, hw + 0.9f, kYShoulder, JitterColor(kGravel, s.rng, 0.06f));
                const Color surface = s.rng.Chance(0.14f) ? kAsphaltPatch : kAsphalt; // нөхөөс
                strip(i, -hw, hw, kYAsphalt, JitterColor(surface, s.rng, 0.07f));

                // Тасархай голын зураас: зарим нь арилсан
                if (i % 3 == 0 && !s.rng.Chance(0.3f))
                {
                    strip(i, -0.08f, 0.08f, kYMarking, JitterColor(kRoadPaint, s.rng, 0.1f));
                }

                // Нүх: асфальт хуулраад шороо гарсан
                if (s.rng.Chance(0.22f))
                {
                    const float offset = s.rng.Range(-hw + 0.9f, hw - 0.9f);
                    const float radius = s.rng.Range(0.35f, 0.9f);
                    const Vector3 c = point(i, offset, kYMarking);
                    s.solid.Quad({ c.x - radius, c.y, c.z - radius * s.rng.Range(0.3f, 1.0f) },
                                 { c.x - radius * s.rng.Range(0.2f, 0.9f), c.y, c.z + radius },
                                 { c.x + radius, c.y, c.z + radius * s.rng.Range(0.3f, 1.0f) },
                                 { c.x + radius * s.rng.Range(0.2f, 0.9f), c.y, c.z - radius },
                                 JitterColor(kPothole, s.rng, 0.1f));
                }

                // Хагарал: нарийн бараан зураас
                if (s.rng.Chance(0.3f))
                {
                    const float a = s.rng.Range(-hw + 0.3f, hw - 0.3f);
                    const float b = std::clamp(a + s.rng.Range(-2.5f, 2.5f), -hw + 0.1f, hw - 0.1f);
                    s.solid.Quad(point(i, a, kYMarking), point(i, a + 0.07f, kYMarking), point(i + 1, b + 0.07f, kYMarking),
                                 point(i + 1, b, kYMarking), ScaleColor(kAsphalt, 0.5f));
                }
            }
            else
            {
                strip(i, -hw, hw, kYDirt, JitterColor(kDirtRoad, s.rng, 0.08f));
                // Дугуйн мөр
                strip(i, -1.05f, -0.65f, kYAsphalt, JitterColor(kDirtRut, s.rng, 0.06f));
                strip(i, 0.65f, 1.05f, kYAsphalt, JitterColor(kDirtRut, s.rng, 0.06f));
            }
        }
    }

    // ------------------------------------------------------------------ Байшин, гэр

    // Барилгын нүүрэн дээрх нимгэн хавтан (хаалга, цонх). face: 0 = +X, 1 = -X, 2 = +Z, 3 = -Z.
    void FacePanel(Scene& s, float cx, float cz, float w, float d, int face, float along, float y, float panelWidth,
                   float panelHeight, float thickness, Color color)
    {
        Vector3 center, size;
        switch (face)
        {
        case 0: center = { cx + w * 0.5f, y, cz + along }; size = { thickness, panelHeight, panelWidth }; break;
        case 1: center = { cx - w * 0.5f, y, cz + along }; size = { thickness, panelHeight, panelWidth }; break;
        case 2: center = { cx + along, y, cz + d * 0.5f }; size = { panelWidth, panelHeight, thickness }; break;
        default: center = { cx + along, y, cz - d * 0.5f }; size = { panelWidth, panelHeight, thickness }; break;
        }
        s.solid.Box(center, size, color);
    }

    void Window(Scene& s, float cx, float cz, float w, float d, int face, float along, float y, float width, float height)
    {
        FacePanel(s, cx, cz, w, d, face, along, y, width + 0.18f, height + 0.18f, 0.08f, kWindowFrame);
        // Зарим цонхыг банзаар хадсан
        const Color pane = s.rng.Chance(0.3f) ? JitterColor(kWood, s.rng, 0.1f) : kWindow;
        FacePanel(s, cx, cz, w, d, face, along, y, width, height, 0.14f, pane);
    }

    // Барилгын суурийн тэмдэглэгээ: газар доорх хавтгай "саад". Хэнийг ч хаахгүй (газраас доош), харин
    // дотор нь өвс ургуулахгүй, газрын зураг дээр барилгын суурийг харуулна.
    void Footprint(Scene& s, float cx, float cz, float w, float d)
    {
        s.Collider({ cx, -0.75f, cz }, { w, 0.5f, d });
    }

    // Тавилга: намхан хайрцаг + мөргөлдөөн (сүүдэргүй, барилга дотор).
    void Furniture(Scene& s, Vector3 center, Vector3 size, Color color)
    {
        s.solid.Box(center, size, color);
        s.Collider(center, size);
    }

    // Дотогш орж болох байшин: дөрвөн хана тус тусдаа саад, doorFace талд хаалганы нээлхийтэй.
    // Байшингийн загвар: үе (аймаг) бүрд, байшин бүрд өөр.
    struct HouseStyle
    {
        Color wall = { 196, 184, 160, 255 };
        Color roof = { 70, 110, 84, 255 };
        bool flatRoof = false;   // хавтгай дээвэр, хашлагатай (тоосгон байшин)
        float roofHeight = 1.9f; // налуу дээврийн өндөр
        bool chimney = true;
        bool porch = false;      // хаалганы дээрх саравч, хоёр баганатай
        int floors = 1;          // 2 бол дээд давхартай (дээд давхар нь цул, дотогш орохгүй)
        bool logs = false;       // дүнзэн байшин: булангаар нь дүнзний үзүүр цухуйна
        bool ornament = false;   // ханын дээд хэсгээр хээтэй өнгөт тууз (баруун аймгууд)
        Color trim = { 46, 78, 140, 255 }; // туузны өнгө
    };

    HouseStyle RandomHouseStyle(Rng& rng);

    // Аймгийн бүсэд тохирсон байшин: өнгө, дээвэр, давхар, онцлог чимэглэл.
    HouseStyle RegionHouseStyle(Scene& s)
    {
        Rng& rng = s.rng;
        HouseStyle style = RandomHouseStyle(rng);
        auto pick = [&](std::initializer_list<Color> colors) {
            const int index = rng.Int(0, (int)colors.size() - 1);
            return *(colors.begin() + index);
        };
        switch (s.region)
        {
        case Region::West:
            // Цагаан, цайвар хөх, ногоон хана; хөх, ногоон, улаан дээвэр; хээтэй тууз
            style.wall = pick({ { 222, 220, 210, 255 }, { 150, 190, 210, 255 }, { 170, 206, 180, 255 }, { 226, 206, 150, 255 } });
            style.roof = pick({ { 46, 96, 160, 255 }, { 52, 130, 96, 255 }, { 170, 56, 46, 255 } });
            style.ornament = true;
            style.trim = pick({ { 40, 80, 150, 255 }, { 170, 50, 46, 255 }, { 36, 120, 110, 255 } });
            style.flatRoof = rng.Chance(0.12f);
            break;
        case Region::Gobi:
            // Шавар, элсэн өнгө; ихэнх нь хавтгай дээвэр, намхан
            style.wall = pick({ { 206, 180, 140, 255 }, { 190, 160, 118, 255 }, { 214, 196, 160, 255 }, { 176, 132, 96, 255 } });
            style.flatRoof = rng.Chance(0.7f);
            style.roof = style.flatRoof ? Color{ 150, 132, 104, 255 } : pick({ { 150, 110, 50, 255 }, { 120, 84, 60, 255 } });
            style.roofHeight = rng.Range(1.0f, 1.6f);
            style.floors = rng.Chance(0.12f) ? 2 : 1;
            style.porch = rng.Chance(0.55f); // нарнаас хамгаалах саравч
            break;
        case Region::Forest:
            // Дүнзэн байшин: бор мод, эгц ногоон, улаан дээвэр
            style.wall = pick({ { 132, 92, 58, 255 }, { 118, 82, 52, 255 }, { 150, 108, 70, 255 } });
            style.roof = pick({ { 52, 104, 66, 255 }, { 150, 56, 44, 255 }, { 70, 70, 76, 255 } });
            style.flatRoof = false;
            style.roofHeight = rng.Range(2.2f, 3.0f);
            style.logs = true;
            style.chimney = true;
            break;
        case Region::City:
            // Саарал, цагаан шавардлага; олон давхар, хавтгай дээвэр
            style.wall = pick({ { 186, 186, 182, 255 }, { 160, 164, 170, 255 }, { 206, 196, 176, 255 }, { 140, 146, 156, 255 } });
            style.flatRoof = rng.Chance(0.5f);
            style.roof = style.flatRoof ? Color{ 96, 96, 100, 255 } : pick({ { 60, 70, 76, 255 }, { 96, 100, 108, 255 } });
            style.floors = rng.Chance(0.6f) ? 2 : 1;
            style.porch = false;
            break;
        case Region::East:
            // Цайвар шар, ногоон, хөх хана; улаан, ногоон дээвэр
            style.wall = pick({ { 224, 206, 140, 255 }, { 170, 196, 150, 255 }, { 156, 180, 200, 255 }, { 210, 200, 186, 255 } });
            style.roof = pick({ { 150, 62, 50, 255 }, { 70, 110, 84, 255 } });
            break;
        }
        return style;
    }

    HouseStyle RandomHouseStyle(Rng& rng)
    {
        static const Color walls[] = {
            { 196, 184, 160, 255 }, { 136, 156, 164, 255 }, { 142, 158, 128, 255 }, { 176, 138, 110, 255 },
            { 206, 200, 190, 255 }, { 150, 120, 96, 255 },  { 170, 164, 132, 255 }, { 128, 136, 150, 255 },
        };
        static const Color roofs[] = {
            { 70, 110, 84, 255 }, { 150, 62, 50, 255 },  { 58, 92, 132, 255 }, { 120, 84, 60, 255 },
            { 96, 100, 108, 255 }, { 150, 110, 50, 255 }, { 60, 70, 76, 255 },
        };

        HouseStyle style;
        style.wall = walls[rng.Int(0, 7)];
        style.roof = roofs[rng.Int(0, 6)];
        style.flatRoof = rng.Chance(0.25f);
        if (style.flatRoof) style.roof = { 110, 108, 104, 255 };
        style.roofHeight = rng.Range(1.3f, 2.5f);
        style.chimney = rng.Chance(0.7f);
        style.porch = rng.Chance(0.4f);
        style.floors = rng.Chance(0.3f) ? 2 : 1;
        return style;
    }

    // Хэмжээсийг хүний биетэй (толгойн орой ~1.95 м) харьцуулж сонгосон: хаалга 2.3 м өндөр, тааз 3.1 м.
    void House(Scene& s, float cx, float cz, float w, float d, const HouseStyle& style, int doorFace)
    {
        // Дуудагчид хуучин жижиг хэмжээг дамжуулдаг тул энд хүний харьцаанд тааруулж томруулна.
        w *= kHouseScale;
        d *= kHouseScale;

        const float wallHeight = 3.1f;
        const float thick = 0.2f;
        const float doorWidth = 1.3f, doorHeight = 2.3f;
        // Цонх нь жинхэнэ нүх: нэвт харагдана, нэвт буудаж болно. Тавцан нь бууны амнаас доор (1.05 м).
        const float windowWidth = 1.25f, sillHeight = 1.05f, headHeight = 2.45f;
        const Color outer = JitterColor(style.wall, s.rng, 0.05f);

        // Нэг ханын хэсэг: face талын дагуу [from, to] мужид (төвөөс хэмжсэн)
        auto wallPiece = [&](int face, float from, float to, float y0, float y1, bool collide) {
            const float mid = (from + to) * 0.5f, length = to - from;
            const float inset = thick * 0.5f;
            Vector3 center, size;
            switch (face)
            {
            case 0: center = { cx + w * 0.5f - inset, (y0 + y1) * 0.5f, cz + mid }; size = { thick, y1 - y0, length }; break;
            case 1: center = { cx - w * 0.5f + inset, (y0 + y1) * 0.5f, cz + mid }; size = { thick, y1 - y0, length }; break;
            case 2: center = { cx + mid, (y0 + y1) * 0.5f, cz + d * 0.5f - inset }; size = { length, y1 - y0, thick }; break;
            default: center = { cx + mid, (y0 + y1) * 0.5f, cz - d * 0.5f + inset }; size = { length, y1 - y0, thick }; break;
            }
            s.solid.Box(center, size, outer);
            if (collide) s.Collider(center, size);
        };

        // Цонхны нүхтэй хана: [from, to] мужид centers-д заасан төвтэй цонхнуудыг тойруулж хэсэг хэсгээр барина.
        // base: тухайн давхрын шалны өндөр (тавцан, тотгыг түүнээс хэмжинэ).
        auto wallWithWindows = [&](int face, float from, float to, float y0, float y1, const float* centers, int count,
                                   float base) {
            const float sill = base + sillHeight, head = base + headHeight;
            // Хүрээ: ханаас бага зэрэг товгор цайвар мод
            auto frame = [&](float a, float b, float ya, float yb) {
                const float mid = (a + b) * 0.5f, length = b - a, inset = thick * 0.5f, depth = thick + 0.1f;
                Vector3 center, size;
                switch (face)
                {
                case 0: center = { cx + w * 0.5f - inset, (ya + yb) * 0.5f, cz + mid }; size = { depth, yb - ya, length }; break;
                case 1: center = { cx - w * 0.5f + inset, (ya + yb) * 0.5f, cz + mid }; size = { depth, yb - ya, length }; break;
                case 2: center = { cx + mid, (ya + yb) * 0.5f, cz + d * 0.5f - inset }; size = { length, yb - ya, depth }; break;
                default: center = { cx + mid, (ya + yb) * 0.5f, cz - d * 0.5f + inset }; size = { length, yb - ya, depth }; break;
                }
                s.solid.Box(center, size, kWindowFrame);
            };

            float cursor = from;
            for (int i = 0; i < count; ++i)
            {
                const float left = centers[i] - windowWidth * 0.5f, right = centers[i] + windowWidth * 0.5f;
                wallPiece(face, cursor, left, y0, y1, true);
                wallPiece(face, left, right, y0, sill, true);  // цонхны доорх хана
                wallPiece(face, left, right, head, y1, true);  // цонхны дээрх тотго
                frame(left - 0.07f, right + 0.07f, sill - 0.07f, sill);
                frame(left - 0.14f, right + 0.14f, sill - 0.11f, sill - 0.07f); // гадна тавцангийн ирмэг
                frame(left - 0.07f, right + 0.07f, head, head + 0.07f);
                frame(left - 0.07f, left, sill, head);
                frame(right, right + 0.07f, sill, head);
                cursor = right;
            }
            wallPiece(face, cursor, to, y0, y1, true);
        };

        // Дээд давхар руу дотуур шатаар гарна. Шат нь хаалгагүй (-Z) ханыг дагаж байрладаг тул
        // хаалга нь +-X талд байгаа байшинд л дээд давхар хийнэ.
        const bool upper = style.floors > 1 && doorFace < 2;
        const float floorTop = wallHeight + 0.15f; // дээд давхрын шалны гадаргуу

        // Цонхоор орж гарах гарц. along: ханын төвөөс цонхны төв хүртэл; upperFloor: 2 давхрын цонх.
        auto windowLink = [&](int face, float along, bool upperFloor) {
            const float nx = face == 0 ? 1.0f : face == 1 ? -1.0f : 0.0f;
            const float nz = face == 2 ? 1.0f : face == 3 ? -1.0f : 0.0f;
            const Vector3 center = face < 2 ? Vector3{ cx + nx * w * 0.5f, 0.0f, cz + along }
                                            : Vector3{ cx + along, 0.0f, cz + nz * d * 0.5f };
            const float outward = std::atan2(nx, nz);
            const float ground = s.terrain.HeightAt(center.x + nx, center.z + nz);
            if (upperFloor)
            {
                // Тавцангаас гараараа зүүгдэнэ (хөл нь тавцангаас 2 м доор), тэндээс газарт үсэрнэ.
                s.passages.push_back({ { center.x - nx * 0.8f, floorTop, center.z - nz * 0.8f },
                                       { center.x + nx * 0.44f, floorTop - 1.0f, center.z + nz * 0.44f },
                                       PassageKind::WindowDrop, outward + PI, 0.7f });
            }
            else
            {
                const Vector3 inside = { center.x - nx * 0.8f, ground + 0.1f, center.z - nz * 0.8f };
                const Vector3 outside = { center.x + nx * 0.95f, ground, center.z + nz * 0.95f };
                // (wall: эхлэх цэгээс 0.2 м зузаан ханын төв хүртэл; гарцын төв нь ханын гадна гадаргуу)
                s.passages.push_back({ inside, outside, PassageKind::WindowOut, outward, 0.7f });
                s.passages.push_back({ outside, inside, PassageKind::WindowIn, outward + PI, 1.05f });
            }
        };

        s.solid.Box({ cx, 0.04f, cz }, { w + 0.3f, 0.08f, d + 0.3f }, kConcrete);                  // суурь
        s.solid.Box({ cx, 0.09f, cz }, { w - thick, 0.03f, d - thick }, JitterColor(kWood, s.rng, 0.08f)); // шал

        for (int face = 0; face < 4; ++face)
        {
            const float span = face < 2 ? d : w;
            const float half = span * 0.5f;

            if (face == doorFace)
            {
                // Хаалганы хоёр талын хана, дээрх тотго
                const float door = -span * 0.2f;
                wallPiece(face, -half, door - doorWidth * 0.5f, 0.0f, wallHeight, true);
                const float doorSideWindow = span * 0.22f;
                wallWithWindows(face, door + doorWidth * 0.5f, half, 0.0f, wallHeight, &doorSideWindow, 1, 0.0f);
                wallPiece(face, door - doorWidth * 0.5f, door + doorWidth * 0.5f, doorHeight, wallHeight, false);

                // Нээж хаадаг хаалга: нугас нь нээлхийн нэг захад, гадагш эргэж онгойно.
                // normal: ханын гадагш харсан чиглэл, along: хана дагуух чиглэл.
                const float nx = face == 0 ? 1.0f : face == 1 ? -1.0f : 0.0f;
                const float nz = face == 2 ? 1.0f : face == 3 ? -1.0f : 0.0f;
                const float wallX = cx + nx * (w * 0.5f - thick * 0.5f);
                const float wallZ = cz + nz * (d * 0.5f - thick * 0.5f);
                const bool alongZ = face < 2; // +-X талын хана Z дагуу сунана
                const float start = door - doorWidth * 0.5f;

                Door leaf = {};
                leaf.hinge = alongZ ? Vector3{ wallX, 0.05f, cz + start } : Vector3{ cx + start, 0.05f, wallZ };
                leaf.width = doorWidth;
                leaf.height = doorHeight - 0.05f;
                leaf.closedYaw = alongZ ? 0.0f : PI * 0.5f;       // хавтас нугаснаас +Z эсвэл +X рүү сунана
                leaf.openYaw = std::atan2(nx, nz);                // гадагш
                leaf.color = kDoor;

                Interior room = {};
                room.center = { cx, cz };
                room.halfX = w * 0.5f - thick;
                room.halfZ = d * 0.5f - thick;
                room.round = false;
                const Vector2 doorway = alongZ ? Vector2{ wallX, cz + door } : Vector2{ cx + door, wallZ };
                room.doorOutside = { doorway.x + nx * 1.5f, doorway.y + nz * 1.5f };
                room.doorInside = { doorway.x - nx * 1.2f, doorway.y - nz * 1.2f };
                s.Entrance(leaf, room);

                if (style.porch)
                {
                    // Саравч: хаалганы дээр гадагш сунасан дээвэр, хоёр баганатай
                    const float reach = 1.7f;
                    const float span = doorWidth + 1.0f;
                    s.solid.Box({ doorway.x + nx * reach * 0.5f, doorHeight + 0.22f, doorway.y + nz * reach * 0.5f },
                                alongZ ? Vector3{ reach, 0.1f, span } : Vector3{ span, 0.1f, reach }, style.roof);
                    for (float side : { -1.0f, 1.0f })
                    {
                        const float lateral = side * (span * 0.5f - 0.1f);
                        const Vector3 post = { doorway.x + nx * (reach - 0.1f) + (alongZ ? 0.0f : lateral),
                                               (doorHeight + 0.2f) * 0.5f,
                                               doorway.y + nz * (reach - 0.1f) + (alongZ ? lateral : 0.0f) };
                        s.solid.Box(post, { 0.12f, doorHeight + 0.2f, 0.12f }, ScaleColor(kWood, 0.8f));
                        s.Collider(post, { 0.14f, doorHeight + 0.2f, 0.14f });
                    }
                }

                windowLink(face, doorSideWindow, false);
            }
            else
            {
                // Урт хананд хоёр, богинод нэг цонх
                const int count = span > 6.2f ? 2 : 1;
                const float centers[2] = { count == 2 ? -span * 0.24f : 0.0f, span * 0.24f };
                wallWithWindows(face, -half, half, 0.0f, wallHeight, centers, count, 0.0f);
                for (int i = 0; i < count; ++i)
                {
                    // (Шаттай ханын цонхоор гарахгүй: дотор талд нь шат байрладаг.)
                    if (!(upper && face == 3)) windowLink(face, centers[i], false);
                }
            }
        }

        // Дээврийн суурийн өндөр: хоёр давхар байшинд дээд давхрын дээр
        const float roofBase = wallHeight + (upper ? 2.9f : 0.0f);
        if (upper)
        {
            const Color floorColor = JitterColor(kWood, s.rng, 0.08f);
            const Color trim = ScaleColor(outer, 0.8f);

            // Давхрын заагийн зурвас (гадна талаар)
            s.solid.Box({ cx, wallHeight, cz - d * 0.5f - 0.03f }, { w + 0.14f, 0.16f, 0.08f }, trim);
            s.solid.Box({ cx, wallHeight, cz + d * 0.5f + 0.03f }, { w + 0.14f, 0.16f, 0.08f }, trim);
            s.solid.Box({ cx - w * 0.5f - 0.03f, wallHeight, cz }, { 0.08f, 0.16f, d + 0.14f }, trim);
            s.solid.Box({ cx + w * 0.5f + 0.03f, wallHeight, cz }, { 0.08f, 0.16f, d + 0.14f }, trim);

            // --- Дээд давхрын хана, цонх (гадна, дотор хоёр талаас харагдана) ---
            for (int face = 0; face < 4; ++face)
            {
                const float span = face < 2 ? d : w;
                const int count = span > 6.2f ? 2 : 1;
                const float centers[2] = { count == 2 ? -span * 0.24f : 0.0f, span * 0.24f };
                wallWithWindows(face, -span * 0.5f, span * 0.5f, wallHeight, roofBase, centers, count, floorTop);
                for (int i = 0; i < count; ++i)
                {
                    if (face != 3) windowLink(face, centers[i], true); // шатны нүхний дээрх цонхоор гарахгүй
                }
            }

            // --- Шат: -Z ханыг дагаж, хаалганы талаас холын хана руу өгсөнө ---
            TraceLog(LOG_DEBUG, "HOUSE2: center %.1f %.1f size %.1f %.1f door face %d", cx, cz, w, d, doorFace);
            const float dir = doorFace == 0 ? -1.0f : 1.0f; // өгсөх чиглэл (X)
            const int steps = 12;
            const float rise = floorTop / (float)steps, tread = 0.3f, stairWidth = 1.05f;
            const float z0 = cz - d * 0.5f + thick, z1 = z0 + stairWidth; // шатны өргөний муж
            const float holeZ = z1 + 0.3f;                                 // нүхний ирмэг (шатнаас арай өргөн)
            const float startX = cx - dir * 1.0f;                          // эхний гишгүүрийн захын X
            const float endX = startX + dir * tread * (float)steps;        // сүүлчийн гишгүүрийн төгсгөл
            for (int k = 0; k < steps; ++k)
            {
                const float top = rise * (float)(k + 1);
                const Vector3 center = { startX + dir * tread * ((float)k + 0.5f), top * 0.5f, (z0 + z1) * 0.5f };
                s.solid.Box(center, { tread, top, stairWidth }, ScaleColor(floorColor, k % 2 == 0 ? 0.92f : 0.82f));
                s.Collider(center, { tread, top, stairWidth }, true, true); // гишгүүр: дээгүүр нь алхана
            }

            // --- Дээд давхрын шал: шатны дээгүүр нүхтэй. Гурван хэсэг: гол талбай, шатны төгсгөлийн тавцан,
            //     шатны эхний гишгүүрүүдийн дээрх хэсэг (тэнд толгой шүргэхгүй). ---
            const float xNear = cx - dir * (w * 0.5f - thick); // хаалганы талын ханын дотор тал
            const float xFar = cx + dir * (w * 0.5f - thick);  // холын ханын дотор тал
            const float holeStart = startX + dir * 0.5f;       // нүх эхлэх X
            auto slab = [&](float xa, float xb, float za, float zb) {
                if (std::fabs(xb - xa) < 0.05f || zb - za < 0.05f) return;
                const Vector3 center = { (xa + xb) * 0.5f, wallHeight + 0.075f, (za + zb) * 0.5f };
                const Vector3 size = { std::fabs(xb - xa), 0.15f, zb - za };
                s.solid.Box(center, size, floorColor);
                s.Collider(center, size);
            };
            slab(xNear, xFar, holeZ, cz + d * 0.5f - thick); // гол талбай
            slab(endX, xFar, z0, holeZ);                     // шатны төгсгөлийн тавцан
            slab(xNear, holeStart, z0, holeZ);               // шатны эхний хэсгийн дээр

            // Шатны байршлыг барилгын дотор талын мэдээлэлд тэмдэглэнэ (зомби дээд давхарт гарахад).
            Interior& interior = s.interiors.back();
            interior.hasStairs = true;
            interior.stairBottom = { startX - dir * 0.6f, (z0 + z1) * 0.5f };
            interior.stairTop = { endX + dir * 0.45f, (z0 + z1) * 0.5f };
            interior.upperFloor = floorTop;

            // Нүхний хашлага: хажуу тал ба эхлэлийн тал (шатны төгсгөлийн тал нээлттэй)
            const Vector3 sideRail = { (holeStart + endX) * 0.5f, floorTop + 0.45f, holeZ };
            s.solid.Box(sideRail, { std::fabs(endX - holeStart), 0.9f, 0.08f }, trim);
            s.Collider(sideRail, { std::fabs(endX - holeStart), 0.9f, 0.12f });
            const Vector3 endRail = { holeStart, floorTop + 0.45f, (z0 + holeZ) * 0.5f };
            s.solid.Box(endRail, { 0.08f, 0.9f, holeZ - z0 }, trim);
            s.Collider(endRail, { 0.12f, 0.9f, holeZ - z0 });

            // Дээд давхрын тавилга: ор, хайрцаг
            Furniture(s, { cx + dir * (w * 0.5f - 0.9f), floorTop + 0.25f, cz + d * 0.5f - 1.35f }, { 1.0f, 0.5f, 2.0f },
                      { 70, 96, 120, 255 });
            Furniture(s, { cx - dir * (w * 0.5f - 0.9f), floorTop + 0.4f, cz + d * 0.5f - 0.8f }, { 0.8f, 0.8f, 0.8f }, kWood);
            s.solid.Box({ cx, roofBase - 0.03f, cz }, { w - thick, 0.05f, d - thick }, ScaleColor(outer, 0.85f)); // тааз
        }

        if (style.flatRoof)
        {
            // Хавтгай дээвэр ба эргэн тойрны намхан хашлага
            const Color parapet = ScaleColor(outer, 0.88f);
            s.solid.Box({ cx, roofBase + 0.12f, cz }, { w + 0.5f, 0.24f, d + 0.5f }, style.roof);
            s.solid.Box({ cx, roofBase + 0.44f, cz - d * 0.5f - 0.15f }, { w + 0.5f, 0.4f, 0.2f }, parapet);
            s.solid.Box({ cx, roofBase + 0.44f, cz + d * 0.5f + 0.15f }, { w + 0.5f, 0.4f, 0.2f }, parapet);
            s.solid.Box({ cx - w * 0.5f - 0.15f, roofBase + 0.44f, cz }, { 0.2f, 0.4f, d + 0.5f }, parapet);
            s.solid.Box({ cx + w * 0.5f + 0.15f, roofBase + 0.44f, cz }, { 0.2f, 0.4f, d + 0.5f }, parapet);
        }
        else
        {
            s.solid.GableRoof({ cx, roofBase, cz }, w + 1.0f, d + 1.0f, style.roofHeight, w >= d, style.roof);
            // Төмөр дээврийн долгион: налуугаар уруудах цайвар зураасууд, хаяаны тууз, нурууны таг
            const bool ridgeX = w >= d;
            const float span = (ridgeX ? w : d) + 1.0f, half = (ridgeX ? d : w) * 0.5f + 0.5f;
            const Color rib = ScaleColor(style.roof, 1.18f);
            for (float t = -span * 0.5f + 0.25f; t < span * 0.5f; t += 0.55f)
            {
                for (float side : { -1.0f, 1.0f })
                {
                    auto at = [&](float along, float across, float y) {
                        return ridgeX ? Vector3{ cx + along, y, cz + across } : Vector3{ cx + across, y, cz + along };
                    };
                    const Vector3 low0 = at(t - 0.03f, side * half, roofBase + 0.02f), low1 = at(t + 0.03f, side * half, roofBase + 0.02f);
                    const Vector3 top0 = at(t - 0.03f, 0.0f, roofBase + style.roofHeight + 0.02f);
                    const Vector3 top1 = at(t + 0.03f, 0.0f, roofBase + style.roofHeight + 0.02f);
                    if ((side > 0) == ridgeX) s.solid.Quad(low0, low1, top1, top0, rib);
                    else s.solid.Quad(low1, low0, top0, top1, rib);
                }
            }
            const Vector3 ridgeSize = ridgeX ? Vector3{ span, 0.08f, 0.22f } : Vector3{ 0.22f, 0.08f, span };
            s.solid.Box({ cx, roofBase + style.roofHeight + 0.02f, cz }, ridgeSize, ScaleColor(style.roof, 0.75f));
            // Хаяаны доорх модон тууз (fascia)
            const Color fascia = { 214, 206, 188, 255 };
            if (ridgeX)
            {
                s.solid.Box({ cx, roofBase - 0.04f, cz - half }, { span, 0.12f, 0.06f }, fascia);
                s.solid.Box({ cx, roofBase - 0.04f, cz + half }, { span, 0.12f, 0.06f }, fascia);
            }
            else
            {
                s.solid.Box({ cx - half, roofBase - 0.04f, cz }, { 0.06f, 0.12f, span }, fascia);
                s.solid.Box({ cx + half, roofBase - 0.04f, cz }, { 0.06f, 0.12f, span }, fascia);
            }
        }
        // Суурь: ханын доод талын бараан тууз, булангийн цайвар хүрээ
        {
            const Color plinth = ScaleColor(outer, 0.62f);
            s.solid.Box({ cx, 0.2f, cz - d * 0.5f - 0.02f }, { w + 0.08f, 0.4f, 0.06f }, plinth);
            s.solid.Box({ cx, 0.2f, cz + d * 0.5f + 0.02f }, { w + 0.08f, 0.4f, 0.06f }, plinth);
            s.solid.Box({ cx - w * 0.5f - 0.02f, 0.2f, cz }, { 0.06f, 0.4f, d + 0.08f }, plinth);
            s.solid.Box({ cx + w * 0.5f + 0.02f, 0.2f, cz }, { 0.06f, 0.4f, d + 0.08f }, plinth);
            const Color corner = style.logs ? ScaleColor(style.wall, 0.8f) : Color{ 222, 216, 200, 255 };
            for (float sx : { -1.0f, 1.0f })
            {
                for (float sz : { -1.0f, 1.0f })
                {
                    if (style.logs)
                    {
                        // Дүнзэн байшин: булан бүрд ээлжлэн хөндлөн цухуйсан дүнзний үзүүрүүд
                        int row = 0;
                        for (float y = 0.5f; y < roofBase - 0.1f; y += 0.28f, ++row)
                        {
                            const bool alongX = row % 2 == 0;
                            const Vector3 c = { cx + sx * (w * 0.5f + (alongX ? 0.1f : 0.0f)), y,
                                                cz + sz * (d * 0.5f + (alongX ? 0.0f : 0.1f)) };
                            s.solid.Box(c, alongX ? Vector3{ 0.5f, 0.24f, 0.24f } : Vector3{ 0.24f, 0.24f, 0.5f },
                                        JitterColor(ScaleColor(style.wall, 0.85f), s.rng, 0.06f));
                        }
                        continue;
                    }
                    s.solid.Box({ cx + sx * (w * 0.5f + 0.01f), roofBase * 0.5f + 0.2f, cz + sz * (d * 0.5f + 0.01f) },
                                { 0.16f, roofBase - 0.4f, 0.16f }, corner);
                }
            }
            if (style.logs)
            {
                // Дүнзний хэвтээ зураас: ханын цонхгүй дээд хэсэг ба тааз, тавцангийн доор
                for (float y : { 0.7f, 2.75f })
                {
                    s.solid.Box({ cx, y, cz - d * 0.5f - 0.02f }, { w, 0.05f, 0.03f }, ScaleColor(style.wall, 0.65f));
                    s.solid.Box({ cx, y, cz + d * 0.5f + 0.02f }, { w, 0.05f, 0.03f }, ScaleColor(style.wall, 0.65f));
                    s.solid.Box({ cx - w * 0.5f - 0.02f, y, cz }, { 0.03f, 0.05f, d }, ScaleColor(style.wall, 0.65f));
                    s.solid.Box({ cx + w * 0.5f + 0.02f, y, cz }, { 0.03f, 0.05f, d }, ScaleColor(style.wall, 0.65f));
                }
            }
            if (style.ornament)
            {
                // Хээтэй тууз: цонхны дээгүүр (2.6..2.95 м), дээр нь цагаан дөрвөлжин хээ
                const float y = 2.78f;
                const Color light = { 236, 232, 220, 255 };
                s.solid.Box({ cx, y, cz - d * 0.5f - 0.03f }, { w + 0.04f, 0.34f, 0.04f }, style.trim);
                s.solid.Box({ cx, y, cz + d * 0.5f + 0.03f }, { w + 0.04f, 0.34f, 0.04f }, style.trim);
                s.solid.Box({ cx - w * 0.5f - 0.03f, y, cz }, { 0.04f, 0.34f, d + 0.04f }, style.trim);
                s.solid.Box({ cx + w * 0.5f + 0.03f, y, cz }, { 0.04f, 0.34f, d + 0.04f }, style.trim);
                for (float t = -w * 0.5f + 0.3f; t < w * 0.5f - 0.1f; t += 0.6f)
                {
                    s.solid.Box({ cx + t, y, cz - d * 0.5f - 0.055f }, { 0.16f, 0.16f, 0.02f }, light);
                    s.solid.Box({ cx + t, y, cz + d * 0.5f + 0.055f }, { 0.16f, 0.16f, 0.02f }, light);
                }
                for (float t = -d * 0.5f + 0.3f; t < d * 0.5f - 0.1f; t += 0.6f)
                {
                    s.solid.Box({ cx - w * 0.5f - 0.055f, y, cz + t }, { 0.02f, 0.16f, 0.16f }, light);
                    s.solid.Box({ cx + w * 0.5f + 0.055f, y, cz + t }, { 0.02f, 0.16f, 0.16f }, light);
                }
            }
        }
        if (!upper)
        {
            s.solid.Box({ cx, wallHeight - 0.03f, cz }, { w - thick, 0.05f, d - thick }, ScaleColor(outer, 0.85f)); // тааз
        }
        if (style.chimney)
        {
            const float top = style.flatRoof ? 1.3f : style.roofHeight * 0.6f + 1.0f;
            s.solid.Box({ cx + w * 0.25f, roofBase + top * 0.5f, cz + d * 0.15f }, { 0.55f, top, 0.55f }, kBrick);
            s.solid.Box({ cx + w * 0.25f, roofBase + top + 0.05f, cz + d * 0.15f }, { 0.68f, 0.1f, 0.68f }, ScaleColor(kBrick, 0.7f));
            s.solid.Cylinder({ cx + w * 0.25f, roofBase + top + 0.1f, cz + d * 0.15f }, 0.1f, 0.1f, 0.3f, 6, { 60, 58, 58, 255 });
        }

        // Тавилга: хаалганаас хол талд ор, ширээ, зуух
        const float sx = doorFace == 1 ? 1.0f : -1.0f;
        const float sz = doorFace == 2 ? -1.0f : 1.0f;
        Furniture(s, { cx + sx * (w * 0.5f - 0.85f), 0.3f, cz + sz * (d * 0.5f - 1.3f) }, { 1.0f, 0.5f, 2.0f },
                  { 120, 60, 54, 255 });                                                              // ор
        s.solid.Box({ cx + sx * (w * 0.5f - 0.85f), 0.6f, cz + sz * (d * 0.5f - 0.55f) }, { 0.8f, 0.14f, 0.4f },
                    { 214, 208, 196, 255 });                                                          // дэр
        Furniture(s, { cx - sx * (w * 0.5f - 1.0f), 0.4f, cz + sz * d * 0.25f }, { 0.8f, 0.8f, 1.1f }, kWood); // ширээ
        Furniture(s, { cx + w * 0.25f, 0.45f, cz + d * 0.15f }, { 0.6f, 0.9f, 0.6f }, { 52, 50, 50, 255 }); // зуух

        // Дээвэр, тааз: камер, сум дээгүүр нэвтрэхгүй (хүний толгойноос дээш тул хөдөлгөөн хаахгүй)
        // (Хоёр давхар байшинд доод давхрын таазыг дээд давхрын шалны хэсгүүд орлоно.)
        s.Collider({ cx, roofBase + 0.5f, cz }, { w, 1.0f, d });
        Footprint(s, cx, cz, w, d);
        s.Shadow(cx - w * 0.5f - 0.45f, cz - d * 0.5f - 0.45f, cx + w * 0.5f + 0.45f, cz + d * 0.5f + 0.45f, 0.0f,
                 roofBase + 0.6f);
    }

    void Shed(Scene& s, float cx, float cz, float w, float d, float h)
    {
        s.solid.Box({ cx, h * 0.5f, cz }, { w, h, d }, JitterColor(kWoodGrey, s.rng, 0.08f));
        s.solid.Box({ cx, h + 0.06f, cz }, { w + 0.3f, 0.12f, d + 0.3f }, JitterColor(kRust, s.rng, 0.1f));
        FacePanel(s, cx, cz, w, d, 3, 0.0f, h * 0.45f, 0.7f, h * 0.8f, 0.1f, kDoor);
        s.Collider({ cx, h * 0.5f, cz }, { w, h, d });
        s.Shadow(cx - w * 0.5f, cz - d * 0.5f, cx + w * 0.5f, cz + d * 0.5f, 0.0f, h);
    }

    // Дотогш орж болох гэр: 16 талт хана, урд талын (-Z) нэг тал нь хаалга.
    // Таван ханатай гэрийн хэмжээ: голч ~7-10 м, хана 2 м, тооно 3.4 м өндөрт.
    void Ger(Scene& s, float cx, float cz, float radius, Color doorColor = { 186, 96, 40, 255 })
    {
        radius *= kGerScale; // дуудагчид хуучин жижиг радиусыг дамжуулдаг

        const float wall = 2.0f;
        const float roof = 1.4f;
        const float doorHeight = 1.85f;
        const int sides = 16;
        const int doorSide = 7; // төв нь -Z зүг рүү харсан тал
        const float crown = radius * 0.22f;
        const Color band = ScaleColor(kGerFelt, 0.6f);
        const Color inner = { 176, 124, 84, 255 }; // доторх модон хана

        // Оройнуудыг хагас алхмаар шилжүүлснээр нэг тал яг урагшаа (-Z) харна.
        auto ring = [&](int index, float r, float y) {
            const float angle = 2.0f * PI * ((float)index + 0.5f) / (float)sides;
            return Vector3{ cx + std::sin(angle) * r, y, cz + std::cos(angle) * r };
        };

        for (int i = 0; i < sides; ++i)
        {
            const Vector3 b0 = ring(i, radius, 0.0f), b1 = ring(i + 1, radius, 0.0f);
            const Vector3 t0 = ring(i, radius, wall), t1 = ring(i + 1, radius, wall);

            // Дээвэр: бүх талд, хананаас бага зэрэг гадагш цухуйсан хаяатай
            const Vector3 e0 = ring(i, radius + 0.14f, wall - 0.08f), e1 = ring(i + 1, radius + 0.14f, wall - 0.08f);
            s.solid.Quad(e0, e1, ring(i + 1, crown, wall + roof), ring(i, crown, wall + roof), JitterColor(kGerRoof, s.rng, 0.03f));
            s.solid.Quad(e1, e0, t0, t1, ScaleColor(kGerRoof, 0.7f)); // хаяаны доод тал
            // Дээврийн хаяавч: хөх хээтэй тууз
            const Vector3 h0 = ring(i, radius + 0.1f, wall + 0.04f), h1 = ring(i + 1, radius + 0.1f, wall + 0.04f);
            s.solid.Quad(e0, e1, h1, h0, i % 2 == 0 ? kGerTrim : kGerTrimLight);
            // Дээврийн бүслүүр (уяа): тооноос хаяа хүртэл
            if (i % 2 == 0)
            {
                const Vector3 r0 = ring(i, radius + 0.15f, wall - 0.07f), r1 = ring(i, crown + 0.02f, wall + roof + 0.01f);
                s.solid.Quad(Vector3Add(r0, { 0.0f, 0.02f, 0.0f }), Vector3Add(r1, { 0.0f, 0.02f, 0.0f }),
                             Vector3Add(r1, { 0.04f, 0.03f, 0.04f }), Vector3Add(r0, { 0.04f, 0.03f, 0.04f }), kGerRope);
            }

            if (i == doorSide)
            {
                // Хаалганы тотго ба хоёр хажуугийн хүрээ
                s.solid.Quad({ b0.x, doorHeight, b0.z }, { b1.x, doorHeight, b1.z }, t1, t0, kGerFelt);
                s.solid.Box({ b0.x, doorHeight * 0.5f, b0.z }, { 0.12f, doorHeight, 0.12f }, doorColor);
                s.solid.Box({ b1.x, doorHeight * 0.5f, b1.z }, { 0.12f, doorHeight, 0.12f }, doorColor);
                s.solid.Box({ cx, doorHeight + 0.05f, b0.z }, { std::fabs(b1.x - b0.x) + 0.12f, 0.1f, 0.12f }, doorColor);
                // Хаалганы дээрх хээ (өлзий маягийн): улаан, хөх дөрвөлжин эгнээ
                for (int k = 0; k < 5; ++k)
                {
                    const float x = std::min(b0.x, b1.x) + 0.12f + (std::fabs(b1.x - b0.x) - 0.24f) * ((float)k + 0.5f) / 5.0f;
                    s.solid.Box({ x, doorHeight + 0.05f, b0.z - 0.065f }, { 0.08f, 0.06f, 0.01f },
                                k % 2 == 0 ? kGerTrim : Color{ 236, 196, 60, 255 });
                }
                // Босго
                s.solid.Box({ cx, 0.06f, b0.z }, { std::fabs(b1.x - b0.x) + 0.12f, 0.12f, 0.16f }, ScaleColor(doorColor, 0.75f));

                // Нээж хаадаг хаалга: нугас нь баруун захад (-X), гадагш (-Z) онгойно.
                const float left = std::min(b0.x, b1.x);
                Door leaf = {};
                leaf.hinge = { left + 0.06f, 0.05f, b0.z };
                leaf.width = std::fabs(b1.x - b0.x) - 0.12f;
                leaf.height = doorHeight - 0.05f;
                leaf.closedYaw = PI * 0.5f; // +X рүү сунана
                leaf.openYaw = PI;          // -Z рүү
                leaf.color = doorColor;

                Interior room = {};
                room.center = { cx, cz };
                room.halfX = room.halfZ = radius - 0.3f;
                room.round = true;
                room.doorOutside = { cx, b0.z - 1.2f };
                room.doorInside = { cx, b0.z + 1.3f };
                s.Entrance(leaf, room);
                continue;
            }

            s.solid.Quad(b0, b1, t1, t0, kGerFelt);
            // Доторх хана: бага зэрэг дотогш, модон өнгөтэй
            s.solid.Quad(ring(i, radius - 0.05f, 0.0f), ring(i + 1, radius - 0.05f, 0.0f), ring(i + 1, radius - 0.05f, wall),
                         ring(i, radius - 0.05f, wall), inner);
            // Бүслүүр
            for (float y : { 0.7f, 1.45f })
            {
                s.solid.Quad(ring(i, radius + 0.03f, y), ring(i + 1, radius + 0.03f, y), ring(i + 1, radius + 0.03f, y + 0.07f),
                             ring(i, radius + 0.03f, y + 0.07f), band);
            }

            // Ханын мөргөлдөөн: талын хоёр үзүүрийг хамарсан хайрцаг
            const Vector3 center = { (b0.x + b1.x) * 0.5f, wall * 0.5f, (b0.z + b1.z) * 0.5f };
            s.Collider(center, { std::fabs(b1.x - b0.x) + 0.22f, wall, std::fabs(b1.z - b0.z) + 0.22f });
        }

        s.solid.Cylinder({ cx, wall + roof, cz }, crown, crown, 0.12f, sides, doorColor); // тооно
        s.solid.Cylinder({ cx, wall + roof + 0.12f, cz }, crown * 0.75f, crown * 0.75f, 0.05f, sides, kGerRoof); // өрх
        // Зуухны яндан тооноор гарна
        s.solid.Cylinder({ cx + crown * 0.3f, wall + roof, cz + crown * 0.2f }, 0.08f, 0.08f, 0.9f, 8, { 60, 58, 58, 255 });
        s.solid.Cylinder({ cx + crown * 0.3f, wall + roof + 0.9f, cz + crown * 0.2f }, 0.11f, 0.0f, 0.1f, 8, { 60, 58, 58, 255 });
        // Шалны суурь: банзан тавцан гадагш бага зэрэг цухуйна
        for (int i = 0; i < sides; ++i)
        {
            s.solid.Quad(ring(i + 1, radius + 0.05f, 0.0f), ring(i, radius + 0.05f, 0.0f), ring(i, radius + 0.05f, 0.1f),
                         ring(i + 1, radius + 0.05f, 0.1f), { 120, 86, 56, 255 });
        }

        // Шал: хивс
        const Vector3 middle = { cx, 0.04f, cz };
        for (int i = 0; i < sides; ++i)
        {
            s.solid.Triangle(middle, ring(i, radius - 0.06f, 0.04f), ring(i + 1, radius - 0.06f, 0.04f),
                             JitterColor({ 132, 60, 48, 255 }, s.rng, 0.06f));
        }

        // Тавилга: голд зуух ба яндан, хоёр талд ор, хойморт авдар
        Furniture(s, { cx, 0.4f, cz }, { 0.7f, 0.8f, 0.7f }, { 52, 50, 50, 255 });
        s.solid.Box({ cx, 0.8f + (wall + roof - 0.8f) * 0.5f, cz }, { 0.16f, wall + roof - 0.8f, 0.16f }, { 60, 58, 58, 255 });
        Furniture(s, { cx - radius + 1.05f, 0.28f, cz + 0.3f }, { 1.0f, 0.56f, 2.0f }, { 150, 84, 40, 255 });
        Furniture(s, { cx + radius - 1.05f, 0.28f, cz + 0.3f }, { 1.0f, 0.56f, 2.0f }, { 150, 84, 40, 255 });
        Furniture(s, { cx, 0.36f, cz + radius - 0.75f }, { 1.5f, 0.72f, 0.55f }, { 186, 96, 40, 255 });

        // Дээвэр бүхэлдээ: камер, сум нэвтрэхгүй. Доод тал нь 2.1 м-т (хүн, энгийн зомбины толгойноос дээш)
        // тул дотор алхахад саад болохгүй; харин 2.5 м өндөр том зомби гэрт багтахгүй.
        s.Collider({ cx, 2.8f, cz }, { radius * 1.96f, 1.4f, radius * 1.96f });
        Footprint(s, cx, cz, radius * 1.9f, radius * 1.9f);
        s.Shadow(cx - radius * 0.85f, cz - radius * 0.85f, cx + radius * 0.85f, cz + radius * 0.85f, 0.0f, wall + roof * 0.6f);
    }

    // ------------------------------------------------------------------ Хашаа

    // Тэнхлэг дагуух банзан хашаа. gapStart..gapEnd (эхнээс хэмжсэн зай) хэсэгт хаалганы зай үлдээнэ.
    void PlankFence(Scene& s, float x0, float z0, float x1, float z1, float gapStart = -1.0f, float gapEnd = -1.0f)
    {
        const bool alongX = std::fabs(x1 - x0) > std::fabs(z1 - z0);
        const float length = alongX ? std::fabs(x1 - x0) : std::fabs(z1 - z0);
        const float start = alongX ? std::min(x0, x1) : std::min(z0, z1);
        const float fixed = alongX ? z0 : x0;
        const int segments = std::max(1, (int)std::round(length / 2.0f));
        const float segmentLength = length / (float)segments;
        const float height = 2.05f; // хүний толгойноос арай өндөр

        // Монгол хашаа: ихэнх нь будаггүй саарал банз, заримыг нь хөх, ногоон, улаан хүрэн өнгөөр будсан (хуучирсан).
        const float paintRoll = s.rng.Next();
        const bool painted = paintRoll < 0.35f;
        const Color paints[4] = { { 70, 110, 150, 255 }, { 72, 120, 88, 255 }, { 138, 70, 52, 255 }, { 196, 170, 96, 255 } };
        const Color paint = paints[s.rng.Int(0, 3)];
        const bool pointed = s.rng.Chance(0.6f); // шовх оройтой банз

        // (дагуу, өндөр, хөндлөн) -> ертөнцийн цэг ба хайрцаг
        auto at = [&](float along, float y, float offset) {
            return alongX ? Vector3{ along, y, fixed + offset } : Vector3{ fixed + offset, y, along };
        };
        auto box = [&](float along, float y, float lengthAlong, float h, float thickness, float offset, Color color) {
            if (alongX) s.solid.Box({ along, y, fixed + offset }, { lengthAlong, h, thickness }, color);
            else s.solid.Box({ fixed + offset, y, along }, { thickness, h, lengthAlong }, color);
        };
        auto shadowRun = [&](float from, float to) {
            if (to <= from) return;
            if (alongX) s.Shadow(from, fixed - 0.05f, to, fixed + 0.05f, 0.0f, height);
            else s.Shadow(fixed - 0.05f, from, fixed + 0.05f, to, 0.0f, height);
        };
        // Банз: тэгш өнцөгт их бие, шовх бол дээрээ гурвалжин үзүүр (хоёр нүүр ба хоёр налуу тал).
        auto plank = [&](float center, float width, float h, float lean, Color color) {
            const float body = pointed ? h - width * 0.6f : h;
            if (std::fabs(lean) > 0.001f)
            {
                // Хазайсан банз: ёроолоо тойрон хашааны дагуу эргэнэ.
                const Vector3 base = at(center, 0.0f, 0.0f);
                const Matrix tilt = alongX ? MatrixRotateZ(-lean) : MatrixRotateX(lean);
                s.solid.SetTransform(MatrixMultiply(MatrixMultiply(MatrixTranslate(-base.x, -base.y, -base.z), tilt),
                                                    MatrixTranslate(base.x, base.y, base.z)));
            }
            box(center, body * 0.5f, width, body, 0.05f, 0.0f, color);
            if (pointed)
            {
                const float tip = h;
                const Color top = ScaleColor(color, 0.92f);
                for (float face : { -0.025f, 0.025f })
                {
                    const Vector3 l = at(center - width * 0.5f, body, face), r = at(center + width * 0.5f, body, face);
                    const Vector3 t = at(center, tip, face);
                    s.solid.Triangle(l, r, t, top);
                    s.solid.Triangle(r, l, t, top);
                }
                s.solid.Quad(at(center - width * 0.5f, body, -0.025f), at(center, tip, -0.025f), at(center, tip, 0.025f),
                             at(center - width * 0.5f, body, 0.025f), top);
                s.solid.Quad(at(center + width * 0.5f, body, 0.025f), at(center, tip, 0.025f), at(center, tip, -0.025f),
                             at(center + width * 0.5f, body, -0.025f), top);
            }
            s.solid.ResetTransform();
        };
        // Багана: дөрвөлжин мод, дээрээ бага зэрэг өргөн малгайтай
        auto post = [&](float along, float h) {
            const Color wood = ScaleColor(kWood, 0.7f);
            box(along, h * 0.5f, 0.15f, h, 0.15f, -0.02f, wood);
            box(along, h + 0.03f, 0.2f, 0.06f, 0.2f, -0.02f, ScaleColor(wood, 0.85f));
        };

        float runStart = start;
        bool gatePosts = false;
        for (int i = 0; i < segments; ++i)
        {
            const float a = start + (float)i * segmentLength;
            const float mid = a + segmentLength * 0.5f;
            const float local = mid - start;
            const bool gate = local > gapStart && local < gapEnd;
            const bool broken = !gate && s.rng.Chance(0.07f);

            if (gate || broken)
            {
                shadowRun(runStart, a);
                runStart = a + segmentLength;
                if (broken)
                {
                    // Унасан банз газар хэвтэнэ, нэг хоёр банз хазайж үлдсэн
                    box(mid, 0.06f, segmentLength * 0.9f, 0.06f, 0.5f, 0.6f, JitterColor(kWoodGrey, s.rng, 0.1f));
                    post(a + 0.07f, height + 0.1f);
                    plank(a + 0.35f, 0.42f, height * 0.9f, 0.35f, JitterColor(kWoodGrey, s.rng, 0.1f));
                }
                if (gate && !gatePosts)
                {
                    // Хаалганы өндөр багана, дээр нь хөндлөн хаалт (Монгол хашааны хаалга)
                    gatePosts = true;
                    const float g0 = start + std::max(0.0f, gapStart), g1 = start + std::min(length, gapEnd);
                    const Color frame = painted ? ScaleColor(paint, 0.8f) : ScaleColor(kWood, 0.65f);
                    box(g0, 1.35f, 0.2f, 2.7f, 0.2f, 0.0f, frame);
                    box(g1, 1.35f, 0.2f, 2.7f, 0.2f, 0.0f, frame);
                    box((g0 + g1) * 0.5f, 2.68f, g1 - g0 + 0.5f, 0.16f, 0.24f, 0.0f, frame);
                    box((g0 + g1) * 0.5f, 2.84f, g1 - g0 + 0.2f, 0.16f, 0.16f, 0.0f, ScaleColor(frame, 0.85f));
                }
                continue;
            }

            s.BeginPanel();
            post(a + 0.07f, height + 0.1f);
            box(mid, 0.6f, segmentLength, 0.09f, 0.07f, -0.08f, ScaleColor(kWood, 0.8f));       // хөндлөвч
            box(mid, 1.6f, segmentLength, 0.09f, 0.07f, -0.08f, ScaleColor(kWood, 0.8f));
            box(mid, 0.08f, segmentLength, 0.16f, 0.08f, 0.03f, ScaleColor(kWood, 0.6f));       // доод тэвш банз

            // Банзнууд: өргөн нь янз бүр, хооронд нь жижиг завсар, зарим нь бага зэрэг хазайсан
            float cursor = a + 0.02f;
            const float end = a + segmentLength - 0.02f;
            while (cursor < end - 0.08f)
            {
                const float width = std::min(end - cursor, s.rng.Range(0.36f, 0.56f));
                const float h = height + s.rng.Range(-0.16f, 0.1f);
                Color color = JitterColor(MixColor(kWood, kWoodGrey, s.rng.Next()), s.rng, 0.1f);
                if (painted)
                {
                    // Будаг хуурч гуужсан: зарим банз саарал модоо харуулна
                    color = s.rng.Chance(0.2f) ? JitterColor(kWoodGrey, s.rng, 0.1f)
                                               : JitterColor(MixColor(paint, kWoodGrey, s.rng.Range(0.0f, 0.35f)), s.rng, 0.06f);
                }
                const float lean = s.rng.Chance(0.12f) ? s.rng.Range(-0.06f, 0.06f) : 0.0f;
                plank(cursor + width * 0.5f, width - s.rng.Range(0.02f, 0.07f), h, lean, color);
                cursor += width;
            }

            if (alongX) s.Collider({ mid, height * 0.5f, fixed }, { segmentLength, height, 0.25f });
            else s.Collider({ fixed, height * 0.5f, mid }, { 0.25f, height, segmentLength });
            const Vector3 p0 = at(a, 0.0f, 0.0f), p1 = at(a + segmentLength, 0.0f, 0.0f);
            s.EndPanel({ p0.x, p0.z }, { p1.x, p1.z }, height, painted ? paint : MixColor(kWood, kWoodGrey, 0.5f), 3.0f, -1,
                       (int)s.obstacles.size() - 1);
        }
        post(start + length - 0.07f, height + 0.1f);
        shadowRun(runStart, start + length);
    }

    // Хашаа: дөрвөн талын банзан хашаа, gateFace тал дээр gateAlong координатад 3 м хаалганы зай.
    void Yard(Scene& s, float x0, float z0, float x1, float z1, int gateFace, float gateAlong)
    {
        auto gap = [&](int face, float origin, float& from, float& to) {
            from = to = -1.0f;
            if (face == gateFace)
            {
                from = gateAlong - origin - 1.5f;
                to = gateAlong - origin + 1.5f;
            }
        };

        float from, to;
        gap(0, z0, from, to);
        PlankFence(s, x1, z0, x1, z1, from, to);
        gap(1, z0, from, to);
        PlankFence(s, x0, z0, x0, z1, from, to);
        gap(2, x0, from, to);
        PlankFence(s, x0, z1, x1, z1, from, to);
        gap(3, x0, from, to);
        PlankFence(s, x0, z0, x1, z0, from, to);
    }

    // Торон хашаа: сум, камер нэвтэрнэ, хүн нэвтрэхгүй. Зарим хэсэг нь урагдсан.
    void ChainFence(Scene& s, float x0, float z0, float x1, float z1)
    {
        const bool alongX = std::fabs(x1 - x0) > std::fabs(z1 - z0);
        const float length = alongX ? std::fabs(x1 - x0) : std::fabs(z1 - z0);
        const float start = alongX ? std::min(x0, x1) : std::min(z0, z1);
        const float fixed = alongX ? z0 : x0;
        const int bays = std::max(1, (int)std::round(length / 3.0f));
        const float bay = length / (float)bays;
        const float height = 2.3f;

        auto at = [&](float along, float y) { return alongX ? Vector3{ along, y, fixed } : Vector3{ fixed, y, along }; };

        for (int i = 0; i <= bays; ++i)
        {
            const Vector3 base = at(start + (float)i * bay, 0.0f);
            s.solid.Cylinder(base, 0.05f, 0.05f, height, 5, kMetal);
        }
        for (int i = 0; i < bays; ++i)
        {
            const float a = start + (float)i * bay;
            const float b = a + bay;
            if (s.rng.Chance(0.12f))
            {
                continue; // урагдсан хэсэг: нэвтэрч болно
            }

            s.glass.Quad(at(a, 0.05f), at(b, 0.05f), at(b, height - 0.1f), at(a, height - 0.1f), kChainLink);
            const Vector3 mid = at((a + b) * 0.5f, height - 0.05f);
            s.solid.Box(mid, alongX ? Vector3{ bay, 0.05f, 0.05f } : Vector3{ 0.05f, 0.05f, bay }, kMetal);

            const Vector3 center = at((a + b) * 0.5f, height * 0.5f);
            s.Collider(center, alongX ? Vector3{ bay, height, 0.15f } : Vector3{ 0.15f, height, bay }, false);
        }
    }

    // ------------------------------------------------------------------ Машин

    // Урт тэнхлэг нь X эсвэл Z дагуу байрлах биеийн хэсгүүдийг байрлуулах туслах.
    struct Oriented
    {
        float cx, cz;
        bool alongX;
        float scale = 1.0f; // бүх хэсгийг төвөөс нь жигд томруулна

        Vector3 Pos(float along, float y, float across) const
        {
            along *= scale; y *= scale; across *= scale;
            return alongX ? Vector3{ cx + along, y, cz + across } : Vector3{ cx + across, y, cz + along };
        }
        Vector3 Size(float length, float height, float width) const
        {
            length *= scale; height *= scale; width *= scale;
            return alongX ? Vector3{ length, height, width } : Vector3{ width, height, length };
        }
    };

    void Part(Scene& s, const Oriented& o, float along, float y, float across, float length, float height, float width,
              Color color)
    {
        s.solid.Box(o.Pos(along, y, across), o.Size(length, height, width), color);
    }

    void VehicleBounds(Scene& s, const Oriented& o, float length, float height, float width)
    {
        const Vector3 size = o.Size(length, height, width);
        s.Collider({ o.cx, size.y * 0.5f, o.cz }, size);
        s.Shadow(o.cx - size.x * 0.5f, o.cz - size.z * 0.5f, o.cx + size.x * 0.5f, o.cz + size.z * 0.5f, 0.0f,
                 size.y * 0.9f);
    }

    // Машины орон зай: x = урт тэнхлэг (урагш +), z = хөндлөн. alongX биш бол Z дагуу эргүүлнэ.
    Matrix VehicleMatrix(float cx, float cz, bool alongX, float scale)
    {
        const Matrix turn = alongX ? MatrixIdentity() : MatrixRotateY(-PI * 0.5f);
        return MatrixMultiply(MatrixMultiply(MatrixScale(scale, scale, scale), turn), MatrixTranslate(cx, 0.0f, cz));
    }

    // Хөндлөн (Z) тэнхлэгтэй дугуй: дугуй, обуд, төв. flat: хий нь гарсан (доод тал хавтгай).
    void CarWheel(MeshBuilder& m, float along, float across, float radius, float width, bool flat)
    {
        const float y = flat ? radius * 0.82f : radius;
        const float side = across > 0.0f ? 1.0f : -1.0f;
        m.Tube({ along, y, across - width * 0.5f }, { along, y, across + width * 0.5f }, radius, radius, 14, kRubber);
        m.Tube({ along, y, across + side * width * 0.5f }, { along, y, across + side * (width * 0.5f + 0.02f) }, radius * 0.6f,
               radius * 0.56f, 12, kMetal);
        m.Tube({ along, y, across + side * (width * 0.5f + 0.02f) }, { along, y, across + side * (width * 0.5f + 0.05f) },
               radius * 0.18f, radius * 0.12f, 8, ScaleColor(kMetal, 1.2f));
    }

    // Налуу хана бүхий бүхээг: доод тэгш өнцөгт (belt), дээд тэгш өнцөгт (roof) хоёрын хооронд шил,
    // булангуудад багана, дээр нь дээвэр.
    void Cabin(MeshBuilder& m, float bottomY, float topY, float bottomBack, float bottomFront, float topBack,
               float topFront, float bottomHalf, float topHalf, Color body, Color glass)
    {
        const Vector3 b0 = { bottomBack, bottomY, -bottomHalf }, b1 = { bottomFront, bottomY, -bottomHalf };
        const Vector3 b2 = { bottomFront, bottomY, bottomHalf }, b3 = { bottomBack, bottomY, bottomHalf };
        const Vector3 t0 = { topBack, topY, -topHalf }, t1 = { topFront, topY, -topHalf };
        const Vector3 t2 = { topFront, topY, topHalf }, t3 = { topBack, topY, topHalf };
        m.Quad(b1, b2, t2, t1, glass); // салхины шил
        m.Quad(b3, b0, t0, t3, glass); // арын шил
        m.Quad(b0, b1, t1, t0, glass); // хажуу
        m.Quad(b2, b3, t3, t2, glass);
        m.Quad(t0, t1, t2, t3, body);  // дээвэр
        // Багана: булан бүрд, дунд хаалганы багана
        const float pillar = 0.05f;
        m.Tube(b0, t0, pillar, pillar, 4, body, false);
        m.Tube(b1, t1, pillar, pillar, 4, body, false);
        m.Tube(b2, t2, pillar, pillar, 4, body, false);
        m.Tube(b3, t3, pillar, pillar, 4, body, false);
        const float midBottom = (bottomBack + bottomFront) * 0.5f, midTop = (topBack + topFront) * 0.5f;
        m.Tube({ midBottom, bottomY, -bottomHalf }, { midTop, topY, -topHalf }, pillar, pillar, 4, body, false);
        m.Tube({ midBottom, bottomY, bottomHalf }, { midTop, topY, topHalf }, pillar, pillar, 4, body, false);
        m.BevelBox({ midTop, topY + 0.03f, 0.0f }, { topFront - topBack + 0.08f, 0.06f, topHalf * 2.0f + 0.06f }, 0.03f, body);
    }

    void Car(Scene& s, float cx, float cz, bool alongX, Color paint)
    {
        const Oriented o = { cx, cz, alongX, kCarScale };
        const Color body = JitterColor(paint, s.rng, 0.08f);
        const Color lower = ScaleColor(body, 0.82f);
        MeshBuilder& m = s.solid;
        m.SetTransform(VehicleMatrix(cx, cz, alongX, kCarScale));

        // Их бие: доод хэсэг, хөдөлгүүрийн таг, ачааны тагш
        m.BevelBox({ 0.0f, 0.42f, 0.0f }, { 3.9f, 0.14f, 1.6f }, 0.04f, kRubber);
        m.BevelBox({ 0.0f, 0.68f, 0.0f }, { 4.2f, 0.46f, 1.76f }, 0.1f, body);
        m.BevelBox({ 1.45f, 0.93f, 0.0f }, { 1.3f, 0.1f, 1.66f }, 0.05f, body);
        m.BevelBox({ -1.65f, 0.93f, 0.0f }, { 0.9f, 0.1f, 1.66f }, 0.05f, body);
        m.Box({ 0.0f, 0.7f, 0.885f }, { 3.9f, 0.04f, 0.01f }, lower);
        m.Box({ 0.0f, 0.7f, -0.885f }, { 3.9f, 0.04f, 0.01f }, lower);
        for (float side : { -1.0f, 1.0f })
        {
            m.Box({ 0.35f, 0.72f, side * 0.885f }, { 0.02f, 0.44f, 0.01f }, lower);               // хаалганы зааг
            m.Box({ -0.65f, 0.72f, side * 0.885f }, { 0.02f, 0.44f, 0.01f }, lower);
            m.Box({ 0.15f, 0.84f, side * 0.89f }, { 0.14f, 0.03f, 0.02f }, kMetal);               // бариул
            m.Box({ -0.85f, 0.84f, side * 0.89f }, { 0.14f, 0.03f, 0.02f }, kMetal);
            m.BevelBox({ 0.82f, 1.02f, side * 0.95f }, { 0.12f, 0.08f, 0.1f }, 0.02f, body);      // толь
            for (float along : { -1.35f, 1.35f })
            {
                m.BevelBox({ along, 0.6f, side * 0.86f }, { 0.86f, 0.12f, 0.06f }, 0.03f, lower); // дугуйн нүхний хүрээ
            }
        }
        Cabin(m, 0.98f, 1.42f, -1.3f, 0.85f, -0.95f, 0.3f, 0.82f, 0.7f, body, kGlassDark);
        // Урд: сараалж, гэрэл, бампер; ард: гэрэл, улсын дугаар
        m.BevelBox({ 2.1f, 0.7f, 0.0f }, { 0.06f, 0.18f, 0.8f }, 0.02f, kRubber);
        for (float side : { -1.0f, 1.0f })
        {
            m.BevelBox({ 2.09f, 0.76f, side * 0.62f }, { 0.06f, 0.14f, 0.36f }, 0.03f, kWindowFrame);
            m.BevelBox({ -2.09f, 0.78f, side * 0.66f }, { 0.06f, 0.14f, 0.3f }, 0.03f, Color{ 150, 30, 26, 255 });
        }
        m.BevelBox({ 2.14f, 0.5f, 0.0f }, { 0.14f, 0.18f, 1.78f }, 0.05f, kMetal);
        m.BevelBox({ -2.14f, 0.5f, 0.0f }, { 0.14f, 0.18f, 1.78f }, 0.05f, kMetal);
        m.Box({ -2.12f, 0.62f, 0.0f }, { 0.03f, 0.12f, 0.4f }, Color{ 220, 220, 214, 255 });
        // Зэв: орхигдсон машин
        m.Box({ s.rng.Range(-1.5f, 1.5f), 0.7f, 0.89f }, { 0.9f, 0.26f, 0.01f }, kRust);
        m.Box({ s.rng.Range(-1.5f, 1.5f), 0.7f, -0.89f }, { 0.7f, 0.22f, 0.01f }, kRust);
        // Дугуй: заримынх нь хий гарсан
        for (float along : { -1.35f, 1.35f })
        {
            for (float across : { -0.78f, 0.78f })
            {
                CarWheel(m, along, across, 0.33f, 0.24f, s.rng.Chance(0.3f));
            }
        }
        m.ResetTransform();

        VehicleBounds(s, o, 4.3f, 1.5f, 1.8f);
    }

    void Truck(Scene& s, float cx, float cz, bool alongX, Color cabColor, Color cargoColor)
    {
        const Oriented o = { cx, cz, alongX, 1.1f };
        const Color cab = JitterColor(cabColor, s.rng, 0.06f);
        const Color cabDark = ScaleColor(cab, 0.78f);
        const Color cargo = JitterColor(cargoColor, s.rng, 0.06f);
        MeshBuilder& m = s.solid;
        m.SetTransform(VehicleMatrix(cx, cz, alongX, 1.1f));

        m.BevelBox({ 0.0f, 0.85f, 0.0f }, { 7.0f, 0.26f, 1.2f }, 0.04f, ScaleColor(kMetal, 0.5f)); // рам
        // Урт хамартай бүхээг (ЗИЛ, ГАЗ маягийн): хамар, дугуйн хаалт, бүхээг, сараалж, гэрэл
        m.BevelBox({ 3.05f, 1.42f, 0.0f }, { 1.3f, 0.75f, 1.3f }, 0.12f, cab);
        for (float side : { -1.0f, 1.0f })
        {
            m.BevelBox({ 2.95f, 1.12f, side * 0.92f }, { 1.4f, 0.16f, 0.5f }, 0.07f, cabDark);    // хаалт
            m.BevelBox({ 3.5f, 0.98f, side * 0.92f }, { 0.4f, 0.28f, 0.5f }, 0.07f, cabDark);
            m.Tube({ 3.62f, 1.45f, side * 0.95f }, { 3.72f, 1.45f, side * 0.95f }, 0.13f, 0.13f, 10, kMetal);
            m.Tube({ 3.72f, 1.45f, side * 0.95f }, { 3.73f, 1.45f, side * 0.95f }, 0.1f, 0.1f, 10, kWindowFrame);
            m.Tube({ 1.9f, 1.4f, side * 1.08f }, { 1.9f, 2.6f, side * 1.08f }, 0.025f, 0.025f, 4, kMetal, false); // толины иш
            m.BevelBox({ 1.9f, 2.5f, side * 1.14f }, { 0.08f, 0.32f, 0.2f }, 0.02f, kRubber);
        }
        m.BevelBox({ 3.72f, 1.45f, 0.0f }, { 0.06f, 0.6f, 0.9f }, 0.02f, kRubber);
        for (int i = 0; i < 7; ++i)
        {
            m.Box({ 3.76f, 1.45f, -0.36f + 0.12f * (float)i }, { 0.03f, 0.56f, 0.04f }, cabDark);
        }
        m.BevelBox({ 3.8f, 0.95f, 0.0f }, { 0.16f, 0.22f, 2.3f }, 0.05f, kMetal);                 // бампер
        m.BevelBox({ 1.85f, 1.65f, 0.0f }, { 1.5f, 1.2f, 2.2f }, 0.1f, cab);                      // бүхээг
        m.BevelBox({ 1.85f, 2.3f, 0.0f }, { 1.56f, 0.12f, 2.26f }, 0.05f, cabDark);
        m.Box({ 2.61f, 2.0f, 0.0f }, { 0.02f, 0.55f, 1.9f }, kGlassDark);                         // салхины шил
        m.Box({ 2.61f, 2.0f, 0.0f }, { 0.03f, 0.55f, 0.06f }, cab);
        for (float side : { -1.0f, 1.0f })
        {
            m.Box({ 1.95f, 2.0f, side * 1.105f }, { 0.75f, 0.5f, 0.02f }, kGlassDark);
            m.Box({ 1.55f, 1.5f, side * 1.105f }, { 0.02f, 0.9f, 0.01f }, cabDark);
            m.Box({ 1.4f, 1.35f, side * 1.11f }, { 0.12f, 0.04f, 0.02f }, kMetal);
            m.Tube({ 1.0f, 0.75f, side * 0.55f }, { 1.0f, 0.75f, side * 0.85f }, 0.22f, 0.22f, 12, ScaleColor(kMetal, 0.8f)); // түлшний сав
        }
        // Тэвш: банзан хана, дээрээ дугуйрсан брезент бүрхүүл, хавирга
        m.BevelBox({ -1.05f, 1.12f, 0.0f }, { 5.0f, 0.16f, 2.42f }, 0.04f, ScaleColor(cargo, 0.65f));
        for (float side : { -1.0f, 1.0f })
        {
            for (int k = 0; k < 3; ++k)
            {
                m.BevelBox({ -1.05f, 1.3f + 0.17f * (float)k, side * 1.19f }, { 5.0f, 0.14f, 0.05f }, 0.02f,
                           ScaleColor(cargo, 0.8f));
            }
        }
        m.BevelBox({ -1.05f, 2.45f, 0.0f }, { 4.9f, 1.55f, 2.36f }, 0.4f, cargo);
        for (int i = 0; i < 5; ++i)
        {
            // Хавирга: бүрхүүлийн дээд тал, хоёр хажуугаар гүйх тууз
            const float x = -3.2f + 1.07f * (float)i;
            const Color rib = ScaleColor(cargo, 0.82f);
            m.Box({ x, 3.225f, 0.0f }, { 0.07f, 0.02f, 1.5f }, rib);
            m.Box({ x, 2.4f, 1.185f }, { 0.07f, 1.2f, 0.02f }, rib);
            m.Box({ x, 2.4f, -1.185f }, { 0.07f, 1.2f, 0.02f }, rib);
        }
        m.Box({ -3.56f, 1.28f, 0.0f }, { 0.04f, 0.5f, 2.3f }, ScaleColor(cargo, 0.7f));
        for (float side : { -1.0f, 1.0f })
        {
            m.Box({ -3.58f, 1.0f, side * 0.95f }, { 0.04f, 0.12f, 0.2f }, Color{ 150, 30, 26, 255 });
        }
        // Дугуй: урд дан, ард хос
        for (float side : { -1.0f, 1.0f })
        {
            CarWheel(m, 2.9f, side * 0.95f, 0.5f, 0.32f, s.rng.Chance(0.25f));
            CarWheel(m, -1.0f, side * 0.95f, 0.5f, 0.34f, false);
            CarWheel(m, -2.4f, side * 0.95f, 0.5f, 0.34f, s.rng.Chance(0.25f));
        }
        m.ResetTransform();

        VehicleBounds(s, o, 7.3f, 3.3f, 2.5f);
    }

    // ------------------------------------------------------------------ Зам дагуух зүйлс

    // state: 0 = босоо, 1 = хазайсан, 2 = унасан
    void LightPole(Scene& s, float x, float z, int state)
    {
        if (state == 2)
        {
            s.solid.Cylinder({ x, 0.0f, z }, 0.14f, 0.12f, 0.9f, 6, kMetal);
            s.solid.Box({ x + 3.7f, 0.14f, z + 0.9f }, { 7.0f, 0.2f, 0.2f }, kMetal);
            s.solid.Box({ x + 7.3f, 0.2f, z + 0.9f }, { 0.6f, 0.14f, 0.34f }, kWindowFrame);
            s.Collider({ x, 0.45f, z }, { 0.35f, 0.9f, 0.35f });
            return;
        }

        if (state == 1)
        {
            s.solid.SetTransform(MatrixMultiply(MatrixRotateZ(-13.0f * DEG2RAD), MatrixTranslate(x, 0.0f, z)));
        }
        else
        {
            s.solid.SetTransform(MatrixTranslate(x, 0.0f, z));
        }
        s.solid.Cylinder({ 0.0f, 0.0f, 0.0f }, 0.14f, 0.09f, 7.5f, 6, kMetal);
        s.solid.Box({ 0.0f, 7.4f, -0.9f }, { 0.1f, 0.1f, 1.9f }, kMetal);                 // гар (зам руу)
        s.solid.Box({ 0.0f, 7.32f, -1.8f }, { 0.34f, 0.14f, 0.6f }, kWindowFrame);        // чийдэн
        s.solid.ResetTransform();

        s.Collider({ x, 3.75f, z }, { 0.35f, 7.5f, 0.35f });
        s.Shadow(x - 0.1f, z - 0.1f, x + 0.1f, z + 0.1f, 0.0f, 7.5f);
    }

    // kind: 0 = суурингийн нэрийн самбар, 1 = анхааруулах, 2 = зогс
    void RoadSign(Scene& s, float x, float z, int kind, float tiltDegrees)
    {
        const Matrix place = MatrixMultiply(MatrixRotateZ(tiltDegrees * DEG2RAD), MatrixTranslate(x, 0.0f, z));
        s.solid.SetTransform(place);
        s.solid.Cylinder({ 0.0f, 0.0f, 0.0f }, 0.05f, 0.05f, 2.6f, 5, kMetal);

        if (kind == 0)
        {
            s.solid.Box({ 0.0f, 2.2f, 0.0f }, { 0.06f, 0.7f, 1.7f }, { 232, 232, 226, 255 });
            s.solid.Box({ 0.0f, 2.2f, 0.0f }, { 0.08f, 0.56f, 1.56f }, { 40, 84, 140, 255 });
        }
        else if (kind == 1)
        {
            // Ромбо: хавтанг өөрийн тэнхлэгээр 45 градус эргүүлнэ
            const Matrix diamond = MatrixMultiply(MatrixRotateX(45.0f * DEG2RAD), MatrixTranslate(0.0f, 2.2f, 0.0f));
            s.solid.SetTransform(MatrixMultiply(diamond, place));
            s.solid.Box({ 0.0f, 0.0f, 0.0f }, { 0.06f, 0.8f, 0.8f }, { 30, 30, 30, 255 });
            s.solid.Box({ 0.0f, 0.0f, 0.0f }, { 0.08f, 0.68f, 0.68f }, { 214, 176, 40, 255 });
        }
        else
        {
            s.solid.Box({ 0.0f, 2.2f, 0.0f }, { 0.06f, 0.8f, 0.8f }, { 170, 40, 36, 255 });
            s.solid.Box({ 0.0f, 2.2f, 0.0f }, { 0.08f, 0.14f, 0.6f }, { 232, 232, 226, 255 });
        }
        s.solid.ResetTransform();

        s.Collider({ x, 1.3f, z }, { 0.25f, 2.6f, 0.25f });
        s.Shadow(x - 0.05f, z - 0.4f, x + 0.05f, z + 0.4f, 1.8f, 2.6f);
    }

    // ------------------------------------------------------------------ Цэргийн пост

    // Тэнхлэг дагуух элсэн уутан хана.
    void Sandbags(Scene& s, float x0, float z0, float x1, float z1, int layers)
    {
        const bool alongX = std::fabs(x1 - x0) > std::fabs(z1 - z0);
        const float length = alongX ? std::fabs(x1 - x0) : std::fabs(z1 - z0);
        const float start = alongX ? std::min(x0, x1) : std::min(z0, z1);
        const float fixed = alongX ? z0 : x0;
        const float bagLength = 0.7f;
        const float bagHeight = 0.25f;

        for (int layer = 0; layer < layers; ++layer)
        {
            const float offset = (layer % 2) * bagLength * 0.5f; // тоосго шиг зөрүүлж өрнө
            for (float along = start + offset; along + bagLength <= start + length + 0.01f; along += bagLength)
            {
                const float y = ((float)layer + 0.5f) * bagHeight;
                const float wobble = s.rng.Range(-0.03f, 0.03f);
                const Color color = JitterColor(kSandbag, s.rng, 0.1f);
                const float mid = along + bagLength * 0.5f;
                if (alongX) s.solid.Box({ mid, y, fixed + wobble }, { bagLength - 0.04f, bagHeight - 0.02f, 0.45f }, color);
                else s.solid.Box({ fixed + wobble, y, mid }, { 0.45f, bagHeight - 0.02f, bagLength - 0.04f }, color);
            }
        }

        const float height = (float)layers * bagHeight;
        const float mid = start + length * 0.5f;
        if (alongX)
        {
            s.Collider({ mid, height * 0.5f, fixed }, { length, height, 0.5f });
            s.Shadow(start, fixed - 0.22f, start + length, fixed + 0.22f, 0.0f, height);
        }
        else
        {
            s.Collider({ fixed, height * 0.5f, mid }, { 0.5f, height, length });
            s.Shadow(fixed - 0.22f, start, fixed + 0.22f, start + length, 0.0f, height);
        }
    }

    // Гурван талтай элсэн уутан хоргодох байр. open: +1 бол +Z тал нь нээлттэй, -1 бол -Z.
    void SandbagNest(Scene& s, float x0, float x1, float zWall, float depth, int open)
    {
        const float zEnd = zWall + depth * (float)open;
        Sandbags(s, x0, zWall, x1, zWall, 4);
        Sandbags(s, x0, zWall, x0, zEnd, 3);
        Sandbags(s, x1, zWall, x1, zEnd, 3);
    }

    void JerseyBarrier(Scene& s, float cx, float cz, bool alongX)
    {
        const Oriented o = { cx, cz, alongX };
        const Color color = JitterColor(kConcrete, s.rng, 0.06f);
        Part(s, o, 0.0f, 0.25f, 0.0f, 2.4f, 0.5f, 0.6f, color);
        Part(s, o, 0.0f, 0.7f, 0.0f, 2.4f, 0.4f, 0.3f, color);
        Part(s, o, 0.0f, 0.7f, 0.0f, 0.5f, 0.42f, 0.32f, { 170, 60, 44, 255 }); // улаан тэмдэг

        const Vector3 size = o.Size(2.4f, 0.9f, 0.6f);
        s.Collider({ cx, 0.45f, cz }, size);
        s.Shadow(cx - size.x * 0.5f, cz - size.z * 0.5f, cx + size.x * 0.5f, cz + size.z * 0.5f, 0.0f, 0.9f);
    }

    void Watchtower(Scene& s, float cx, float cz)
    {
        const float half = 1.3f;
        const float deck = 4.2f;
        const Color timber = ScaleColor(kWood, 0.8f);

        for (float dx : { -half, half })
        {
            for (float dz : { -half, half })
            {
                s.solid.Box({ cx + dx, 3.2f, cz + dz }, { 0.22f, 6.4f, 0.22f }, timber);
                s.Collider({ cx + dx, deck * 0.5f, cz + dz }, { 0.35f, deck, 0.35f });
                s.Shadow(cx + dx - 0.1f, cz + dz - 0.1f, cx + dx + 0.1f, cz + dz + 0.1f, 0.0f, deck);
            }
        }
        // Хөндлөн бэхэлгээ
        s.solid.Box({ cx, 2.0f, cz - half }, { half * 2.0f, 0.12f, 0.12f }, timber);
        s.solid.Box({ cx, 2.0f, cz + half }, { half * 2.0f, 0.12f, 0.12f }, timber);
        s.solid.Box({ cx - half, 2.0f, cz }, { 0.12f, 0.12f, half * 2.0f }, timber);
        s.solid.Box({ cx + half, 2.0f, cz }, { 0.12f, 0.12f, half * 2.0f }, timber);

        // Тавцан: дээр нь зогсож болно. Хашлага нь унахаас хамгаална (шатны талд онгорхой).
        s.solid.Box({ cx, deck, cz }, { 3.2f, 0.2f, 3.2f }, timber);
        s.Collider({ cx, deck, cz }, { 3.2f, 0.2f, 3.2f });
        const struct { Vector3 center, size; } rails[] = {
            { { cx, deck + 0.6f, cz - 1.55f }, { 3.2f, 1.0f, 0.1f } },
            { { cx, deck + 0.6f, cz + 1.55f }, { 3.2f, 1.0f, 0.1f } },
            { { cx + 1.55f, deck + 0.6f, cz }, { 0.1f, 1.0f, 3.2f } },
            { { cx - 1.55f, deck + 0.6f, cz + 0.8f }, { 0.1f, 1.0f, 1.6f } }, // шатны талд онгорхой
        };
        for (const auto& rail : rails)
        {
            s.solid.Box(rail.center, rail.size, kOlive);
            s.Collider(rail.center, { rail.size.x + 0.06f, rail.size.y, rail.size.z + 0.06f });
        }
        s.solid.GableRoof({ cx, 6.4f, cz }, 3.9f, 3.9f, 0.9f, true, JitterColor(kRust, s.rng, 0.08f));

        // Шат: хоёр хажуугийн мод, гишгүүрүүд. F товчоор авирч, бууна.
        const float ladderX = cx - 1.72f, ladderZ = cz - 0.75f;
        s.solid.Box({ ladderX, (deck + 0.9f) * 0.5f, ladderZ - 0.25f }, { 0.07f, deck + 0.9f, 0.07f }, timber);
        s.solid.Box({ ladderX, (deck + 0.9f) * 0.5f, ladderZ + 0.25f }, { 0.07f, deck + 0.9f, 0.07f }, timber);
        for (float y = 0.35f; y < deck + 0.2f; y += 0.35f)
        {
            s.solid.Box({ ladderX, y, ladderZ }, { 0.05f, 0.05f, 0.5f }, timber);
        }
        // Авирагч шатнаас kGap зайд босоо дээшилнэ (гар нь гишгүүрт хүрнэ); wall = доод цэгээс шат хүртэлх зай.
        const float kGap = 0.42f;
        const float ground = s.terrain.HeightAt(ladderX - kGap, ladderZ);
        const Vector3 bottom = { ladderX - kGap, ground, ladderZ };
        const Vector3 top = { cx - 0.9f, deck + 0.1f, ladderZ };
        s.passages.push_back({ bottom, top, PassageKind::LadderUp, PI * 0.5f, kGap });   // +X зүг (цамхаг руу) харна
        s.passages.push_back({ top, bottom, PassageKind::LadderDown, PI * 0.5f, kGap });

        s.Shadow(cx - 1.6f, cz - 1.6f, cx + 1.6f, cz + 1.6f, deck, deck + 1.2f);
        s.Shadow(cx - 1.95f, cz - 1.95f, cx + 1.95f, cz + 1.95f, 6.4f, 6.9f);
    }

    void GuardBooth(Scene& s, float cx, float cz, Color wall = { 196, 192, 180, 255 }, Color stripe = { 160, 52, 40, 255 })
    {
        const float w = 2.2f, h = 2.5f;
        s.solid.Box({ cx, h * 0.5f, cz }, { w, h, w }, wall);
        s.solid.Box({ cx, 1.0f, cz }, { w + 0.04f, 0.3f, w + 0.04f }, stripe); // өнгөт зурвас
        s.solid.Box({ cx, h + 0.08f, cz }, { w + 0.6f, 0.16f, w + 0.6f }, ScaleColor(kMetal, 0.7f));
        FacePanel(s, cx, cz, w, w, 3, 0.0f, 1.75f, 1.3f, 0.8f, 0.1f, kWindow);
        FacePanel(s, cx, cz, w, w, 0, 0.0f, 1.0f, 0.8f, 1.9f, 0.1f, kDoor);
        s.Collider({ cx, h * 0.5f, cz }, { w, h, w });
        s.Shadow(cx - w * 0.5f - 0.3f, cz - w * 0.5f - 0.3f, cx + w * 0.5f + 0.3f, cz + w * 0.5f + 0.3f, 0.0f, h);
    }

    // Өргөгдсөн хаалт: тулгуур нь замын хойд талд, гар нь зам дээгүүр.
    void BarrierArm(Scene& s, float x, float z)
    {
        s.solid.Box({ x, 0.55f, z }, { 0.3f, 1.1f, 0.3f }, { 196, 192, 180, 255 });
        s.Collider({ x, 0.55f, z }, { 0.3f, 1.1f, 0.3f });

        s.solid.SetTransform(MatrixMultiply(MatrixRotateX(55.0f * DEG2RAD), MatrixTranslate(x, 1.0f, z)));
        for (int i = 0; i < 6; ++i)
        {
            const Color color = i % 2 == 0 ? Color{ 176, 48, 40, 255 } : Color{ 226, 224, 216, 255 };
            s.solid.Box({ 0.0f, 0.0f, -0.45f - (float)i * 0.9f }, { 0.12f, 0.12f, 0.9f }, color);
        }
        s.solid.ResetTransform();
    }

    void Tent(Scene& s, float cx, float cz, Color canvas = kOlive)
    {
        const float w = 4.2f, d = 6.5f;
        s.solid.GableRoof({ cx, 0.0f, cz }, w, d, 2.3f, false, JitterColor(canvas, s.rng, 0.08f));
        s.solid.Box({ cx, 0.9f, cz - d * 0.5f - 0.03f }, { 1.0f, 1.8f, 0.05f }, ScaleColor(canvas, 0.45f)); // үүд
        s.Collider({ cx, 0.9f, cz }, { 3.0f, 1.8f, d });
        s.Shadow(cx - w * 0.5f, cz - d * 0.5f, cx + w * 0.5f, cz + d * 0.5f, 0.0f, 1.5f);
    }

    void Flagpole(Scene& s, float x, float z)
    {
        s.solid.Cylinder({ x, 0.0f, z }, 0.06f, 0.04f, 7.0f, 6, kWindowFrame);
        // Төрийн далбаа: улаан, хөх, улаан
        const Color red = { 190, 44, 48, 255 };
        const Color blue = { 20, 96, 166, 255 };
        s.solid.Box({ x + 0.35f, 6.4f, z }, { 0.6f, 0.95f, 0.03f }, red);
        s.solid.Box({ x + 0.95f, 6.4f, z }, { 0.6f, 0.95f, 0.03f }, blue);
        s.solid.Box({ x + 1.55f, 6.4f, z }, { 0.6f, 0.95f, 0.03f }, red);
        s.Collider({ x, 3.5f, z }, { 0.25f, 7.0f, 0.25f });
        s.Shadow(x - 0.05f, z - 0.05f, x + 0.05f, z + 0.05f, 0.0f, 7.0f);
    }

    // Дэлгүүр (Layout.h: kShopCenter): 2 давхар байшинтай ижил суурьтай, өндөр нэг давхар, цонхгүй.
    // +Z талд өргөн хаалгатай, дээр нь шар хаяг. Дотор нь лангуу, тавиурууд. Зомби дотогш орохгүй (Interior::safe).
    void ShopBuilding(Scene& s, float cx, float cz)
    {
        const float w = 9.0f, d = 7.5f;
        const float wallHeight = 3.8f, thick = 0.2f;
        const float doorWidth = 1.6f, doorHeight = 2.5f;
        const Color wall = { 214, 200, 170, 255 };
        const Color trim = { 150, 40, 36, 255 };
        const Color sign = { 240, 190, 40, 255 };

        auto wallPiece = [&](int face, float from, float to, float y0, float y1, bool collide) {
            const float mid = (from + to) * 0.5f, length = to - from, inset = thick * 0.5f;
            Vector3 center, size;
            switch (face)
            {
            case 0: center = { cx + w * 0.5f - inset, (y0 + y1) * 0.5f, cz + mid }; size = { thick, y1 - y0, length }; break;
            case 1: center = { cx - w * 0.5f + inset, (y0 + y1) * 0.5f, cz + mid }; size = { thick, y1 - y0, length }; break;
            case 2: center = { cx + mid, (y0 + y1) * 0.5f, cz + d * 0.5f - inset }; size = { length, y1 - y0, thick }; break;
            default: center = { cx + mid, (y0 + y1) * 0.5f, cz - d * 0.5f + inset }; size = { length, y1 - y0, thick }; break;
            }
            s.solid.Box(center, size, wall);
            if (collide) s.Collider(center, size);
        };

        s.solid.Box({ cx, 0.04f, cz }, { w + 0.3f, 0.08f, d + 0.3f }, kConcrete);                  // суурь
        s.solid.Box({ cx, 0.09f, cz }, { w - thick, 0.03f, d - thick }, { 120, 116, 108, 255 }); // шал

        // Хана: гурван тал цул, +Z талд голдоо хаалганы нээлхий
        wallPiece(0, -d * 0.5f, d * 0.5f, 0.0f, wallHeight, true);
        wallPiece(1, -d * 0.5f, d * 0.5f, 0.0f, wallHeight, true);
        wallPiece(3, -w * 0.5f, w * 0.5f, 0.0f, wallHeight, true);
        wallPiece(2, -w * 0.5f, -doorWidth * 0.5f, 0.0f, wallHeight, true);
        wallPiece(2, doorWidth * 0.5f, w * 0.5f, 0.0f, wallHeight, true);
        wallPiece(2, -doorWidth * 0.5f, doorWidth * 0.5f, doorHeight, wallHeight, false);

        // Хаалга ба дотор тал (safe: зомби орохгүй)
        const float wallZ = cz + d * 0.5f - thick * 0.5f;
        Door leaf = {};
        leaf.hinge = { cx - doorWidth * 0.5f, 0.05f, wallZ };
        leaf.width = doorWidth;
        leaf.height = doorHeight - 0.05f;
        leaf.closedYaw = PI * 0.5f;
        leaf.openYaw = 0.0f; // гадагш (+Z)
        leaf.color = { 70, 90, 110, 255 };
        Interior room = {};
        room.center = { cx, cz };
        room.halfX = w * 0.5f - thick;
        room.halfZ = d * 0.5f - thick;
        room.round = false;
        room.doorOutside = { cx, wallZ + 1.5f };
        room.doorInside = { cx, wallZ - 1.2f };
        room.safe = true;
        s.Entrance(leaf, room);

        // Гадна тал: доод бараан тууз, дээд улаан тууз, хаалганы дээрх том шар хаяг (зоосны дүрстэй)
        const float front = cz + d * 0.5f + 0.03f;
        const Color plinth = ScaleColor(wall, 0.6f);
        s.solid.Box({ cx, 0.2f, front }, { w + 0.08f, 0.4f, 0.06f }, plinth);
        s.solid.Box({ cx, 0.2f, cz - d * 0.5f - 0.03f }, { w + 0.08f, 0.4f, 0.06f }, plinth);
        s.solid.Box({ cx - w * 0.5f - 0.03f, 0.2f, cz }, { 0.06f, 0.4f, d + 0.08f }, plinth);
        s.solid.Box({ cx + w * 0.5f + 0.03f, 0.2f, cz }, { 0.06f, 0.4f, d + 0.08f }, plinth);
        s.solid.Box({ cx, wallHeight - 0.25f, front }, { w + 0.1f, 0.3f, 0.06f }, trim);
        s.solid.Box({ cx, wallHeight - 0.25f, cz - d * 0.5f - 0.03f }, { w + 0.1f, 0.3f, 0.06f }, trim);
        s.solid.Box({ cx - w * 0.5f - 0.03f, wallHeight - 0.25f, cz }, { 0.06f, 0.3f, d + 0.1f }, trim);
        s.solid.Box({ cx + w * 0.5f + 0.03f, wallHeight - 0.25f, cz }, { 0.06f, 0.3f, d + 0.1f }, trim);
        s.solid.Box({ cx, wallHeight + 0.55f, front + 0.05f }, { 5.2f, 1.1f, 0.14f }, sign);
        s.solid.Box({ cx, wallHeight + 0.55f, front + 0.13f }, { 5.0f, 0.12f, 0.04f }, trim);
        for (float x : { -1.8f, 1.8f })
        {
            // Зоосны дүрс: хаяган дээр наасан хоёр алтан дөрвөлжин (дотор нь бараан тэмдэг)
            s.solid.Box({ cx + x, wallHeight + 0.55f, front + 0.14f }, { 0.7f, 0.7f, 0.04f }, { 200, 140, 20, 255 });
            s.solid.Box({ cx + x, wallHeight + 0.55f, front + 0.17f }, { 0.12f, 0.45f, 0.02f }, { 120, 80, 10, 255 });
        }
        // Хаалганы дээрх саравч
        s.solid.Box({ cx, doorHeight + 0.25f, front + 0.6f }, { doorWidth + 1.2f, 0.1f, 1.2f }, trim);

        // Хавтгай дээвэр, хашлага
        const float roofBase = wallHeight;
        const Color parapet = ScaleColor(wall, 0.88f);
        s.solid.Box({ cx, roofBase + 0.12f, cz }, { w + 0.5f, 0.24f, d + 0.5f }, { 96, 96, 100, 255 });
        s.solid.Box({ cx, roofBase + 0.44f, cz - d * 0.5f - 0.15f }, { w + 0.5f, 0.4f, 0.2f }, parapet);
        s.solid.Box({ cx - w * 0.5f - 0.15f, roofBase + 0.44f, cz }, { 0.2f, 0.4f, d + 0.5f }, parapet);
        s.solid.Box({ cx + w * 0.5f + 0.15f, roofBase + 0.44f, cz }, { 0.2f, 0.4f, d + 0.5f }, parapet);
        s.solid.Box({ cx, roofBase - 0.03f, cz }, { w - thick, 0.05f, d - thick }, { 230, 228, 220, 255 }); // тааз

        // --- Дотор: арын хананы дагуу лангуу, ард нь тавиур; хажуугийн хананд зэвсгийн тавиур, эмийн шүүгээ ---
        const float back = cz - d * 0.5f + thick;
        const Color wood = { 120, 84, 52, 255 };
        Furniture(s, { cx, 0.55f, back + 1.7f }, { 4.4f, 1.1f, 0.7f }, wood);                        // лангуу
        s.solid.Box({ cx, 1.12f, back + 1.7f }, { 4.5f, 0.05f, 0.8f }, ScaleColor(wood, 1.2f));
        s.solid.Box({ cx - 1.4f, 1.24f, back + 1.7f }, { 0.4f, 0.2f, 0.3f }, { 40, 40, 44, 255 });    // касс
        s.solid.Box({ cx + 1.2f, 1.2f, back + 1.65f }, { 0.5f, 0.12f, 0.35f }, { 200, 140, 20, 255 }); // зоосны сав
        Furniture(s, { cx, 1.2f, back + 0.25f }, { 6.0f, 2.4f, 0.45f }, ScaleColor(wood, 0.8f));       // тавиур
        for (int row = 0; row < 3; ++row)
        {
            for (int i = 0; i < 7; ++i)
            {
                const int k = (i + row) % 3;
                const Color box = k == 0 ? kOlive : k == 1 ? Color{ 150, 120, 70, 255 } : Color{ 90, 96, 70, 255 };
                s.solid.Box({ cx - 2.55f + 0.85f * (float)i, 0.55f + 0.7f * (float)row, back + 0.5f }, { 0.6f, 0.4f, 0.12f }, box);
            }
        }
        // Зэвсгийн тавиур (-X хана): бараан буунууд босоо
        const float left = cx - w * 0.5f + thick;
        Furniture(s, { left + 0.2f, 1.1f, cz }, { 0.3f, 2.2f, 3.0f }, ScaleColor(wood, 0.7f));
        for (int i = 0; i < 6; ++i)
        {
            s.solid.Box({ left + 0.4f, 1.15f, cz - 1.2f + 0.48f * (float)i }, { 0.08f, 1.1f, 0.12f }, { 40, 42, 40, 255 });
            s.solid.Box({ left + 0.42f, 0.7f, cz - 1.2f + 0.48f * (float)i }, { 0.1f, 0.25f, 0.2f }, { 70, 50, 34, 255 });
        }
        // Эмийн шүүгээ (+X хана): цагаан, улаан загалмайтай
        const float right = cx + w * 0.5f - thick;
        Furniture(s, { right - 0.3f, 1.0f, cz }, { 0.5f, 2.0f, 2.2f }, { 236, 236, 230, 255 });
        s.solid.Box({ right - 0.56f, 1.4f, cz }, { 0.02f, 0.5f, 0.15f }, { 210, 30, 36, 255 });
        s.solid.Box({ right - 0.56f, 1.4f, cz }, { 0.02f, 0.15f, 0.5f }, { 210, 30, 36, 255 });
        // Таазны гэрэл
        for (float x : { -2.0f, 2.0f }) s.solid.Box({ cx + x, wallHeight - 0.12f, cz }, { 1.2f, 0.06f, 0.2f }, { 250, 246, 220, 255 });

        s.Collider({ cx, roofBase + 0.5f, cz }, { w, 1.0f, d });
        Footprint(s, cx, cz, w, d);
        s.Shadow(cx - w * 0.5f - 0.25f, cz - d * 0.5f - 0.25f, cx + w * 0.5f + 0.25f, cz + d * 0.5f + 0.25f, 0.0f, roofBase + 1.2f);
    }

    // ------------------------------------------------------------------ Жижиг зүйлс

    void Crate(Scene& s, float cx, float cz, float size, float baseY, Color color)
    {
        const Vector3 center = { cx, baseY + size * 0.5f, cz };
        s.solid.Box(center, { size, size, size }, JitterColor(color, s.rng, 0.08f));
        s.solid.Box({ cx, baseY + size - 0.03f, cz }, { size + 0.04f, 0.06f, size + 0.04f }, ScaleColor(color, 0.7f));
        s.solid.Box({ cx, baseY + 0.03f, cz }, { size + 0.04f, 0.06f, size + 0.04f }, ScaleColor(color, 0.7f));
        s.Collider(center, { size, size, size });
        s.Shadow(cx - size * 0.5f, cz - size * 0.5f, cx + size * 0.5f, cz + size * 0.5f, baseY, baseY + size);
    }

    void Barrel(Scene& s, float cx, float cz, Color color)
    {
        s.solid.Cylinder({ cx, 0.0f, cz }, 0.32f, 0.32f, 0.95f, 8, JitterColor(color, s.rng, 0.1f));
        s.solid.Cylinder({ cx, 0.3f, cz }, 0.34f, 0.34f, 0.05f, 8, ScaleColor(color, 0.6f), false);
        s.solid.Cylinder({ cx, 0.62f, cz }, 0.34f, 0.34f, 0.05f, 8, ScaleColor(color, 0.6f), false);
        s.Collider({ cx, 0.475f, cz }, { 0.6f, 0.95f, 0.6f });
        s.Shadow(cx - 0.28f, cz - 0.28f, cx + 0.28f, cz + 0.28f, 0.0f, 0.95f);
    }

    // ------------------------------------------------------------------ Том барилгууд

    void Warehouse(Scene& s, float cx, float cz)
    {
        const float w = 16.0f, d = 10.0f, h = 5.0f;
        // Аймаг бүрт өөр өнгөтэй төмөр хавтан
        static const Color walls[] = { { 98, 114, 124, 255 }, { 120, 132, 96, 255 }, { 150, 120, 90, 255 },
                                       { 170, 166, 156, 255 }, { 84, 104, 140, 255 }, { 140, 80, 64, 255 } };
        const Color wall = walls[s.variant % 6];

        s.solid.Box({ cx, h * 0.5f, cz }, { w, h, d }, wall);
        s.solid.Box({ cx, 0.45f, cz }, { w + 0.08f, 0.9f, d + 0.08f }, ScaleColor(wall, 0.7f));
        // Долгионт төмрийн хавтангийн босоо зураас
        for (float x = cx - w * 0.5f + 2.0f; x < cx + w * 0.5f; x += 2.0f)
        {
            s.solid.Box({ x, h * 0.5f + 0.45f, cz }, { 0.08f, h - 0.9f, d + 0.06f }, ScaleColor(wall, 0.85f));
        }
        s.solid.GableRoof({ cx, h, cz }, w + 0.8f, d + 0.8f, 1.8f, true,
                          JitterColor(s.variant % 2 == 0 ? kRust : Color{ 96, 100, 108, 255 }, s.rng, 0.06f));

        // Зам руу харсан (+X) том гүйдэг хаалга, хагас онгорхой
        FacePanel(s, cx, cz, w, d, 0, -0.6f, 1.95f, 3.4f, 3.9f, 0.14f, JitterColor(kRust, s.rng, 0.08f));
        FacePanel(s, cx, cz, w, d, 0, 1.8f, 1.95f, 1.3f, 3.9f, 0.1f, { 18, 18, 20, 255 });
        for (float along : { -5.5f, -2.0f, 2.0f, 5.5f })
        {
            FacePanel(s, cx, cz, w, d, 2, along, 4.0f, 1.4f, 0.6f, 0.1f, kWindow);
            FacePanel(s, cx, cz, w, d, 3, along, 4.0f, 1.4f, 0.6f, 0.1f, kWindow);
        }

        s.Collider({ cx, h * 0.5f, cz }, { w, h, d });
        s.Shadow(cx - w * 0.5f - 0.4f, cz - d * 0.5f - 0.4f, cx + w * 0.5f + 0.4f, cz + d * 0.5f + 0.4f, 0.0f, h + 0.9f);

        // Хаалганы дэргэдэх ачаа: хайрцаг дээр үсэрч гарч болно
        const float doorX = cx + w * 0.5f;
        Crate(s, doorX + 1.6f, cz - 3.8f, 1.0f, 0.0f, kWood);
        Crate(s, doorX + 2.8f, cz - 3.9f, 1.0f, 0.0f, kWood);
        Crate(s, doorX + 2.2f, cz - 3.85f, 1.0f, 1.0f, kWood);
        Crate(s, doorX + 1.4f, cz + 4.2f, 1.2f, 0.0f, kWoodGrey);
        Barrel(s, doorX + 3.0f, cz + 3.6f, { 60, 96, 130, 255 });
        Barrel(s, doorX + 3.8f, cz + 4.0f, kRust);
        Barrel(s, doorX + 3.3f, cz + 4.5f, { 60, 96, 130, 255 });
    }

    // Засмал замын урд талын шатахуун түгээх газар ба дэлгүүр.
    // ox, oz: бүх байгууламжийг шилжүүлэх зөрүү (Улаанбаатарын жинхэнэ ШТС дээр барихад)
    // style: брэнд ба түгээгүүрийн тоо (-1 = аймгийн дугаараар)
    void GasStation(Scene& s, float ox = 0.0f, float oz = 0.0f, int style = -1)
    {
        const int look = style >= 0 ? style : s.variant;
        // Аймаг бүрт өөр брэндийн өнгө (гол, туслах), дэлгүүрийн хана, түгээгүүрийн тоо
        struct Brand { Color main, accent, wall; };
        static const Brand brands[] = {
            { { 164, 52, 42, 255 }, { 214, 176, 40, 255 }, { 178, 172, 158, 255 } },  // улаан, шар
            { { 40, 120, 70, 255 }, { 230, 230, 220, 255 }, { 200, 204, 196, 255 } },  // ногоон, цагаан
            { { 36, 76, 150, 255 }, { 236, 140, 40, 255 }, { 186, 190, 196, 255 } },   // хөх, улбар шар
            { { 90, 56, 130, 255 }, { 230, 200, 60, 255 }, { 196, 186, 170, 255 } },   // нил ягаан, шар
            { { 30, 40, 60, 255 }, { 200, 50, 50, 255 }, { 170, 172, 176, 255 } },     // хар хөх, улаан
        };
        const Brand& brand = brands[look % 5];

        // Дэлгүүр
        const float cx = 44.0f + ox, cz = -14.0f + oz, w = 10.0f, d = 5.0f, h = 3.2f;
        s.solid.Box({ cx, h * 0.5f, cz }, { w, h, d }, brand.wall);
        s.solid.Box({ cx, 0.3f, cz }, { w + 0.06f, 0.6f, d + 0.06f }, ScaleColor(brand.wall, 0.68f));
        s.solid.Box({ cx, h + 0.12f, cz }, { w + 0.7f, 0.24f, d + 0.7f }, ScaleColor(kMetal, 0.6f));
        s.solid.Box({ cx, h - 0.3f, cz + d * 0.5f + 0.02f }, { w, 0.3f, 0.04f }, brand.main);           // фасадын тууз
        s.solid.Box({ cx, h + 0.85f, cz + d * 0.5f }, { 5.0f, 1.0f, 0.2f }, brand.main);               // хаягийн самбар
        s.solid.Box({ cx, h + 0.85f, cz + d * 0.5f + 0.02f }, { 4.4f, 0.22f, 0.2f }, brand.accent);
        FacePanel(s, cx, cz, w, d, 2, -3.2f, 1.05f, 1.0f, 2.1f, 0.12f, kDoor);
        Window(s, cx, cz, w, d, 2, -0.6f, 1.7f, 2.2f, 1.3f);
        Window(s, cx, cz, w, d, 2, 2.8f, 1.7f, 2.2f, 1.3f);
        s.Collider({ cx, h * 0.5f, cz }, { w, h, d });
        s.Shadow(cx - w * 0.5f - 0.35f, cz - d * 0.5f - 0.35f, cx + w * 0.5f + 0.35f, cz + d * 0.5f + 0.35f, 0.0f, h + 0.2f);

        // Саравч ба түгээгүүр
        const float px = 30.0f + ox, pz = -11.5f + oz;
        for (float dx : { -3.2f, 3.2f })
        {
            for (float dz : { -2.0f, 2.0f })
            {
                s.Block({ px + dx, 2.0f, pz + dz }, { 0.25f, 4.0f, 0.25f }, kWindowFrame);
            }
        }
        s.solid.Box({ px, 4.2f, pz }, { 8.4f, 0.4f, 5.6f }, { 206, 200, 188, 255 });
        s.solid.Box({ px, 4.2f, pz }, { 8.5f, 0.2f, 5.7f }, brand.main);
        s.solid.Box({ px, 4.42f, pz }, { 8.5f, 0.06f, 5.7f }, brand.accent);
        s.Shadow(px - 4.2f, pz - 2.8f, px + 4.2f, pz + 2.8f, 4.0f, 4.4f);

        // Түгээгүүр: 2 эсвэл 3, брэндийн өнгөтэй, хоолой, дэлгэцтэй
        const int pumps = 2 + look % 2;
        s.solid.Box({ px, 0.08f, pz }, { 1.0f + 2.4f * (float)pumps, 0.16f, 1.1f }, kConcrete);
        for (int i = 0; i < pumps; ++i)
        {
            const float dx = ((float)i - (float)(pumps - 1) * 0.5f) * 2.6f;
            s.Block({ px + dx, 0.9f, pz }, { 0.6f, 1.5f, 0.42f }, brand.main);
            s.solid.Box({ px + dx, 1.25f, pz }, { 0.44f, 0.4f, 0.46f }, kWindow);
            s.solid.Box({ px + dx, 1.62f, pz }, { 0.62f, 0.12f, 0.44f }, brand.accent);
            s.solid.Box({ px + dx + 0.34f, 0.8f, pz }, { 0.06f, 0.6f, 0.06f }, { 30, 30, 32, 255 }); // хоолой
        }
        // Үнийн самбар (зам дагуу өндөр шон)
        const float sx = 22.5f + ox, sz = -6.8f + oz;
        s.solid.Box({ sx, 2.6f, sz }, { 0.3f, 5.2f, 0.3f }, kMetal);
        s.solid.Box({ sx, 5.3f, sz }, { 2.2f, 1.6f, 0.3f }, brand.main);
        s.solid.Box({ sx, 5.75f, sz }, { 2.0f, 0.4f, 0.32f }, brand.accent);
        for (int k = 0; k < 2; ++k)
        {
            s.solid.Box({ sx, 4.9f + 0.36f * (float)k, sz }, { 1.6f, 0.24f, 0.34f }, { 20, 20, 24, 255 });
        }
        s.Collider({ sx, 2.6f, sz }, { 0.4f, 5.2f, 0.4f });
        s.Shadow(sx - 1.1f, sz - 0.15f, sx + 1.1f, sz + 0.15f, 4.5f, 6.1f);
        Barrel(s, 36.5f + ox, -15.5f + oz, kRust);
        Barrel(s, 37.3f + ox, -15.9f + oz, { 60, 96, 130, 255 });
    }

    // ------------------------------------------------------------------ Бүсүүд

    // Төв суурингийн тогтмол хэсэг: агуулах, шатахуун түгээх газар, нэрийн самбар.
    // Хашаа байшингууд нь үе бүрд өөрөөр үүснэ (BuildTownLots).
    void BuildTownFixed(Scene& s)
    {
        Warehouse(s, 10.0f, 70.0f);
        GasStation(s);
        RoadSign(s, 18.5f, 5.6f, 0, 4.0f);
    }

    // Танк саатуулагч "зараа": гурван хөндлөн төмөр
    void Hedgehog(Scene& s, float cx, float cz)
    {
        const Color steel = { 70, 66, 62, 255 };
        for (int i = 0; i < 3; ++i)
        {
            const Matrix rot = i == 0 ? MatrixRotateZ(45.0f * DEG2RAD)
                             : i == 1 ? MatrixRotateX(45.0f * DEG2RAD)
                                      : MatrixMultiply(MatrixRotateZ(-45.0f * DEG2RAD), MatrixRotateY(45.0f * DEG2RAD));
            s.solid.SetTransform(MatrixMultiply(rot, MatrixTranslate(cx, 0.6f, cz)));
            s.solid.Box({ 0.0f, 0.0f, 0.0f }, i == 1 ? Vector3{ 0.12f, 0.12f, 1.6f } : Vector3{ 1.6f, 0.12f, 0.12f },
                        JitterColor(steel, s.rng, 0.1f));
        }
        s.solid.ResetTransform();
        s.Collider({ cx, 0.55f, cz }, { 1.1f, 1.1f, 1.1f });
        s.Shadow(cx - 0.55f, cz - 0.55f, cx + 0.55f, cz + 0.55f, 0.0f, 1.1f);
    }

    void BuildCheckpoint(Scene& s)
    {
        // Аймаг бүрт өөр хэв маягтай застав: 0 = цэргийн пост, 1 = цагдаагийн шалган нэвтрүүлэх, 2 = хилийн застав
        const int style = s.variant % 3;

        // Замыг могойрон гарах бетон хаалтууд
        JerseyBarrier(s, -44.0f, -2.5f, false);
        JerseyBarrier(s, -50.0f, 2.5f, false);
        JerseyBarrier(s, -56.0f, -2.5f, false);

        if (style == 1)
        {
            // Цагдаа: цагаан хөх бүхээг, элсэн уутны оронд бетон хаалтууд, эргүүлийн машин
            GuardBooth(s, -53.0f, 7.4f, { 226, 228, 230, 255 }, { 36, 76, 150, 255 });
            for (float x : { -58.0f, -61.0f }) JerseyBarrier(s, x, 7.0f, true);
            for (float x : { -44.0f, -47.0f }) JerseyBarrier(s, x, -7.0f, true);
            Car(s, -38.5f, 9.5f, true, { 226, 228, 230, 255 });
            s.solid.Box({ -38.5f, 1.56f, 9.5f }, { 0.3f, 0.12f, 1.2f }, { 40, 70, 200, 255 });   // гэрлэн дохио
        }
        else
        {
            // Замын хоёр талын элсэн уутан хоргодох байр
            SandbagNest(s, -49.0f, -43.0f, 5.4f, 2.8f, +1);
            SandbagNest(s, -60.0f, -54.0f, -5.4f, 2.8f, -1);
            GuardBooth(s, -53.0f, 7.4f, style == 2 ? Color{ 120, 128, 100, 255 } : Color{ 196, 192, 180, 255 },
                       style == 2 ? Color{ 60, 120, 70, 255 } : Color{ 160, 52, 40, 255 });
            Watchtower(s, -60.5f, 10.5f);
        }
        if (style == 2)
        {
            // Хил: хоёр дахь харуулын цамхаг, замын хажуугаар танк саатуулагч зараа
            Watchtower(s, -36.0f, -24.0f);
            for (float x : { -40.0f, -43.5f, -60.0f, -63.0f }) Hedgehog(s, x, 9.0f);
            for (float x : { -40.0f, -36.5f }) Hedgehog(s, x, -8.5f);
        }
        BarrierArm(s, -51.2f, 4.7f);
        Flagpole(s, -56.5f, 13.0f);

        // Торон хашаа: замын хоёр талаар хөндлөн татсан
        ChainFence(s, -64.0f, 5.6f, -64.0f, 29.0f);
        ChainFence(s, -64.0f, 29.0f, -43.0f, 29.0f);
        ChainFence(s, -64.0f, -29.0f, -64.0f, -5.6f);
        ChainFence(s, -64.0f, -29.0f, -43.0f, -29.0f);

        // Хуаран
        ShopBuilding(s, kShopCenter.x, kShopCenter.y);
        const Color canvas = style == 1 ? Color{ 92, 104, 120, 255 } : style == 2 ? Color{ 98, 94, 70, 255 } : kOlive;
        Tent(s, -49.5f, -17.0f, canvas);
        Tent(s, -43.0f, -17.0f, canvas);
        Crate(s, -55.5f, -13.0f, 0.9f, 0.0f, kOlive);
        Crate(s, -56.5f, -13.2f, 0.9f, 0.0f, kOlive);
        Crate(s, -56.0f, -13.1f, 0.9f, 0.9f, kOlive);
        Barrel(s, -58.5f, -15.0f, kOlive);
        Barrel(s, -59.2f, -15.6f, kOlive);
        Truck(s, -34.0f, -8.2f, true, kOlive, kCanvas);

        RoadSign(s, -37.0f, 5.4f, 2, -3.0f);
        RoadSign(s, -22.0f, -5.4f, 1, 9.0f);
    }

    // Засмал замын төв шугамын z координат (x-ээр).
    float HighwayZ(const Layout& layout, float x)
    {
        const std::vector<Vector2>& points = layout.roads[0].points;
        for (size_t i = 0; i + 1 < points.size(); ++i)
        {
            if (x <= points[i + 1].x)
            {
                const float t = (x - points[i].x) / (points[i + 1].x - points[i].x);
                return points[i].y + (points[i + 1].y - points[i].y) * std::clamp(t, 0.0f, 1.0f);
            }
        }
        return points.back().y;
    }

    void BuildHighwayProps(Scene& s)
    {
        // Гэрэлтүүлгийн шон: замын хойд талаар жигд зайтай, зарим нь хазайсан эсвэл унасан
        int index = 0;
        for (float x = -184.0f; x <= 530.0f; x += 38.0f, ++index)
        {
            const int state = index % 5 == 3 ? 2 : (index % 4 == 1 ? 1 : 0);
            LightPole(s, x, HighwayZ(s.layout, x) + 6.4f, state);
        }

        // Орхигдсон машинууд
        Car(s, 10.0f, -1.9f, true, { 138, 62, 50, 255 });
        Car(s, 58.0f, 1.0f, false, { 186, 182, 170, 255 });                       // замд хөндлөн
        Truck(s, -14.0f, 2.1f, true, { 70, 98, 126, 255 }, { 132, 126, 110, 255 });
        Car(s, 96.0f, HighwayZ(s.layout, 96.0f) - 1.9f, true, { 96, 104, 96, 255 });
        Car(s, -112.0f, HighwayZ(s.layout, -112.0f) + 1.8f, true, { 58, 70, 96, 255 });
        Car(s, 150.0f, HighwayZ(s.layout, 150.0f) + 5.6f, true, { 150, 120, 60, 255 }); // замаас гарсан
        Car(s, 262.0f, HighwayZ(s.layout, 262.0f) - 1.8f, true, { 120, 60, 54, 255 });
        Truck(s, 340.0f, HighwayZ(s.layout, 340.0f) + 2.0f, true, { 132, 120, 70, 255 }, { 110, 112, 116, 255 });
        Car(s, 430.0f, HighwayZ(s.layout, 430.0f) + 1.6f, false, { 70, 84, 100, 255 });
        Car(s, -165.0f, HighwayZ(s.layout, -165.0f) - 1.9f, true, { 164, 160, 150, 255 });

        RoadSign(s, 78.0f, HighwayZ(s.layout, 78.0f) - 5.4f, 1, -6.0f);
        RoadSign(s, -128.0f, HighwayZ(s.layout, -128.0f) - 5.4f, 0, 5.0f);
        RoadSign(s, 228.0f, HighwayZ(s.layout, 228.0f) - 5.6f, 1, -8.0f);

        // Төвөөс хол: засмал замын дагуу санамсаргүй зайтай орхигдсон машин, ачааны машин
        const Color paints[] = { { 138, 62, 50, 255 }, { 186, 182, 170, 255 }, { 96, 104, 96, 255 }, { 58, 70, 96, 255 },
                                 { 150, 120, 60, 255 }, { 70, 84, 100, 255 } };
        Rng rng(31415u);
        for (float x = -kPlayableHalfExtent; x < kPlayableHalfExtent; x += rng.Range(120.0f, 260.0f))
        {
            if (std::fabs(x) < 520.0f) continue; // төв хэсгийг дээр гараар байрлуулсан
            // Машин X тэнхлэгийн дагуу байрлана: зам огцом эргэсэн хэсэгт тавихгүй
            if (std::fabs(HighwayZ(s.layout, x + 6.0f) - HighwayZ(s.layout, x - 6.0f)) > 12.0f * 0.45f) continue;

            const float side = rng.Chance(0.5f) ? 1.9f : -1.9f;
            const float z = HighwayZ(s.layout, x) + (rng.Chance(0.2f) ? side * 3.0f : side); // зарим нь замаас гарсан
            if (rng.Chance(0.2f))
            {
                Truck(s, x, z, true, paints[rng.Int(0, 5)], { 120, 116, 104, 255 });
            }
            else
            {
                Car(s, x, z, !rng.Chance(0.15f), paints[rng.Int(0, 5)]);
            }
        }
    }

    // ------------------------------------------------------------------ Үе бүрд өөрөөр үүсэх суурингууд

    Color RandomGerDoor(Rng& rng)
    {
        static const Color doors[] = { { 186, 96, 40, 255 }, { 58, 104, 150, 255 }, { 70, 120, 84, 255 },
                                       { 150, 50, 44, 255 } };
        return doors[rng.Int(0, 3)];
    }

    Color RandomCarPaint(Rng& rng)
    {
        static const Color paints[] = { { 138, 62, 50, 255 }, { 186, 182, 170, 255 }, { 96, 104, 96, 255 },
                                        { 58, 70, 96, 255 },  { 150, 120, 60, 255 },  { 110, 122, 134, 255 } };
        return paints[rng.Int(0, 5)];
    }

    // Нэг хашаа (эсвэл хашаагүй талбай). Гол барилга (байшин эсвэл гэр) арын буланд, хоёр дахь зүйл
    // (гэр, жижиг байшин, амбаар, машин) урд талын нөгөө буланд; хэмжээ, загвар нь санамсаргүй.
    // gateFace: зам аль талд байгаа (0 = +X, 1 = -X); хашааны хаалга, байшингийн хаалга тийш харна.
    void BuildLot(Scene& s, float x0, float z0, float x1, float z1, int gateFace)
    {
        const bool fenced = s.rng.Chance(0.82f);
        if (fenced)
        {
            Yard(s, x0, z0, x1, z1, gateFace, z0 + (z1 - z0) * 0.3f);
        }

        const float margin = 2.4f;
        const float ix0 = x0 + margin, ix1 = x1 - margin, iz0 = z0 + margin, iz1 = z1 - margin;
        const float innerWidth = ix1 - ix0, innerDepth = iz1 - iz0;
        if (innerWidth < 7.0f || innerDepth < 7.0f)
        {
            return;
        }

        const float toFront = gateFace == 0 ? 1.0f : -1.0f; // арын захаас зам руу чиглэсэн X чиглэл
        const float backX = gateFace == 0 ? ix0 : ix1;
        const float frontX = gateFace == 0 ? ix1 : ix0;

        // --- Гол барилга: арын (замаас хол) доод буланд ---
        float aHalfX = 0.0f, aHalfZ = 0.0f;
        if (s.rng.Chance(0.35f))
        {
            const float limit = std::min(innerWidth - 3.0f, innerDepth) * 0.5f;
            const float radius = std::min(s.rng.Range(2.3f, 3.0f) * kGerScale, limit);
            aHalfX = aHalfZ = radius;
            Ger(s, backX + toFront * radius, iz0 + radius, radius / kGerScale, RandomGerDoor(s.rng));
        }
        else
        {
            const float width = std::min(s.rng.Range(5.6f, 8.6f) * kHouseScale, innerWidth - 3.0f);
            const float depth = std::min(s.rng.Range(4.8f, 6.6f) * kHouseScale, innerDepth);
            aHalfX = width * 0.5f;
            aHalfZ = depth * 0.5f;
            House(s, backX + toFront * aHalfX, iz0 + aHalfZ, width / kHouseScale, depth / kHouseScale,
                  RegionHouseStyle(s), gateFace);
        }

        // --- Хоёр дахь зүйл: урд талын дээд буланд, гол барилгатай давхцахгүй бол ---
        auto fits = [&](float halfX, float halfZ) {
            return aHalfX * 2.0f + halfX * 2.0f + 1.2f <= innerWidth || aHalfZ * 2.0f + halfZ * 2.0f + 1.2f <= innerDepth;
        };
        const int choice = s.rng.Int(0, 4);
        const float gerRadius = s.rng.Range(2.3f, 2.8f) * kGerScale;
        const float smallWidth = s.rng.Range(4.6f, 5.6f) * kHouseScale, smallDepth = s.rng.Range(4.4f, 5.2f) * kHouseScale;

        if (choice == 0 && fits(gerRadius, gerRadius))
        {
            Ger(s, frontX - toFront * gerRadius, iz1 - gerRadius, gerRadius / kGerScale, RandomGerDoor(s.rng));
        }
        else if (choice == 1 && fits(smallWidth * 0.5f, smallDepth * 0.5f))
        {
            // Жижиг байшин: хаалга нь хашаан дотогш (-Z) харна
            HouseStyle style = RegionHouseStyle(s);
            style.floors = 1;
            House(s, frontX - toFront * smallWidth * 0.5f, iz1 - smallDepth * 0.5f, smallWidth / kHouseScale,
                  smallDepth / kHouseScale, style, 3);
        }
        else if (choice == 2 && fits(1.3f, 2.7f))
        {
            Car(s, frontX - toFront * 1.3f, iz1 - 2.7f, false, RandomCarPaint(s.rng));
            Crate(s, frontX - toFront * 3.4f, iz1 - 0.8f, 0.9f, 0.0f, kWood);
        }
        else if (fits(1.3f, 1.2f))
        {
            Shed(s, frontX - toFront * 1.3f, iz1 - 1.2f, s.rng.Range(2.0f, 2.6f), s.rng.Range(1.8f, 2.4f), 2.2f);
        }

        // Арын дээд буланд жижиг зүйлс (гол барилга тэр хүртэл сунаагүй бол)
        if (aHalfZ * 2.0f + 2.6f <= innerDepth)
        {
            if (s.rng.Chance(0.5f)) Barrel(s, backX + toFront * 0.6f, iz1 - 0.6f, s.rng.Chance(0.5f) ? kRust : Color{ 60, 96, 130, 255 });
            if (s.rng.Chance(0.4f)) Crate(s, backX + toFront * 1.9f, iz1 - 0.7f, 0.9f, 0.0f, kWoodGrey);
        }
    }

    // Замын нэг талын хашаануудын багана: x0..x1 өргөнтэй, z = 9..57 мужийг rows эгнээнд хуваана.
    void LotColumn(Scene& s, float x0, float x1, int gateFace, int rows)
    {
        const float gap = 3.5f;
        float z = 9.0f;
        float remaining = 48.0f - gap * (float)(rows - 1);
        for (int row = 0; row < rows; ++row)
        {
            const int left = rows - row;
            const float depth = left == 1 ? remaining : remaining / (float)left * s.rng.Range(0.85f, 1.15f);
            BuildLot(s, x0, z, x1, z + depth, gateFace);
            z += depth + gap;
            remaining -= depth;
        }
    }

    // Гудамж дагуу хашаагүй зэрэгцсэн байшингууд (ихэнх нь 2 давхар): сумын төвийн гудамж.
    // facadeX: хаалгатай нүүрэн талын X; doorFace: 0 = +X, 1 = -X руу харна.
    void StreetRow(Scene& s, float facadeX, float z0, float z1, int doorFace)
    {
        float z = z0;
        for (;;)
        {
            HouseStyle style = RegionHouseStyle(s);
            style.floors = s.rng.Chance(0.7f) ? 2 : 1;
            const float width = s.rng.Range(5.0f, 6.4f) * kHouseScale; // X дагуу
            const float depth = s.rng.Range(5.4f, 8.0f) * kHouseScale; // гудамж дагуу (Z)
            if (z + depth > z1)
            {
                break;
            }
            const float cx = doorFace == 0 ? facadeX - width * 0.5f : facadeX + width * 0.5f;
            House(s, cx, z + depth * 0.5f, width / kHouseScale, depth / kHouseScale, style, doorFace);
            z += depth + s.rng.Range(2.5f, 5.0f);
        }
    }

    // Төв суурин: шороон замын (x ~ 25) хоёр талаар. Зохион байгуулалт нь үе бүрд гурван төлөвлөгөөний нэг,
    // доторх хашаа, байшингийн тоо, хэмжээ, загвар нь мөн үе бүрд өөр.
    void BuildTownLots(Scene& s, int variant)
    {
        switch (variant % 3)
        {
        case 0: // замын хоёр талаар нэг нэг эгнээ том хашаа
            LotColumn(s, -2.0f, 20.0f, 0, s.rng.Int(2, 3));
            LotColumn(s, 33.0f, 57.0f, 1, s.rng.Int(2, 3));
            break;

        case 1: // хоёр эгнээ гүн: замын дагуух хашаануудын ард гадагшаа харсан хоёр дахь эгнээ
            LotColumn(s, -2.0f, 20.0f, 0, s.rng.Int(2, 3));
            LotColumn(s, -24.0f, -5.0f, 1, s.rng.Int(2, 3));
            LotColumn(s, 33.0f, 55.0f, 1, s.rng.Int(2, 3));
            LotColumn(s, 58.0f, 78.0f, 0, s.rng.Int(2, 3));
            break;

        default: // төв гудамж: баруун талд хашаагүй зэрэгцсэн байшингууд, цаана нь хашаанууд
            StreetRow(s, 20.0f, 9.0f, 57.0f, 0);
            LotColumn(s, -24.0f, 8.0f, 1, 2);
            LotColumn(s, 33.0f, 57.0f, 1, s.rng.Int(2, 3));
            StreetRow(s, 60.0f, 9.0f, 57.0f, 1);
            break;
        }

        // Гудамжинд орхигдсон машин
        Car(s, 27.4f + s.rng.Range(-0.5f, 0.5f), s.rng.Range(36.0f, 53.0f), false, RandomCarPaint(s.rng));
    }

    // Жижиг суурин: гурван янзын зохион байгуулалтын нэг.
    void Hamlet(Scene& s, float x, float z)
    {
        switch (s.rng.Int(0, 2))
        {
        case 0: // хойд, урд хоёр том хашаа
            BuildLot(s, x - 22.0f, z + 3.0f, x + 20.0f, z + 26.0f, 1);
            BuildLot(s, x - 22.0f, z - 26.0f, x + 20.0f, z - 3.0f, 1);
            break;

        case 1: // баруун талд хоёр, тэдний цаана гудамжаар орох гурав дахь хашаа
            BuildLot(s, x - 22.0f, z + 4.0f, x + 2.0f, z + 26.0f, 1);
            BuildLot(s, x - 22.0f, z - 26.0f, x + 2.0f, z - 4.0f, 1);
            BuildLot(s, x + 5.0f, z - 12.0f, x + 21.0f, z + 12.0f, 1);
            break;

        default: // гурван эгнээ нарийн хашаа
            BuildLot(s, x - 22.0f, z + 10.0f, x + 20.0f, z + 26.0f, 1);
            BuildLot(s, x - 22.0f, z - 8.0f, x + 20.0f, z + 7.0f, 1);
            BuildLot(s, x - 22.0f, z - 26.0f, x + 20.0f, z - 11.0f, 1);
            break;
        }

        // Суурингийн урд гудамжинд орхигдсон машин (заримд нь)
        if (s.rng.Chance(0.6f))
        {
            Car(s, x - 30.0f, z + s.rng.Range(-14.0f, 14.0f), s.rng.Chance(0.5f), RandomCarPaint(s.rng));
        }
    }

    void BuildHamlets(Scene& s)
    {
        for (const Vector2& hamlet : s.layout.hamlets)
        {
            Hamlet(s, hamlet.x, hamlet.y);
        }
    }

    // Малчны отор: дөрвөн янзын зохион байгуулалтын нэг.
    void HerderCamp(Scene& s, float x, float z)
    {
        switch (s.rng.Int(0, 3))
        {
        case 0: // хоёр гэр, зүүн талд малын хашаа
            Ger(s, x - 6.0f, z + 4.0f, 2.8f, RandomGerDoor(s.rng));
            Ger(s, x + 2.0f, z + 7.0f, 2.5f, RandomGerDoor(s.rng));
            Yard(s, x + 3.0f, z - 12.0f, x + 17.0f, z - 1.0f, 3, x + 10.0f);
            Shed(s, x - 11.0f, z - 6.0f, 2.4f, 2.2f, 2.1f);
            Barrel(s, x - 8.4f, z - 6.4f, kRust);
            break;

        case 1: // ганц том гэр, баруун талд малын хашаа
            Ger(s, x, z + 6.0f, 3.0f, RandomGerDoor(s.rng));
            Yard(s, x - 18.0f, z - 12.0f, x - 5.0f, z - 1.0f, 3, x - 11.0f);
            Shed(s, x + 9.0f, z - 5.0f, 2.2f, 2.4f, 2.1f);
            Crate(s, x + 6.2f, z - 5.0f, 0.9f, 0.0f, kWood);
            Crate(s, x + 6.3f, z - 6.1f, 0.9f, 0.0f, kWoodGrey);
            break;

        case 2: // гурван гэр нум хэлбэрээр, хашаагүй
            Ger(s, x - 9.0f, z + 3.0f, 2.5f, RandomGerDoor(s.rng));
            Ger(s, x, z + 9.0f, 2.7f, RandomGerDoor(s.rng));
            Ger(s, x + 9.0f, z + 3.0f, 2.4f, RandomGerDoor(s.rng));
            Barrel(s, x + 5.0f, z - 4.0f, kRust);
            Barrel(s, x + 5.8f, z - 4.5f, { 60, 96, 130, 255 });
            break;

        default: // гэр ба өвөлжөөний жижиг байшин, зүүн талд хашаа
            Ger(s, x - 6.0f, z + 6.0f, 2.6f, RandomGerDoor(s.rng));
            {
                HouseStyle style = RegionHouseStyle(s);
                style.floors = 1;
                House(s, x + 6.0f, z + 7.0f, 4.8f, 4.4f, style, 3);
            }
            Yard(s, x + 5.0f, z - 12.0f, x + 17.0f, z - 2.0f, 3, x + 11.0f);
            Shed(s, x - 12.0f, z - 4.0f, 2.2f, 2.0f, 2.1f);
            break;
        }
    }

    void BuildHerderCamps(Scene& s)
    {
        for (const Vector2& camp : s.layout.camps)
        {
            HerderCamp(s, camp.x, camp.y);
        }
    }

    // ------------------------------------------------------------------ Улаанбаатар

    // Аяны үеийн дугаар (Regions.cpp-ийн аймгуудын дараалал дахь Улаанбаатарын индекс).
    const int kUlaanbaatarStage = 15;

    const Color kBronze = { 64, 76, 66, 255 };
    const Color kGranite = { 146, 100, 90, 255 };
    const Color kPaving = { 166, 162, 154, 255 };

    // Өндөр барилгын сүүдэр. Урт тул газрын хотгор гүдгэрийг дагуулж жижиг дөрвөлжнүүдэд хуваана;
    // барилгын суурийн доорх хэсгийг зурахгүй.
    void TallShadow(Scene& s, float x0, float z0, float x1, float z1, float height)
    {
        const float dx = height * kShadowX, dz = height * kShadowZ;
        auto at = [&](float x, float z) { return Vector3{ x, s.terrain.HeightAt(x, z) + kYShadow, z }; };

        // (ax, az)-(bx, bz) ирмэгийг сүүдрийн чиглэлд шүүрдсэн параллелограмм
        auto sweep = [&](float ax, float az, float bx, float bz) {
            const int across = std::max(1, (int)std::ceil((std::fabs(bx - ax) + std::fabs(bz - az)) / 6.0f));
            const int along = std::max(1, (int)std::ceil(height / 6.0f));
            auto point = [&](int i, int j) {
                const float u = (float)i / (float)across, t = (float)j / (float)along;
                return at(ax + (bx - ax) * u + dx * t, az + (bz - az) * u + dz * t);
            };
            for (int i = 0; i < across; ++i)
            {
                for (int j = 0; j < along; ++j)
                {
                    s.shadows.Quad(point(i, j), point(i + 1, j), point(i + 1, j + 1), point(i, j + 1), kShadow);
                }
            }
        };
        sweep(x1, z1, x1, z0);
        sweep(x0, z1, x1, z1);
    }

    // Олон давхар угсармал орон сууцны байр. Дотогш орохгүй: бүхэлдээ нэг цул саад.
    // Налуу газарт суурь нь газар доош сунаж, давхрууд хамгийн өндөр булангаас эхэлнэ.
    // entranceFace: орцны хаалгууд байрлах тал (0 = +X, 1 = -X, 2 = +Z, 3 = -Z).
    void ApartmentBlock(Scene& s, float cx, float cz, float w, float d, int floors, Color wall, int entranceFace)
    {
        const float floorHeight = 3.0f;
        float low = 1.0e9f, ground = -1.0e9f;
        for (float sx : { -0.5f, 0.0f, 0.5f })
        {
            for (float sz : { -0.5f, 0.0f, 0.5f })
            {
                const float h = s.terrain.HeightAt(cx + sx * w, cz + sz * d);
                low = std::min(low, h);
                ground = std::max(ground, h);
            }
        }
        const float bottom = low - 1.0f;
        const float top = ground + (float)floors * floorHeight + 0.5f;
        const Color body = JitterColor(wall, s.rng, 0.04f);
        const Color trim = ScaleColor(body, 0.84f);

        s.solid.Box({ cx, (bottom + top) * 0.5f, cz }, { w, top - bottom, d }, body);
        s.solid.Box({ cx, (bottom + ground + 0.8f) * 0.5f, cz }, { w + 0.16f, ground + 0.8f - bottom, d + 0.16f },
                    ScaleColor(body, 0.6f));                                                         // суурь
        s.solid.Box({ cx, top + 0.2f, cz }, { w + 0.3f, 0.4f, d + 0.3f }, ScaleColor(body, 0.74f)); // дээврийн хүрээ
        s.solid.Box({ cx + (w >= d ? w * 0.22f : 0.0f), top + 1.3f, cz + (w >= d ? 0.0f : d * 0.22f) },
                    { 3.2f, 1.8f, 3.2f }, ScaleColor(body, 0.68f));                                  // лифтний өрөө

        for (int floor = 0; floor < floors; ++floor)
        {
            const float base = ground + (float)floor * floorHeight;
            s.solid.Box({ cx, base + floorHeight, cz }, { w + 0.06f, 0.09f, d + 0.06f }, trim); // хавтангийн зааг

            for (int face = 0; face < 4; ++face)
            {
                const float span = face < 2 ? d : w;
                const int count = std::max(1, (int)((span - 1.2f) / 3.2f));
                const float pitch = span / (float)count;
                const bool longFace = span >= std::max(w, d) - 0.01f;
                for (int i = 0; i < count; ++i)
                {
                    const float along = -span * 0.5f + pitch * ((float)i + 0.5f);
                    const bool entrance = floor == 0 && face == entranceFace && i % 3 == 1;
                    if (entrance)
                    {
                        // Орцны хаалга ба саравч
                        FacePanel(s, cx, cz, w, d, face, along, base + 1.15f, 1.5f, 2.3f, 0.12f, ScaleColor(kMetal, 0.5f));
                        FacePanel(s, cx, cz, w, d, face, along, base + 2.55f, 2.6f, 0.16f, 2.2f, trim);
                        continue;
                    }

                    Color pane = kWindow;
                    const float roll = s.rng.Next();
                    if (roll < 0.1f) pane = JitterColor(kWood, s.rng, 0.1f);           // банзаар хадсан
                    else if (roll < 0.2f) pane = { 146, 148, 132, 255 };               // хөшигтэй
                    else if (roll < 0.27f) pane = { 14, 14, 16, 255 };                 // хагарсан, харанхуй
                    FacePanel(s, cx, cz, w, d, face, along, base + 1.8f, 1.5f, 1.4f, 0.1f, pane);

                    // Тагт: урт талуудын гурав дахь багана бүрд, 2-р давхраас дээш
                    if (longFace && floor > 0 && i % 3 == 0)
                    {
                        FacePanel(s, cx, cz, w, d, face, along, base + 0.55f, 2.5f, 1.1f, 1.7f, trim);
                    }
                }
            }
        }

        s.Collider({ cx, (bottom + top) * 0.5f, cz }, { w, top - bottom, d });
        TallShadow(s, cx - w * 0.5f, cz - d * 0.5f, cx + w * 0.5f, cz + d * 0.5f, top - ground);
    }

    // Нэг цэгт (x, y, z) суурьтай, yaw өнцгөөр эргэсэн, scale дахин томруулсан хэсгүүдийг нэмэх хувиргалт.
    void PlaceFigure(Scene& s, float x, float y, float z, float yaw, float scale)
    {
        s.solid.SetTransform(MatrixMultiply(MatrixScale(scale, scale, scale),
                                            MatrixMultiply(MatrixRotateY(yaw), MatrixTranslate(x, y, z))));
    }

    // Сүхбаатарын морьт хөшөө: гурван шатлалт суурь дээр, гараа өргөсөн морьтон. -Z зүг рүү харна.
    void HorseRider(Scene& s, float x, float y, float z, float yaw, float scale);

    void SukhbaatarStatue(Scene& s, float x, float z)
    {
        s.solid.Box({ x, 0.25f, z }, { 7.0f, 0.5f, 7.0f }, kConcrete);
        s.solid.Box({ x, 0.8f, z }, { 4.6f, 0.6f, 5.6f }, ScaleColor(kGranite, 0.8f));
        s.solid.Box({ x, 2.9f, z }, { 2.4f, 3.6f, 4.6f }, kGranite);
        s.solid.Box({ x, 4.75f, z }, { 2.7f, 0.2f, 4.9f }, ScaleColor(kGranite, 0.8f));
        s.Collider({ x, 0.25f, z }, { 7.0f, 0.5f, 7.0f });
        s.Collider({ x, 4.5f, z }, { 2.6f, 9.0f, 4.8f });

        // Орон нутгийн тэнхлэгт морь +Z рүү харна; бүтнээр нь эргүүлж -Z рүү харуулна.
        HorseRider(s, x, 4.85f, z, PI, 1.5f);

        s.Shadow(x - 3.5f, z - 3.5f, x + 3.5f, z + 3.5f, 0.0f, 0.5f);
        s.Shadow(x - 1.2f, z - 2.3f, x + 1.2f, z + 2.3f, 0.5f, 9.0f);
    }

    // Хүрэл морьт хөшөө (тавцангүй): морь орон нутгийн +Z рүү харна, scale = 1 бол ~3 м өндөр
    void HorseRider(Scene& s, float x, float y, float z, float yaw, float scale)
    {
        PlaceFigure(s, x, y, z, yaw, scale);
        for (float lx : { -0.28f, 0.28f })
        {
            for (float lz : { -0.85f, 0.85f })
            {
                s.solid.Box({ lx, 0.5f, lz }, { 0.18f, 1.0f, 0.18f }, kBronze); // морины хөл
            }
            s.solid.Box({ lx * 1.55f, 1.3f, 0.1f }, { 0.18f, 0.7f, 0.22f }, kBronze); // морьтны хөл
        }
        s.solid.Box({ 0.0f, 1.35f, 0.0f }, { 0.7f, 0.75f, 2.2f }, kBronze);     // их бие
        s.solid.Box({ 0.0f, 1.95f, 1.0f }, { 0.34f, 0.9f, 0.45f }, kBronze);    // хүзүү
        s.solid.Box({ 0.0f, 2.3f, 1.4f }, { 0.28f, 0.36f, 0.7f }, kBronze);     // толгой
        s.solid.Box({ 0.0f, 1.2f, -1.18f }, { 0.14f, 0.8f, 0.14f }, kBronze);   // сүүл
        s.solid.Box({ 0.0f, 2.15f, -0.05f }, { 0.55f, 0.85f, 0.34f }, kBronze); // морьтны цээж
        s.solid.Box({ 0.0f, 2.74f, -0.05f }, { 0.3f, 0.32f, 0.3f }, kBronze);   // толгой
        s.solid.Cylinder({ 0.0f, 2.9f, -0.05f }, 0.25f, 0.0f, 0.32f, 6, kBronze); // жанжин малгай
        s.solid.Box({ -0.36f, 2.78f, 0.05f }, { 0.14f, 0.75f, 0.14f }, kBronze); // өргөсөн гар
        s.solid.Box({ 0.36f, 2.0f, 0.15f }, { 0.14f, 0.55f, 0.14f }, kBronze);   // жолоо барьсан гар
        s.solid.ResetTransform();
    }

    // Төрийн ордон: урд талдаа (-Z, талбай руу) багананууд, голд нь суугаа Чингис хааны хөшөө.
    void GovernmentPalace(Scene& s, float cx, float cz)
    {
        const float w = 40.0f, d = 10.0f, h = 13.0f;
        const Color wall = { 212, 208, 198, 255 };
        const Color stone = { 228, 226, 218, 255 };
        const Color glass = { 44, 62, 88, 255 };
        const float front = cz - d * 0.5f;

        s.solid.Box({ cx, (h - 1.5f) * 0.5f, cz }, { w, h + 1.5f, d }, wall);
        s.solid.Box({ cx, 0.5f, cz }, { w + 0.2f, 1.0f, d + 0.2f }, ScaleColor(wall, 0.7f));
        s.solid.Box({ cx, h + 0.3f, cz }, { w + 0.8f, 0.6f, d + 0.8f }, stone);                 // дээврийн хүрээ
        s.solid.Box({ cx, h + 1.6f, cz }, { 16.0f, 2.0f, d + 0.4f }, wall);                     // голын өргөгдсөн хэсэг
        s.solid.Box({ cx, h + 2.75f, cz }, { 16.6f, 0.3f, d + 1.0f }, stone);

        // Багананууд ба тэдгээрийн дээрх дам нуруу; голын хэсэгт хөшөөнд зай үлдээнэ.
        const float columnZ = front - 1.3f;
        s.solid.Box({ cx, 0.07f, front - 1.6f }, { w + 1.0f, 0.14f, 4.2f }, stone);             // довжоо
        s.solid.Box({ cx, 11.2f, front - 1.2f }, { w + 0.6f, 1.4f, 2.8f }, stone);
        for (int i = 0; i < 12; ++i)
        {
            const float x = cx - 18.5f + (float)i * (37.0f / 11.0f);
            if (std::fabs(x - cx) < 3.5f)
            {
                continue;
            }
            s.solid.Cylinder({ x, 0.0f, columnZ }, 0.48f, 0.42f, 10.5f, 8, stone);
            s.solid.Box({ x, 0.2f, columnZ }, { 1.2f, 0.4f, 1.2f }, stone);
            s.Collider({ x, 5.25f, columnZ }, { 0.9f, 10.5f, 0.9f });
        }

        // Цонхнууд: урд, ард гурван эгнээ, хажууд хоёр багана
        for (float y : { 2.8f, 6.0f, 9.2f })
        {
            for (int i = 0; i < 11; ++i)
            {
                const float along = -16.8f + (float)i * 3.36f;
                if (std::fabs(along) > 1.0f || y > 4.0f) // голд нь төв хаалга
                {
                    FacePanel(s, cx, cz, w, d, 3, along, y, 1.4f, 2.0f, 0.1f, glass);
                }
                FacePanel(s, cx, cz, w, d, 2, along, y, 1.4f, 2.0f, 0.1f, glass);
            }
            for (float along : { -2.4f, 2.4f })
            {
                FacePanel(s, cx, cz, w, d, 0, along, y, 1.4f, 2.0f, 0.1f, glass);
                FacePanel(s, cx, cz, w, d, 1, along, y, 1.4f, 2.0f, 0.1f, glass);
            }
        }
        FacePanel(s, cx, cz, w, d, 3, 0.0f, 1.7f, 2.6f, 3.4f, 0.12f, { 84, 58, 40, 255 }); // төв хаалга

        // Дээвэр дээрх төрийн далбаа
        s.solid.Cylinder({ cx, h + 2.9f, cz }, 0.07f, 0.05f, 5.0f, 6, kWindowFrame);
        s.solid.Box({ cx + 0.45f, h + 7.3f, cz }, { 0.8f, 1.2f, 0.04f }, { 190, 44, 48, 255 });
        s.solid.Box({ cx + 1.25f, h + 7.3f, cz }, { 0.8f, 1.2f, 0.04f }, { 20, 96, 166, 255 });
        s.solid.Box({ cx + 2.05f, h + 7.3f, cz }, { 0.8f, 1.2f, 0.04f }, { 190, 44, 48, 255 });

        // Чингис хааны суугаа хөшөө: голын багануудын хооронд, талбай руу харсан
        const float statueZ = front - 2.6f;
        s.solid.Box({ cx, 0.6f, statueZ }, { 5.0f, 1.2f, 4.2f }, kGranite);
        s.Collider({ cx, 3.5f, statueZ }, { 5.0f, 7.0f, 4.2f });
        PlaceFigure(s, cx, 1.2f, statueZ, PI, 1.5f);
        s.solid.Box({ 0.0f, 1.2f, -0.5f }, { 2.2f, 2.4f, 1.6f }, ScaleColor(kBronze, 0.85f)); // сэнтий
        s.solid.Box({ 0.0f, 1.25f, 0.45f }, { 1.7f, 0.7f, 1.3f }, kBronze);                   // өвөр
        s.solid.Box({ 0.0f, 0.45f, 0.95f }, { 1.5f, 0.9f, 0.4f }, kBronze);                   // шилбэ
        s.solid.Box({ 0.0f, 2.3f, 0.05f }, { 1.5f, 1.5f, 0.9f }, kBronze);                    // цээж
        s.solid.Box({ 0.0f, 3.38f, 0.1f }, { 0.62f, 0.66f, 0.6f }, kBronze);                  // толгой
        s.solid.Cylinder({ 0.0f, 3.7f, 0.1f }, 0.46f, 0.0f, 0.46f, 6, kBronze);               // малгай
        for (float side : { -0.92f, 0.92f })
        {
            s.solid.Box({ side, 1.75f, 0.4f }, { 0.34f, 0.34f, 1.1f }, kBronze);              // түшсэн гар
        }
        s.solid.ResetTransform();

        s.Collider({ cx, (h + 3.0f) * 0.5f, cz }, { w, h + 3.0f, d });
        TallShadow(s, cx - w * 0.5f, front - 2.6f, cx + w * 0.5f, cz + d * 0.5f, h + 0.6f);
    }

    // Сүхбаатарын талбай: чулуун хавтан дэвссэн талбай, голд нь морьт хөшөө, хойд (+Z) талд нь Төрийн ордон.
    void SukhbaatarSquare(Scene& s)
    {
        const float x0 = 36.0f, x1 = 76.0f, z0 = 12.0f, z1 = 52.0f;
        const float cx = (x0 + x1) * 0.5f, cz = (z0 + z1) * 0.5f;

        s.solid.Box({ cx, 0.035f, cz }, { x1 - x0, 0.07f, z1 - z0 }, kPaving);
        for (float x = x0; x <= x1 + 0.01f; x += 4.0f)
        {
            s.solid.Box({ x, 0.05f, cz }, { 0.14f, 0.09f, z1 - z0 }, ScaleColor(kPaving, 0.8f));
        }
        for (float z = z0; z <= z1 + 0.01f; z += 4.0f)
        {
            s.solid.Box({ cx, 0.05f, z }, { x1 - x0, 0.09f, 0.14f }, ScaleColor(kPaving, 0.8f));
        }
        Footprint(s, cx, cz, x1 - x0, z1 - z0); // өвс ургуулахгүй, газрын зурагт талбай харагдана

        SukhbaatarStatue(s, cx, cz - 2.0f);
        GovernmentPalace(s, cx, 60.0f);

        LightPole(s, x0 + 1.0f, z0 + 1.0f, 0);
        LightPole(s, x1 - 1.0f, z0 + 1.0f, 1);
        LightPole(s, x0 + 1.0f, z1 - 6.0f, 0);
        LightPole(s, x1 - 1.0f, z1 - 6.0f, 0);
        Flagpole(s, cx - 9.0f, z1 - 3.0f);
        Flagpole(s, cx + 9.0f, z1 - 3.0f);

        // Талбай дээр орхигдсон машин, хаалт
        Car(s, x0 + 7.0f, z0 + 6.0f, true, RandomCarPaint(s.rng));
        Car(s, x1 - 8.0f, z0 + 15.0f, false, RandomCarPaint(s.rng));
        Truck(s, x0 + 9.0f, z1 - 12.0f, true, { 96, 104, 96, 255 }, { 120, 116, 104, 255 });
        Crate(s, cx + 6.0f, cz + 6.0f, 1.0f, 0.0f, kWood);
        Crate(s, cx + 7.1f, cz + 6.2f, 1.0f, 0.0f, kWoodGrey);
        Barrel(s, cx - 7.0f, cz - 9.0f, kRust);
    }

    // Улаанбаатарын үеийн төв: 10 давхар орон сууцны байрнууд ба Сүхбаатарын талбай.
    // Агуулах, шатахуун түгээх газар, пост, замууд бусад үетэй ижил байрандаа үлдэнэ.
    void BuildUlaanbaatar(Scene& s)
    {
        static const Color walls[] = { { 186, 182, 172, 255 }, { 196, 180, 160, 255 }, { 164, 176, 184, 255 },
                                       { 190, 168, 160, 255 }, { 172, 178, 160, 255 } };
        const struct { float x, z, w, d; int entrance; } blocks[] = {
            { -12.0f, 18.0f, 26.0f, 11.0f, 2 },  // шороон замын баруун талын хороолол
            { -12.0f, 44.0f, 26.0f, 11.0f, 3 },
            { 12.0f, 31.0f, 11.0f, 24.0f, 0 },
            { 11.0f, 53.0f, 11.0f, 10.0f, 0 },
            { -42.0f, 50.0f, 11.0f, 24.0f, 0 },
            { -16.0f, 70.0f, 26.0f, 11.0f, 3 },
            { 88.0f, 20.0f, 12.0f, 26.0f, 1 },   // талбайн зүүн талд
            { 88.0f, 50.0f, 12.0f, 26.0f, 1 },
            { 64.0f, 82.0f, 26.0f, 11.0f, 3 },   // Төрийн ордны ард
            { 0.0f, -26.0f, 26.0f, 11.0f, 2 },   // засмал замын урд талд
            { 62.0f, -30.0f, 26.0f, 11.0f, 2 },
        };
        for (const auto& block : blocks)
        {
            ApartmentBlock(s, block.x, block.z, block.w, block.d, 10, walls[s.rng.Int(0, 4)], block.entrance);
        }

        SukhbaatarSquare(s);

        // Хорооллын хашаан доторх зүйлс
        Car(s, -16.0f, 28.0f, true, RandomCarPaint(s.rng));
        Car(s, -6.0f, 34.0f, true, RandomCarPaint(s.rng));
        Car(s, 2.5f, 60.0f, false, RandomCarPaint(s.rng));
        Shed(s, -22.0f, 31.0f, 2.6f, 2.2f, 2.2f);
        Barrel(s, -19.6f, 31.4f, kRust);
        LightPole(s, 21.0f, 14.0f, 0);
        LightPole(s, 21.0f, 44.0f, 1);
        Car(s, 27.4f, 44.0f, false, RandomCarPaint(s.rng));
    }

    // ------------------------------------------------------------------ Аймгийн төв / хот

    // Гэр хорооллын хашаа: банзан хашаа, нэг эсвэл хоёр гэр, модон жорлон, түлээ, заримд нь жижиг байшин.
    // gateFace: 2 = +Z (хойд гудамж), 3 = -Z (урд гудамж).
    void GerLot(Scene& s, float x0, float z0, float x1, float z1, int gateFace)
    {
        Yard(s, x0, z0, x1, z1, gateFace, (x0 + x1) * 0.5f + s.rng.Range(-2.0f, 2.0f));
        const float cx = (x0 + x1) * 0.5f, cz = (z0 + z1) * 0.5f;
        const float back = gateFace == 2 ? -1.0f : 1.0f; // гудамжнаас хол тал руу
        const float depth = z1 - z0;
        const float radius = s.rng.Range(2.1f, 2.5f);
        if (s.rng.Chance(0.25f))
        {
            // Гэрийн оронд жижиг байшин (хашаа байшин)
            HouseStyle style = RegionHouseStyle(s);
            style.floors = 1;
            House(s, cx, cz + back * depth * 0.18f, 4.6f, 4.2f, style, gateFace == 2 ? 2 : 3);
        }
        else
        {
            Ger(s, cx + s.rng.Range(-1.0f, 1.0f), cz + back * depth * 0.18f, radius, RandomGerDoor(s.rng));
        }
        if (s.rng.Chance(0.35f) && (x1 - x0) > 13.0f)
        {
            Ger(s, cx + (s.rng.Chance(0.5f) ? -1.0f : 1.0f) * (x1 - x0) * 0.24f, cz - back * depth * 0.16f, 2.0f,
                RandomGerDoor(s.rng));
        }
        // Модон жорлон (хашааны арын буланд), түлээ, торх
        Shed(s, x0 + 1.4f, cz + back * (depth * 0.5f - 1.4f), 1.3f, 1.3f, 2.2f);
        if (s.rng.Chance(0.6f)) Crate(s, x1 - 1.4f, cz + back * (depth * 0.5f - 1.3f), 0.9f, 0.0f, kWoodGrey);
        if (s.rng.Chance(0.4f)) Barrel(s, x1 - 1.0f, cz - back * (depth * 0.5f - 1.6f), s.rng.Chance(0.5f) ? kRust : Color{ 60, 96, 130, 255 });
    }

    // Хороолол (гудамжаар хүрээлэгдсэн дөрвөлжин): 3 x 2 гэр хорооллын хашаа
    void GerBlock(Scene& s, float x0, float z0, float x1, float z1)
    {
        const float width = (x1 - x0) / 3.0f;
        const float mid = (z0 + z1) * 0.5f;
        for (int i = 0; i < 3; ++i)
        {
            const float a = x0 + width * (float)i + 0.6f, b = x0 + width * (float)(i + 1) - 0.6f;
            GerLot(s, a, z0, b, mid - 1.5f, 3);
            GerLot(s, a, mid + 1.5f, b, z1, 2);
        }
    }

    // Хашаат байшингийн хороолол: 2 x 2 хашаа, хаалга нь хажуугийн гудамж руу
    void HouseBlock(Scene& s, float x0, float z0, float x1, float z1)
    {
        const float mx = (x0 + x1) * 0.5f, mz = (z0 + z1) * 0.5f;
        BuildLot(s, x0, z0, mx - 1.5f, mz - 1.5f, 1);
        BuildLot(s, x0, mz + 1.5f, mx - 1.5f, z1, 1);
        BuildLot(s, mx + 1.5f, z0, x1, mz - 1.5f, 0);
        BuildLot(s, mx + 1.5f, mz + 1.5f, x1, z1, 0);
    }

    // Орон сууцны хороолол: хоёр урт байр, дунд нь хашаа (машин, тоглоомын талбай, гэрлийн шон)
    void ApartmentQuarter(Scene& s, float x0, float z0, float x1, float z1, int minFloors, int maxFloors)
    {
        static const Color walls[] = { { 186, 182, 172, 255 }, { 196, 180, 160, 255 }, { 164, 176, 184, 255 },
                                       { 190, 168, 160, 255 }, { 172, 178, 160, 255 }, { 206, 196, 150, 255 } };
        const float w = x1 - x0 - 4.0f, d = 11.0f;
        const float cx = (x0 + x1) * 0.5f;
        ApartmentBlock(s, cx, z0 + 2.0f + d * 0.5f, w, d, s.rng.Int(minFloors, maxFloors), walls[s.rng.Int(0, 5)], 2);
        ApartmentBlock(s, cx, z1 - 2.0f - d * 0.5f, w, d, s.rng.Int(minFloors, maxFloors), walls[s.rng.Int(0, 5)], 3);
        // Хашаа: машин, гэрлийн шон, тоглоомын талбайн хашлага
        const float yard = (z0 + z1) * 0.5f;
        Car(s, x0 + 6.0f + s.rng.Range(0.0f, 8.0f), yard - 3.0f, true, RandomCarPaint(s.rng));
        if (s.rng.Chance(0.6f)) Car(s, x1 - 8.0f - s.rng.Range(0.0f, 6.0f), yard + 3.0f, true, RandomCarPaint(s.rng));
        LightPole(s, cx + s.rng.Range(-8.0f, 8.0f), yard, s.rng.Chance(0.2f) ? 1 : 0);
        for (int i = 0; i < 3; ++i)
        {
            const float px = cx - 6.0f + 6.0f * (float)i;
            s.solid.Box({ px, 0.9f, yard + 4.6f }, { 0.12f, 1.8f, 0.12f }, { 176, 60, 50, 255 }); // тоглоомын талбайн хүрээ
        }
        s.solid.Box({ cx, 1.8f, yard + 4.6f }, { 12.2f, 0.1f, 0.1f }, { 176, 60, 50, 255 });
    }

    // Төв талбай: хавтан, төрийн далбаа, хөшөө, Засаг даргын тамгын газар, гэрлийн шонгууд
    void TownSquare(Scene& s, float x0, float z0, float x1, float z1, bool city)
    {
        const float cx = (x0 + x1) * 0.5f, cz = (z0 + z1) * 0.5f;
        const Color tile = { 168, 164, 156, 255 };
        for (float x = x0 + 2.0f; x < x1 - 2.0f; x += 6.0f)
        {
            for (float z = z0 + 2.0f; z < z1 - 16.0f; z += 6.0f)
            {
                const float ground = s.terrain.HeightAt(x + 3.0f, z + 3.0f);
                s.solid.Box({ x + 3.0f, ground + 0.04f, z + 3.0f }, { 5.9f, 0.08f, 5.9f }, JitterColor(tile, s.rng, 0.04f));
            }
        }
        // Засаг даргын тамгын газар: талбайн хойд талд, төвдөө өндөр хаалгатай, багануудтай
        const float hz = z1 - 8.5f;
        ApartmentBlock(s, cx, hz, x1 - x0 - 6.0f, 12.0f, city ? 5 : 3, { 214, 206, 186, 255 }, 3);
        for (int i = 0; i < 6; ++i)
        {
            const float px = cx - 7.5f + 3.0f * (float)i;
            s.solid.Cylinder({ px, 0.0f, hz - 6.6f }, 0.35f, 0.32f, 6.2f, 10, { 232, 228, 216, 255 });
        }
        s.solid.Box({ cx, 6.4f, hz - 6.6f }, { 18.0f, 0.5f, 1.4f }, { 222, 216, 200, 255 });
        s.Collider({ cx, 3.1f, hz - 6.6f }, { 17.0f, 6.2f, 0.8f });
        // Хөшөө: тавцан, морьтон баатрын дүрс (хялбаршуулсан)
        const float sz = cz - 6.0f;
        s.solid.Box({ cx, 1.0f, sz }, { 4.2f, 2.0f, 4.2f }, { 120, 116, 110, 255 });
        s.solid.Box({ cx, 2.15f, sz }, { 3.6f, 0.3f, 3.6f }, { 150, 146, 138, 255 });
        s.solid.Box({ cx, 3.3f, sz }, { 0.9f, 2.0f, 2.4f }, { 70, 84, 70, 255 });      // морь
        s.solid.Box({ cx, 4.6f, sz - 0.2f }, { 0.6f, 1.2f, 0.6f }, { 70, 84, 70, 255 }); // морьтон
        s.solid.Box({ cx, 4.2f, sz + 1.3f }, { 0.5f, 0.9f, 0.5f }, { 70, 84, 70, 255 }); // морины толгой
        s.Collider({ cx, 1.2f, sz }, { 4.2f, 2.4f, 4.2f });
        s.Shadow(cx - 2.1f, sz - 2.1f, cx + 2.1f, sz + 2.1f, 0.0f, 5.2f);
        Flagpole(s, cx - 9.0f, sz);
        Flagpole(s, cx + 9.0f, sz);
        for (float x : { x0 + 3.0f, x1 - 3.0f })
        {
            for (float z : { z0 + 4.0f, cz })
            {
                LightPole(s, x, z, 0);
            }
        }
        // Сандал
        for (float x : { cx - 5.0f, cx + 5.0f })
        {
            s.solid.Box({ x, 0.45f, sz - 5.0f }, { 2.0f, 0.1f, 0.5f }, kWood);
            s.solid.Box({ x, 0.22f, sz - 5.0f }, { 1.8f, 0.45f, 0.1f }, kMetal);
        }
    }

    // Аймгийн төв: 4 x 4 хороолол. Төвд нь талбай, орон сууц; хүрээнд нь хашаат байшин; хойд ба зүүн захаар гэр хороолол.
    // Хот (Эрдэнэт, Дархан, Улаанбаатар, Чингис) илүү өндөр, олон орон сууцтай.
    void BuildTown(Scene& s)
    {
        bool city = false;
        TownName(s.variant, city);
        const float margin = 6.0f; // гудамжны тэнхлэгээс хорооллын зах хүртэл
        for (int bx = 0; bx < 4; ++bx)
        {
            for (int bz = 0; bz < 4; ++bz)
            {
                const float x0 = kTownStreets[bx] + margin, x1 = kTownStreets[bx + 1] - margin;
                const float z0 = kTownRows[bz] + margin, z1 = kTownRows[bz + 1] - margin;
                const bool inner = (bx == 1 || bx == 2) && (bz == 1 || bz == 2);
                const bool outerEdge = bz == 3 || bx == 3;
                if (bx == 1 && bz == 1)
                {
                    TownSquare(s, x0, z0, x1, z1, city);
                }
                else if (inner)
                {
                    ApartmentQuarter(s, x0, z0, x1, z1, city ? 5 : 3, city ? 9 : 5);
                }
                else if (city && !outerEdge)
                {
                    ApartmentQuarter(s, x0, z0, x1, z1, 4, 5);
                }
                else if (outerEdge)
                {
                    GerBlock(s, x0, z0, x1, z1);
                }
                else
                {
                    HouseBlock(s, x0, z0, x1, z1);
                }
            }
        }

        // Гудамжинд орхигдсон машинууд, гэрлийн шонгууд, хогийн сав
        for (int i = 0; i < 5; ++i)
        {
            for (int j = 0; j < 4; ++j)
            {
                const float z = kTownRows[j] + s.rng.Range(14.0f, 40.0f);
                if (s.rng.Chance(0.35f)) Car(s, kTownStreets[i] + (s.rng.Chance(0.5f) ? 2.0f : -2.0f), z, false, RandomCarPaint(s.rng));
                if (s.rng.Chance(0.5f)) LightPole(s, kTownStreets[i] + 4.6f, kTownRows[j] + 27.0f, s.rng.Chance(0.15f) ? 1 : 0);
            }
        }
        // Хотын үүдний самбар (гол өргөн чөлөөний эхэнд)
        RoadSign(s, kTownStreets[2] + 5.5f, kTownRows[0] - 10.0f, 0, 2.0f);
    }

    // ------------------------------------------------------------------ Буянт-Ухаа нисэх онгоцны буудал
    // OpenStreetMap-ийн өгөгдлөөр (AirportData.h) жинхэнэ хэмжээ, жинхэнэ чиглэлтэй. Зөвхөн Улаанбаатарын үед.

    const Color kRunwayAsphalt = { 64, 66, 70, 255 };
    const Color kTaxiAsphalt = { 80, 80, 82, 255 };
    const Color kApronConcrete = { 156, 154, 146, 255 };
    const Color kRunwayPaint = { 228, 228, 222, 255 };
    const Color kTaxiPaint = { 218, 178, 58, 255 };
    const float kYApron = 0.05f, kYTaxi = 0.06f, kYRunway = 0.07f, kYAirPaint = 0.09f;

    Vector3 AirGround(const Scene& s, Vector2 p, float lift)
    {
        return { p.x, s.terrain.HeightAt(p.x, p.y) + lift, p.y };
    }

    // Дээшээ харсан гурвалжин (xz дээрх оройн дарааллаас үл хамааран); том бол хуваана
    void PaveTriangle(Scene& s, Vector2 a, Vector2 b, Vector2 c, float lift, Color color, float jitter, int depth = 0)
    {
        const float ab = Vector2Distance(a, b), bc = Vector2Distance(b, c), ca = Vector2Distance(c, a);
        if (std::max({ ab, bc, ca }) > 32.0f && depth < 8)
        {
            // Хамгийн урт ирмэгийг тэнд нь хуваана
            if (ab >= bc && ab >= ca)
            {
                const Vector2 m = Vector2Lerp(a, b, 0.5f);
                PaveTriangle(s, a, m, c, lift, color, jitter, depth + 1);
                PaveTriangle(s, m, b, c, lift, color, jitter, depth + 1);
            }
            else if (bc >= ca)
            {
                const Vector2 m = Vector2Lerp(b, c, 0.5f);
                PaveTriangle(s, a, b, m, lift, color, jitter, depth + 1);
                PaveTriangle(s, a, m, c, lift, color, jitter, depth + 1);
            }
            else
            {
                const Vector2 m = Vector2Lerp(c, a, 0.5f);
                PaveTriangle(s, a, b, m, lift, color, jitter, depth + 1);
                PaveTriangle(s, m, b, c, lift, color, jitter, depth + 1);
            }
            return;
        }
        const float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        if (cross > 0.0f) std::swap(b, c); // Box-ийн дээд талтай ижил эргэлт (дээрээс харагдана)
        s.solid.Triangle(AirGround(s, a, lift), AirGround(s, b, lift), AirGround(s, c, lift), JitterColor(color, s.rng, jitter));
    }

    void PavePolygon(Scene& s, const Polygon& polygon, float lift, Color color, float jitter)
    {
        const std::vector<Vector2> triangles = Triangulate(polygon);
        for (size_t i = 0; i + 2 < triangles.size(); i += 3)
        {
            PaveTriangle(s, triangles[i], triangles[i + 1], triangles[i + 2], lift, color, jitter);
        }
    }

    // Олон шугамын дагуух зурвас (ХБЗ, явгалах зам): тэнхлэгийн дагуух зай -> цэг, хажуугийн чиглэл
    struct StripPath
    {
        std::vector<Vector2> points;
        std::vector<Vector2> side;
        std::vector<float> distance;

        StripPath(const std::vector<Vector2>& raw, float spacing)
        {
            points = Resample(raw, spacing);
            const int count = (int)points.size();
            side.resize(count);
            distance.resize(count);
            for (int i = 0; i < count; ++i)
            {
                const Vector2 tangent = Vector2Normalize(Vector2Subtract(points[std::min(i + 1, count - 1)], points[std::max(i - 1, 0)]));
                side[i] = { -tangent.y, tangent.x };
                distance[i] = i == 0 ? 0.0f : distance[i - 1] + Vector2Distance(points[i - 1], points[i]);
            }
        }
        float Length() const { return distance.back(); }

        // along зайд, хажуу тийш across: тэнхлэгийн шулуун хэсгүүдийн дагуу шугаман завсарлана
        Vector2 At(float along, float across) const
        {
            along = std::clamp(along, 0.0f, Length());
            size_t i = 0;
            while (i + 2 < points.size() && distance[i + 1] < along) ++i;
            const float span = std::max(0.001f, distance[i + 1] - distance[i]);
            const float t = (along - distance[i]) / span;
            const Vector2 p = Vector2Lerp(points[i], points[i + 1], t);
            const Vector2 n = Vector2Normalize(Vector2Lerp(side[i], side[i + 1], t));
            return { p.x + n.x * across, p.y + n.y * across };
        }

        // [a0, a1] x [c0, c1] тэгш өнцөгт (урт бол хэсэгчилнэ)
        void Paint(Scene& s, float a0, float a1, float c0, float c1, float lift, Color color) const
        {
            const int pieces = std::max(1, (int)std::ceil((a1 - a0) / 24.0f));
            for (int k = 0; k < pieces; ++k)
            {
                const float u0 = a0 + (a1 - a0) * (float)k / (float)pieces;
                const float u1 = a0 + (a1 - a0) * (float)(k + 1) / (float)pieces;
                s.solid.Quad(AirGround(s, At(u0, c0), lift), AirGround(s, At(u1, c0), lift), AirGround(s, At(u1, c1), lift),
                             AirGround(s, At(u0, c1), lift), color);
            }
        }
    };

    // ХБЗ-ын дугаар (7 хэсэгт цифр). end = 0: эхний үзүүр (нисгэгч тэнхлэгийн дагуу урагш харна), 1: сүүлийн үзүүр.
    void RunwayNumber(Scene& s, const StripPath& path, int end, int number)
    {
        static const uint8_t masks[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };
        const float L = path.Length();
        const float H = 14.0f, W = 6.0f, t = 1.3f, v0 = 50.0f;
        // Нисгэгчийн өнцгөөс: u = баруун тийш, v = урагш (ХБЗ руу)
        auto paint = [&](float u0, float u1, float w0, float w1) {
            if (end == 0) path.Paint(s, v0 + w0, v0 + w1, u0, u1, kYAirPaint, kRunwayPaint);
            else path.Paint(s, L - v0 - w1, L - v0 - w0, -u1, -u0, kYAirPaint, kRunwayPaint);
        };
        const int digits[2] = { number / 10, number % 10 };
        for (int d = 0; d < 2; ++d)
        {
            const float x = d == 0 ? -W - 2.0f : 2.0f;
            const uint8_t m = masks[digits[d]];
            if (m & 0x01) paint(x, x + W, H - t, H);           // a: дээд
            if (m & 0x02) paint(x + W - t, x + W, H * 0.5f, H); // b
            if (m & 0x04) paint(x + W - t, x + W, 0.0f, H * 0.5f); // c
            if (m & 0x08) paint(x, x + W, 0.0f, t);             // d: доод
            if (m & 0x10) paint(x, x + t, 0.0f, H * 0.5f);      // e
            if (m & 0x20) paint(x, x + t, H * 0.5f, H);         // f
            if (m & 0x40) paint(x, x + W, H * 0.5f - t * 0.5f, H * 0.5f + t * 0.5f); // g: дунд
        }
    }

    void BuildRunway(Scene& s, const airport::Strip& strip, int numberStart)
    {
        const std::vector<Vector2> raw(airport::kPoints + strip.first, airport::kPoints + strip.first + strip.count);
        const StripPath path(raw, 12.0f);
        const float L = path.Length(), hw = strip.halfWidth;
        const bool gravel = strip.kind == 1;

        // Хучилт: хөвөө, гадаргуу (хавтан бүр бага зэрэг өөр өнгө)
        for (size_t i = 0; i + 1 < path.points.size(); ++i)
        {
            const float a0 = path.distance[i], a1 = path.distance[i + 1];
            path.Paint(s, a0, a1, -hw - 1.5f, hw + 1.5f, kYRunway - 0.01f, JitterColor(gravel ? kDirtRoad : kGravel, s.rng, 0.05f));
            path.Paint(s, a0, a1, -hw, hw, kYRunway, JitterColor(gravel ? kGravel : kRunwayAsphalt, s.rng, gravel ? 0.07f : 0.03f));
        }
        if (gravel)
        {
            // Хайрган ХБЗ: захын цагаан тэмдэг (будсан чулуу) 50 м тутам
            for (float a = 10.0f; a < L - 5.0f; a += 50.0f)
            {
                for (float side : { -1.0f, 1.0f })
                {
                    const Vector2 p = path.At(a, side * (hw + 0.8f));
                    s.solid.Box({ p.x, s.terrain.HeightAt(p.x, p.y) + 0.15f, p.y }, { 1.2f, 0.3f, 1.2f }, kRunwayPaint);
                }
            }
            return;
        }

        // Тэмдэглэгээ: захын шугам, тэнхлэгийн тасархай, босгоны "төгөлдөр хуурын" зураас, онох цэг, дугаар
        path.Paint(s, 0.0f, L, -hw + 0.6f, -hw + 1.5f, kYAirPaint, kRunwayPaint);
        path.Paint(s, 0.0f, L, hw - 1.5f, hw - 0.6f, kYAirPaint, kRunwayPaint);
        for (float a = 80.0f; a + 30.0f < L - 80.0f; a += 50.0f)
        {
            if (!s.rng.Chance(0.12f)) path.Paint(s, a, a + 30.0f, -0.45f, 0.45f, kYAirPaint, kRunwayPaint); // зарим нь арилсан
        }
        for (int end = 0; end < 2; ++end)
        {
            for (int k = 0; k < 6; ++k)
            {
                for (float side : { -1.0f, 1.0f })
                {
                    const float c = side * (3.0f + 3.6f * (float)k);
                    if (end == 0) path.Paint(s, 6.0f, 36.0f, c - 0.9f, c + 0.9f, kYAirPaint, kRunwayPaint);
                    else path.Paint(s, L - 36.0f, L - 6.0f, c - 0.9f, c + 0.9f, kYAirPaint, kRunwayPaint);
                }
            }
            for (float side : { -1.0f, 1.0f })
            {
                const float c0 = side > 0.0f ? 6.0f : -15.0f, c1 = side > 0.0f ? 15.0f : -6.0f;
                if (end == 0) path.Paint(s, 300.0f, 345.0f, c0, c1, kYAirPaint, kRunwayPaint);
                else path.Paint(s, L - 345.0f, L - 300.0f, c0, c1, kYAirPaint, kRunwayPaint);
            }
        }
        RunwayNumber(s, path, 0, numberStart);
        RunwayNumber(s, path, 1, (numberStart + 18 - 1) % 36 + 1);

        // Захын гэрэл 60 м тутам, үзүүрт ногоон/улаан гэрлийн эгнээ
        for (float a = 0.0f; a <= L; a += 60.0f)
        {
            for (float side : { -1.0f, 1.0f })
            {
                const Vector2 p = path.At(a, side * (hw + 2.5f));
                const float y = s.terrain.HeightAt(p.x, p.y);
                s.solid.Box({ p.x, y + 0.18f, p.y }, { 0.12f, 0.36f, 0.12f }, kMetal);
                s.solid.Box({ p.x, y + 0.42f, p.y }, { 0.26f, 0.14f, 0.26f }, { 236, 226, 170, 255 });
            }
        }
        for (int end = 0; end < 2; ++end)
        {
            for (float c = -hw + 1.0f; c <= hw - 1.0f; c += 3.0f)
            {
                const Vector2 p = path.At(end == 0 ? 1.0f : L - 1.0f, c);
                s.solid.Box({ p.x, s.terrain.HeightAt(p.x, p.y) + 0.12f, p.y }, { 0.4f, 0.24f, 0.4f },
                            end == 0 ? Color{ 70, 170, 90, 255 } : Color{ 190, 60, 50, 255 });
            }
        }

        // Салхины уут: эхний үзүүрээс 200 м-т ХБЗ-ын хажууд
        const Vector2 sock = path.At(200.0f, hw + 45.0f);
        const float ground = s.terrain.HeightAt(sock.x, sock.y);
        s.solid.Cylinder({ sock.x, ground, sock.y }, 0.1f, 0.07f, 6.0f, 6, kRunwayPaint);
        const Vector2 dir = Vector2Normalize(Vector2Subtract(path.At(210.0f, 0.0f), path.At(200.0f, 0.0f)));
        for (int k = 0; k < 4; ++k)
        {
            const float a = 0.25f + 0.85f * (float)k, b = a + 0.85f;
            s.solid.Tube({ sock.x + dir.x * a, ground + 5.8f - 0.08f * a, sock.y + dir.y * a },
                         { sock.x + dir.x * b, ground + 5.8f - 0.08f * b, sock.y + dir.y * b }, 0.45f - 0.06f * a,
                         0.45f - 0.06f * b, 8, k % 2 == 0 ? Color{ 230, 110, 40, 255 } : kRunwayPaint, false);
        }
        s.Collider({ sock.x, ground + 3.0f, sock.y }, { 0.3f, 6.0f, 0.3f });
    }

    void BuildTaxiway(Scene& s, const airport::Strip& strip)
    {
        const std::vector<Vector2> raw(airport::kPoints + strip.first, airport::kPoints + strip.first + strip.count);
        if (raw.size() < 2) return;
        const StripPath path(raw, 8.0f);
        const float hw = strip.halfWidth;
        for (size_t i = 0; i + 1 < path.points.size(); ++i)
        {
            path.Paint(s, path.distance[i], path.distance[i + 1], -hw, hw, kYTaxi, kTaxiAsphalt);
        }
        path.Paint(s, 0.0f, path.Length(), -0.15f, 0.15f, kYTaxi + 0.015f, kTaxiPaint); // шар тэнхлэг
    }

    void BuildHelipad(Scene& s, const airport::Ring& ring)
    {
        const Polygon polygon(airport::kPoints + ring.first, airport::kPoints + ring.first + ring.count);
        PavePolygon(s, polygon, kYApron + 0.01f, { 140, 140, 134, 255 }, 0.02f);
        Vector2 c = { 0.0f, 0.0f };
        for (const Vector2& p : polygon) c = Vector2Add(c, p);
        c = Vector2Scale(c, 1.0f / (float)polygon.size());
        // Тойрог ба "H" тэмдэг: талбайн ирмэгийн чиглэлтэй зэрэгцүүлнэ
        const Vector2 u = Vector2Normalize(Vector2Subtract(polygon[1], polygon[0]));
        const Vector2 v = { -u.y, u.x };
        const float y = s.terrain.HeightAt(c.x, c.y) + kYApron + 0.03f;
        auto rect = [&](float u0, float u1, float v0, float v1, Color color) {
            auto p = [&](float a, float b) { return Vector3{ c.x + u.x * a + v.x * b, y, c.y + u.y * a + v.y * b }; };
            s.solid.Quad(p(u0, v0), p(u0, v1), p(u1, v1), p(u1, v0), color);
            s.solid.Quad(p(u1, v0), p(u1, v1), p(u0, v1), p(u0, v0), color);
        };
        rect(-4.0f, -2.6f, -5.0f, 5.0f, kRunwayPaint);
        rect(2.6f, 4.0f, -5.0f, 5.0f, kRunwayPaint);
        rect(-2.6f, 2.6f, -0.7f, 0.7f, kRunwayPaint);
        for (int k = 0; k < 32; ++k)
        {
            const float a0 = 2.0f * PI * (float)k / 32.0f, a1 = 2.0f * PI * (float)(k + 1) / 32.0f;
            const float r0 = 8.6f, r1 = 9.4f;
            auto p = [&](float a, float r) {
                return Vector3{ c.x + (u.x * std::cos(a) + v.x * std::sin(a)) * r, y, c.y + (u.y * std::cos(a) + v.y * std::sin(a)) * r };
            };
            s.solid.Quad(p(a0, r0), p(a1, r0), p(a1, r1), p(a0, r1), kTaxiPaint);
            s.solid.Quad(p(a0, r1), p(a1, r1), p(a1, r0), p(a0, r0), kTaxiPaint);
        }
    }

    // Олон өнцөгт суурьтай барилгын мөргөлдөөн: 2 м зузаан хэвтээ зурвасуудаар дүүргэнэ
    void PolygonColliders(Scene& s, const Vector2* points, int count, float base, float height, bool walkable = false)
    {
        float z0 = 1e9f, z1 = -1e9f;
        for (int i = 0; i < count; ++i)
        {
            z0 = std::min(z0, points[i].y);
            z1 = std::max(z1, points[i].y);
        }
        const float row = 2.0f;
        std::vector<float> xs;
        for (float z = z0 + row * 0.5f; z < z1 + row * 0.5f; z += row)
        {
            const float zc = std::min(z, z1 - 0.05f);
            xs.clear();
            for (int i = 0, j = count - 1; i < count; j = i++)
            {
                const Vector2 a = points[i], b = points[j];
                if ((a.y > zc) != (b.y > zc)) xs.push_back(a.x + (zc - a.y) * (b.x - a.x) / (b.y - a.y));
            }
            std::sort(xs.begin(), xs.end());
            for (size_t k = 0; k + 1 < xs.size(); k += 2)
            {
                if (xs[k + 1] - xs[k] < 0.3f) continue;
                s.Collider({ (xs[k] + xs[k + 1]) * 0.5f, base + height * 0.5f, zc }, { xs[k + 1] - xs[k], height, row }, true, walkable);
            }
        }
    }

    void AirportBuilding(Scene& s, const airport::Building& building)
    {
        const Vector2* p = airport::kPoints + building.first;
        const int n = building.count;
        const float h = building.height;

        float base = 1e9f;
        Vector2 center = { 0.0f, 0.0f };
        for (int i = 0; i < n; ++i)
        {
            base = std::min(base, s.terrain.HeightAt(p[i].x, p[i].y));
            center = Vector2Add(center, p[i]);
        }
        center = Vector2Scale(center, 1.0f / (float)n);
        base -= 0.2f;
        const float top = base + 0.2f + h;

        static const Color pastel[] = { { 214, 204, 176, 255 }, { 196, 186, 168, 255 }, { 176, 188, 196, 255 },
                                        { 210, 190, 170, 255 }, { 188, 196, 172, 255 }, { 220, 214, 200, 255 } };
        Color wall, roof, window = { 44, 54, 64, 255 };
        switch (building.kind)
        {
        case 0: wall = { 222, 220, 212, 255 }; roof = { 132, 134, 136, 255 }; window = { 62, 92, 112, 255 }; break;
        case 1: wall = { 176, 182, 186, 255 }; roof = { 118, 126, 132, 255 }; break;
        case 4: wall = { 232, 228, 216, 255 }; roof = { 70, 104, 86, 255 }; break;
        default: wall = pastel[s.rng.Int(0, 5)]; roof = { 112, 104, 98, 255 }; break;
        }
        wall = JitterColor(wall, s.rng, 0.04f);

        // Хананууд (оройнууд цагийн зүүний эсрэг тул гадна тал нь (dz, -dx))
        for (int i = 0; i < n; ++i)
        {
            const Vector2 a = p[i], c = p[(i + 1) % n];
            const float length = Vector2Distance(a, c);
            if (length < 0.05f) continue;
            const Vector2 normal = { (c.y - a.y) / length, -(c.x - a.x) / length };
            s.solid.Quad({ c.x, base, c.y }, { a.x, base, a.y }, { a.x, top, a.y }, { c.x, top, c.y }, wall);

            auto face = [&](float t0, float t1, float y0, float y1, float out, Color color) {
                auto q = [&](float t, float y) {
                    return Vector3{ a.x + (c.x - a.x) * t + normal.x * out, y, a.y + (c.y - a.y) * t + normal.y * out };
                };
                s.solid.Quad(q(t1, y0), q(t0, y0), q(t0, y1), q(t1, y1), color);
            };

            if (building.kind == 0)
            {
                // Терминал: давхар бүрд шилэн зурвас
                for (float y = base + 1.4f; y + 2.2f < top - 0.8f; y += 4.2f)
                {
                    face(0.02f, 0.98f, y, y + 2.4f, 0.05f, window);
                }
            }
            else if (building.kind == 1)
            {
                // Ангар: босоо хавирга, хамгийн урт ханад том хаалга
                for (float t = 4.0f; t < length - 1.0f; t += 4.0f)
                {
                    face(t / length - 0.12f / length, t / length + 0.12f / length, base, top - 0.3f, 0.04f, ScaleColor(wall, 0.8f));
                }
                float longest = 0.0f;
                for (int k = 0; k < n; ++k) longest = std::max(longest, Vector2Distance(p[k], p[(k + 1) % n]));
                if (length >= longest - 0.01f)
                {
                    face(0.12f, 0.88f, base, base + (top - base) * 0.72f, 0.07f, { 128, 136, 142, 255 });
                }
            }
            else if (length > 2.5f)
            {
                // Цонхнууд: давхар бүрд 3.2 м тутам
                const int floors = std::max(1, (int)std::round((h - 0.6f) / 3.1f));
                const int columns = std::max(1, (int)(length / 3.2f));
                for (int f = 0; f < floors; ++f)
                {
                    const float y = base + 0.2f + 0.9f + 3.1f * (float)f;
                    for (int k = 0; k < columns; ++k)
                    {
                        const float t = ((float)k + 0.5f) / (float)columns;
                        const float half = 0.7f / length;
                        face(t - half, t + half, y, y + 1.4f, 0.05f, s.rng.Chance(0.15f) ? Color{ 24, 26, 30, 255 } : window);
                    }
                }
            }
        }

        // Хавтгай дээвэр (дээрээс харагдана)
        const Polygon outline(p, p + n);
        const std::vector<Vector2> triangles = Triangulate(outline);
        for (size_t i = 0; i + 2 < triangles.size(); i += 3)
        {
            Vector2 a = triangles[i], b = triangles[i + 1], c = triangles[i + 2];
            const float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
            if (cross > 0.0f) std::swap(b, c);
            s.solid.Triangle({ a.x, top, a.y }, { b.x, top, b.y }, { c.x, top, c.y }, roof);
        }

        if (building.kind == 4)
        {
            // Сүм: төвдөө бөмбөгөр
            s.solid.Cylinder({ center.x, top, center.y }, 2.2f, 2.2f, 2.4f, 12, wall);
            s.solid.Ellipsoid({ center.x, top + 3.0f, center.y }, { 2.3f, 2.0f, 2.3f }, 6, 12, { 200, 170, 80, 255 });
        }
        else if (building.kind == 0 && h > 12.0f)
        {
            // Терминалын дээврийн агааржуулалтын төхөөрөмж
            for (int k = 0; k < 4; ++k)
            {
                const Vector2 q = Vector2Lerp(p[0], center, 0.25f + 0.18f * (float)k);
                s.solid.Box({ q.x, top + 0.8f, q.y }, { 3.0f, 1.6f, 2.2f }, kMetal);
            }
        }

        PolygonColliders(s, p, n, base, top - base);
    }

    // Зорчигч тээврийн онгоц (Боинг 737 орчим): урт ~39 м, далавч ~35 м. heading: хамрын чиглэл (xz).
    // belly: буух араа эвдэрч гэдсэн дээрээ хэвтсэн.
    void Airliner(Scene& s, Vector2 position, Vector2 heading, Color tail, Color stripe, bool belly)
    {
        heading = Vector2Normalize(heading);
        const float ground = s.terrain.HeightAt(position.x, position.y);
        const float drop = belly ? -1.2f : 0.0f;
        Matrix place = MatrixMultiply(MatrixRotateY(std::atan2(-heading.y, heading.x)),
                                      MatrixTranslate(position.x, ground + drop, position.y));
        if (belly) place = MatrixMultiply(MatrixRotateX(0.05f), place);
        MeshBuilder& m = s.solid;
        m.SetTransform(place);

        const Color white = JitterColor({ 232, 232, 228, 255 }, s.rng, 0.03f);
        const Color grey = { 182, 186, 190, 255 };
        const Color dark = { 40, 46, 54, 255 };
        const float y = 3.3f, r = 1.95f;
        auto twoSided = [&](Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color color) {
            m.Quad(a, b, c, d, color);
            m.Quad(d, c, b, a, color);
        };

        // Их бие, хамар, сүүл
        m.Tube({ -14.5f, y, 0.0f }, { 13.0f, y, 0.0f }, r, r, 14, white);
        m.Ellipsoid({ 13.0f, y, 0.0f }, { 4.4f, r, r }, 6, 14, white);
        m.Tube({ -21.5f, y + 1.2f, 0.0f }, { -14.5f, y, 0.0f }, 0.35f, r, 14, white);
        m.Box({ 15.0f, y + 0.95f, 0.0f }, { 1.3f, 0.5f, 2.9f }, dark); // бүхээгийн шил
        for (float side : { -1.0f, 1.0f })
        {
            m.Box({ -1.0f, y + 0.55f, side * 1.93f }, { 25.0f, 0.3f, 0.08f }, dark);   // зорчигчийн цонхнууд
            m.Box({ -1.0f, y - 0.15f, side * 1.94f }, { 27.0f, 0.42f, 0.08f }, stripe); // будгийн зураас
            m.Box({ 10.2f, y + 0.1f, side * 1.94f }, { 0.9f, 1.9f, 0.06f }, ScaleColor(white, 0.82f)); // хаалга
        }

        // Далавч: доод далавч, сум хэлбэртэй, үзүүрт босоо далавчлуур
        for (float side : { -1.0f, 1.0f })
        {
            twoSided({ 4.0f, y - 1.0f, side * 1.8f }, { -6.4f, y - 0.25f, side * 17.4f }, { -8.8f, y - 0.25f, side * 17.4f },
                     { -3.4f, y - 1.0f, side * 1.8f }, grey);
            twoSided({ -6.4f, y - 0.25f, side * 17.4f }, { -7.6f, y + 1.9f, side * 17.6f }, { -8.8f, y + 1.9f, side * 17.6f },
                     { -8.8f, y - 0.25f, side * 17.4f }, tail);
            // Хөдөлгүүр ба тулгуур
            m.Tube({ 4.4f, y - 1.95f, side * 5.6f }, { 0.4f, y - 1.95f, side * 5.6f }, 1.05f, 0.8f, 14, grey);
            m.Tube({ 4.45f, y - 1.95f, side * 5.6f }, { 4.3f, y - 1.95f, side * 5.6f }, 0.88f, 0.88f, 14, dark);
            m.Box({ 1.4f, y - 1.2f, side * 5.6f }, { 3.2f, 0.9f, 0.25f }, grey);
            // Хэвтээ тогтворжуулагч
            twoSided({ -16.5f, y + 0.9f, side * 0.6f }, { -20.4f, y + 1.3f, side * 6.8f }, { -22.0f, y + 1.3f, side * 6.8f },
                     { -20.0f, y + 0.9f, side * 0.6f }, white);
        }
        // Босоо сүүл (компанийн өнгө)
        twoSided({ -15.0f, y + 1.5f, 0.0f }, { -20.0f, y + 8.7f, 0.0f }, { -22.6f, y + 8.7f, 0.0f }, { -20.8f, y + 1.5f, 0.0f }, tail);

        if (!belly)
        {
            m.Tube({ 11.5f, 0.35f, 0.0f }, { 11.5f, y - 1.3f, 0.0f }, 0.1f, 0.1f, 6, kMetal);
            m.Tube({ 11.5f, 0.35f, -0.35f }, { 11.5f, 0.35f, 0.35f }, 0.35f, 0.35f, 10, kRubber);
            for (float side : { -1.0f, 1.0f })
            {
                m.Tube({ -1.0f, 0.55f, side * 2.9f }, { -1.0f, y - 1.0f, side * 2.9f }, 0.13f, 0.13f, 6, kMetal);
                m.Tube({ -1.0f, 0.55f, side * 2.45f }, { -1.0f, 0.55f, side * 3.35f }, 0.55f, 0.55f, 12, kRubber);
            }
        }
        m.ResetTransform();

        // Мөргөлдөөн: их биеийн дагуу, хөдөлгүүрүүд
        auto world = [&](float lx, float lz) {
            return Vector2{ position.x + heading.x * lx - heading.y * lz, position.y + heading.y * lx + heading.x * lz };
        };
        const float bodyTop = y + r + drop;
        for (float lx = -19.0f; lx <= 16.0f; lx += 2.5f)
        {
            const Vector2 q = world(lx, 0.0f);
            s.Collider({ q.x, ground + bodyTop * 0.5f, q.y }, { 3.4f, bodyTop, 3.4f });
        }
        for (float side : { -1.0f, 1.0f })
        {
            const Vector2 q = world(2.4f, side * 5.6f);
            const float engineTop = std::max(0.5f, y - 0.9f + drop);
            s.Collider({ q.x, ground + engineTop * 0.5f, q.y }, { 2.4f, engineTop, 2.4f });
        }
    }

    // Ми-8 нисдэг тэрэг: хамар нь heading чиглэлд
    void Helicopter(Scene& s, Vector2 position, Vector2 heading, Color paint)
    {
        heading = Vector2Normalize(heading);
        const float ground = s.terrain.HeightAt(position.x, position.y);
        const Matrix place = MatrixMultiply(MatrixRotateY(std::atan2(-heading.y, heading.x)),
                                            MatrixTranslate(position.x, ground, position.y));
        MeshBuilder& m = s.solid;
        m.SetTransform(place);
        const Color body = JitterColor(paint, s.rng, 0.05f);
        const Color dark = { 36, 42, 48, 255 };
        m.BevelBox({ -0.6f, 2.0f, 0.0f }, { 9.2f, 2.6f, 2.5f }, 0.5f, body);
        m.Ellipsoid({ 3.9f, 1.9f, 0.0f }, { 2.1f, 1.3f, 1.25f }, 6, 12, body);
        m.Ellipsoid({ 4.4f, 2.35f, 0.0f }, { 1.5f, 0.8f, 1.05f }, 5, 10, dark); // бүхээгийн шил
        m.BevelBox({ 0.3f, 3.6f, 0.0f }, { 5.2f, 1.0f, 1.9f }, 0.3f, ScaleColor(body, 0.9f)); // хөдөлгүүрийн бүрхүүл
        m.Tube({ -4.8f, 2.6f, 0.0f }, { -12.8f, 3.4f, 0.0f }, 0.6f, 0.25f, 8, body);
        m.Box({ -12.6f, 4.3f, 0.0f }, { 1.2f, 2.2f, 0.12f }, body);
        m.Box({ -12.8f, 4.6f, 0.35f }, { 0.25f, 2.8f, 0.06f }, dark); // сүүлний сэнс
        for (float side : { -1.0f, 1.0f })
        {
            m.Box({ 0.0f, 2.3f, side * 1.27f }, { 6.0f, 0.5f, 0.04f }, dark); // цонхнууд
            m.Tube({ -1.0f, 0.4f, side * 1.6f }, { -1.0f, 0.4f, side * 1.9f }, 0.4f, 0.4f, 10, kRubber);
        }
        m.Tube({ 3.6f, 0.3f, -0.15f }, { 3.6f, 0.3f, 0.15f }, 0.3f, 0.3f, 8, kRubber);
        m.Tube({ 0.3f, 4.1f, 0.0f }, { 0.3f, 4.8f, 0.0f }, 0.16f, 0.12f, 8, kMetal);
        for (int k = 0; k < 5; ++k)
        {
            m.SetTransform(MatrixMultiply(MatrixMultiply(MatrixRotateY((float)k * 2.0f * PI / 5.0f + 0.4f), MatrixTranslate(0.3f, 4.85f, 0.0f)), place));
            m.Box({ 5.3f, -0.12f * 5.3f / 10.6f, 0.0f }, { 10.6f, 0.07f, 0.5f }, dark);
        }
        m.ResetTransform();
        for (float lx = -4.0f; lx <= 4.0f; lx += 2.5f)
        {
            const Vector2 q = { position.x + heading.x * lx, position.y + heading.y * lx };
            s.Collider({ q.x, ground + 1.9f, q.y }, { 2.6f, 3.8f, 2.6f });
        }
    }

    // ---------------- "Чингис хаан" олон улсын нисэх буудлын терминал (Буянт-Ухаа)
    // Орон нутгийн тэнхлэг (kTerminalFrame): x = урт тэнхлэг (±88 м), z = агаарын тал руу; урд (хуурай газрын) нүүр z = -23.
    // Төвд 26 м өндөр блок ("CHINGGIS KHAAN" бичиг, соёмбо, алтан хээтэй шилэн нүүр), хоёр жигүүр сум хэлбэрийн
    // металл дээвэртэй, урд талаар нь 2 давхарт гарах гүүрэн зам (хоёр үзүүрээрээ эргэж газар руу буух).

    const Color kTerminalWhite = { 232, 230, 222, 255 };
    const Color kTerminalGlass = { 56, 82, 100, 255 };
    const Color kTerminalRoof = { 178, 184, 190, 255 };
    const Color kTerminalFascia = { 92, 98, 106, 255 };
    const Color kSignBlue = { 26, 58, 196, 255 };
    const Color kSoyomboRed = { 196, 30, 36, 255 };
    const Color kLatticeGold = { 200, 160, 78, 255 };
    const float kWingTop = 15.0f, kCenterTop = 26.0f, kDeckTop = 7.2f;

    // Хоёр талаас харагдах дөрвөн өнцөгт (локал хувиргалттай)
    void TwoSidedQuad(MeshBuilder& m, Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color color)
    {
        m.Quad(a, b, c, d, color);
        m.Quad(d, c, b, a, color);
    }

    // Урд (-Z) тал руу харсан босоо тэгш өнцөгт: үзэгчийн баруун тал нь -X
    void FrontRect(MeshBuilder& m, float x0, float x1, float y0, float y1, float z, Color color)
    {
        m.Quad({ x1, y0, z }, { x0, y0, z }, { x0, y1, z }, { x1, y1, z }, color);
    }
    // Ар (+Z) тал руу харсан
    void BackRect(MeshBuilder& m, float x0, float x1, float y0, float y1, float z, Color color)
    {
        m.Quad({ x0, y0, z }, { x1, y0, z }, { x1, y1, z }, { x0, y1, z }, color);
    }

    // 5x7 цэгэн үсэг (зөвхөн бичигт хэрэгтэй үсгүүд)
    const char* const* Glyph(char c)
    {
        static const char* const kC[7] = { ".###.", "#...#", "#....", "#....", "#....", "#...#", ".###." };
        static const char* const kH[7] = { "#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#" };
        static const char* const kI[7] = { ".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###." };
        static const char* const kN[7] = { "#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#", "#...#" };
        static const char* const kG[7] = { ".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".###." };
        static const char* const kS[7] = { ".####", "#....", "#....", ".###.", "....#", "....#", "####." };
        static const char* const kK[7] = { "#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#" };
        static const char* const kA[7] = { ".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#" };
        static const char* const kT[7] = { "#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.." };
        static const char* const kE[7] = { "#####", "#....", "#....", "####.", "#....", "#....", "#####" };
        static const char* const kR[7] = { "####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#" };
        static const char* const kO[7] = { ".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###." };
        static const char* const kL[7] = { "#....", "#....", "#....", "#....", "#....", "#....", "#####" };
        static const char* const kP[7] = { "####.", "#...#", "#...#", "####.", "#....", "#....", "#...." };
        switch (c)
        {
        case 'C': return kC; case 'H': return kH; case 'I': return kI; case 'N': return kN; case 'G': return kG;
        case 'S': return kS; case 'K': return kK; case 'A': return kA; case 'T': return kT; case 'E': return kE;
        case 'R': return kR; case 'O': return kO; case 'L': return kL; case 'P': return kP;
        default: return nullptr;
        }
    }

    // Цэгэн зургийг урд нүүрэнд будна: мөр бүрийн зэрэгцээ цэгүүдийг нэг тэгш өнцөгт болгоно.
    // left: зургийн зүүн дээд булангийн x (үзэгчийн зүүн = +X), top: дээд ирмэгийн y.
    void PixelArt(MeshBuilder& m, const char* const* rows, int count, float left, float top, float px, float z, Color color)
    {
        for (int r = 0; r < count; ++r)
        {
            const int width = (int)std::strlen(rows[r]);
            for (int c = 0; c < width;)
            {
                if (rows[r][c] != '#') { ++c; continue; }
                int end = c;
                while (end < width && rows[r][end] == '#') ++end;
                FrontRect(m, left - px * (float)end, left - px * (float)c, top - px * (float)(r + 1), top - px * (float)r, z, color);
                c = end;
            }
        }
    }

    // Бичгийг төвд нь (x = center) байрлуулна
    void SignText(MeshBuilder& m, const char* text, float center, float top, float px, float z, Color color)
    {
        const int length = (int)std::strlen(text);
        const float width = (float)(length * 6 - 1) * px;
        float left = center + width * 0.5f;
        for (int i = 0; i < length; ++i, left -= 6.0f * px)
        {
            if (const char* const* glyph = Glyph(text[i])) PixelArt(m, glyph, 7, left, top, px, z, color);
        }
    }

    void Soyombo(MeshBuilder& m, float center, float top, float px, float z)
    {
        static const char* const rows[] = {
            "....#....", "...###...", "..#####..", ".##.#.##.", ".........", "..#####..", ".#######.", "..#####..",
            ".........", ".#.....#.", "..#####..", ".........", "#########", "#.#####.#", "#..###..#", "#...#...#",
            "#.......#", "#########", "#.......#", "#.##.##.#", "#.##.##.#", "#.......#", "#########", "#.......#",
            "#########",
        };
        PixelArt(m, rows, 25, center + 4.5f * px, top, px, z, kSoyomboRed);
    }

    // Терминалын ертөнцийн координат дахь тэгш өнцөгтийн мөргөлдөөн
    void TerminalCollider(Scene& s, float x0, float x1, float z0, float z1, float base, float height, bool walkable = false)
    {
        const Vector2 corners[4] = { kTerminalFrame.ToWorld(x0, z0), kTerminalFrame.ToWorld(x1, z0),
                                     kTerminalFrame.ToWorld(x1, z1), kTerminalFrame.ToWorld(x0, z1) };
        PolygonColliders(s, corners, 4, base, height, walkable);
    }

    // Жигүүрийн сум хэлбэрийн металл дээвэр. inner: төв блок талын x, outer: гадна үзүүрийн x (тэмдэгтэйгээр)
    void WingRoof(MeshBuilder& m, float inner, float outer)
    {
        const float dir = outer > inner ? 1.0f : -1.0f;
        const float y0 = kWingTop, y1 = kWingTop + 3.4f;
        const float zf = -25.5f, zb = 25.5f;
        const float tip = outer + dir * 7.0f;             // хурц хамар
        const float corner = outer + dir * 1.5f;          // эгнээний булан
        const float ridge = tip - dir * 22.0f;             // нурууны гадна үзүүр
        const Vector3 fi = { inner, y0, zf }, bi = { inner, y0, zb }, fc = { corner, y0, zf }, bc = { corner, y0, zb };
        const Vector3 t = { tip, y0, 0.0f }, ri = { inner, y1, 0.0f }, ro = { ridge, y1, 0.0f };
        TwoSidedQuad(m, fi, fc, ro, ri, kTerminalRoof);
        TwoSidedQuad(m, bc, bi, ri, ro, ScaleColor(kTerminalRoof, 0.92f));
        m.Triangle(fc, t, ro, ScaleColor(kTerminalRoof, 1.04f));
        m.Triangle(ro, t, fc, ScaleColor(kTerminalRoof, 1.04f));
        m.Triangle(t, bc, ro, ScaleColor(kTerminalRoof, 0.96f));
        m.Triangle(ro, bc, t, ScaleColor(kTerminalRoof, 0.96f));
        // Доод тал (хаяавч) ба бараан хүрээ
        const Vector3 outline[5] = { fi, fc, t, bc, bi };
        for (int i = 0; i < 4; ++i)
        {
            const Vector3 a = outline[i], b = outline[i + 1];
            TwoSidedQuad(m, { a.x, y0 - 0.7f, a.z }, { b.x, y0 - 0.7f, b.z }, b, a, kTerminalFascia);
            TwoSidedQuad(m, { a.x, y0 - 0.7f, a.z }, { b.x, y0 - 0.7f, b.z }, { b.x, y0 - 0.7f, 0.0f }, { a.x, y0 - 0.7f, 0.0f },
                         { 204, 204, 198, 255 });
        }
    }

    // Гүүрэн зам: тэнхлэгийн цэгүүд (x, z, дээд өндөр). solid: доогуураа битүү бетон (налуу хэсэг)
    struct DeckPoint { float x, z, y; };
    void BuildDeck(Scene& s, const std::vector<DeckPoint>& path, float halfWidth, bool solid, float ground)
    {
        MeshBuilder& m = s.solid;
        const Color asphalt = { 74, 76, 80, 255 }, concrete = { 196, 194, 186, 255 };
        for (size_t i = 0; i + 1 < path.size(); ++i)
        {
            const DeckPoint a = path[i], b = path[i + 1];
            auto side = [&](const DeckPoint& p, size_t k) {
                const DeckPoint q0 = path[k == 0 ? 0 : k - 1], q1 = path[std::min(k + 1, path.size() - 1)];
                const Vector2 tangent = Vector2Normalize({ q1.x - q0.x, q1.z - q0.z });
                (void)p;
                return Vector2{ -tangent.y, tangent.x };
            };
            const Vector2 na = side(a, i), nb = side(b, i + 1);
            auto at = [&](const DeckPoint& p, Vector2 n, float offset, float y) { return Vector3{ p.x + n.x * offset, y, p.z + n.y * offset }; };
            // Замын гадаргуу ба зураас
            TwoSidedQuad(m, at(a, na, -halfWidth, a.y), at(b, nb, -halfWidth, b.y), at(b, nb, halfWidth, b.y), at(a, na, halfWidth, a.y), asphalt);
            if (i % 2 == 0)
            {
                TwoSidedQuad(m, at(a, na, -0.1f, a.y + 0.02f), at(b, nb, -0.1f, b.y + 0.02f), at(b, nb, 0.1f, b.y + 0.02f),
                             at(a, na, 0.1f, a.y + 0.02f), kRoadPaint);
            }
            for (float edge : { -1.0f, 1.0f })
            {
                const float o = edge * halfWidth;
                // Хашлага (бетон хана 1 м)
                TwoSidedQuad(m, at(a, na, o, a.y), at(b, nb, o, b.y), at(b, nb, o, b.y + 1.0f), at(a, na, o, a.y + 1.0f), concrete);
                TwoSidedQuad(m, at(a, na, o - edge * 0.3f, a.y + 1.0f), at(b, nb, o - edge * 0.3f, b.y + 1.0f),
                             at(b, nb, o, b.y + 1.0f), at(a, na, o, a.y + 1.0f), ScaleColor(concrete, 1.05f));
                // Доод хэсэг: битүү хана эсвэл зузаан ирмэг
                const float bottomA = solid ? 0.0f : a.y - 1.4f, bottomB = solid ? 0.0f : b.y - 1.4f;
                TwoSidedQuad(m, at(a, na, o, bottomA), at(b, nb, o, bottomB), at(b, nb, o, b.y), at(a, na, o, a.y), ScaleColor(concrete, 0.94f));
            }
            if (!solid)
            {
                TwoSidedQuad(m, at(a, na, -halfWidth, a.y - 1.4f), at(b, nb, -halfWidth, b.y - 1.4f), at(b, nb, halfWidth, b.y - 1.4f),
                             at(a, na, halfWidth, a.y - 1.4f), ScaleColor(concrete, 0.8f));
            }

            // Мөргөлдөөн (ертөнцийн координатаар): гадаргуу дээр алхаж болно
            const Vector3 c0 = at(a, na, -halfWidth, 0), c1 = at(b, nb, -halfWidth, 0), c2 = at(b, nb, halfWidth, 0), c3 = at(a, na, halfWidth, 0);
            const Vector2 corners[4] = { kTerminalFrame.ToWorld(c0.x, c0.z), kTerminalFrame.ToWorld(c1.x, c1.z),
                                         kTerminalFrame.ToWorld(c2.x, c2.z), kTerminalFrame.ToWorld(c3.x, c3.z) };
            const float top = (a.y + b.y) * 0.5f;
            if (solid) PolygonColliders(s, corners, 4, ground, std::max(0.2f, top), true);
            else PolygonColliders(s, corners, 4, ground + top - 1.4f, 1.4f, true);
        }
    }

    void BuildTerminal(Scene& s)
    {
        const TerminalFrame& f = kTerminalFrame;
        const float ground = s.terrain.HeightAt(f.center.x, f.center.y);
        MeshBuilder& m = s.solid;
        m.SetTransform(MatrixMultiply(MatrixRotateY(f.Angle()), MatrixTranslate(f.center.x, ground, f.center.y)));

        const float L = 88.0f, D = 23.0f, C = 25.0f; // хагас урт, хагас гүн, төв блокийн хагас өргөн
        // --- Жигүүрүүд: их бие, урд нүүрийн шил, баганууд, хаалганууд ---
        for (float sign : { -1.0f, 1.0f })
        {
            const float x0 = sign < 0.0f ? -L : C, x1 = sign < 0.0f ? -C : L;
            m.Box({ (x0 + x1) * 0.5f, kWingTop * 0.5f, 0.0f }, { x1 - x0, kWingTop, D * 2.0f }, kTerminalWhite);
            // 2-р давхрын (гарах танхим) шилэн зурвас, хүрээ
            FrontRect(m, x0 + 1.0f, x1 - 1.0f, 8.4f, 13.2f, -D - 0.05f, kTerminalGlass);
            for (float x = x0 + 1.0f; x <= x1 - 0.9f; x += 2.4f) FrontRect(m, x, x + 0.18f, 8.4f, 13.2f, -D - 0.1f, { 210, 210, 206, 255 });
            FrontRect(m, x0 + 1.0f, x1 - 1.0f, 10.7f, 10.9f, -D - 0.1f, { 210, 210, 206, 255 });
            // Газрын давхар: багана ба шар хүрээтэй шилэн хаалганууд
            for (float x = x0 + 4.0f; x < x1 - 2.0f; x += 8.0f)
            {
                m.Box({ x, 3.3f, -D - 0.5f }, { 1.0f, 6.6f, 1.0f }, ScaleColor(kTerminalWhite, 0.95f));
                FrontRect(m, x + 1.2f, x + 6.8f, 0.0f, 4.2f, -D - 0.05f, kTerminalGlass);
                FrontRect(m, x + 2.8f, x + 5.2f, 0.0f, 3.2f, -D - 0.1f, { 210, 168, 40, 255 });
                FrontRect(m, x + 3.0f, x + 5.0f, 0.0f, 3.0f, -D - 0.12f, { 44, 62, 76, 255 });
            }
            // Арын (перроны) тал: цонхны зурвас, хаалганууд
            BackRect(m, x0 + 1.0f, x1 - 1.0f, 8.6f, 11.6f, D + 0.05f, kTerminalGlass);
            for (float x = x0 + 6.0f; x < x1 - 4.0f; x += 14.0f) BackRect(m, x, x + 3.6f, 0.0f, 3.4f, D + 0.05f, { 120, 126, 132, 255 });
            // Гадна үзүүрийн цонхнууд
            const float endX = sign < 0.0f ? x0 : x1;
            for (float z = -D + 3.0f; z < D - 2.0f; z += 4.0f)
            {
                for (float y : { 1.2f, 5.2f, 9.2f })
                {
                    const float e = endX + sign * 0.05f;
                    if (sign > 0.0f) m.Quad({ e, y, z }, { e, y, z + 1.8f }, { e, y + 1.8f, z + 1.8f }, { e, y + 1.8f, z }, kTerminalGlass);
                    else m.Quad({ e, y, z + 1.8f }, { e, y, z }, { e, y + 1.8f, z }, { e, y + 1.8f, z + 1.8f }, kTerminalGlass);
                }
            }
            WingRoof(m, sign * C, sign * L);
            // Дээврийн антенн
            m.Box({ sign * (L - 10.0f), kWingTop + 4.6f, 6.0f }, { 0.2f, 2.4f, 0.2f }, kMetal);
            TerminalCollider(s, x0, x1, -D, D, ground, kWingTop + 3.0f);
        }

        // --- Төв блок: доод хэсэг үүдтэй, дээд хэсэг урагшаа 4 м цухуйсан ---
        m.Box({ 0.0f, 3.6f, 0.0f }, { C * 2.0f, 7.2f, D * 2.0f }, kTerminalWhite);
        m.Box({ 0.0f, (7.2f + kCenterTop) * 0.5f, -2.0f }, { C * 2.0f, kCenterTop - 7.2f, D * 2.0f + 4.0f }, kTerminalWhite);
        m.Box({ 0.0f, kCenterTop + 0.5f, -2.0f }, { C * 2.0f + 1.0f, 1.0f, D * 2.0f + 5.0f }, { 196, 196, 190, 255 }); // парапет
        const float zFront = -D - 4.0f;
        // Алтан хээтэй шилэн нүүр
        FrontRect(m, -C + 3.0f, C - 3.0f, 8.0f, 18.4f, zFront - 0.05f, { 34, 44, 54, 255 });
        for (float x = -C + 3.0f; x <= C - 3.0f + 0.01f; x += 1.1f) FrontRect(m, x - 0.07f, x + 0.07f, 8.0f, 18.4f, zFront - 0.1f, kLatticeGold);
        for (float y = 8.0f; y <= 18.41f; y += 1.3f) FrontRect(m, -C + 3.0f, C - 3.0f, y - 0.07f, y + 0.07f, zFront - 0.1f, kLatticeGold);
        for (float x = -C + 3.55f; x < C - 3.0f; x += 2.2f)
        {
            for (float y = 8.65f; y < 18.0f; y += 2.6f) FrontRect(m, x - 0.25f, x + 0.25f, y - 0.25f, y + 0.25f, zFront - 0.12f, kLatticeGold);
        }
        // Бичгийн самбар: CHINGGIS KHAAN / INTERNATIONAL AIRPORT, хоёр талд соёмбо
        m.Box({ 0.0f, 22.3f, zFront - 0.3f }, { C * 2.0f - 1.0f, 6.6f, 0.6f }, { 238, 238, 234, 255 });
        SignText(m, "CHINGGIS KHAAN", 0.0f, 25.0f, 0.42f, zFront - 0.65f, kSignBlue);
        SignText(m, "INTERNATIONAL AIRPORT", 0.0f, 21.4f, 0.24f, zFront - 0.65f, kSignBlue);
        for (float x : { -21.4f, 21.4f }) Soyombo(m, x, 25.4f, 0.25f, zFront - 0.65f);
        // Гол хаалганууд (гүүрэн замын доор)
        for (float x = -C + 4.0f; x < C - 3.0f; x += 7.0f)
        {
            FrontRect(m, x, x + 5.6f, 0.0f, 5.6f, -D - 0.05f, kTerminalGlass);
            FrontRect(m, x + 1.6f, x + 4.0f, 0.0f, 3.2f, -D - 0.1f, { 210, 168, 40, 255 });
            FrontRect(m, x + 1.8f, x + 3.8f, 0.0f, 3.0f, -D - 0.12f, { 44, 62, 76, 255 });
        }
        BackRect(m, -C + 2.0f, C - 2.0f, 8.6f, 20.0f, D + 0.05f, kTerminalGlass);
        TerminalCollider(s, -C, C, -D, D, ground, 7.2f);
        TerminalCollider(s, -C, C, zFront, D, ground + 7.2f, kCenterTop - 6.2f);

        // --- Дээвэр дээрх нислэгийн удирдлагын цамхаг (арын хэсэгт) ---
        const float tz = 12.0f;
        m.Cylinder({ 0.0f, kCenterTop + 1.0f, tz }, 2.6f, 2.4f, 14.0f, 12, kTerminalWhite);
        m.Cylinder({ 0.0f, kCenterTop + 15.0f, tz }, 4.0f, 4.8f, 1.0f, 12, kTerminalWhite);
        m.Cylinder({ 0.0f, kCenterTop + 16.0f, tz }, 4.8f, 5.2f, 3.4f, 12, { 58, 84, 100, 255 });
        m.Cylinder({ 0.0f, kCenterTop + 19.4f, tz }, 5.6f, 3.4f, 0.9f, 12, kTerminalWhite);
        m.Cylinder({ 0.0f, kCenterTop + 20.3f, tz }, 0.12f, 0.06f, 4.0f, 5, kMetal);
        TerminalCollider(s, -2.6f, 2.6f, tz - 2.6f, tz + 2.6f, ground + kCenterTop, 15.0f);
        TerminalCollider(s, -4.8f, 4.8f, tz - 4.8f, tz + 4.8f, ground + kCenterTop + 15.0f, 5.3f);

        // --- Гүүрэн зам: урд нүүрийн дагуу 2-р давхрын түвшинд, үзүүрүүдээрээ эргэж газар руу буух ---
        const float roadZ = -35.0f, roadHalf = 6.0f;
        // Явган хүний зам (барилга ба замын хооронд)
        TwoSidedQuad(m, { -L - 8.0f, kDeckTop, -D }, { L + 8.0f, kDeckTop, -D }, { L + 8.0f, kDeckTop, roadZ + roadHalf },
                     { -L - 8.0f, kDeckTop, roadZ + roadHalf }, { 186, 182, 172, 255 });
        TwoSidedQuad(m, { -L - 8.0f, kDeckTop - 1.0f, -D }, { L + 8.0f, kDeckTop - 1.0f, -D },
                     { L + 8.0f, kDeckTop - 1.0f, roadZ + roadHalf }, { -L - 8.0f, kDeckTop - 1.0f, roadZ + roadHalf }, { 150, 148, 142, 255 });
        TerminalCollider(s, -L - 8.0f, L + 8.0f, roadZ + roadHalf, -D, ground + kDeckTop - 1.0f, 1.0f, true);
        std::vector<DeckPoint> straight;
        for (float x = -96.0f; x <= 96.01f; x += 12.0f) straight.push_back({ x, roadZ, kDeckTop });
        const float R = 13.0f;
        for (float sign : { -1.0f, 1.0f })
        {
            std::vector<DeckPoint> loop;
            for (int k = 0; k <= 12; ++k)
            {
                const float phi = PI * (float)k / 12.0f;
                loop.push_back({ sign * (96.0f + R * std::sin(phi)), roadZ - R + R * std::cos(phi), kDeckTop - 3.0f * (float)k / 12.0f });
            }
            for (int k = 1; k <= 14; ++k)
            {
                const float t = (float)k / 14.0f;
                loop.push_back({ sign * (96.0f - 56.0f * t), roadZ - 2.0f * R, (kDeckTop - 3.0f) * (1.0f - t) });
            }
            BuildDeck(s, loop, roadHalf, true, ground);
        }
        BuildDeck(s, straight, roadHalf, false, ground);
        // Гадна талын хашлагын мөргөлдөөн (2-р давхраас унахгүй)
        TerminalCollider(s, -96.0f, 96.0f, roadZ - roadHalf - 0.3f, roadZ - roadHalf + 0.3f, ground + kDeckTop, 1.0f);
        m.ResetTransform();
    }

    // Терминалаас онгоц руу гарах өндөрлөг шилэн коридорууд (OSM-ийн нимгэн "салаанууд")
    void JetBridges(Scene& s)
    {
        static const std::vector<std::vector<Vector2>> piers = {
            { { -559.0f, 1383.0f }, { -539.0f, 1363.0f }, { -553.0f, 1348.0f } },
            { { -512.0f, 1425.0f }, { -493.0f, 1408.0f }, { -506.0f, 1395.0f } },
            { { -508.0f, 1425.0f }, { -446.0f, 1431.0f }, { -431.0f, 1414.0f } },
            { { -446.0f, 1431.0f }, { -397.0f, 1484.0f }, { -384.0f, 1466.0f } },
        };
        for (const std::vector<Vector2>& pier : piers)
        {
            for (size_t i = 0; i + 1 < pier.size(); ++i)
            {
                const Vector2 a = pier[i], b = pier[i + 1];
                const Vector2 d = Vector2Subtract(b, a);
                const float length = Vector2Length(d);
                const Vector2 mid = Vector2Lerp(a, b, 0.5f);
                const float ground = s.terrain.HeightAt(mid.x, mid.y);
                s.solid.SetTransform(MatrixMultiply(MatrixRotateY(std::atan2(-d.y, d.x)), MatrixTranslate(mid.x, ground, mid.y)));
                s.solid.Box({ 0.0f, 5.6f, 0.0f }, { length + 2.6f, 3.0f, 2.8f }, { 222, 222, 216, 255 });
                for (float side : { -1.0f, 1.0f }) s.solid.Box({ 0.0f, 5.9f, side * 1.42f }, { length + 2.4f, 1.0f, 0.06f }, kTerminalGlass);
                s.solid.ResetTransform();
                for (float t = 0.0f; t <= 1.0f; t += std::max(0.25f, 12.0f / length))
                {
                    const Vector2 p = Vector2Lerp(a, b, t);
                    s.solid.Cylinder({ p.x, s.terrain.HeightAt(p.x, p.y), p.y }, 0.3f, 0.3f, 4.1f, 8, kMetal);
                    s.Collider({ p.x, ground + 2.05f, p.y }, { 0.6f, 4.1f, 0.6f });
                }
                const Vector2 n = Vector2Scale(Vector2Normalize({ -d.y, d.x }), 1.4f);
                const Vector2 corners[4] = { Vector2Subtract(a, n), Vector2Subtract(b, n), Vector2Add(b, n), Vector2Add(a, n) };
                PolygonColliders(s, corners, 4, ground + 4.1f, 3.0f);
            }
        }
    }

    void BuildAirport(Scene& s)
    {
        using namespace airport;
        for (const Ring& apron : kAprons)
        {
            PavePolygon(s, Polygon(kPoints + apron.first, kPoints + apron.first + apron.count), kYApron, kApronConcrete, 0.035f);
        }
        {
            Vector2 corners[4];
            AirportParking(corners);
            PavePolygon(s, Polygon(corners, corners + 4), kYApron, { 92, 92, 94, 255 }, 0.03f);
            // Терминалын урд талбай (гүүрэн замын доорх ирэх замын эгнээ)
            const TerminalFrame& f = kTerminalFrame;
            const Polygon forecourt = { f.ToWorld(-116.0f, -72.0f), f.ToWorld(116.0f, -72.0f), f.ToWorld(116.0f, -22.0f),
                                        f.ToWorld(-116.0f, -22.0f) };
            PavePolygon(s, forecourt, kYApron + 0.005f, { 84, 84, 88, 255 }, 0.02f);
        }
        for (const Strip& strip : kStrips)
        {
            if (strip.kind == 2) BuildTaxiway(s, strip);
        }
        for (const Strip& strip : kStrips)
        {
            if (strip.kind == 2) continue;
            // Гол (засмал) ХБЗ: 14/32, хайрган нь 15/33. Хойд үзүүрт нь 14 (15), өмнөд үзүүрт 32 (33).
            const bool northFirst = kPoints[strip.first].y > kPoints[strip.first + strip.count - 1].y;
            const int north = strip.kind == 0 ? 14 : 15;
            BuildRunway(s, strip, northFirst ? north : north + 18);
        }
        for (const Ring& pad : kHelipads) BuildHelipad(s, pad);
        for (const Building& building : kBuildings)
        {
            if (building.kind == 0 && building.height > 12.0f) continue; // гол терминалыг тусгай загвараар
            AirportBuilding(s, building);
        }
        BuildTerminal(s);
        JetBridges(s);
        BuildRoad(s, AirportAccessRoad());

        // Коридорын үзүүрт (хаалганд) зогссон онгоцнууд, нэг нь эвдэрч гэдсэн дээрээ хэвтсэн
        const Color miatBlue = { 30, 64, 140, 255 }, hunnuRed = { 170, 40, 44, 255 }, aeroGreen = { 40, 120, 90, 255 };
        const Vector2 pierEnds[] = { { -553.0f, 1348.0f }, { -506.0f, 1395.0f }, { -431.0f, 1414.0f }, { -384.0f, 1466.0f } };
        const Color tails[] = { miatBlue, hunnuRed, miatBlue, aeroGreen };
        for (int i = 0; i < 4; ++i)
        {
            const Vector2 nose = Vector2Negate(kTerminalFrame.axisZ);
            const Vector2 gate = Vector2Add(pierEnds[i], Vector2Scale(kTerminalFrame.axisZ, 21.0f));
            Airliner(s, gate, nose, tails[i], tails[i], false);
            // Шатахууны машин онгоцны хажууд
            Truck(s, gate.x - nose.y * 9.0f, gate.y + nose.x * 9.0f, std::fabs(nose.x) > std::fabs(nose.y),
                  { 220, 214, 200, 255 }, { 230, 150, 40, 255 });
        }
        Airliner(s, { -150.0f, 1500.0f }, { 0.7f, -0.71f }, aeroGreen, aeroGreen, true);

        // Нисдэг тэрэгнүүд
        for (int i = 0; i < 2; ++i)
        {
            const Ring& pad = kHelipads[i * 2];
            Vector2 c = { 0.0f, 0.0f };
            for (int k = 0; k < pad.count; ++k) c = Vector2Add(c, kPoints[pad.first + k]);
            c = Vector2Scale(c, 1.0f / (float)pad.count);
            Helicopter(s, c, { 0.62f + 0.2f * (float)i, -0.78f }, i == 0 ? Color{ 92, 104, 84, 255 } : Color{ 196, 200, 206, 255 });
        }


        // Зогсоолд орхигдсон машинууд
        {
            Vector2 corners[4];
            AirportParking(corners);
            for (int i = 0; i < 16; ++i)
            {
                const float a = s.rng.Range(0.08f, 0.92f), b = s.rng.Range(0.15f, 0.85f);
                const Vector2 p = Vector2Lerp(Vector2Lerp(corners[0], corners[1], a), Vector2Lerp(corners[3], corners[2], a), b);
                Car(s, p.x, p.y, s.rng.Chance(0.5f), RandomCarPaint(s.rng));
            }
        }
        // Перрон дээр тархсан тэрэг, торх, хайрцаг
        for (int i = 0; i < 14; ++i)
        {
            const Vector2 p = { s.rng.Range(-700.0f, -300.0f), s.rng.Range(1150.0f, 1450.0f) };
            if (!s.layout.Paved(p.x, p.y)) continue;
            if (s.rng.Chance(0.5f)) Crate(s, p.x, p.y, 1.2f, 0.0f, kOlive);
            else Barrel(s, p.x, p.y, s.rng.Chance(0.5f) ? kRust : Color{ 60, 96, 130, 255 });
        }
    }

    // ------------------------------------------------------------------ Байгаль: мод, овоо, нуур, мал

    Vector3 GroundNormalAt(const Scene& s, float x, float z)
    {
        const float d = 3.0f;
        const float hx = s.terrain.HeightAt(x + d, z) - s.terrain.HeightAt(x - d, z);
        const float hz = s.terrain.HeightAt(x, z + d) - s.terrain.HeightAt(x, z - d);
        return Vector3Normalize({ -hx, 2.0f * d, -hz });
    }

    // Шинэс: нарийн их бие, давхарласан шовгор мөчир (хойд аймгийн ой)
    void Larch(Scene& s, float x, float z, float height)
    {
        const float ground = s.terrain.HeightAt(x, z) - 0.2f;
        const Color bark = JitterColor({ 92, 70, 52, 255 }, s.rng, 0.08f);
        const Color needles = JitterColor({ 58, 88, 52, 255 }, s.rng, 0.1f);
        s.solid.Cylinder({ x, ground, z }, 0.16f + height * 0.012f, 0.06f, height, 6, bark);
        const int tiers = 4;
        for (int i = 0; i < tiers; ++i)
        {
            const float t = (float)i / (float)tiers;
            const float base = ground + height * (0.25f + 0.68f * t);
            const float radius = (1.0f - t * 0.75f) * height * 0.19f;
            s.solid.Cylinder({ x, base, z }, radius, 0.0f, height * 0.32f, 7, ScaleColor(needles, 0.9f + 0.12f * t));
        }
        s.Collider({ x, ground + 1.5f, z }, { 0.5f, 3.0f, 0.5f });
        s.Shadow(x - height * 0.15f, z - height * 0.15f, x + height * 0.15f, z + height * 0.15f, height * 0.25f, height);
    }

    // Хус: цагаан их бие, цайвар ногоон титэм
    void Birch(Scene& s, float x, float z, float height)
    {
        const float ground = s.terrain.HeightAt(x, z) - 0.2f;
        s.solid.Cylinder({ x, ground, z }, 0.14f, 0.07f, height * 0.85f, 6, { 222, 220, 210, 255 });
        for (int i = 0; i < 3; ++i)
        {
            s.solid.Box({ x, ground + 1.0f + 1.3f * (float)i, z }, { 0.3f, 0.12f, 0.3f }, { 40, 40, 38, 255 }); // хар толбо
        }
        const Color leaves = JitterColor({ 112, 150, 72, 255 }, s.rng, 0.1f);
        for (int i = 0; i < 3; ++i)
        {
            const float r = height * s.rng.Range(0.16f, 0.22f);
            s.solid.Blob({ x + s.rng.Range(-0.6f, 0.6f), ground + height * (0.62f + 0.12f * (float)i), z + s.rng.Range(-0.6f, 0.6f) },
                         { r, r * 1.2f, r }, leaves, s.rng, 0.1f);
        }
        s.Collider({ x, ground + 1.5f, z }, { 0.4f, 3.0f, 0.4f });
        s.Shadow(x - height * 0.2f, z - height * 0.2f, x + height * 0.2f, z + height * 0.2f, height * 0.45f, height);
    }

    // Хайлаас: тал хээрийн ганц бие, өргөн титэмтэй мод
    void Elm(Scene& s, float x, float z, float height)
    {
        const float ground = s.terrain.HeightAt(x, z) - 0.2f;
        const Color bark = JitterColor({ 96, 82, 66, 255 }, s.rng, 0.08f);
        s.solid.Cylinder({ x, ground, z }, 0.32f, 0.2f, height * 0.5f, 7, bark);
        const float lean = s.rng.Range(-0.8f, 0.8f);
        s.solid.Tube({ x, ground + height * 0.45f, z }, { x + lean * 1.6f, ground + height * 0.75f, z + 0.9f }, 0.18f, 0.08f, 5, bark);
        s.solid.Tube({ x, ground + height * 0.45f, z }, { x - 1.4f, ground + height * 0.7f, z - lean }, 0.16f, 0.07f, 5, bark);
        const Color leaves = JitterColor({ 86, 114, 62, 255 }, s.rng, 0.1f);
        for (int i = 0; i < 5; ++i)
        {
            const float angle = (float)i * 1.26f + s.rng.Range(0.0f, 0.5f);
            const float r = height * s.rng.Range(0.18f, 0.24f);
            const float spread = i == 0 ? 0.0f : height * 0.22f;
            s.solid.Blob({ x + std::cos(angle) * spread, ground + height * (0.78f + s.rng.Range(-0.05f, 0.08f)), z + std::sin(angle) * spread },
                         { r * 1.2f, r * 0.8f, r * 1.2f }, leaves, s.rng, 0.12f);
        }
        s.Collider({ x, ground + 1.5f, z }, { 0.7f, 3.0f, 0.7f });
        s.Shadow(x - height * 0.4f, z - height * 0.4f, x + height * 0.4f, z + height * 0.4f, height * 0.55f, height);
    }

    // Заг: говийн мушгирсан саарал бут
    void Saxaul(Scene& s, float x, float z)
    {
        const float ground = s.terrain.HeightAt(x, z) - 0.1f;
        const Color wood = JitterColor({ 128, 116, 100, 255 }, s.rng, 0.1f);
        for (int i = 0; i < 4; ++i)
        {
            const float a = s.rng.Range(0.0f, 2.0f * PI);
            const float h = s.rng.Range(1.0f, 2.2f);
            s.solid.Tube({ x, ground, z }, { x + std::cos(a) * 0.9f, ground + h, z + std::sin(a) * 0.9f }, 0.09f, 0.03f, 4, wood);
            s.solid.Blob({ x + std::cos(a) * 0.9f, ground + h, z + std::sin(a) * 0.9f }, { 0.45f, 0.3f, 0.45f },
                         JitterColor({ 120, 128, 88, 255 }, s.rng, 0.1f), s.rng, 0.1f);
        }
        s.Collider({ x, ground + 0.8f, z }, { 1.0f, 1.6f, 1.0f });
    }

    // Овоо: уулын оройн чулуун овоолго, төвдөө шон, хөх хадагтай
    void Ovoo(Scene& s, float x, float z)
    {
        const float ground = s.terrain.HeightAt(x, z) - 0.2f;
        for (int layer = 0; layer < 4; ++layer)
        {
            const float radius = 2.6f - 0.6f * (float)layer;
            const int stones = 9 - layer * 2;
            for (int i = 0; i < stones; ++i)
            {
                const float a = 2.0f * PI * (float)i / (float)stones + (float)layer * 0.4f;
                const float r = s.rng.Range(0.35f, 0.6f);
                s.solid.Rock({ x + std::cos(a) * radius * 0.7f, ground + 0.3f + 0.5f * (float)layer, z + std::sin(a) * radius * 0.7f },
                             { r, r * 0.7f, r }, JitterColor({ 132, 128, 120, 255 }, s.rng, 0.12f), s.rng);
            }
        }
        s.solid.Cylinder({ x, ground, z }, 0.09f, 0.06f, 5.0f, 6, { 120, 92, 64, 255 });
        // Хадаг: оройгоос доош унжсан хөх, цагаан, шар туузууд
        const Color ribbons[] = { { 40, 92, 188, 255 }, { 232, 232, 226, 255 }, { 40, 92, 188, 255 }, { 220, 186, 60, 255 }, { 60, 140, 80, 255 } };
        for (int i = 0; i < 10; ++i)
        {
            const float a = 2.0f * PI * (float)i / 10.0f;
            const Vector3 top = { x, ground + 4.8f, z };
            const Vector3 end = { x + std::cos(a) * 3.0f, ground + 1.2f, z + std::sin(a) * 3.0f };
            const Vector3 side = Vector3Scale(Vector3Normalize({ -std::sin(a), 0.0f, std::cos(a) }), 0.12f);
            const Color c = ribbons[i % 5];
            s.solid.Quad(Vector3Subtract(top, side), Vector3Add(top, side), Vector3Add(end, side), Vector3Subtract(end, side), c);
            s.solid.Quad(Vector3Subtract(end, side), Vector3Add(end, side), Vector3Add(top, side), Vector3Subtract(top, side), c);
        }
        s.Collider({ x, ground + 1.1f, z }, { 3.6f, 2.2f, 3.6f });
    }

    // --- Мал: хонь ямаа, тэмээ, сарлаг (хөдөлгөөнгүй, бэлчиж буй) ---
    void Sheep(Scene& s, float x, float z, float heading)
    {
        const float ground = s.terrain.HeightAt(x, z);
        static const Color wool[] = { { 230, 224, 206, 255 }, { 220, 212, 192, 255 }, { 236, 232, 222, 255 }, { 90, 72, 60, 255 }, { 50, 46, 44, 255 } };
        const bool goat = s.rng.Chance(0.3f);
        const Color body = JitterColor(wool[goat ? s.rng.Int(3, 4) : s.rng.Int(0, 2)], s.rng, 0.05f);
        MeshBuilder& m = s.solid;
        m.SetTransform(MatrixMultiply(MatrixRotateY(heading), MatrixTranslate(x, ground, z)));
        m.Ellipsoid({ 0.0f, 0.62f, 0.0f }, { goat ? 0.24f : 0.32f, goat ? 0.24f : 0.3f, 0.5f }, 3, 6, body);
        const bool grazing = s.rng.Chance(0.6f);
        m.Box({ 0.0f, grazing ? 0.34f : 0.72f, 0.55f }, { 0.16f, 0.2f, 0.28f }, goat ? body : Color{ 40, 36, 34, 255 });
        for (int i = 0; i < 4; ++i)
        {
            m.Box({ (i % 2 ? 0.14f : -0.14f), 0.22f, (i < 2 ? 0.3f : -0.3f) }, { 0.07f, 0.44f, 0.07f }, { 50, 44, 40, 255 });
        }
        m.ResetTransform();
    }

    void Camel(Scene& s, float x, float z, float heading)
    {
        const float ground = s.terrain.HeightAt(x, z);
        const Color hair = JitterColor({ 168, 132, 88, 255 }, s.rng, 0.06f);
        MeshBuilder& m = s.solid;
        const Matrix place = MatrixMultiply(MatrixRotateY(heading), MatrixTranslate(x, ground, z));
        m.SetTransform(place);
        m.Ellipsoid({ 0.0f, 1.7f, 0.0f }, { 0.5f, 0.5f, 1.1f }, 4, 8, hair);
        m.Ellipsoid({ 0.0f, 2.15f, 0.45f }, { 0.32f, 0.38f, 0.32f }, 4, 6, ScaleColor(hair, 0.9f)); // хоёр бөх
        m.Ellipsoid({ 0.0f, 2.15f, -0.45f }, { 0.32f, 0.38f, 0.32f }, 4, 6, ScaleColor(hair, 0.9f));
        m.Tube({ 0.0f, 1.8f, 0.95f }, { 0.0f, 2.5f, 1.5f }, 0.2f, 0.14f, 6, hair);
        m.Ellipsoid({ 0.0f, 2.55f, 1.75f }, { 0.16f, 0.17f, 0.35f }, 3, 6, hair);
        for (int i = 0; i < 4; ++i)
        {
            const float lx = i % 2 ? 0.25f : -0.25f, lz = i < 2 ? 0.7f : -0.7f;
            m.Tube({ lx, 1.45f, lz }, { lx, 0.0f, lz }, 0.11f, 0.07f, 5, ScaleColor(hair, 0.85f));
        }
        m.ResetTransform();
        s.Collider({ x, ground + 1.2f, z }, { 1.4f, 2.4f, 1.4f });
    }

    void Yak(Scene& s, float x, float z, float heading)
    {
        const float ground = s.terrain.HeightAt(x, z);
        const Color hair = JitterColor(s.rng.Chance(0.25f) ? Color{ 200, 196, 186, 255 } : Color{ 46, 38, 34, 255 }, s.rng, 0.06f);
        MeshBuilder& m = s.solid;
        m.SetTransform(MatrixMultiply(MatrixRotateY(heading), MatrixTranslate(x, ground, z)));
        m.Ellipsoid({ 0.0f, 1.0f, 0.0f }, { 0.55f, 0.55f, 0.95f }, 4, 8, hair);
        m.Cylinder({ 0.0f, 0.35f, 0.0f }, 0.6f, 0.5f, 0.5f, 8, ScaleColor(hair, 0.85f)); // унжсан урт үс
        m.Ellipsoid({ 0.0f, 1.32f, 0.35f }, { 0.4f, 0.32f, 0.35f }, 3, 6, hair);         // бөх
        m.Ellipsoid({ 0.0f, 0.9f, 1.0f }, { 0.25f, 0.25f, 0.32f }, 3, 6, hair);
        m.Tube({ 0.15f, 1.05f, 1.0f }, { 0.35f, 1.3f, 1.05f }, 0.05f, 0.02f, 4, { 220, 212, 190, 255 });
        m.Tube({ -0.15f, 1.05f, 1.0f }, { -0.35f, 1.3f, 1.05f }, 0.05f, 0.02f, 4, { 220, 212, 190, 255 });
        m.ResetTransform();
        s.Collider({ x, ground + 0.8f, z }, { 1.2f, 1.6f, 1.2f });
    }

    void BuildLivestock(Scene& s)
    {
        for (const Vector2& camp : s.layout.camps)
        {
            if (!s.rng.Chance(0.75f)) continue;
            // Отроос 25-55 м-т бэлчиж буй сүрэг
            const float a = s.rng.Range(0.0f, 2.0f * PI);
            const float d = s.rng.Range(25.0f, 55.0f);
            const float fx = camp.x + std::cos(a) * d, fz = camp.y + std::sin(a) * d;
            const float flockHeading = s.rng.Range(0.0f, 2.0f * PI);
            const int count = s.rng.Int(10, 18);
            for (int i = 0; i < count; ++i)
            {
                const float x = fx + s.rng.Range(-9.0f, 9.0f), z = fz + s.rng.Range(-9.0f, 9.0f);
                if (s.layout.RoadDistance(x, z) < 1.5f) continue;
                Sheep(s, x, z, flockHeading + s.rng.Range(-0.8f, 0.8f));
            }
            if (s.region == Region::Gobi || (s.region == Region::East && s.rng.Chance(0.3f)))
            {
                const int camels = s.rng.Int(2, 4);
                for (int i = 0; i < camels; ++i)
                {
                    Camel(s, camp.x + s.rng.Range(-30.0f, 30.0f), camp.y + s.rng.Range(14.0f, 30.0f) * (s.rng.Chance(0.5f) ? 1.0f : -1.0f),
                          s.rng.Range(0.0f, 2.0f * PI));
                }
            }
            if (s.region == Region::Forest || s.region == Region::West)
            {
                const int yaks = s.rng.Int(2, 4);
                for (int i = 0; i < yaks; ++i)
                {
                    Yak(s, camp.x + s.rng.Range(14.0f, 32.0f) * (s.rng.Chance(0.5f) ? 1.0f : -1.0f), camp.y + s.rng.Range(-30.0f, 30.0f),
                        s.rng.Range(0.0f, 2.0f * PI));
                }
            }
        }
    }

    bool InAnyMountain(const Scene& s, float x, float z)
    {
        for (const Mountain& m : s.layout.Mountains())
        {
            if (m.Profile(x, z) > 0.0f) return true;
        }
        return false;
    }

    void BuildTrees(Scene& s)
    {
        // Бүс нутгаас хамаарсан ургамал: хойд ойт аймагт шинэс, хусан ой; баруунд уулын шинэс;
        // төв, зүүн талд ганц нэг хайлаас, хус; говьд заг, хааяа хайлаас.
        int larches = 0, birches = 0, elms = 0, saxauls = 0;
        switch (s.region)
        {
        case Region::Forest: larches = 2200; birches = 700; elms = 40; break;
        case Region::West: larches = 260; birches = 60; elms = 40; break;
        case Region::City: larches = 120; birches = 140; elms = 110; break;
        case Region::East: birches = 50; elms = 120; break;
        case Region::Gobi: elms = 60; saxauls = 420; break;
        }
        Rng rng((uint32_t)(91 + s.variant * 7));
        const float extent = kPlayableHalfExtent * kUlaanbaatarMapScale;
        auto place = [&](int count, int kind) {
            for (int attempt = 0, placed = 0; attempt < count * 8 && placed < count; ++attempt)
            {
                const float x = rng.Range(-extent, extent), z = rng.Range(-extent, extent);
                if (s.layout.FlatDistance(x, z) < 14.0f) continue;
                if (s.layout.IsCity() && s.layout.CityBuiltDistance(x, z) < 130.0f) continue; // аймгийн төвийн гадна
                if (InAnyMountain(s, x, z)) continue; // уулын ойг BuildMountains тарина
                const Vector3 n = GroundNormalAt(s, x, z);
                const float h = s.terrain.HeightAt(x, z);
                if (kind <= 1)
                {
                    // Ой: хойд (+Z) энгэр, ой бүхий толбо (noise), хэт эгц биш
                    const float grove = Fbm(x * 0.004f + 13.0f, z * 0.004f + 7.0f, 3);
                    if (n.y < 0.75f || grove < (kind == 0 ? 0.42f : 0.40f)) continue;
                    if (n.z < -0.05f && rng.Chance(0.5f)) continue; // ой ихэвчлэн хойд энгэрт
                    if (h < 4.0f && rng.Chance(0.5f)) continue;
                    // Төгөл: ойр ойрхон
                    const int cluster = rng.Int(3, 7);
                    for (int k = 0; k < cluster && placed < count; ++k, ++placed)
                    {
                        const float cx = x + rng.Range(-12.0f, 12.0f), cz = z + rng.Range(-12.0f, 12.0f);
                        if (s.layout.FlatDistance(cx, cz) < 10.0f) continue;
                        if (kind == 0) Larch(s, cx, cz, rng.Range(7.0f, 13.0f));
                        else Birch(s, cx, cz, rng.Range(6.0f, 10.0f));
                    }
                }
                else if (kind == 2)
                {
                    if (n.y < 0.9f || h > 25.0f) continue; // хөндий, тал
                    Elm(s, x, z, rng.Range(5.0f, 8.0f));
                    ++placed;
                }
                else
                {
                    if (n.y < 0.85f) continue;
                    Saxaul(s, x, z);
                    ++placed;
                }
            }
        };
        place(larches, 0);
        place(birches, 1);
        place(elms, 2);
        place(saxauls, 3);
    }

    void BuildOvoos(Scene& s)
    {
        // Толгодын орой: ойр орчмоосоо өндөр, замаас хол цэгүүд
        const float extent = kPlayableHalfExtent * kUlaanbaatarMapScale - 60.0f;
        int placed = 0;
        for (float z = -extent; z <= extent && placed < 16; z += 130.0f)
        {
            for (float x = -extent; x <= extent && placed < 16; x += 130.0f)
            {
                // Нүд доторх хамгийн өндөр цэг
                float bestH = -1e9f, bx = x, bz = z;
                for (float dz = 0.0f; dz < 130.0f; dz += 13.0f)
                {
                    for (float dx = 0.0f; dx < 130.0f; dx += 13.0f)
                    {
                        const float h = s.terrain.HeightAt(x + dx, z + dz);
                        if (h > bestH) { bestH = h; bx = x + dx; bz = z + dz; }
                    }
                }
                if (bestH < 22.0f || s.layout.FlatDistance(bx, bz) < 25.0f) continue;
                bool peak = true;
                for (int k = 0; k < 8 && peak; ++k)
                {
                    const float a = (float)k * PI * 0.25f;
                    peak = s.terrain.HeightAt(bx + std::cos(a) * 40.0f, bz + std::sin(a) * 40.0f) < bestH - 1.5f;
                }
                if (!peak) continue;
                Ovoo(s, bx, bz);
                ++placed;
            }
        }
    }

    // Нуур: ус (говьд хатсан давсны мараа)
    void BuildLakes(Scene& s)
    {
        for (const Lake& lake : s.layout.lakes)
        {
            const bool dry = s.region == Region::Gobi;
            const int segments = 40;
            const float r = lake.radius * 0.9f;
            for (int i = 0; i < segments; ++i)
            {
                const float a0 = 2.0f * PI * (float)i / (float)segments, a1 = 2.0f * PI * (float)(i + 1) / (float)segments;
                const float w0 = 1.0f + 0.08f * std::sin(a0 * 3.0f + lake.center.x), w1 = 1.0f + 0.08f * std::sin(a1 * 3.0f + lake.center.x);
                const Vector3 c = { lake.center.x, kLakeLevel, lake.center.y };
                const Vector3 p0 = { lake.center.x + std::cos(a0) * r * w0, kLakeLevel, lake.center.y + std::sin(a0) * r * w0 };
                const Vector3 p1 = { lake.center.x + std::cos(a1) * r * w1, kLakeLevel, lake.center.y + std::sin(a1) * r * w1 };
                if (dry)
                {
                    s.solid.Triangle(c, p1, p0, { 214, 208, 192, 255 }); // давсны цагаан хучаас
                }
                else
                {
                    s.glass.Triangle(c, p1, p0, { 70, 112, 140, 190 });
                }
            }
            if (dry) continue;
            // Эргийн зэгс
            for (int i = 0; i < 70; ++i)
            {
                const float a = s.rng.Range(0.0f, 2.0f * PI);
                const float d = r * s.rng.Range(0.92f, 1.08f);
                const float x = lake.center.x + std::cos(a) * d, z = lake.center.y + std::sin(a) * d;
                const float ground = std::max(s.terrain.HeightAt(x, z), kLakeLevel - 0.2f);
                for (int k = 0; k < 5; ++k)
                {
                    const float ox = s.rng.Range(-0.4f, 0.4f), oz = s.rng.Range(-0.4f, 0.4f);
                    const float h = s.rng.Range(0.8f, 1.6f);
                    s.solid.Triangle({ x + ox - 0.03f, ground, z + oz }, { x + ox + 0.03f, ground, z + oz },
                                     { x + ox * 1.4f, ground + h, z + oz * 1.4f }, JitterColor({ 120, 132, 70, 255 }, s.rng, 0.1f));
                }
            }
        }
    }

    void BuildNature(Scene& s)
    {
        BuildLakes(s);
        BuildTrees(s);
        BuildOvoos(s);
        BuildLivestock(s);
    }

    // ------------------------------------------------------------------ Улаанбаатарын төв (OpenStreetMap)

    // Хотын барилгуудын эзэлсэн газар (2 м тор): мод, гэрэлтүүлгийн шон барилга дотор орохгүй.
    struct CityMask
    {
        static constexpr float kCell = 2.0f;
        int side = 0;
        std::vector<bool> cells;

        void Reset()
        {
            side = (int)std::ceil(kWorldHalfExtent * 2.0f / kCell);
            cells.assign((size_t)side * side, false);
        }
        bool At(float x, float z) const
        {
            const int ix = (int)((x + kWorldHalfExtent) / kCell), iz = (int)((z + kWorldHalfExtent) / kCell);
            return ix < 0 || iz < 0 || ix >= side || iz >= side || cells[(size_t)iz * side + ix];
        }
        // (x, z)-ээс radius зайд барилга байгаа эсэх (төв ба дөрвөн зүгийн цэгүүдээр)
        bool Blocked(float x, float z, float radius) const
        {
            return At(x, z) || At(x + radius, z) || At(x - radius, z) || At(x, z + radius) || At(x, z - radius);
        }
        void Fill(const std::vector<Vector2>& outline)
        {
            float minZ = 1e9f, maxZ = -1e9f;
            for (const Vector2& p : outline)
            {
                minZ = std::min(minZ, p.y);
                maxZ = std::max(maxZ, p.y);
            }
            std::vector<float> xs;
            const int z0 = std::max(0, (int)((minZ + kWorldHalfExtent) / kCell));
            const int z1 = std::min(side - 1, (int)((maxZ + kWorldHalfExtent) / kCell));
            for (int iz = z0; iz <= z1; ++iz)
            {
                // Нүдний доод, төв, дээд ирмэгээр: нарийн барилга ч нүдээ эзэлнэ
                for (float sub : { 0.05f, 0.5f, 0.95f })
                {
                    const float z = -kWorldHalfExtent + ((float)iz + sub) * kCell;
                    xs.clear();
                    for (size_t i = 0, j = outline.size() - 1; i < outline.size(); j = i++)
                    {
                        const Vector2 a = outline[i], b = outline[j];
                        if ((a.y > z) != (b.y > z)) xs.push_back(a.x + (z - a.y) * (b.x - a.x) / (b.y - a.y));
                    }
                    std::sort(xs.begin(), xs.end());
                    for (size_t k = 0; k + 1 < xs.size(); k += 2)
                    {
                        const int x0 = std::max(0, (int)((xs[k] + kWorldHalfExtent) / kCell));
                        const int x1 = std::min(side - 1, (int)((xs[k + 1] + kWorldHalfExtent) / kCell));
                        for (int ix = x0; ix <= x1; ++ix) cells[(size_t)iz * side + ix] = true;
                    }
                }
            }
        }
    };
    CityMask gCityMask;
    std::vector<Vector3> gCityVehicleSpots; // унаж болох машины байрлал (x, z, y = чиглэл)

    // Хотын гудамжинд орхигдсон машин: энгийн хэлбэр (нэг машинд ~70 гурвалжин), газрын налууг дагана.
    // heading: урд тал (sin, cos) зүг рүү.
    void CityParkedCar(Scene& s, float x, float z, float heading)
    {
        static const Color paints[] = { { 186, 182, 170, 255 }, { 40, 42, 46, 255 },   { 196, 198, 200, 255 }, { 138, 62, 50, 255 },
                                        { 58, 70, 96, 255 },    { 110, 122, 134, 255 }, { 150, 120, 60, 255 },  { 214, 210, 196, 255 } };
        const float ground = s.terrain.HeightAt(x, z);
        const bool suv = s.rng.Chance(0.35f); // жийп (Land Cruiser, X-Trail) эсвэл суудлын (Prius)
        const bool burnt = s.rng.Chance(0.12f);
        Color body = burnt ? Color{ 46, 40, 36, 255 } : JitterColor(paints[s.rng.Int(0, 7)], s.rng, 0.06f);
        const Color glass = burnt ? Color{ 24, 22, 20, 255 } : kGlassDark;
        const float length = suv ? 4.6f : 4.4f, width = suv ? 1.85f : 1.75f, sill = 0.32f, belt = suv ? 1.05f : 0.9f;
        const float roof = suv ? 1.8f : 1.45f;
        const float tilt = s.rng.Chance(0.15f) ? s.rng.Range(-0.08f, 0.08f) : 0.0f; // хий гарсан дугуй
        MeshBuilder& m = s.solid;
        m.SetTransform(MatrixMultiply(MatrixMultiply(MatrixRotateZ(tilt), MatrixRotateY(heading)), MatrixTranslate(x, ground, z)));
        m.Box({ 0.0f, (sill + belt) * 0.5f, 0.0f }, { width, belt - sill, length }, body);                   // их бие
        // Бүхээг: доороо өргөн, дээрээ нарийн (шил), дээвэр нь биеийн өнгөтэй
        const float c0 = suv ? -1.9f : -1.4f, c1 = suv ? 1.0f : 0.9f, t0 = suv ? -1.8f : -0.95f, t1 = suv ? 0.55f : 0.25f;
        const float hw = width * 0.5f - 0.05f, tw = width * 0.5f - 0.18f;
        const Vector3 b00 = { -hw, belt, c0 }, b01 = { hw, belt, c0 }, b10 = { -hw, belt, c1 }, b11 = { hw, belt, c1 };
        const Vector3 r00 = { -tw, roof, t0 }, r01 = { tw, roof, t0 }, r10 = { -tw, roof, t1 }, r11 = { tw, roof, t1 };
        m.Quad(b10, b11, r11, r10, glass); // салхины шил
        m.Quad(b01, b00, r00, r01, glass); // хойд шил
        m.Quad(b00, b10, r10, r00, glass); // хажуу
        m.Quad(b11, b01, r01, r11, glass);
        m.Quad(r00, r10, r11, r01, burnt ? body : ScaleColor(body, 1.05f)); // дээвэр
        // Дугуй: тэнхлэг бүрд нэг хар хайрцаг (хоёр талаасаа бага зэрэг цухуйна)
        for (float axle : { -length * 0.32f, length * 0.32f })
        {
            m.Box({ 0.0f, 0.33f, axle }, { width + 0.06f, 0.62f, 0.66f }, kRubber);
        }
        // Гэрэл
        for (float side : { -0.6f, 0.6f })
        {
            m.Box({ side * width * 0.5f, belt - 0.15f, length * 0.5f + 0.01f }, { 0.32f, 0.12f, 0.04f }, burnt ? glass : kWindowFrame);
            m.Box({ side * width * 0.5f, belt - 0.15f, -length * 0.5f - 0.01f }, { 0.3f, 0.12f, 0.04f }, burnt ? glass : Color{ 150, 30, 26, 255 });
        }
        m.ResetTransform();

        // Мөргөлдөөн: эргэлдсэн биеийг хоёр хайрцгаар (урд, хойд хагас)
        const float fx = std::sin(heading), fz = std::cos(heading);
        for (float along : { -length * 0.25f, length * 0.25f })
        {
            const float cx = x + fx * along, cz = z + fz * along;
            const float ex = std::fabs(fx) * length * 0.25f + std::fabs(fz) * width * 0.5f;
            const float ez = std::fabs(fz) * length * 0.25f + std::fabs(fx) * width * 0.5f;
            s.Collider({ cx, ground + roof * 0.5f, cz }, { ex * 2.0f * 0.9f, roof, ez * 2.0f * 0.9f });
        }
    }

    // Даалгаврын газрууд: OSM-ийн барилгыг хасаж, тоглоомын өөрийн байгууламжийг барина.
    struct CityClearing
    {
        float x0, z0, x1, z1;
    };
    std::vector<CityClearing> CityClearings(const CityData& city)
    {
        // Застав (хуаран, дэлгүүр) ба тоглогчийн эхлэх цэг, эхний унаанууд (Улаанбаатарт Сүхбаатарын талбай)
        std::vector<CityClearing> clearings = { { -74.0f, -41.0f, 41.0f, 41.0f } };
        for (const CityPoi& poi : city.pois)
        {
            const Vector2 o = poi.position;
            if (poi.kind == CityPoiKind::Station) clearings.push_back({ 18.0f + o.x, -20.0f + o.y, 52.0f + o.x, -3.0f + o.y }); // ШТС
            if (poi.kind == CityPoiKind::Warehouse) clearings.push_back({ o.x - 12.0f, o.y - 10.0f, o.x + 22.0f, o.y + 10.0f }); // агуулах
        }
        return clearings;
    }

    bool InCityClearing(const std::vector<CityClearing>& clearings, const std::vector<Vector2>& outline)
    {
        float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
        for (const Vector2& p : outline)
        {
            minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
            minZ = std::min(minZ, p.y); maxZ = std::max(maxZ, p.y);
        }
        for (const CityClearing& c : clearings)
        {
            if (maxX > c.x0 && minX < c.x1 && maxZ > c.z0 && minZ < c.z1) return true;
        }
        return false;
    }

    // Гудамж: ангиасаа хамаарсан өргөн, өнгө; том замд явган хүний зам, голын зураас.
    // Ангиуд давхцах уулзварт анивчихгүйн тулд том зам бага зэрэг өндөр.
    void CityStreet(Scene& s, const CityRoad& road)
    {
        const int klass = (int)road.klass;
        const bool dirt = road.dirt || road.klass == CityRoadClass::Track;
        const bool pedestrian = road.klass == CityRoadClass::Pedestrian;
        const bool major = klass <= (int)CityRoadClass::Tertiary && !dirt;
        const bool sidewalks = klass <= (int)CityRoadClass::Secondary && !dirt;
        const std::vector<Vector2> path = Resample(road.points, major ? 8.0f : 11.0f);
        const int count = (int)path.size();
        if (count < 2) return;

        std::vector<Vector2> side(count);
        for (int i = 0; i < count; ++i)
        {
            const Vector2 tangent = Vector2Normalize(Vector2Subtract(path[std::min(i + 1, count - 1)], path[std::max(i - 1, 0)]));
            side[i] = { -tangent.y, tangent.x };
        }
        auto point = [&](int i, float offset, float lift) {
            const float x = path[i].x + side[i].x * offset, z = path[i].y + side[i].y * offset;
            return Vector3{ x, s.terrain.HeightAt(x, z) + lift, z };
        };
        auto strip = [&](int i, float from, float to, float lift, Color color) {
            s.solid.Quad(point(i, from, lift), point(i + 1, from, lift), point(i + 1, to, lift), point(i, to, lift), color);
        };

        const float hw = road.halfWidth;
        const float lift = (dirt ? 0.05f : pedestrian ? 0.052f : 0.078f - 0.004f * (float)klass);
        const Color surface = JitterColor(dirt ? kDirtRoad : pedestrian ? kPaving : kAsphalt, s.rng, 0.04f);
        const Color sidewalk = JitterColor({ 138, 136, 128, 255 }, s.rng, 0.03f);
        for (int i = 0; i + 1 < count; ++i)
        {
            strip(i, -hw, hw, lift, surface);
            if (sidewalks)
            {
                strip(i, -hw - 2.4f, -hw, 0.045f, sidewalk);
                strip(i, hw, hw + 2.4f, 0.045f, sidewalk);
            }
            if (major)
            {
                if (i % 2 == 0) strip(i, -0.1f, 0.1f, lift + 0.012f, kRoadPaint); // тасархай голын зураас
                if (klass <= (int)CityRoadClass::Primary && hw > 6.0f)
                {
                    // Олон эгнээ: эгнээний тасархай зураас
                    for (float lane : { -hw * 0.5f, hw * 0.5f })
                    {
                        if (i % 3 == 1) strip(i, lane - 0.07f, lane + 0.07f, lift + 0.012f, ScaleColor(kRoadPaint, 0.9f));
                    }
                }
            }
            if (!dirt && !pedestrian && s.rng.Chance(0.06f))
            {
                // Нүх: асфальт хуулраад шороо гарсан
                const float offset = s.rng.Range(-hw + 0.8f, hw - 0.8f);
                const float radius = s.rng.Range(0.35f, 0.8f);
                const Vector3 c = point(i, offset, lift + 0.01f);
                s.solid.Quad({ c.x - radius, c.y, c.z - radius * 0.6f }, { c.x - radius * 0.5f, c.y, c.z + radius },
                             { c.x + radius, c.y, c.z + radius * 0.7f }, { c.x + radius * 0.6f, c.y, c.z - radius },
                             JitterColor(kPothole, s.rng, 0.1f));
            }
        }
    }

    // Олон өнцөгт суурьтай барилгын мөргөлдөөн: Z дагуу ижил зузаантай хэвтээ зурвасууд; хоорондоо бараг
    // ижил X мужтай дараалсан зурвасуудыг нэгтгэнэ (тэнхлэгт зэрэгцээ тэгш өнцөгт барилга нэг хайрцаг болно).
    void CityColliders(Scene& s, const std::vector<Vector2>& outline, float base, float height)
    {
        float z0 = 1e9f, z1 = -1e9f;
        for (const Vector2& p : outline)
        {
            z0 = std::min(z0, p.y);
            z1 = std::max(z1, p.y);
        }
        const int rows = std::max(1, (int)std::ceil((z1 - z0) / 2.0f));
        const float row = (z1 - z0) / (float)rows;
        bool open = false;
        float px0 = 0.0f, px1 = 0.0f, pz0 = 0.0f, pz1 = 0.0f;
        auto flush = [&]() {
            if (open) s.Collider({ (px0 + px1) * 0.5f, base + height * 0.5f, (pz0 + pz1) * 0.5f }, { px1 - px0, height, pz1 - pz0 });
            open = false;
        };
        std::vector<float> xs;
        for (int r = 0; r < rows; ++r)
        {
            const float zc = z0 + ((float)r + 0.5f) * row;
            xs.clear();
            for (size_t i = 0, j = outline.size() - 1; i < outline.size(); j = i++)
            {
                const Vector2 a = outline[i], b = outline[j];
                if ((a.y > zc) != (b.y > zc)) xs.push_back(a.x + (zc - a.y) * (b.x - a.x) / (b.y - a.y));
            }
            std::sort(xs.begin(), xs.end());
            const float rz0 = zc - row * 0.5f, rz1 = zc + row * 0.5f;
            if (xs.size() == 2)
            {
                if (open && std::fabs(xs[0] - px0) < 0.35f && std::fabs(xs[1] - px1) < 0.35f)
                {
                    px0 = std::min(px0, xs[0]);
                    px1 = std::max(px1, xs[1]);
                    pz1 = rz1;
                    continue;
                }
                flush();
                open = true;
                px0 = xs[0]; px1 = xs[1]; pz0 = rz0; pz1 = rz1;
                continue;
            }
            flush();
            for (size_t k = 0; k + 1 < xs.size(); k += 2)
            {
                if (xs[k + 1] - xs[k] < 0.3f) continue;
                s.Collider({ (xs[k] + xs[k + 1]) * 0.5f, base + height * 0.5f, zc }, { xs[k + 1] - xs[k], height, row });
            }
        }
        flush();
    }

    // ------------------------------------------------------------------ Аймгийн төвийн төрх

    // Аймгийн төвийн өнгө, материал. Бүс нутгаас хамаарна; аймаг бүр бүс нутгийнхаа өнгөнөөс өөрийн хослолыг
    // сонгоно (хот бүхэлдээ нэгдмэл, хөрш аймгууд ч ялгаатай харагдана).
    struct TownStyle
    {
        Region region = Region::West;
        Color walls[3];       // хувийн байшингийн хана (шохойдсон, будсан)
        Color publicWalls[2]; // орон сууц, олон нийтийн барилга (Зөвлөлтийн үеийн пастел)
        Color roofs[2];       // тугалган дээвэр
        Color fenceWood[2];
        Color fenceMetal[2];
        Color wallFence;      // шавар, тоосгон хашаа
        float metalFences = 0.3f; // төмөр хашааны хувь
        float treeChance = 0.6f;  // гудамжны модны нягт
        float logHouses = 0.0f;   // дүнзэн байшингийн хувь (ойт бүс)
        int province = -1;
        Vector2 site = { 0.0f, 0.0f }; // хотын төвийн талбай (TownSite)
        const CityBuilding* mainHall = nullptr; // Засаг даргын тамгын газар (талбайн дэргэдэх)
    };
    TownStyle gTown;

    TownStyle MakeTownStyle(int province)
    {
        struct Palette
        {
            std::vector<Color> walls, publicWalls, roofs, fenceWood, fenceMetal;
            Color wallFence;
            float metalFences, treeChance, logHouses;
        };
        Palette p;
        const Region region = province >= 0 ? RegionFor(province) : Region::City;
        switch (region)
        {
        case Region::West: // шохойдсон цагаан, цайвар хөх, ногоон; хөх, ногоон, улаан тугалга
            p = { { { 236, 232, 222, 255 }, { 210, 224, 232, 255 }, { 226, 214, 186, 255 }, { 200, 222, 212, 255 }, { 188, 206, 226, 255 } },
                  { { 226, 214, 170, 255 }, { 206, 218, 226, 255 }, { 232, 202, 182, 255 } },
                  { { 46, 96, 170, 255 }, { 52, 128, 96, 255 }, { 170, 52, 44, 255 }, { 60, 140, 150, 255 } },
                  { { 120, 104, 84, 255 }, { 132, 126, 114, 255 } },
                  { { 52, 104, 150, 255 }, { 60, 120, 80, 255 }, { 40, 120, 160, 255 } },
                  { 196, 186, 168, 255 }, 0.35f, 0.5f, 0.0f };
            break;
        case Region::Gobi: // шавар, элсэн шар; хүрэн, ногоон дээвэр, мод ховор
            p = { { { 212, 190, 150, 255 }, { 226, 206, 170, 255 }, { 232, 224, 206, 255 }, { 214, 178, 152, 255 }, { 200, 184, 150, 255 } },
                  { { 226, 206, 160, 255 }, { 232, 196, 170, 255 }, { 214, 214, 200, 255 } },
                  { { 120, 96, 72, 255 }, { 80, 120, 96, 255 }, { 150, 60, 48, 255 }, { 110, 112, 116, 255 } },
                  { { 150, 136, 112, 255 }, { 128, 118, 100, 255 } },
                  { { 150, 90, 60, 255 }, { 90, 110, 130, 255 } },
                  { 186, 160, 124, 255 }, 0.25f, 0.25f, 0.0f };
            break;
        case Region::Forest: // дүнзэн байшин, ногоон, улаан дээвэр, шигүү мод
            p = { { { 214, 206, 180, 255 }, { 196, 210, 186, 255 }, { 226, 220, 206, 255 }, { 206, 196, 170, 255 } },
                  { { 226, 210, 160, 255 }, { 206, 222, 196, 255 }, { 232, 206, 186, 255 } },
                  { { 40, 110, 70, 255 }, { 150, 48, 40, 255 }, { 70, 80, 92, 255 }, { 46, 90, 150, 255 } },
                  { { 104, 80, 58, 255 }, { 120, 100, 76, 255 } },
                  { { 50, 100, 70, 255 }, { 60, 90, 140, 255 } },
                  { 150, 144, 132, 255 }, 0.2f, 0.9f, 0.55f };
            break;
        case Region::City: // панель, саарал шаргал; төмөр хашаа
            p = { { { 214, 210, 200, 255 }, { 200, 196, 186, 255 }, { 226, 214, 186, 255 }, { 206, 214, 220, 255 } },
                  { { 214, 206, 186, 255 }, { 200, 200, 196, 255 }, { 226, 210, 170, 255 } },
                  { { 100, 104, 110, 255 }, { 70, 110, 84, 255 }, { 150, 60, 48, 255 } },
                  { { 124, 110, 90, 255 }, { 132, 126, 114, 255 } },
                  { { 80, 110, 140, 255 }, { 70, 110, 80, 255 } },
                  { 160, 150, 136, 255 }, 0.5f, 0.8f, 0.0f };
            break;
        default: // зүүн тал: цайвар шар, ягаан; улаан, ногоон дээвэр
            p = { { { 232, 226, 210, 255 }, { 226, 214, 178, 255 }, { 232, 206, 196, 255 }, { 206, 220, 230, 255 } },
                  { { 230, 214, 170, 255 }, { 236, 206, 186, 255 }, { 214, 222, 230, 255 } },
                  { { 160, 56, 46, 255 }, { 52, 120, 90, 255 }, { 50, 100, 160, 255 }, { 120, 120, 124, 255 } },
                  { { 130, 110, 86, 255 }, { 120, 116, 106, 255 } },
                  { { 60, 110, 150, 255 }, { 150, 70, 50, 255 } },
                  { 180, 166, 146, 255 }, 0.3f, 0.45f, 0.0f };
            break;
        }

        // Аймаг бүрт тогтмол: бүс нутгийн өнгөнөөс өөрийн хослол
        Rng rng((uint32_t)(province * 7919 + 1301));
        auto pick = [&](std::vector<Color>& from) {
            const int index = rng.Int(0, (int)from.size() - 1);
            const Color c = from[(size_t)index];
            if (from.size() > 1) from.erase(from.begin() + index);
            return c;
        };
        TownStyle t;
        t.region = region;
        t.province = province;
        for (Color& c : t.walls) c = pick(p.walls);
        for (Color& c : t.publicWalls) c = pick(p.publicWalls);
        for (Color& c : t.roofs) c = pick(p.roofs);
        for (int i = 0; i < 2; ++i) t.fenceWood[i] = p.fenceWood[(size_t)i];
        for (Color& c : t.fenceMetal) c = pick(p.fenceMetal);
        t.wallFence = p.wallFence;
        t.metalFences = p.metalFences;
        t.treeChance = p.treeChance;
        t.logHouses = p.logHouses;
        return t;
    }

    // Суурийн чиглэлтэй хүрээ (хамгийн бага талбайтай тэгш өнцөгт): дээвэр, багана, хаалгыг үүгээр байрлуулна.
    struct Plan
    {
        Vector2 center = { 0.0f, 0.0f };
        Vector2 axis = { 1.0f, 0.0f }; // урт талын чиглэл
        float halfLong = 0.0f, halfShort = 0.0f;
        float area = 0.0f;              // олон өнцөгтийн жинхэнэ талбай
        Vector2 Side() const { return { -axis.y, axis.x }; }
        Vector2 At(float along, float across) const
        {
            return { center.x + axis.x * along - axis.y * across, center.y + axis.y * along + axis.x * across };
        }
    };

    Plan MeasureFootprint(const std::vector<Vector2>& p)
    {
        Plan f;
        const size_t n = p.size();
        for (size_t i = 0, j = n - 1; i < n; j = i++) f.area += p[j].x * p[i].y - p[i].x * p[j].y;
        f.area = std::fabs(f.area) * 0.5f;
        float best = 1e18f;
        for (size_t i = 0; i < n; ++i)
        {
            const Vector2 edge = Vector2Subtract(p[(i + 1) % n], p[i]);
            const float length = Vector2Length(edge);
            if (length < 0.5f) continue;
            const Vector2 u = Vector2Scale(edge, 1.0f / length), v = { -u.y, u.x };
            float u0 = 1e9f, u1 = -1e9f, v0 = 1e9f, v1 = -1e9f;
            for (const Vector2& q : p)
            {
                const float a = Vector2DotProduct(q, u), b = Vector2DotProduct(q, v);
                u0 = std::min(u0, a); u1 = std::max(u1, a);
                v0 = std::min(v0, b); v1 = std::max(v1, b);
            }
            const float area = (u1 - u0) * (v1 - v0);
            if (area >= best) continue;
            best = area;
            const float cu = (u0 + u1) * 0.5f, cv = (v0 + v1) * 0.5f;
            f.center = Vector2Add(Vector2Scale(u, cu), Vector2Scale(v, cv));
            if (u1 - u0 >= v1 - v0)
            {
                f.axis = u;
                f.halfLong = (u1 - u0) * 0.5f;
                f.halfShort = (v1 - v0) * 0.5f;
            }
            else
            {
                f.axis = v;
                f.halfLong = (v1 - v0) * 0.5f;
                f.halfShort = (u1 - u0) * 0.5f;
            }
        }
        return f;
    }

    // Гэр хороолол (хашаатай хороолол) эсэх: 40 м нүдэнд хашааны урт, гэр, байшингийн тоо.
    struct TownZones
    {
        static constexpr float kCell = 40.0f;
        int side = 0;
        std::vector<float> fence;
        std::vector<uint16_t> homes;

        void Reset()
        {
            side = (int)std::ceil(kWorldHalfExtent * 2.0f / kCell);
            fence.assign((size_t)side * side, 0.0f);
            homes.assign((size_t)side * side, 0);
        }
        int Index(float x, float z) const
        {
            const int ix = std::clamp((int)((x + kWorldHalfExtent) / kCell), 0, side - 1);
            const int iz = std::clamp((int)((z + kWorldHalfExtent) / kCell), 0, side - 1);
            return iz * side + ix;
        }
        // Хашаатай гэр хороолол: эргэн тойрны 3x3 нүдэнд хашаа олон
        bool Khashaa(float x, float z) const
        {
            if (side == 0) return false;
            float length = 0.0f;
            int count = 0;
            for (int dz = -1; dz <= 1; ++dz)
            {
                for (int dx = -1; dx <= 1; ++dx)
                {
                    const int i = Index(x + (float)dx * kCell, z + (float)dz * kCell);
                    length += fence[(size_t)i];
                    count += homes[(size_t)i];
                }
            }
            return length > 260.0f || count >= 10;
        }
    };
    TownZones gTownZones;

    // Хашааны шугамууд (4 м тор): шинээр хашаа үүсгэхэд байгаа хашаатай давхцуулахгүй.
    struct FenceMask
    {
        static constexpr float kCell = 4.0f;
        int side = 0;
        std::vector<bool> cells;

        void Reset()
        {
            side = (int)std::ceil(kWorldHalfExtent * 2.0f / kCell);
            cells.assign((size_t)side * side, false);
        }
        bool At(float x, float z) const
        {
            const int ix = (int)((x + kWorldHalfExtent) / kCell), iz = (int)((z + kWorldHalfExtent) / kCell);
            return ix >= 0 && iz >= 0 && ix < side && iz < side && cells[(size_t)iz * side + ix];
        }
        bool Near(float x, float z, float radius) const
        {
            for (float dz = -radius; dz <= radius; dz += kCell)
            {
                for (float dx = -radius; dx <= radius; dx += kCell)
                {
                    if (At(x + dx, z + dz)) return true;
                }
            }
            return false;
        }
        void Mark(Vector2 a, Vector2 b)
        {
            const int steps = std::max(1, (int)(Vector2Distance(a, b) / (kCell * 0.5f)));
            for (int i = 0; i <= steps; ++i)
            {
                const Vector2 q = Vector2Lerp(a, b, (float)i / (float)steps);
                const int ix = (int)((q.x + kWorldHalfExtent) / kCell), iz = (int)((q.y + kWorldHalfExtent) / kCell);
                if (ix >= 0 && iz >= 0 && ix < side && iz < side) cells[(size_t)iz * side + ix] = true;
            }
        }
    };
    FenceMask gFenceMask;

    // Хананы a -> c ирмэгийн гадна талд (t0..t1, y0..y1) тэгш өнцөгт
    void WallFace(Scene& s, Vector2 a, Vector2 c, float t0, float t1, float y0, float y1, float out, Color color)
    {
        const float length = std::max(Vector2Distance(a, c), 0.001f);
        const Vector2 normal = { (c.y - a.y) / length, -(c.x - a.x) / length };
        auto q = [&](float t, float y) { return Vector3{ a.x + (c.x - a.x) * t + normal.x * out, y, a.y + (c.y - a.y) * t + normal.y * out }; };
        s.solid.Quad(q(t1, y0), q(t0, y0), q(t0, y1), q(t1, y1), color);
    }

    // Нэг давхрын цонхны эгнээ: шил (framed бол цайвар жаазтай), хооронд нь ижил зай. Олон давхар барилгад
    // жаазгүй: хот бүрт хэдэн зуун мянган цонх гардаг тул утсанд хүнд.
    void WindowRow(Scene& s, Vector2 a, Vector2 c, float y, float width, float height, float spacing, Color frame, Color glass, bool framed = true)
    {
        const float length = Vector2Distance(a, c);
        const int count = (int)((length - 0.8f) / spacing);
        if (count < 1) return;
        const float start = (length - (float)count * spacing) * 0.5f + spacing * 0.5f;
        const float half = width * 0.5f, rim = 0.09f;
        for (int k = 0; k < count; ++k)
        {
            const float mid = start + spacing * (float)k;
            if (framed) WallFace(s, a, c, (mid - half - rim) / length, (mid + half + rim) / length, y - rim, y + height + rim, 0.04f, frame);
            WallFace(s, a, c, (mid - half) / length, (mid + half) / length, y, y + height, 0.07f, glass);
        }
    }

    // Хоёр налуу дээвэр (хүрээний урт тэнхлэгийн дагуу нуруутай), хоёр үзүүрт хананы өнгөтэй гурвалжин
    void TownGableRoof(Scene& s, const Plan& f, float eave, float rise, float overhang, Color roof, Color gable)
    {
        const float A = f.halfLong + overhang, B = f.halfShort + overhang;
        auto p3 = [&](float along, float across, float y) { const Vector2 q = f.At(along, across); return Vector3{ q.x, y, q.y }; };
        const float drop = rise * overhang / std::max(f.halfShort, 0.1f); // хаяа нь хананаас доош унжина
        const Vector3 e1 = p3(-A, -B, eave - drop), e2 = p3(A, -B, eave - drop), e3 = p3(A, B, eave - drop), e4 = p3(-A, B, eave - drop);
        const Vector3 r1 = p3(-A, 0.0f, eave + rise), r2 = p3(A, 0.0f, eave + rise);
        s.solid.Quad(e1, e2, r2, r1, roof);
        s.solid.Quad(e3, e4, r1, r2, ScaleColor(roof, 0.9f));
        const float a0 = f.halfLong, b0 = f.halfShort;
        s.solid.Triangle(p3(-a0, -b0, eave), p3(-a0, b0, eave), p3(-a0, 0.0f, eave + rise), gable);
        s.solid.Triangle(p3(a0, b0, eave), p3(a0, -b0, eave), p3(a0, 0.0f, eave + rise), gable);
    }

    // Дөрвөн налуу дээвэр (сүм хийд, ордон): нуруу нь богино, хаяа нь гадагш
    void TownHipRoof(Scene& s, const Plan& f, float eave, float rise, float overhang, Color roof)
    {
        const float A = f.halfLong + overhang, B = f.halfShort + overhang;
        auto p3 = [&](float along, float across, float y) { const Vector2 q = f.At(along, across); return Vector3{ q.x, y, q.y }; };
        const float ridge = std::max(0.0f, A - B);
        const Vector3 e1 = p3(-A, -B, eave), e2 = p3(A, -B, eave), e3 = p3(A, B, eave), e4 = p3(-A, B, eave);
        const Vector3 r1 = p3(-ridge, 0.0f, eave + rise), r2 = p3(ridge, 0.0f, eave + rise);
        s.solid.Quad(e1, e2, r2, r1, roof);
        s.solid.Quad(e3, e4, r1, r2, ScaleColor(roof, 0.9f));
        s.solid.Triangle(e2, e3, r2, ScaleColor(roof, 0.95f));
        s.solid.Triangle(e4, e1, r1, ScaleColor(roof, 0.95f));
    }

    // Олон өнцөгт суурийн хавтгай дээвэр
    void FlatRoof(Scene& s, const std::vector<Vector2>& p, float y, Color roof)
    {
        const std::vector<Vector2> triangles = Triangulate(p);
        for (size_t i = 0; i + 2 < triangles.size(); i += 3)
        {
            Vector2 a = triangles[i], b = triangles[i + 1], c = triangles[i + 2];
            if ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x) > 0.0f) std::swap(b, c);
            s.solid.Triangle({ a.x, y, a.y }, { b.x, y, b.y }, { c.x, y, c.y }, roof);
        }
    }

    // Аймгийн төвийн барилгын хэлбэр (OSM-ийн төрөл, суурийн хэмжээ, хорооллоос)
    enum class TownKind
    {
        Shed,       // жорлон, саравч, амбаар
        House,      // хашаан доторх байшин
        Garage,
        Shop,       // 1-2 давхар дэлгүүр (доод давхар шилэн, хаяг)
        Office,     // 2-3 давхар байгууллага
        Apartments, // Зөвлөлтийн үеийн орон сууц
        School,
        Government, // Засаг даргын тамгын газар
        Warehouse,  // агуулах, үйлдвэр
        Temple      // сүм хийд (Өлгийд сүм)
    };

    TownKind ClassifyTownBuilding(const CityBuilding& b, const Plan& f, Vector2 center)
    {
        const float area = f.area;
        const bool khashaa = gTownZones.Khashaa(center.x, center.y);
        const float fromCentre = Vector2Distance(center, gTown.site);
        switch (b.type)
        {
        case CityBuildingType::Religious: return TownKind::Temple;
        case CityBuildingType::Government: return TownKind::Government;
        case CityBuildingType::School: return TownKind::School;
        case CityBuildingType::Industrial: return TownKind::Warehouse;
        case CityBuildingType::Garage: return area < 14.0f ? TownKind::Shed : TownKind::Garage;
        case CityBuildingType::Commercial: return area < 450.0f ? TownKind::Shop : TownKind::Office;
        case CityBuildingType::Apartments: return TownKind::Apartments;
        case CityBuildingType::House: return area < 14.0f ? TownKind::Shed : TownKind::House;
        default: break;
        }
        // building=yes: хэмжээ, хорооллоос
        if (area < 14.0f) return TownKind::Shed;
        if (khashaa) return area < 230.0f ? TownKind::House : TownKind::Warehouse;
        if (area < 90.0f) return TownKind::House;
        if (area < 320.0f) return TownKind::Shop;
        if (area < 1300.0f) return fromCentre < 750.0f ? TownKind::Office : TownKind::Warehouse;
        return TownKind::Warehouse;
    }

    // Аймгийн төвийн барилга: хэлбэрээс хамаарсан өндөр, хана, цонх, дээвэр.
    // Налуу газарт суурь нь хамгийн нам буланд (бусад нь газарт шигдэнэ).
    // Аймгийн төвийн нэг давхар байшин, дэлгүүр: дотогш орж болно. Налуу тэгш өнцөгт (OSM-ийн хэлбэр) тул хана бүр
    // эргэлттэй хайрцаг, мөргөлдөөн нь налуу хана (WallSegment: хүн, зомбийг хаана, сум, камерыг хаана). Цонх нь
    // жинхэнэ нүх (шилгүй): нэвт харагдана, нэвт буудна, F-ээр авирч орж гарна. Хаалга нь замын талын урт хананд.
    void EnterableTownBuilding(Scene& s, const Plan& f, bool shop, float low, float high, float storey, Color wall, Color roof,
                               Color frame, bool pitched)
    {
        const float thick = 0.2f, floorY = high + 0.08f, ceiling = floorY + storey, base = low - 0.3f;
        const float doorWidth = 1.3f, doorHeight = 2.3f;
        const float windowWidth = shop ? 1.6f : 1.1f, sill = shop ? 0.7f : 0.95f, head = shop ? 2.3f : 2.15f;
        const float L = f.halfLong, S = f.halfShort;
        const Vector2 u = f.axis, v = f.Side();
        // Булангууд ба хана (a -> b, гадагш харсан чиглэл)
        const Vector2 c0 = f.At(-L, -S), c1 = f.At(L, -S), c2 = f.At(L, S), c3 = f.At(-L, S);
        struct Edge
        {
            Vector2 a, b, out;
            bool longSide;
        };
        const Edge edges[4] = { { c0, c1, Vector2Negate(v), true }, { c1, c2, u, false }, { c2, c3, v, true }, { c3, c0, Vector2Negate(u), false } };
        // Хаалга: зам руу ойр урт хана
        const Vector2 m0 = Vector2Lerp(c0, c1, 0.5f), m2 = Vector2Lerp(c2, c3, 0.5f);
        const int doorEdge = s.layout.RoadDistance(m0.x, m0.y) <= s.layout.RoadDistance(m2.x, m2.y) ? 0 : 2;

        auto box = [&](Vector2 at, float yaw, float y0, float y1, float across, float along, Color color) {
            s.solid.SetTransform(MatrixMultiply(MatrixRotateY(yaw), MatrixTranslate(at.x, (y0 + y1) * 0.5f, at.y)));
            s.solid.Box({ 0.0f, 0.0f, 0.0f }, { across, y1 - y0, along }, color);
            s.solid.ResetTransform();
        };

        for (int e = 0; e < 4; ++e)
        {
            const Edge& edge = edges[e];
            const float length = Vector2Distance(edge.a, edge.b);
            const Vector2 dir = Vector2Scale(Vector2Subtract(edge.b, edge.a), 1.0f / length);
            const float yaw = std::atan2(dir.x, dir.y);
            const Vector2 inset = Vector2Scale(edge.out, -thick * 0.5f); // ханын төв шугам
            auto at = [&](float t) { return Vector2Add(Vector2Add(edge.a, Vector2Scale(dir, t)), inset); };

            // Нээлхийнүүд (хана дагуу метрээр)
            struct Opening
            {
                float t0, t1;
                bool door;
            };
            std::vector<Opening> openings;
            float doorCenter = -100.0f;
            if (e == doorEdge)
            {
                doorCenter = length * 0.3f;
                openings.push_back({ doorCenter - doorWidth * 0.5f, doorCenter + doorWidth * 0.5f, true });
            }
            const int windows = edge.longSide ? std::clamp((int)((length - 1.0f) / 3.2f), 1, 3) : (length >= 3.5f ? 1 : 0);
            for (int k = 0; k < windows; ++k)
            {
                const float c = length * ((float)k + 0.5f) / (float)windows;
                if (std::fabs(c - doorCenter) < (doorWidth + windowWidth) * 0.5f + 0.4f) continue;
                if (c - windowWidth * 0.5f < 0.5f || c + windowWidth * 0.5f > length - 0.5f) continue;
                openings.push_back({ c - windowWidth * 0.5f, c + windowWidth * 0.5f, false });
            }
            std::sort(openings.begin(), openings.end(), [](const Opening& x, const Opening& y) { return x.t0 < y.t0; });

            const Color inner = ScaleColor(wall, 0.6f); // дотор тал бараан: гаднаас цонхоор харахад нүх тод
            // Ханын хэсэг: гадна, дотор хоёр хавтгай (барилгыг culling-гүй зурдаг), нээлхийн талд л таг (тавцан, тотго,
            // хажуу): хотод зуу зуун байшин тул хайрцгаас хоёр дахин цөөн гурвалжин.
            auto piece = [&](float t0, float t1, float y0, float y1) {
                if (t1 - t0 < 0.02f || y1 - y0 < 0.02f) return;
                s.walls.push_back({ at(t0), at(t1), y0, y1, thick * 0.5f, false, true });
                const Vector2 half = Vector2Scale(edge.out, thick * 0.5f);
                const Vector2 oa = Vector2Add(at(t0), half), ob = Vector2Add(at(t1), half);
                const Vector2 ia = Vector2Subtract(at(t0), half), ib = Vector2Subtract(at(t1), half);
                auto quad = [&](Vector2 p0, float h0, Vector2 p1, float h1, Vector2 p2, float h2, Vector2 p3, float h3, Color c) {
                    s.solid.Quad({ p0.x, h0, p0.y }, { p1.x, h1, p1.y }, { p2.x, h2, p2.y }, { p3.x, h3, p3.y }, c);
                };
                quad(ob, y0, oa, y0, oa, y1, ob, y1, wall);
                const float lowY = std::max(y0, floorY);
                if (y1 > lowY) quad(ia, lowY, ib, lowY, ib, y1, ia, y1, inner);
                if (y1 < ceiling - 0.01f) quad(oa, y1, ob, y1, ib, y1, ia, y1, wall);  // цонхны тавцан
                if (y0 > base + 0.01f) quad(oa, y0, ia, y0, ib, y0, ob, y0, wall);    // тотгоны доод тал
                if (t0 > 0.01f) quad(oa, y0, ia, y0, ia, y1, oa, y1, wall);           // нээлхийн хажуу
                if (t1 < length - 0.01f) quad(ob, y0, ob, y1, ib, y1, ib, y0, wall);
            };
            float cursor = 0.0f;
            for (const Opening& o : openings)
            {
                piece(cursor, o.t0, base, ceiling);
                const float top = o.door ? floorY + doorHeight : floorY + head;
                if (!o.door) piece(o.t0, o.t1, base, floorY + sill);
                piece(o.t0, o.t1, top, ceiling);
                const Vector2 mid = at((o.t0 + o.t1) * 0.5f);
                const float width = o.t1 - o.t0;
                // Жааз: тотго, (цонх бол) тавцан
                box(Vector2Add(mid, Vector2Scale(edge.out, 0.02f)), yaw, top, top + 0.08f, thick + 0.06f, width + 0.16f, frame);
                if (!o.door)
                {
                    box(Vector2Add(mid, Vector2Scale(edge.out, 0.06f)), yaw, floorY + sill - 0.06f, floorY + sill, thick + 0.14f,
                        width + 0.2f, frame);
                    // Цонхоор авирч орох, үсэрч гарах гарц
                    const float outward = std::atan2(edge.out.x, edge.out.y);
                    const Vector2 face = Vector2Add(mid, Vector2Scale(edge.out, thick * 0.5f));
                    const Vector2 inQ = Vector2Subtract(face, Vector2Scale(edge.out, 0.8f + thick));
                    const Vector2 outQ = Vector2Add(face, Vector2Scale(edge.out, 0.95f));
                    const Vector3 inside = { inQ.x, floorY, inQ.y };
                    const Vector3 outside = { outQ.x, s.terrain.HeightAt(outQ.x, outQ.y), outQ.y };
                    s.passages.push_back({ inside, outside, PassageKind::WindowOut, outward, 0.7f });
                    s.passages.push_back({ outside, inside, PassageKind::WindowIn, outward + PI, 1.05f });
                }
                else
                {
                    // Нээж хаадаг хаалга: нугас нээлхийн эхэнд, гадагш эргэнэ
                    Door leaf = {};
                    const Vector2 hinge = at(o.t0);
                    leaf.hinge = { hinge.x, floorY, hinge.y };
                    leaf.width = doorWidth;
                    leaf.height = doorHeight - 0.05f;
                    leaf.closedYaw = yaw;
                    leaf.openYaw = std::atan2(edge.out.x, edge.out.y);
                    leaf.color = kDoor;
                    Interior room = {};
                    room.center = f.center;
                    room.axis = u;
                    room.halfX = L - thick;
                    room.halfZ = S - thick;
                    room.round = false;
                    room.doorOutside = Vector2Add(mid, Vector2Scale(edge.out, 1.5f));
                    room.doorInside = Vector2Subtract(mid, Vector2Scale(edge.out, 1.2f));
                    s.Entrance(leaf, room);
                }
                cursor = o.t1;
            }
            piece(cursor, length, base, ceiling);
        }

        // Газар доорх тэмдэглэгээ (хэнийг ч хаахгүй): дотор нь өвс ургахгүй
        {
            const float minX = std::min({ c0.x, c1.x, c2.x, c3.x }), maxX = std::max({ c0.x, c1.x, c2.x, c3.x });
            const float minZ = std::min({ c0.y, c1.y, c2.y, c3.y }), maxZ = std::max({ c0.y, c1.y, c2.y, c3.y });
            s.Collider({ (minX + maxX) * 0.5f, base - 0.6f, (minZ + maxZ) * 0.5f }, { maxX - minX, 0.4f, maxZ - minZ });
        }

        // Шал, тааз (дотроос харагдана), дээвэр
        const float axisYaw = std::atan2(u.x, u.y);
        box(f.center, axisYaw, floorY - 0.1f, floorY, 2.0f * S - 0.1f, 2.0f * L - 0.1f, JitterColor(kWood, s.rng, 0.08f));
        box(f.center, axisYaw, ceiling - 0.08f, ceiling, 2.0f * S, 2.0f * L, { 214, 208, 196, 255 });
        if (pitched) TownGableRoof(s, f, ceiling, std::clamp(S * 0.6f, 0.6f, 3.2f), 0.4f, roof, wall);
        else FlatRoof(s, { c0, c1, c2, c3 }, ceiling, roof);

        // Тавилга (налуу тул WallSegment-ээр мөргөлдөнө): дэлгүүрт лангуу, байшинд ор
        const Vector2 back = f.At(0.0f, (doorEdge == 0 ? 1.0f : -1.0f) * (S - thick - (shop ? 0.6f : 0.55f)));
        const float span = std::min(shop ? 3.0f : 1.9f, 2.0f * L - 1.6f);
        const float halfDepth = shop ? 0.3f : 0.45f, height = shop ? 1.0f : 0.55f;
        const Vector2 e0 = Vector2Subtract(back, Vector2Scale(u, span * 0.5f)), e1 = Vector2Add(back, Vector2Scale(u, span * 0.5f));
        box(back, axisYaw, floorY, floorY + height, halfDepth * 2.0f, span, shop ? Color{ 120, 92, 64, 255 } : Color{ 150, 60, 52, 255 });
        s.walls.push_back({ e0, e1, floorY, floorY + height, halfDepth, false, false });
    }

    void TownBuilding(Scene& s, const CityBuilding& b)
    {
        const std::vector<Vector2>& p = b.outline;
        const int n = (int)p.size();
        const Plan f = MeasureFootprint(p);
        Vector2 center = { 0.0f, 0.0f };
        float low = 1e9f, high = -1e9f;
        for (const Vector2& q : p)
        {
            center = Vector2Add(center, q);
            const float h = s.terrain.HeightAt(q.x, q.y);
            low = std::min(low, h);
            high = std::max(high, h);
        }
        center = Vector2Scale(center, 1.0f / (float)n);
        const TownKind kind = (&b == gTown.mainHall) ? TownKind::Government : ClassifyTownBuilding(b, f, center);
        const bool rectangular = n <= 8 && f.area > 0.82f * 4.0f * f.halfLong * f.halfShort; // дээвэр хүрээнд таарна

        // --- Өндөр (давхар) ---
        // gen_city: давхрын тоо тэмдэглэгдээгүй орон сууц 28 м (9 давхар) болдог. Аймгийн төвд ихэнх нь 2-5 давхар.
        const bool defaultHeight = b.type == CityBuildingType::Apartments && b.height == 28.0f && b.levels == 9;
        int floors = std::clamp(b.levels, 1, 12);
        float floorHeight = 3.0f;
        switch (kind)
        {
        case TownKind::Shed: floors = 1; floorHeight = 2.2f; break;
        case TownKind::House: floors = (f.area > 110.0f && s.rng.Chance(0.35f)) ? 2 : 1; floorHeight = 2.8f; break;
        case TownKind::Garage: floors = 1; floorHeight = 3.0f; break;
        case TownKind::Shop: floors = b.type == CityBuildingType::Commercial ? std::clamp(b.levels, 1, 3) : (f.area > 180.0f ? 2 : 1); floorHeight = 3.4f; break;
        case TownKind::Office: floors = b.type == CityBuildingType::Generic ? (f.area > 700.0f ? 3 : 2) : std::clamp(b.levels, 2, 4); floorHeight = 3.4f; break;
        case TownKind::Apartments:
            if (defaultHeight)
            {
                if (gTown.region == Region::City) floors = f.area > 900.0f ? 9 : 5; // Эрдэнэт, Дархан
                else floors = f.area > 1000.0f ? 5 : s.rng.Chance(0.4f) ? 3 : 4;
            }
            floors = std::clamp(floors, 2, gTown.region == Region::City ? 12 : 6);
            floorHeight = 3.0f;
            break;
        case TownKind::School: floors = std::clamp(b.levels, 2, 4); floorHeight = 3.5f; break;
        case TownKind::Government: floors = std::clamp(b.levels, 2, 4); floorHeight = 3.8f; break;
        case TownKind::Warehouse: floors = 1; floorHeight = f.area > 600.0f ? 7.5f : 5.0f; break;
        case TownKind::Temple: floors = 1; floorHeight = 5.5f; break;
        }
        const float base = low - 0.3f;
        const float top = high + (float)floors * floorHeight + (kind == TownKind::Apartments || kind == TownKind::Office ? 0.6f : 0.0f);

        // --- Өнгө ---
        // Цонх: шилгүй нүх (дотор нь харанхуй)
        Color wall, roof = { 92, 90, 88, 255 }, frame = kWindowFrame, glass = kWindowHole;
        bool pitched = false, windows = true;
        switch (kind)
        {
        case TownKind::Shed:
            wall = JitterColor(s.rng.Chance(0.5f) ? kWood : kWoodGrey, s.rng, 0.06f);
            windows = false;
            break;
        case TownKind::House:
            if (s.rng.Chance(gTown.logHouses)) wall = JitterColor({ 150, 112, 76, 255 }, s.rng, 0.08f); // дүнзэн байшин
            else wall = JitterColor(gTown.walls[s.rng.Int(0, 2)], s.rng, 0.03f);
            roof = JitterColor(gTown.roofs[s.rng.Int(0, 1)], s.rng, 0.06f);
            frame = s.rng.Chance(0.5f) ? Color{ 236, 236, 230, 255 } : Color{ 60, 110, 170, 255 }; // цагаан эсвэл хөх жааз
            pitched = rectangular;
            break;
        case TownKind::Garage: wall = JitterColor(s.rng.Chance(0.5f) ? kBrick : kConcrete, s.rng, 0.05f); windows = false; break;
        case TownKind::Shop: wall = JitterColor(gTown.walls[s.rng.Int(0, 2)], s.rng, 0.04f); pitched = rectangular && floors == 1 && s.rng.Chance(0.4f);
            roof = gTown.roofs[s.rng.Int(0, 1)]; break;
        case TownKind::Office: wall = JitterColor(gTown.publicWalls[s.rng.Int(0, 1)], s.rng, 0.03f); break;
        case TownKind::Apartments: wall = JitterColor(gTown.publicWalls[s.rng.Int(0, 1)], s.rng, 0.03f); break;
        case TownKind::School: wall = JitterColor({ 226, 196, 150, 255 }, s.rng, 0.03f); break;
        case TownKind::Government: wall = { 232, 228, 216, 255 }; break;
        case TownKind::Warehouse:
            wall = JitterColor(s.rng.Chance(0.5f) ? Color{ 150, 152, 150, 255 } : Color{ 120, 136, 150, 255 }, s.rng, 0.05f);
            roof = { 120, 122, 124, 255 };
            pitched = rectangular && s.rng.Chance(0.6f);
            windows = false;
            break;
        case TownKind::Temple: wall = { 232, 226, 210, 255 }; roof = { 52, 120, 84, 255 }; windows = false; break;
        }

        // Нэг давхар байшин, дэлгүүр: дотогш орж болно (бусад нь цул)
        if ((kind == TownKind::House || kind == TownKind::Shop) && floors == 1 && rectangular && f.area >= 24.0f && f.area <= 260.0f &&
            f.halfShort >= 2.0f && high - low < 0.45f)
        {
            EnterableTownBuilding(s, f, kind == TownKind::Shop, low, high, floorHeight, wall, roof, frame, pitched);
            gCityMask.Fill(p);
            return;
        }

        // --- Хана, цонх ---
        const bool slab = kind == TownKind::Apartments || kind == TownKind::Office || kind == TownKind::School || kind == TownKind::Government;
        const float roofLevel = slab ? top - 0.6f : top; // хавтгай дээврийн ирмэг (парапет)
        int longest = 0;
        float longestLength = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float length = Vector2Distance(p[(size_t)i], p[(size_t)((i + 1) % n)]);
            if (length > longestLength) { longestLength = length; longest = i; }
        }
        for (int i = 0; i < n; ++i)
        {
            const Vector2 a = p[(size_t)i], c = p[(size_t)((i + 1) % n)];
            const float length = Vector2Distance(a, c);
            if (length < 0.05f) continue;
            s.solid.Quad({ c.x, base, c.y }, { a.x, base, a.y }, { a.x, top, a.y }, { c.x, top, c.y }, wall);
            if (slab && length > 4.0f) WallFace(s, a, c, 0.0f, 1.0f, base, high + 0.7f, 0.03f, ScaleColor(wall, 0.72f)); // суурийн бүс
            if (!windows || length < 2.4f) continue;
            // Олон давхар барилгын тайрсан (богино) хана цонхгүй: Зөвлөлтийн үеийн панель байрных шиг
            if (slab && length < 0.55f * longestLength) continue;
            if (kind == TownKind::House)
            {
                // Урт хоёр хананд, хана бүрт 3 хүртэл цонх (гэр хороололд мянга мянган байшин)
                if (length < 0.6f * longestLength) continue;
                const float spacing = std::max(length > 9.0f ? 3.2f : 2.6f, (length - 0.8f) / 3.0f);
                for (int floor = 0; floor < floors; ++floor) WindowRow(s, a, c, high + 0.95f + 2.8f * (float)floor, 1.0f, 1.15f, spacing, frame, glass);
                continue;
            }
            if (kind == TownKind::Shop)
            {
                // Доод давхар: шилэн витрин, дээр нь өнгөт хаяг
                if (length > 4.0f)
                {
                    WallFace(s, a, c, 0.08f, 0.92f, high + 0.4f, high + 2.6f, 0.05f, { 70, 96, 118, 255 });
                    static const Color signs[] = { { 196, 40, 40, 255 }, { 30, 90, 180, 255 }, { 220, 170, 30, 255 }, { 40, 140, 80, 255 } };
                    WallFace(s, a, c, 0.05f, 0.95f, high + 2.75f, high + 3.25f, 0.08f, signs[s.rng.Int(0, 3)]);
                }
                for (int floor = 1; floor < floors; ++floor) WindowRow(s, a, c, high + 1.0f + floorHeight * (float)floor, 1.4f, 1.4f, 3.0f, frame, glass, false);
                continue;
            }
            if (length < 4.0f) continue; // олон давхар барилгын богино хана (булан, тагт)
            for (int floor = 0; floor < floors; ++floor)
            {
                WindowRow(s, a, c, high + 1.0f + floorHeight * (float)floor, 1.4f, 1.5f, kind == TownKind::Apartments ? 3.0f : 3.4f, frame, glass,
                          kind == TownKind::Government);
            }
        }

        // Орох хаалга, саравч: урт талын дагуу (орон сууцанд орц бүрд)
        {
            const Vector2 a = p[(size_t)longest], c = p[(size_t)((longest + 1) % n)];
            const float length = longestLength;
            if (kind == TownKind::Apartments && length > 10.0f)
            {
                const int entrances = std::max(1, (int)(length / 18.0f));
                for (int e = 0; e < entrances; ++e)
                {
                    const float t = ((float)e + 0.5f) / (float)entrances;
                    const float half = 0.75f / length;
                    WallFace(s, a, c, t - half, t + half, high, high + 2.2f, 0.09f, { 70, 56, 46, 255 });
                    // саравч
                    const Vector2 mid = Vector2Lerp(a, c, t);
                    const Vector2 dir = Vector2Scale(Vector2Subtract(c, a), 1.0f / length);
                    const Vector2 out = { dir.y, -dir.x };
                    const float yaw = std::atan2(out.x, out.y);
                    s.solid.SetTransform(MatrixMultiply(MatrixRotateY(yaw), MatrixTranslate(mid.x + out.x * 0.7f, high + 2.6f, mid.y + out.y * 0.7f)));
                    s.solid.Box({ 0.0f, 0.0f, 0.0f }, { 2.6f, 0.16f, 1.4f }, ScaleColor(wall, 0.8f));
                    s.solid.ResetTransform();
                }
            }
            else if ((kind == TownKind::House || kind == TownKind::Shed || kind == TownKind::Office || kind == TownKind::School) && length > 2.0f)
            {
                const float half = (kind == TownKind::Office || kind == TownKind::School ? 0.9f : 0.45f) / length;
                const float t = kind == TownKind::House ? 0.25f : 0.5f;
                WallFace(s, a, c, t - half, t + half, high, high + 2.0f, 0.09f, kind == TownKind::House ? Color{ 96, 66, 46, 255 } : Color{ 70, 60, 50, 255 });
            }
            else if ((kind == TownKind::Garage || kind == TownKind::Warehouse) && length > 4.0f)
            {
                const float half = std::min(1.6f, length * 0.3f) / length;
                WallFace(s, a, c, 0.5f - half, 0.5f + half, high, high + std::min(3.2f, top - high - 0.4f), 0.06f, { 96, 104, 110, 255 });
            }
        }

        // --- Дээвэр ---
        if (pitched)
        {
            const float rise = std::clamp(f.halfShort * (kind == TownKind::Warehouse ? 0.25f : 0.6f), 0.6f, 3.2f);
            TownGableRoof(s, f, top, rise, kind == TownKind::Warehouse ? 0.2f : 0.4f, roof, wall);
        }
        else if (kind == TownKind::Shed && rectangular)
        {
            // Нэг налуу дээвэр
            auto p3 = [&](float along, float across, float y) { const Vector2 q = f.At(along, across); return Vector3{ q.x, y, q.y }; };
            const float A = f.halfLong + 0.2f, B = f.halfShort + 0.2f;
            s.solid.Quad(p3(-A, -B, top - 0.1f), p3(A, -B, top - 0.1f), p3(A, B, top + 0.45f), p3(-A, B, top + 0.45f), { 110, 112, 116, 255 });
        }
        else if (kind == TownKind::Temple && rectangular)
        {
            // Сүм хийд: улаан бүс, хоёр давхар ногоон дээвэр, алтан ганжир
            const Color red = { 160, 40, 36, 255 }, gold = { 214, 172, 64, 255 };
            for (int i = 0; i < n; ++i) WallFace(s, p[(size_t)i], p[(size_t)((i + 1) % n)], 0.0f, 1.0f, top - 1.0f, top, 0.05f, red);
            TownHipRoof(s, f, top, std::clamp(f.halfShort * 0.45f, 1.2f, 3.5f), 1.2f, roof);
            Plan upper = f;
            upper.halfLong *= 0.55f;
            upper.halfShort *= 0.55f;
            const float upperBase = top + std::clamp(f.halfShort * 0.45f, 1.2f, 3.5f) * 0.55f;
            s.solid.Box({ f.center.x, upperBase + 0.6f, f.center.y }, { upper.halfShort * 1.4f, 1.2f, upper.halfShort * 1.4f }, red);
            TownHipRoof(s, upper, upperBase + 1.2f, std::clamp(upper.halfShort * 0.5f, 0.8f, 2.4f), 0.8f, roof);
            s.solid.Cylinder({ f.center.x, upperBase + 1.2f + std::clamp(upper.halfShort * 0.5f, 0.8f, 2.4f), f.center.y }, 0.35f, 0.08f, 1.6f, 8, gold);
        }
        else
        {
            FlatRoof(s, p, roofLevel, roof);
            if (slab && f.area > 150.0f && floors >= 3)
            {
                s.solid.Box({ f.center.x, roofLevel + 1.1f, f.center.y }, { 3.0f, 2.2f, 3.0f }, ScaleColor(wall, 0.7f)); // лифт, шатны өрөө
            }
        }

        // Засаг даргын тамгын газар: урд талдаа цагаан багана, туг
        if (kind == TownKind::Government && &b == gTown.mainHall && f.area > 0.68f * 4.0f * f.halfLong * f.halfShort)
        {
            // Талбай руу харсан урт тал
            const Vector2 toSite = Vector2Subtract(gTown.site, f.center);
            const float sign = Vector2DotProduct(toSite, f.Side()) >= 0.0f ? 1.0f : -1.0f;
            const int columns = std::clamp((int)(f.halfLong / 2.2f), 4, 8);
            const float span = f.halfLong * 0.7f;
            for (int k = 0; k < columns; ++k)
            {
                const float along = -span + 2.0f * span * (float)k / (float)(columns - 1);
                const Vector2 q = f.At(along, sign * (f.halfShort + 2.2f));
                s.solid.Cylinder({ q.x, high, q.y }, 0.38f, 0.32f, roofLevel - high - 0.6f, 8, { 240, 238, 230, 255 });
                s.Collider({ q.x, high + 3.0f, q.y }, { 0.8f, 6.0f, 0.8f });
            }
            // Хаалт (портик) ба гишгүүр
            const Vector2 pc = f.At(0.0f, sign * (f.halfShort + 1.3f));
            const float yaw = std::atan2(f.axis.x, f.axis.y);
            s.solid.SetTransform(MatrixMultiply(MatrixRotateY(yaw), MatrixTranslate(pc.x, 0.0f, pc.y)));
            s.solid.Box({ 0.0f, roofLevel - 0.3f, 0.0f }, { 3.4f, 0.6f, span * 2.0f + 1.6f }, { 236, 234, 226, 255 });
            s.solid.Box({ 0.0f, high + 0.15f, 0.0f }, { 4.2f, 0.3f, span * 2.0f + 2.0f }, kConcrete);
            s.solid.ResetTransform();
            AtLevel(s, roofLevel, [&] { Flagpole(s, f.center.x, f.center.y); });
        }

        CityColliders(s, p, base, top - base);
        gCityMask.Fill(p);
    }

    // Гэр хорооллын гэр: хана (цилиндр), дээвэр (конус), хөх хээтэй хаяавч, тооно, яндан, хаалга (урагш).
    void CityGer(Scene& s, Vector2 center, float radius)
    {
        const float ground = s.terrain.HeightAt(center.x, center.y) - 0.1f;
        const float wall = std::min(1.9f, 1.2f + radius * 0.15f);
        // Гэр хороолол мянга мянган гэртэй тул хямд: 8 тал, тоонгүй (~60 гурвалжин)
        const int sides = 8;
        s.solid.Cylinder({ center.x, ground, center.y }, radius, radius, wall, sides, JitterColor(kGerFelt, s.rng, 0.04f), false);
        s.solid.Cylinder({ center.x, ground + wall - 0.28f, center.y }, radius * 1.01f, radius * 1.01f, 0.22f, sides, kGerTrim, false); // хээтэй тууз
        s.solid.Cylinder({ center.x, ground + wall, center.y }, radius * 1.05f, radius * 0.18f, radius * 0.42f, sides,
                         JitterColor(kGerRoof, s.rng, 0.05f), false);
        // Зуухны яндан тооноос
        s.solid.Cylinder({ center.x + radius * 0.12f, ground + wall + radius * 0.3f, center.y }, 0.08f, 0.08f, 1.4f, 4, { 70, 70, 72, 255 }, false);
        // Хаалга урд (-Z) зүгт (Монгол гэр өмнө зүг рүү харна): улбар шар, хүрээтэй
        const float half = std::min(0.45f, radius * 0.2f), front = center.y - radius - 0.03f;
        s.solid.Quad({ center.x - half - 0.08f, ground, front + 0.01f }, { center.x + half + 0.08f, ground, front + 0.01f },
                     { center.x + half + 0.08f, ground + 1.62f, front + 0.01f }, { center.x - half - 0.08f, ground + 1.62f, front + 0.01f },
                     ScaleColor(kGerDoor, 0.7f));
        s.solid.Quad({ center.x - half, ground, front }, { center.x + half, ground, front }, { center.x + half, ground + 1.5f, front },
                     { center.x - half, ground + 1.5f, front }, kGerDoor);
        s.Collider({ center.x, ground + wall * 0.5f + 0.2f, center.y }, { radius * 1.6f, wall + 0.4f, radius * 1.6f });
        const std::vector<Vector2> square = { { center.x - radius, center.y - radius }, { center.x + radius, center.y - radius },
                                              { center.x + radius, center.y + radius }, { center.x - radius, center.y + radius } };
        gCityMask.Fill(square);
    }

    // OSM-ийн барилга: суурийн олон өнцөгтийг өндрөөр нь босгоно. Давхар бүрд цонхны эгнээ, хавтгай дээвэр;
    // жижиг байшин, сүм хийдэд майхан дээвэр. Налуу газарт суурь нь хамгийн нам буланд.
    void CityBuildingMesh(Scene& s, const CityBuilding& b)
    {
        const std::vector<Vector2>& p = b.outline;
        const int n = (int)p.size();
        float area = 0.0f;
        Vector2 center = { 0.0f, 0.0f };
        float low = 1e9f, high = -1e9f;
        for (int i = 0; i < n; ++i)
        {
            const Vector2 a = p[(size_t)((i + n - 1) % n)], c = p[(size_t)i];
            area += a.x * c.y - c.x * a.y;
            center = Vector2Add(center, c);
            const float h = s.terrain.HeightAt(c.x, c.y);
            low = std::min(low, h);
            high = std::max(high, h);
        }
        area = std::fabs(area) * 0.5f;
        center = Vector2Scale(center, 1.0f / (float)n);

        if (b.type == CityBuildingType::Ger)
        {
            CityGer(s, center, std::clamp(std::sqrt(area / PI), 2.2f, 7.0f));
            return;
        }
        if (!s.layout.IsCapital())
        {
            TownBuilding(s, b); // аймгийн төв: хэлбэр, бүс нутгийн өнгөөр
            return;
        }

        const bool house = b.type == CityBuildingType::House || (b.type == CityBuildingType::Generic && area < 45.0f);
        const float height = house ? std::min(b.height, 3.4f) : b.height;
        const float base = low - 0.3f;
        const float top = high + height;

        // Өнгө: төрлөөр
        static const Color panel[] = { { 186, 182, 172, 255 }, { 196, 180, 160, 255 }, { 164, 176, 184, 255 }, { 190, 168, 160, 255 },
                                       { 172, 178, 160, 255 }, { 206, 196, 170, 255 }, { 178, 156, 136, 255 } };
        static const Color glassy[] = { { 112, 132, 150, 255 }, { 170, 170, 176, 255 }, { 200, 196, 186, 255 }, { 132, 120, 110, 255 } };
        static const Color plaster[] = { { 196, 186, 160, 255 }, { 168, 120, 96, 255 }, { 150, 160, 168, 255 }, { 206, 200, 190, 255 },
                                         { 120, 140, 110, 255 }, { 170, 140, 110, 255 } };
        static const Color tin[] = { { 70, 110, 84, 255 }, { 52, 84, 130, 255 }, { 150, 58, 46, 255 }, { 118, 118, 120, 255 },
                                     { 92, 128, 140, 255 } };
        Color wall, roof = { 86, 86, 90, 255 }, window = kWindowHole; // шилгүй цонхны нүх
        bool windows = !house && height >= 5.0f;
        bool pitched = false;
        switch (b.type)
        {
        case CityBuildingType::Apartments: wall = panel[s.rng.Int(0, 6)]; break;
        case CityBuildingType::Commercial: wall = glassy[s.rng.Int(0, 3)]; break;
        case CityBuildingType::School: wall = { 206, 186, 150, 255 }; break;
        case CityBuildingType::Government: wall = { 214, 210, 200, 255 }; break;
        case CityBuildingType::Religious: wall = { 200, 176, 128, 255 }; roof = { 70, 110, 84, 255 }; pitched = n <= 8; break;
        case CityBuildingType::Industrial: wall = { 150, 148, 140, 255 }; windows = false; break;
        case CityBuildingType::Garage: wall = { 128, 124, 118, 255 }; windows = false; break;
        default:
            if (house)
            {
                wall = plaster[s.rng.Int(0, 5)];
                roof = tin[s.rng.Int(0, 4)];
                pitched = n <= 6;
            }
            else
            {
                wall = s.rng.Chance(0.5f) ? panel[s.rng.Int(0, 6)] : glassy[s.rng.Int(0, 3)];
            }
            break;
        }
        wall = JitterColor(wall, s.rng, 0.04f);

        // Хананууд (оройнууд цагийн зүүний эсрэг тул гадна тал нь (dz, -dx)) ба цонхны эгнээ
        const int floors = std::clamp(b.levels, 1, 40);
        const float floorHeight = std::clamp(height / (float)floors, 2.8f, 5.0f);
        for (int i = 0; i < n; ++i)
        {
            const Vector2 a = p[(size_t)i], c = p[(size_t)((i + 1) % n)];
            const float length = Vector2Distance(a, c);
            if (length < 0.05f) continue;
            s.solid.Quad({ c.x, base, c.y }, { a.x, base, a.y }, { a.x, top, a.y }, { c.x, top, c.y }, wall);
            const Vector2 normal = { (c.y - a.y) / length, -(c.x - a.x) / length };
            auto face = [&](float t0, float t1, float y0, float y1, float out, Color color) {
                auto q = [&](float t, float y) {
                    return Vector3{ a.x + (c.x - a.x) * t + normal.x * out, y, a.y + (c.y - a.y) * t + normal.y * out };
                };
                s.solid.Quad(q(t1, y0), q(t0, y0), q(t0, y1), q(t1, y1), color);
            };
            if (windows && length > 6.0f)
            {
                const float inset = std::min(0.4f, 1.0f / length);
                for (int f = 0; f < floors; ++f)
                {
                    const float y = high + 0.9f + floorHeight * (float)f;
                    if (y + 1.4f > top - 0.4f) break;
                    face(inset, 1.0f - inset, y, y + 1.4f, 0.05f, s.rng.Chance(0.12f) ? Color{ 22, 24, 28, 255 } : window);
                }
            }
            else if (house && length > 3.0f && s.rng.Chance(0.6f))
            {
                const float half = 0.6f / length;
                face(0.5f - half, 0.5f + half, high + 1.0f, high + 2.1f, 0.05f, kWindow); // ганц цонх
            }
        }

        // Дээвэр (дээрээс харагдана)
        if (pitched)
        {
            const float rise = std::clamp(std::sqrt(area) * 0.22f, 0.9f, 3.5f);
            const Vector3 apex = { center.x, top + rise, center.y };
            for (int i = 0; i < n; ++i)
            {
                const Vector2 a = p[(size_t)i], c = p[(size_t)((i + 1) % n)];
                s.solid.Triangle({ c.x, top, c.y }, { a.x, top, a.y }, apex, JitterColor(roof, s.rng, 0.05f));
            }
        }
        else
        {
            const std::vector<Vector2> triangles = Triangulate(p);
            for (size_t i = 0; i + 2 < triangles.size(); i += 3)
            {
                Vector2 a = triangles[i], bb = triangles[i + 1], c = triangles[i + 2];
                const float cross = (bb.x - a.x) * (c.y - a.y) - (bb.y - a.y) * (c.x - a.x);
                if (cross > 0.0f) std::swap(bb, c);
                s.solid.Triangle({ a.x, top, a.y }, { bb.x, top, bb.y }, { c.x, top, c.y }, roof);
            }
            if (!house && height > 12.0f && area > 150.0f)
            {
                s.solid.Box({ center.x, top + 1.1f, center.y }, { 3.0f, 2.2f, 3.0f }, ScaleColor(wall, 0.7f)); // лифтний өрөө
            }
        }

        CityColliders(s, p, base, top - base);
        gCityMask.Fill(p);
    }

    // Хотын мод (улиас, хайлаас): энгийн хэлбэр, нэг модонд ~30 гурвалжин.
    void CityTree(Scene& s, float x, float z, float height)
    {
        const float ground = s.terrain.HeightAt(x, z) - 0.2f;
        const Color bark = JitterColor({ 96, 82, 66, 255 }, s.rng, 0.08f);
        s.solid.Cylinder({ x, ground, z }, 0.24f, 0.14f, height * 0.55f, 5, bark, false);
        const Color leaves = JitterColor(s.rng.Chance(0.3f) ? Color{ 104, 130, 64, 255 } : Color{ 82, 112, 60, 255 }, s.rng, 0.1f);
        const float r = height * 0.24f;
        s.solid.Blob({ x, ground + height * 0.62f, z }, { r * 1.1f, r * 0.9f, r * 1.1f }, leaves, s.rng, 0.12f);
        s.solid.Blob({ x + s.rng.Range(-0.4f, 0.4f), ground + height * 0.86f, z + s.rng.Range(-0.4f, 0.4f) },
                     { r * 0.8f, r * 0.75f, r * 0.8f }, ScaleColor(leaves, 1.08f), s.rng, 0.12f);
        s.Collider({ x, ground + 1.5f, z }, { 0.5f, 3.0f, 0.5f });
    }

    // Гудамжны гэрэлтүүлэг: газрын өндрийг дагана, зарим нь хазайсан.
    void CityLamp(Scene& s, float x, float z, Vector2 toRoad)
    {
        const float ground = s.terrain.HeightAt(x, z);
        const float tilt = s.rng.Chance(0.15f) ? s.rng.Range(-14.0f, 14.0f) * DEG2RAD : 0.0f;
        const float yaw = std::atan2(toRoad.x, toRoad.y);
        s.solid.SetTransform(MatrixMultiply(MatrixMultiply(MatrixRotateZ(tilt), MatrixRotateY(yaw)), MatrixTranslate(x, ground, z)));
        s.solid.Cylinder({ 0.0f, 0.0f, 0.0f }, 0.13f, 0.08f, 8.0f, 5, kMetal, false);
        s.solid.Box({ 0.0f, 7.9f, 0.9f }, { 0.1f, 0.1f, 1.9f }, kMetal);
        s.solid.Box({ 0.0f, 7.82f, 1.8f }, { 0.3f, 0.14f, 0.6f }, kWindowFrame);
        s.solid.ResetTransform();
        s.Collider({ x, ground + 4.0f, z }, { 0.3f, 8.0f, 0.3f });
    }

    // Ус (цөөрөм, голын сав): газрын хотгорын дээгүүр, эргийн түвшнээс бага зэрэг доор
    Vector3 CityWaterPoint(const Scene& s, float x, float z)
    {
        return { x, s.terrain.HeightAt(x, z) + s.layout.LakeDepth(x, z) - 0.45f, z };
    }

    void CityWaterPolygon(Scene& s, const std::vector<Vector2>& outline)
    {
        const std::vector<Vector2> triangles = Triangulate(outline);
        const Color water = { 70, 112, 140, 190 };
        for (size_t i = 0; i + 2 < triangles.size(); i += 3)
        {
            s.glass.Triangle(CityWaterPoint(s, triangles[i].x, triangles[i].y), CityWaterPoint(s, triangles[i + 1].x, triangles[i + 1].y),
                             CityWaterPoint(s, triangles[i + 2].x, triangles[i + 2].y), water);
        }
    }

    // ------------------------------------------------------------------ Хашаа, талбай, дурсгалт газар, хотын үүд

    const float kGateWidth = 4.0f; // хашааны хаалганы өргөн (машин чөлөөтэй багтана)

    // Хашааны хаалгатай хэсгүүд: замд хамгийн ойр урт хэсэгт заавал нэг, урт хашаанд ~50 м тутамд нэмэлт
    // (OSM-д хаалга нь тэмдэглэгддэггүй).
    std::vector<bool> FenceGates(Scene& s, const std::vector<Vector2>& path)
    {
        const int count = (int)path.size();
        std::vector<bool> gates(count > 1 ? (size_t)count - 1 : 0, false);
        std::vector<float> along((size_t)std::max(count, 1), 0.0f);
        for (int i = 1; i < count; ++i) along[(size_t)i] = along[(size_t)i - 1] + Vector2Distance(path[(size_t)i - 1], path[(size_t)i]);
        auto fits = [&](int i) { return Vector2Distance(path[(size_t)i], path[(size_t)i + 1]) >= kGateWidth + 1.2f; };
        int best = -1, longest = -1;
        float bestDistance = 1e9f, longestLength = 0.0f;
        for (int i = 0; i + 1 < count; ++i)
        {
            const float length = Vector2Distance(path[(size_t)i], path[(size_t)i + 1]);
            if (length > longestLength)
            {
                longestLength = length;
                longest = i;
            }
            if (!fits(i)) continue;
            const Vector2 mid = Vector2Lerp(path[(size_t)i], path[(size_t)i + 1], 0.5f);
            const float distance = s.layout.RoadDistance(mid.x, mid.y);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = i;
            }
        }
        // Урт хэсэггүй (жижиг хэсгүүдээс тогтсон) хашаа: хамгийн урт хэсэгт нь (FenceGate нарийсгана)
        if (best < 0 && longestLength >= 3.0f) best = longest;
        if (best < 0) return gates;
        gates[(size_t)best] = true;
        std::vector<float> marks = { (along[(size_t)best] + along[(size_t)best + 1]) * 0.5f };
        for (int i = 0; i + 1 < count; ++i)
        {
            if (gates[(size_t)i] || !fits(i)) continue;
            const float mark = (along[(size_t)i] + along[(size_t)i + 1]) * 0.5f;
            bool far = true;
            for (float other : marks) far = far && std::fabs(other - mark) >= 50.0f;
            if (!far) continue;
            gates[(size_t)i] = true;
            marks.push_back(mark);
        }
        return gates;
    }

    // a -> b хэсгийн голд kGateWidth өргөн нүх (g0, g1 гаргана). Нүхэнд g0 нугастай нэг хавтас хаалга: F-ээр
    // нээж хаана, хаалттай үед хүн, унаа нэвтрэхгүй (зомби удаан нүдвэл эвдэрнэ). Замаас холдох тал руу нээгдэнэ.
    void FenceGate(Scene& s, Vector2 a, Vector2 b, float height, Color paint, Vector2& g0, Vector2& g1)
    {
        const float length = Vector2Distance(a, b);
        const float width = std::min(kGateWidth, length - 0.6f);
        const float half = width * 0.5f / length;
        g0 = Vector2Lerp(a, b, 0.5f - half);
        g1 = Vector2Lerp(a, b, 0.5f + half);
        const Vector2 dir = Vector2Normalize(Vector2Subtract(g1, g0));
        const Vector2 mid = Vector2Lerp(g0, g1, 0.5f);
        Vector2 inward = { -dir.y, dir.x };
        if (s.layout.RoadDistance(mid.x + inward.x * 2.0f, mid.y + inward.y * 2.0f) <
            s.layout.RoadDistance(mid.x - inward.x * 2.0f, mid.y - inward.y * 2.0f))
        {
            inward = Vector2Negate(inward);
        }
        Door leaf = {};
        leaf.hinge = { g0.x, std::min(s.terrain.HeightAt(g0.x, g0.y), s.terrain.HeightAt(g1.x, g1.y)) - 0.05f, g0.y };
        leaf.width = width;
        leaf.height = height;
        leaf.gate = true;
        leaf.closedYaw = std::atan2(dir.x, dir.y);
        leaf.openYaw = std::atan2(inward.x, inward.y);
        leaf.color = paint;
        s.doors.push_back(leaf);
    }

    // Хашаа: нэг шугам бүхэлдээ нэг материал (банз, будсан төмөр, шавар хана), булан бүрд багана. Хаалга нь
    // замд хамгийн ойр хэсэгт (урт хашаанд хэд хэд), нээж хаадаг.
    void TownFence(Scene& s, const std::vector<Vector2>& path, bool masonry)
    {
        const int count = (int)path.size();
        if (count < 2) return;
        Color color;
        float height;
        if (masonry)
        {
            color = JitterColor(gTown.wallFence, s.rng, 0.04f);
            height = 2.1f;
        }
        else if (s.rng.Chance(gTown.metalFences))
        {
            color = JitterColor(gTown.fenceMetal[s.rng.Int(0, 1)], s.rng, 0.05f); // будсан төмөр хуудас
            height = 2.0f;
        }
        else
        {
            color = JitterColor(gTown.fenceWood[s.rng.Int(0, 1)], s.rng, 0.05f); // банз
            height = s.rng.Range(1.8f, 2.2f);
        }
        const Color dusty = ScaleColor(color, 0.72f); // доод хэсэг шороонд
        const Color post = ScaleColor(color, 0.62f);

        const std::vector<bool> gates = FenceGates(s, path);

        // ~2.2 м хэсгүүд: хэсэг бүр машинаар дайрвал тусдаа нурна (шавар хана илүү хурдад)
        auto panel = [&](Vector2 from, Vector2 to, float h, Color bottom, Color upper) {
            const int pieces = std::max(1, (int)std::round(Vector2Distance(from, to) / 2.2f));
            for (int k = 0; k < pieces; ++k)
            {
                const Vector2 a = Vector2Lerp(from, to, (float)k / (float)pieces), b = Vector2Lerp(from, to, (float)(k + 1) / (float)pieces);
                const float ha = s.terrain.HeightAt(a.x, a.y) - 0.1f, hb = s.terrain.HeightAt(b.x, b.y) - 0.1f;
                const Vector3 a0 = { a.x, ha, a.y }, b0 = { b.x, hb, b.y }, b1 = { b.x, hb + h, b.y }, a1 = { a.x, ha + h, a.y };
                s.BeginPanel();
                s.solid.Triangle(a0, b0, b1, bottom, bottom, upper);
                s.solid.Triangle(a0, b1, a1, bottom, upper, upper);
                // Мөргөлдөөн: хүн, зомби нэвтрэхгүй; банзан, төмөр хашааг цэрэг давна (чулуун ханыг ч)
                s.walls.push_back({ a, b, std::min(ha, hb), std::max(ha, hb) + h, 0.08f, true });
                s.EndPanel(a, b, h, upper, masonry ? 7.0f : 3.0f, (int)s.walls.size() - 1, -1);
            }
        };
        auto postAt = [&](Vector2 q, float h, Color c, float size) {
            const float ground = s.terrain.HeightAt(q.x, q.y) - 0.1f;
            s.solid.Box({ q.x, ground + h * 0.5f, q.y }, { size, h, size }, c);
        };

        for (int i = 0; i + 1 < count; ++i)
        {
            const Vector2 a = path[(size_t)i], b = path[(size_t)i + 1];
            gFenceMask.Mark(a, b);
            if (!gates[(size_t)i])
            {
                panel(a, b, height, dusty, color);
                continue;
            }
            // Хаалга: будсан төмөр хавтас (нээж хаадаг), хоёр өндөр багана, дээр нь хөндлөвч
            static const Color paints[] = { { 40, 90, 160, 255 }, { 50, 120, 80, 255 }, { 150, 50, 40, 255 }, { 110, 80, 50, 255 } };
            const Color paint = JitterColor(paints[s.rng.Int(0, 3)], s.rng, 0.05f);
            Vector2 g0, g1;
            FenceGate(s, a, b, height, paint, g0, g1);
            panel(a, g0, height, dusty, color);
            panel(g1, b, height, dusty, color);
            const Vector2 gm = Vector2Lerp(g0, g1, 0.5f);
            postAt(g0, height + 0.7f, post, 0.24f);
            postAt(g1, height + 0.7f, post, 0.24f);
            const Vector2 dir = Vector2Scale(Vector2Subtract(g1, g0), 1.0f / Vector2Distance(g0, g1));
            const float ground = (s.terrain.HeightAt(g0.x, g0.y) + s.terrain.HeightAt(g1.x, g1.y)) * 0.5f - 0.1f;
            s.solid.SetTransform(MatrixMultiply(MatrixRotateY(std::atan2(dir.x, dir.y)), MatrixTranslate(gm.x, ground + height + 0.6f, gm.y)));
            s.solid.Box({ 0.0f, 0.0f, 0.0f }, { 0.2f, 0.24f, 3.8f }, post);
            s.solid.ResetTransform();
        }
        if (!masonry)
        {
            // Булангийн багана: үзүүр ба 25 хэмээс их эргэлт бүрд
            for (int i = 0; i < count; ++i)
            {
                bool corner = i == 0 || i == count - 1;
                if (!corner)
                {
                    const Vector2 d0 = Vector2Normalize(Vector2Subtract(path[(size_t)i], path[(size_t)i - 1]));
                    const Vector2 d1 = Vector2Normalize(Vector2Subtract(path[(size_t)i + 1], path[(size_t)i]));
                    corner = Vector2DotProduct(d0, d1) < 0.9f;
                }
                if (corner) postAt(path[(size_t)i], height + 0.12f, post, 0.16f);
            }
        }
    }

    // Цагаан суварга: шатлалт суурь, бөмбөгөр, алтан оргил (scale = 1 бол ~9 м)
    void TownStupa(Scene& s, float x, float z, float scale)
    {
        const Color white = { 236, 234, 226, 255 }, gold = { 206, 168, 72, 255 };
        const float g = s.terrain.HeightAt(x, z) - 0.2f;
        s.solid.Box({ x, g + 0.5f * scale, z }, { 5.0f * scale, 1.0f * scale, 5.0f * scale }, white);
        s.solid.Box({ x, g + 1.4f * scale, z }, { 3.9f * scale, 0.8f * scale, 3.9f * scale }, white);
        s.solid.Box({ x, g + 2.05f * scale, z }, { 3.0f * scale, 0.5f * scale, 3.0f * scale }, white);
        s.solid.Ellipsoid({ x, g + 3.3f * scale, z }, { 1.6f * scale, 1.5f * scale, 1.6f * scale }, 5, 10, white);
        s.solid.Box({ x, g + 4.9f * scale, z }, { 1.0f * scale, 0.5f * scale, 1.0f * scale }, white);
        s.solid.Cylinder({ x, g + 5.15f * scale, z }, 0.45f * scale, 0.1f * scale, 2.6f * scale, 8, gold);
        s.solid.Ellipsoid({ x, g + 7.9f * scale, z }, { 0.25f * scale, 0.3f * scale, 0.25f * scale }, 3, 6, gold);
        s.Collider({ x, g + 2.5f * scale, z }, { 5.0f * scale, 5.0f * scale, 5.0f * scale });
    }

    // Мөргөлдөөнгүй, барилгагүй, усгүй, налуу багатай (half хагас хэмжээтэй) дөрвөлжин газар
    bool TownFreeArea(const Scene& s, Vector2 c, float half, const std::vector<CityClearing>& clearings, bool avoidSite = true)
    {
        if (std::fabs(c.x) > kCityHalfExtent - half - 20.0f || std::fabs(c.y) > kCityHalfExtent - half - 20.0f) return false;
        for (const CityClearing& k : clearings)
        {
            if (c.x + half > k.x0 - 10.0f && c.x - half < k.x1 + 10.0f && c.y + half > k.z0 - 10.0f && c.y - half < k.z1 + 10.0f) return false;
        }
        if (avoidSite && Vector2Distance(c, gTown.site) < half + 35.0f) return false; // төвийн талбай, даалгаврын газар
        float low = 1e9f, high = -1e9f;
        for (float dz = -half; dz <= half; dz += 4.0f)
        {
            for (float dx = -half; dx <= half; dx += 4.0f)
            {
                const float x = c.x + dx, z = c.y + dz;
                if (gCityMask.At(x, z) || gFenceMask.At(x, z) || s.layout.RoadDistance(x, z) < 2.0f || s.layout.CityGround(x, z) == 6) return false;
                const float h = s.terrain.HeightAt(x, z);
                low = std::min(low, h);
                high = std::max(high, h);
            }
        }
        return high - low < 3.5f;
    }

    // from-оос rMin..rMax зайд хамгийн ойрын чөлөөт газар (олдохгүй бол false)
    bool TownFindArea(const Scene& s, Vector2 from, float rMin, float rMax, float half, const std::vector<CityClearing>& clearings, Vector2& out)
    {
        for (float r = rMin; r <= rMax; r += 25.0f)
        {
            const int steps = std::max(8, (int)(2.0f * PI * r / 30.0f));
            for (int k = 0; k < steps; ++k)
            {
                const float a = 2.0f * PI * (float)k / (float)steps + r * 0.01f;
                const Vector2 c = { from.x + std::cos(a) * r, from.y + std::sin(a) * r };
                if (TownFreeArea(s, c, half, clearings))
                {
                    out = c;
                    return true;
                }
            }
        }
        return false;
    }

    // Чөлөөт газрыг эзэлсэн гэж тэмдэглэнэ (мод, машин орохгүй)
    void TownOccupy(Vector2 c, float halfX, float halfZ)
    {
        gCityMask.Fill({ { c.x - halfX, c.y - halfZ }, { c.x + halfX, c.y - halfZ }, { c.x + halfX, c.y + halfZ }, { c.x - halfX, c.y + halfZ } });
    }

    // Газрын налууг дагасан хавтан чулуун талбай (4 м нүд, ээлжилсэн өнгө)
    void TownPaving(Scene& s, Vector2 c, Vector2 u, float halfU, float halfV, Color color)
    {
        const Vector2 v = { -u.y, u.x };
        const int nu = std::max(1, (int)(halfU * 2.0f / 4.0f)), nv = std::max(1, (int)(halfV * 2.0f / 4.0f));
        auto at = [&](int i, int j) {
            const float a = -halfU + 2.0f * halfU * (float)i / (float)nu, b = -halfV + 2.0f * halfV * (float)j / (float)nv;
            const Vector2 q = Vector2Add(c, Vector2Add(Vector2Scale(u, a), Vector2Scale(v, b)));
            return Vector3{ q.x, s.terrain.HeightAt(q.x, q.y) + 0.095f, q.y };
        };
        for (int j = 0; j < nv; ++j)
        {
            for (int i = 0; i < nu; ++i)
            {
                const Color tile = (i + j) % 2 ? color : ScaleColor(color, 0.93f);
                s.solid.Quad(at(i, j), at(i + 1, j), at(i + 1, j + 1), at(i, j + 1), tile);
            }
        }
    }

    // Аймгийн төвийн талбай: хавтан чулуу, морьт хөшөө, гурван туг, гэрэлтүүлэг, сандал, цэцгийн мандал,
    // хоёр талдаа эгнээ мод. Талбайн голд (TownSite) даалгаврын газар тул хөшөө нь Засаг даргын байрны зүгт.
    void TownSquare(Scene& s, const CityData& city)
    {
        Vector2 site = gTown.site;
        Vector2 u = { 0.0f, 1.0f }; // талбайн урт тэнхлэг: Засаг даргын байрнаас талбай руу
        if (gTown.mainHall)
        {
            const Plan hall = MeasureFootprint(gTown.mainHall->outline);
            const Vector2 d = Vector2Subtract(site, hall.center);
            if (Vector2Length(d) > 1.0f) u = Vector2Normalize(d);
        }
        const Vector2 v = { -u.y, u.x };
        // Их зам, гудамж (3-р зэрэглэл хүртэл): талбай тэдгээрийг хаахгүй. Хорооллын гудамж, явган зам, байрны
        // хашааны замын дээгүүр хавтан тавина (хавтан нь бүх замаас өндөр).
        CityMask majorRoads;
        majorRoads.Reset();
        for (const CityRoad& road : city.roads)
        {
            if (road.klass > CityRoadClass::Tertiary || road.dirt) continue;
            const std::vector<Vector2> path = Resample(road.points, 2.0f);
            for (size_t i = 0; i + 1 < path.size(); ++i)
            {
                const Vector2 along = Vector2Normalize(Vector2Subtract(path[i + 1], path[i]));
                const Vector2 side = { -along.y, along.x };
                const float w = road.halfWidth + 1.0f;
                majorRoads.Fill({ Vector2Add(path[i], Vector2Scale(side, -w)), Vector2Add(path[i + 1], Vector2Scale(side, -w)),
                                  Vector2Add(path[i + 1], Vector2Scale(side, w)), Vector2Add(path[i], Vector2Scale(side, w)) });
            }
        }
        // Барилга, их замгүй хамгийн том хэмжээ: даалгаврын газрыг төвд нь, эсвэл түүнийг багтаасан ойролцоох байрлалд
        float halfU = 0.0f, halfV = 0.0f;
        auto fits = [&](Vector2 c, float hu, float hv) {
            for (float a = -hu; a <= hu; a += 3.0f)
            {
                for (float b = -hv; b <= hv; b += 3.0f)
                {
                    const Vector2 q = Vector2Add(c, Vector2Add(Vector2Scale(u, a), Vector2Scale(v, b)));
                    if (gCityMask.At(q.x, q.y) || gFenceMask.At(q.x, q.y) || majorRoads.At(q.x, q.y) || s.layout.CityGround(q.x, q.y) == 6)
                    {
                        return false;
                    }
                }
            }
            return true;
        };
        for (float scale : { 1.0f, 0.8f, 0.62f, 0.48f })
        {
            const float hu = 26.0f * scale, hv = 34.0f * scale;
            for (float shiftU : { 0.0f, -0.5f, 0.5f })
            {
                for (float shiftV : { 0.0f, -0.5f, 0.5f })
                {
                    const Vector2 c = Vector2Add(gTown.site, Vector2Add(Vector2Scale(u, shiftU * hu), Vector2Scale(v, shiftV * hv)));
                    if (halfU > 0.0f || !fits(c, hu, hv)) continue;
                    site = c;
                    halfU = hu;
                    halfV = hv;
                }
            }
            if (halfU > 0.0f) break;
        }
        if (halfU <= 0.0f)
        {
            // Төвийн цэг давчуу (зам, барилга ойр): 200 м дотор хамгийн ойрын чөлөөт газарт жижиг талбай
            bool found = false;
            for (Vector2 half : { Vector2{ 18.0f, 21.0f }, Vector2{ 14.0f, 16.0f } })
            {
                for (float r = 15.0f; r <= 350.0f && !found; r += 10.0f)
                {
                    for (int k = 0; k < 16 && !found; ++k)
                    {
                        const float a = 2.0f * PI * (float)k / 16.0f;
                        const Vector2 c = { gTown.site.x + std::cos(a) * r, gTown.site.y + std::sin(a) * r };
                        if (Vector2Distance(c, gTown.site) < half.y + 8.0f || !fits(c, half.x, half.y)) continue; // даалгаврын газрыг хаахгүй
                        site = c;
                        halfU = half.x;
                        halfV = half.y;
                        found = true;
                    }
                }
            }
            if (!found)
            {
                TraceLog(LOG_INFO, "TOWN: no room for the central square near (%.0f, %.0f)", gTown.site.x, gTown.site.y);
                return;
            }
        }
        auto P = [&](float a, float b) { return Vector2Add(site, Vector2Add(Vector2Scale(u, a), Vector2Scale(v, b))); };
        TownPaving(s, site, u, halfU, halfV, kPaving);

        // Хөшөө: талбайн голоос Засаг даргын байрны зүгт; даалгаврын газар (gTown.site) тэнд байвал эсрэг талд
        const float end = Vector2Distance(P(-halfU * 0.5f, 0.0f), gTown.site) > 10.0f ? -1.0f : 1.0f;
        const Vector2 statue = P(end * halfU * 0.5f, 0.0f);
        AtLevel(s, s.terrain.HeightAt(statue.x, statue.y), [&] { SukhbaatarStatue(s, statue.x, statue.y); });
        // Төрийн далбаа: хөшөөний ард гурав
        for (float b : { -4.0f, 0.0f, 4.0f })
        {
            const Vector2 q = P(end * halfU * 0.86f, b);
            AtLevel(s, s.terrain.HeightAt(q.x, q.y), [&] { Flagpole(s, q.x, q.y); });
        }
        // Гэрэлтүүлэг: дөрвөн буланд
        for (float a : { -1.0f, 1.0f })
        {
            for (float b : { -1.0f, 1.0f })
            {
                const Vector2 q = P(a * (halfU - 2.0f), b * (halfV - 2.0f));
                AtLevel(s, s.terrain.HeightAt(q.x, q.y), [&] { LightPole(s, q.x, q.y, 0); });
            }
        }
        // Сандал: урт хоёр талын дагуу
        const float yawU = std::atan2(u.x, u.y);
        for (float b : { -1.0f, 1.0f })
        {
            for (float a = -halfU * 0.6f; a <= halfU * 0.6f; a += 7.0f)
            {
                const Vector2 q = P(a, b * (halfV - 4.5f));
                const float g = s.terrain.HeightAt(q.x, q.y) + 0.07f;
                s.solid.SetTransform(MatrixMultiply(MatrixRotateY(yawU), MatrixTranslate(q.x, g, q.y)));
                s.solid.Box({ 0.0f, 0.45f, 0.0f }, { 0.5f, 0.08f, 1.8f }, kWood);
                s.solid.Box({ b * 0.22f, 0.75f, 0.0f }, { 0.06f, 0.5f, 1.8f }, kWood);
                for (float leg : { -0.75f, 0.75f }) s.solid.Box({ 0.0f, 0.22f, leg }, { 0.45f, 0.44f, 0.08f }, kMetal);
                s.solid.ResetTransform();
            }
        }
        // Цэцгийн мандал: хөшөөний хоёр талд
        static const Color flowers[] = { { 206, 52, 48, 255 }, { 236, 196, 52, 255 }, { 236, 232, 220, 255 }, { 196, 90, 160, 255 } };
        for (float b : { -1.0f, 1.0f })
        {
            const Vector2 q = P(end * halfU * 0.5f, b * 9.0f);
            const float g = s.terrain.HeightAt(q.x, q.y);
            s.solid.SetTransform(MatrixMultiply(MatrixRotateY(yawU), MatrixTranslate(q.x, g, q.y)));
            s.solid.Box({ 0.0f, 0.2f, 0.0f }, { 7.0f, 0.4f, 3.0f }, kConcrete);
            s.solid.Box({ 0.0f, 0.42f, 0.0f }, { 6.6f, 0.08f, 2.6f }, { 92, 120, 60, 255 });
            for (int k = 0; k < 18; ++k)
            {
                s.solid.Box({ s.rng.Range(-3.0f, 3.0f), 0.52f, s.rng.Range(-1.1f, 1.1f) }, { 0.25f, 0.16f, 0.25f }, flowers[s.rng.Int(0, 3)]);
            }
            s.solid.ResetTransform();
        }
        TownOccupy(site, std::max(halfU, halfV) * 0.7f, std::max(halfU, halfV) * 0.7f);
        // Эгнээ мод: урт хоёр талын гадна
        for (float b : { -1.0f, 1.0f })
        {
            for (float a = -halfU + 3.0f; a <= halfU - 3.0f; a += 7.0f)
            {
                const Vector2 q = P(a, b * (halfV + 3.0f));
                if (gCityMask.Blocked(q.x, q.y, 1.4f) || s.layout.RoadDistance(q.x, q.y) < 1.0f) continue;
                CityTree(s, q.x, q.y, 7.5f);
            }
        }
        // Хөшөөний орчмыг эзэлнэ (талбайн бусад хэсэгт машин тавихгүй)
        gCityMask.Fill({ P(-halfU, -halfV), P(halfU, -halfV), P(halfU, halfV), P(-halfU, halfV) });
        TraceLog(LOG_INFO, "TOWN: central square %.0f x %.0f m at (%.0f, %.0f)", halfU * 2.0f, halfV * 2.0f, site.x, site.y);
    }

    // Аймаг бүрийн онцлох газар (ойролцоо: жинхэнэ байршлыг OSM-ийн сүм хийдээс эсвэл төвийн ойролцоох
    // чөлөөт газраас авна). Өөрчлөх бол LandmarkFor-ын хүснэгтийг засна.
    enum class Landmark
    {
        None,
        Monastery,    // сүм хийд: хашаа, гол дуган, суваргууд
        Mosque,       // сүм (Баян-Өлгий)
        Fortress,     // хэрмийн туурь (Ховдын Сангийн хэрэм)
        HillPavilion, // толгой дээрх асар (Улиастай)
        Industry,     // уулын баяжуулах үйлдвэр (Эрдэнэт)
        Stupas,       // суваргын эгнээ
        Station,      // төмөр замын өртөө
        Memorial,     // хөшөө дурсгал (өндөр багана, од)
        Chinggis      // Чингис хааны морьт хөшөө (Хэнтий)
    };

    Landmark LandmarkFor(int province)
    {
        static const Landmark table[22] = {
            Landmark::Mosque,       // 0 Баян-Өлгий
            Landmark::Monastery,    // 1 Увс
            Landmark::Fortress,     // 2 Ховд
            Landmark::HillPavilion, // 3 Завхан
            Landmark::Stupas,       // 4 Говь-Алтай
            Landmark::Monastery,    // 5 Хөвсгөл
            Landmark::Monastery,    // 6 Баянхонгор
            Landmark::Monastery,    // 7 Архангай
            Landmark::Stupas,       // 8 Булган
            Landmark::Monastery,    // 9 Өвөрхангай
            Landmark::Industry,     // 10 Орхон
            Landmark::Stupas,       // 11 Өмнөговь
            Landmark::Station,      // 12 Сэлэнгэ
            Landmark::Memorial,     // 13 Дархан-Уул
            Landmark::Memorial,     // 14 Төв
            Landmark::None,         // 15 Улаанбаатар (Сүхбаатарын талбай)
            Landmark::Monastery,    // 16 Дундговь
            Landmark::Station,      // 17 Говьсүмбэр
            Landmark::Station,      // 18 Дорноговь
            Landmark::Chinggis,     // 19 Хэнтий
            Landmark::Monastery,    // 20 Сүхбаатар
            Landmark::Memorial,     // 21 Дорнод
        };
        return province >= 0 && province < 22 ? table[province] : Landmark::None;
    }

    // Нэг давхар дөрвөлжин байгууламж (дуган, асар): хана, улаан бүс, дөрвөн налуу дээвэр
    void TownHall(Scene& s, Vector2 c, Vector2 u, float halfLong, float halfShort, float wallHeight, Color wall, Color roof)
    {
        Plan f;
        f.center = c;
        f.axis = u;
        f.halfLong = halfLong;
        f.halfShort = halfShort;
        float low = 1e9f;
        for (float a : { -1.0f, 1.0f })
        {
            for (float b : { -1.0f, 1.0f })
            {
                const Vector2 q = f.At(a * halfLong, b * halfShort);
                low = std::min(low, s.terrain.HeightAt(q.x, q.y));
            }
        }
        const float yaw = std::atan2(u.x, u.y);
        s.solid.SetTransform(MatrixMultiply(MatrixRotateY(yaw), MatrixTranslate(c.x, low - 0.3f, c.y)));
        s.solid.Box({ 0.0f, 0.3f, 0.0f }, { halfShort * 2.0f + 1.6f, 0.9f, halfLong * 2.0f + 1.6f }, kConcrete); // тавцан
        s.solid.Box({ 0.0f, 0.75f + wallHeight * 0.5f, 0.0f }, { halfShort * 2.0f, wallHeight, halfLong * 2.0f }, wall);
        s.solid.Box({ 0.0f, 0.75f + wallHeight - 0.5f, 0.0f }, { halfShort * 2.0f + 0.1f, 1.0f, halfLong * 2.0f + 0.1f }, { 160, 40, 36, 255 });
        s.solid.Box({ halfShort + 0.06f, 0.75f + 1.3f, 0.0f }, { 0.06f, 2.6f, 2.2f }, { 150, 36, 32, 255 }); // хаалга
        s.solid.ResetTransform();
        TownHipRoof(s, f, low + 0.45f + wallHeight, std::clamp(halfShort * 0.55f, 1.2f, 4.0f), 1.3f, roof);
        const std::vector<Vector2> outline = { f.At(-halfLong, -halfShort), f.At(halfLong, -halfShort), f.At(halfLong, halfShort), f.At(-halfLong, halfShort) };
        CityColliders(s, outline, low - 0.3f, wallHeight + 1.0f);
        gCityMask.Fill(outline);
    }

    // Нэг хэсэг хана (шороон хэрэм, сүм хийдийн хашаа): газрын налууг дагана
    void TownWall(Scene& s, Vector2 a, Vector2 b, float height, float thickness, Color color, bool collide)
    {
        const float length = Vector2Distance(a, b);
        const Vector2 mid = Vector2Lerp(a, b, 0.5f);
        const Vector2 dir = Vector2Scale(Vector2Subtract(b, a), 1.0f / std::max(length, 0.01f));
        const float ground = std::min(s.terrain.HeightAt(a.x, a.y), s.terrain.HeightAt(b.x, b.y)) - 0.4f;
        s.solid.SetTransform(MatrixMultiply(MatrixRotateY(std::atan2(dir.x, dir.y)), MatrixTranslate(mid.x, ground, mid.y)));
        s.solid.Box({ 0.0f, (height + 0.4f) * 0.5f, 0.0f }, { thickness, height + 0.4f, length }, color);
        s.solid.ResetTransform();
        if (!collide) return;
        // Налуу хана: богино хэсгүүдээр мөргөлдөөн
        const int pieces = std::max(1, (int)(length / 3.0f));
        for (int k = 0; k < pieces; ++k)
        {
            const Vector2 q = Vector2Lerp(a, b, ((float)k + 0.5f) / (float)pieces);
            const float size = std::max(thickness, length / (float)pieces);
            s.Collider({ q.x, ground + height * 0.5f, q.y }, { std::fabs(dir.x) * size + std::fabs(dir.y) * thickness, height + 0.4f,
                                                             std::fabs(dir.y) * size + std::fabs(dir.x) * thickness });
        }
    }

    void TownLandmark(Scene& s, const CityData& city, const std::vector<CityClearing>& clearings)
    {
        const Landmark landmark = LandmarkFor(gTown.province);
        if (landmark == Landmark::None) return;
        Vector2 at = gTown.site;
        const Color white = { 236, 234, 226, 255 }, gold = { 206, 168, 72, 255 }, red = { 160, 40, 36, 255 };
        const Color greenRoof = { 52, 120, 84, 255 };
        const char* name = "";
        switch (landmark)
        {
        case Landmark::Monastery:
        {
            // Хамгийн ойрын OSM-ийн сүм хийдийн дэргэд, эсвэл төвөөс 150-900 м-т чөлөөт газар
            Vector2 near = gTown.site;
            float best = 2500.0f;
            for (const CityBuilding& b : city.buildings)
            {
                if (b.type != CityBuildingType::Religious) continue;
                const Vector2 c = MeasureFootprint(b.outline).center;
                const float d = Vector2Distance(c, gTown.site);
                if (d < best) { best = d; near = c; }
            }
            if (!TownFindArea(s, near, near.x == gTown.site.x && near.y == gTown.site.y ? 150.0f : 30.0f, 900.0f, 30.0f, clearings, at)) return;
            name = "monastery";
            // Хашаа (цагаан хана, улаан оройтой), урд зүгт (-Z) хаалга
            const float half = 28.0f;
            const Vector2 corners[4] = { { at.x - half, at.y - half }, { at.x + half, at.y - half }, { at.x + half, at.y + half }, { at.x - half, at.y + half } };
            for (int e = 0; e < 4; ++e)
            {
                const Vector2 a = corners[e], b = corners[(e + 1) % 4];
                if (e == 0)
                {
                    TownWall(s, a, { at.x - 3.0f, a.y }, 2.6f, 0.6f, white, true);
                    TownWall(s, { at.x + 3.0f, a.y }, b, 2.6f, 0.6f, white, true);
                    for (float x : { -3.4f, 3.4f })
                    {
                        const float g = s.terrain.HeightAt(at.x + x, a.y);
                        s.solid.Box({ at.x + x, g + 2.0f, a.y }, { 1.0f, 4.4f, 1.0f }, red);
                    }
                    const float g = s.terrain.HeightAt(at.x, a.y);
                    s.solid.Box({ at.x, g + 4.4f, a.y }, { 8.4f, 0.7f, 1.6f }, greenRoof);
                    continue;
                }
                TownWall(s, a, b, 2.6f, 0.6f, white, true);
            }
            TownHall(s, { at.x, at.y + 6.0f }, { 1.0f, 0.0f }, 9.0f, 7.0f, 5.5f, white, greenRoof); // гол дуган
            TownHall(s, { at.x - 17.0f, at.y + 2.0f }, { 0.0f, 1.0f }, 6.0f, 4.0f, 4.0f, white, greenRoof);
            TownHall(s, { at.x + 17.0f, at.y + 2.0f }, { 0.0f, 1.0f }, 6.0f, 4.0f, 4.0f, white, greenRoof);
            for (float x : { -10.0f, 0.0f, 10.0f }) TownStupa(s, at.x + x, at.y - 16.0f, x == 0.0f ? 0.8f : 0.6f);
            TownOccupy(at, half, half);
            break;
        }
        case Landmark::Mosque:
        {
            if (!TownFindArea(s, gTown.site, 120.0f, 900.0f, 20.0f, clearings, at)) return;
            name = "mosque";
            const float g = s.terrain.HeightAt(at.x, at.y) - 0.3f;
            const Color wall = { 236, 232, 222, 255 }, dome = { 40, 120, 150, 255 };
            s.solid.Box({ at.x, g + 0.4f, at.y }, { 26.0f, 0.8f, 26.0f }, kConcrete);
            s.solid.Box({ at.x, g + 4.0f, at.y }, { 18.0f, 7.0f, 18.0f }, wall);
            s.solid.Box({ at.x, g + 7.2f, at.y }, { 18.4f, 0.6f, 18.4f }, dome);
            s.solid.Cylinder({ at.x, g + 7.5f, at.y }, 6.5f, 6.5f, 1.5f, 16, wall);
            s.solid.Ellipsoid({ at.x, g + 9.0f, at.y }, { 6.5f, 6.0f, 6.5f }, 6, 16, dome);
            s.solid.Cylinder({ at.x, g + 14.8f, at.y }, 0.12f, 0.12f, 2.0f, 6, gold);
            for (float x : { -1.0f, 1.0f })
            {
                // Хоёр минарет
                const float mx = at.x + x * 11.5f, mz = at.y - 8.0f;
                s.solid.Cylinder({ mx, g, mz }, 1.1f, 0.9f, 21.0f, 10, wall);
                s.solid.Cylinder({ mx, g + 16.0f, mz }, 1.5f, 1.5f, 0.6f, 10, dome);
                s.solid.Cylinder({ mx, g + 21.0f, mz }, 1.0f, 0.0f, 3.2f, 10, dome);
                s.Collider({ mx, g + 10.0f, mz }, { 2.2f, 20.0f, 2.2f });
            }
            for (float x : { -5.0f, 0.0f, 5.0f })
            {
                s.solid.Box({ at.x + x, g + 3.6f, at.y - 9.05f }, { 2.0f, 4.0f, 0.1f }, { 60, 90, 120, 255 }); // нуман цонх, хаалга
            }
            s.Collider({ at.x, g + 6.0f, at.y }, { 18.0f, 12.0f, 18.0f });
            TownOccupy(at, 14.0f, 14.0f);
            break;
        }
        case Landmark::Fortress:
        {
            if (!TownFindArea(s, gTown.site, 350.0f, 1800.0f, 60.0f, clearings, at)) return;
            name = "fortress ruins";
            // Шороон хэрмийн туурь: зарим хэсэг нурсан, урд, хойд талд хаалга, буланд цамхгийн туурь
            const Color earth = { 176, 150, 112, 255 };
            const float half = 56.0f;
            const Vector2 corners[4] = { { at.x - half, at.y - half }, { at.x + half, at.y - half }, { at.x + half, at.y + half }, { at.x - half, at.y + half } };
            for (int e = 0; e < 4; ++e)
            {
                const Vector2 a = corners[e], b = corners[(e + 1) % 4];
                const int pieces = 7;
                for (int k = 0; k < pieces; ++k)
                {
                    if ((e == 0 || e == 2) && k == pieces / 2) continue; // хаалга
                    if (s.rng.Chance(0.15f)) continue;                   // нурсан хэсэг
                    const Vector2 p0 = Vector2Lerp(a, b, (float)k / (float)pieces), p1 = Vector2Lerp(a, b, (float)(k + 1) / (float)pieces);
                    TownWall(s, p0, p1, s.rng.Range(2.8f, 4.5f), 3.0f, JitterColor(earth, s.rng, 0.05f), true);
                }
                const float g = s.terrain.HeightAt(a.x, a.y);
                s.solid.Box({ a.x, g + 2.2f, a.y }, { 6.0f, 5.4f, 6.0f }, ScaleColor(earth, 0.94f));
                s.Collider({ a.x, g + 2.2f, a.y }, { 6.0f, 5.4f, 6.0f });
            }
            TownOccupy(at, half + 3.0f, half + 3.0f);
            break;
        }
        case Landmark::HillPavilion:
        {
            // Хотын дэргэдэх хамгийн өндөр толгой
            float best = -1e9f;
            bool found = false;
            for (float r = 300.0f; r <= 1700.0f; r += 40.0f)
            {
                for (int k = 0; k < 36; ++k)
                {
                    const float a = 2.0f * PI * (float)k / 36.0f;
                    const Vector2 c = { gTown.site.x + std::cos(a) * r, gTown.site.y + std::sin(a) * r };
                    const float h = s.terrain.HeightAt(c.x, c.y);
                    if (h <= best || !TownFreeArea(s, c, 9.0f, clearings)) continue;
                    best = h;
                    at = c;
                    found = true;
                }
            }
            if (!found) return;
            name = "hill pavilion";
            const float g = s.terrain.HeightAt(at.x, at.y) - 0.3f;
            s.solid.Cylinder({ at.x, g, at.y }, 7.5f, 7.0f, 1.1f, 8, kConcrete);
            for (int k = 0; k < 8; ++k)
            {
                const float a = 2.0f * PI * ((float)k + 0.5f) / 8.0f;
                s.solid.Cylinder({ at.x + std::cos(a) * 5.4f, g + 1.1f, at.y + std::sin(a) * 5.4f }, 0.3f, 0.3f, 4.2f, 6, red);
            }
            s.solid.Cylinder({ at.x, g + 5.3f, at.y }, 7.4f, 7.4f, 0.5f, 8, red);
            s.solid.Cylinder({ at.x, g + 5.8f, at.y }, 7.8f, 2.6f, 2.4f, 8, greenRoof);
            s.solid.Cylinder({ at.x, g + 8.2f, at.y }, 2.8f, 0.2f, 2.2f, 8, greenRoof);
            s.solid.Cylinder({ at.x, g + 10.2f, at.y }, 0.3f, 0.05f, 1.6f, 6, gold);
            for (int k = 0; k < 8; ++k)
            {
                const float a = 2.0f * PI * ((float)k + 0.5f) / 8.0f;
                s.Collider({ at.x + std::cos(a) * 5.4f, g + 3.0f, at.y + std::sin(a) * 5.4f }, { 0.6f, 4.2f, 0.6f });
            }
            Ovoo(s, at.x + 14.0f, at.y + 6.0f);
            TownOccupy(at, 9.0f, 9.0f);
            break;
        }
        case Landmark::Industry:
        {
            if (!TownFindArea(s, gTown.site, 500.0f, 2000.0f, 45.0f, clearings, at)) return;
            name = "concentrator plant";
            const float g = s.terrain.HeightAt(at.x, at.y) - 0.5f;
            const Color steel = { 150, 152, 150, 255 }, rust = { 140, 96, 66, 255 };
            s.solid.Box({ at.x, g + 9.0f, at.y }, { 60.0f, 18.0f, 26.0f }, steel);          // гол цех
            s.solid.Box({ at.x, g + 18.6f, at.y }, { 60.4f, 1.2f, 26.4f }, ScaleColor(steel, 0.8f));
            s.solid.Box({ at.x - 18.0f, g + 14.0f, at.y + 22.0f }, { 20.0f, 28.0f, 16.0f }, ScaleColor(steel, 0.9f));
            for (int k = 0; k < 3; ++k) s.solid.Cylinder({ at.x + 10.0f + (float)k * 9.0f, g, at.y + 24.0f }, 3.6f, 3.6f, 16.0f, 12, rust); // силос
            for (float x : { -26.0f, -16.0f })
            {
                // Улаан цагаан судалтай яндан
                for (int band = 0; band < 6; ++band)
                {
                    s.solid.Cylinder({ at.x + x, g + (float)band * 8.0f, at.y - 20.0f }, 2.4f - 0.2f * (float)band, 2.2f - 0.2f * (float)band, 8.0f, 12,
                                     band % 2 ? Color{ 232, 230, 224, 255 } : Color{ 178, 44, 40, 255 });
                }
                s.Collider({ at.x + x, g + 24.0f, at.y - 20.0f }, { 4.6f, 48.0f, 4.6f });
            }
            // Ташуу туузан дамжуулга: доод үзүүр нь газарт (36 м урт, 0.35 радиан налуу -> 12 м өргөгдөнө)
            s.solid.SetTransform(MatrixMultiply(MatrixRotateX(-0.35f), MatrixTranslate(at.x + 20.0f, g + 6.6f, at.y - 22.0f)));
            s.solid.Box({ 0.0f, 0.0f, 0.0f }, { 3.0f, 2.6f, 36.0f }, ScaleColor(steel, 0.75f));
            s.solid.ResetTransform();
            s.Collider({ at.x, g + 9.0f, at.y }, { 60.0f, 18.0f, 26.0f });
            s.Collider({ at.x - 18.0f, g + 14.0f, at.y + 22.0f }, { 20.0f, 28.0f, 16.0f });
            TownOccupy(at, 40.0f, 40.0f);
            break;
        }
        case Landmark::Stupas:
        {
            if (!TownFindArea(s, gTown.site, 100.0f, 900.0f, 22.0f, clearings, at)) return;
            name = "stupas";
            TownStupa(s, at.x, at.y, 1.3f);
            for (int k = 0; k < 8; ++k)
            {
                const float a = 2.0f * PI * (float)k / 8.0f;
                TownStupa(s, at.x + std::cos(a) * 15.0f, at.y + std::sin(a) * 15.0f, 0.42f);
            }
            TownOccupy(at, 19.0f, 19.0f);
            break;
        }
        case Landmark::Station:
        {
            // Төвд хамгийн ойр төмөр зам; түүнтэй зэрэгцээ, 16 м-т өртөө
            float best = 1e9f;
            Vector2 rail = gTown.site, along = { 1.0f, 0.0f };
            for (const CityLine& line : city.lines)
            {
                if (line.kind != CityLineKind::Railway) continue;
                for (size_t i = 0; i + 1 < line.points.size(); ++i)
                {
                    const Vector2 q = Vector2Lerp(line.points[i], line.points[i + 1], 0.5f);
                    const float d = Vector2Distance(q, gTown.site);
                    if (d < best && Vector2Distance(line.points[i], line.points[i + 1]) > 1.0f)
                    {
                        best = d;
                        rail = q;
                        along = Vector2Normalize(Vector2Subtract(line.points[i + 1], line.points[i]));
                    }
                }
            }
            if (best > 3000.0f) return;
            Vector2 side = { -along.y, along.x };
            if (Vector2DotProduct(side, Vector2Subtract(gTown.site, rail)) < 0.0f) side = Vector2Scale(side, -1.0f);
            bool found = false;
            for (float slide = 0.0f; slide < 400.0f && !found; slide += 20.0f)
            {
                for (float sign : { 1.0f, -1.0f })
                {
                    const Vector2 c = Vector2Add(Vector2Add(rail, Vector2Scale(along, slide * sign)), Vector2Scale(side, 18.0f));
                    if (TownFreeArea(s, c, 14.0f, clearings))
                    {
                        at = c;
                        found = true;
                        break;
                    }
                }
            }
            if (!found) return;
            name = "railway station";
            Plan f;
            f.center = at;
            f.axis = along;
            f.halfLong = 22.0f;
            f.halfShort = 6.0f;
            const std::vector<Vector2> outline = { f.At(-22.0f, -6.0f), f.At(22.0f, -6.0f), f.At(22.0f, 6.0f), f.At(-22.0f, 6.0f) };
            CityBuilding hall;
            hall.type = CityBuildingType::Government;
            hall.levels = 2;
            hall.height = 8.0f;
            hall.outline = outline;
            TownBuilding(s, hall);
            // Цагны цамхаг ба тавцан
            const float g = s.terrain.HeightAt(at.x, at.y);
            s.solid.Box({ at.x, g + 9.0f, at.y }, { 4.0f, 18.0f, 4.0f }, gTown.publicWalls[0]);
            s.solid.Cylinder({ at.x, g + 18.0f, at.y }, 2.6f, 0.2f, 3.0f, 4, greenRoof);
            for (float y : { -1.0f, 1.0f }) s.solid.Box({ at.x + side.x * 2.05f * y, g + 15.0f, at.y + side.y * 2.05f * y }, { 1.6f, 1.6f, 1.6f }, white); // цаг
            const Vector2 platform = Vector2Add(at, Vector2Scale(side, -10.0f));
            TownPaving(s, platform, along, 40.0f, 3.0f, kConcrete);
            s.Collider({ at.x, g + 9.0f, at.y }, { 4.0f, 18.0f, 4.0f });
            break;
        }
        case Landmark::Memorial:
        {
            if (!TownFindArea(s, gTown.site, 90.0f, 800.0f, 16.0f, clearings, at)) return;
            name = "memorial";
            TownPaving(s, at, { 0.0f, 1.0f }, 16.0f, 16.0f, kPaving);
            const float g = s.terrain.HeightAt(at.x, at.y);
            s.solid.Box({ at.x, g + 0.5f, at.y }, { 9.0f, 1.0f, 9.0f }, kGranite);
            s.solid.Box({ at.x, g + 1.4f, at.y }, { 6.0f, 0.8f, 6.0f }, ScaleColor(kGranite, 0.85f));
            s.solid.Cylinder({ at.x, g + 1.8f, at.y }, 1.6f, 0.7f, 20.0f, 4, { 220, 218, 210, 255 }); // багана
            s.solid.Ellipsoid({ at.x, g + 22.4f, at.y }, { 0.9f, 0.9f, 0.9f }, 3, 5, { 196, 40, 36, 255 });  // од
            s.Collider({ at.x, g + 1.0f, at.y }, { 9.0f, 2.0f, 9.0f });
            s.Collider({ at.x, g + 11.0f, at.y }, { 2.6f, 20.0f, 2.6f });
            if (gTown.province == 21)
            {
                // Дорнод: тавцан дээрх танк
                const Vector2 t = { at.x + 11.0f, at.y };
                const Color tank = { 70, 84, 64, 255 };
                s.solid.Box({ t.x, g + 1.0f, t.y }, { 4.0f, 2.0f, 7.5f }, kGranite);
                s.solid.Box({ t.x, g + 2.6f, t.y }, { 3.2f, 1.2f, 6.4f }, tank);
                s.solid.Box({ t.x, g + 3.6f, t.y - 0.4f }, { 2.2f, 0.9f, 2.6f }, tank);
                s.solid.SetTransform(MatrixMultiply(MatrixRotateX(-0.08f), MatrixTranslate(t.x, g + 3.7f, t.y + 2.4f)));
                s.solid.Box({ 0.0f, 0.0f, 0.0f }, { 0.22f, 0.22f, 4.4f }, ScaleColor(tank, 0.8f));
                s.solid.ResetTransform();
                s.Collider({ t.x, g + 2.0f, t.y }, { 4.0f, 4.0f, 7.5f });
            }
            TownOccupy(at, 16.0f, 16.0f);
            break;
        }
        case Landmark::Chinggis:
        {
            if (!TownFindArea(s, gTown.site, 90.0f, 900.0f, 18.0f, clearings, at)) return;
            name = "Chinggis Khaan statue";
            TownPaving(s, at, { 0.0f, 1.0f }, 18.0f, 18.0f, kPaving);
            const float g = s.terrain.HeightAt(at.x, at.y);
            s.solid.Box({ at.x, g + 0.6f, at.y }, { 12.0f, 1.2f, 12.0f }, kGranite);
            s.solid.Box({ at.x, g + 4.2f, at.y }, { 6.0f, 6.0f, 9.0f }, ScaleColor(kGranite, 0.9f));
            s.Collider({ at.x, g + 3.6f, at.y }, { 12.0f, 7.2f, 12.0f });
            HorseRider(s, at.x, g + 7.2f, at.y, PI, 3.2f);
            s.Collider({ at.x, g + 12.0f, at.y }, { 3.0f, 10.0f, 7.0f });
            TownOccupy(at, 18.0f, 18.0f);
            break;
        }
        case Landmark::None: break;
        }
        TraceLog(LOG_INFO, "TOWN: landmark %s at (%.0f, %.0f)", name, at.x, at.y);
    }

    // Хот руу орох их зам: Монгол хээтэй хаалга, хажууд нь угтах самбар, ойролцоох толгод дээр овоо.
    void TownEntrances(Scene& s, const CityData& city)
    {
        std::vector<Vector2> gates;
        for (const CityRoad& road : city.roads)
        {
            if (road.klass > CityRoadClass::Secondary || road.dirt || gates.size() >= 3) continue;
            const std::vector<Vector2> path = Resample(road.points, 8.0f);
            for (size_t i = 0; i + 1 < path.size() && gates.size() < 3; ++i)
            {
                const float b0 = s.layout.CityBuiltDistance(path[i].x, path[i].y), b1 = s.layout.CityBuiltDistance(path[i + 1].x, path[i + 1].y);
                if ((b0 < 140.0f) == (b1 < 140.0f)) continue;
                const Vector2 q = b0 >= 140.0f ? path[i] : path[i + 1]; // хотын гадна тал
                if (std::fabs(q.x) > kCityHalfExtent - 60.0f || std::fabs(q.y) > kCityHalfExtent - 60.0f) continue;
                bool apart = true;
                for (const Vector2& g : gates) apart = apart && Vector2Distance(g, q) > 500.0f;
                if (!apart) continue;
                gates.push_back(q);

                const Vector2 along = Vector2Normalize(Vector2Subtract(path[i + 1], path[i]));
                const Vector2 side = { -along.y, along.x };
                const float span = road.halfWidth + 1.8f;
                const Color white = { 238, 236, 228, 255 }, blue = { 30, 80, 170, 255 }, red = { 180, 40, 36, 255 }, gold = { 206, 168, 72, 255 };
                float ground = 1e9f;
                for (float k : { -1.0f, 1.0f }) ground = std::min(ground, s.terrain.HeightAt(q.x + side.x * span * k, q.y + side.y * span * k));
                ground -= 0.3f;
                const float yaw = std::atan2(side.x, side.y);
                s.solid.SetTransform(MatrixMultiply(MatrixRotateY(yaw), MatrixTranslate(q.x, ground, q.y)));
                for (float k : { -1.0f, 1.0f })
                {
                    s.solid.Box({ 0.0f, 3.8f, k * span }, { 1.4f, 7.6f, 1.4f }, white);           // багана
                    s.solid.Box({ 0.0f, 2.2f, k * span }, { 1.5f, 0.5f, 1.5f }, blue);
                }
                s.solid.Box({ 0.0f, 8.0f, 0.0f }, { 1.6f, 1.4f, span * 2.0f + 1.4f }, white);    // хөндлөн дам нуруу
                s.solid.Box({ 0.0f, 8.0f, 0.0f }, { 1.7f, 0.5f, span * 2.0f + 1.0f }, blue);      // хээтэй тууз
                s.solid.Box({ 0.0f, 8.85f, 0.0f }, { 2.4f, 0.3f, span * 2.0f + 2.4f }, red);
                s.solid.ResetTransform();
                Plan roof;
                roof.center = q;
                roof.axis = side;
                roof.halfLong = span + 0.8f;
                roof.halfShort = 1.0f;
                TownHipRoof(s, roof, ground + 9.0f, 1.3f, 0.5f, { 52, 120, 84, 255 });
                s.solid.Cylinder({ q.x, ground + 10.3f, q.y }, 0.4f, 0.1f, 1.4f, 8, gold);
                for (float k : { -1.0f, 1.0f })
                {
                    const Vector2 c = Vector2Add(q, Vector2Scale(side, span * k));
                    s.Collider({ c.x, ground + 3.8f, c.y }, { 1.4f, 7.6f, 1.4f });
                }
                // Угтах самбар: замын хажууд, хотоос гарах зүгт (хөх самбар, цагаан хүрээ)
                const Vector2 inward = s.layout.CityBuiltDistance(path[i + 1].x, path[i + 1].y) < b0 ? along : Vector2Scale(along, -1.0f);
                const Vector2 board = Vector2Add(Vector2Add(q, Vector2Scale(inward, -30.0f)), Vector2Scale(side, span + 3.5f));
                const float bg = s.terrain.HeightAt(board.x, board.y);
                s.solid.SetTransform(MatrixMultiply(MatrixRotateY(std::atan2(inward.x, inward.y)), MatrixTranslate(board.x, bg, board.y)));
                for (float x : { -1.8f, 1.8f }) s.solid.Box({ x, 1.4f, 0.0f }, { 0.16f, 2.8f, 0.16f }, kMetal);
                s.solid.Box({ 0.0f, 3.2f, 0.0f }, { 4.8f, 2.0f, 0.14f }, white);
                s.solid.Box({ 0.0f, 3.2f, -0.03f }, { 4.4f, 1.6f, 0.14f }, blue);
                s.solid.Box({ 0.0f, 3.6f, -0.08f }, { 3.4f, 0.22f, 0.06f }, white); // бичгийн мөр
                s.solid.Box({ 0.0f, 3.0f, -0.08f }, { 2.6f, 0.18f, 0.06f }, white);
                s.solid.ResetTransform();

                // Овоо: хаалганаас 80-500 м-т хамгийн өндөр, замаас хол газар
                Vector2 ovoo = Vector2Add(q, Vector2Scale(side, span + 20.0f));
                float best = s.terrain.HeightAt(ovoo.x, ovoo.y) + 2.0f; // тод толгод олдохгүй бол замын хажууд
                for (float r = 80.0f; r <= 500.0f; r += 30.0f)
                {
                    for (int k = 0; k < 24; ++k)
                    {
                        const float a = 2.0f * PI * (float)k / 24.0f;
                        const Vector2 c = { q.x + std::cos(a) * r, q.y + std::sin(a) * r };
                        const float h = s.terrain.HeightAt(c.x, c.y);
                        if (h <= best || s.layout.RoadDistance(c.x, c.y) < 12.0f || s.layout.CityBuiltDistance(c.x, c.y) < 60.0f ||
                            gCityMask.Blocked(c.x, c.y, 4.0f) || std::fabs(c.x) > kCityHalfExtent - 20.0f || std::fabs(c.y) > kCityHalfExtent - 20.0f)
                        {
                            continue;
                        }
                        best = h;
                        ovoo = c;
                    }
                }
                Ovoo(s, ovoo.x, ovoo.y);
                TownOccupy(ovoo, 4.0f, 4.0f);
                TraceLog(LOG_INFO, "TOWN: entrance gate at (%.0f, %.0f), ovoo at (%.0f, %.0f)", q.x, q.y, ovoo.x, ovoo.y);
            }
        }
    }

    // Гол, горхи (ус) ба төмөр зам (хайрга, хоёр төмөр)
    void CityLineMesh(Scene& s, const CityLine& line)
    {
        const bool fence = line.kind == CityLineKind::Fence || line.kind == CityLineKind::Wall;
        if (fence && !s.layout.IsCapital())
        {
            TownFence(s, line.points, line.kind == CityLineKind::Wall);
            return;
        }
        const std::vector<Vector2> path = fence ? line.points : Resample(line.points, 6.0f);
        const int count = (int)path.size();
        const std::vector<bool> gates = fence ? FenceGates(s, path) : std::vector<bool>();
        std::vector<Vector2> side(count);
        for (int i = 0; i < count; ++i)
        {
            const Vector2 tangent = Vector2Normalize(Vector2Subtract(path[std::min(i + 1, count - 1)], path[std::max(i - 1, 0)]));
            side[i] = { -tangent.y, tangent.x };
        }
        auto at = [&](int i, float offset) { return Vector2{ path[i].x + side[i].x * offset, path[i].y + side[i].y * offset }; };
        for (int i = 0; i + 1 < count; ++i)
        {
            if (line.kind == CityLineKind::Fence || line.kind == CityLineKind::Wall)
            {
                // Хашаа: газрын налууг дагасан банз (саарал, хүрэн) эсвэл чулуун хана. Хүн, зомби давж гарна
                // (World::FindVault), дундуур нь нэвтрэхгүй; хаалгаар (FenceGates) орж гарна.
                const bool wall = line.kind == CityLineKind::Wall;
                const float height = wall ? 2.0f : 1.7f;
                const Color color = wall ? JitterColor({ 150, 144, 132, 255 }, s.rng, 0.05f)
                                         : JitterColor(s.rng.Chance(0.5f) ? kWood : kWoodGrey, s.rng, 0.08f);
                auto piece = [&](Vector2 from, Vector2 to) {
                    const int pieces = std::max(1, (int)std::round(Vector2Distance(from, to) / 2.2f));
                    for (int k = 0; k < pieces; ++k)
                    {
                        const Vector2 a = Vector2Lerp(from, to, (float)k / (float)pieces), b = Vector2Lerp(from, to, (float)(k + 1) / (float)pieces);
                        const float ha = s.terrain.HeightAt(a.x, a.y), hb = s.terrain.HeightAt(b.x, b.y);
                        s.BeginPanel();
                        s.solid.Quad({ a.x, ha - 0.1f, a.y }, { b.x, hb - 0.1f, b.y }, { b.x, hb + height, b.y }, { a.x, ha + height, a.y }, color);
                        s.walls.push_back({ a, b, std::min(ha, hb) - 0.1f, std::max(ha, hb) + height, 0.08f, true });
                        s.EndPanel(a, b, height, color, wall ? 7.0f : 3.0f, (int)s.walls.size() - 1, -1);
                    }
                };
                const Vector2 a = path[i], b = path[i + 1];
                if (!gates[(size_t)i])
                {
                    piece(a, b);
                    continue;
                }
                Vector2 g0, g1;
                FenceGate(s, a, b, height, wall ? Color{ 70, 80, 90, 255 } : ScaleColor(color, 0.8f), g0, g1);
                piece(a, g0);
                piece(g1, b);
                for (Vector2 q : { g0, g1 })
                {
                    const float ground = s.terrain.HeightAt(q.x, q.y) - 0.1f;
                    s.solid.Box({ q.x, ground + (height + 0.3f) * 0.5f, q.y }, { 0.22f, height + 0.3f, 0.22f }, ScaleColor(color, 0.7f));
                }
                continue;
            }
            if (line.kind == CityLineKind::Railway)
            {
                auto g = [&](int k, float offset, float lift) {
                    const Vector2 q = at(k, offset);
                    return Vector3{ q.x, s.terrain.HeightAt(q.x, q.y) + lift, q.y };
                };
                s.solid.Quad(g(i, -2.0f, 0.09f), g(i + 1, -2.0f, 0.09f), g(i + 1, 2.0f, 0.09f), g(i, 2.0f, 0.09f), kGravel);
                for (float rail : { -0.72f, 0.72f })
                {
                    s.solid.Quad(g(i, rail - 0.05f, 0.24f), g(i + 1, rail - 0.05f, 0.24f), g(i + 1, rail + 0.05f, 0.24f),
                                 g(i, rail + 0.05f, 0.24f), kMetal);
                }
                continue;
            }
            const float hw = line.halfWidth;
            const Vector2 a0 = at(i, -hw), a1 = at(i, hw), b0 = at(i + 1, -hw), b1 = at(i + 1, hw);
            s.glass.Quad(CityWaterPoint(s, a0.x, a0.y), CityWaterPoint(s, b0.x, b0.y), CityWaterPoint(s, b1.x, b1.y),
                         CityWaterPoint(s, a1.x, a1.y), { 70, 112, 140, 190 });
        }
    }

    // Хотын тогтмол хэсэг: гудамж, барилга, ус, төмөр зам, мод, гэрэлтүүлэг.
    // Гэр хороолол (хашаа, гэр, хувийн байшин) хот бүрт хэдэн мянгаараа байж зураглалыг удаашруулдаг тул
    // хасагдсан: зөвхөн төвийн голлох барилгууд үлдэнэ. Гэр хороолол дотор зөвхөн олон нийтийн барилга
    // (сургууль, засаг захиргаа, сүм хийд, орон сууц, дэлгүүр).
    bool KeepCityBuilding(const CityBuilding& b, const Plan& f)
    {
        switch (b.type)
        {
        case CityBuildingType::Ger:
        case CityBuildingType::House:
        case CityBuildingType::Garage: return false;
        default: break;
        }
        if (f.area < 90.0f) return false; // жижиг байшин, саравч, амбаар
        if (!gTownZones.Khashaa(f.center.x, f.center.y)) return true;
        switch (b.type)
        {
        case CityBuildingType::School:
        case CityBuildingType::Government:
        case CityBuildingType::Religious:
        case CityBuildingType::Apartments:
        case CityBuildingType::Commercial: return true;
        default: return false;
        }
    }

    bool NearBulganComplex(const Mountain& m, float x, float z);

    void BuildCity(Scene& s, const CityData& city)
    {
        const bool town = !s.layout.IsCapital(); // аймгийн төв (Улаанбаатарт талбай, дурсгалт газар нэмэхгүй)
        gCityMask.Reset();
        gFenceMask.Reset();
        gTownZones.Reset();
        gTown = MakeTownStyle(city.province);
        gTown.site = CityTownSite(city);
        // Гэр хороолол: нүд бүрийн хашааны урт, гэр байшингийн тоо (Улаанбаатарт хашаа тэмдэглэгдээгүй тул гэр, байшингаар)
        for (const CityLine& line : city.lines)
        {
            if (line.kind != CityLineKind::Fence && line.kind != CityLineKind::Wall) continue;
            for (size_t i = 0; i + 1 < line.points.size(); ++i)
            {
                const Vector2 mid = Vector2Lerp(line.points[i], line.points[i + 1], 0.5f);
                gTownZones.fence[(size_t)gTownZones.Index(mid.x, mid.y)] += Vector2Distance(line.points[i], line.points[i + 1]);
            }
        }
        for (const CityBuilding& b : city.buildings)
        {
            if (b.outline.size() < 3 || (b.type != CityBuildingType::Ger && b.type != CityBuildingType::House)) continue;
            const Vector2 c = b.outline[0];
            ++gTownZones.homes[(size_t)gTownZones.Index(c.x, c.y)];
        }
        if (town)
        {
            // Засаг даргын тамгын газар: талбайд хамгийн ойр засаг захиргааны барилга (эсвэл 200 м доторх том барилга)
            float best = 400.0f;
            for (const CityBuilding& b : city.buildings)
            {
                if (b.outline.size() < 3) continue;
                const Plan f = MeasureFootprint(b.outline);
                if (!KeepCityBuilding(b, f)) continue;
                const float d = Vector2Distance(f.center, gTown.site);
                const bool candidate = b.type == CityBuildingType::Government ||
                                       ((b.type == CityBuildingType::Generic || b.type == CityBuildingType::Commercial) && f.area > 300.0f && d < 200.0f);
                if (!candidate) continue;
                const float score = d + (b.type == CityBuildingType::Government ? 0.0f : 120.0f);
                if (score < best)
                {
                    best = score;
                    gTown.mainHall = &b;
                }
            }
        }
        for (const CityRoad& road : city.roads)
        {
            CityStreet(s, road);
        }
        const int streetTriangles = s.solid.TriangleCount();

        int built = 0;
        const std::vector<CityClearing> clearings = CityClearings(city);
        int skipped = 0;
        for (const CityBuilding& building : city.buildings)
        {
            if (building.outline.size() < 3 || InCityClearing(clearings, building.outline)) continue;
            if (!KeepCityBuilding(building, MeasureFootprint(building.outline)))
            {
                ++skipped;
                continue;
            }
            CityBuildingMesh(s, building);
            ++built;
        }
        const int buildingTriangles = s.solid.TriangleCount() - streetTriangles;

        for (const CityArea& area : city.areas)
        {
            if (area.kind == CityAreaKind::Water) CityWaterPolygon(s, area.outline);
            else if (area.kind == CityAreaKind::Plaza) PavePolygon(s, area.outline, 0.04f, kPaving, 0.03f);
        }
        for (const CityLine& line : city.lines)
        {
            const bool fence = line.kind == CityLineKind::Fence || line.kind == CityLineKind::Wall;
            if (fence && InCityClearing(clearings, line.points)) continue;
            if (fence && !line.points.empty())
            {
                // Гэр хорооллын хашаа хасагдсан (төвийн байгууллагын хашаа, хана үлдэнэ)
                const Vector2 mid = line.points[line.points.size() / 2];
                if (gTownZones.Khashaa(mid.x, mid.y)) continue;
            }
            CityLineMesh(s, line);
        }

        if (town)
        {
            const int beforeTown = s.solid.TriangleCount();
            TownSquare(s, city);
            TownLandmark(s, city, clearings);
            TownEntrances(s, city);
            const Vector2 hall = gTown.mainHall ? MeasureFootprint(gTown.mainHall->outline).center : Vector2{ 0.0f, 0.0f };
            TraceLog(LOG_INFO, "TOWN: province %d (region %d): main hall %s (%.0f, %.0f), %d triangles",
                     city.province, (int)gTown.region, gTown.mainHall ? "found" : "none", hall.x, hall.y, s.solid.TriangleCount() - beforeTown);
        }

        // Гудамжны мод ба гэрэлтүүлэг: том замуудын хоёр талаар
        Rng rng(4242u);
        int trees = 0, lamps = 0;
        // Булган уулын цогцолборын шат, хаалганы дэргэд гудамжны мод тавихгүй
        auto freeSpot = [&](float x, float z, float radius) {
            for (const Mountain& m : s.layout.Mountains())
            {
                if (NearBulganComplex(m, x, z)) return false;
            }
            return std::fabs(x) < kCityHalfExtent - 5.0f && std::fabs(z) < kCityHalfExtent - 5.0f && !gCityMask.Blocked(x, z, radius) &&
                   s.layout.RoadDistance(x, z) > 0.6f && s.layout.CityGround(x, z) != 6;
        };
        for (const CityRoad& road : city.roads)
        {
            if ((int)road.klass > (int)CityRoadClass::Tertiary || road.dirt) continue;
            // Аймгийн төвд: гудамж бүхэлдээ модтой эсвэл модгүй, мод нь ижил зайтай, ижил өндөртэй эгнээ
            const bool lined = !town || rng.Chance(std::min(1.0f, gTown.treeChance * 1.3f));
            const float rowHeight = rng.Range(6.5f, 9.5f);
            const std::vector<Vector2> path = Resample(road.points, town ? 10.0f : 13.0f);
            for (size_t i = 0; i + 1 < path.size(); ++i)
            {
                const Vector2 tangent = Vector2Normalize(Vector2Subtract(path[i + 1], path[i]));
                const Vector2 normal = { -tangent.y, tangent.x };
                for (float sideSign : { -1.0f, 1.0f })
                {
                    const float offset = road.halfWidth + 3.2f;
                    const float x = path[i].x + normal.x * offset * sideSign, z = path[i].y + normal.y * offset * sideSign;
                    const bool lampRoad = road.klass <= (town ? CityRoadClass::Tertiary : CityRoadClass::Primary);
                    if (lampRoad && i % 3 == 0 && sideSign > 0.0f)
                    {
                        const float lx = path[i].x + normal.x * (road.halfWidth + 1.2f), lz = path[i].y + normal.y * (road.halfWidth + 1.2f);
                        if (freeSpot(lx, lz, 0.6f))
                        {
                            CityLamp(s, lx, lz, Vector2Scale(normal, -1.0f));
                            ++lamps;
                        }
                        continue;
                    }
                    if (town)
                    {
                        if (!lined || !freeSpot(x, z, 1.6f)) continue;
                        CityTree(s, x, z, rowHeight + rng.Range(-0.4f, 0.4f));
                        ++trees;
                        continue;
                    }
                    if (!rng.Chance(0.4f) || !freeSpot(x, z, 1.6f)) continue;
                    CityTree(s, x + rng.Range(-1.5f, 1.5f), z, rng.Range(6.0f, 10.0f));
                    ++trees;
                }
            }
        }
        // Цэцэрлэгт хүрээлэн, ой: талбайгаас хамаарч шигүү
        for (const CityArea& area : city.areas)
        {
            if (area.kind != CityAreaKind::Park && area.kind != CityAreaKind::Forest && area.kind != CityAreaKind::Cemetery) continue;
            float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
            for (const Vector2& p : area.outline)
            {
                minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
                minZ = std::min(minZ, p.y); maxZ = std::max(maxZ, p.y);
            }
            const float density = area.kind == CityAreaKind::Forest ? 1.0f / 110.0f : 1.0f / 280.0f;
            if (town && area.kind != CityAreaKind::Forest)
            {
                // Аймгийн цэцэрлэгт хүрээлэн: 8 м зайтай эгнээ мод (бага зэрэг зөрүүтэй)
                for (float z = minZ + 4.0f; z < maxZ && trees < 4200; z += 8.0f)
                {
                    for (float x = minX + 4.0f; x < maxX && trees < 4200; x += 8.0f)
                    {
                        const Vector2 q = { x + rng.Range(-0.6f, 0.6f), z + rng.Range(-0.6f, 0.6f) };
                        if (!rng.Chance(gTown.treeChance + 0.2f) || !PointInPolygon(area.outline, q) || !freeSpot(q.x, q.y, 1.6f)) continue;
                        CityTree(s, q.x, q.y, rng.Range(6.5f, 8.5f));
                        ++trees;
                    }
                }
                continue;
            }
            const int wanted = std::min(400, (int)((maxX - minX) * (maxZ - minZ) * density));
            for (int k = 0; k < wanted && trees < 4200; ++k)
            {
                const Vector2 q = { rng.Range(minX, maxX), rng.Range(minZ, maxZ) };
                if (!PointInPolygon(area.outline, q) || !freeSpot(q.x, q.y, 1.6f)) continue;
                CityTree(s, q.x, q.y, rng.Range(6.0f, 11.0f));
                ++trees;
            }
        }
        // Гудамжны захад зогссон (орхигдсон) машинууд ба унаж болох машины байрлалууд
        auto inClearing = [&](float x, float z) {
            for (const CityClearing& c : clearings)
            {
                if (x > c.x0 - 4.0f && x < c.x1 + 4.0f && z > c.z0 - 4.0f && z < c.z1 + 4.0f) return true;
            }
            return false;
        };
        // Хотын хэмжээнээс хамаарах тоо: Улаанбаатарт 3200 зогссон, 110 унаж болох машин
        // Аймгийн төвд машин цөөн (барилгын тоо / 28): хэт олон машин гудамжийг замбараагүй харагдуулна
        const int parkedLimit = town ? std::clamp(built / 12, 40, 420) : std::min(3200, built / 3); // үлдсэн (төвийн) барилгаас
        const size_t vehicleLimit = (size_t)std::clamp((int)city.buildings.size() / 90, 30, 110);
        std::vector<Vector3> spots; // x, z, y = чиглэл
        auto addSpot = [&](float x, float z, float heading, float spacing) {
            if (std::fabs(x) > kCityHalfExtent - 20.0f || std::fabs(z) > kCityHalfExtent - 20.0f) return;
            if (gCityMask.Blocked(x, z, 2.6f) || s.layout.CityGround(x, z) == 6 || inClearing(x, z)) return;
            for (size_t k = spots.size() > 60 ? spots.size() - 60 : 0; k < spots.size(); ++k)
            {
                if (std::fabs(spots[k].x - x) < spacing && std::fabs(spots[k].z - z) < spacing) return;
            }
            spots.push_back({ x, heading, z });
        };
        for (const CityRoad& road : city.roads)
        {
            if (road.klass > CityRoadClass::Service || road.dirt) continue;
            // Том замд олон: хүмүүс зугтах үед орхисон машинууд
            const float chance = road.klass <= CityRoadClass::Primary ? 0.42f : road.klass <= CityRoadClass::Tertiary ? 0.32f
                               : road.klass == CityRoadClass::Residential ? 0.2f : 0.16f;
            const std::vector<Vector2> path = Resample(road.points, 9.0f);
            for (size_t i = 0; i + 1 < path.size(); ++i)
            {
                const Vector2 tangent = Vector2Normalize(Vector2Subtract(path[i + 1], path[i]));
                const Vector2 normal = { -tangent.y, tangent.x };
                const Vector2 mid = Vector2Lerp(path[i], path[i + 1], 0.5f);
                const float along = std::atan2(tangent.x, tangent.y);
                if (road.halfWidth >= 6.0f && rng.Chance(town ? 0.01f : 0.04f))
                {
                    // Түгжрэл: хэдэн эгнээгээр зогссон, хазгай эргэсэн машинууд
                    const int cars = rng.Int(4, 8);
                    for (int c = 0; c < cars; ++c)
                    {
                        const float lane = rng.Range(-road.halfWidth + 1.3f, road.halfWidth - 1.3f);
                        const float back = rng.Range(-9.0f, 9.0f);
                        addSpot(mid.x + normal.x * lane + tangent.x * back, mid.y + normal.y * lane + tangent.y * back,
                                along + (lane > 0.0f ? PI : 0.0f) + rng.Range(-0.5f, 0.5f), 3.2f);
                    }
                    continue;
                }
                if (!rng.Chance(chance)) continue;
                const float sideSign = rng.Chance(0.5f) ? 1.0f : -1.0f;
                // Өргөн замд захын эгнээнд, нарийн замд замын хажууд
                const float offset = road.halfWidth >= 3.4f ? road.halfWidth - 1.2f : road.halfWidth + 1.4f;
                // баруун гар талын хөдөлгөөн: зүүн талынх эсрэг зүг рүү харна
                addSpot(mid.x + normal.x * offset * sideSign, mid.y + normal.y * offset * sideSign,
                        along + (sideSign > 0.0f ? PI : 0.0f) + rng.Range(-0.08f, 0.08f), 6.0f);
            }
        }
        // Холилдуулж: нэг хэсэг нь орхигдсон машин, нөгөө нь унаж болох машин (хотын өнцөг булан бүрд)
        for (size_t i = spots.size(); i > 1; --i) std::swap(spots[i - 1], spots[(size_t)rng.Int(0, (int)i - 1)]);
        gCityVehicleSpots.clear();
        int parked = 0;
        for (size_t i = 0; i < spots.size(); ++i)
        {
            // (аймгийн төвд унаж болох машин олон: зогссон машин цөөн тул тэдний хоёр тутмын нэг)
            if (gCityVehicleSpots.size() < vehicleLimit && i % (town ? 2 : 5) == 0)
            {
                gCityVehicleSpots.push_back(spots[i]);
                continue;
            }
            if (parked >= parkedLimit) break;
            CityParkedCar(s, spots[i].x, spots[i].z, spots[i].y);
            ++parked;
        }
        TraceLog(LOG_INFO, "CITY: %d buildings (%d triangles, %d ger district buildings left out), streets %d triangles, %d trees, %d lamps, "
                 "%d parked cars, %d vehicle spots, %d triangles in all",
                 built, buildingTriangles, skipped, streetTriangles, trees, lamps, parked, (int)gCityVehicleSpots.size(), s.solid.TriangleCount());
    }

    // Хятад маягийн шар дээвэр (одоогийн SetTransform-ийн дотор): c = хормойн төв, halfW (X) x halfD (Z), rise өндөр
    // нуруу; хормойн булангууд дээшээ сөрвөлзөнө, доод тал нь улаан.
    void PagodaRoof(Scene& s, Vector3 c, float halfW, float halfD, float rise, Color color)
    {
        const Color under = { 150, 40, 34, 255 }, ridge = ScaleColor(color, 0.85f);
        const float lift = std::min(0.45f, rise * 0.4f);
        for (int layer = 0; layer < 2; ++layer)
        {
            const float dy = layer == 0 ? 0.0f : -0.22f;
            const Color face = layer == 0 ? color : under;
            const Vector3 rl = { c.x - halfW * 0.72f, c.y + rise + dy, c.z }, rr = { c.x + halfW * 0.72f, c.y + rise + dy, c.z };
            for (float side : { -1.0f, 1.0f })
            {
                const Vector3 el = { c.x - halfW, c.y + lift + dy, c.z + side * halfD };
                const Vector3 em = { c.x, c.y + dy, c.z + side * halfD };
                const Vector3 er = { c.x + halfW, c.y + lift + dy, c.z + side * halfD };
                s.solid.Triangle(rl, el, em, face);
                s.solid.Triangle(rl, em, rr, face);
                s.solid.Triangle(rr, em, er, face);
            }
            // Хажуугийн гурвалжин (hip)
            for (float side : { -1.0f, 1.0f })
            {
                const Vector3 r = side < 0.0f ? rl : rr;
                s.solid.Triangle(r, { c.x + side * halfW, c.y + lift + dy, c.z - halfD }, { c.x + side * halfW, c.y + lift + dy, c.z + halfD }, face);
            }
        }
        // Нурууны тагла ба үзүүрийн чимэглэл, хормойн улаан хүрээ
        s.solid.Box({ c.x, c.y + rise + 0.12f, c.z }, { halfW * 1.44f + 0.3f, 0.26f, 0.32f }, ridge);
        for (float side : { -1.0f, 1.0f })
        {
            s.solid.Box({ c.x + side * (halfW * 0.72f + 0.1f), c.y + rise + 0.4f, c.z }, { 0.22f, 0.5f, 0.3f }, ridge);
            s.solid.Box({ c.x, c.y + 0.02f, c.z + side * (halfD - 0.25f) }, { halfW * 1.7f, 0.16f, 0.12f }, ScaleColor(color, 0.8f));
        }
    }

    // Цагаан чулуун Будда: бадамлянхуан тавцан дээр зогссон, баруун гараа өргөсөн (айшгүй мудра). Орон нутгийн
    // хүрээнд (SetTransform) -Z тал руу харна; base = тавцангийн ёроол.
    void BuddhaStatue(Scene& s, float scale)
    {
        const Color stone = { 232, 230, 222, 255 }, shade = { 214, 210, 200, 255 };
        auto v = [&](float x, float y, float z) { return Vector3{ x * scale, y * scale, z * scale }; };
        // Тавцан: найман талт хоёр давхар, бадам цэцгийн дэлбээ
        s.solid.Cylinder(v(0.0f, -0.6f, 0.0f), 2.5f * scale, 2.5f * scale, 1.4f * scale, 8, shade);
        s.solid.Cylinder(v(0.0f, 0.8f, 0.0f), 2.0f * scale, 1.7f * scale, 1.0f * scale, 8, stone);
        for (int i = 0; i < 14; ++i)
        {
            const float a = (float)i / 14.0f * 2.0f * PI;
            s.solid.Ellipsoid(v(std::sin(a) * 1.75f, 2.0f, std::cos(a) * 1.75f), v(0.5f, 0.4f, 0.5f), 3, 6, stone);
        }
        s.solid.Cylinder(v(0.0f, 2.05f, 0.0f), 1.6f * scale, 1.5f * scale, 0.35f * scale, 12, shade);
        const float y0 = 2.4f;
        // Нөмрөг (доороо өргөн), цээж, мөр
        s.solid.Cylinder(v(0.0f, y0, 0.0f), 1.05f * scale, 0.78f * scale, 4.3f * scale, 14, stone);
        s.solid.Cylinder(v(0.0f, y0, 0.0f), 1.12f * scale, 1.08f * scale, 0.35f * scale, 14, shade); // хормой
        s.solid.Ellipsoid(v(0.0f, y0 + 4.55f, 0.0f), v(1.05f, 0.75f, 0.62f), 5, 12, stone);
        s.solid.Cylinder(v(0.0f, y0 + 5.05f, 0.0f), 0.3f * scale, 0.3f * scale, 0.45f * scale, 10, stone);
        // Толгой, ушниша (оройн товгор), чих
        s.solid.Ellipsoid(v(0.0f, y0 + 5.9f, 0.0f), v(0.46f, 0.56f, 0.5f), 6, 12, stone);
        s.solid.Ellipsoid(v(0.0f, y0 + 6.5f, 0.0f), v(0.27f, 0.24f, 0.27f), 4, 8, shade);
        for (float x : { -0.47f, 0.47f }) s.solid.Ellipsoid(v(x, y0 + 5.75f, 0.0f), v(0.08f, 0.3f, 0.13f), 3, 6, stone);
        // Баруун гар (статуйн): тохой бөхийлгөж, алгаа урагш өргөнө
        s.solid.Tube(v(-0.95f, y0 + 4.7f, 0.0f), v(-1.12f, y0 + 3.6f, -0.15f), 0.24f * scale, 0.2f * scale, 8, stone);
        s.solid.Tube(v(-1.12f, y0 + 3.6f, -0.15f), v(-1.02f, y0 + 4.75f, -0.62f), 0.2f * scale, 0.16f * scale, 8, stone);
        s.solid.Ellipsoid(v(-1.02f, y0 + 5.0f, -0.66f), v(0.17f, 0.3f, 0.07f), 3, 8, stone);
        // Зүүн гар: доош унжуулж, алгаа урагш
        s.solid.Tube(v(0.95f, y0 + 4.7f, 0.0f), v(1.12f, y0 + 3.6f, -0.1f), 0.24f * scale, 0.2f * scale, 8, stone);
        s.solid.Tube(v(1.12f, y0 + 3.6f, -0.1f), v(0.85f, y0 + 2.8f, -0.45f), 0.2f * scale, 0.16f * scale, 8, stone);
        s.solid.Ellipsoid(v(0.82f, y0 + 2.6f, -0.5f), v(0.16f, 0.28f, 0.08f), 3, 8, stone);
        // Нөмрөгийн нугалаа (урд талд)
        for (float x : { -0.45f, 0.0f, 0.45f }) s.solid.Box(v(x, y0 + 1.6f, -0.86f), v(0.06f, 2.6f, 0.08f), shade);
    }

    // Энгэр дээрх дугуй талбай: газрын налууг дагасан хавтан (явж гарахад ирмэггүй); захаараа намхан
    // цагаан хашлага, баганатай. Хашлагад шатны чигт (d ба -d) gap өнцгийн (радиан) нүх үлдээнэ (back = false бол
    // зөвхөн урд талд, шат дээшээ үргэлжлэхгүй).
    float RoundPlaza(Scene& s, Vector2 c, float radius, Vector2 d, float curb, float gap, bool back, Color paving)
    {
        auto at = [&](float r, float a) {
            const float x = c.x + std::sin(a) * r, z = c.y + std::cos(a) * r;
            return Vector3{ x, s.terrain.HeightAt(x, z) + 0.1f, z };
        };
        const int rings = std::max(2, (int)(radius / 3.0f));
        for (int k = 0; k < rings; ++k)
        {
            const float r0 = radius * (float)k / (float)rings, r1 = radius * (float)(k + 1) / (float)rings;
            const Color tile = k % 2 ? paving : ScaleColor(paving, 0.94f);
            for (int i = 0; i < 40; ++i)
            {
                const float a0 = (float)i / 40.0f * 2.0f * PI, a1 = (float)(i + 1) / 40.0f * 2.0f * PI;
                s.solid.Quad(at(r0, a0), at(r1, a0), at(r1, a1), at(r0, a1), tile);
            }
        }
        const float top = s.terrain.HeightAt(c.x, c.y) + 0.1f;
        const Color white = { 236, 234, 226, 255 };
        const float front = std::atan2(-d.x, -d.y), rear = std::atan2(d.x, d.y);
        const int segments = 40;
        for (int i = 0; i < segments; ++i)
        {
            const float a0 = (float)i / (float)segments * 2.0f * PI, a1 = (float)(i + 1) / (float)segments * 2.0f * PI;
            const float mid = (a0 + a1) * 0.5f;
            const bool open = std::fabs(std::remainder(mid - front, 2.0f * PI)) < gap ||
                              (back && std::fabs(std::remainder(mid - rear, 2.0f * PI)) < gap);
            if (open) continue;
            const Vector2 p0 = { c.x + std::sin(a0) * (radius - 0.2f), c.y + std::cos(a0) * (radius - 0.2f) };
            const Vector2 p1 = { c.x + std::sin(a1) * (radius - 0.2f), c.y + std::cos(a1) * (radius - 0.2f) };
            const Vector2 n = { std::sin(mid) * 0.12f, std::cos(mid) * 0.12f };
            const float h0 = s.terrain.HeightAt(p0.x, p0.y), h1 = s.terrain.HeightAt(p1.x, p1.y);
            // Хашлага: хоёр нүүртэй нимгэн хана, дээрээ тагтай (газрыг дагана)
            for (float side : { -1.0f, 1.0f })
            {
                const Vector2 q0 = Vector2Add(p0, Vector2Scale(n, side)), q1 = Vector2Add(p1, Vector2Scale(n, side));
                s.solid.Quad({ q0.x, h0 - 0.1f, q0.y }, { q1.x, h1 - 0.1f, q1.y }, { q1.x, h1 + curb, q1.y }, { q0.x, h0 + curb, q0.y }, white);
            }
            const Vector2 i0 = Vector2Subtract(p0, n), o0 = Vector2Add(p0, n), i1 = Vector2Subtract(p1, n), o1 = Vector2Add(p1, n);
            s.solid.Quad({ i0.x, h0 + curb, i0.y }, { i1.x, h1 + curb, i1.y }, { o1.x, h1 + curb, o1.y }, { o0.x, h0 + curb, o0.y },
                         { 222, 220, 212, 255 });
            if (i % 3 == 0) s.solid.Box({ p0.x, h0 + curb * 0.5f + 0.2f, p0.y }, { 0.35f, curb + 0.4f, 0.35f }, white);
            s.walls.push_back({ p0, p1, std::min(h0, h1) - 0.2f, std::max(h0, h1) + curb, 0.12f, true });
        }
        return top;
    }

    // Хүрэл тулга: гурван муруй хөл, хоёр цагираг (галын голомт)
    void BronzeTulga(Scene& s, Vector3 base)
    {
        const Color bronze = { 168, 112, 52, 255 }, dark = { 120, 80, 40, 255 };
        s.solid.Cylinder(base, 0.9f, 0.85f, 0.25f, 12, { 150, 146, 138, 255 }); // чулуун суурь
        const Vector3 b = { base.x, base.y + 0.25f, base.z };
        auto ring = [&](float y, float r, float thick) {
            for (int i = 0; i < 16; ++i)
            {
                const float a0 = (float)i / 16.0f * 2.0f * PI, a1 = (float)(i + 1) / 16.0f * 2.0f * PI;
                s.solid.Tube({ b.x + std::sin(a0) * r, b.y + y, b.z + std::cos(a0) * r }, { b.x + std::sin(a1) * r, b.y + y, b.z + std::cos(a1) * r },
                             thick, thick, 5, bronze, false);
            }
        };
        ring(1.05f, 0.5f, 0.06f);
        ring(0.55f, 0.42f, 0.045f);
        for (int k = 0; k < 3; ++k)
        {
            const float a = (float)k / 3.0f * 2.0f * PI;
            const Vector3 foot = { b.x + std::sin(a) * 0.62f, b.y, b.z + std::cos(a) * 0.62f };
            const Vector3 knee = { b.x + std::sin(a) * 0.4f, b.y + 0.55f, b.z + std::cos(a) * 0.4f };
            const Vector3 head = { b.x + std::sin(a) * 0.5f, b.y + 1.25f, b.z + std::cos(a) * 0.5f };
            s.solid.Tube(foot, knee, 0.07f, 0.06f, 6, dark);
            s.solid.Tube(knee, head, 0.06f, 0.07f, 6, bronze);
            s.solid.Ellipsoid(head, { 0.09f, 0.11f, 0.09f }, 3, 6, bronze); // толгойн чимэглэл
        }
        s.Collider({ base.x, base.y + 0.7f, base.z }, { 1.6f, 1.4f, 1.6f });
    }

    // Найман талт асар: улаан шар багана, ногоон шовх дээвэр
    void Pavilion(Scene& s, float x, float z)
    {
        const float ground = s.terrain.HeightAt(x, z);
        s.solid.Cylinder({ x, ground - 0.4f, z }, 2.4f, 2.4f, 0.65f, 8, kConcrete);
        for (int i = 0; i < 8; ++i)
        {
            const float a = (float)i / 8.0f * 2.0f * PI;
            s.solid.Box({ x + std::sin(a) * 2.0f, ground + 1.5f, z + std::cos(a) * 2.0f }, { 0.2f, 2.5f, 0.2f }, { 214, 120, 40, 255 });
        }
        s.solid.Cylinder({ x, ground + 2.65f, z }, 2.3f, 2.3f, 0.35f, 8, { 196, 60, 40, 255 });
        s.solid.Cylinder({ x, ground + 3.0f, z }, 2.9f, 0.2f, 1.6f, 8, { 52, 140, 80, 255 });
        s.solid.Cylinder({ x, ground + 4.55f, z }, 0.12f, 0.04f, 0.6f, 6, { 206, 168, 72, 255 });
        s.Collider({ x, ground + 1.5f, z }, { 0.6f, 3.0f, 0.6f });
    }

    // Булган уулын өврийн цогцолбор (Цэцэрлэг): шар дээвэртэй гурван хаалгатай үүд, хоёр эгнээ урт шат, шатны оройд
    // Зуу бурханы хийдийн яг урд цагаан Будда, хоёр талдаа бяцхан суварга.
    void BulganComplex(Scene& s, const Mountain& m)
    {
        const Color white = { 236, 234, 226, 255 }, stoneGrey = { 204, 200, 192, 255 }, red = { 160, 42, 36, 255 };
        const Color yellow = { 228, 186, 38, 255 }, blue = { 46, 112, 196, 255 }, teal = { 40, 120, 130, 255 };
        const Vector2 top = m.buddha, bottom = m.gate;
        TraceLog(LOG_INFO, "TOWN: Bulgan complex gate (%.0f, %.0f), rest (%.0f, %.0f), Buddha (%.0f, %.0f)", bottom.x, bottom.y, m.rest.x, m.rest.y, top.x, top.y);
        const float length = Vector2Distance(bottom, top);
        const Vector2 d = Vector2Scale(Vector2Subtract(top, bottom), 1.0f / length);
        const Vector2 right = { d.y, -d.x }; // орон нутгийн +X (MatrixRotateY(atan2(d.x, d.y)))
        const float yaw = std::atan2(d.x, d.y);
        auto world = [&](Vector2 origin, float x, float z) { return Vector2Add(origin, Vector2Add(Vector2Scale(right, x), Vector2Scale(d, z))); };

        // --- Үүдний талбай ба хаалга
        const Vector2 gate = world(bottom, 0.0f, -3.0f);
        TownPaving(s, world(bottom, 0.0f, -6.0f), d, 3.5f, 11.0f, { 170, 166, 158, 255 });
        float gateGround = s.terrain.HeightAt(gate.x, gate.y);
        for (float x : { -8.0f, 8.0f }) gateGround = std::min(gateGround, s.terrain.HeightAt(world(gate, x, 0.0f).x, world(gate, x, 0.0f).y));
        const float base = gateGround + 0.9f; // хаалганы тавцан (урдаа гишгүүртэй)
        s.solid.SetTransform(MatrixMultiply(MatrixRotateY(yaw), MatrixTranslate(gate.x, base, gate.y)));
        s.solid.Box({ 0.0f, -0.9f, 0.6f }, { 17.4f, 2.0f, 4.4f }, kConcrete); // тавцан
        for (int i = 0; i < 5; ++i) s.solid.Box({ 0.0f, -0.2f - 0.18f * (float)i, -1.9f - 0.42f * (float)i }, { 10.0f, 0.18f, 0.42f }, ScaleColor(kConcrete, 0.95f));
        // Гол хоёр багана: чулуун, урд хойд талдаа улаан самбартай
        for (float x : { -3.3f, 3.3f })
        {
            s.solid.Box({ x, 3.25f, 0.0f }, { 1.3f, 6.5f, 1.3f }, stoneGrey);
            for (float z : { -0.67f, 0.67f }) s.solid.Box({ x, 3.3f, z }, { 0.62f, 4.6f, 0.04f }, red);
            s.solid.Box({ x, 0.2f, 0.0f }, { 1.6f, 0.4f, 1.6f }, ScaleColor(stoneGrey, 0.9f));
        }
        // Хөндлөвч: улаан, дээрээ хөх ногоон хээтэй тууз, голдоо самбар
        s.solid.Box({ 0.0f, 6.25f, 0.0f }, { 8.4f, 0.75f, 1.0f }, red);
        s.solid.Box({ 0.0f, 6.78f, 0.0f }, { 8.2f, 0.3f, 1.06f }, teal);
        s.solid.Box({ 0.0f, 6.25f, -0.53f }, { 2.4f, 0.5f, 0.05f }, { 230, 226, 210, 255 });
        PagodaRoof(s, { 0.0f, 7.0f, 0.0f }, 5.6f, 2.3f, 2.0f, yellow);
        s.solid.Cylinder({ 0.0f, 9.2f, 0.0f }, 0.14f, 0.08f, 0.9f, 6, { 206, 168, 72, 255 }); // оройн гандирс
        // Хажуугийн хоёр жижиг хаалга: намхан багана, хөндлөвч, шар дээвэр
        for (float side : { -1.0f, 1.0f })
        {
            const float outer = side * 8.0f, mid = side * 5.65f;
            s.solid.Box({ outer, 1.9f, 0.0f }, { 0.9f, 3.8f, 0.9f }, stoneGrey);
            s.solid.Box({ outer, 2.0f, -0.46f }, { 0.4f, 2.6f, 0.04f }, red);
            s.solid.Box({ mid, 3.55f, 0.0f }, { 4.0f, 0.45f, 0.8f }, red);
            PagodaRoof(s, { mid, 3.95f, 0.0f }, 2.75f, 1.45f, 1.2f, yellow);
            // Хадаг ороосон арслан (хөх)
            const float lx = side * 3.3f;
            s.solid.Box({ lx, 0.35f, -1.4f }, { 0.8f, 0.7f, 0.8f }, stoneGrey);
            s.solid.Ellipsoid({ lx, 1.15f, -1.4f }, { 0.36f, 0.5f, 0.36f }, 4, 8, blue);
            s.solid.Ellipsoid({ lx, 1.75f, -1.45f }, { 0.28f, 0.28f, 0.28f }, 4, 8, { 120, 120, 116, 255 });
        }
        s.solid.ResetTransform();
        for (float x : { -8.0f, -3.3f, 3.3f, 8.0f })
        {
            const Vector2 q = world(gate, x, 0.0f);
            const float w = std::fabs(x) > 5.0f ? 0.9f : 1.3f;
            s.Collider({ q.x, base + 3.0f, q.y }, { w, 8.0f, w });
        }

        // --- Шат: хоёр эгнээ (дундуураа цагаан баганатай хуваагч), гадна талдаа цагаан хашлага. Буддагийн өмнөх
        // талбай хүртэл.
        const float kBuddhaPlaza = m.buddhaRadius, kRestPlaza = m.restRadius;
        const float stairsLength = std::max(10.0f, length - kBuddhaPlaza + 0.5f);
        const float restAt = Vector2Distance(bottom, m.rest);
        const int steps = (int)(stairsLength / 0.8f);
        for (int i = 0; i < steps; ++i)
        {
            const float along = ((float)i + 0.5f) * stairsLength / (float)steps;
            if (std::fabs(along - restAt) < kRestPlaza - 0.4f) continue;
            const Vector2 p = Vector2Add(bottom, Vector2Scale(d, along));
            const float ground = s.terrain.HeightAt(p.x, p.y);
            s.solid.SetTransform(MatrixMultiply(MatrixRotateY(yaw), MatrixTranslate(p.x, ground, p.y)));
            const Color step = i % 2 ? kConcrete : ScaleColor(kConcrete, 0.9f);
            for (float x : { -2.55f, 2.55f }) s.solid.Box({ x, -0.35f, 0.0f }, { 3.6f, 1.24f, 0.8f }, step); // газарт суулгасан (налуу дээр хөвөхгүй)
            s.solid.Box({ 0.0f, -0.25f, 0.0f }, { 1.4f, 1.38f, 0.82f }, ScaleColor(kConcrete, 0.8f)); // хуваагч
            if (i % 8 == 0) s.solid.Box({ 0.0f, 1.0f, 0.0f }, { 0.4f, 1.2f, 0.4f }, white);  // хуваагчийн багана
            if (i % 3 == 0)
            {
                for (float x : { -4.45f, 4.45f }) s.solid.Box({ x, 0.55f, 0.0f }, { 0.12f, 1.0f, 0.12f }, white); // хашлагын багана
            }
            s.solid.ResetTransform();
            if (i % 3 == 0)
            {
                for (float sx : { -4.45f, 4.45f })
                {
                    const Vector2 a = Vector2Add(p, Vector2Scale(right, sx));
                    const Vector2 b = Vector2Add(a, Vector2Scale(d, 3.0f * stairsLength / (float)steps));
                    const float ha = s.terrain.HeightAt(a.x, a.y) + 1.0f, hb = s.terrain.HeightAt(b.x, b.y) + 1.0f;
                    s.solid.Quad({ a.x, ha, a.y }, { b.x, hb, b.y }, { b.x, hb + 0.1f, b.y }, { a.x, ha + 0.1f, a.y }, white);
                }
            }
        }

        // --- Шатны дундах амрах талбай: энгийн дугуй, голд нь хүрэл тулга
        const Vector2 rest = m.rest;
        const float restTop = RoundPlaza(s, rest, kRestPlaza, d, 0.5f, 0.32f, true, { 180, 176, 168, 255 });
        BronzeTulga(s, { rest.x, restTop, rest.y });

        // --- Шатны орой: хийдийн урд дугуй талбай, голд нь өндөр тавцан дээр Будда (хот руу, шат руу харна);
        // захаараа цагаан хашлага, хоёр талдаа асар, гэрлийн шон
        const float topGround = RoundPlaza(s, top, kBuddhaPlaza, d, 0.8f, 0.2f, false, { 172, 168, 160, 255 });
        s.solid.SetTransform(MatrixMultiply(MatrixRotateY(yaw), MatrixTranslate(top.x, topGround, top.y)));
        s.solid.Box({ 0.0f, 0.35f, 0.0f }, { 6.0f, 0.7f, 6.0f }, { 200, 198, 192, 255 }); // дөрвөлжин тавцан
        s.solid.Box({ 0.0f, 0.9f, 0.0f }, { 4.8f, 0.4f, 4.8f }, { 214, 212, 206, 255 });
        s.solid.ResetTransform();
        s.solid.SetTransform(MatrixMultiply(MatrixRotateY(yaw), MatrixTranslate(top.x, topGround + 1.7f, top.y)));
        BuddhaStatue(s, 1.25f);
        s.solid.ResetTransform();
        s.Collider({ top.x, topGround + 5.5f, top.y }, { 6.0f, 11.0f, 6.0f });
        for (float x : { -18.0f, 18.0f })
        {
            const Vector2 q = world(top, x, 2.0f);
            Pavilion(s, q.x, q.y);
        }
        for (float a : { 0.7f, -0.7f, 2.2f, -2.2f })
        {
            // Хашлагын дотор талд, шатнаас хоёр тийш
            const Vector2 q = world(top, std::sin(a) * (kBuddhaPlaza - 1.2f), -std::cos(a) * (kBuddhaPlaza - 1.2f));
            CityLamp(s, q.x, q.y, Vector2Subtract(top, q));
        }
    }

    // Цогцолборын шат, талбай, сүмийн дэргэд мод, хад ургуулахгүй
    bool NearBulganComplex(const Mountain& m, float x, float z)
    {
        if (m.buddha.x > 1e8f) return false;
        const Vector2 a = m.gate, b = m.buddha, q = { x, z };
        const Vector2 ab = Vector2Subtract(b, a);
        const float t = Clamp(Vector2DotProduct(Vector2Subtract(q, a), ab) / Vector2DotProduct(ab, ab), -0.03f, 1.08f);
        if (Vector2Distance(q, m.rest) < m.restRadius + 4.0f || Vector2Distance(q, b) < m.buddhaRadius + 12.0f) return true;
        return Vector2Distance(q, Vector2Add(a, Vector2Scale(ab, t))) < 14.0f;
    }

    // Энгэр дээрх цагаан суварга (Галдан зуу); доороос нь Булган уулын цогцолборын шат өгсөнө.
    void MountainStupa(Scene& s, const Mountain& m, Rng& rng)
    {
        const Color white = { 236, 234, 226, 255 }, gold = { 206, 168, 72, 255 };
        const Vector2 top = m.stupa;
        if (m.buddha.x < 1e8f) BulganComplex(s, m);
        // Суварга: тавцан, шатлалт суурь, бөмбөгөр, алтан оргил
        const float ground = s.terrain.HeightAt(top.x, top.y);
        float low = ground;
        for (float dx : { -5.0f, 5.0f })
        {
            for (float dz : { -5.0f, 5.0f }) low = std::min(low, s.terrain.HeightAt(top.x + dx, top.y + dz));
        }
        s.solid.Box({ top.x, (low - 1.0f + ground + 0.5f) * 0.5f, top.y }, { 10.0f, ground + 0.5f - low + 1.0f, 10.0f }, kConcrete); // тавцан
        s.solid.Box({ top.x, ground + 1.3f, top.y }, { 6.0f, 1.6f, 6.0f }, white);
        s.solid.Box({ top.x, ground + 2.6f, top.y }, { 4.6f, 1.0f, 4.6f }, white);
        s.solid.Box({ top.x, ground + 3.4f, top.y }, { 3.6f, 0.6f, 3.6f }, white);
        s.solid.Ellipsoid({ top.x, ground + 4.9f, top.y }, { 2.0f, 1.9f, 2.0f }, 5, 10, white);
        s.solid.Box({ top.x, ground + 7.0f, top.y }, { 1.2f, 0.6f, 1.2f }, white);
        s.solid.Cylinder({ top.x, ground + 7.3f, top.y }, 0.55f, 0.12f, 3.0f, 8, gold);
        s.solid.Ellipsoid({ top.x, ground + 10.5f, top.y }, { 0.3f, 0.35f, 0.3f }, 3, 6, gold);
        s.Collider({ top.x, ground + 3.0f, top.y }, { 6.0f, 6.0f, 6.0f });
        (void)rng;
    }

    // Хотын дэргэдэх уул (Цэцэрлэгийн Булган уул): хойд энгэрт шинэсэн ой, урд эгц энгэр, нуруундаа хад чулуу,
    // оройн ойролцоо тахилгат овоо. Бэлийн хотын хэсэгт юу ч тавихгүй.
    void BuildMountains(Scene& s)
    {
        Rng rng(5150u);
        int larches = 0, rocks = 0;
        for (const Mountain& m : s.layout.Mountains())
        {
            if (m.ovoo.x < 1e8f) Ovoo(s, m.ovoo.x, m.ovoo.y);
            if (m.stupa.x < 1e8f) MountainStupa(s, m, rng);
            const float x0 = m.peak.x - m.east, x1 = m.peak.x + m.west, z0 = m.peak.y - m.south, z1 = m.peak.y + m.north;
            for (float gz = z0; gz < z1; gz += 9.0f)
            {
                for (float gx = x0; gx < x1; gx += 9.0f)
                {
                    const float x = gx + rng.Range(-4.0f, 4.0f), z = gz + rng.Range(-4.0f, 4.0f);
                    const float profile = m.Profile(x, z);
                    if (profile < 0.06f || s.layout.CityBuiltDistance(x, z) < 50.0f || s.layout.RoadDistance(x, z) < 2.0f) continue;
                    if (m.ovoo.x < 1e8f && std::fabs(x - m.ovoo.x) < 6.0f && std::fabs(z - m.ovoo.y) < 6.0f) continue;
                    if (NearBulganComplex(m, x, z)) continue;
                    const Vector3 n = GroundNormalAt(s, x, z);
                    const float grove = Fbm(x * 0.006f + 41.0f, z * 0.006f + 9.0f, 3);
                    if (profile < 0.9f && grove > 0.36f && larches < 1600 && rng.Chance(n.z > 0.05f ? 0.07f + 0.15f * n.z : 0.012f))
                    {
                        // Сийрэг шинэс: хойд энгэр, хавцалд арай олон, урд энгэрт ганц нэг
                        const int cluster = rng.Int(1, 2);
                        for (int k = 0; k < cluster; ++k)
                        {
                            const float tx = x + rng.Range(-3.5f, 3.5f), tz = z + rng.Range(-3.5f, 3.5f);
                            Larch(s, tx, tz, rng.Range(9.0f, 16.0f) * (1.0f - profile * 0.35f));
                            ++larches;
                        }
                    }
                    else if (profile > 0.12f && rocks < 1800 && rng.Chance((n.z < 0.1f ? 0.06f : 0.025f) * (0.5f + profile)))
                    {
                        // Боржин хад, асга: энгэр даяар, дээшээ томорно (ягаан саарал)
                        const float size = rng.Range(1.0f, 2.2f) * (1.0f + profile * 1.6f);
                        const float rx = size * rng.Range(0.8f, 1.4f), ry = size * rng.Range(0.6f, 1.1f), rz = size * rng.Range(0.8f, 1.4f);
                        const float ground = s.terrain.HeightAt(x, z);
                        const Color granite = MixColor({ 164, 136, 120, 255 }, { 128, 116, 108, 255 }, rng.Next());
                        s.solid.Rock({ x, ground + ry * 0.25f, z }, { rx, ry, rz }, JitterColor(granite, rng, 0.08f), rng);
                        s.Collider({ x, ground + ry * 0.5f, z }, { rx * 1.3f, ry * 1.4f, rz * 1.3f });
                        ++rocks;
                    }
                }
            }
        }
        if (!s.layout.Mountains().empty()) TraceLog(LOG_INFO, "CITY: mountain forest %d larches, %d rocks", larches, rocks);
    }

    // Хотын үеийн байгууламжууд: застав (Сүхбаатарын талбай дээр), ШТС, хөшөө, орхигдсон машинууд.
    void CapitalSquare(Scene& s);

    void BuildCitySettlement(Scene& s)
    {
        const CityData& city = CurrentCity();
        const Layout& layout = s.layout;
        AtLevel(s, layout.BuildLevel(-40.0f, 0.0f), [&] { BuildCheckpoint(s); });
        int station = 0;
        for (const CityPoi& poi : city.pois)
        {
            const Vector2 p = poi.position;
            if (poi.kind == CityPoiKind::Station)
            {
                const int style = s.variant + station++;
                AtLevel(s, layout.BuildLevel(p.x + 35.0f, p.y - 11.0f), [&] { GasStation(s, p.x, p.y, style); });
            }
            if (poi.kind == CityPoiKind::Warehouse) AtLevel(s, layout.BuildLevel(p.x + 5.0f, p.y), [&] { Warehouse(s, p.x, p.y); });
        }
        if (!s.layout.IsCapital())
        {
            // Аймгийн төвөөс гадна: хот айл, жижиг суурин, мод, овоо, мал (бүс нутгаас хамаарна)
            for (const Vector2& camp : layout.camps) AtLevel(s, layout.BuildLevel(camp.x, camp.y), [&] { HerderCamp(s, camp.x, camp.y); });
            for (const Vector2& hamlet : layout.hamlets)
            {
                AtLevel(s, layout.BuildLevel(hamlet.x - 4.0f, hamlet.y), [&] { Hamlet(s, hamlet.x, hamlet.y); });
            }
            BuildMountains(s);
            BuildNature(s);
            return;
        }
        AtLevel(s, layout.BuildLevel(0.0f, 0.0f), [&] { CapitalSquare(s); });
    }

    // Сүхбаатарын талбайн хөшөө, гэрэлтүүлэг, орхигдсон машинууд
    void CapitalSquare(Scene& s)
    {
        SukhbaatarStatue(s, 0.0f, 0.0f);
        for (float x : { -60.0f, 60.0f })
        {
            for (float z : { -90.0f, 90.0f }) LightPole(s, x, z, s.rng.Chance(0.25f) ? 1 : 0);
        }
        Car(s, 30.0f, -40.0f, true, RandomCarPaint(s.rng));
        Car(s, 44.0f, 52.0f, false, RandomCarPaint(s.rng));
        Truck(s, 58.0f, -70.0f, true, { 96, 104, 96, 255 }, { 120, 116, 104, 255 });
        Crate(s, 12.0f, 20.0f, 1.0f, 0.0f, kWood);
        Barrel(s, -8.0f, -24.0f, kRust);
    }

    // Талд тархсан том чулуунууд: ард нь хоргодож болно.
    void BuildBoulders(Scene& s)
    {
        Rng rng(7717u);
        int placed = 0;
        for (int attempt = 0; attempt < 6000 && placed < 1400; ++attempt)
        {
            const float x = rng.Range(-kPlayableHalfExtent, kPlayableHalfExtent);
            const float z = rng.Range(-kPlayableHalfExtent, kPlayableHalfExtent);
            if (s.layout.FlatDistance(x, z) < 6.0f)
            {
                continue;
            }
            if (s.layout.IsCity() && s.layout.CityBuiltDistance(x, z) < 150.0f) continue; // аймгийн төвийн гадна
            if (InAnyMountain(s, x, z)) continue;

            const float rx = rng.Range(1.0f, 2.2f);
            const float ry = rng.Range(0.8f, 1.7f);
            const float rz = rng.Range(1.0f, 2.2f);
            const float ground = s.terrain.HeightAt(x, z);

            s.solid.Rock({ x, ground + ry * 0.35f, z }, { rx, ry, rz }, JitterColor(kBoulder, rng, 0.1f), rng);
            s.Collider({ x, ground + ry * 0.55f, z }, { rx * 1.3f, ry * 1.5f, rz * 1.3f });
            ++placed;
        }
    }
}

const std::vector<Vector3>& CityVehicleSpots()
{
    return gCityVehicleSpots;
}

void BuildStaticStructures(const Layout& layout, const Terrain& terrain, std::vector<Obstacle>& obstacles,
                           MeshBuilder& solid, MeshBuilder& shadows, MeshBuilder& glass,
                           std::vector<Interior>& interiors, std::vector<Door>& doors,
                           std::vector<Passage>& passages, std::vector<WallSegment>& walls,
                           std::vector<FencePanel>& panels)
{
    Scene scene = { layout, terrain, obstacles, solid, shadows, glass, interiors, doors, passages, walls, panels };
    if (layout.IsCity())
    {
        BuildCity(scene, CurrentCity()); // Улаанбаатар / аймгийн жинхэнэ төв
        if (!layout.IsCapital()) BuildBoulders(scene); // хотоос гадна хээр тал
        return;
    }

    for (const Road& road : layout.roads)
    {
        BuildRoad(scene, road);
    }
    BuildHighwayProps(scene);
    BuildBoulders(scene);
}

void BuildSettlementStructures(int variant, const Layout& layout, const Terrain& terrain,
                               std::vector<Obstacle>& obstacles, MeshBuilder& solid, MeshBuilder& shadows,
                               MeshBuilder& glass, std::vector<Interior>& interiors, std::vector<Door>& doors,
                           std::vector<Passage>& passages, std::vector<WallSegment>& walls,
                           std::vector<FencePanel>& panels)
{
    Scene scene = { layout, terrain, obstacles, solid, shadows, glass, interiors, doors, passages, walls, panels };
    scene.rng = Rng((uint32_t)(5000 + variant * 97)); // үе бүрд өөр, гэхдээ тухайн үед үргэлж ижил
    scene.variant = variant;
    scene.region = RegionFor(variant);
    if (layout.IsCity())
    {
        BuildCitySettlement(scene);
        return;
    }

    // Агуулах, шатахуун түгээх газар, застав: аймаг бүрт өөр загвартай (байрлал нь даалгаврын улмаас тогтмол)
    BuildTownFixed(scene);
    BuildCheckpoint(scene);

    if (variant == kUlaanbaatarStage)
    {
        BuildUlaanbaatar(scene);
        BuildAirport(scene); // Буянт-Ухаа: жинхэнэ хэмжээтэй нисэх буудал
    }
    else
    {
        BuildTownLots(scene, variant);
    }
    BuildTown(scene);
    BuildHerderCamps(scene);
    BuildHamlets(scene);
    BuildNature(scene); // мод, овоо, нуур, мал (бүс нутгаас хамаарна)
}
