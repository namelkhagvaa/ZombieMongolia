#include "Weather.h"

#include "Procedural.h"
#include "raymath.h"
#include "rlgl.h"

#include <cmath>

namespace
{
    // Бөөмсийн хайрцаг (камерын эргэн тойронд): хэвтээ 2 x kHalf, босоо kHeight
    const float kHalf = 22.0f;
    const float kHeight = 16.0f;

    float Wrap(float value, float half)
    {
        const float size = half * 2.0f;
        value = std::fmod(value + half, size);
        if (value < 0.0f) value += size;
        return value - half;
    }
}

void Precipitation::Reset(WeatherKind newKind)
{
    kind = newKind;
    initialized = true;
    drops.clear();
    int count = 0;
    switch (kind)
    {
    case WeatherKind::Rain: count = 1100; break;
    case WeatherKind::Snow: count = 1300; break;
    case WeatherKind::Dust: count = 700; break;
    default: count = 0; break;
    }
    Rng rng(777u);
    drops.reserve((size_t)count);
    for (int i = 0; i < count; ++i)
    {
        drops.push_back({ { rng.Range(-kHalf, kHalf), rng.Range(0.0f, kHeight), rng.Range(-kHalf, kHalf) }, rng.Range(0.0f, 6.28f),
                          rng.Range(0.8f, 1.2f) });
    }
}

void Precipitation::Update(float dt, Vector3 viewPosition, WeatherKind newKind, Vector2 newWind)
{
    if (!initialized || newKind != kind) Reset(newKind);
    wind = newWind;
    time += dt;

    // Камер хөдлөхөд бөөмс ертөнцөд хөдлөхгүй: хайрцгийн төв шилжихэд offset-ыг эсрэгээр нь шилжүүлнэ.
    const Vector3 move = Vector3Subtract(viewPosition, center);
    center = viewPosition;

    for (Drop& d : drops)
    {
        Vector3 velocity = { 0.0f, 0.0f, 0.0f };
        switch (kind)
        {
        case WeatherKind::Rain: velocity = { wind.x * 1.2f, -17.0f * d.speed, wind.y * 1.2f }; break;
        case WeatherKind::Snow:
            velocity = { wind.x * 0.6f + std::sin(time * 1.3f + d.phase) * 0.5f, -1.3f * d.speed,
                         wind.y * 0.6f + std::cos(time * 1.1f + d.phase) * 0.5f };
            break;
        case WeatherKind::Dust:
            velocity = { wind.x * 3.2f * d.speed, std::sin(time * 2.0f + d.phase) * 0.6f, wind.y * 3.2f * d.speed };
            break;
        default: break;
        }
        d.offset.x = Wrap(d.offset.x + velocity.x * dt - move.x, kHalf);
        d.offset.z = Wrap(d.offset.z + velocity.z * dt - move.z, kHalf);
        d.offset.y = std::fmod(d.offset.y + velocity.y * dt - move.y + kHeight * 4.0f, kHeight);
    }
}

void Precipitation::Draw(const Camera3D& camera, float brightness) const
{
    if (drops.empty()) return;
    const Vector3 base = { center.x, center.y - kHeight * 0.35f, center.z };
    const Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    const Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, { 0.0f, 1.0f, 0.0f }));
    const Vector3 up = Vector3CrossProduct(right, forward);
    auto shade = [&](int r, int g, int b, int a) {
        return Color{ (unsigned char)(r * brightness), (unsigned char)(g * brightness), (unsigned char)(b * brightness), (unsigned char)a };
    };

    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    if (kind == WeatherKind::Rain)
    {
        const Color color = shade(190, 200, 214, 120);
        const Vector3 streak = Vector3Normalize({ wind.x * 1.2f, -17.0f, wind.y * 1.2f });
        rlBegin(RL_LINES);
        rlColor4ub(color.r, color.g, color.b, color.a);
        for (const Drop& d : drops)
        {
            const Vector3 p = Vector3Add(base, d.offset);
            const Vector3 q = Vector3Subtract(p, Vector3Scale(streak, 0.55f * d.speed));
            rlVertex3f(p.x, p.y, p.z);
            rlVertex3f(q.x, q.y, q.z);
        }
        rlEnd();
    }
    else
    {
        // Цас: цагаан ширхэг, шороо: шаргал жижиг толбо (камер руу харсан дөрвөлжин)
        const bool snow = kind == WeatherKind::Snow;
        const Color color = snow ? shade(242, 244, 250, 220) : shade(196, 164, 118, 150);
        rlBegin(RL_QUADS);
        rlColor4ub(color.r, color.g, color.b, color.a);
        for (const Drop& d : drops)
        {
            const Vector3 p = Vector3Add(base, d.offset);
            const float s = (snow ? 0.045f : 0.03f) * d.speed;
            const Vector3 r = Vector3Scale(right, s), u = Vector3Scale(up, s);
            const Vector3 a = Vector3Subtract(Vector3Subtract(p, r), u), b = Vector3Add(Vector3Subtract(p, u), r);
            const Vector3 c = Vector3Add(Vector3Add(p, r), u), e = Vector3Subtract(Vector3Add(p, u), r);
            rlVertex3f(a.x, a.y, a.z);
            rlVertex3f(b.x, b.y, b.z);
            rlVertex3f(c.x, c.y, c.z);
            rlVertex3f(e.x, e.y, e.z);
        }
        rlEnd();
    }
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
}
