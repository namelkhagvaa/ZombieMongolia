#include "Vehicle.h"

#include "Humanoid.h"
#include "MeshBuilder.h"
#include "Mover.h"
#include "Procedural.h"
#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <map>

const VehicleSpec& SpecFor(VehicleType type)
{
    //                name          top   acc   brake rev  turn  rad   asph  dirt  steppe slope ram   encl   seat                   cam
    static const VehicleSpec specs[] = {
        /* Jeep  */ { "ЖИЙП",       30.0f, 9.0f, 20.0f, 7.0f, 1.9f, 1.5f, 1.00f, 0.70f, 0.45f, 1.6f, 9.0f, true,  { 0.0f, 1.0f, 0.0f },   4.0f },
        /* Moto  */ { "МОТОЦИКЛ", 26.0f, 11.0f, 18.0f, 3.0f, 2.4f, 0.7f, 1.00f, 0.80f, 0.60f, 1.2f, 3.0f, false, { 0.0f, 0.86f, -0.22f }, 1.5f },
        /* Trike */ { "ГУРВАН ДУГУЙТ",      17.0f, 6.0f, 14.0f, 4.0f, 1.8f, 1.0f, 1.00f, 0.85f, 0.70f, 1.4f, 4.5f, false, { 0.0f, 0.92f, 0.05f },  2.0f },
        /* Horse */ { "МОРЬ",      13.0f, 8.0f, 12.0f, 2.0f, 2.8f, 0.8f, 0.85f, 1.00f, 0.95f, 0.6f, 2.0f, false, { 0.0f, 1.5f, -0.05f },  1.5f },
    };
    return specs[(int)type];
}

namespace
{
    // Унааны mesh-үүд: угсарсан хэлбэр бүрийг GPU руу нэг удаа хуулж, дараа нь DrawMesh-ээр (rlgl-ийн одоогийн
    // хувиргалтаар) зурна. Оройг кадр бүрт CPU-аас илгээхээс хэд дахин хурдан.
    const Material* propMaterial = nullptr;
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
        const float gait = std::min(1.0f, std::fabs(v.speed) / 4.0f);
        const float bounce = std::sin(v.animation * 2.0f) * 0.05f * gait;

        rlPushMatrix();
        rlTranslatef(0.0f, bounce, 0.0f);
        DrawProp(HorseBody());

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
    vehicles.push_back(vehicle);
}

int Garage::Nearest(Vector3 position, float maxDistance) const
{
    int best = -1;
    float bestDistance = maxDistance;
    for (int i = 0; i < Count(); ++i)
    {
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
        }
    }
    else if (vehicle.type == VehicleType::Jeep)
    {
        seat.x = 0.45f; // жолоочийн суудал
    }
    if (vehicle.type == VehicleType::Horse)
    {
        // Морины давхилттай хамт дэгдэнэ.
        seat.y += std::sin(vehicle.animation * 2.0f) * 0.05f * std::min(1.0f, std::fabs(vehicle.speed) / 4.0f);
    }
    return HumanoidLocalToWorld(vehicle.position, vehicle.heading, seat);
}

void Garage::Update(float dt, const World& world, ZombieHorde& horde, float throttle, float steer, bool handbrake)
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
            vehicle.speed = vehicle.netSpeed;
            vehicle.steer += (vehicle.netSteer - vehicle.steer) * follow;
            vehicle.animation += vehicle.speed * dt * (vehicle.type == VehicleType::Horse ? 1.1f : 1.0f);
            const float speed = std::fabs(vehicle.speed);
            if (vehicle.occupied && speed > 4.0f)
            {
                const float impact = speed * remoteSpec.ramDamage;
                const float sign = vehicle.speed >= 0.0f ? 1.0f : -1.0f;
                const Vector3 push = { std::sin(vehicle.heading) * speed * sign, 0.0f, std::cos(vehicle.heading) * speed * sign };
                horde.Ram(vehicle.position, remoteSpec.radius + 0.5f, impact * dt * 6.0f, impact > 40.0f, push);
            }
            continue;
        }
        const bool controlled = byPlayer || i == escort; // тоглогч эсвэл багийн гишүүн жолоодож байгаа
        const float steerInput = byPlayer ? steer : escortSteer;
        if (!controlled && vehicle.speed == 0.0f)
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
        const float maxSpeed = spec.topSpeed * surfaceFactor * slopeFactor;

        // --- Хурд ---
        const float input = byPlayer ? throttle : controlled ? escortThrottle : 0.0f;
        if (byPlayer && handbrake)
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

        // --- Эргэлт: хурдтай үед л эргэнэ, хурд их байх тусам эргэлт өргөснө ---
        if (controlled && steerInput != 0.0f)
        {
            const float grip = std::clamp(vehicle.speed / 3.0f, -1.0f, 1.0f);
            const float rate = spec.turnRate / (1.0f + std::fabs(vehicle.speed) * 0.045f);
            vehicle.heading -= steerInput * rate * grip * dt; // D = баруун тийш
        }

        // --- Хөдөлгөөн ба мөргөлдөөн ---
        if (std::fabs(vehicle.speed) > 0.001f)
        {
            const Vector3 before = vehicle.position;
            const float step = vehicle.speed * dt;
            vehicle.position.x += std::sin(vehicle.heading) * step;
            vehicle.position.z += std::cos(vehicle.heading) * step;
            world.ResolveHorizontal(vehicle.position, spec.radius, 1.6f, 0.3f);

            // Саад мөргөсөн бол (төлөвлөснөөс хамаагүй бага явсан) хурдаа алдана.
            const float moved = HorizontalDistance(before, vehicle.position);
            if (moved < std::fabs(step) * 0.6f)
            {
                vehicle.speed *= 0.3f;
            }
        }

        // Газрын гадаргуу ба налууг дагана.
        vehicle.position.y = world.GroundHeight(vehicle.position.x, vehicle.position.z);
        vehicle.pitch += (std::atan(grade) - vehicle.pitch) * std::min(1.0f, 8.0f * dt);

        // Дугуй / хөлийн хөдөлгөөн. Дугуйнд: явсан зай (м); зурахдаа дугуйн радиусаар хуваавал эргэлтийн өнцөг.
        vehicle.animation += vehicle.speed * dt * (vehicle.type == VehicleType::Horse ? 1.1f : 1.0f);
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
            // Хүчтэй цохилт (жийп ~16 км/ц-аас дээш, мотоцикл ~48 км/ц-аас дээш) зомбийг агаарт шидэж ална;
            // сул цохилт зөвхөн гэмтээнэ. Морь хэзээ ч шидэх хүчгүй.
            const float speed = std::fabs(vehicle.speed);
            if (speed > 4.0f)
            {
                const float impact = speed * spec.ramDamage;
                const bool launch = impact > 40.0f;
                const float direction = vehicle.speed >= 0.0f ? 1.0f : -1.0f;
                const Vector3 push = { std::sin(vehicle.heading) * speed * direction, 0.0f,
                                       std::cos(vehicle.heading) * speed * direction };

                const int hit = horde.Ram(vehicle.position, spec.radius + 0.5f, impact * dt * 6.0f, launch, push);
                if (hit > 0)
                {
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

void Garage::Draw(Vector3 viewPosition, float maxDistance, const Material* material) const
{
    propMaterial = material;
    // Угсарсан хэлбэрүүдийн (хоолой, налуу ирмэг) зарим тал эсрэг эргэлттэй тул хоёр талыг нь зурна.
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    for (const Vehicle& vehicle : vehicles)
    {
        if (HorizontalDistance(viewPosition, vehicle.position) > maxDistance) continue;

        DrawBlobShadow(vehicle.position, SpecFor(vehicle.type).radius * 1.15f);

        rlPushMatrix();
        rlTranslatef(vehicle.position.x, vehicle.position.y, vehicle.position.z);
        rlRotatef(vehicle.heading * RAD2DEG, 0.0f, 1.0f, 0.0f);
        rlRotatef(-vehicle.pitch * RAD2DEG, 1.0f, 0.0f, 0.0f); // өгсүүрт хошуу дээшилнэ

        switch (vehicle.type)
        {
        case VehicleType::Jeep: DrawJeep(vehicle); break;
        case VehicleType::Motorcycle: DrawMotorcycle(vehicle); break;
        case VehicleType::Trike: DrawTrike(vehicle); break;
        case VehicleType::Horse: DrawHorse(vehicle); break;
        }
        rlPopMatrix();
    }
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
}
