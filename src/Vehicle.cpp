#include "Vehicle.h"
#include "VehicleModel.h"

#include "Audio.h"
#include "HorseModel.h"
#include "Humanoid.h"
#include "MeshBuilder.h"
#include "Mover.h"
#include "Procedural.h"
#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>

// Зомбийг мөргөж унагааж алах (агаарт шидэх) хурд, м/с. Жийп хүнд тул ~16 км/ц-аас; морь дэвсэж, мотоцикл
// мөргөж ~22-25 км/ц-аас дээш хурдтай давхиж ирэхэд зомбийг унагааж ална.
float RamLaunchSpeed(VehicleType type)
{
    switch (type)
    {
    case VehicleType::Horse: return 6.0f;
    case VehicleType::Motorcycle: return 7.0f;
    case VehicleType::Trike: return 8.0f;
    default: return 4.5f;
    }
}

const VehicleSpec& SpecFor(VehicleType type)
{
    //                name          top   acc   brake rev  turn  rad   asph  dirt  steppe slope ram   encl   seat                   cam
    static const VehicleSpec specs[] = {
        /* Jeep  */ { "ЖИЙП",       30.0f, 9.0f, 20.0f, 7.0f, 1.9f, 1.5f, 1.00f, 0.70f, 0.45f, 1.6f, 9.0f, true,  { 0.0f, 1.0f, 0.0f },   4.0f, 160.0f, 9.0f, 1.6f },
        /* Moto  */ { "МОТОЦИКЛ", 26.0f, 11.0f, 18.0f, 3.0f, 2.4f, 0.7f, 1.00f, 0.80f, 0.60f, 1.2f, 3.0f, false, { 0.0f, 0.86f, -0.22f }, 1.5f, 60.0f, 7.0f, 0.35f },
        /* Trike */ { "ГУРВАН ДУГУЙТ",      17.0f, 6.0f, 14.0f, 4.0f, 1.8f, 1.0f, 1.00f, 0.85f, 0.70f, 1.4f, 4.5f, false, { 0.0f, 0.92f, 0.05f },  2.0f, 90.0f, 6.0f, 0.7f },
        /* Horse */ { "МОРЬ",      17.0f, 9.0f, 12.0f, 2.0f, 2.8f, 0.8f, 0.85f, 1.00f, 0.95f, 0.6f, 2.0f, false, { 0.0f, 1.5f, -0.05f },  1.5f, 90.0f, 0.0f, 0.6f },
        /* Truck */ { "АЧААНЫ МАШИН", 22.0f, 4.5f, 12.0f, 4.0f, 1.25f, 2.1f, 1.00f, 0.78f, 0.55f, 1.9f, 16.0f, true, { 0.5f, 1.75f, 1.25f }, 7.0f, 420.0f, 8.0f, 6.0f },
        /* Suv   */ { "X-TRAIL",    31.0f, 9.5f, 20.0f, 7.0f, 1.95f, 1.5f, 1.00f, 0.72f, 0.50f, 1.5f, 8.0f, true,  { 0.0f, 0.95f, 0.0f },  4.0f, 140.0f, 10.0f, 1.45f },
        /* Hatch */ { "YARIS",      35.0f, 10.0f, 21.0f, 7.0f, 2.15f, 1.35f, 1.00f, 0.62f, 0.35f, 2.0f, 6.0f, true, { 0.0f, 0.85f, 0.0f },  3.5f, 105.0f, 12.0f, 1.05f },
    };
    return specs[(int)type];
}

namespace
{
    // Унааны mesh-үүд: угсарсан хэлбэр бүрийг GPU руу нэг удаа хуулж, дараа нь DrawMesh-ээр (rlgl-ийн одоогийн
    // хувиргалтаар) зурна. Оройг кадр бүрт CPU-аас илгээхээс хэд дахин хурдан.
    const Material* propMaterial = nullptr;
    const Material* modelDepthMaterial = nullptr; // сүүдрийн зургийн үед текстуртай загварыг зурах материал
    std::map<const MeshBuilder*, Mesh> propMeshes;

    void DrawProp(const MeshBuilder& builder)
    {
        if (!propMaterial)
        {
            builder.DrawImmediate(); // shader-гүй (туршилт): шууд зурна
            return;
        }
        auto found = propMeshes.find(&builder);
        if (found == propMeshes.end())
        {
            found = propMeshes.emplace(&builder, builder.Upload()).first;
        }
        DrawBuiltMesh(found->second, *propMaterial);
    }

    const Color kTire = { 30, 30, 32, 255 };
    const Color kRim = { 128, 130, 134, 255 };
    const Color kChrome = { 168, 172, 178, 255 };
    const Color kSeat = { 34, 30, 30, 255 };
    const Color kDark = { 44, 44, 46, 255 };
    const Color kLamp = { 240, 234, 196, 255 };
    const Color kTailLamp = { 170, 30, 26, 255 };
    const Color kGlass = { 46, 58, 68, 255 };

    // Тэнхлэг нь X дагуу байрлах дугуй (дугуйн төв = 0): дугуй, обуд, төв, хигээс.
    void BuildWheel(MeshBuilder& b, float radius, float width, int spokes, bool thinSpokes)
    {
        const float hw = width * 0.5f;
        b.Tube({ -hw, 0.0f, 0.0f }, { hw, 0.0f, 0.0f }, radius, radius, 18, kTire);
        // Дугуйн хээ: гадаргуу дээрх жижиг товгорууд
        for (int i = 0; i < 18; ++i)
        {
            const float a = 2.0f * PI * ((float)i + 0.5f) / 18.0f;
            b.Box({ 0.0f, std::sin(a) * radius, std::cos(a) * radius }, { width * 0.9f, radius * 0.08f, radius * 0.16f },
                  ScaleColor(kTire, 0.85f));
        }
        // Обуд: хоёр талдаа бага зэрэг товгор цагираг
        for (float side : { -1.0f, 1.0f })
        {
            const float x = side * (hw + 0.005f);
            b.Tube({ x - side * 0.02f, 0.0f, 0.0f }, { x, 0.0f, 0.0f }, radius * 0.62f, radius * 0.6f, 14, kRim);
            b.Tube({ x, 0.0f, 0.0f }, { x + side * 0.025f, 0.0f, 0.0f }, radius * 0.18f, radius * 0.12f, 8, kChrome);
        }
        // Хигээс: дугуй эргэхэд харагдана
        for (int i = 0; i < spokes; ++i)
        {
            const float a = 2.0f * PI * (float)i / (float)spokes;
            const Vector3 tip = { 0.0f, std::sin(a) * radius * 0.6f, std::cos(a) * radius * 0.6f };
            for (float side : { -1.0f, 1.0f })
            {
                const float x = side * (thinSpokes ? hw * 0.3f : hw + 0.01f);
                b.Tube({ x, 0.0f, 0.0f }, { x, tip.y, tip.z }, thinSpokes ? 0.008f : 0.03f, thinSpokes ? 0.008f : 0.025f,
                       4, thinSpokes ? kChrome : ScaleColor(kRim, 0.8f), false);
            }
        }
    }

    // ------------------------------------------------------------ Жийп (УАЗ-469 маягийн)
    const MeshBuilder& JeepBody()
    {
        static MeshBuilder b;
        if (!b.Empty()) return b;
        const Color body = { 82, 96, 62, 255 };   // цэргийн ногоон
        const Color bodyDark = ScaleColor(body, 0.78f);
        const Color canvas = { 128, 118, 88, 255 };

        b.BevelBox({ 0.0f, 0.5f, 0.0f }, { 1.5f, 0.16f, 3.3f }, 0.03f, kDark);             // рам, доод тал
        b.BevelBox({ 0.0f, 0.84f, -0.05f }, { 1.72f, 0.52f, 3.56f }, 0.07f, body);         // их бие
        b.BevelBox({ 0.0f, 1.12f, 1.2f }, { 1.48f, 0.2f, 1.2f }, 0.06f, body);             // хөдөлгүүрийн таг
        b.Box({ 0.0f, 1.225f, 1.2f }, { 0.08f, 0.02f, 1.1f }, bodyDark);                    // тагны хавирга
        // Дугуйн хаалт: дугуй бүрийн дээгүүр товгор
        for (float x : { -0.8f, 0.8f })
        {
            for (float z : { -1.2f, 1.2f })
            {
                b.BevelBox({ x, 0.98f, z }, { 0.32f, 0.16f, 1.02f }, 0.06f, bodyDark);
                b.BevelBox({ x * 1.02f, 0.9f, z + 0.5f }, { 0.3f, 0.14f, 0.12f }, 0.03f, bodyDark);
                b.BevelBox({ x * 1.02f, 0.9f, z - 0.5f }, { 0.3f, 0.14f, 0.12f }, 0.03f, bodyDark);
            }
            // Хаалганы зураас, бариул
            b.Box({ x * 1.08f, 0.86f, 0.35f }, { 0.01f, 0.44f, 0.02f }, kDark);
            b.Box({ x * 1.08f, 0.86f, -0.55f }, { 0.01f, 0.44f, 0.02f }, kDark);
            b.Box({ x * 1.085f, 0.98f, 0.18f }, { 0.02f, 0.03f, 0.12f }, kChrome);
            b.Box({ x * 1.085f, 0.98f, -0.72f }, { 0.02f, 0.03f, 0.12f }, kChrome);
        }
        // Урд тал: сараалжтай радиатор, дугуй гэрэл, дохионы гэрэл, бампер
        b.BevelBox({ 0.0f, 0.98f, 1.81f }, { 1.02f, 0.36f, 0.06f }, 0.02f, bodyDark);
        for (int i = 0; i < 8; ++i)
        {
            b.Box({ -0.38f + 0.108f * (float)i, 0.98f, 1.845f }, { 0.03f, 0.3f, 0.02f }, kDark);
        }
        for (float x : { -0.62f, 0.62f })
        {
            b.Tube({ x, 1.02f, 1.78f }, { x, 1.02f, 1.86f }, 0.11f, 0.11f, 12, kChrome);
            b.Tube({ x, 1.02f, 1.86f }, { x, 1.02f, 1.87f }, 0.085f, 0.085f, 12, kLamp);
            b.Box({ x * 0.82f, 0.82f, 1.84f }, { 0.08f, 0.05f, 0.03f }, { 220, 140, 40, 255 });
        }
        b.BevelBox({ 0.0f, 0.6f, 1.92f }, { 1.86f, 0.15f, 0.14f }, 0.04f, kDark);
        b.BevelBox({ 0.0f, 0.6f, -1.92f }, { 1.86f, 0.15f, 0.14f }, 0.04f, kDark);
        for (float x : { -0.6f, 0.6f })
        {
            b.Box({ x, 0.95f, -1.85f }, { 0.12f, 0.16f, 0.04f }, kTailLamp);
        }
        // Салхины шил: налуу хүрээ
        Matrix tilt = MatrixMultiply(MatrixMultiply(MatrixTranslate(0.0f, -1.24f, -0.66f), MatrixRotateX(-14.0f * DEG2RAD)),
                                     MatrixTranslate(0.0f, 1.24f, 0.66f));
        b.SetTransform(tilt);
        b.BevelBox({ 0.0f, 1.52f, 0.66f }, { 1.62f, 0.56f, 0.06f }, 0.02f, bodyDark);
        b.Box({ 0.0f, 1.52f, 0.69f }, { 1.46f, 0.44f, 0.02f }, kGlass);
        b.ResetTransform();
        // Брезент дээвэр: дугуйрсан ирмэг, тулгуур хавирга, хажуугийн цонх
        b.BevelBox({ 0.0f, 1.55f, -0.52f }, { 1.68f, 0.72f, 2.26f }, 0.14f, canvas);
        for (float z : { -1.3f, -0.5f, 0.3f })
        {
            b.BevelBox({ 0.0f, 1.555f, z }, { 1.7f, 0.74f, 0.05f }, 0.12f, ScaleColor(canvas, 0.85f));
        }
        for (float x : { -0.845f, 0.845f })
        {
            b.Box({ x, 1.6f, 0.0f }, { 0.01f, 0.34f, 0.5f }, kGlass);
            b.Box({ x, 1.6f, -0.92f }, { 0.01f, 0.34f, 0.6f }, kGlass);
            // Толь
            b.Tube({ x * 0.98f, 1.25f, 0.62f }, { x * 1.18f, 1.42f, 0.66f }, 0.012f, 0.012f, 4, kDark, false);
            b.BevelBox({ x * 1.2f, 1.47f, 0.66f }, { 0.04f, 0.12f, 0.1f }, 0.015f, kDark);
        }
        b.Box({ 0.0f, 1.62f, -1.66f }, { 1.0f, 0.3f, 0.01f }, kGlass);                     // арын цонх
        // Нөөц дугуй ард
        b.SetTransform(MatrixMultiply(MatrixRotateY(PI * 0.5f), MatrixTranslate(0.0f, 1.25f, -2.0f)));
        BuildWheel(b, 0.4f, 0.24f, 5, false);
        b.ResetTransform();
        return b;
    }

    const MeshBuilder& JeepWheel()
    {
        static MeshBuilder b;
        if (b.Empty()) BuildWheel(b, 0.42f, 0.28f, 5, false);
        return b;
    }

    // ------------------------------------------------------------ Ачааны машин (ЗИЛ-131 маягийн, 6 дугуйтай)
    const float kTruckWheel = 0.55f;
    const float kTruckAxles[3] = { 2.3f, -1.05f, -2.45f }; // урд (жолоотой), хойд хос

    const MeshBuilder& TruckBody()
    {
        // Цул их бие (УАЗ-469-ийн загвартай адил): дотор нь хөндий биш, завсаргүй, рам нь ил гарахгүй.
        static MeshBuilder b;
        if (!b.Empty()) return b;
        const Color body = { 80, 94, 62, 255 };   // цэргийн ногоон
        const Color bodyDark = ScaleColor(body, 0.78f);
        const Color canvas = { 120, 114, 84, 255 };
        const Color wood = { 104, 92, 68, 255 };

        // --- Доод их бие: урдаас хойш нэг цул, дугуйн нүхтэй мэт хоёр талдаа бараан хаалт ---
        b.BevelBox({ 0.0f, 0.98f, -0.55f }, { 2.06f, 0.56f, 7.3f }, 0.08f, bodyDark);
        for (float z : kTruckAxles)
        {
            for (float x : { -1.04f, 1.04f })
            {
                b.BevelBox({ x, 1.24f, z }, { 0.5f, 0.16f, 1.38f }, 0.06f, kDark); // дугуйн дээрх хаалт
            }
        }

        // --- Хөдөлгүүрийн тасаг: таг, хаалтууд нэг цул ---
        b.BevelBox({ 0.0f, 1.5f, 2.62f }, { 2.36f, 0.62f, 1.62f }, 0.14f, body);       // урд хаалтууд (өргөн)
        b.BevelBox({ 0.0f, 1.86f, 2.6f }, { 1.72f, 0.5f, 1.56f }, 0.16f, body);        // хөдөлгүүрийн таг
        b.Box({ 0.0f, 2.115f, 2.6f }, { 0.07f, 0.02f, 1.4f }, bodyDark);              // тагны хавирга
        // Радиатор, гэрэл, бампер
        b.BevelBox({ 0.0f, 1.6f, 3.42f }, { 1.36f, 0.66f, 0.08f }, 0.03f, bodyDark);
        for (int i = 0; i < 9; ++i) b.Box({ -0.52f + 0.13f * (float)i, 1.6f, 3.465f }, { 0.045f, 0.56f, 0.02f }, kDark);
        for (float x : { -0.86f, 0.86f })
        {
            b.Tube({ x, 1.62f, 3.4f }, { x, 1.62f, 3.47f }, 0.16f, 0.16f, 14, kChrome);
            b.Tube({ x, 1.62f, 3.47f }, { x, 1.62f, 3.48f }, 0.13f, 0.13f, 14, kLamp);
            b.Box({ x * 1.15f, 1.32f, 3.43f }, { 0.12f, 0.07f, 0.04f }, { 220, 140, 40, 255 });
        }
        b.BevelBox({ 0.0f, 1.0f, 3.5f }, { 2.42f, 0.3f, 0.24f }, 0.06f, kDark);
        for (float x : { -0.65f, 0.65f }) b.Tube({ x, 1.02f, 3.6f }, { x, 1.02f, 3.72f }, 0.07f, 0.07f, 8, kDark);

        // --- Бүхээг: шал хүртэл цул ---
        b.BevelBox({ 0.0f, 2.2f, 1.18f }, { 2.36f, 1.9f, 1.46f }, 0.16f, body);
        b.BevelBox({ 0.0f, 3.17f, 1.18f }, { 2.2f, 0.08f, 1.34f }, 0.03f, bodyDark);  // дээвэр
        for (float x : { -0.52f, 0.52f })
        {
            b.Box({ x, 2.62f, 1.915f }, { 0.94f, 0.66f, 0.03f }, kGlass);             // салхины шил
        }
        b.Box({ 0.0f, 2.62f, 1.92f }, { 0.08f, 0.68f, 0.03f }, bodyDark);
        for (float x : { -1.185f, 1.185f })
        {
            b.Box({ x, 2.62f, 1.2f }, { 0.03f, 0.58f, 0.86f }, kGlass);                // хажуугийн цонх
            b.Box({ x, 2.0f, 1.2f }, { 0.03f, 0.04f, 0.9f }, bodyDark);               // хаалганы зураас
            b.Box({ x * 1.01f, 1.95f, 1.5f }, { 0.03f, 0.04f, 0.18f }, kChrome);       // бариул
            b.Tube({ x * 0.98f, 2.55f, 1.85f }, { x * 1.22f, 2.75f, 1.98f }, 0.016f, 0.016f, 4, kDark, false);
            b.BevelBox({ x * 1.24f, 2.82f, 1.99f }, { 0.06f, 0.3f, 0.18f }, 0.02f, kDark); // толь
            b.BevelBox({ x * 0.97f, 1.2f, 1.25f }, { 0.22f, 0.1f, 0.62f }, 0.03f, kDark);  // гишгүүр
        }
        b.Tube({ -1.0f, 2.2f, 0.5f }, { -1.0f, 3.4f, 0.5f }, 0.06f, 0.06f, 8, kDark);   // яндан
        // Түлшний сав (их биеийн хажууд)
        for (float x : { -1.08f, 1.08f }) b.Tube({ x, 1.0f, 0.2f }, { x, 1.0f, -0.75f }, 0.26f, 0.26f, 12, bodyDark);

        // --- Тэвш: шал, хажуу, ар тал нэг цул хайрцаг ---
        b.BevelBox({ 0.0f, 1.72f, -1.88f }, { 2.5f, 0.96f, 4.3f }, 0.05f, wood);
        for (float x : { -1.255f, 1.255f })
        {
            for (int i = 0; i < 2; ++i) b.Box({ x, 1.5f + 0.38f * (float)i, -1.88f }, { 0.02f, 0.04f, 4.2f }, ScaleColor(wood, 0.75f)); // банзны зураас
            for (int i = 0; i < 4; ++i) b.Box({ x * 1.004f, 1.72f, -0.15f - 1.15f * (float)i }, { 0.03f, 0.96f, 0.08f }, bodyDark);
        }
        for (float x : { -0.9f, 0.9f }) b.Box({ x, 1.4f, -4.04f }, { 0.24f, 0.14f, 0.04f }, kTailLamp);

        // --- Брезент: нуман, хоёр үзүүрээрээ битүү (хаалттай хэлбэр) ---
        {
            const float zFront = 0.3f, zBack = -4.0f, base = 2.2f;
            const Vector2 profile[] = { { -1.24f, base }, { -1.24f, 2.95f }, { -1.12f, 3.18f }, { -0.82f, 3.33f }, { -0.42f, 3.4f },
                                        { 0.0f, 3.42f }, { 0.42f, 3.4f }, { 0.82f, 3.33f }, { 1.12f, 3.18f }, { 1.24f, 2.95f },
                                        { 1.24f, base } };
            const int count = (int)(sizeof(profile) / sizeof(profile[0]));
            for (int i = 0; i + 1 < count; ++i)
            {
                const Vector2 p = profile[i], q = profile[i + 1];
                const Color c = ScaleColor(canvas, 0.88f + 0.12f * std::min(p.y, q.y) / 3.42f);
                b.Quad({ p.x, p.y, zBack }, { q.x, q.y, zBack }, { q.x, q.y, zFront }, { p.x, p.y, zFront }, c);
                // Урд, ар талын битүү таг (хэлбэрийн төвөөс)
                b.Triangle({ 0.0f, 2.8f, zFront }, { p.x, p.y, zFront }, { q.x, q.y, zFront }, ScaleColor(canvas, 0.84f));
                b.Triangle({ 0.0f, 2.8f, zBack }, { q.x, q.y, zBack }, { p.x, p.y, zBack }, ScaleColor(canvas, 0.84f));
                // Хавирга
                for (float z : { -0.7f, -1.6f, -2.5f, -3.4f })
                {
                    const float k = 1.012f;
                    b.Quad({ p.x * k, base + (p.y - base) * k, z - 0.05f }, { q.x * k, base + (q.y - base) * k, z - 0.05f },
                           { q.x * k, base + (q.y - base) * k, z + 0.05f }, { p.x * k, base + (p.y - base) * k, z + 0.05f },
                           ScaleColor(canvas, 0.8f));
                }
            }
            // Ар талын хормой (уясан брезент), олс
            b.Box({ 0.0f, 2.62f, zBack - 0.02f }, { 2.2f, 0.8f, 0.03f }, ScaleColor(canvas, 0.7f));
            for (float x : { -0.7f, 0.0f, 0.7f }) b.Box({ x, 2.62f, zBack - 0.04f }, { 0.04f, 0.82f, 0.02f }, ScaleColor(canvas, 0.5f));
            for (float x : { -1.255f, 1.255f }) b.Box({ x, 2.24f, (zFront + zBack) * 0.5f }, { 0.03f, 0.05f, zFront - zBack }, ScaleColor(canvas, 0.55f));
        }
        // Шавар хаалт
        for (float x : { -1.0f, 1.0f }) b.Box({ x, 0.82f, -3.25f }, { 0.5f, 0.42f, 0.04f }, kDark);
        return b;
    }

    const MeshBuilder& TruckWheel()
    {
        static MeshBuilder b;
        if (b.Empty()) BuildWheel(b, kTruckWheel, 0.36f, 6, false);
        return b;
    }

    void DrawTruck(const Vehicle& v)
    {
        // Текстуртай загвар байвал түүгээр; эс бөгөөс кодоор угсарсан ЗИЛ-131
        if (HasVehicleModel(VehicleModelId::Truck))
        {
            const Color tint = propMaterial && !modelDepthMaterial ? propMaterial->maps[MATERIAL_MAP_DIFFUSE].color : WHITE;
            DrawVehicleModel(VehicleModelId::Truck, v.animation, v.steer, modelDepthMaterial, tint);
            return;
        }
        DrawProp(TruckBody());
        const float spin = v.animation / kTruckWheel;
        for (float x : { -1.02f, 1.02f })
        {
            for (int axle = 0; axle < 3; ++axle)
            {
                rlPushMatrix();
                rlTranslatef(x, kTruckWheel, kTruckAxles[axle]);
                if (axle == 0) rlRotatef(-v.steer * 28.0f, 0.0f, 1.0f, 0.0f);
                rlRotatef(spin * RAD2DEG, 1.0f, 0.0f, 0.0f);
                DrawProp(TruckWheel());
                rlPopMatrix();
            }
        }
    }

    // Нэг удаа угсарсан mesh-ийг (x, y, z) байрлалд X тэнхлэгээр spin эргүүлж зурна.
    void DrawSpinning(const MeshBuilder& b, float x, float y, float z, float spin)
    {
        rlPushMatrix();
        rlTranslatef(x, y, z);
        rlRotatef(spin * RAD2DEG, 1.0f, 0.0f, 0.0f);
        DrawProp(b);
        rlPopMatrix();
    }

    void DrawJeep(const Vehicle& v)
    {
        // Текстуртай загвар (G-Class, X-Trail, Yaris) байвал түүгээр; эс бөгөөс кодоор угсарсан УАЗ
        const VehicleModelId model = v.type == VehicleType::Suv         ? VehicleModelId::Suv
                                     : v.type == VehicleType::Hatchback ? VehicleModelId::Hatchback
                                                                        : VehicleModelId::Jeep;
        if (HasVehicleModel(model))
        {
            const Color tint = propMaterial && !modelDepthMaterial ? propMaterial->maps[MATERIAL_MAP_DIFFUSE].color : WHITE;
            DrawVehicleModel(model, v.animation, v.steer, modelDepthMaterial, tint);
            return;
        }
        DrawProp(JeepBody());
        const float spin = v.animation / 0.42f;
        for (float x : { -0.86f, 0.86f })
        {
            for (float z : { -1.2f, 1.2f })
            {
                rlPushMatrix();
                rlTranslatef(x, 0.42f, z);
                if (z > 0.0f) rlRotatef(-v.steer * 30.0f, 0.0f, 1.0f, 0.0f); // урд дугуйнууд жолоогоор эргэнэ
                rlRotatef(spin * RAD2DEG, 1.0f, 0.0f, 0.0f);
                DrawProp(JeepWheel());
                rlPopMatrix();
            }
        }
    }

    // ------------------------------------------------------------ Мотоцикл, гурван дугуйт
    const MeshBuilder& MotoWheel()
    {
        static MeshBuilder b;
        if (b.Empty()) BuildWheel(b, 0.34f, 0.1f, 12, true);
        return b;
    }

    // Урд хэсэг: сэрээ, жолоо (Монгол унаачдын өнгөт цацагтай), гэрэл, түлшний сав, суудал, рам.
    // Жолооны тэнхлэг: урд хэсэг (сэрээ, жолоо, гэрэл, урд дугуй) эндээс босоо тэнхлэгээр эргэнэ.
    const float kMotoSteerZ = 0.6f;

    // Урд хэсэг: эргэдэг хэсэг (сэрээ, хаалт, жолоо, гэрэл) нь s-д, бусад нь b-д.
    void BuildMotoFront(MeshBuilder& s, MeshBuilder& b, Color paint, float seatZ)
    {
        const Color frame = ScaleColor(paint, 0.55f);
        // Сэрээ: жолооны тулгуураас урд дугуйн тэнхлэг хүртэл
        for (float x : { -0.075f, 0.075f })
        {
            s.Tube({ x, 1.08f, 0.52f }, { x, 0.34f, 0.72f }, 0.025f, 0.022f, 8, kChrome);
        }
        s.BevelBox({ 0.0f, 1.0f, 0.55f }, { 0.2f, 0.06f, 0.1f }, 0.02f, kDark);
        // Урд шаврын хаалт: дугуйн дээгүүр нуман
        for (int i = 0; i < 6; ++i)
        {
            const float a0 = -0.9f + 1.5f * (float)i / 6.0f, a1 = -0.9f + 1.5f * (float)(i + 1) / 6.0f;
            s.Tube({ 0.0f, 0.34f + std::cos(a0) * 0.42f, 0.72f + std::sin(a0) * 0.42f },
                   { 0.0f, 0.34f + std::cos(a1) * 0.42f, 0.72f + std::sin(a1) * 0.42f }, 0.07f, 0.07f, 6, paint, false);
        }
        // Жолоо, бариул, толь, өнгөт цацаг
        s.Tube({ -0.36f, 1.16f, 0.48f }, { 0.36f, 1.16f, 0.48f }, 0.016f, 0.016f, 6, kChrome);
        s.Tube({ 0.0f, 1.08f, 0.52f }, { 0.0f, 1.16f, 0.48f }, 0.03f, 0.03f, 6, kChrome);
        const Color tassels[3] = { { 196, 36, 40, 255 }, { 236, 190, 40, 255 }, { 40, 90, 170, 255 } };
        for (float side : { -1.0f, 1.0f })
        {
            s.Tube({ side * 0.26f, 1.16f, 0.48f }, { side * 0.38f, 1.16f, 0.48f }, 0.026f, 0.026f, 8, kSeat);
            s.Tube({ side * 0.2f, 1.17f, 0.48f }, { side * 0.26f, 1.38f, 0.46f }, 0.008f, 0.008f, 4, kChrome, false);
            s.Ellipsoid({ side * 0.27f, 1.41f, 0.46f }, { 0.05f, 0.035f, 0.015f }, 4, 6, kDark);
            for (int k = 0; k < 3; ++k)
            {
                s.Box({ side * (0.38f + 0.012f * (float)k), 1.08f - 0.02f * (float)k, 0.48f }, { 0.012f, 0.14f, 0.03f },
                      tassels[k]);
            }
        }
        // Гэрэл
        s.Tube({ 0.0f, 0.98f, 0.62f }, { 0.0f, 0.98f, 0.76f }, 0.08f, 0.1f, 12, kChrome);
        s.Tube({ 0.0f, 0.98f, 0.76f }, { 0.0f, 0.98f, 0.77f }, 0.085f, 0.085f, 12, kLamp);
        // Рам: жолооны тулгуураас хөдөлгүүр, суудлын ард хүртэл
        b.Tube({ 0.0f, 1.0f, 0.5f }, { 0.0f, 0.82f, 0.0f }, 0.035f, 0.035f, 6, frame);
        b.Tube({ 0.0f, 1.0f, 0.5f }, { 0.0f, 0.42f, 0.25f }, 0.03f, 0.03f, 6, frame);
        b.Tube({ 0.0f, 0.82f, 0.0f }, { 0.0f, 0.84f, seatZ - 0.42f }, 0.03f, 0.03f, 6, frame);
        // Түлшний сав: дугуй хэлбэртэй, хажуудаа өнгөт зураастай
        b.Ellipsoid({ 0.0f, 0.93f, 0.2f }, { 0.17f, 0.12f, 0.3f }, 6, 10, paint);
        b.Ellipsoid({ 0.0f, 0.93f, 0.2f }, { 0.172f, 0.03f, 0.22f }, 4, 10, { 230, 220, 200, 255 });
        b.Tube({ 0.0f, 1.04f, 0.26f }, { 0.0f, 1.06f, 0.26f }, 0.04f, 0.04f, 8, kChrome);
        // Суудал: зөөлөн, хоёр давхар
        b.BevelBox({ 0.0f, 0.88f, seatZ }, { 0.3f, 0.1f, 0.62f }, 0.04f, kSeat);
        b.BevelBox({ 0.0f, 0.82f, seatZ }, { 0.32f, 0.05f, 0.64f }, 0.02f, ScaleColor(paint, 0.7f));
    }

    // Хөдөлгүүр, яндан (мотоцикл, гурван дугуйт)
    void BuildMotoEngine(MeshBuilder& b, float z)
    {
        b.BevelBox({ 0.0f, 0.48f, z }, { 0.26f, 0.28f, 0.38f }, 0.05f, { 70, 72, 76, 255 });
        // Цилиндрийн сэрвээ: урагш налуу
        for (int i = 0; i < 4; ++i)
        {
            b.BevelBox({ 0.0f, 0.62f + 0.04f * (float)i, z + 0.12f + 0.03f * (float)i }, { 0.22f, 0.02f, 0.18f }, 0.008f,
                       { 110, 112, 116, 255 });
        }
        // Хажуугийн дугуй таг
        b.Tube({ -0.13f, 0.46f, z - 0.04f }, { -0.16f, 0.46f, z - 0.04f }, 0.11f, 0.09f, 12, kChrome);
        // Яндан: хөдөлгүүрээс доош, баруун талаар хойш, төгсгөлдөө том дуу намсгагч
        b.Tube({ 0.08f, 0.62f, z + 0.18f }, { 0.12f, 0.36f, z + 0.12f }, 0.025f, 0.025f, 6, kChrome);
        b.Tube({ 0.12f, 0.36f, z + 0.12f }, { 0.17f, 0.36f, z - 0.4f }, 0.025f, 0.025f, 6, kChrome);
        b.Tube({ 0.17f, 0.36f, z - 0.4f }, { 0.17f, 0.42f, z - 0.82f }, 0.05f, 0.04f, 10, kChrome);
        // Хөлийн тулгуур
        b.Tube({ -0.24f, 0.32f, z - 0.05f }, { 0.24f, 0.32f, z - 0.05f }, 0.015f, 0.015f, 4, kDark);
    }

    MeshBuilder motorcycleSteer, trikeSteer; // эргэдэг урд хэсэг (биеийн хамт үүснэ)

    const MeshBuilder& MotorcycleBody()
    {
        static MeshBuilder b;
        if (!b.Empty()) return b;
        const Color paint = { 160, 34, 30, 255 };
        BuildMotoFront(motorcycleSteer, b, paint, -0.34f);
        BuildMotoEngine(b, 0.08f);
        // Арын сэрээ, амортизатор
        for (float x : { -0.09f, 0.09f })
        {
            b.Tube({ x, 0.42f, -0.05f }, { x, 0.34f, -0.72f }, 0.025f, 0.02f, 6, kDark);
            b.Tube({ x * 1.1f, 0.84f, -0.56f }, { x, 0.38f, -0.68f }, 0.022f, 0.022f, 6, kChrome);
        }
        // Арын шаврын хаалт, тээшний тавиур, гэрэл
        for (int i = 0; i < 5; ++i)
        {
            const float a0 = -0.2f - 1.3f * (float)i / 5.0f, a1 = -0.2f - 1.3f * (float)(i + 1) / 5.0f;
            b.Tube({ 0.0f, 0.34f + std::cos(a0) * 0.42f, -0.72f + std::sin(a0) * 0.42f },
                   { 0.0f, 0.34f + std::cos(a1) * 0.42f, -0.72f + std::sin(a1) * 0.42f }, 0.075f, 0.075f, 6, paint, false);
        }
        b.BevelBox({ 0.0f, 0.86f, -0.76f }, { 0.26f, 0.03f, 0.3f }, 0.01f, kChrome);
        b.BevelBox({ 0.0f, 0.82f, -0.94f }, { 0.12f, 0.07f, 0.04f }, 0.015f, kTailLamp);
        // Хажуугийн таг
        for (float x : { -0.14f, 0.14f })
        {
            b.BevelBox({ x, 0.66f, -0.22f }, { 0.04f, 0.16f, 0.24f }, 0.02f, paint);
        }
        return b;
    }

    // Урд хэсэг: жолооны тэнхлэгээр эргүүлж, дугуйг нь хамт зурна (D = баруун = -X тал руу).
    void DrawMotoSteering(const MeshBuilder& steer, const Vehicle& v, float spin)
    {
        rlPushMatrix();
        rlTranslatef(0.0f, 0.0f, kMotoSteerZ);
        rlRotatef(-v.steer * 28.0f, 0.0f, 1.0f, 0.0f);
        rlTranslatef(0.0f, 0.0f, -kMotoSteerZ);
        DrawProp(steer);
        DrawSpinning(MotoWheel(), 0.0f, 0.34f, 0.72f, spin);
        rlPopMatrix();
    }

    void DrawMotorcycle(const Vehicle& v)
    {
        DrawProp(MotorcycleBody());
        const float spin = v.animation / 0.34f;
        DrawMotoSteering(motorcycleSteer, v, spin);
        DrawSpinning(MotoWheel(), 0.0f, 0.34f, -0.72f, spin);
    }

    const MeshBuilder& TrikeBody()
    {
        static MeshBuilder b;
        if (!b.Empty()) return b;
        const Color paint = { 44, 88, 146, 255 };
        const Color plank = { 58, 102, 156, 255 };
        BuildMotoFront(trikeSteer, b, paint, -0.12f);
        BuildMotoEngine(b, 0.12f);
        // Рам, арын тэнхлэг
        b.BevelBox({ 0.0f, 0.46f, -0.75f }, { 0.16f, 0.1f, 1.1f }, 0.03f, kDark);
        b.Tube({ -0.62f, 0.34f, -0.85f }, { 0.62f, 0.34f, -0.85f }, 0.04f, 0.04f, 8, kDark);
        // Ачааны тэвш: шал, банзан хана, хавирга, ирмэгийн хүрээ
        b.BevelBox({ 0.0f, 0.6f, -0.9f }, { 1.3f, 0.06f, 1.2f }, 0.02f, ScaleColor(plank, 0.8f));
        for (float x : { -0.64f, 0.64f })
        {
            for (int k = 0; k < 3; ++k)
            {
                b.BevelBox({ x, 0.7f + 0.12f * (float)k, -0.9f }, { 0.04f, 0.1f, 1.2f }, 0.01f, plank);
            }
            for (float z : { -1.46f, -1.1f, -0.7f, -0.34f })
            {
                b.Box({ x * 1.04f, 0.8f, z }, { 0.03f, 0.4f, 0.05f }, ScaleColor(paint, 0.7f));
            }
            // Арын дугуйн хаалт
            b.BevelBox({ x * 0.98f, 0.66f, -0.85f }, { 0.22f, 0.06f, 0.62f }, 0.02f, ScaleColor(paint, 0.8f));
        }
        for (int k = 0; k < 3; ++k)
        {
            b.BevelBox({ 0.0f, 0.7f + 0.12f * (float)k, -1.48f }, { 1.3f, 0.1f, 0.04f }, 0.01f, plank);
            b.BevelBox({ 0.0f, 0.7f + 0.12f * (float)k, -0.3f }, { 1.3f, 0.1f, 0.04f }, 0.01f, plank);
        }
        b.BevelBox({ 0.0f, 0.95f, -0.3f }, { 1.36f, 0.04f, 0.06f }, 0.01f, kChrome);
        // Ачаа: шуудай, сав
        b.BevelBox({ 0.25f, 0.78f, -0.95f }, { 0.5f, 0.3f, 0.5f }, 0.1f, { 150, 128, 96, 255 });
        b.Tube({ -0.3f, 0.63f, -1.1f }, { -0.3f, 0.95f, -1.1f }, 0.14f, 0.14f, 10, { 70, 110, 70, 255 });
        for (float x : { -0.5f, 0.5f })
        {
            b.Box({ x, 0.62f, -1.52f }, { 0.12f, 0.08f, 0.03f }, kTailLamp);
        }
        return b;
    }

    void DrawTrike(const Vehicle& v)
    {
        DrawProp(TrikeBody());
        const float spin = v.animation / 0.34f;
        DrawMotoSteering(trikeSteer, v, spin);
        DrawSpinning(MotoWheel(), -0.62f, 0.34f, -0.85f, spin);
        DrawSpinning(MotoWheel(), 0.62f, 0.34f, -0.85f, spin);
    }

    // ------------------------------------------------------------ Монгол морь
    const Color kCoat = { 128, 84, 50, 255 };    // хүрэн зээрд
    const Color kMane = { 40, 30, 24, 255 };
    const Color kHoof = { 36, 32, 30, 255 };

    // Их бие, эмээл, тохом: хөдлөхгүй хэсэг
    const MeshBuilder& HorseBody()
    {
        static MeshBuilder b;
        if (!b.Empty()) return b;
        const Color belly = ScaleColor(kCoat, 1.1f);
        b.Ellipsoid({ 0.0f, 1.16f, 0.0f }, { 0.27f, 0.3f, 0.6f }, 8, 14, kCoat);      // гэдэс
        b.Ellipsoid({ 0.0f, 1.22f, 0.5f }, { 0.25f, 0.31f, 0.32f }, 8, 12, kCoat);    // цээж, мөр
        b.Ellipsoid({ 0.0f, 1.24f, -0.52f }, { 0.28f, 0.3f, 0.34f }, 8, 12, kCoat);   // ууц
        b.Ellipsoid({ 0.0f, 1.02f, 0.05f }, { 0.22f, 0.12f, 0.45f }, 6, 10, belly);
        // Мөр, гуяны булчин (хөл эндээс эхэлнэ)
        for (float x : { -0.17f, 0.17f })
        {
            b.Ellipsoid({ x, 1.1f, 0.56f }, { 0.11f, 0.22f, 0.15f }, 6, 8, kCoat);
            b.Ellipsoid({ x, 1.14f, -0.58f }, { 0.13f, 0.24f, 0.2f }, 6, 8, kCoat);
        }
        return b;
    }

    // Монгол эмээл, тохом, дөрөө, бүслүүр (кодоор үүсгэсэн морь ч, ястай загвар ч нуруун дээрээ үүрнэ)
    const MeshBuilder& HorseSaddle()
    {
        static MeshBuilder b;
        if (!b.Empty()) return b;
        // Монгол эмээл: өнгөт тохом, улаан суудал, өндөр урд хойд бүүрэг мөнгөн товруутай, дөрөө
        const Color felt = { 36, 66, 128, 255 }, trim = { 220, 170, 50, 255 };
        const Color saddle = { 150, 40, 34, 255 }, wood = { 120, 66, 36, 255 }, silver = { 210, 210, 214, 255 };
        b.BevelBox({ 0.0f, 1.44f, -0.06f }, { 0.62f, 0.05f, 0.72f }, 0.02f, felt);
        b.BevelBox({ 0.0f, 1.435f, -0.06f }, { 0.64f, 0.04f, 0.74f }, 0.02f, trim);
        for (float x : { -0.3f, 0.3f })
        {
            b.BevelBox({ x, 1.3f, -0.06f }, { 0.03f, 0.28f, 0.66f }, 0.012f, felt);       // тохомын унжлага
        }
        b.BevelBox({ 0.0f, 1.5f, -0.06f }, { 0.4f, 0.08f, 0.48f }, 0.03f, saddle);
        // Урд, хойд бүүрэг: нарийн өндөр модон хавтан, гадна талдаа мөнгөн товруутай
        for (float z : { 0.2f, -0.32f })
        {
            const float lean = z > 0 ? 1.0f : -1.0f;
            Matrix tilt = MatrixMultiply(MatrixMultiply(MatrixTranslate(0.0f, -1.52f, -z), MatrixRotateX(-10.0f * lean * DEG2RAD)),
                                         MatrixTranslate(0.0f, 1.52f, z));
            b.SetTransform(tilt);
            b.BevelBox({ 0.0f, 1.63f, z }, { 0.3f, 0.24f, 0.06f }, 0.03f, wood);
            b.BevelBox({ 0.0f, 1.755f, z }, { 0.24f, 0.03f, 0.07f }, 0.012f, saddle);
            for (int k = 0; k < 3; ++k)
            {
                b.Ellipsoid({ -0.08f + 0.08f * (float)k, 1.64f, z + 0.035f * lean }, { 0.022f, 0.022f, 0.012f }, 3, 6, silver);
            }
            b.ResetTransform();
        }
        // Олом, дөрөө
        for (float x : { -0.24f, 0.24f })
        {
            b.Tube({ x, 1.46f, -0.04f }, { x * 1.3f, 1.0f, -0.04f }, 0.012f, 0.012f, 4, { 60, 40, 28, 255 }, false);
            b.Tube({ x * 1.3f - 0.06f, 0.98f, -0.04f }, { x * 1.3f + 0.06f, 0.98f, -0.04f }, 0.015f, 0.015f, 6, silver);
        }
        b.Tube({ -0.27f, 1.0f, 0.08f }, { 0.27f, 1.0f, 0.08f }, 0.03f, 0.03f, 6, { 60, 40, 28, 255 }, false); // бүслүүр
        return b;
    }

    // Хүзүү, толгой: хүзүүний суурь (0, 0, 0) -ээс урагш дээш; сунжаа, дэл, хазаартай.
    const MeshBuilder& HorseHead()
    {
        static MeshBuilder b;
        if (!b.Empty()) return b;
        const Vector3 base = { 0.0f, 0.0f, 0.0f }, top = { 0.0f, 0.46f, 0.32f };
        b.Tube(base, top, 0.2f, 0.12f, 10, kCoat);
        b.Ellipsoid({ 0.0f, 0.04f, 0.04f }, { 0.2f, 0.22f, 0.2f }, 6, 10, kCoat);
        // Өтгөн дэл: хүзүүний орой дагуу
        for (int i = 0; i <= 8; ++i)
        {
            const float t = (float)i / 8.0f;
            const Vector3 p = Vector3Lerp({ 0.0f, 0.16f, -0.12f }, { 0.0f, 0.56f, 0.26f }, t);
            b.BevelBox(p, { 0.07f, 0.16f, 0.1f }, 0.025f, kMane);
        }
        // Толгой: дух, хацар, хошуу (доош урагш)
        const Vector3 poll = { 0.0f, 0.52f, 0.36f }, muzzle = { 0.0f, 0.2f, 0.62f };
        b.Ellipsoid({ 0.0f, 0.5f, 0.38f }, { 0.11f, 0.12f, 0.13f }, 6, 8, kCoat);
        b.Tube(poll, muzzle, 0.1f, 0.075f, 10, kCoat);
        b.Ellipsoid(muzzle, { 0.08f, 0.075f, 0.09f }, 6, 8, ScaleColor(kCoat, 0.55f));
        b.Ellipsoid({ 0.0f, 0.38f, 0.42f }, { 0.07f, 0.09f, 0.06f }, 4, 6, ScaleColor(kCoat, 0.9f)); // хацар
        for (float x : { -0.065f, 0.065f })
        {
            b.Tube({ x, 0.6f, 0.34f }, { x * 1.4f, 0.74f, 0.3f }, 0.03f, 0.0f, 6, kCoat);            // чих
            b.Ellipsoid({ x * 1.45f, 0.48f, 0.44f }, { 0.018f, 0.022f, 0.02f }, 3, 5, { 20, 16, 14, 255 }); // нүд
            // Хазаар: хамрын, хацрын оосор
            b.Tube({ x * 1.3f, 0.52f, 0.36f }, { x * 1.25f, 0.24f, 0.58f }, 0.012f, 0.012f, 4, { 70, 40, 26, 255 }, false);
        }
        b.Tube({ -0.09f, 0.3f, 0.56f }, { 0.09f, 0.3f, 0.56f }, 0.014f, 0.014f, 4, { 70, 40, 26, 255 }, false);
        b.BevelBox({ 0.0f, 0.6f, 0.4f }, { 0.06f, 0.08f, 0.1f }, 0.02f, kMane);                    // хөхөл
        // Жолоо: амнаас эмээлийн урд руу
        for (float x : { -0.08f, 0.08f })
        {
            b.Tube({ x, 0.22f, 0.6f }, { x * 2.0f, 0.1f, -0.1f }, 0.008f, 0.008f, 4, { 70, 40, 26, 255 }, false);
        }
        return b;
    }

    // Сүүл: эхлэх цэг (0, 0, 0)-оос доош урсана.
    const MeshBuilder& HorseTail()
    {
        static MeshBuilder b;
        if (!b.Empty()) return b;
        b.Tube({ 0.0f, 0.0f, 0.0f }, { 0.0f, -0.25f, -0.06f }, 0.06f, 0.08f, 8, kMane);
        b.Tube({ 0.0f, -0.25f, -0.06f }, { 0.0f, -0.62f, -0.04f }, 0.08f, 0.05f, 8, kMane);
        b.Tube({ 0.0f, -0.62f, -0.04f }, { 0.0f, -0.78f, 0.02f }, 0.05f, 0.01f, 8, kMane);
        return b;
    }

    // Хөлийн дээд хэсэг (шуу / шилбэ): үенээс доош 0.48 м
    const MeshBuilder& HorseUpperLeg(bool hind)
    {
        static MeshBuilder front, back;
        MeshBuilder& b = hind ? back : front;
        if (!b.Empty()) return b;
        // Монгол адуу: намхан, бүдүүн хөлтэй
        b.Tube({ 0.0f, 0.04f, 0.0f }, { 0.0f, -0.48f, hind ? -0.04f : 0.0f }, hind ? 0.12f : 0.1f, 0.068f, 8, kCoat);
        b.Ellipsoid({ 0.0f, -0.48f, hind ? -0.04f : 0.0f }, { 0.072f, 0.08f, 0.075f }, 4, 6, kCoat); // өвдөг / тахилзуур
        return b;
    }

    // Хөлийн доод хэсэг (шагай, туурай): өвдөгнөөс доош 0.46 м
    const MeshBuilder& HorseLowerLeg()
    {
        static MeshBuilder b;
        if (!b.Empty()) return b;
        b.Tube({ 0.0f, 0.0f, 0.0f }, { 0.0f, -0.36f, 0.0f }, 0.058f, 0.05f, 8, kCoat);
        b.Ellipsoid({ 0.0f, -0.37f, 0.01f }, { 0.062f, 0.06f, 0.066f }, 4, 6, kCoat);              // шилбэний үе
        b.Tube({ 0.0f, -0.38f, 0.02f }, { 0.0f, -0.46f, 0.04f }, 0.06f, 0.075f, 8, kHoof);        // туурай
        b.Tube({ 0.0f, -0.35f, 0.0f }, { 0.0f, -0.42f, -0.03f }, 0.072f, 0.05f, 8, ScaleColor(kMane, 1.2f)); // туурайн үс
        return b;
    }

    void DrawHorse(const Vehicle& v)
    {
        // (газар дээрээ эргэхэд ч хөлөө бага зэрэг өргөж гишгэлнэ)
        const float gait = std::max(std::min(1.0f, std::fabs(v.speed) / 4.0f), std::fabs(v.steer) * 0.35f);
        // Давхилт: биеэрээ урагш хойш найган, илүү өндөр дэгдэнэ
        const float gallop = SmoothStep(9.0f, 13.0f, std::fabs(v.speed));
        const float stride = HorseStridePhase(v.animation);
        // Давхилт: бүх хөл агаарт байх үед (p ~0.9) хамгийн өндөр, урд хөл газарт тулах үед нам;
        // хойд хөл түлхэхэд хошуу дээш, урд хөл буухад хошуу доош найгана.
        const float trotBounce = std::sin(v.animation * 2.0f) * 0.05f;
        const float gallopBounce = 0.09f * std::cos(2.0f * PI * (stride - 0.9f));
        const float bounce = (trotBounce + (gallopBounce - trotBounce) * gallop) * gait;
        const float rock = -6.0f * std::sin(2.0f * PI * (stride - 0.05f)) * gallop;

        rlPushMatrix();
        rlTranslatef(0.0f, bounce, 0.0f);
        rlTranslatef(0.0f, 1.0f, 0.0f); // биеийн дундаас найгана
        rlRotatef(rock, 1.0f, 0.0f, 0.0f);
        rlTranslatef(0.0f, -1.0f, 0.0f);
        if (HasHorseModel())
        {
            // Ястай загвар (assets/horse_mesh.glb): хөл, хүзүү, сүүлийг HorseModel хөдөлгөнө. Эмээл, хазаар,
            // дөрөө нь загварын текстурт байгаа тул кодоор үүсгэсэн эмээлийг зурахгүй.
            DrawHorseModel(v.animation, gait, v.steer, v.position.x * 0.37f + v.position.z * 0.61f, gallop);
            rlPopMatrix();
            return;
        }
        DrawProp(HorseBody());
        DrawProp(HorseSaddle());

        // Хүзүү, толгой: давхихад дээш доош толгой дохино
        rlPushMatrix();
        rlTranslatef(0.0f, 1.32f, 0.68f);
        rlRotatef(-v.steer * 22.0f, 0.0f, 1.0f, 0.0f); // эргэх зүг рүүгээ толгойгоо эргүүлнэ
        rlRotatef(-6.0f + std::sin(v.animation * 2.0f + 0.6f) * 6.0f * gait - gait * 8.0f, 1.0f, 0.0f, 0.0f);
        DrawProp(HorseHead());
        rlPopMatrix();

        // Сүүл
        rlPushMatrix();
        rlTranslatef(0.0f, 1.4f, -0.84f);
        rlRotatef(18.0f + gait * 34.0f + std::sin(v.animation * 2.0f) * 6.0f, 1.0f, 0.0f, 0.0f); // хойш, ууцаас холдоно
        DrawProp(HorseTail());
        rlPopMatrix();

        // Хөл: урд, хойд хос ээлжлэн савлана
        const float swing = 34.0f * gait;
        const struct { float x, z, phase; bool hind; } legs[] = {
            { -0.15f, 0.56f, 0.0f, false }, { 0.15f, 0.56f, 0.5f, false },
            { -0.16f, -0.6f, 0.6f, true }, { 0.16f, -0.6f, 0.1f, true },
        };
        for (const auto& leg : legs)
        {
            const float angle = std::sin(v.animation + leg.phase * 2.0f * PI) * swing;
            const float knee = std::max(0.0f, std::cos(v.animation + leg.phase * 2.0f * PI)) * swing * 0.9f;
            rlPushMatrix();
            rlTranslatef(leg.x, 0.98f - bounce, leg.z);
            rlRotatef(-angle, 1.0f, 0.0f, 0.0f);
            DrawProp(HorseUpperLeg(leg.hind));
            rlTranslatef(0.0f, -0.48f, leg.hind ? -0.04f : 0.0f);
            rlRotatef(leg.hind ? -knee : knee, 1.0f, 0.0f, 0.0f);
            DrawProp(HorseLowerLeg());
            rlPopMatrix();
        }

        rlPopMatrix();
    }
}

namespace
{
    // Гэмтлийг үржүүлэх коэффициент: ачааны машин бат бөх, мотоцикл, морь эмзэг
    float ArmorFactor(VehicleType type)
    {
        switch (type)
        {
        case VehicleType::Truck: return 0.45f;
        case VehicleType::Jeep: return 1.0f;
        case VehicleType::Trike: return 1.1f;
        case VehicleType::Motorcycle: return 1.3f;
        case VehicleType::Horse: return 1.2f;
        case VehicleType::Suv: return 1.05f;
        case VehicleType::Hatchback: return 1.25f;
        }
        return 1.0f;
    }

    // Мөргөлдөөний хэлбэр: уртаараа сунасан капсул (хагас урт, хагас өргөн)
    Vector2 CapsuleOf(VehicleType type)
    {
        switch (type)
        {
        case VehicleType::Truck: return { 3.0f, 1.25f };
        case VehicleType::Jeep: return { 1.5f, 0.92f };
        case VehicleType::Trike: return { 1.0f, 0.7f };
        case VehicleType::Motorcycle: return { 0.75f, 0.38f };
        case VehicleType::Horse: return { 0.85f, 0.42f };
        case VehicleType::Suv: return { 1.45f, 0.9f };       // 4.69 м урт
        case VehicleType::Hatchback: return { 1.15f, 0.85f }; // 3.94 м урт
        }
        return { 1.0f, 0.8f };
    }

    // Хоёр хэрчмийн хамгийн ойр цэгүүд (xz)
    void ClosestPoints(Vector2 p1, Vector2 q1, Vector2 p2, Vector2 q2, Vector2& c1, Vector2& c2)
    {
        const Vector2 d1 = Vector2Subtract(q1, p1), d2 = Vector2Subtract(q2, p2), r = Vector2Subtract(p1, p2);
        const float a = Vector2DotProduct(d1, d1), e = Vector2DotProduct(d2, d2), f = Vector2DotProduct(d2, r);
        float s = 0.0f, t = 0.0f;
        const float c = Vector2DotProduct(d1, r), b = Vector2DotProduct(d1, d2);
        const float denom = a * e - b * b;
        if (denom > 1e-6f) s = std::clamp((b * f - c * e) / denom, 0.0f, 1.0f);
        t = e > 1e-6f ? (b * s + f) / e : 0.0f;
        if (t < 0.0f) { t = 0.0f; s = a > 1e-6f ? std::clamp(-c / a, 0.0f, 1.0f) : 0.0f; }
        else if (t > 1.0f) { t = 1.0f; s = a > 1e-6f ? std::clamp((b - c) / a, 0.0f, 1.0f) : 0.0f; }
        c1 = Vector2Add(p1, Vector2Scale(d1, s));
        c2 = Vector2Add(p2, Vector2Scale(d2, t));
    }
}

void Garage::Collide(int index, const World& world)
{
    // Тээврийн хэрэгсэл хоорондын мөргөлдөөн: хүнд нь хөнгөнийг түлхэж, харьцангуй хурд ба жингээс хамааран
    // хоёулаа гэмтэнэ. Мөргүүлсэн нь хажуу тийш шидэгдэж эргэлдэнэ.
    Vehicle& a = vehicles[(size_t)index];
    const VehicleSpec& sa = SpecFor(a.type);
    for (int j = 0; j < Count(); ++j)
    {
        if (j == index) continue;
        Vehicle& b = vehicles[(size_t)j];
        const VehicleSpec& sb = SpecFor(b.type);
        const float far = sa.radius + sb.radius + 4.0f;
        if (std::fabs(b.position.x - a.position.x) > far || std::fabs(b.position.z - a.position.z) > far) continue;
        // Капсулуудын тэнхлэгийн хамгийн ойр цэгүүд
        const Vector2 ca = CapsuleOf(a.type), cb = CapsuleOf(b.type);
        const Vector2 fa = { std::sin(a.heading), std::cos(a.heading) }, fb = { std::sin(b.heading), std::cos(b.heading) };
        const Vector2 pa = { a.position.x, a.position.z }, pb = { b.position.x, b.position.z };
        Vector2 qa, qb;
        ClosestPoints(Vector2Subtract(pa, Vector2Scale(fa, ca.x)), Vector2Add(pa, Vector2Scale(fa, ca.x)),
                      Vector2Subtract(pb, Vector2Scale(fb, cb.x)), Vector2Add(pb, Vector2Scale(fb, cb.x)), qa, qb);
        const float dx = qb.x - qa.x, dz = qb.y - qa.y;
        const float distance = std::sqrt(dx * dx + dz * dz);
        const float minDistance = ca.y + cb.y;
        if (distance >= minDistance) continue;

        Vector2 n = distance > 1e-4f ? Vector2{ dx / distance, dz / distance } : Vector2Normalize(Vector2Subtract(pb, pa));
        // Урдаас нь мөргөхөд хөнгөн нь урагш чирэгдэхгүй, хажуу тийш шидэгдэнэ
        const float frontal = Vector2DotProduct(n, fa);
        if (frontal > 0.5f && a.speed > 1.0f)
        {
            const Vector2 side = { fa.y, -fa.x };
            const float sign = Vector2DotProduct(Vector2Subtract(pb, pa), side) >= 0.0f ? 1.0f : -1.0f;
            n = Vector2Normalize(Vector2Add(n, Vector2Scale(side, sign * 0.9f)));
        }
        const Vector2 va = { std::sin(a.heading) * a.speed + std::cos(a.heading) * a.slip + a.knock.x,
                             std::cos(a.heading) * a.speed - std::sin(a.heading) * a.slip + a.knock.y };
        const Vector2 vb = { std::sin(b.heading) * b.speed + std::cos(b.heading) * b.slip + b.knock.x,
                             std::cos(b.heading) * b.speed - std::sin(b.heading) * b.slip + b.knock.y };
        const float approach = Vector2DotProduct(Vector2Subtract(va, vb), n); // > 0: ойртож байна
        const float ma = sa.mass, mb = sb.mass, total = ma + mb;

        if (approach > 0.0f)
        {
            const float impulse = approach * 1.25f; // бага зэрэг ойж үсэрнэ
            // a: урагшлах хурдаа алдана (мөргөлтийн чиглэлд)
            const float alongA = std::sin(a.heading) * n.x + std::cos(a.heading) * n.y;
            a.speed -= alongA * impulse * mb / total;
            // b: хажуу тийш шидэгдэж эргэлдэнэ; зогсож байсан бол хурдаа алдана
            b.knock = Vector2Add(b.knock, Vector2Scale(n, impulse * ma / total));
            const float side = std::cos(b.heading) * n.x - std::sin(b.heading) * n.y;
            b.spin += side * impulse * ma / total * 0.35f;
            b.speed *= 0.6f;
            if (approach > 2.5f)
            {
                Audio::Play(Sfx::Crash, b.position, std::min(1.0f, approach / 12.0f), sb.mass > 1.0f || sa.mass > 1.0f ? 0.9f : 1.2f);
                const float hit = approach - 2.5f;
                Damage(j, hit * 5.5f * ma / total * ArmorFactor(b.type));
                Damage(index, hit * 5.5f * mb / total * ArmorFactor(a.type));
            }
        }
        // Давхцлыг салгана: хөнгөн нь илүү холдоно
        const float overlap = minDistance - distance;
        Vector3 pushB = { b.position.x + n.x * overlap * ma / total, b.position.y, b.position.z + n.y * overlap * ma / total };
        world.ResolveHorizontal(pushB, sb.radius, 1.6f, 0.3f);
        b.position.x = pushB.x;
        b.position.z = pushB.z;
        a.position.x -= n.x * overlap * mb / total;
        a.position.z -= n.y * overlap * mb / total;
    }
}

void Garage::Clear()
{
    vehicles.clear();
    driven = -1;
    escort = -1;
}

void Garage::Add(VehicleType type, Vector3 position, float heading)
{
    Vehicle vehicle = {};
    vehicle.type = type;
    vehicle.position = position;
    vehicle.heading = heading;
    vehicle.health = SpecFor(type).maxHealth;
    // Бензин: хэрэгсэл бүрд өөр (хамтарсан тоглолтод хоёр талд ижил байхаар индексээс)
    const unsigned hash = (unsigned)vehicles.size() * 2654435761u;
    vehicle.fuel = SpecFor(type).fuelRange > 0.0f ? 0.3f + 0.6f * (float)(hash % 1000u) / 1000.0f : 1.0f;
    vehicles.push_back(vehicle);
}

float Garage::Refuel(int index, float amount)
{
    Vehicle& vehicle = vehicles[(size_t)index];
    if (SpecFor(vehicle.type).fuelRange <= 0.0f || vehicle.wrecked) return 0.0f;
    const float added = std::min(amount, 1.0f - vehicle.fuel);
    vehicle.fuel += added;
    return added;
}

float Garage::Repair(int index, float amount)
{
    Vehicle& vehicle = vehicles[(size_t)index];
    if (vehicle.wrecked) return 0.0f;
    const float added = std::min(amount, SpecFor(vehicle.type).maxHealth - vehicle.health);
    vehicle.health += added;
    return added;
}

void Garage::Damage(int index, float amount)
{
    Vehicle& vehicle = vehicles[(size_t)index];
    // Хамтарсан тоглолт: нөгөө талын жолоодож буй унааг энд гэмтээвэл хоёр талд зөрнө (жишээ нь зочны дэлгэцэнд
    // хостын морь үхчихээд давхисаар). Эрүүл мэндийг нь SyncHealth-ээр авна.
    if (vehicle.wrecked || amount <= 0.0f || vehicle.remote) return;
    vehicle.health -= amount;
    vehicle.hurtTimer = 0.0f;
    if (vehicle.health <= 0.0f)
    {
        vehicle.health = 0.0f;
        vehicle.wrecked = true;
        // Эвдрэхэд: машин тэсрэх мэт, морь унах
        Audio::Play(vehicle.type == VehicleType::Horse ? Sfx::Thud : Sfx::Explosion, vehicle.position, 0.8f,
                    vehicle.type == VehicleType::Horse ? 0.7f : 1.3f);
    }
}

int Garage::Nearest(Vector3 position, float maxDistance) const
{
    int best = -1;
    float bestDistance = maxDistance;
    for (int i = 0; i < Count(); ++i)
    {
        if (vehicles[i].wrecked) continue; // эвдэрсэн (үхсэн морь) унах боломжгүй
        const float distance = HorizontalDistance(position, vehicles[i].position) - SpecFor(vehicles[i].type).radius;
        if (distance < bestDistance)
        {
            best = i;
            bestDistance = distance;
        }
    }
    return best;
}

void Garage::SetRemote(int index, Vector3 position, float heading, float speed, float steer, bool occupied)
{
    if (index < 0 || index >= Count()) return;
    Vehicle& vehicle = vehicles[(size_t)index];
    if (!vehicle.remote || HorizontalDistance(vehicle.position, position) > 8.0f)
    {
        vehicle.position = position; // анх удаа эсвэл хэт хоцорсон: шууд байрлуулна
        vehicle.heading = heading;
    }
    vehicle.remote = true;
    vehicle.occupied = occupied;
    vehicle.netPosition = position;
    vehicle.netHeading = heading;
    vehicle.netSpeed = speed;
    vehicle.netSteer = steer;
}

void Garage::SyncHealth(int index, float health)
{
    if (index < 0 || index >= Count() || index == driven) return;
    Vehicle& vehicle = vehicles[(size_t)index];
    vehicle.health = std::max(0.0f, health);
    if (vehicle.health <= 0.0f && !vehicle.wrecked)
    {
        vehicle.wrecked = true;
        Audio::Play(vehicle.type == VehicleType::Horse ? Sfx::Thud : Sfx::Explosion, vehicle.position, 0.8f,
                    vehicle.type == VehicleType::Horse ? 0.7f : 1.3f);
    }
    else if (vehicle.health > 0.0f && vehicle.wrecked)
    {
        vehicle.wrecked = false; // нөгөө тал засварласан
    }
}

void Garage::ClearRemote(int index)
{
    if (index < 0 || index >= Count()) return;
    vehicles[(size_t)index].remote = false;
    vehicles[(size_t)index].occupied = false;
}

void Garage::Dismount()
{
    driven = -1;
}

Vector3 Garage::SeatPosition() const
{
    if (driven < 0)
    {
        return { 0.0f, 0.0f, 0.0f };
    }
    return SeatWorld(driven, false);
}

namespace
{
    // Унаа налуу дээр зурахдаа хөндлөн тэнхлэгээ тойрон хазайдаг (Draw: rlRotatef(-pitch, X)). Суудал, хазаарын
    // цэгийг ч мөн адил хазайлгана: эс бөгөөс өгсүүрт унаач эмээлээс урагш гарч морины хүзүүнд шигдэнэ.
    Vector3 PitchLocal(const Vehicle& vehicle, Vector3 local)
    {
        const float cr = std::cos(vehicle.roll), sr = std::sin(vehicle.roll);
        local = { local.x * cr - local.y * sr, local.x * sr + local.y * cr, local.z };
        const float c = std::cos(-vehicle.pitch), s = std::sin(-vehicle.pitch);
        return { local.x, local.y * c - local.z * s, local.y * s + local.z * c };
    }

    // Хажуугийн налуу: зүүн, баруун дугуйн доорх газрын өндрийн зөрүүгээс. Морь, мотоцикл босоо хэвээр
    // (хажуу налуу дээр жинхэнэдээ ч хазайдаггүй), дугуйтай машин хазайна.
    float SideRollTarget(const Vehicle& vehicle, const World& world)
    {
        if (vehicle.type == VehicleType::Horse || vehicle.type == VehicleType::Motorcycle) return 0.0f;
        const float halfTrack = vehicle.type == VehicleType::Truck ? 1.1f : 0.85f;
        // Дүрсний орон зайн +X тэнхлэг (heading-ээр эргүүлсэн)
        const float sideX = std::cos(vehicle.heading), sideZ = -std::sin(vehicle.heading);
        const float left = world.GroundHeight(vehicle.position.x + sideX * halfTrack, vehicle.position.z + sideZ * halfTrack);
        const float right = world.GroundHeight(vehicle.position.x - sideX * halfTrack, vehicle.position.z - sideZ * halfTrack);
        return std::clamp(std::atan((left - right) / (2.0f * halfTrack)), -0.6f, 0.6f);
    }
}

Vector3 Garage::SeatWorld(int index, bool passenger) const
{
    const Vehicle& vehicle = vehicles[index];
    Vector3 seat = SpecFor(vehicle.type).seat;
    if (passenger)
    {
        switch (vehicle.type)
        {
        case VehicleType::Jeep: seat = { -0.45f, 1.0f, 0.0f }; break;         // жолоочийн эсрэг талын суудал
        case VehicleType::Motorcycle: seat = { 0.0f, 0.9f, -0.62f }; break;   // сундлана
        case VehicleType::Trike: seat = { -0.22f, 0.98f, -1.0f }; break;      // ачааны тэвшин дээр
        case VehicleType::Horse: seat = { 0.0f, 1.5f, -0.52f }; break;        // сундлана
        case VehicleType::Truck: seat = { -0.5f, 1.75f, -1.6f }; break;       // тэвшин дээр
        case VehicleType::Suv:
        case VehicleType::Hatchback: seat.x = -0.45f; break;                   // жолоочийн эсрэг талын суудал
        }
    }
    else if (IsCar(vehicle.type))
    {
        seat.x = 0.45f; // жолоочийн суудал
    }
    if (vehicle.type == VehicleType::Horse)
    {
        // Морины давхилттай хамт дэгдэнэ (давхихад илүү их, урагш бөхийнө).
        const float gallop = SmoothStep(9.0f, 13.0f, std::fabs(vehicle.speed));
        const float stride = HorseStridePhase(vehicle.animation);
        const float trotBounce = std::sin(vehicle.animation * 2.0f) * 0.05f;
        const float gallopBounce = 0.07f * std::cos(2.0f * PI * (stride - 0.95f)); // унаач морьноос бага зэрэг хоцорно
        seat.y += (trotBounce + (gallopBounce - trotBounce) * gallop) * std::min(1.0f, std::fabs(vehicle.speed) / 4.0f);
        seat.z += 0.12f * gallop;
    }
    return HumanoidLocalToWorld(vehicle.position, vehicle.heading, PitchLocal(vehicle, seat));
}

Vector3 Garage::ReinWorld(int index, bool& hasRein) const
{
    const Vehicle& vehicle = vehicles[(size_t)index];
    Vector3 local = { 0.0f, 0.0f, 0.0f };
    hasRein = true;
    switch (vehicle.type)
    {
    case VehicleType::Motorcycle:
    case VehicleType::Trike:
    {
        // Зүүн бариул (+X = зүүн тал): жолооны тэнхлэгийг (kMotoSteerZ) тойрон жолоотой хамт эргэнэ.
        const float angle = -vehicle.steer * 28.0f * DEG2RAD;
        const float x = 0.31f, z = 0.48f - kMotoSteerZ;
        local = { x * std::cos(angle) + z * std::sin(angle), 1.16f, kMotoSteerZ - x * std::sin(angle) + z * std::cos(angle) };
        break;
    }
    case VehicleType::Horse:
        local = { 0.09f, 1.6f, 0.36f }; // хазаар: эмээлийн урд, хүзүүний дээр
        break;
    default:
        hasRein = false;
        break;
    }
    return HumanoidLocalToWorld(vehicle.position, vehicle.heading, PitchLocal(vehicle, local));
}

void Garage::Update(float dt, const World& world, ZombieHorde& horde, float throttle, float steer, bool handbrake, bool boost)
{
    for (int i = 0; i < Count(); ++i)
    {
        Vehicle& vehicle = vehicles[i];
        const VehicleSpec& spec = SpecFor(vehicle.type);
        const bool byPlayer = i == driven;
        if (vehicle.remote && !byPlayer)
        {
            // Сүлжээгээр хөдөлгөгдөж буй хэрэгсэл: байрлалыг зөөлөн дагаж, дугуй эргүүлнэ; жолоодож буй бол зомбийг
            // дайрна (хост дээр л үйлчилнэ: зочны зомбинууд хохирол авдаггүй).
            const VehicleSpec& remoteSpec = SpecFor(vehicle.type);
            const float follow = std::min(1.0f, 10.0f * dt);
            vehicle.position.x += (vehicle.netPosition.x - vehicle.position.x) * follow;
            vehicle.position.z += (vehicle.netPosition.z - vehicle.position.z) * follow;
            vehicle.position.y = world.GroundHeight(vehicle.position.x, vehicle.position.z);
            vehicle.heading += std::remainder(vehicle.netHeading - vehicle.heading, 2.0f * PI) * follow;
            {
                // Налууг дагана (сүлжээгээр ирдэггүй тул энд тооцно)
                const float reach = remoteSpec.radius + 0.6f;
                const float fx = std::sin(vehicle.heading) * reach, fz = std::cos(vehicle.heading) * reach;
                const float grade = (world.GroundHeight(vehicle.position.x + fx, vehicle.position.z + fz) -
                                     world.GroundHeight(vehicle.position.x - fx, vehicle.position.z - fz)) / (reach * 2.0f);
                vehicle.pitch += (std::atan(grade) - vehicle.pitch) * std::min(1.0f, 8.0f * dt);
                vehicle.roll += (SideRollTarget(vehicle, world) - vehicle.roll) * std::min(1.0f, 8.0f * dt);
            }
            vehicle.speed = vehicle.netSpeed;
            vehicle.steer += (vehicle.netSteer - vehicle.steer) * follow;
            vehicle.animation += vehicle.speed * dt * (vehicle.type == VehicleType::Horse ? 1.1f : 1.0f);
            const float speed = std::fabs(vehicle.speed);
            if (vehicle.occupied && speed > 4.0f)
            {
                const float impact = speed * remoteSpec.ramDamage;
                const float sign = vehicle.speed >= 0.0f ? 1.0f : -1.0f;
                const Vector3 push = { std::sin(vehicle.heading) * speed * sign, 0.0f, std::cos(vehicle.heading) * speed * sign };
                horde.SetCreditRemote(true); // хост: зочны унаа мөргөсөн зомбины зоос зочинд
                horde.Ram(vehicle.position, remoteSpec.radius + 0.5f, impact * dt * 6.0f, speed >= RamLaunchSpeed(vehicle.type), push);
                horde.SetCreditRemote(false);
            }
            continue;
        }
        const bool controlled = (byPlayer || i == escort) && !vehicle.wrecked; // тоглогч эсвэл баг жолоодож байгаа
        const float steerInput = byPlayer ? steer : escortSteer;
        vehicle.hurtTimer += dt;

        // Мөргүүлж шидэгдсэн: хажуу тийш гулсаж, эргэлдэж, аажим зогсоно; хана мөргөвөл гэмтэнэ.
        const float knockSpeed = Vector2Length(vehicle.knock);
        if (knockSpeed > 0.05f || std::fabs(vehicle.spin) > 0.02f)
        {
            const Vector3 before = vehicle.position;
            vehicle.position.x += vehicle.knock.x * dt;
            vehicle.position.z += vehicle.knock.y * dt;
            world.ResolveHorizontal(vehicle.position, spec.radius, 1.6f, 0.3f);
            const float moved = HorizontalDistance(before, vehicle.position);
            if (knockSpeed > 3.0f && moved < knockSpeed * dt * 0.5f)
            {
                Damage(i, (knockSpeed - 3.0f) * 2.5f);
                vehicle.knock = Vector2Scale(vehicle.knock, 0.2f);
            }
            vehicle.heading += vehicle.spin * dt;
            const float decay = std::exp(-3.0f * dt);
            vehicle.knock = Vector2Scale(vehicle.knock, decay);
            vehicle.spin *= decay;
            vehicle.position.y = world.GroundHeight(vehicle.position.x, vehicle.position.z);
            Collide(i, world);
        }
        if (!controlled && vehicle.speed == 0.0f && vehicle.slip == 0.0f)
        {
            continue; // зогсож буй унаа: тооцох зүйлгүй (ертөнцөд олон унаа байгаа)
        }
        const float forwardX = std::sin(vehicle.heading), forwardZ = std::cos(vehicle.heading);

        // --- Газрын онцлог: гадаргуу ба явж буй чиглэл дэх налуу ---
        const Surface surface = world.SurfaceAt(vehicle.position.x, vehicle.position.z);
        const float surfaceFactor = surface == Surface::Asphalt ? spec.onAsphalt
                                  : surface == Surface::Dirt ? spec.onDirt : spec.onSteppe;

        const float reach = spec.radius + 0.6f;
        const float ahead = world.GroundHeight(vehicle.position.x + forwardX * reach, vehicle.position.z + forwardZ * reach);
        const float behind = world.GroundHeight(vehicle.position.x - forwardX * reach, vehicle.position.z - forwardZ * reach);
        const float grade = (ahead - behind) / (reach * 2.0f); // эерэг = өгсүүр (урагш явахад)

        // Өгсөхөд удааширч, уруудахад бага зэрэг хурдасна.
        const float direction = vehicle.speed >= 0.0f ? 1.0f : -1.0f;
        const float slopeFactor = std::clamp(1.0f - grade * direction * spec.slopePenalty, 0.25f, 1.15f);
        float maxSpeed = spec.topSpeed * surfaceFactor * slopeFactor;
        if (vehicle.type == VehicleType::Horse && byPlayer && !boost)
        {
            maxSpeed *= 0.55f; // Shift дарахгүй бол хатирна (~33 км/ц); Shift: давхина (~60 км/ц)
        }

        // --- Хурд ---
        float input = byPlayer ? throttle : controlled ? escortThrottle : 0.0f;
        // Бензин: явсан зай ба ажиллаж буй хөдөлгүүрээр зарцуулна (багийн жолооч зарцуулахгүй).
        // Дууссан бол хий өгөхгүй (зөвхөн тоормос, ухрах ч үгүй).
        if (spec.fuelRange > 0.0f)
        {
            if (byPlayer)
            {
                vehicle.fuel -= (std::fabs(vehicle.speed) * dt / (spec.fuelRange * 1000.0f) + 0.00025f * dt);
                vehicle.fuel = std::max(0.0f, vehicle.fuel);
            }
            if (vehicle.fuel <= 0.0f && byPlayer && input > 0.0f) input = 0.0f;
            if (vehicle.fuel <= 0.0f && byPlayer && input < 0.0f && vehicle.speed <= 0.5f) input = 0.0f;
        }
        // Шарвалт зөвхөн тоормос (Space эсвэл S) + хажуу тийш эргэхэд эхэлнэ; бусад үед дугуй сайн наалдана.
        const bool braking = handbrake || (input < 0.0f && vehicle.speed > 0.5f);
        const bool sliding = byPlayer && braking && steerInput != 0.0f && !vehicle.wrecked &&
                             vehicle.type != VehicleType::Horse && vehicle.type != VehicleType::Motorcycle &&
                             std::fabs(vehicle.speed) + std::fabs(vehicle.slip) > 6.0f;
        vehicle.drifting = sliding;
        vehicle.driftTimer = sliding ? 1.0f : std::max(0.0f, vehicle.driftTimer - dt);

        if (vehicle.wrecked)
        {
            vehicle.speed -= std::clamp(vehicle.speed, -spec.braking * dt, spec.braking * dt);
        }
        else if (sliding)
        {
            // Шарвах үед: арын дугуй түгжигдэж гулсана, бага зэрэг удаашрана
            vehicle.speed -= std::clamp(vehicle.speed, -3.0f * dt, 3.0f * dt);
        }
        else if (byPlayer && handbrake)
        {
            vehicle.speed -= std::clamp(vehicle.speed, -spec.braking * 1.5f * dt, spec.braking * 1.5f * dt);
        }
        else if (input > 0.0f)
        {
            // Урагш: ухарч байсан бол эхлээд тоормослоно.
            vehicle.speed += (vehicle.speed < 0.0f ? spec.braking : spec.acceleration) * input * dt;
        }
        else if (input < 0.0f)
        {
            // Тоормос, зогссон бол ухрах
            vehicle.speed += (vehicle.speed > 0.5f ? spec.braking : spec.acceleration * 0.6f) * input * dt;
            vehicle.speed = std::max(vehicle.speed, -spec.reverseSpeed * surfaceFactor);
        }
        else
        {
            // Хий гишгээгүй: үрэлтээр аажим зогсоно
            const float friction = (3.0f + std::fabs(vehicle.speed) * 0.15f) * dt;
            vehicle.speed -= std::clamp(vehicle.speed, -friction, friction);
        }

        // Одоогийн газар зөвшөөрөх хурдаас давсан бол (замаас гарах, өгсөх) хурдан удааширна.
        if (vehicle.speed > maxSpeed)
        {
            vehicle.speed = std::max(maxSpeed, vehicle.speed - spec.braking * 0.8f * dt);
        }

        // Эргэхээс өмнөх хөдөлгөөний вектор (ертөнцөд): эргэлтийн дараа хэсэгчлэн хажуу тийш гулсалт болж үлдэнэ
        const Vector2 velocity = { std::sin(vehicle.heading) * vehicle.speed + std::cos(vehicle.heading) * vehicle.slip,
                                   std::cos(vehicle.heading) * vehicle.speed - std::sin(vehicle.heading) * vehicle.slip };

        // --- Эргэлт: хурдтай үед л эргэнэ, хурд их байх тусам эргэлт өргөснө ---
        if (controlled && steerInput != 0.0f)
        {
            // Морь машин шиг биш: зогсож байхдаа ч газар дээрээ эргэнэ (саад тойрохдоо ухрах шаардлагагүй).
            const float grip = vehicle.type == VehicleType::Horse ? (vehicle.speed < -0.1f ? -1.0f : 1.0f)
                                                                  : std::clamp(vehicle.speed / 3.0f, -1.0f, 1.0f);
            const float rate = spec.turnRate / (1.0f + std::fabs(vehicle.speed) * 0.045f);
            vehicle.heading -= steerInput * rate * grip * dt; // D = баруун тийш
            // Гар тоормостой шарвахад ар нь гулсаж, машин илүү огцом эргэнэ
            if (sliding) vehicle.heading -= steerInput * (vehicle.type == VehicleType::Truck ? 0.5f : 1.1f) * (vehicle.speed >= 0.0f ? 1.0f : -1.0f) * dt;
        }

        // --- Дугуйн наалдац: хажуугийн гулсалтыг унтраана (засмал дээр хурдан, хээр талд удаан, гар тоормостой үед бараг үгүй) ---
        if (vehicle.type == VehicleType::Horse)
        {
            vehicle.slip = 0.0f; // морь гулсахгүй
        }
        else
        {
            const Vector2 forward = { std::sin(vehicle.heading), std::cos(vehicle.heading) };
            const Vector2 right = { std::cos(vehicle.heading), -std::sin(vehicle.heading) };
            const float along = Vector2DotProduct(velocity, forward), lateral = Vector2DotProduct(velocity, right);
            // Энгийн жолоодлого: бараг гулсахгүй. Шарвах үед гадаргуугаас хамааран (засмал дээр богино, хээр талд урт).
            const float surfaceGrip = surface == Surface::Asphalt ? 8.0f : surface == Surface::Dirt ? 4.5f : 3.2f;
            float grip = 40.0f;
            if (sliding) grip = surfaceGrip * 0.12f;
            // Шарвалтыг хий дарж барих: саяхан шарвуулсан, хажуу тийш гулсаж байхад хий дарвал үргэлжилнэ
            // (эсрэг тийш жолоо эргүүлж дрифтийг удаан хийнэ). Хий тавибал тэгширнэ.
            else if (byPlayer && vehicle.driftTimer > 0.0f && input > 0.3f && std::fabs(lateral) > 2.0f)
            {
                grip = surfaceGrip * 0.4f;
                vehicle.driftTimer = 0.6f;
            }
            const float newSlip = lateral * std::exp(-grip * dt);
            // Унтарсан гулсалтын хурдны ихэнх нь урагшлах хурд болж буцна (энгийн эргэлтэд хурд алдагдахгүй),
            // харин шарвах үед ихэнх нь үрэлтэд алдагдана.
            const float magnitude = Vector2Length(velocity);
            const float recovered = std::sqrt(std::max(0.0f, magnitude * magnitude - newSlip * newSlip));
            const float keep = sliding ? 0.25f : 0.85f;
            const float sign = along >= 0.0f ? 1.0f : -1.0f;
            vehicle.speed = sign * (std::fabs(along) + (recovered - std::fabs(along)) * keep);
            vehicle.slip = std::fabs(newSlip) < 0.05f ? 0.0f : newSlip;
        }

        // --- Хөдөлгөөн ба мөргөлдөөн ---
        if (std::fabs(vehicle.speed) > 0.001f || vehicle.slip != 0.0f)
        {
            const Vector3 before = vehicle.position;
            const float stepX = (std::sin(vehicle.heading) * vehicle.speed + std::cos(vehicle.heading) * vehicle.slip) * dt;
            const float stepZ = (std::cos(vehicle.heading) * vehicle.speed - std::sin(vehicle.heading) * vehicle.slip) * dt;
            const float step = std::sqrt(stepX * stepX + stepZ * stepZ);
            vehicle.position.x += stepX;
            vehicle.position.z += stepZ;
            world.ResolveHorizontal(vehicle.position, spec.radius, 1.6f, 0.3f);

            // Саад мөргөсөн бол (төлөвлөснөөс хамаагүй бага явсан) хурдаа алдаж, хурдаас хамааран гэмтэнэ.
            const float moved = HorizontalDistance(before, vehicle.position);
            if (moved < step * 0.6f)
            {
                vehicle.slip *= 0.3f;
                const float impact = std::sqrt(vehicle.speed * vehicle.speed + vehicle.slip * vehicle.slip);
                if (impact > 4.0f)
                {
                    Damage(i, (impact - 4.0f) * 2.2f * ArmorFactor(vehicle.type));
                    Audio::Play(vehicle.type == VehicleType::Horse ? Sfx::Thud : Sfx::Crash, vehicle.position, std::min(1.0f, impact / 14.0f));
                }
                vehicle.speed *= 0.3f;
            }
            Collide(i, world);
        }

        // Газрын гадаргуу ба налууг дагана.
        vehicle.position.y = world.GroundHeight(vehicle.position.x, vehicle.position.z);
        vehicle.pitch += (std::atan(grade) - vehicle.pitch) * std::min(1.0f, 8.0f * dt);
        vehicle.roll += (SideRollTarget(vehicle, world) - vehicle.roll) * std::min(1.0f, 8.0f * dt);

        // Дугуй / хөлийн хөдөлгөөн. Дугуйнд: явсан зай (м); зурахдаа дугуйн радиусаар хуваавал эргэлтийн өнцөг.
        if (vehicle.type == VehicleType::Horse)
        {
            const float gallop = SmoothStep(9.0f, 13.0f, std::fabs(vehicle.speed));
            const float before = HorseStridePhase(vehicle.animation);
            vehicle.animation += vehicle.speed * dt * (1.1f - 0.18f * gallop);
            const float after = HorseStridePhase(vehicle.animation);
            // Туурайн дуу: хатирахад алхамд 2, давхихад 4 цохилт
            if (std::fabs(vehicle.speed) > 1.0f)
            {
                static const float trot[2] = { 0.0f, 0.5f }, run[4] = { 0.0f, 0.1f, 0.35f, 0.45f };
                const float* beats = gallop > 0.5f ? run : trot;
                const int count = gallop > 0.5f ? 4 : 2;
                for (int k = 0; k < count; ++k)
                {
                    const bool crossed = before <= after ? (before < beats[k] && beats[k] <= after) : (before < beats[k] || beats[k] <= after);
                    if (crossed) Audio::Play(Sfx::Hoof, vehicle.position, 0.45f + 0.35f * gallop, 0.9f + 0.1f * (float)k);
                }
            }
        }
        else
        {
            vehicle.animation += vehicle.speed * dt;
        }
        if (vehicle.type == VehicleType::Horse && controlled && std::fabs(vehicle.speed) < 1.5f)
        {
            vehicle.animation += std::fabs(steerInput) * 4.0f * dt; // газар дээрээ эргэхэд хөлөө гишгэлнэ
        }
        // Жолоо: товч дарахад аажим эргэж, тавихад буцаж тэгширнэ.
        const float steerTarget = controlled ? steerInput : 0.0f;
        vehicle.steer += (steerTarget - vehicle.steer) * std::min(1.0f, (steerTarget != 0.0f ? 7.0f : 5.0f) * dt);

        if (byPlayer)
        {
            drivenSurface = surface;
            drivenMaxSpeed = maxSpeed;
        }
        if (controlled)
        {

            // Зомбийг дайрах. Цохилтын хүч = хурд x унааны жин (ramDamage).
            // Хүчтэй цохилт (RamLaunchSpeed-ээс хурдан: жийп ~16, морь ~22, мотоцикл ~25 км/ц) зомбийг агаарт
            // шидэж ална; удаан мөргөлт зөвхөн гэмтээнэ.
            const float speed = std::fabs(vehicle.speed);
            if (speed > 4.0f)
            {
                const float impact = speed * spec.ramDamage;
                const bool launch = speed >= RamLaunchSpeed(vehicle.type);
                const float direction = vehicle.speed >= 0.0f ? 1.0f : -1.0f;
                const Vector3 push = { std::sin(vehicle.heading) * speed * direction, 0.0f,
                                       std::cos(vehicle.heading) * speed * direction };

                const int hit = horde.Ram(vehicle.position, spec.radius + 0.5f, impact * dt * 6.0f, launch, push);
                if (hit > 0)
                {
                    // Зомби мөргөх бүрд унаа гэмтэнэ: хэт олныг дайрвал эвдэрнэ (ачааны машин бат бөх, морь эмзэг).
                    const float perHit = launch ? 2.4f * speed / 12.0f : 3.0f * dt;
                    if (launch) Audio::Play(Sfx::Thud, vehicle.position, std::min(1.0f, 0.4f + speed / 20.0f));
                    Damage(i, perHit * (float)hit * ArmorFactor(vehicle.type));
                    // Мөргөх бүрд унаа удааширна: шидсэн бол нэг удаагийн цохилт, үгүй бол үргэлжилсэн үрэлт
                    // Сул цохилтын үрэлт нь хэчнээн зомби байсан ч хязгаартай: бөөгнөрөл дунд ч унаа
                    // хурдаа авч, шидэх хүчтэй болж чадна.
                    vehicle.speed *= launch ? std::max(0.5f, 1.0f - 0.06f * (float)hit)
                                            : 1.0f - 0.5f * (float)std::min(hit, 2) * dt;
                }
            }
        }
    }
}

void Garage::PushOut(Vector3& position, float radius) const
{
    for (int i = 0; i < Count(); ++i)
    {
        if (i == driven) continue;

        const Vehicle& vehicle = vehicles[i];
        const float minDistance = SpecFor(vehicle.type).radius * 0.8f + radius;
        const float dx = position.x - vehicle.position.x;
        const float dz = position.z - vehicle.position.z;
        const float distance = std::sqrt(dx * dx + dz * dz);
        if (distance < minDistance && distance > 1e-4f)
        {
            position.x = vehicle.position.x + dx / distance * minDistance;
            position.z = vehicle.position.z + dz / distance * minDistance;
        }
    }
}

void Garage::Draw(Vector3 viewPosition, float maxDistance, const Material* material, bool shadowPass) const
{
    propMaterial = material;
    modelDepthMaterial = shadowPass ? material : nullptr;
    // Угсарсан хэлбэрүүдийн (хоолой, налуу ирмэг) зарим тал эсрэг эргэлттэй тул хоёр талыг нь зурна.
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    for (const Vehicle& vehicle : vehicles)
    {
        if (HorizontalDistance(viewPosition, vehicle.position) > maxDistance) continue;

        DrawBlobShadow(vehicle.position, SpecFor(vehicle.type).radius * 1.15f);

        // Морины загвар зурсны дараа culling дахин асдаг: машин бүрийн өмнө унтраана
        // (эс бөгөөс эсрэг эргэлттэй талууд алга болж машин нэвт харагдана).
        rlDrawRenderBatchActive();
        rlDisableBackfaceCulling();
        rlPushMatrix();
        rlTranslatef(vehicle.position.x, vehicle.position.y, vehicle.position.z);
        rlRotatef(vehicle.heading * RAD2DEG, 0.0f, 1.0f, 0.0f);
        rlRotatef(-vehicle.pitch * RAD2DEG, 1.0f, 0.0f, 0.0f); // өгсүүрт хошуу дээшилнэ
        rlRotatef(vehicle.roll * RAD2DEG, 0.0f, 0.0f, 1.0f);   // хажуу налууд хазайна

        // Эвдрэл: гэмтэх тусам бараан, эвдэрсэн бол шатсан хар. Үхсэн морь хажуугаараа хэвтэнэ.
        const float condition = vehicle.health / SpecFor(vehicle.type).maxHealth;
        const float shade = vehicle.wrecked ? (vehicle.type == VehicleType::Horse ? 0.85f : 0.32f) : 0.75f + 0.25f * std::min(1.0f, condition * 2.0f);
        Material* tintable = const_cast<Material*>(propMaterial);
        const Color tint = { (unsigned char)(255 * shade), (unsigned char)(250 * shade), (unsigned char)(245 * shade), 255 };
        if (tintable) tintable->maps[MATERIAL_MAP_DIFFUSE].color = tint;
        if (vehicle.wrecked && vehicle.type == VehicleType::Horse)
        {
            rlTranslatef(0.0f, 0.42f, 0.0f);
            rlRotatef(84.0f, 0.0f, 0.0f, 1.0f);
            rlTranslatef(0.0f, -0.9f, 0.0f);
        }

        switch (vehicle.type)
        {
        case VehicleType::Jeep: DrawJeep(vehicle); break;
        case VehicleType::Motorcycle: DrawMotorcycle(vehicle); break;
        case VehicleType::Trike: DrawTrike(vehicle); break;
        case VehicleType::Horse: DrawHorse(vehicle); break;
        case VehicleType::Truck: DrawTruck(vehicle); break;
        case VehicleType::Suv:
        case VehicleType::Hatchback: DrawJeep(vehicle); break;
        }
        if (tintable) tintable->maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
        rlPopMatrix();
    }
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
}
