#pragma once

#include "raylib.h"

#include <vector>

// Сумны мөр: богино хугацаанд харагдаад арилах шугамууд.
class Tracers
{
public:
    void Add(Vector3 from, Vector3 to, Color color);
    void Update(float dt);
    void Draw() const;
    void Clear() { lines.clear(); }

private:
    struct Line
    {
        Vector3 from;
        Vector3 to;
        Color color;
        float timeLeft;
    };

    std::vector<Line> lines;
};
