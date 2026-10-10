#include "Polygon.h"

#include "raymath.h"

#include <algorithm>
#include <cmath>

namespace
{
    float Cross(Vector2 a, Vector2 b, Vector2 c)
    {
        return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    }

    float SignedArea(const Polygon& polygon)
    {
        float area = 0.0f;
        for (size_t i = 0; i < polygon.size(); ++i)
        {
            const Vector2 a = polygon[i];
            const Vector2 b = polygon[(i + 1) % polygon.size()];
            area += a.x * b.y - b.x * a.y;
        }
        return area * 0.5f;
    }

    bool PointInTriangle(Vector2 p, Vector2 a, Vector2 b, Vector2 c)
    {
        const float d1 = Cross(a, b, p), d2 = Cross(b, c, p), d3 = Cross(c, a, p);
        const bool negative = d1 < 0.0f || d2 < 0.0f || d3 < 0.0f;
        const bool positive = d1 > 0.0f || d2 > 0.0f || d3 > 0.0f;
        return !(negative && positive);
    }

    Vector2 ClosestPointOnSegment(Vector2 a, Vector2 b, Vector2 p)
    {
        const Vector2 ab = Vector2Subtract(b, a);
        const float lengthSq = Vector2LengthSqr(ab);
        float t = lengthSq > 0.0f ? Vector2DotProduct(Vector2Subtract(p, a), ab) / lengthSq : 0.0f;
        t = std::clamp(t, 0.0f, 1.0f);
        return Vector2Add(a, Vector2Scale(ab, t));
    }
}

bool PointInPolygon(const Polygon& polygon, Vector2 point)
{
    // Цэгээс баруун тийш татсан туяа хилийг сондгой удаа огтолбол дотор байна.
    bool inside = false;
    for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++)
    {
        const Vector2 a = polygon[i], b = polygon[j];
        if ((a.y > point.y) != (b.y > point.y) &&
            point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x)
        {
            inside = !inside;
        }
    }
    return inside;
}

Vector2 ClosestPointOnPolygon(const Polygon& polygon, Vector2 point)
{
    Vector2 best = polygon.empty() ? point : polygon[0];
    float bestDistance = 1e30f;
    for (size_t i = 0; i < polygon.size(); ++i)
    {
        const Vector2 candidate = ClosestPointOnSegment(polygon[i], polygon[(i + 1) % polygon.size()], point);
        const float distance = Vector2DistanceSqr(candidate, point);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = candidate;
        }
    }
    return best;
}

std::vector<Vector2> Triangulate(const Polygon& polygon)
{
    std::vector<Vector2> triangles;
    if (polygon.size() < 3)
    {
        return triangles;
    }

    // Нар зөв эргэлттэй (CCW) болгож, "чих"-ийг нэг нэгээр нь тайрна.
    Polygon ring = polygon;
    if (SignedArea(ring) < 0.0f)
    {
        std::reverse(ring.begin(), ring.end());
    }

    int guard = (int)ring.size() * (int)ring.size() + 16;
    while (ring.size() > 3 && guard-- > 0)
    {
        bool clipped = false;
        for (size_t i = 0; i < ring.size(); ++i)
        {
            const Vector2 prev = ring[(i + ring.size() - 1) % ring.size()];
            const Vector2 current = ring[i];
            const Vector2 next = ring[(i + 1) % ring.size()];

            if (Cross(prev, current, next) <= 0.0f)
            {
                continue; // хонхор эсвэл шулуун орой
            }

            bool containsOther = false;
            for (size_t k = 0; k < ring.size() && !containsOther; ++k)
            {
                const Vector2 p = ring[k];
                const bool isCorner = (p.x == prev.x && p.y == prev.y) || (p.x == current.x && p.y == current.y) ||
                                      (p.x == next.x && p.y == next.y);
                containsOther = !isCorner && PointInTriangle(p, prev, current, next);
            }
            if (containsOther)
            {
                continue;
            }

            triangles.push_back(prev);
            triangles.push_back(current);
            triangles.push_back(next);
            ring.erase(ring.begin() + (long long)i);
            clipped = true;
            break;
        }

        if (!clipped)
        {
            break; // доройтсон хэлбэр: үлдсэнийг нь сэнс хэлбэрээр хаана
        }
    }

    for (size_t i = 1; i + 1 < ring.size(); ++i)
    {
        triangles.push_back(ring[0]);
        triangles.push_back(ring[i]);
        triangles.push_back(ring[i + 1]);
    }
    return triangles;
}

void DrawTriangleAnyWinding(Vector2 a, Vector2 b, Vector2 c, Color color)
{
    // raylib нь нэг эргэлттэй гурвалжинг л зурдаг тул дарааллыг нь тааруулна.
    if (Cross(a, b, c) > 0.0f)
    {
        DrawTriangle(a, c, b, color);
    }
    else
    {
        DrawTriangle(a, b, c, color);
    }
}

bool Shape::Contains(Vector2 point) const
{
    if (!PointInPolygon(outline, point))
    {
        return false;
    }
    for (const Polygon& hole : holes)
    {
        if (PointInPolygon(hole, point))
        {
            return false;
        }
    }
    return true;
}

float Shape::EdgeDistance(Vector2 point) const
{
    float best = Vector2Distance(ClosestPointOnPolygon(outline, point), point);
    for (const Polygon& hole : holes)
    {
        best = std::min(best, Vector2Distance(ClosestPointOnPolygon(hole, point), point));
    }
    return best;
}

Vector2 Shape::Constrain(Vector2 point) const
{
    if (Contains(point))
    {
        return point;
    }

    // Аль хилийг зөрчсөн бэ: нүхэнд орсон бол тэр нүхний, үгүй бол гадна хилийн ирмэг рүү буцаана.
    const Polygon* violated = &outline;
    for (const Polygon& hole : holes)
    {
        if (PointInPolygon(hole, point))
        {
            violated = &hole;
        }
    }

    const Vector2 edge = ClosestPointOnPolygon(*violated, point);
    const Vector2 inward = Vector2Subtract(edge, point); // гаднаас хил рүү = дотогшоо
    const float length = Vector2Length(inward);
    if (length < 1e-5f)
    {
        return edge;
    }

    const Vector2 nudged = Vector2Add(edge, Vector2Scale(inward, 0.05f / length));
    return Contains(nudged) ? nudged : edge;
}

Vector2 Shape::InteriorPoint() const
{
    const Rectangle bounds = Bounds();
    const int steps = 48;
    Vector2 best = { bounds.x + bounds.width * 0.5f, bounds.y + bounds.height * 0.5f };
    float bestDistance = -1.0f;

    for (int iy = 1; iy < steps; ++iy)
    {
        for (int ix = 1; ix < steps; ++ix)
        {
            const Vector2 p = { bounds.x + bounds.width * (float)ix / (float)steps,
                                bounds.y + bounds.height * (float)iy / (float)steps };
            if (!Contains(p)) continue;

            const float distance = EdgeDistance(p);
            if (distance > bestDistance)
            {
                bestDistance = distance;
                best = p;
            }
        }
    }
    return best;
}

Rectangle Shape::Bounds() const
{
    if (outline.empty())
    {
        return { 0.0f, 0.0f, 0.0f, 0.0f };
    }

    float minX = outline[0].x, maxX = outline[0].x, minY = outline[0].y, maxY = outline[0].y;
    for (const Vector2& p : outline)
    {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }
    return { minX, minY, maxX - minX, maxY - minY };
}
