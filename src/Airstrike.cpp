#include "Airstrike.h"

#include "Input.h"
#include "Mover.h"
#include "Particles.h"
#include "UiStyle.h"
#include "World.h"
#include "Zombie.h"
#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>

namespace
{
    // Цагийн хуваарь (секунд)
    const float kRadioEnd = 2.2f;   // холбоогоор дуудах
    const float kFlyStart = 0.8f;   // онгоцнууд алсаас ирж эхэлнэ
    const float kFlyTime = 6.8f;    // нутаг дэвсгэрийг нэвт нисэх хугацаа
    const float kClearTime = 8.6f;  // үлдсэн бүх зомби устана
    const float kFlyHalf = 300.0f;  // нислэгийн замын хагас урт, метр
    const float kFallTime = 1.3f;   // бөмбөг унах хугацаа
    const int kMaxBombs = 70;

    const Vector3 kFormation[3] = { { 0.0f, 0.0f, 0.0f }, { -22.0f, 4.0f, -26.0f }, { 22.0f, -3.0f, -26.0f } }; // (хажуу, өндөр, урагш)

    const Color kBody = { 108, 118, 112, 255 };
    const Color kBodyDark = { 74, 82, 78, 255 };
    const Color kCanopy = { 40, 52, 64, 255 };
    const Color kStar = { 190, 40, 40, 255 };

    float Smooth(float x)
    {
        x = std::clamp(x, 0.0f, 1.0f);
        return x * x * (3.0f - 2.0f * x);
    }
}

void Airstrike::Start(Vector3 newCenter, float heading)
{
    active = true;
    skippable = true;
    cleared = false;
    time = 0.0f;
    shake = 0.0f;
    center = newCenter;
    direction = { std::sin(heading), 0.0f, std::cos(heading) };
    side = { -direction.z, 0.0f, direction.x };
    altitude = center.y + 55.0f;

    bombs.clear();
    targeted = false;
    camera.up = { 0.0f, 1.0f, 0.0f };
    camera.fovy = 55.0f;
    camera.projection = CAMERA_PERSPECTIVE;
    UpdateCamera(0.0f);
}

void Airstrike::Target(const ZombieHorde& horde)
{
    // Бай: нислэгийн замын ойролцоох амьд зомбинууд (замын дагуу эрэмбэлж, хаях агшныг тооцно),
    // зай завсарт нь шугамаар хэдэн бөмбөг нэмж "хивс" бөмбөгдөлт хийнэ.
    std::vector<std::pair<float, Vector3>> targets;
    for (int i = 0; i < horde.Count(); ++i)
    {
        const Zombie& zombie = horde.At(i);
        if (!zombie.IsAlive()) continue;
        const Vector3 offset = Vector3Subtract(zombie.mover.position, center);
        const float along = Vector3DotProduct(offset, direction);
        const float across = Vector3DotProduct(offset, side);
        if (std::fabs(along) > kFlyHalf - 60.0f || std::fabs(across) > 70.0f) continue;
        targets.push_back({ HorizontalDistance(zombie.mover.position, center), zombie.mover.position });
    }
    std::sort(targets.begin(), targets.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    if ((int)targets.size() > kMaxBombs - 12) targets.resize(kMaxBombs - 12);
    for (float along = -150.0f; along <= 150.0f; along += 30.0f)
    {
        const float lateral = (along / 30.0f) * 7.0f;
        targets.push_back({ 0.0f, Vector3Add(Vector3Add(center, Vector3Scale(direction, along)),
                                              Vector3Scale(side, std::fmod(lateral, 24.0f) - 12.0f)) });
    }

    const float speed = 2.0f * kFlyHalf / kFlyTime;
    for (size_t i = 0; i < targets.size(); ++i)
    {
        const Vector3 to = targets[i].second;
        const float along = Vector3DotProduct(Vector3Subtract(to, center), direction);
        // Онгоц бай хүрэхээс өмнө (унах хугацаанд урагшлах зайгаар) хаяна.
        const float lead = speed * kFallTime * 0.6f;
        const float release = kFlyStart + (along - lead + kFlyHalf) / speed;
        const int plane = (int)(i % 3);
        Bomb bomb;
        bomb.to = to;
        bomb.release = std::max(kFlyStart + 0.5f, release);
        bomb.from = PlanePosition(plane, bomb.release);
        bombs.push_back(bomb);
    }
}

Vector3 Airstrike::PlanePosition(int index, float at) const
{
    const float along = -kFlyHalf + 2.0f * kFlyHalf * (at - kFlyStart) / kFlyTime;
    const Vector3 slot = kFormation[index];
    Vector3 p = Vector3Add(center, Vector3Scale(direction, along + slot.z));
    p = Vector3Add(p, Vector3Scale(side, slot.x));
    p.y = altitude + slot.y;
    return p;
}

void Airstrike::Update(float dt, const World& world, ZombieHorde& horde, Particles& particles)
{
    if (!active) return;
    if (!targeted)
    {
        Target(horde); // бай: онгоц ирэх үеийн зомбинуудын байрлал
        targeted = true;
    }
    time += dt;
    shake = std::max(0.0f, shake - dt * 2.5f);

    for (Bomb& bomb : bombs)
    {
        if (bomb.exploded || time < bomb.release) continue;
        bomb.dropped = true;
        if (time - bomb.release >= kFallTime)
        {
            bomb.exploded = true;
            Vector3 at = bomb.to;
            at.y = world.GroundHeight(at.x, at.z);
            particles.Explosion(at);
            horde.Ram(at, 5.0f, 1.0e6f, true, Vector3Scale(direction, 9.0f)); // ойрынхыг агаарт шидэнэ
            horde.Explode(at, 11.0f, 4000.0f);
            shake = std::min(1.0f, shake + 0.35f);
        }
    }

    // Төгсгөл: замаас хол байсан зомбинууд ч бүгд устана (бүх нутаг дэвсгэр цэвэрлэгдэнэ).
    if (!cleared && time >= kClearTime)
    {
        cleared = true;
        horde.Explode(center, 1.0e7f, 1.0e9f);
    }

    UpdateCamera(dt);
}

void Airstrike::UpdateCamera(float dt)
{
    (void)dt;
    const Vector3 lead = PlanePosition(0, std::clamp(time, kFlyStart, kFlyStart + kFlyTime));
    const Vector3 ground = Vector3Add(center, { 0.0f, 2.0f, 0.0f });

    // 1) Холбоо: тоглогчийн арын камер онгоц ирэх зүг рүү тэнгэрт харна.
    const Vector3 behind = Vector3Add(Vector3Add(center, Vector3Scale(direction, 9.0f)), { 0.0f, 4.0f, 0.0f });
    const Vector3 behindTarget = Vector3Lerp(Vector3Add(center, Vector3Scale(direction, -60.0f)), lead, 0.5f);
    // 2) Онгоцнууд толгой дээгүүр: доороос, хажуугаас дагаж харна.
    const Vector3 under = Vector3Add(Vector3Add(center, Vector3Scale(side, 28.0f)), { 0.0f, 10.0f, 0.0f });
    // 3) Бөмбөгдөлт: хажуугийн өндөр цэгээс газар дээрх дэлбэрэлтийг ойроос харна.
    const Vector3 high = Vector3Add(Vector3Add(Vector3Add(center, Vector3Scale(side, 52.0f)), Vector3Scale(direction, -18.0f)),
                                    { 0.0f, 24.0f, 0.0f });
    // 4) Төгсгөл: утаатай талбарыг тойрон аажим эргэнэ.
    const float orbit = (time - kClearTime) * 0.15f;
    const Vector3 around = Vector3Add(center, Vector3Add(Vector3Scale(side, std::cos(orbit) * 55.0f),
                                                         Vector3Add(Vector3Scale(direction, std::sin(orbit) * 55.0f),
                                                                    { 0.0f, 26.0f, 0.0f })));

    const float a = Smooth((time - kRadioEnd) / 1.0f);
    const float b = Smooth((time - 4.4f) / 1.2f);
    const float c = Smooth((time - kClearTime) / 1.0f);
    Vector3 position = Vector3Lerp(Vector3Lerp(Vector3Lerp(behind, under, a), high, b), around, c);
    Vector3 target = Vector3Lerp(Vector3Lerp(Vector3Lerp(behindTarget, Vector3Lerp(lead, ground, 0.35f), a), ground, b), ground, c);

    // Дэлбэрэлтийн чичиргээ
    if (shake > 0.0f)
    {
        const float s = shake * 0.5f;
        position.x += s * std::sin(time * 61.0f);
        position.y += s * std::sin(time * 47.0f + 1.3f);
        target.x += s * 0.5f * std::sin(time * 53.0f + 0.7f);
    }
    camera.position = position;
    camera.target = target;
}

void Airstrike::Draw() const
{
    if (!active) return;

    // Онгоцнууд: их бие, далавч, сүүл, бүхээг (нислэгийн зүг рүү эргүүлсэн)
    const float yaw = std::atan2(direction.x, direction.z) * RAD2DEG;
    for (int i = 0; i < 3; ++i)
    {
        if (time < kFlyStart || time > kFlyStart + kFlyTime + 1.0f) break;
        const Vector3 p = PlanePosition(i, time);
        rlPushMatrix();
        rlTranslatef(p.x, p.y, p.z);
        rlRotatef(yaw, 0.0f, 1.0f, 0.0f);
        rlRotatef(3.0f * std::sin(time * 1.7f + (float)i), 0.0f, 0.0f, 1.0f); // бага зэрэг найгана
        DrawCube({ 0.0f, 0.0f, 0.0f }, 1.6f, 1.5f, 13.0f, kBody);                 // их бие
        DrawCube({ 0.0f, 0.0f, 7.0f }, 1.1f, 1.0f, 1.6f, kBodyDark);              // хамар
        DrawCube({ 0.0f, 0.75f, 2.8f }, 0.9f, 0.6f, 2.6f, kCanopy);               // бүхээг
        DrawCube({ 0.0f, -0.1f, 0.0f }, 14.0f, 0.25f, 3.6f, kBody);               // далавч
        DrawCube({ 0.0f, 0.0f, -5.8f }, 5.2f, 0.2f, 1.6f, kBody);                 // сүүлний далавч
        DrawCube({ 0.0f, 1.4f, -5.6f }, 0.25f, 2.4f, 1.8f, kBodyDark);            // сүүлний сэрвээ
        DrawCube({ 5.0f, -0.05f, 0.2f }, 1.0f, 0.3f, 1.0f, kStar);                // далавчны тэмдэг
        DrawCube({ -5.0f, -0.05f, 0.2f }, 1.0f, 0.3f, 1.0f, kStar);
        DrawCube({ 0.0f, 0.0f, -6.7f }, 0.9f, 0.9f, 0.4f, { 255, 170, 60, 255 }); // хөдөлгүүрийн гал
        rlPopMatrix();
    }

    // Унаж буй бөмбөгнүүд: хаясан цэгээс бай руу нум зурж унана
    for (const Bomb& bomb : bombs)
    {
        if (!bomb.dropped || bomb.exploded) continue;
        const float u = std::clamp((time - bomb.release) / kFallTime, 0.0f, 1.0f);
        Vector3 p = Vector3Lerp(bomb.from, bomb.to, u);
        p.y = bomb.from.y + (bomb.to.y - bomb.from.y) * u * u;
        DrawCube(p, 0.45f, 1.3f, 0.45f, { 50, 54, 48, 255 });
    }
}

void Airstrike::DrawOverlay() const
{
    if (!active) return;
    const int width = ViewWidth();
    const int height = ViewHeight();

    // Киноны хар хүрээ (дээд, доод)
    const int bar = (int)(height * 0.11f * Smooth(time / 0.6f));
    DrawRectangle(0, 0, width, bar, BLACK);
    DrawRectangle(0, height - bar, width, bar, BLACK);

    // Холбооны бичиг: доод хүрээн дээр
    const char* line = nullptr;
    const char* speaker = nullptr;
    if (time < 2.4f) { speaker = "ХОЛБОО"; line = "Агаарын дэмжлэг хэрэгтэй! Бүх даалгавар биеллээ — үлдсэн зомбийг цэвэрлэ!"; }
    else if (time < 5.0f) { speaker = "НИСГЭГЧ"; line = "Ойлголоо. Бай руу орж байна... Бөмбөг хаялаа!"; }
    else if (time < kClearTime) { speaker = "НИСГЭГЧ"; line = "Бүсийг бүхэлд нь бөмбөгдөж байна!"; }
    else { speaker = "ХОЛБОО"; line = "Нутаг дэвсгэр зомбиос цэвэрлэгдлээ. Сайн ажиллалаа!"; }
    if (line && bar > 20)
    {
        const int y = height - bar + (bar - 26) / 2;
        const int speakerWidth = Ui::MeasureBold(speaker, 20);
        const int lineWidth = Ui::Measure(line, 20);
        const int x = (width - speakerWidth - 16 - lineWidth) / 2;
        Ui::TextBold(speaker, x, y, 20, Ui::kAccent);
        Ui::Text(line, x + speakerWidth + 16, y, 20, Ui::kText);
    }
    const char* skip = skippable ? "ENTER / товших — алгасах" : "Хост дуусгахыг хүлээж байна";
    Ui::Text(skip, width - Ui::Measure(skip, 14) - 16, bar > 30 ? (bar - 18) / 2 : 8, 14, Ui::kTextDim);

    // Төгсгөлд харанхуй руу бүдгэрнэ (дараа нь ялалтын дэлгэц)
    const float fade = Smooth((time - (kDuration - 0.8f)) / 0.8f);
    if (fade > 0.0f) DrawRectangle(0, 0, width, height, Fade(BLACK, fade));
}
