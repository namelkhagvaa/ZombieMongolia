#include "Particles.h"

#include "Audio.h"
#include "VehicleModel.h"
#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>

namespace
{
    const size_t kMaxParticles = 260; // сул компьютерт хязгаартай
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
        p.size = Random(0.04f, 0.09f);
        p.life = p.maxLife = Random(2.5f, 4.0f);
        p.color = { (unsigned char)GetRandomValue(110, 160), (unsigned char)GetRandomValue(10, 24), 16, 255 };
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
        p.life = p.maxLife = Random(0.3f, 0.6f);
        p.color = { 176, 160, 130, 255 };
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

void Particles::Explosion(Vector3 at)
{
    // Унаа гэмтээх төв (нэг дэлбэрэлтийн хоёр бөмбөлгийг нэг гэж тооцно)
    bool known = false;
    for (const Vector3& b : recentBlasts) known = known || Vector3Distance(b, at) < 2.5f;
    if (!known)
    {
        recentBlasts.push_back(at);
        Audio::Play(Sfx::Explosion, at, 1.0f, 0.9f + 0.2f * (float)GetRandomValue(0, 100) / 100.0f);
    }
    blasts.push_back({ at, kBlastLife });

    // Тоос шороо: өргөн тархаж удаан сарнина
    for (int i = 0; i < 26; ++i)
    {
        Particle p = {};
        p.kind = Kind::Dust;
        p.position = Vector3Add(at, RandomSpread(0.6f));
        p.velocity = RandomSpread(6.0f);
        p.velocity.y = Random(1.5f, 7.0f);
        p.size = Random(0.18f, 0.4f);
        p.life = p.maxLife = Random(0.7f, 1.5f);
        p.color = i % 3 == 0 ? Color{ 70, 64, 58, 255 } : Color{ 150, 132, 104, 255 };
        Add(p);
    }

    // Хэлтэрхий: хурдан нисэж газарт унана
    for (int i = 0; i < 14; ++i)
    {
        Particle p = {};
        p.kind = Kind::Shell;
        p.position = at;
        p.velocity = RandomSpread(9.0f);
        p.velocity.y = Random(3.0f, 10.0f);
        p.size = Random(0.04f, 0.08f);
        p.life = p.maxLife = Random(1.0f, 2.0f);
        p.color = { 52, 48, 44, 255 };
        Add(p);
    }
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
        if (p.settled)
        {
            continue;
        }

        if (p.kind == Kind::Dust)
        {
            // Тоос: удааширч, тэлнэ
            p.velocity = Vector3Scale(p.velocity, 1.0f - std::min(1.0f, 3.0f * dt));
            p.size += 0.25f * dt;
        }
        else if (p.kind == Kind::Puff)
        {
            p.velocity = Vector3Scale(p.velocity, 1.0f - std::min(1.0f, 1.2f * dt));
            p.size += 1.1f * dt;
        }
        else
        {
            p.velocity.y -= kGravity * dt;
        }
        p.position = Vector3Add(p.position, Vector3Scale(p.velocity, dt));

        const float ground = world.GroundHeight(p.position.x, p.position.z);
        if (p.kind != Kind::Dust && p.kind != Kind::Puff && p.position.y <= ground + 0.02f)
        {
            p.position.y = ground + 0.02f;
            if ((p.kind == Kind::Shell || p.kind == Kind::Casing) && p.velocity.y < -1.5f)
            {
                // Хонгио нэг удаа ойно
                p.velocity = { p.velocity.x * 0.4f, -p.velocity.y * 0.3f, p.velocity.z * 0.4f };
            }
            else
            {
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
        if (p.kind == Kind::Blood && p.settled)
        {
            // Газар дээрх цусны толбо: хавтгай, аажим алга болно
            const float fade = std::min(1.0f, p.life / 0.8f);
            const float spread = p.size * 2.6f;
            DrawCubeV({ p.position.x, p.position.y + 0.12f, p.position.z }, { spread, 0.01f, spread },
                      Fade(p.color, 0.85f * fade));
        }
        else if (p.kind == Kind::Dust)
        {
            DrawCubeV(p.position, { p.size, p.size, p.size }, Fade(p.color, 0.7f * p.life / p.maxLife));
        }
        else if (p.kind == Kind::Puff)
        {
            continue; // доор: гүний буферт бичихгүйгээр
        }
        else if (p.kind == Kind::Casing && HasPropModel(PropModelId::Casing))
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
        else
        {
            DrawCubeV(p.position, { p.size, p.size, p.size }, p.color);
        }
    }
}

void Particles::DrawPuffs() const
{
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    for (const Particle& p : particles)
    {
        if (p.kind != Kind::Puff) continue;
        const float t = p.life / p.maxLife;
        DrawSphereEx(p.position, p.size, 4, 6, Fade(p.color, 0.32f * t * std::min(1.0f, (1.0f - t) * 6.0f)));
    }
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
}

void Particles::DrawUnlit() const
{
    for (const MuzzleFlash& flash : flashes)
    {
        const float scale = 0.6f + 0.4f * flash.life / kFlashLife;
        const Vector3 tip = Vector3Add(flash.position, Vector3Scale(flash.direction, 0.45f * scale));
        DrawCylinderEx(flash.position, tip, 0.09f * scale, 0.0f, 6, { 255, 176, 64, 255 });
        DrawSphereEx(flash.position, 0.09f * scale, 4, 6, { 255, 240, 170, 255 });
    }

    // Дэлбэрэлтийн галт бөмбөлөг: хурдан тэлж, бүдгэрнэ
    for (const Blast& blast : blasts)
    {
        const float t = 1.0f - blast.life / kBlastLife; // 0 -> 1
        const float radius = 0.6f + 3.2f * t;
        DrawSphereEx(blast.position, radius, 6, 8, Fade({ 255, 150, 50, 255 }, 0.75f * (1.0f - t)));
        DrawSphereEx(blast.position, radius * 0.55f, 5, 8, Fade({ 255, 236, 170, 255 }, 0.9f * (1.0f - t)));
    }
}

void Particles::Clear()
{
    particles.clear();
    flashes.clear();
    blasts.clear();
}
