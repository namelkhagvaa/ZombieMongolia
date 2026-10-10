#include "Tracers.h"

#include <algorithm>

namespace
{
    const float kLifetime = 0.07f;
}

void Tracers::Add(Vector3 from, Vector3 to, Color color)
{
    lines.push_back({ from, to, color, kLifetime });
}

void Tracers::Update(float dt)
{
    for (Line& line : lines)
    {
        line.timeLeft -= dt;
    }
    lines.erase(std::remove_if(lines.begin(), lines.end(), [](const Line& l) { return l.timeLeft <= 0.0f; }),
                lines.end());
}

void Tracers::Draw() const
{
    for (const Line& line : lines)
    {
        DrawLine3D(line.from, line.to, line.color);
        DrawSphere(line.to, 0.06f, line.color); // туссан цэг
    }
}
