#include "Weather.h"

#include "Procedural.h"
#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>

namespace
{
    // Бөөмсийн хайрцаг (камерын эргэн тойронд): хэвтээ 2 x kHalf, босоо kHeight
    const float kHalf = 22.0f;
    const float kHeight = 16.0f;
    const int kSplashes = 220;          // бороонд нэгэн зэрэг үсэрч буй дусал (хүчээр үржинэ)
    const float kSplashRadius = 13.0f;  // камерын эргэн тойронд

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

void Precipitation::Update(float dt, Vector3 viewPosition, WeatherKind newKind, Vector2 newWind, float newAmount,
                           const std::function<float(float, float)>& ground)
{
    if (!initialized || newKind != kind) Reset(newKind);
    wind = newWind;
    amount = std::clamp(newAmount, 0.0f, 1.0f);
    time += dt;

    // Дусал үсрэх: богино насалтай, дуусмагц камерын ойролцоо шинэ газар (хүч багатай үед цөөн)
    if (kind == WeatherKind::Rain)
    {
        splashes.resize((size_t)kSplashes);
        auto random = [&]() {
            seed = seed * 1664525u + 1013904223u;
            return (float)(seed >> 8) / 16777216.0f;
        };
        for (size_t i = 0; i < splashes.size(); ++i)
        {
            Splash& s = splashes[i];
            s.age += dt;
            if (s.age < s.life) continue;
            if ((float)i >= amount * (float)kSplashes) { s.life = 0.0f; continue; } // идэвхгүй
            const float angle = random() * 2.0f * PI, r = std::sqrt(random()) * kSplashRadius;
            const float x = viewPosition.x + std::cos(angle) * r, z = viewPosition.z + std::sin(angle) * r;
            s.position = { x, ground(x, z) + 0.02f, z };
            s.life = 0.22f + random() * 0.12f;
            s.age = -random() * 0.3f; // зэрэг биш, ээлжлэн
        }
    }
    else splashes.clear();

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
    // Хүч багатай үед (шиврээ) бөөмсийн зөвхөн эхний хэсгийг зурна
    const size_t count = std::min(drops.size(), (size_t)((float)drops.size() * (0.15f + 0.85f * amount)));
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
        for (size_t i = 0; i < count; ++i)
        {
            const Drop& d = drops[i];
            const Vector3 p = Vector3Add(base, d.offset);
            const Vector3 q = Vector3Subtract(p, Vector3Scale(streak, 0.55f * d.speed));
            rlVertex3f(p.x, p.y, p.z);
            rlVertex3f(q.x, q.y, q.z);
        }
        // Дусал үсрэлт: газраас дээш, гадагш цацарсан 5 жижиг судал (титэм), хурдан бүдгэрнэ
        for (const Splash& s : splashes)
        {
            if (s.age < 0.0f || s.age >= s.life) continue;
            const float t = s.age / s.life;
            const float spread = 0.03f + 0.11f * t, rise = 0.09f * std::sin(PI * t) + 0.01f;
            const Color c = shade(214, 222, 232, (int)(170.0f * (1.0f - t)));
            rlColor4ub(c.r, c.g, c.b, c.a);
            for (int k = 0; k < 5; ++k)
            {
                const float a = (float)k * 1.2566f + s.position.x * 3.1f;
                const float dx = std::cos(a), dz = std::sin(a);
                rlVertex3f(s.position.x + dx * spread * 0.45f, s.position.y, s.position.z + dz * spread * 0.45f);
                rlVertex3f(s.position.x + dx * spread, s.position.y + rise, s.position.z + dz * spread);
            }
        }
        rlEnd();
    }
    else
    {
        // Цас: цагаан ширхэг, шороо: шаргал жижиг толбо. Камер руу харсан дөрвөлжин дээр төв нь тод, зах руугаа
        // уусдаг дугуй зураг (өмнө нь цул дөрвөлжин: цас "дөрвөлжин" харагддаг байв). Ширхэг бүр өөр хэмжээтэй.
        static Texture2D flake = {};
        if (flake.id == 0)
        {
            Image image = GenImageColor(32, 32, BLANK);
            for (int y = 0; y < 32; ++y)
            {
                for (int x = 0; x < 32; ++x)
                {
                    const float dx = ((float)x + 0.5f) / 16.0f - 1.0f, dy = ((float)y + 0.5f) / 16.0f - 1.0f;
                    const float r = std::sqrt(dx * dx + dy * dy);
                    const float a = std::clamp(1.0f - r, 0.0f, 1.0f);
                    ImageDrawPixel(&image, x, y, { 255, 255, 255, (unsigned char)(255.0f * a * a * (3.0f - 2.0f * a)) });
                }
            }
            flake = LoadTextureFromImage(image);
            UnloadImage(image);
            SetTextureFilter(flake, TEXTURE_FILTER_BILINEAR);
        }
        const bool snow = kind == WeatherKind::Snow;
        const Color color = snow ? shade(246, 248, 252, 235) : shade(196, 164, 118, 150);
        rlSetTexture(flake.id);
        rlBegin(RL_QUADS);
        rlColor4ub(color.r, color.g, color.b, color.a);
        for (size_t i = 0; i < count; ++i)
        {
            const Drop& d = drops[i];
            const Vector3 p = Vector3Add(base, d.offset);
            // (тоон хувьд санамсаргүй хэмжээ: ширхэг бүрийн байрлалаас)
            const float vary = 0.6f + 0.8f * (0.5f + 0.5f * std::sin((float)i * 12.9898f));
            const float s = (snow ? 0.06f : 0.04f) * d.speed * vary;
            const Vector3 r = Vector3Scale(right, s), u = Vector3Scale(up, s);
            const Vector3 a = Vector3Subtract(Vector3Subtract(p, r), u), b = Vector3Add(Vector3Subtract(p, u), r);
            const Vector3 c = Vector3Add(Vector3Add(p, r), u), e = Vector3Subtract(Vector3Add(p, u), r);
            rlTexCoord2f(0.0f, 1.0f); rlVertex3f(a.x, a.y, a.z);
            rlTexCoord2f(1.0f, 1.0f); rlVertex3f(b.x, b.y, b.z);
            rlTexCoord2f(1.0f, 0.0f); rlVertex3f(c.x, c.y, c.z);
            rlTexCoord2f(0.0f, 0.0f); rlVertex3f(e.x, e.y, e.z);
        }
        rlEnd();
        rlSetTexture(0);
    }
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
}
