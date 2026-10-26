#include "Navigation.h"

#include "Input.h"
#include "Polygon.h"
#include "UiFont.h"
#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <queue>

namespace
{
    const float kReplanDistance = 35.0f; // замаас ийм холдвол дахин тооцно
    const float kArriveDistance = 14.0f;

    float SegmentDistance(Vector2 p, Vector2 a, Vector2 b, float& t)
    {
        const Vector2 ab = Vector2Subtract(b, a);
        const float lengthSq = Vector2LengthSqr(ab);
        t = lengthSq > 0.0f ? std::clamp(Vector2DotProduct(Vector2Subtract(p, a), ab) / lengthSq, 0.0f, 1.0f) : 0.0f;
        return Vector2Distance(p, Vector2Add(a, Vector2Scale(ab, t)));
    }

    // Douglas-Peucker: торын алхмуудаас цөөн цэгтэй шугам
    void Simplify(const std::vector<Vector2>& points, size_t a, size_t b, float tolerance, std::vector<char>& keep)
    {
        float best = 0.0f;
        size_t index = a;
        for (size_t i = a + 1; i < b; ++i)
        {
            float t;
            const float d = SegmentDistance(points[i], points[a], points[b], t);
            if (d > best)
            {
                best = d;
                index = i;
            }
        }
        if (best > tolerance)
        {
            keep[index] = 1;
            Simplify(points, a, index, tolerance, keep);
            Simplify(points, index, b, tolerance, keep);
        }
    }
}

bool FindLocalPath(const World& world, Vector2 from, Vector2 to, float radius, std::vector<Vector2>& out)
{
    out.clear();
    const float cell = 2.0f, margin = 24.0f;
    float minX = std::min(from.x, to.x) - margin, maxX = std::max(from.x, to.x) + margin;
    float minZ = std::min(from.y, to.y) - margin, maxZ = std::max(from.y, to.y) + margin;
    if (maxX - minX > 200.0f || maxZ - minZ > 200.0f) return false; // хэт хол: шууд явна (тоглогчийг гүйцэх логик)
    const int w = (int)std::ceil((maxX - minX) / cell), h = (int)std::ceil((maxZ - minZ) / cell);
    std::vector<char> blocked((size_t)w * h, 0);
    auto cellOf = [&](Vector2 p, int& x, int& z) {
        x = std::clamp((int)((p.x - minX) / cell), 0, w - 1);
        z = std::clamp((int)((p.y - minZ) / cell), 0, h - 1);
    };
    auto centerOf = [&](int x, int z) { return Vector2{ minX + ((float)x + 0.5f) * cell, minZ + ((float)z + 0.5f) * cell }; };

    // Хүний өндөрт байгаа саад (хашаа, хана, байшин): radius-аар томруулж нүд бүрийг тэмдэглэнэ
    const float pad = radius + 0.3f;
    for (const Obstacle& o : world.Obstacles())
    {
        if (o.walkable || o.size.y < 0.7f) continue;
        if (o.center.x + o.size.x * 0.5f + pad < minX || o.center.x - o.size.x * 0.5f - pad > maxX) continue;
        if (o.center.z + o.size.z * 0.5f + pad < minZ || o.center.z - o.size.z * 0.5f - pad > maxZ) continue;
        const float ground = world.GroundHeight(o.center.x, o.center.z);
        if (o.Top() < ground + 0.6f || o.Bottom() > ground + 1.6f) continue; // газар доорх, дээвэр
        int x0, z0, x1, z1;
        cellOf({ o.center.x - o.size.x * 0.5f - pad, o.center.z - o.size.z * 0.5f - pad }, x0, z0);
        cellOf({ o.center.x + o.size.x * 0.5f + pad, o.center.z + o.size.z * 0.5f + pad }, x1, z1);
        for (int z = z0; z <= z1; ++z)
        {
            for (int x = x0; x <= x1; ++x)
            {
                // Нүдний төвөөс саадын хайрцаг хүртэлх зай < pad: (нимгэн хашааг ч алгасахгүй)
                const Vector2 c = centerOf(x, z);
                const float dx = std::max(0.0f, std::fabs(c.x - o.center.x) - o.size.x * 0.5f);
                const float dz = std::max(0.0f, std::fabs(c.y - o.center.z) - o.size.z * 0.5f);
                if (dx * dx + dz * dz < (pad + cell * 0.5f) * (pad + cell * 0.5f)) blocked[(size_t)z * w + x] = 1;
            }
        }
    }
    for (int z = 0; z < h; ++z)
    {
        for (int x = 0; x < w; ++x)
        {
            const Vector2 c = centerOf(x, z);
            if (!world.InsideBoundary(c.x, c.y) || world.InteriorAt(c.x, c.y) >= 0) blocked[(size_t)z * w + x] = 1;
        }
    }

    int sx, sz, gx, gz;
    cellOf(from, sx, sz);
    cellOf(to, gx, gz);
    // Эхлэл, төгсгөл саадын хажууд (томруулсан хэсэгт) байж болно: чөлөөлнө
    blocked[(size_t)sz * w + sx] = 0;
    blocked[(size_t)gz * w + gx] = 0;

    const int cells = w * h, start = sz * w + sx, goal = gz * w + gx;
    std::vector<float> g((size_t)cells, 1e30f);
    std::vector<int> parent((size_t)cells, -1);
    std::vector<char> closed((size_t)cells, 0);
    using Node = std::pair<float, int>;
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
    auto heuristic = [&](int k) {
        const float dx = (float)(k % w - gx), dz = (float)(k / w - gz);
        return std::sqrt(dx * dx + dz * dz);
    };
    g[(size_t)start] = 0.0f;
    open.push({ heuristic(start), start });
    static const int kDx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 }, kDz[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
    bool found = false;
    int expanded = 0;
    while (!open.empty() && expanded < 12000)
    {
        const int k = open.top().second;
        open.pop();
        if (closed[(size_t)k]) continue;
        closed[(size_t)k] = 1;
        ++expanded;
        if (k == goal)
        {
            found = true;
            break;
        }
        const int x = k % w, z = k / w;
        for (int d = 0; d < 8; ++d)
        {
            const int nx = x + kDx[d], nz = z + kDz[d];
            if (nx < 0 || nz < 0 || nx >= w || nz >= h) continue;
            const int n = nz * w + nx;
            if (blocked[(size_t)n] || closed[(size_t)n]) continue;
            if (d >= 4 && (blocked[(size_t)(z * w + nx)] || blocked[(size_t)(nz * w + x)])) continue; // булан огтлохгүй
            const float candidate = g[(size_t)k] + (d >= 4 ? 1.4142f : 1.0f);
            if (candidate < g[(size_t)n])
            {
                g[(size_t)n] = candidate;
                parent[(size_t)n] = k;
                open.push({ candidate + heuristic(n), n });
            }
        }
    }
    if (!found) return false;

    std::vector<int> chain;
    for (int k = goal; k >= 0; k = parent[(size_t)k]) chain.push_back(k);
    std::reverse(chain.begin(), chain.end());
    // Шулуутгах: харагдах (нүд дамжин саадгүй) хамгийн холын цэг рүү шууд
    auto clear = [&](int a, int b) {
        const Vector2 pa = centerOf(a % w, a / w), pb = centerOf(b % w, b / w);
        const float length = Vector2Distance(pa, pb);
        const int steps = std::max(1, (int)(length / (cell * 0.4f)));
        for (int i = 1; i < steps; ++i)
        {
            int x, z;
            cellOf(Vector2Lerp(pa, pb, (float)i / (float)steps), x, z);
            if (blocked[(size_t)z * w + x]) return false;
        }
        return true;
    };
    size_t at = 0;
    while (at + 1 < chain.size())
    {
        size_t next = at + 1;
        for (size_t j = chain.size() - 1; j > at + 1; --j)
        {
            if (clear(chain[at], chain[j]))
            {
                next = j;
                break;
            }
        }
        out.push_back(next + 1 == chain.size() ? to : centerOf(chain[next] % w, chain[next] / w));
        at = next;
    }
    return !out.empty();
}

int Navigator::Cell(float v) const
{
    return std::clamp((int)std::floor((v + half) / kCell), 0, side - 1);
}

void Navigator::BuildGrid(const World& world)
{
    const Rectangle bounds = world.Boundary().Bounds();
    if (gridProvince == world.CityProvince() && gridObstacles == world.Obstacles().size() && bounds.x == gridBounds.x &&
        bounds.y == gridBounds.y && bounds.width == gridBounds.width && bounds.height == gridBounds.height && !cost.empty())
    {
        return; // өмнөх тор хүчинтэй
    }
    const double start = GetTime();
    gridProvince = world.CityProvince();
    gridObstacles = world.Obstacles().size();
    gridBounds = bounds;
    half = world.HalfSize();
    side = (int)std::ceil(half * 2.0f / kCell);
    cost.assign((size_t)side * side, 30); // хээр тал

    const Layout& layout = world.GetLayout();
    auto center = [&](int i) { return -half + ((float)i + 0.5f) * kCell; };

    // Зам: засмал 10, шороон 13 (замын тэнхлэгээс өргөний хагас + 2 м)
    for (const Road& road : layout.roads)
    {
        const uint8_t value = road.kind == RoadKind::Asphalt ? 10 : 13;
        const float reach = road.halfWidth + 2.0f;
        for (size_t i = 0; i + 1 < road.points.size(); ++i)
        {
            const Vector2 a = road.points[i], b = road.points[i + 1];
            const int x0 = Cell(std::min(a.x, b.x) - reach), x1 = Cell(std::max(a.x, b.x) + reach);
            const int z0 = Cell(std::min(a.y, b.y) - reach), z1 = Cell(std::max(a.y, b.y) + reach);
            for (int iz = z0; iz <= z1; ++iz)
            {
                for (int ix = x0; ix <= x1; ++ix)
                {
                    float t;
                    if (SegmentDistance({ center(ix), center(iz) }, a, b, t) > reach) continue;
                    uint8_t& c = cost[(size_t)iz * side + ix];
                    c = std::min(c, value);
                }
            }
        }
    }

    // Ус: гүн газар удаан (гатлах боломжтой ч зайлсхийнэ), хилийн гадна нэвтрэхгүй
    for (int iz = 0; iz < side; ++iz)
    {
        for (int ix = 0; ix < side; ++ix)
        {
            const float x = center(ix), z = center(iz);
            uint8_t& c = cost[(size_t)iz * side + ix];
            if (!world.InsideBoundary(x, z)) c = 0;
            else if (c > 13 && layout.LakeDepth(x, z) > 0.8f) c = 90;
        }
    }

    // Барилга, өндөр саад: нүдний төв нь саад дотор бол нэвтрэхгүй
    for (const Obstacle& o : world.Obstacles())
    {
        if (o.size.y < 1.2f) continue;
        const float ground = world.GroundHeight(o.center.x, o.center.z);
        if (o.Top() < ground + 1.0f || o.Bottom() > ground + 2.0f) continue; // газар доорх тэмдэглэгээ, дээвэр
        const int x0 = Cell(o.center.x - o.size.x * 0.5f), x1 = Cell(o.center.x + o.size.x * 0.5f);
        const int z0 = Cell(o.center.z - o.size.z * 0.5f), z1 = Cell(o.center.z + o.size.z * 0.5f);
        for (int iz = z0; iz <= z1; ++iz)
        {
            for (int ix = x0; ix <= x1; ++ix)
            {
                if (std::fabs(center(ix) - o.center.x) <= o.size.x * 0.5f && std::fabs(center(iz) - o.center.z) <= o.size.z * 0.5f)
                {
                    cost[(size_t)iz * side + ix] = 0;
                }
            }
        }
    }
    TraceLog(LOG_INFO, "NAV: %d x %d grid in %.2f s", side, side, GetTime() - start);
}

bool Navigator::Plan(const World& world, Vector2 from)
{
    BuildGrid(world);
    route.clear();
    const int cells = side * side;
    auto index = [&](Vector2 p) { return Cell(p.y) * side + Cell(p.x); };

    // Эхлэл, төгсгөл саад дотор бол ойрын нэвтрэх нүд
    auto nearestFree = [&](int k) {
        if (cost[(size_t)k]) return k;
        const int cx = k % side, cz = k / side;
        for (int r = 1; r < 12; ++r)
        {
            for (int dz = -r; dz <= r; ++dz)
            {
                for (int dx = -r; dx <= r; ++dx)
                {
                    const int x = cx + dx, z = cz + dz;
                    if (x < 0 || z < 0 || x >= side || z >= side) continue;
                    if (cost[(size_t)z * side + x]) return z * side + x;
                }
            }
        }
        return -1;
    };
    const int startCell = nearestFree(index(from)), goalCell = nearestFree(index(target));
    if (startCell < 0 || goalCell < 0) return false;

    std::vector<float> g((size_t)cells, 1e30f);
    std::vector<int> parent((size_t)cells, -1);
    std::vector<char> closed((size_t)cells, 0);
    using Node = std::pair<float, int>;
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
    const int gx = goalCell % side, gz = goalCell / side;
    auto heuristic = [&](int k) {
        const float dx = (float)(k % side - gx), dz = (float)(k / side - gz);
        return std::sqrt(dx * dx + dz * dz) * 10.0f; // хамгийн хямд (засмал) жин
    };
    g[(size_t)startCell] = 0.0f;
    open.push({ heuristic(startCell), startCell });
    static const int kDx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 }, kDz[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
    bool found = false;
    while (!open.empty())
    {
        const int k = open.top().second;
        open.pop();
        if (closed[(size_t)k]) continue;
        closed[(size_t)k] = 1;
        if (k == goalCell)
        {
            found = true;
            break;
        }
        const int x = k % side, z = k / side;
        for (int d = 0; d < 8; ++d)
        {
            const int nx = x + kDx[d], nz = z + kDz[d];
            if (nx < 0 || nz < 0 || nx >= side || nz >= side) continue;
            const int n = nz * side + nx;
            if (!cost[(size_t)n] || closed[(size_t)n]) continue;
            if (d >= 4 && (!cost[(size_t)(z * side + nx)] || !cost[(size_t)(nz * side + x)])) continue; // булан огтлохгүй
            const float step = (d >= 4 ? 1.4142f : 1.0f) * 0.5f * (float)(cost[(size_t)k] + cost[(size_t)n]);
            const float candidate = g[(size_t)k] + step;
            if (candidate < g[(size_t)n])
            {
                g[(size_t)n] = candidate;
                parent[(size_t)n] = k;
                open.push({ candidate + heuristic(n), n });
            }
        }
    }
    if (!found) return false;

    std::vector<Vector2> points;
    for (int k = goalCell; k >= 0; k = parent[(size_t)k])
    {
        points.push_back({ -half + ((float)(k % side) + 0.5f) * kCell, -half + ((float)(k / side) + 0.5f) * kCell });
    }
    std::reverse(points.begin(), points.end());
    points.front() = from;
    points.back() = target;
    std::vector<char> keep(points.size(), 0);
    keep.front() = keep.back() = 1;
    if (points.size() > 2) Simplify(points, 0, points.size() - 1, 5.0f, keep);
    for (size_t i = 0; i < points.size(); ++i)
    {
        if (keep[i]) route.push_back(points[i]);
    }
    return true;
}

void Navigator::SetTarget(const World& world, Vector3 from, Vector2 point)
{
    target = point;
    active = true;
    arrived = false;
    replanTimer = 0.0f;
    if (!Plan(world, { from.x, from.z }))
    {
        route = { { from.x, from.z }, target }; // зам олдохгүй: шууд чиглэл
    }
}

void Navigator::Clear()
{
    active = false;
    shownValid = false;
    route.clear();
}

bool Navigator::TakeArrived()
{
    const bool value = arrived;
    arrived = false;
    return value;
}

float Navigator::RemainingDistance() const
{
    float total = 0.0f;
    for (size_t i = 0; i + 1 < route.size(); ++i) total += Vector2Distance(route[i], route[i + 1]);
    return total;
}

void Navigator::Update(const World& world, Vector3 player, float dt)
{
    if (!active) return;
    const Vector2 p = { player.x, player.z };
    if (Vector2Distance(p, target) < kArriveDistance)
    {
        Clear();
        arrived = true;
        return;
    }
    replanTimer -= dt;

    // Замын хамгийн ойрын цэг: өмнөх хэсгийг хасаж, эхлэлийг тоглогч дээр тавина
    float best = 1e30f, bestT = 0.0f;
    size_t bestSegment = 0;
    for (size_t i = 0; i + 1 < route.size() && i < 64; ++i)
    {
        float t;
        const float d = SegmentDistance(p, route[i], route[i + 1], t);
        if (d < best)
        {
            best = d;
            bestT = t;
            bestSegment = i;
        }
    }
    if (route.size() < 2 || (best > kReplanDistance && replanTimer <= 0.0f))
    {
        replanTimer = 1.5f;
        if (!Plan(world, p)) route = { p, target };
        return;
    }
    const Vector2 on = Vector2Lerp(route[bestSegment], route[bestSegment + 1], bestT);
    route.erase(route.begin(), route.begin() + (long)bestSegment);
    route[0] = best < 6.0f ? on : p;
    if (best >= 6.0f && Vector2Distance(on, p) > 0.1f) route.insert(route.begin() + 1, on); // замруу буцах хэсэг
}

void Navigator::Draw3D(const Camera3D& view, Vector3 player) const
{
    if (!active || route.size() < 2) return;
    // Камерын өмнө 4 м-т, дэлгэцийн дээд хэсэгт (y ~ 210 / 720) хөвнө. Газрын хавтгайд хэвтэж, очих зүг рүү
    // ертөнцөд чиглэнэ: камер эргэхэд сум эсрэгээр эргэж, налуугаар нь 3D хэлбэр нь харагдана.
    const Vector3 forward = Vector3Normalize(Vector3Subtract(view.target, view.position));
    const Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, { 0.0f, 1.0f, 0.0f }));
    const Vector3 up = Vector3CrossProduct(right, forward);
    const float depth = 4.0f;
    const float lift = depth * std::tan(view.fovy * 0.5f * DEG2RAD) * 0.42f;
    const Vector3 center = Vector3Add(Vector3Add(view.position, Vector3Scale(forward, depth)), Vector3Scale(up, lift));

    // Очих газар аль зүгт байгааг (замаар биш, шулуун). Камерт харьцангуй зүгийг (дэлгэц дээрх эргэлт) ба
    // хазайлтыг зөөлрүүлнэ: машинтай хурдан явахад камер бага зэрэг чичирхийлдэг (дагах, овон товон), сум тэр
    // чичирхийллийг дагаж эргэлдэхгүй. Ертөнцийн зүгийг зөөлрүүлбэл камерын чичирхийлэл шууд сумд гардаг байсан.
    const float cameraYaw = std::atan2(forward.x, forward.z);
    const float wantRelative = std::remainder(std::atan2(target.x - player.x, target.y - player.z) - cameraYaw, 2.0f * PI);
    const float wantTilt = std::max(0.0f, 1.0f - std::asin(std::clamp(-forward.y, -1.0f, 1.0f)));
    const float follow = 1.0f - std::exp(-5.0f * std::min(GetFrameTime(), 0.1f)); // кадрын хугацаа савлахад жигд
    if (!shownValid)
    {
        shownHeading = wantRelative;
        shownTilt = wantTilt;
        shownValid = true;
    }
    shownHeading += std::remainder(wantRelative - shownHeading, 2.0f * PI) * follow;
    shownTilt += (wantTilt - shownTilt) * follow;
    const float heading = cameraYaw + shownHeading;
    const Vector3 ahead = { std::sin(heading), 0.0f, std::cos(heading) }, side = { std::cos(heading), 0.0f, -std::sin(heading) };
    const float size = 0.4f, thick = 0.06f;
    // Камер бараг хэвтээ харвал хавтгай сум ирмэгээрээ харагдана: камерын баруун тэнхлэгийг тойруулан камер руу
    // хазайлгана (алсын үзүүр нь дээшилнэ), харин чиглэл нь ертөнцөд хэвээр.
    const float tilt = shownTilt; // камер руу хазайх (~57 градус хүртэл)
    auto point = [&](float x, float z, float y) {
        Vector3 offset = Vector3Add(Vector3Scale(side, x * size), Vector3Add(Vector3Scale(ahead, z * size), { 0.0f, y, 0.0f }));
        offset = Vector3RotateByAxisAngle(offset, right, tilt);
        return Vector3Add(center, offset);
    };
    // Сумны хүрээ (дээрээс харахад): үзүүр, далавч, иш
    const Vector2 outline[7] = { { 0.0f, 1.0f }, { 0.75f, 0.1f }, { 0.3f, 0.1f }, { 0.3f, -0.9f },
                                 { -0.3f, -0.9f }, { -0.3f, 0.1f }, { -0.75f, 0.1f } };
    const Color top = { 255, 205, 55, 255 }, rim = { 196, 132, 18, 255 }, under = { 120, 78, 10, 255 };

    // Гүний шалгалтгүй зурна (хана, дүрсний цаана ч үргэлж харагдана): гадаргуунуудыг камераас холоос ойр руу
    // эрэмбэлж зурна. Тогтсон дарааллаар (доод тал, хажуу, дээд тал) зурахад доороос, хажуугаас харах үед арын
    // хажуу тал урдах дээр гарч сум "нисдэг цаас" шиг эвдэрч харагддаг байсан (утсан дээр).
    struct Face
    {
        Vector3 a, b, c;
        Color color;
        float depth;
    };
    std::vector<Face> list;
    list.reserve(20);
    auto add = [&](Vector3 a, Vector3 b, Vector3 c, Color color) {
        const Vector3 middle = Vector3Scale(Vector3Add(Vector3Add(a, b), c), 1.0f / 3.0f);
        list.push_back({ a, b, c, color, Vector3DotProduct(Vector3Subtract(middle, view.position), forward) });
    };
    auto faces = [&](float y, Color color) {
        add(point(0.0f, 1.0f, y), point(0.75f, 0.1f, y), point(-0.75f, 0.1f, y), color);
        add(point(0.3f, 0.1f, y), point(0.3f, -0.9f, y), point(-0.3f, -0.9f, y), color);
        add(point(0.3f, 0.1f, y), point(-0.3f, -0.9f, y), point(-0.3f, 0.1f, y), color);
    };
    faces(-thick, under);
    faces(thick, top);
    // Хажуу талууд: зүг бүрээр гэрэлтүүлэг (камерын тал тод)
    for (int k = 0; k < 7; ++k)
    {
        const Vector2 a = outline[k], b = outline[(k + 1) % 7];
        const Vector3 a0 = point(a.x, a.y, -thick), b0 = point(b.x, b.y, -thick), a1 = point(a.x, a.y, thick), b1 = point(b.x, b.y, thick);
        const Vector3 edge = Vector3Subtract(b1, a1);
        const Vector3 normal = Vector3Normalize({ edge.z, 0.0f, -edge.x });
        const float light = 0.55f + 0.45f * std::max(0.0f, -Vector3DotProduct(normal, forward));
        const Color shade = { (unsigned char)(rim.r * light), (unsigned char)(rim.g * light), (unsigned char)(rim.b * light), 255 };
        add(a0, b0, b1, shade);
        add(a0, b1, a1, shade);
    }
    std::sort(list.begin(), list.end(), [](const Face& x, const Face& y) { return x.depth > y.depth; });

    rlDrawRenderBatchActive();
    rlDisableDepthTest();
    rlDisableBackfaceCulling();
    for (const Face& face : list) DrawTriangle3D(face.a, face.b, face.c, face.color);
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    rlEnableDepthTest();
}

void Navigator::DrawHud(Vector3 player, float cameraYaw) const
{
    if (!active || route.size() < 2) return;
    (void)cameraYaw;
    // Сум нь 3D (Draw3D): энд зөвхөн очих газар хүртэлх шулуун зай, сумны доор
    const Vector2 c = { (float)ViewWidth() * 0.5f, 210.0f };
    const Color color = { 255, 196, 40, 255 };
    const float distance = Vector2Distance({ player.x, player.z }, target);
    const char* text = distance >= 1000.0f ? TextFormat("%.1f км", distance / 1000.0f) : TextFormat("%d м", (int)distance);
    const int width = UiMeasure(text, 18);
    UiText(text, (int)c.x - width / 2 + 1, (int)c.y + 41, 18, Fade(BLACK, 0.8f));
    UiText(text, (int)c.x - width / 2, (int)c.y + 40, 18, color);
}
