#pragma once

#include "Environment.h"
#include "raylib.h"

#include <vector>

// Камерыг тойрсон цаг агаарын бөөмс: бороо (судал), цас (ширхэг), шороон шуурга (нисэх тоос).
// Бөөмс камерын эргэн тойрны хайрцагт ороогдон байрлана: хэчнээн хол явсан ч үргэлж эргэн тойронд.
class Precipitation
{
public:
    void Update(float dt, Vector3 viewPosition, WeatherKind kind, Vector2 wind);
    // Гэрэлтүүлэггүй хэсэгт (environment.End()-ийн дараа) зурна. brightness: өдөр 1, шөнө бүдэг.
    void Draw(const Camera3D& camera, float brightness) const;

private:
    struct Drop
    {
        Vector3 offset; // хайрцаг доторх байрлал
        float phase;
        float speed;
    };
    void Reset(WeatherKind kind);

    WeatherKind kind = WeatherKind::Clear;
    bool initialized = false;
    std::vector<Drop> drops;
    Vector3 center = { 0.0f, 0.0f, 0.0f };
    Vector2 wind = { 0.0f, 0.0f };
    float time = 0.0f;
};
