#include "Structures.h"

#include "Procedural.h"
#include "Layout.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>

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
        Rng rng{ 20240u };
        int variant = 0;               // аймгийн дугаар
        Region region = Region::West;  // аймгийн бүс нутаг

        void Collider(Vector3 center, Vector3 size, bool blocksRays = true, bool walkable = false)
        {
            obstacles.push_back({ center, size, blocksRays, walkable });
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
    void GasStation(Scene& s)
    {
        // Аймаг бүрт өөр брэндийн өнгө (гол, туслах), дэлгүүрийн хана, түгээгүүрийн тоо
        struct Brand { Color main, accent, wall; };
        static const Brand brands[] = {
            { { 164, 52, 42, 255 }, { 214, 176, 40, 255 }, { 178, 172, 158, 255 } },  // улаан, шар
            { { 40, 120, 70, 255 }, { 230, 230, 220, 255 }, { 200, 204, 196, 255 } },  // ногоон, цагаан
            { { 36, 76, 150, 255 }, { 236, 140, 40, 255 }, { 186, 190, 196, 255 } },   // хөх, улбар шар
            { { 90, 56, 130, 255 }, { 230, 200, 60, 255 }, { 196, 186, 170, 255 } },   // нил ягаан, шар
            { { 30, 40, 60, 255 }, { 200, 50, 50, 255 }, { 170, 172, 176, 255 } },     // хар хөх, улаан
        };
        const Brand& brand = brands[s.variant % 5];

        // Дэлгүүр
        const float cx = 44.0f, cz = -14.0f, w = 10.0f, d = 5.0f, h = 3.2f;
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
        const float px = 30.0f, pz = -11.5f;
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
        const int pumps = 2 + s.variant % 2;
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
        const float sx = 22.5f, sz = -6.8f;
        s.solid.Box({ sx, 2.6f, sz }, { 0.3f, 5.2f, 0.3f }, kMetal);
        s.solid.Box({ sx, 5.3f, sz }, { 2.2f, 1.6f, 0.3f }, brand.main);
        s.solid.Box({ sx, 5.75f, sz }, { 2.0f, 0.4f, 0.32f }, brand.accent);
        for (int k = 0; k < 2; ++k)
        {
            s.solid.Box({ sx, 4.9f + 0.36f * (float)k, sz }, { 1.6f, 0.24f, 0.34f }, { 20, 20, 24, 255 });
        }
        s.Collider({ sx, 2.6f, sz }, { 0.4f, 5.2f, 0.4f });
        s.Shadow(sx - 1.1f, sz - 0.15f, sx + 1.1f, sz + 0.15f, 4.5f, 6.1f);
        Barrel(s, 36.5f, -15.5f, kRust);
        Barrel(s, 37.3f, -15.9f, { 60, 96, 130, 255 });
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
        for (float x = -526.0f; x <= 530.0f; x += 38.0f, ++index)
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
        Car(s, -205.0f, HighwayZ(s.layout, -205.0f) - 1.9f, true, { 164, 160, 150, 255 });
        Truck(s, -300.0f, HighwayZ(s.layout, -300.0f) - 2.1f, true, { 96, 60, 52, 255 }, { 86, 100, 88, 255 });
        Car(s, -410.0f, HighwayZ(s.layout, -410.0f) + 5.4f, true, { 90, 98, 76, 255 });

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
    void SukhbaatarStatue(Scene& s, float x, float z)
    {
        s.solid.Box({ x, 0.25f, z }, { 7.0f, 0.5f, 7.0f }, kConcrete);
        s.solid.Box({ x, 0.8f, z }, { 4.6f, 0.6f, 5.6f }, ScaleColor(kGranite, 0.8f));
        s.solid.Box({ x, 2.9f, z }, { 2.4f, 3.6f, 4.6f }, kGranite);
        s.solid.Box({ x, 4.75f, z }, { 2.7f, 0.2f, 4.9f }, ScaleColor(kGranite, 0.8f));
        s.Collider({ x, 0.25f, z }, { 7.0f, 0.5f, 7.0f });
        s.Collider({ x, 4.5f, z }, { 2.6f, 9.0f, 4.8f });

        // Орон нутгийн тэнхлэгт морь +Z рүү харна; бүтнээр нь эргүүлж -Z рүү харуулна.
        PlaceFigure(s, x, 4.85f, z, PI, 1.5f);
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

        s.Shadow(x - 3.5f, z - 3.5f, x + 3.5f, z + 3.5f, 0.0f, 0.5f);
        s.Shadow(x - 1.2f, z - 2.3f, x + 1.2f, z + 2.3f, 0.5f, 9.0f);
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

void BuildStaticStructures(const Layout& layout, const Terrain& terrain, std::vector<Obstacle>& obstacles,
                           MeshBuilder& solid, MeshBuilder& shadows, MeshBuilder& glass,
                           std::vector<Interior>& interiors, std::vector<Door>& doors,
                           std::vector<Passage>& passages)
{
    Scene scene = { layout, terrain, obstacles, solid, shadows, glass, interiors, doors, passages };

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
                           std::vector<Passage>& passages)
{
    Scene scene = { layout, terrain, obstacles, solid, shadows, glass, interiors, doors, passages };
    scene.rng = Rng((uint32_t)(5000 + variant * 97)); // үе бүрд өөр, гэхдээ тухайн үед үргэлж ижил
    scene.variant = variant;
    scene.region = RegionFor(variant);

    // Агуулах, шатахуун түгээх газар, застав: аймаг бүрт өөр загвартай (байрлал нь даалгаврын улмаас тогтмол)
    BuildTownFixed(scene);
    BuildCheckpoint(scene);

    if (variant == kUlaanbaatarStage)
    {
        BuildUlaanbaatar(scene);
    }
    else
    {
        BuildTownLots(scene, variant);
    }
    BuildHerderCamps(scene);
    BuildHamlets(scene);
}
