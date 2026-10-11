#include "Regions.h"

#include "raymath.h"

#include <algorithm>

namespace
{
    // ------------------------------------------------------------------------------------------
    // Хилийн цэгүүд (уртраг, өргөрөг). Энэ бол бодит хилийн нарийвчилсан өгөгдөл биш, гараар
    // зурсан бүдүүвч: аймаг бүрийн байрлал, хэмжээ, хөршүүд зөв боловч хилийн муруйлт ойролцоо.
    //
    // B.. = улсын хил дээрх цэгүүд (баруун үзүүрээс цагийн зүүний дагуу)
    // J.. = гурван аймгийн уулзвар
    // бусад = хоёр аймгийн хоорондох хилийн завсрын цэг
    // ------------------------------------------------------------------------------------------

    // Улсын хил
    const Vector2 B1 = { 87.8f, 49.1f },  B2 = { 88.7f, 49.5f },  B3 = { 89.9f, 50.0f },  B4 = { 91.4f, 50.6f };
    const Vector2 B5 = { 92.9f, 50.8f },  B6 = { 94.6f, 50.2f },  B7 = { 95.4f, 50.0f },  B8 = { 97.0f, 49.8f };
    const Vector2 B9 = { 98.2f, 50.4f },  B10 = { 98.6f, 51.3f }, B11 = { 98.9f, 52.0f }, B12 = { 100.3f, 51.7f };
    const Vector2 B13 = { 102.2f, 51.3f }, B14 = { 102.6f, 50.4f }, B15 = { 103.8f, 50.2f }, B16 = { 105.3f, 50.4f };
    const Vector2 B17 = { 106.6f, 50.3f }, B18 = { 107.9f, 49.9f }, B19 = { 108.5f, 49.4f }, B20 = { 110.3f, 49.2f };
    const Vector2 B21 = { 111.5f, 49.4f }, B22 = { 112.7f, 49.6f }, B23 = { 114.3f, 50.3f }, B24 = { 116.7f, 49.9f };
    const Vector2 B25 = { 115.6f, 48.2f }, B26 = { 117.8f, 47.7f }, B27 = { 119.9f, 46.8f }, B28 = { 117.6f, 46.5f };
    const Vector2 B29 = { 116.0f, 45.7f }, B30 = { 114.6f, 45.2f }, B31 = { 113.6f, 44.8f }, B32 = { 112.0f, 45.0f };
    const Vector2 B33 = { 111.4f, 44.3f }, B34 = { 111.9f, 43.7f }, B35 = { 110.4f, 42.8f }, B36 = { 109.0f, 42.4f };
    const Vector2 B37 = { 107.4f, 42.4f }, B38 = { 105.0f, 41.6f }, B39 = { 103.3f, 41.9f }, B40 = { 101.8f, 42.5f };
    const Vector2 B41 = { 100.2f, 42.6f }, B42 = { 98.8f, 42.7f },  B43 = { 97.2f, 42.8f },  B44 = { 96.4f, 42.7f };
    const Vector2 B45 = { 95.4f, 44.3f },  B46 = { 93.7f, 45.0f },  B48 = { 90.9f, 45.2f },  B49 = { 90.7f, 46.3f };
    const Vector2 B50 = { 91.0f, 46.6f },  B51 = { 90.0f, 47.8f },  B52 = { 88.6f, 48.2f };

    // Гурван аймгийн уулзварууд
    const Vector2 J1 = { 91.5f, 49.1f },   J2 = { 93.8f, 48.5f },   J3 = { 93.9f, 47.5f },   J4 = { 97.8f, 47.2f };
    const Vector2 J5 = { 98.7f, 47.3f },   J6 = { 99.2f, 48.7f },   J7 = { 102.0f, 48.9f },  J9 = { 101.2f, 46.6f };
    const Vector2 J10 = { 103.4f, 47.4f }, J11 = { 101.3f, 44.4f }, J12 = { 104.4f, 47.1f }, J13 = { 104.6f, 46.3f };
    const Vector2 J14 = { 104.3f, 44.6f }, J15 = { 105.0f, 48.6f }, J16 = { 108.2f, 48.7f }, J17 = { 108.9f, 46.9f };
    const Vector2 J18 = { 108.0f, 46.5f }, J19 = { 108.9f, 45.9f }, J20 = { 107.6f, 44.3f }, J21 = { 109.4f, 46.5f };
    const Vector2 J22 = { 112.5f, 47.3f }, J23 = { 110.8f, 46.4f };

    // Хоёр аймгийн хоорондох завсрын цэгүүд
    const Vector2 n1 = { 90.7f, 49.6f },  k1 = { 91.7f, 48.2f },  k2 = { 91.2f, 47.4f };   // Баян-Өлгий
    const Vector2 u1 = { 95.6f, 49.2f },  u2 = { 95.0f, 48.7f },  u3 = { 92.6f, 48.8f };   // Увс
    const Vector2 g1 = { 93.2f, 46.5f },  g2 = { 93.6f, 45.6f };                            // Ховд / Говь-Алтай
    const Vector2 z1 = { 98.4f, 49.6f },  z2 = { 98.8f, 48.0f },  z3 = { 95.8f, 47.3f };   // Завхан
    const Vector2 b1 = { 98.4f, 46.0f },  b2 = { 98.6f, 44.4f };                            // Говь-Алтай / Баянхонгор
    const Vector2 s1 = { 102.5f, 49.6f }, s2 = { 100.6f, 48.6f };                           // Хөвсгөл
    const Vector2 a1 = { 103.2f, 48.4f }, a2 = { 102.3f, 46.9f }, a3 = { 99.8f, 46.9f };   // Архангай
    const Vector2 o1 = { 101.5f, 45.6f }, m1 = { 100.6f, 43.4f }, m2 = { 102.8f, 44.3f };  // Баянхонгор / Өвөрхангай / Өмнөговь
    const Vector2 m3 = { 106.0f, 44.3f }, d1 = { 104.2f, 45.4f }, d2 = { 106.5f, 46.2f };  // Дундговь
    const Vector2 e1 = { 104.0f, 49.6f }, t2 = { 104.6f, 47.8f }, t3 = { 107.0f, 48.75f }; // Булган / Төв / Сэлэнгэ
    const Vector2 t4 = { 106.0f, 48.8f }, h2 = { 108.8f, 47.8f }, g3 = { 108.2f, 46.1f };
    const Vector2 n2 = { 108.5f, 45.0f }, w1 = { 107.4f, 43.4f }, x1 = { 111.4f, 45.6f };  // Дорноговь
    const Vector2 q1 = { 112.6f, 48.4f }, p1 = { 115.0f, 46.6f }, p2 = { 113.8f, 47.0f };  // Хэнтий / Дорнод / Сүхбаатар

    // Орхон: Булган, Сэлэнгийн дунд орших жижиг аймаг
    const Vector2 r1 = { 104.0f, 49.25f }, r2 = { 104.55f, 49.15f }, r3 = { 104.5f, 48.85f }, r4 = { 103.95f, 48.9f };

    // Сэлэнгэ доторх Дархан-Уул, Төв доторх Улаанбаатар
    const Polygon kDarkhanUul = { { 105.7f, 49.75f }, { 106.4f, 49.7f }, { 106.5f, 49.2f }, { 105.9f, 49.0f }, { 105.6f, 49.3f } };
    const Polygon kUlaanbaatar = { { 106.3f, 47.75f }, { 106.6f, 48.15f }, { 107.3f, 48.25f }, { 107.7f, 48.0f },
                                   { 107.6f, 47.65f }, { 107.0f, 47.55f }, { 106.5f, 47.6f } };

    const float kLongitudeScale = 0.682f; // cos(47 градус): Монголын дундаж өргөрөгт уртрагийн градусын агшилт
}

const std::vector<Province>& Provinces()
{
    // Аяны дараалал: хамгийн баруун аймгаас зүүн тийш.
    static const std::vector<Province> provinces = {
        { "Баян-Өлгий", { B1, B2, B3, n1, J1, k1, k2, B50, B51, B52 }, {} },
        { "Увс", { B3, B4, B5, B6, B7, u1, u2, J2, u3, J1, n1 }, {} },
        { "Ховд", { J1, u3, J2, J3, g1, g2, B46, B48, B49, B50, k2, k1 }, {} },
        { "Завхан", { B7, B8, B9, z1, J6, z2, J5, J4, z3, J3, J2, u2, u1 }, {} },
        { "Говь-Алтай", { J3, z3, J4, b1, b2, B42, B43, B44, B45, B46, g2, g1 }, {} },
        { "Хөвсгөл", { B9, B10, B11, B12, B13, B14, s1, J7, s2, J6, z1 }, {} },
        { "Баянхонгор", { J4, J5, a3, J9, o1, J11, m1, B41, B42, b2, b1 }, {} },
        { "Архангай", { J6, s2, J7, a1, J10, a2, J9, a3, J5, z2 }, {} },
        { "Булган", { B14, B15, e1, r1, r4, r3, J15, t2, J12, J10, a1, J7, s1 }, {} },
        { "Өвөрхангай", { J9, a2, J10, J12, J13, d1, J14, m2, J11, o1 }, {} },
        { "Орхон", { r1, r2, r3, r4 }, {} },
        { "Өмнөговь", { J11, m2, J14, m3, J20, w1, B37, B38, B39, B40, B41, m1 }, {} },
        { "Сэлэнгэ", { B15, B16, B17, B18, B19, J16, t3, t4, J15, r3, r2, r1, e1 }, { kDarkhanUul } },
        { "Дархан-Уул", kDarkhanUul, {} },
        { "Төв", { J15, t4, t3, J16, h2, J17, J18, d2, J13, J12, t2 }, { kUlaanbaatar } },
        { "Улаанбаатар", kUlaanbaatar, {} },
        { "Дундговь", { J13, d2, J18, g3, J19, n2, J20, m3, J14, d1 }, {} },
        { "Говьсүмбэр", { J18, J17, J21, J19, g3 }, {} },
        { "Дорноговь", { J21, J23, x1, B32, B33, B34, B35, B36, B37, w1, J20, n2, J19 }, {} },
        { "Хэнтий", { B19, B20, B21, B22, q1, J22, J23, J21, J17, h2, J16 }, {} },
        { "Сүхбаатар", { J22, p2, p1, B29, B30, B31, B32, x1, J23 }, {} },
        { "Дорнод", { B22, B23, B24, B25, B26, B27, B28, B29, p1, p2, J22, q1 }, {} },
    };
    return provinces;
}

Vector2 ProjectLonLat(Vector2 lonLat)
{
    return { lonLat.x * kLongitudeScale, lonLat.y };
}

Shape MakeMissionShape(int provinceIndex, float radius)
{
    const Province& province = Provinces()[provinceIndex];

    // 1. Хавтгайд буулгана (зүүн, хойд).
    Shape flat;
    for (const Vector2& p : province.outline) flat.outline.push_back(ProjectLonLat(p));
    for (const Polygon& hole : province.holes)
    {
        Polygon projected;
        for (const Vector2& p : hole) projected.push_back(ProjectLonLat(p));
        flat.holes.push_back(projected);
    }

    // 2. Тоглогчийн эхлэх цэг: хилээс (нүхний хилийг оролцуулаад) хамгийн хол дотоод цэг.
    const Vector2 anchor = flat.InteriorPoint();
    float farthest = 0.001f;
    for (const Vector2& p : flat.outline)
    {
        farthest = std::max(farthest, Vector2Distance(p, anchor));
    }
    const float scale = radius / farthest;

    // 3. Ертөнцийн координат: хойд = +Z. Тоглоомын ертөнцөд хойш харахад зүүн зүг нь -X талд байдаг.
    auto toWorld = [&](Vector2 p) { return Vector2{ -(p.x - anchor.x) * scale, (p.y - anchor.y) * scale }; };

    Shape shape;
    for (const Vector2& p : flat.outline) shape.outline.push_back(toWorld(p));
    for (const Polygon& hole : flat.holes)
    {
        Polygon ring;
        for (const Vector2& p : hole) ring.push_back(toWorld(p));
        shape.holes.push_back(ring);
    }
    return shape;
}
