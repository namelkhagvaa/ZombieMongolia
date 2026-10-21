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

void Navigator::Draw3D(const World& world, Vector3 player) const
{
    if (!active || route.size() < 2) return;
    // Газар дээрх сумнууд: тоглогчоос 6 м-ээс эхлэн 7 м тутам, 70 м хүртэл
    const Color color = { 255, 214, 60, 235 };
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    float travelled = 0.0f, next = 6.0f;
    for (size_t i = 0; i + 1 < route.size() && next < 70.0f; ++i)
    {
        const Vector2 a = route[i], b = route[i + 1];
        const float length = Vector2Distance(a, b);
        if (length < 0.01f) continue;
        const Vector2 dir = Vector2Scale(Vector2Subtract(b, a), 1.0f / length);
        const Vector2 side = { -dir.y, dir.x };
        while (next < travelled + length && next < 70.0f)
        {
            const Vector2 c = Vector2Add(a, Vector2Scale(dir, next - travelled));
            auto at = [&](Vector2 offset) {
                const Vector2 q = Vector2Add(c, offset);
                return Vector3{ q.x, world.SurfaceHeightAt(q.x, q.y, player.y + 3.0f) + 0.22f, q.y };
            };
            // "^" хэлбэрийн өргөн сум (шеврон): орой, хоёр далавч, дотор талын хонхор
            auto local = [&](float x, float y) { return at(Vector2Add(Vector2Scale(side, x), Vector2Scale(dir, y))); };
            const Vector3 tip = local(0.0f, 1.4f), notch = local(0.0f, 0.3f);
            for (float s : { -1.0f, 1.0f })
            {
                const Vector3 outer = local(s * 1.9f, -0.6f), inner = local(s * 1.9f, -1.5f);
                DrawTriangle3D(tip, outer, notch, color);
                DrawTriangle3D(outer, inner, notch, color);
            }
            next += 7.0f;
        }
        travelled += length;
    }
    rlDrawRenderBatchActive(); // хуримтлагдсан гурвалжнуудыг хоёр талаас нь зурна
    rlEnableBackfaceCulling();
}

void Navigator::DrawHud(Vector3 player, float cameraYaw) const
{
    if (!active || route.size() < 2) return;
    // Чиглэх цэг: замын дагуу 25 м урагш (эргэлтийг урьдчилан заана)
    Vector2 aim = route.back();
    float remaining = 25.0f;
    for (size_t i = 0; i + 1 < route.size(); ++i)
    {
        const float length = Vector2Distance(route[i], route[i + 1]);
        if (length >= remaining)
        {
            aim = Vector2Lerp(route[i], route[i + 1], remaining / std::max(length, 0.01f));
            break;
        }
        remaining -= length;
    }
    const float dx = aim.x - player.x, dz = aim.y - player.z;
    // Газрын зурагтай ижил: дэлгэцийн дээш = камерын харж буй зүг
    const float right = -dx * std::cos(cameraYaw) + dz * std::sin(cameraYaw);
    const float up = dx * std::sin(cameraYaw) + dz * std::cos(cameraYaw);
    const float angle = std::atan2(right, up);

    const Vector2 c = { (float)ViewWidth() * 0.5f, 210.0f }; // мэдэгдлийн зурвасын доор
    DrawCircleV(c, 25.0f, Fade(BLACK, 0.55f));
    DrawCircleLinesV(c, 25.0f, { 255, 196, 40, 230 });
    auto rotate = [&](float x, float y) {
        return Vector2{ c.x + x * std::cos(angle) - y * std::sin(angle), c.y + x * std::sin(angle) + y * std::cos(angle) };
    };
    const Color color = { 255, 196, 40, 255 };
    DrawTriangleAnyWinding(rotate(0.0f, -18.0f), rotate(-12.0f, 8.0f), rotate(0.0f, 2.0f), color);
    DrawTriangleAnyWinding(rotate(0.0f, -18.0f), rotate(0.0f, 2.0f), rotate(12.0f, 8.0f), color);

    const float distance = RemainingDistance();
    const char* text = distance >= 1000.0f ? TextFormat("%.1f км", distance / 1000.0f) : TextFormat("%d м", (int)distance);
    const int width = UiMeasure(text, 18);
    UiText(text, (int)c.x - width / 2 + 1, (int)c.y + 30, 18, Fade(BLACK, 0.8f));
    UiText(text, (int)c.x - width / 2, (int)c.y + 29, 18, color);
}
