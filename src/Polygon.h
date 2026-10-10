#pragma once

#include "raylib.h"

#include <vector>

// Хавтгай дээрх олон өнцөгтийн туслах функцүүд (аймгийн хил, даалгаврын талбарын хязгаар).
using Polygon = std::vector<Vector2>;

bool PointInPolygon(const Polygon& polygon, Vector2 point);

// Олон өнцөгтийн ирмэг дээрх point-д хамгийн ойр цэг.
Vector2 ClosestPointOnPolygon(const Polygon& polygon, Vector2 point);

// Гурвалжнуудад хуваана (ear clipping). Буцаах утга: гурав гурваараа нэг гурвалжин болох оройнууд.
std::vector<Vector2> Triangulate(const Polygon& polygon);

// Оройн дарааллаас үл хамааран дүүргэсэн гурвалжин зурна (2D).
void DrawTriangleAnyWinding(Vector2 a, Vector2 b, Vector2 c, Color color);

// Гадна хил ба дотор нь орж болохгүй "нүх"-нүүдтэй талбай.
// Жишээ нь Төв аймаг: дотроо Улаанбаатарын нутгийг нүх болгон агуулна.
struct Shape
{
    Polygon outline;
    std::vector<Polygon> holes;

    bool Contains(Vector2 point) const;      // хил дотор, нүхний гадна
    float EdgeDistance(Vector2 point) const; // хамгийн ойрын хил хүртэлх зай
    Vector2 Constrain(Vector2 point) const;  // гадна гарсан цэгийг хамгийн ойрын хилийн дотор тал руу буцаана
    Vector2 InteriorPoint() const;           // хилээс хамгийн хол орших дотоод цэг
    Rectangle Bounds() const;
};
