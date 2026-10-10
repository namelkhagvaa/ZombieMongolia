#include "Structures.h"

#include "Procedural.h"
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
        Rng rng{ 20240u };

        void Collider(Vector3 center, Vector3 size, bool blocksRays = true)
        {
            obstacles.push_back({ center, size, blocksRays });
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
    };

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
        const float windowY = 1.95f, windowWidth = 1.25f, windowHeight = 1.1f;
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
                wallPiece(face, door + doorWidth * 0.5f, half, 0.0f, wallHeight, true);
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

                Window(s, cx, cz, w, d, face, span * 0.22f, windowY, windowWidth, windowHeight);
                Window(s, cx, cz, w - thick * 2.1f, d - thick * 2.1f, face, span * 0.22f, windowY, windowWidth, windowHeight);
            }
            else
            {
                wallPiece(face, -half, half, 0.0f, wallHeight, true);

                // Цонх: гадна, дотор хоёр талаас харагдана
                const float offsets[2] = { -span * 0.24f, span * 0.24f };
                const int count = span > 6.2f ? 2 : 1;
                for (int i = 0; i < count; ++i)
                {
                    const float along = count == 2 ? offsets[i] : 0.0f;
                    Window(s, cx, cz, w, d, face, along, windowY, windowWidth, windowHeight);
                    Window(s, cx, cz, w - thick * 2.1f, d - thick * 2.1f, face, along, windowY, windowWidth, windowHeight);
                }
            }
        }

        if (style.flatRoof)
        {
            // Хавтгай дээвэр ба эргэн тойрны намхан хашлага
            const Color parapet = ScaleColor(outer, 0.88f);
            s.solid.Box({ cx, wallHeight + 0.12f, cz }, { w + 0.5f, 0.24f, d + 0.5f }, style.roof);
            s.solid.Box({ cx, wallHeight + 0.44f, cz - d * 0.5f - 0.15f }, { w + 0.5f, 0.4f, 0.2f }, parapet);
            s.solid.Box({ cx, wallHeight + 0.44f, cz + d * 0.5f + 0.15f }, { w + 0.5f, 0.4f, 0.2f }, parapet);
            s.solid.Box({ cx - w * 0.5f - 0.15f, wallHeight + 0.44f, cz }, { 0.2f, 0.4f, d + 0.5f }, parapet);
            s.solid.Box({ cx + w * 0.5f + 0.15f, wallHeight + 0.44f, cz }, { 0.2f, 0.4f, d + 0.5f }, parapet);
        }
        else
        {
            s.solid.GableRoof({ cx, wallHeight, cz }, w + 1.0f, d + 1.0f, style.roofHeight, w >= d, style.roof);
        }
        s.solid.Box({ cx, wallHeight - 0.03f, cz }, { w - thick, 0.05f, d - thick }, ScaleColor(outer, 0.85f)); // тааз
        if (style.chimney)
        {
            const float top = style.flatRoof ? 1.3f : style.roofHeight * 0.6f + 1.0f;
            s.solid.Box({ cx + w * 0.25f, wallHeight + top * 0.5f, cz + d * 0.15f }, { 0.55f, top, 0.55f }, kBrick);
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
        s.Collider({ cx, wallHeight + 0.5f, cz }, { w, 1.0f, d });
        Footprint(s, cx, cz, w, d);
        s.Shadow(cx - w * 0.5f - 0.45f, cz - d * 0.5f - 0.45f, cx + w * 0.5f + 0.45f, cz + d * 0.5f + 0.45f, 0.0f,
                 wallHeight + 0.6f);
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

            // Дээвэр: бүх талд
            s.solid.Quad(t0, t1, ring(i + 1, crown, wall + roof), ring(i, crown, wall + roof), JitterColor(kGerRoof, s.rng, 0.03f));

            if (i == doorSide)
            {
                // Хаалганы тотго ба хоёр хажуугийн хүрээ
                s.solid.Quad({ b0.x, doorHeight, b0.z }, { b1.x, doorHeight, b1.z }, t1, t0, kGerFelt);
                s.solid.Box({ b0.x, doorHeight * 0.5f, b0.z }, { 0.12f, doorHeight, 0.12f }, doorColor);
                s.solid.Box({ b1.x, doorHeight * 0.5f, b1.z }, { 0.12f, doorHeight, 0.12f }, doorColor);
                s.solid.Box({ cx, doorHeight + 0.05f, b0.z }, { std::fabs(b1.x - b0.x) + 0.12f, 0.1f, 0.12f }, doorColor);

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

        // (дагуу, өндөр, хөндлөн) -> ертөнцийн хайрцаг
        auto box = [&](float along, float y, float lengthAlong, float h, float thickness, float offset, Color color) {
            if (alongX) s.solid.Box({ along, y, fixed + offset }, { lengthAlong, h, thickness }, color);
            else s.solid.Box({ fixed + offset, y, along }, { thickness, h, lengthAlong }, color);
        };
        auto shadowRun = [&](float from, float to) {
            if (to <= from) return;
            if (alongX) s.Shadow(from, fixed - 0.05f, to, fixed + 0.05f, 0.0f, height);
            else s.Shadow(fixed - 0.05f, from, fixed + 0.05f, to, 0.0f, height);
        };

        float runStart = start;
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
                    // Унасан банз газар хэвтэнэ
                    box(mid, 0.06f, segmentLength * 0.9f, 0.06f, 0.5f, 0.6f, JitterColor(kWoodGrey, s.rng, 0.1f));
                }
                continue;
            }

            box(a + 0.07f, height * 0.5f, 0.14f, height, 0.14f, 0.0f, ScaleColor(kWood, 0.75f)); // багана
            box(mid, 0.6f, segmentLength, 0.09f, 0.07f, -0.08f, ScaleColor(kWood, 0.8f));       // хөндлөвч
            box(mid, 1.6f, segmentLength, 0.09f, 0.07f, -0.08f, ScaleColor(kWood, 0.8f));

            const int planks = 4;
            const float plankWidth = segmentLength / (float)planks;
            for (int p = 0; p < planks; ++p)
            {
                const float h = height + s.rng.Range(-0.14f, 0.08f);
                const Color color = JitterColor(MixColor(kWood, kWoodGrey, s.rng.Next()), s.rng, 0.1f);
                box(a + ((float)p + 0.5f) * plankWidth, h * 0.5f, plankWidth - 0.04f, h, 0.05f, 0.0f, color);
            }

            if (alongX) s.Collider({ mid, height * 0.5f, fixed }, { segmentLength, height, 0.25f });
            else s.Collider({ fixed, height * 0.5f, mid }, { 0.25f, height, segmentLength });
        }
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

    void Car(Scene& s, float cx, float cz, bool alongX, Color paint)
    {
        const Oriented o = { cx, cz, alongX, kCarScale };
        const Color body = JitterColor(paint, s.rng, 0.08f);

        Part(s, o, 0.0f, 0.62f, 0.0f, 4.2f, 0.55f, 1.75f, body);
        Part(s, o, -0.25f, 1.15f, 0.0f, 2.3f, 0.5f, 1.6f, kGlassDark);                    // бүхээг, шил
        Part(s, o, -0.25f, 1.43f, 0.0f, 2.4f, 0.08f, 1.68f, ScaleColor(body, 0.9f));      // дээвэр
        Part(s, o, -1.36f, 1.15f, 0.0f, 0.1f, 0.5f, 1.62f, body);                          // арын багана
        Part(s, o, 0.86f, 1.15f, 0.0f, 0.1f, 0.5f, 1.62f, body);                           // урд багана
        Part(s, o, 2.12f, 0.5f, 0.0f, 0.12f, 0.2f, 1.7f, kMetal);                          // бампер
        Part(s, o, -2.12f, 0.5f, 0.0f, 0.12f, 0.2f, 1.7f, kMetal);
        Part(s, o, 2.11f, 0.74f, 0.6f, 0.06f, 0.14f, 0.3f, kWindowFrame);                  // гэрэл
        Part(s, o, 2.11f, 0.74f, -0.6f, 0.06f, 0.14f, 0.3f, kWindowFrame);
        Part(s, o, s.rng.Range(-1.5f, 1.5f), 0.7f, 0.88f, 1.0f, 0.3f, 0.02f, kRust);       // зэв
        Part(s, o, s.rng.Range(-1.5f, 1.5f), 0.7f, -0.88f, 0.8f, 0.25f, 0.02f, kRust);

        for (float along : { -1.35f, 1.35f })
        {
            for (float across : { -0.82f, 0.82f })
            {
                Part(s, o, along, 0.33f, across, 0.66f, 0.66f, 0.26f, kRubber);
            }
        }

        VehicleBounds(s, o, 4.3f, 1.5f, 1.8f);
    }

    void Truck(Scene& s, float cx, float cz, bool alongX, Color cabColor, Color cargoColor)
    {
        const Oriented o = { cx, cz, alongX, 1.1f };

        Part(s, o, 0.0f, 0.85f, 0.0f, 7.2f, 0.3f, 2.1f, ScaleColor(kMetal, 0.5f));          // арал
        Part(s, o, 2.6f, 1.85f, 0.0f, 1.9f, 1.7f, 2.3f, JitterColor(cabColor, s.rng, 0.06f)); // бүхээг
        Part(s, o, 3.56f, 2.15f, 0.0f, 0.04f, 0.7f, 1.9f, kGlassDark);
        Part(s, o, 2.6f, 2.15f, 1.16f, 0.9f, 0.6f, 0.04f, kGlassDark);
        Part(s, o, 2.6f, 2.15f, -1.16f, 0.9f, 0.6f, 0.04f, kGlassDark);
        Part(s, o, 3.6f, 1.15f, 0.0f, 0.12f, 0.3f, 2.3f, kMetal);                            // бампер
        Part(s, o, -1.05f, 2.15f, 0.0f, 5.0f, 2.3f, 2.4f, JitterColor(cargoColor, s.rng, 0.06f)); // тэвш
        Part(s, o, -1.05f, 1.1f, 0.0f, 5.1f, 0.25f, 2.45f, ScaleColor(cargoColor, 0.7f));

        for (float along : { 2.5f, -1.0f, -2.4f })
        {
            for (float across : { -1.0f, 1.0f })
            {
                Part(s, o, along, 0.5f, across, 1.0f, 1.0f, 0.32f, kRubber);
            }
        }

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

        s.solid.Box({ cx, deck, cz }, { 3.2f, 0.2f, 3.2f }, timber);                          // шал
        s.solid.Box({ cx, deck + 0.6f, cz - 1.55f }, { 3.2f, 1.0f, 0.1f }, kOlive);            // хашлага
        s.solid.Box({ cx, deck + 0.6f, cz + 1.55f }, { 3.2f, 1.0f, 0.1f }, kOlive);
        s.solid.Box({ cx + 1.55f, deck + 0.6f, cz }, { 0.1f, 1.0f, 3.2f }, kOlive);
        s.solid.Box({ cx - 1.55f, deck + 0.6f, cz + 0.8f }, { 0.1f, 1.0f, 1.6f }, kOlive);    // шатны талд онгорхой
        s.solid.GableRoof({ cx, 6.4f, cz }, 3.9f, 3.9f, 0.9f, true, JitterColor(kRust, s.rng, 0.08f));
        s.solid.Box({ cx - 1.75f, deck * 0.5f, cz - 0.7f }, { 0.08f, deck, 0.5f }, timber);   // шат

        s.Shadow(cx - 1.6f, cz - 1.6f, cx + 1.6f, cz + 1.6f, deck, deck + 1.2f);
        s.Shadow(cx - 1.95f, cz - 1.95f, cx + 1.95f, cz + 1.95f, 6.4f, 6.9f);
    }

    void GuardBooth(Scene& s, float cx, float cz)
    {
        const float w = 2.2f, h = 2.5f;
        s.solid.Box({ cx, h * 0.5f, cz }, { w, h, w }, { 196, 192, 180, 255 });
        s.solid.Box({ cx, 1.0f, cz }, { w + 0.04f, 0.3f, w + 0.04f }, { 160, 52, 40, 255 }); // улаан зурвас
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

    void Tent(Scene& s, float cx, float cz)
    {
        const float w = 4.2f, d = 6.5f;
        s.solid.GableRoof({ cx, 0.0f, cz }, w, d, 2.3f, false, JitterColor(kOlive, s.rng, 0.08f));
        s.solid.Box({ cx, 0.9f, cz - d * 0.5f - 0.03f }, { 1.0f, 1.8f, 0.05f }, ScaleColor(kOlive, 0.45f)); // үүд
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
        const Color wall = { 98, 114, 124, 255 };

        s.solid.Box({ cx, h * 0.5f, cz }, { w, h, d }, wall);
        s.solid.Box({ cx, 0.45f, cz }, { w + 0.08f, 0.9f, d + 0.08f }, ScaleColor(wall, 0.7f));
        // Долгионт төмрийн хавтангийн босоо зураас
        for (float x = cx - w * 0.5f + 2.0f; x < cx + w * 0.5f; x += 2.0f)
        {
            s.solid.Box({ x, h * 0.5f + 0.45f, cz }, { 0.08f, h - 0.9f, d + 0.06f }, ScaleColor(wall, 0.85f));
        }
        s.solid.GableRoof({ cx, h, cz }, w + 0.8f, d + 0.8f, 1.8f, true, JitterColor(kRust, s.rng, 0.06f));

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
        // Дэлгүүр
        const float cx = 44.0f, cz = -14.0f, w = 10.0f, d = 5.0f, h = 3.2f;
        s.solid.Box({ cx, h * 0.5f, cz }, { w, h, d }, { 178, 172, 158, 255 });
        s.solid.Box({ cx, 0.3f, cz }, { w + 0.06f, 0.6f, d + 0.06f }, { 120, 116, 108, 255 });
        s.solid.Box({ cx, h + 0.12f, cz }, { w + 0.7f, 0.24f, d + 0.7f }, ScaleColor(kMetal, 0.6f));
        s.solid.Box({ cx, h + 0.85f, cz + d * 0.5f }, { 5.0f, 1.0f, 0.2f }, { 164, 52, 42, 255 });   // хаягийн самбар
        s.solid.Box({ cx, h + 0.85f, cz + d * 0.5f + 0.02f }, { 4.4f, 0.22f, 0.2f }, { 214, 176, 40, 255 });
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
        s.solid.Box({ px, 4.2f, pz }, { 8.5f, 0.2f, 5.7f }, { 164, 52, 42, 255 });
        s.Shadow(px - 4.2f, pz - 2.8f, px + 4.2f, pz + 2.8f, 4.0f, 4.4f);

        s.solid.Box({ px, 0.08f, pz }, { 5.4f, 0.16f, 1.1f }, kConcrete);
        for (float dx : { -1.6f, 1.6f })
        {
            s.Block({ px + dx, 0.9f, pz }, { 0.6f, 1.5f, 0.42f }, { 168, 60, 46, 255 });
            s.solid.Box({ px + dx, 1.25f, pz }, { 0.44f, 0.4f, 0.46f }, kWindow);
        }
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

    void BuildCheckpoint(Scene& s)
    {
        // Замыг могойрон гарах бетон хаалтууд
        JerseyBarrier(s, -44.0f, -2.5f, false);
        JerseyBarrier(s, -50.0f, 2.5f, false);
        JerseyBarrier(s, -56.0f, -2.5f, false);

        // Замын хоёр талын элсэн уутан хоргодох байр
        SandbagNest(s, -49.0f, -43.0f, 5.4f, 2.8f, +1);
        SandbagNest(s, -60.0f, -54.0f, -5.4f, 2.8f, -1);

        GuardBooth(s, -53.0f, 7.4f);
        BarrierArm(s, -51.2f, 4.7f);
        Watchtower(s, -60.5f, 10.5f);
        Flagpole(s, -56.5f, 13.0f);

        // Торон хашаа: замын хоёр талаар хөндлөн татсан
        ChainFence(s, -64.0f, 5.6f, -64.0f, 29.0f);
        ChainFence(s, -64.0f, 29.0f, -43.0f, 29.0f);
        ChainFence(s, -64.0f, -29.0f, -64.0f, -5.6f);
        ChainFence(s, -64.0f, -29.0f, -43.0f, -29.0f);

        // Хуаран
        Tent(s, -49.5f, -17.0f);
        Tent(s, -43.0f, -17.0f);
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
                  RandomHouseStyle(s.rng), gateFace);
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
            House(s, frontX - toFront * smallWidth * 0.5f, iz1 - smallDepth * 0.5f, smallWidth / kHouseScale,
                  smallDepth / kHouseScale, RandomHouseStyle(s.rng), 3);
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

    // Төв суурин: шороон замын хоёр талаар хашаанууд. Хашааны тоо, хэмжээ үе бүрд өөр.
    void BuildTownLots(Scene& s)
    {
        for (int side = 0; side < 2; ++side)
        {
            const float x0 = side == 0 ? -2.0f : 33.0f;
            const float x1 = side == 0 ? 20.0f : 57.0f;
            const int gateFace = side == 0 ? 0 : 1; // зам руу харсан тал

            const int rows = s.rng.Int(2, 3);
            const float gap = 3.5f;
            const float usable = 48.0f - gap * (float)(rows - 1); // z = 9 .. 57
            float z = 9.0f;
            float remaining = usable;
            for (int row = 0; row < rows; ++row)
            {
                const int left = rows - row;
                const float depth = left == 1 ? remaining : remaining / (float)left * s.rng.Range(0.85f, 1.15f);
                BuildLot(s, x0, z, x1, z + depth, gateFace);
                z += depth + gap;
                remaining -= depth;
            }
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
            House(s, x + 6.0f, z + 7.0f, 4.8f, 4.4f, RandomHouseStyle(s.rng), 3);
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

            s.solid.Blob({ x, ground + ry * 0.35f, z }, { rx, ry, rz }, JitterColor(kBoulder, rng, 0.1f), rng);
            s.Collider({ x, ground + ry * 0.55f, z }, { rx * 1.3f, ry * 1.5f, rz * 1.3f });
            ++placed;
        }
    }
}

void BuildStaticStructures(const Layout& layout, const Terrain& terrain, std::vector<Obstacle>& obstacles,
                           MeshBuilder& solid, MeshBuilder& shadows, MeshBuilder& glass,
                           std::vector<Interior>& interiors, std::vector<Door>& doors)
{
    Scene scene = { layout, terrain, obstacles, solid, shadows, glass, interiors, doors };

    for (const Road& road : layout.roads)
    {
        BuildRoad(scene, road);
    }
    BuildTownFixed(scene);
    BuildCheckpoint(scene);
    BuildHighwayProps(scene);
    BuildBoulders(scene);
}

void BuildSettlementStructures(int variant, const Layout& layout, const Terrain& terrain,
                               std::vector<Obstacle>& obstacles, MeshBuilder& solid, MeshBuilder& shadows,
                               MeshBuilder& glass, std::vector<Interior>& interiors, std::vector<Door>& doors)
{
    Scene scene = { layout, terrain, obstacles, solid, shadows, glass, interiors, doors };
    scene.rng = Rng((uint32_t)(5000 + variant * 97)); // үе бүрд өөр, гэхдээ тухайн үед үргэлж ижил

    BuildTownLots(scene);
    BuildHerderCamps(scene);
    BuildHamlets(scene);
}
