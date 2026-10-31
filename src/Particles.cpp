#include "Particles.h"

#include "Audio.h"
#include "Procedural.h"
#include "VehicleModel.h"
#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>

namespace
{
    const size_t kMaxParticles = 600; // сул компьютерт хязгаартай
    const float kGravity = 14.0f;
    const float kFlashLife = 0.05f;
    const float kBlastLife = 0.32f;

    float Random(float low, float high)
    {
        return low + (high - low) * (float)GetRandomValue(0, 10000) / 10000.0f;
    }

    Vector3 RandomSpread(float amount)
    {
        return { Random(-amount, amount), Random(-amount, amount), Random(-amount, amount) };
    }

    float Smooth(float edge0, float edge1, float x)
    {
        const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    // ------------------------------------------------------------ Процедурын текстурууд (цагаан, өнгийг зурахдаа өгнө)

    enum Sprite
    {
        kPuffSprite,   // утаа, тоос: бөөрөнхий, ирмэг нь сэвсгэр, дотроо үүлэрхэг
        kDropSprite,   // дусал: зөөлөн бөөрөнхий
        kSplatSprite,  // газар дээрх цусны толбо: жигд биш, эргэн тойронд цацагдсан дуслууд
        kScorchSprite, // түлэгдэлт: төв нь бараан, зах руу сэвсгэр
        kSpriteCount
    };

    Texture2D sprites[kSpriteCount] = {};
    bool spritesLoaded = false;

    Texture2D MakeSprite(int size, float (*alphaAt)(float u, float v), float (*shadeAt)(float u, float v))
    {
        Image image = GenImageColor(size, size, BLANK);
        Color* pixels = (Color*)image.data;
        for (int y = 0; y < size; ++y)
        {
            for (int x = 0; x < size; ++x)
            {
                const float u = ((float)x + 0.5f) / (float)size * 2.0f - 1.0f, v = ((float)y + 0.5f) / (float)size * 2.0f - 1.0f;
                const float a = std::clamp(alphaAt(u, v), 0.0f, 1.0f);
                const float s = std::clamp(shadeAt(u, v), 0.0f, 1.0f);
                pixels[y * size + x] = { (unsigned char)(s * 255.0f), (unsigned char)(s * 255.0f), (unsigned char)(s * 255.0f),
                                         (unsigned char)(a * 255.0f) };
            }
        }
        Texture2D texture = LoadTextureFromImage(image);
        UnloadImage(image);
        GenTextureMipmaps(&texture);
        SetTextureFilter(texture, TEXTURE_FILTER_TRILINEAR);
        SetTextureWrap(texture, TEXTURE_WRAP_CLAMP);
        return texture;
    }

    void LoadSprites()
    {
        if (spritesLoaded) return;
        spritesLoaded = true;
        // Утаа: дугуй хэлбэр, ирмэг нь үүлэрхэг дуу чимээгээр "идэгдсэн", дотор нь өтгөн, сийрэг хэсгүүдтэй
        sprites[kPuffSprite] = MakeSprite(
            96,
            [](float u, float v) {
                const float d = std::sqrt(u * u + v * v);
                const float n = Fbm(u * 2.2f + 7.0f, v * 2.2f + 3.0f, 4);
                const float edge = 0.62f + 0.38f * n;
                return Smooth(edge, edge * 0.35f, d) * (0.55f + 0.45f * Fbm(u * 4.5f + 1.0f, v * 4.5f + 9.0f, 3));
            },
            [](float u, float v) {
                // Дээд тал нь (тэнгэрийн гэрэл) цайвар, доод тал нь бараан: бөмбөлөг эзэлхүүнтэй харагдана
                return 0.72f + 0.28f * (-v * 0.5f + 0.5f) * (0.7f + 0.3f * Fbm(u * 3.0f + 5.0f, v * 3.0f, 3));
            });
        sprites[kDropSprite] = MakeSprite(
            32, [](float u, float v) { return std::exp(-(u * u + v * v) * 4.5f); },
            [](float u, float v) { return 0.75f + 0.25f * Smooth(0.6f, 0.0f, std::sqrt((u + 0.3f) * (u + 0.3f) + (v + 0.3f) * (v + 0.3f))); });
        // Цусны толбо: дунд нь бөөрөнхий биш том толбо, эргэн тойронд нь цацагдсан жижиг дуслууд
        sprites[kSplatSprite] = MakeSprite(
            128,
            [](float u, float v) {
                const float angle = std::atan2(v, u);
                const float d = std::sqrt(u * u + v * v);
                const float wobble = 0.34f + 0.16f * Fbm(std::cos(angle) * 1.8f + 4.0f, std::sin(angle) * 1.8f + 4.0f, 3);
                float a = Smooth(wobble + 0.03f, wobble - 0.03f, d);
                for (int i = 0; i < 14; ++i)
                {
                    const float h = (float)i * 2.3999f;
                    const float r = 0.45f + 0.45f * std::fmod((float)i * 0.618f, 1.0f);
                    const float cx = std::cos(h) * r, cy = std::sin(h) * r;
                    const float size = 0.025f + 0.05f * std::fmod((float)i * 0.371f, 1.0f) * (1.0f - r * 0.6f);
                    const float dx = u - cx, dy = v - cy;
                    a = std::max(a, Smooth(size, size * 0.6f, std::sqrt(dx * dx + dy * dy)));
                }
                return a * 0.92f;
            },
            [](float u, float v) {
                // Төв нь өтгөн (бараан), зах нь шингэн (цайвар): хатаж буй цус
                const float d = std::sqrt(u * u + v * v);
                return 0.62f + 0.38f * Smooth(0.0f, 0.45f, d) + 0.08f * (Fbm(u * 5.0f, v * 5.0f, 2) - 0.5f);
            });
        sprites[kScorchSprite] = MakeSprite(
            96,
            [](float u, float v) {
                const float d = std::sqrt(u * u + v * v);
                const float n = Fbm(u * 3.0f + 11.0f, v * 3.0f + 2.0f, 4);
                return Smooth(0.75f + 0.25f * n, 0.15f, d) * (0.6f + 0.4f * n);
            },
            [](float u, float v) { return 0.25f + 0.35f * Fbm(u * 6.0f, v * 6.0f, 3); });
    }

    Color Scale(Color c, Color light, float alpha)
    {
        return { (unsigned char)(c.r * light.r / 255), (unsigned char)(c.g * light.g / 255), (unsigned char)(c.b * light.b / 255),
                 (unsigned char)std::clamp(alpha * 255.0f, 0.0f, 255.0f) };
    }

    // Камер руу харсан дөрвөлжин (эргэлттэй). rlBegin(RL_QUADS) дотор, текстур тавьсан байна.
    void Billboard(Vector3 center, Vector3 right, Vector3 up, float size, float rotation, Color color)
    {
        const float c = std::cos(rotation) * size, s = std::sin(rotation) * size;
        const Vector3 a = Vector3Add(Vector3Scale(right, c), Vector3Scale(up, s));
        const Vector3 b = Vector3Add(Vector3Scale(right, -s), Vector3Scale(up, c));
        rlColor4ub(color.r, color.g, color.b, color.a);
        const Vector3 p0 = Vector3Subtract(Vector3Subtract(center, a), b), p1 = Vector3Add(Vector3Subtract(center, b), a);
        const Vector3 p2 = Vector3Add(Vector3Add(center, a), b), p3 = Vector3Subtract(Vector3Add(center, b), a);
        rlTexCoord2f(0.0f, 1.0f); rlVertex3f(p0.x, p0.y, p0.z);
        rlTexCoord2f(1.0f, 1.0f); rlVertex3f(p1.x, p1.y, p1.z);
        rlTexCoord2f(1.0f, 0.0f); rlVertex3f(p2.x, p2.y, p2.z);
        rlTexCoord2f(0.0f, 0.0f); rlVertex3f(p3.x, p3.y, p3.z);
    }

    // Нисэх чиглэлдээ сунасан дөрвөлжин (дусал, оч): дэлгэц дээрх хөдөлгөөний мөр
    void Streak(Vector3 center, Vector3 velocity, Vector3 forward, float width, float stretch, Color color)
    {
        Vector3 along = Vector3Subtract(velocity, Vector3Scale(forward, Vector3DotProduct(velocity, forward)));
        const float speed = Vector3Length(along);
        along = speed > 0.001f ? Vector3Scale(along, 1.0f / speed) : Vector3{ 0.0f, 1.0f, 0.0f };
        const Vector3 side = Vector3Scale(Vector3Normalize(Vector3CrossProduct(along, forward)), width);
        const Vector3 half = Vector3Scale(along, width + speed * stretch);
        rlColor4ub(color.r, color.g, color.b, color.a);
        const Vector3 p0 = Vector3Subtract(Vector3Subtract(center, half), side), p1 = Vector3Add(Vector3Subtract(center, half), side);
        const Vector3 p2 = Vector3Add(Vector3Add(center, half), side), p3 = Vector3Subtract(Vector3Add(center, half), side);
        rlTexCoord2f(0.0f, 1.0f); rlVertex3f(p0.x, p0.y, p0.z);
        rlTexCoord2f(1.0f, 1.0f); rlVertex3f(p1.x, p1.y, p1.z);
        rlTexCoord2f(1.0f, 0.0f); rlVertex3f(p2.x, p2.y, p2.z);
        rlTexCoord2f(0.0f, 0.0f); rlVertex3f(p3.x, p3.y, p3.z);
    }

    // Газар дээр хэвтээ дөрвөлжин (толбо)
    void Decal(Vector3 center, float size, float rotation, Color color)
    {
        Billboard(center, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, size, rotation, color);
    }
}

void Particles::Add(const Particle& particle)
{
    if (particles.size() >= kMaxParticles)
    {
        particles.erase(particles.begin()); // хамгийн хуучныг хасна
    }
    particles.push_back(particle);
}

void Particles::Blood(Vector3 at, Vector3 direction, int count)
{
    for (int i = 0; i < count; ++i)
    {
        Particle p = {};
        p.kind = Kind::Blood;
        p.position = at;
        p.velocity = Vector3Add(Vector3Scale(direction, Random(1.0f, 3.5f)), RandomSpread(1.6f));
        p.velocity.y += Random(0.5f, 2.5f);
        p.size = Random(0.025f, 0.05f);
        p.life = p.maxLife = Random(2.5f, 4.0f);
        // Бараан хүрэн улаан (тод улаан биш: бодит цус газарт хар хүрэн болж хатна)
        p.color = { (unsigned char)GetRandomValue(66, 100), (unsigned char)GetRandomValue(5, 11), (unsigned char)GetRandomValue(5, 10), 255 };
        p.rotation = Random(0.0f, 2.0f * PI);
        Add(p);
    }
    // Онох мөчийн нарийн цусан манан: хурдан тэлж, бүдгэрнэ
    for (int i = 0; i < std::max(1, count / 3); ++i)
    {
        Particle p = {};
        p.kind = Kind::BloodMist;
        p.position = Vector3Add(at, RandomSpread(0.05f));
        p.velocity = Vector3Add(Vector3Scale(direction, Random(0.6f, 1.6f)), RandomSpread(0.4f));
        p.size = Random(0.12f, 0.2f);
        p.life = p.maxLife = Random(0.25f, 0.45f);
        p.color = { 92, 12, 10, 255 };
        p.rotation = Random(0.0f, 2.0f * PI);
        p.spin = Random(-2.0f, 2.0f);
        Add(p);
    }
}

void Particles::Dust(Vector3 at)
{
    for (int i = 0; i < 5; ++i)
    {
        Particle p = {};
        p.kind = Kind::Dust;
        p.position = at;
        p.velocity = RandomSpread(0.9f);
        p.velocity.y = Random(0.4f, 1.6f);
        p.size = Random(0.06f, 0.12f);
        p.life = p.maxLife = Random(0.4f, 0.8f);
        p.color = { 176, 160, 130, 255 };
        p.rotation = Random(0.0f, 2.0f * PI);
        p.spin = Random(-1.5f, 1.5f);
        Add(p);
    }
}

void Particles::Kickup(Vector3 at, Vector3 drift, Color color)
{
    Particle p = {};
    p.kind = Kind::Puff;
    p.position = Vector3Add(at, RandomSpread(0.3f));
    p.velocity = Vector3Add(drift, RandomSpread(0.5f));
    p.velocity.y = Random(0.3f, 0.9f);
    p.size = Random(0.35f, 0.6f);
    p.life = p.maxLife = Random(1.2f, 2.0f);
    p.color = color;
    p.rotation = Random(0.0f, 2.0f * PI);
    p.spin = Random(-0.6f, 0.6f);
    Add(p);
}

void Particles::Shell(Vector3 at, Vector3 sideDirection)
{
    Particle p = {};
    p.kind = Kind::Casing;
    p.position = at;
    p.velocity = Vector3Add(Vector3Scale(sideDirection, Random(1.6f, 2.6f)), RandomSpread(0.4f));
    p.velocity.y = Random(2.0f, 3.2f);
    p.size = 0.035f;
    p.life = p.maxLife = 2.5f;
    p.color = { 206, 166, 70, 255 };
    // Хөндлөн тэнхлэгээр (босоо ба нисэх чиглэлтэй перпендикуляр) хурдан эргэлдэнэ
    p.axis = Vector3Normalize(Vector3Add(Vector3CrossProduct({ 0.0f, 1.0f, 0.0f }, sideDirection), RandomSpread(0.3f)));
    p.spin = Random(900.0f, 1600.0f) * (GetRandomValue(0, 1) ? 1.0f : -1.0f);
    Add(p);
}

void Particles::Flash(Vector3 at, Vector3 direction)
{
    flashes.push_back({ at, direction, kFlashLife });
}

void Particles::Flame(Vector3 at, Vector3 velocity, float size, float life)
{
    Particle p = {};
    p.kind = Kind::Flame;
    p.position = at;
    p.velocity = velocity;
    p.size = size;
    p.life = p.maxLife = life;
    p.rotation = Random(-0.35f, 0.35f);
    p.spin = Random(-1.5f, 1.5f);
    Add(p);
}

void Particles::Smoke(Vector3 at, float size, Color color)
{
    Particle p = {};
    p.kind = Kind::Smoke;
    p.position = at;
    p.velocity = RandomSpread(0.3f);
    p.velocity.y = Random(0.8f, 1.6f);
    p.size = size;
    p.life = p.maxLife = Random(2.0f, 3.5f);
    p.color = color;
    p.rotation = Random(0.0f, 2.0f * PI);
    p.spin = Random(-0.5f, 0.5f);
    Add(p);
}

void Particles::Explosion(Vector3 at)
{
    // Унаа гэмтээх төв (нэг дэлбэрэлтийн хоёр бөмбөлгийг нэг гэж тооцно)
    bool known = false;
    for (const Vector3& b : recentBlasts) known = known || Vector3Distance(b, at) < 2.5f;
    for (const Particle& p : particles) known = known || (p.kind == Kind::Scorch && p.life > p.maxLife - 0.2f && Vector3Distance(p.position, at) < 3.0f);
    if (!known)
    {
        recentBlasts.push_back(at);
        Audio::Play(Sfx::Explosion, at, 1.0f, 0.9f + 0.2f * (float)GetRandomValue(0, 100) / 100.0f);
    }
    blasts.push_back({ at, kBlastLife });

    // Галт бөмбөлөг: хэдэн гэрэлтэх үүл (хурдан тэлж, дээш хөөрч, улбар шараас улаан болж унтарна)
    for (int i = 0; i < 9; ++i)
    {
        Particle p = {};
        p.kind = Kind::Fire;
        p.position = Vector3Add(at, RandomSpread(0.5f));
        p.velocity = RandomSpread(3.5f);
        p.velocity.y = Random(1.0f, 4.0f);
        p.size = Random(0.6f, 1.1f);
        p.life = p.maxLife = Random(0.35f, 0.7f);
        p.rotation = Random(0.0f, 2.0f * PI);
        p.spin = Random(-2.5f, 2.5f);
        Add(p);
    }

    // Оч: хурдан нисэх гэрэлтэх судал
    for (int i = 0; i < 18; ++i)
    {
        Particle p = {};
        p.kind = Kind::Spark;
        p.position = at;
        p.velocity = RandomSpread(14.0f);
        p.velocity.y = Random(2.0f, 14.0f);
        p.size = Random(0.015f, 0.03f);
        p.life = p.maxLife = Random(0.3f, 0.9f);
        Add(p);
    }

    // Тоос шороо: газраар цагираг хэлбэрээр тархаж, удаан сарнина
    for (int i = 0; i < 22; ++i)
    {
        Particle p = {};
        p.kind = Kind::Dust;
        const float angle = Random(0.0f, 2.0f * PI);
        const Vector3 out = { std::cos(angle), 0.0f, std::sin(angle) };
        p.position = Vector3Add(at, Vector3Scale(out, Random(0.2f, 0.8f)));
        p.velocity = Vector3Scale(out, Random(4.0f, 9.0f));
        p.velocity.y = Random(0.5f, 3.5f);
        p.size = Random(0.35f, 0.7f);
        p.life = p.maxLife = Random(1.4f, 2.6f);
        p.color = i % 3 == 0 ? Color{ 92, 84, 74, 255 } : Color{ 156, 138, 108, 255 };
        p.rotation = Random(0.0f, 2.0f * PI);
        p.spin = Random(-1.0f, 1.0f);
        Add(p);
    }

    // Хэлтэрхий: хурдан нисэж газарт унана
    for (int i = 0; i < 12; ++i)
    {
        Particle p = {};
        p.kind = Kind::Shell;
        p.position = at;
        p.velocity = RandomSpread(9.0f);
        p.velocity.y = Random(3.0f, 10.0f);
        p.size = Random(0.03f, 0.06f);
        p.life = p.maxLife = Random(1.0f, 2.0f);
        p.color = { 46, 42, 38, 255 };
        Add(p);
    }

    if (known) return; // (хоёр дахь бөмбөлөг: утаа, ул мөр нэг л удаа)

    // Бараан утаа: галын дараа үлдэж, дээш хөөрч тэлнэ
    for (int i = 0; i < 10; ++i)
    {
        Particle p = {};
        p.kind = Kind::Smoke;
        p.position = Vector3Add(at, { Random(-0.8f, 0.8f), Random(0.2f, 1.5f), Random(-0.8f, 0.8f) });
        p.velocity = RandomSpread(1.2f);
        p.velocity.y = Random(1.5f, 3.5f);
        p.size = Random(0.8f, 1.4f);
        p.life = p.maxLife = Random(3.0f, 5.5f);
        const unsigned char grey = (unsigned char)GetRandomValue(38, 70);
        p.color = { grey, (unsigned char)(grey - 2), (unsigned char)(grey - 4), 255 };
        p.rotation = Random(0.0f, 2.0f * PI);
        p.spin = Random(-0.5f, 0.5f);
        Add(p);
    }

    // Газар дээрх түлэгдсэн ул мөр (удаан алга болно)
    Particle scorch = {};
    scorch.kind = Kind::Scorch;
    scorch.position = at;
    scorch.size = Random(1.6f, 2.2f);
    scorch.life = scorch.maxLife = 25.0f;
    scorch.color = { 20, 18, 16, 255 };
    scorch.rotation = Random(0.0f, 2.0f * PI);
    scorch.settled = true;
    Add(scorch);
}

void Particles::Update(float dt, const World& world)
{
    for (Blast& blast : blasts)
    {
        blast.life -= dt;
    }
    blasts.erase(std::remove_if(blasts.begin(), blasts.end(), [](const Blast& b) { return b.life <= 0.0f; }),
                 blasts.end());

    for (Particle& p : particles)
    {
        p.life -= dt;
        p.rotation += p.spin * dt * (p.kind == Kind::Casing ? 0.0f : 1.0f);
        if (p.settled)
        {
            // Түлэгдэлт: дэлбэрэлт агаарт болсон ч доорх газар дээр
            if (p.kind == Kind::Scorch) p.position.y = world.GroundHeight(p.position.x, p.position.z);
            continue;
        }

        switch (p.kind)
        {
        case Kind::Dust:
            // Тоос: удааширч, тэлнэ
            p.velocity = Vector3Scale(p.velocity, 1.0f - std::min(1.0f, 3.0f * dt));
            p.size += 0.3f * dt;
            break;
        case Kind::Puff:
            p.velocity = Vector3Scale(p.velocity, 1.0f - std::min(1.0f, 1.2f * dt));
            p.size += 1.1f * dt;
            break;
        case Kind::Smoke:
            // Утаа: хажуу тийш удааширч, дээш хөөрөх хурд нь ~1.2 м/с руу тогтворжино
            p.velocity.x *= 1.0f - std::min(1.0f, 0.8f * dt);
            p.velocity.z *= 1.0f - std::min(1.0f, 0.8f * dt);
            p.velocity.y += (1.2f - p.velocity.y) * std::min(1.0f, 0.9f * dt);
            p.size += 0.9f * dt;
            break;
        case Kind::Fire:
            p.velocity = Vector3Scale(p.velocity, 1.0f - std::min(1.0f, 4.0f * dt));
            p.velocity.y += 3.0f * dt;
            p.size += 2.4f * dt;
            break;
        case Kind::Flame:
            // Дөл: дээш хурдсаж (халуун агаар), хажуу тийш удааширч, сүүлдээ нарийсна
            p.velocity.y += 4.0f * dt;
            p.velocity.x *= 1.0f - std::min(1.0f, 2.0f * dt);
            p.velocity.z *= 1.0f - std::min(1.0f, 2.0f * dt);
            p.size *= 1.0f - std::min(0.9f, 0.9f * dt);
            break;
        case Kind::BloodMist:
            p.velocity = Vector3Scale(p.velocity, 1.0f - std::min(1.0f, 5.0f * dt));
            p.size += 0.9f * dt;
            break;
        default:
            p.velocity.y -= kGravity * dt;
            break;
        }
        p.position = Vector3Add(p.position, Vector3Scale(p.velocity, dt));

        const bool falls = p.kind == Kind::Blood || p.kind == Kind::Shell || p.kind == Kind::Casing || p.kind == Kind::Spark;
        if (!falls) continue;
        const float ground = world.GroundHeight(p.position.x, p.position.z);
        if (p.position.y <= ground + 0.02f)
        {
            p.position.y = ground + 0.02f;
            if (p.kind == Kind::Spark)
            {
                p.life = 0.0f; // оч газарт унтарна
            }
            else if ((p.kind == Kind::Shell || p.kind == Kind::Casing) && p.velocity.y < -1.5f)
            {
                // Хонгио нэг удаа ойно
                p.velocity = { p.velocity.x * 0.4f, -p.velocity.y * 0.3f, p.velocity.z * 0.4f };
            }
            else
            {
                if (p.kind == Kind::Blood)
                {
                    // Газарт тусахад цацагдаж толбо болно: хурдтай дусал томхон
                    p.size = std::clamp(0.08f + Vector3Length(p.velocity) * 0.025f, 0.1f, 0.26f) * Random(0.8f, 1.3f);
                    p.life = p.maxLife = Random(9.0f, 14.0f);
                }
                p.velocity = { 0.0f, 0.0f, 0.0f };
                p.settled = true;
            }
        }
    }
    particles.erase(std::remove_if(particles.begin(), particles.end(), [](const Particle& p) { return p.life <= 0.0f; }),
                    particles.end());

    for (MuzzleFlash& flash : flashes)
    {
        flash.life -= dt;
    }
    flashes.erase(std::remove_if(flashes.begin(), flashes.end(), [](const MuzzleFlash& f) { return f.life <= 0.0f; }),
                  flashes.end());
}

void Particles::Draw() const
{
    for (const Particle& p : particles)
    {
        if (p.kind == Kind::Casing && HasPropModel(PropModelId::Casing))
        {
            // Нисэж байхад эргэлдэнэ; газарт хажуугаараа хэвтэнэ (загварын урт тэнхлэг +Z нь хэвтээ).
            const float age = p.maxLife - p.life;
            rlPushMatrix();
            if (p.settled)
            {
                rlTranslatef(p.position.x, p.position.y - 0.014f, p.position.z);
                rlRotatef(std::atan2(p.axis.x, p.axis.z) * RAD2DEG, 0.0f, 1.0f, 0.0f);
            }
            else
            {
                rlTranslatef(p.position.x, p.position.y, p.position.z);
                rlRotatef(p.spin * age, p.axis.x, p.axis.y, p.axis.z);
            }
            rlTranslatef(0.0f, 0.0f, -0.0285f); // загварын төвийг тойрно (ёроол нь z = 0)
            DrawPropModel(PropModelId::Casing);
            rlPopMatrix();
        }
        else if (p.kind == Kind::Shell || p.kind == Kind::Casing)
        {
            // Хэлтэрхий: жигд биш хэмжээтэй жижиг хэсэг
            DrawCubeV(p.position, { p.size, p.size * 0.6f, p.size * 0.8f }, p.color);
        }
    }
}

void Particles::DrawSoft(const Camera3D& camera, Color light) const
{
    LoadSprites();
    const Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    const Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera.up));
    const Vector3 up = Vector3CrossProduct(right, forward);

    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    rlDisableBackfaceCulling();

    // 1. Газар дээрх толбо (түлэгдэлт, цус): газрын дээгүүр бага зэрэг (гүний зөрчил гарахгүй)
    rlSetTexture(sprites[kScorchSprite].id);
    rlBegin(RL_QUADS);
    for (const Particle& p : particles)
    {
        if (p.kind != Kind::Scorch) continue;
        const float fade = std::min(1.0f, p.life / 4.0f);
        Decal({ p.position.x, p.position.y + 0.05f, p.position.z }, p.size, p.rotation, Scale(p.color, WHITE, 0.85f * fade));
    }
    rlEnd();
    rlSetTexture(sprites[kSplatSprite].id);
    rlBegin(RL_QUADS);
    for (const Particle& p : particles)
    {
        if (p.kind != Kind::Blood || !p.settled) continue;
        // Хатах тусам бараан, хүрэн (хуучин толбо шинэ толбоноос ялгаатай)
        const float fade = std::min(1.0f, p.life / 2.0f), dry = 1.0f - std::min(1.0f, p.life / p.maxLife + 0.3f);
        const Color c = { (unsigned char)(p.color.r * (1.0f - 0.35f * dry)), (unsigned char)(p.color.g + 8.0f * dry),
                          (unsigned char)(p.color.b + 4.0f * dry), 255 };
        Decal({ p.position.x, p.position.y + 0.1f, p.position.z }, p.size, p.rotation, Scale(c, light, 0.85f * fade));
    }
    rlEnd();

    // 2. Нисэж буй цусны дусал: хөдөлгөөний чиглэлдээ сунасан
    rlSetTexture(sprites[kDropSprite].id);
    rlBegin(RL_QUADS);
    for (const Particle& p : particles)
    {
        if (p.kind != Kind::Blood || p.settled) continue;
        Streak(p.position, p.velocity, forward, p.size, 0.012f, Scale(p.color, light, 0.95f));
    }
    rlEnd();

    // 3. Утаа, тоос, манан: холоос ойр руу (хагас тунгалаг давхарга зөв давхцана)
    static std::vector<std::pair<float, int>> order;
    order.clear();
    for (int i = 0; i < (int)particles.size(); ++i)
    {
        const Kind kind = particles[(size_t)i].kind;
        if (kind == Kind::Dust || kind == Kind::Puff || kind == Kind::Smoke || kind == Kind::BloodMist)
        {
            order.push_back({ -Vector3DistanceSqr(particles[(size_t)i].position, camera.position), i });
        }
    }
    std::sort(order.begin(), order.end());
    rlSetTexture(sprites[kPuffSprite].id);
    rlBegin(RL_QUADS);
    for (const auto& entry : order)
    {
        const Particle& p = particles[(size_t)entry.second];
        const float t = p.life / p.maxLife;            // 1 -> 0
        const float in = std::min(1.0f, (1.0f - t) * 8.0f); // эхэндээ хурдан гарч ирнэ
        float alpha = 0.0f;
        switch (p.kind)
        {
        case Kind::Dust: alpha = 0.75f * t * in; break;
        case Kind::Puff: alpha = 0.4f * t * std::min(1.0f, (1.0f - t) * 6.0f); break;
        case Kind::Smoke: alpha = 0.7f * std::min(1.0f, t * 1.6f) * in; break;
        case Kind::BloodMist: alpha = 0.55f * t; break;
        default: break;
        }
        Billboard(p.position, right, up, p.size, p.rotation, Scale(p.color, light, alpha));
    }
    rlEnd();

    rlSetTexture(0);
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
}

void Particles::DrawUnlit(const Camera3D& camera) const
{
    bool any = !blasts.empty() || !flashes.empty();
    for (const Particle& p : particles) any = any || p.kind == Kind::Fire || p.kind == Kind::Flame || p.kind == Kind::Spark;
    if (!any) return;

    LoadSprites();
    const Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    const Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera.up));
    const Vector3 up = Vector3CrossProduct(right, forward);

    // Гэрэлтэх зүйлс: нэмэх холилтоор (давхцах тусам тодорно), гүнд бичихгүй
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    BeginBlendMode(BLEND_ADDITIVE);

    rlSetTexture(sprites[kPuffSprite].id);
    rlBegin(RL_QUADS);
    // Дэлбэрэлтийн эхний агшны тод гэрэл
    for (const Blast& blast : blasts)
    {
        const float t = 1.0f - blast.life / kBlastLife; // 0 -> 1
        const float k = (1.0f - t) * (1.0f - t);
        Billboard(blast.position, right, up, 1.2f + 3.0f * t, t * 2.0f, { (unsigned char)(255 * k), (unsigned char)(200 * k), (unsigned char)(120 * k), 255 });
    }
    // Гал: цагаан шар -> улбар -> бараан улаан
    for (const Particle& p : particles)
    {
        if (p.kind != Kind::Fire) continue;
        const float t = p.life / p.maxLife; // 1 -> 0
        const float heat = t * t, red = std::min(1.0f, t * 1.4f);
        const Color c = { (unsigned char)(255 * red), (unsigned char)(255 * red * (0.25f + 0.6f * heat)),
                          (unsigned char)(255 * red * 0.35f * heat * heat), 255 };
        Billboard(p.position, right, up, p.size, p.rotation, c);
    }
    // Шаталтын дөл: босоо сунасан, ёроол нь цагаан шар, оройд улаан болж унтарна
    for (const Particle& p : particles)
    {
        if (p.kind != Kind::Flame) continue;
        const float t = p.life / p.maxLife; // 1 -> 0
        const float in = std::min(1.0f, (1.0f - t) * 10.0f);
        // (ногоон, хөх нь улаанаас хэзээ ч их биш: унтрахдаа бараан улаан, ногоон өнгө орохгүй)
        const float red = in * std::min(1.0f, t * 1.6f);
        const Color c = { (unsigned char)(255 * red), (unsigned char)(255 * red * (0.22f + 0.6f * t)), (unsigned char)(255 * red * 0.3f * t * t), 255 };
        const float c0 = std::cos(p.rotation), s0 = std::sin(p.rotation);
        const Vector3 r = Vector3Scale(Vector3Add(Vector3Scale(right, c0), Vector3Scale(up, s0)), 0.85f);
        const Vector3 u = Vector3Scale(Vector3Add(Vector3Scale(right, -s0), Vector3Scale(up, c0)), 2.0f); // босоо сунасан
        Billboard(p.position, r, u, p.size, 0.0f, c);
    }
    // Бууны амны гал, оч: тод цэг (эргэлт нь гал бүрд өөр)
    for (const MuzzleFlash& flash : flashes)
    {
        const float k = flash.life / kFlashLife;
        const float rotation = flash.position.x * 7.0f + flash.position.z * 3.0f;
        Billboard(Vector3Add(flash.position, Vector3Scale(flash.direction, 0.08f)), right, up, 0.16f + 0.1f * k, rotation,
                  { 255, (unsigned char)(200 + 40 * k), (unsigned char)(120 + 60 * k), 255 });
    }
    rlEnd();

    // Амны галын урагш цацрах дөл: бууны чиглэлд сунасан
    rlSetTexture(sprites[kDropSprite].id);
    rlBegin(RL_QUADS);
    for (const MuzzleFlash& flash : flashes)
    {
        const float k = flash.life / kFlashLife;
        Streak(Vector3Add(flash.position, Vector3Scale(flash.direction, 0.22f)), Vector3Scale(flash.direction, 10.0f), forward,
               0.07f + 0.03f * k, 0.022f, { 255, 190, 90, 255 });
    }
    rlEnd();

    rlSetTexture(sprites[kDropSprite].id);
    rlBegin(RL_QUADS);
    for (const Particle& p : particles)
    {
        if (p.kind != Kind::Spark) continue;
        const float t = p.life / p.maxLife;
        Streak(p.position, p.velocity, forward, p.size, 0.02f,
               { 255, (unsigned char)(170 + 70 * t), (unsigned char)(80 * t), (unsigned char)(255 * std::min(1.0f, t * 2.0f)) });
    }
    rlEnd();
    rlSetTexture(0);

    EndBlendMode();
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
}

void Particles::Clear()
{
    particles.clear();
    flashes.clear();
    blasts.clear();
}
