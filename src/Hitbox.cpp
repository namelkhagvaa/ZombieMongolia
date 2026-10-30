#include "Hitbox.h"

#include "raymath.h"

#include <algorithm>
#include <cmath>

namespace
{
    struct Capsule
    {
        Vector3 a, b; // хөлийн орон зайд (x баруун тийш -X, y дээш, z урагш), зогсож буй биеийн хэмжээгээр
        float radius;
        HitZone zone;
    };

    // Зогсож буй (буугаа барьсан) бие. Баруун гар нь -X талд (Humanoid-тэй ижил).
    const Capsule kStand[] = {
        { { 0.0f, 1.62f, 0.03f }, { 0.0f, 1.62f, 0.03f }, 0.12f, HitZone::Head },
        { { 0.0f, 1.18f, 0.0f }, { 0.0f, 1.36f, 0.02f }, 0.21f, HitZone::Chest },
        { { 0.0f, 0.9f, 0.0f }, { 0.0f, 1.12f, 0.0f }, 0.165f, HitZone::Stomach },
        { { -0.08f, 0.85f, 0.0f }, { -0.09f, 0.08f, 0.0f }, 0.1f, HitZone::Leg },
        { { 0.08f, 0.85f, 0.0f }, { 0.09f, 0.08f, 0.0f }, 0.1f, HitZone::Leg },
        { { -0.19f, 1.42f, 0.0f }, { -0.06f, 1.22f, 0.36f }, 0.055f, HitZone::Arm },
        { { 0.19f, 1.42f, 0.0f }, { 0.03f, 1.3f, 0.5f }, 0.05f, HitZone::Arm },
    };
    // Суусан (өвдөг нугалсан, бие урагш бөхийсөн): толгой ~1.22 м
    const Capsule kCrouch[] = {
        { { 0.0f, 1.22f, 0.18f }, { 0.0f, 1.22f, 0.18f }, 0.115f, HitZone::Head },
        { { 0.0f, 0.82f, 0.06f }, { 0.0f, 1.06f, 0.14f }, 0.19f, HitZone::Chest },
        { { 0.0f, 0.58f, 0.0f }, { 0.0f, 0.8f, 0.05f }, 0.165f, HitZone::Stomach },
        { { -0.1f, 0.55f, 0.0f }, { -0.1f, 0.5f, 0.4f }, 0.1f, HitZone::Leg },
        { { 0.1f, 0.55f, 0.0f }, { 0.1f, 0.5f, 0.4f }, 0.1f, HitZone::Leg },
        { { -0.09f, 0.5f, 0.4f }, { -0.09f, 0.06f, 0.3f }, 0.1f, HitZone::Leg },
        { { 0.09f, 0.5f, 0.4f }, { 0.09f, 0.06f, 0.3f }, 0.1f, HitZone::Leg },
        { { -0.19f, 1.02f, 0.12f }, { -0.06f, 0.85f, 0.45f }, 0.055f, HitZone::Arm },
        { { 0.19f, 1.02f, 0.12f }, { 0.03f, 0.92f, 0.6f }, 0.05f, HitZone::Arm },
    };
    // Унаанд сууж яваа: их бие зогсож буйтай ижил, гуя урагш хэвтээ
    const Capsule kSeated[] = {
        { { 0.0f, 1.62f, 0.03f }, { 0.0f, 1.62f, 0.03f }, 0.12f, HitZone::Head },
        { { 0.0f, 1.18f, 0.0f }, { 0.0f, 1.36f, 0.02f }, 0.21f, HitZone::Chest },
        { { 0.0f, 0.9f, 0.0f }, { 0.0f, 1.12f, 0.0f }, 0.165f, HitZone::Stomach },
        { { -0.09f, 0.88f, 0.0f }, { -0.09f, 0.85f, 0.45f }, 0.1f, HitZone::Leg },
        { { 0.09f, 0.88f, 0.0f }, { 0.09f, 0.85f, 0.45f }, 0.1f, HitZone::Leg },
        { { -0.09f, 0.85f, 0.45f }, { -0.09f, 0.1f, 0.5f }, 0.09f, HitZone::Leg },
        { { 0.09f, 0.85f, 0.45f }, { 0.09f, 0.1f, 0.5f }, 0.09f, HitZone::Leg },
        { { -0.19f, 1.42f, 0.0f }, { -0.06f, 1.22f, 0.36f }, 0.055f, HitZone::Arm },
        { { 0.19f, 1.42f, 0.0f }, { 0.03f, 1.3f, 0.5f }, 0.05f, HitZone::Arm },
    };
    // Хэвтэх: гар, буу толгой дээгүүр урагш сунана (зогсож буй биеийн орон зайд, дараа нь эргүүлнэ)
    const Capsule kProneArms[] = {
        { { -0.19f, 1.42f, 0.0f }, { -0.08f, 1.95f, 0.05f }, 0.055f, HitZone::Arm },
        { { 0.19f, 1.42f, 0.0f }, { 0.05f, 1.98f, 0.05f }, 0.05f, HitZone::Arm },
    };

    Vector3 RotateX(Vector3 p, float degrees)
    {
        const float c = std::cos(degrees * DEG2RAD), s = std::sin(degrees * DEG2RAD);
        return { p.x, p.y * c - p.z * s, p.y * s + p.z * c };
    }

    // Хэвтэх, унах: DrawHumanoid-ийн эргүүлэлттэй ижил
    Vector3 Posed(Vector3 p, BodyPosture posture)
    {
        if (posture == BodyPosture::Prone) return Vector3Add(RotateX(p, 86.0f), { 0.0f, 0.26f, -0.85f });
        if (posture == BodyPosture::Downed) return Vector3Add(RotateX(p, -88.0f), { 0.0f, 0.16f, 0.0f });
        return p;
    }

    Vector3 ToWorld(Vector3 feet, float facing, Vector3 p)
    {
        const float c = std::cos(facing), s = std::sin(facing);
        return { feet.x + p.x * c + p.z * s, feet.y + p.y, feet.z - p.x * s + p.z * c };
    }

    // Туяа ба капсулын огтлолцол: туяа капсулын тэнхлэгт хамгийн ойртох цэгээс радиусын хэмжээгээр өмнө
    bool RayCapsule(Ray ray, Vector3 a, Vector3 b, float radius, float& t)
    {
        const Vector3 ab = Vector3Subtract(b, a), ao = Vector3Subtract(ray.position, a);
        const float abab = Vector3DotProduct(ab, ab), abd = Vector3DotProduct(ab, ray.direction);
        const float abao = Vector3DotProduct(ab, ao), dao = Vector3DotProduct(ray.direction, ao);
        float s = 0.0f, rayT = 0.0f;
        if (abab < 1e-6f)
        {
            rayT = -dao; // бөмбөлөг
        }
        else
        {
            const float denom = abab - abd * abd;
            s = denom > 1e-6f ? std::clamp((abao - abd * dao) / denom, 0.0f, 1.0f) : 0.0f;
            rayT = Vector3DotProduct(ray.direction, Vector3Subtract(Vector3Add(a, Vector3Scale(ab, s)), ray.position));
            // Туяан дээрх цэгт хамгийн ойрын тэнхлэгийн цэгийг дахин тооцно
            const Vector3 onRay = Vector3Add(ray.position, Vector3Scale(ray.direction, std::max(0.0f, rayT)));
            s = std::clamp(Vector3DotProduct(Vector3Subtract(onRay, a), ab) / abab, 0.0f, 1.0f);
            rayT = Vector3DotProduct(ray.direction, Vector3Subtract(Vector3Add(a, Vector3Scale(ab, s)), ray.position));
        }
        if (rayT < 0.0f) return false;
        const Vector3 axis = Vector3Add(a, Vector3Scale(ab, s));
        const Vector3 onRay = Vector3Add(ray.position, Vector3Scale(ray.direction, rayT));
        const float gap = Vector3Distance(axis, onRay);
        if (gap > radius) return false;
        t = rayT - std::sqrt(radius * radius - gap * gap);
        return true;
    }

    template <typename F> void ForEachCapsule(BodyPosture posture, F&& visit)
    {
        switch (posture)
        {
        case BodyPosture::Crouch:
            for (const Capsule& c : kCrouch) visit(c);
            break;
        case BodyPosture::Seated:
            for (const Capsule& c : kSeated) visit(c);
            break;
        case BodyPosture::Prone:
            for (const Capsule& c : kStand)
            {
                if (c.zone != HitZone::Arm) visit(c);
            }
            for (const Capsule& c : kProneArms) visit(c);
            break;
        default:
            for (const Capsule& c : kStand) visit(c);
            break;
        }
    }
}

bool RaycastBody(Ray ray, Vector3 feet, float facing, BodyPosture posture, float minDistance, float& distance, HitZone& zone)
{
    // Хурдан шалгалт: биеийг багтаах бөмбөлгөөр (хэвтсэн бие хөлөөс 0.85 м хойш, толгой 0.8 м урагш)
    const Vector3 middle = { feet.x, feet.y + (posture == BodyPosture::Prone || posture == BodyPosture::Downed ? 0.3f : 0.9f), feet.z };
    const RayCollision bound = GetRayCollisionSphere(ray, middle, 1.9f);
    if (!bound.hit) return false;

    bool hit = false;
    ForEachCapsule(posture, [&](const Capsule& capsule) {
        const Vector3 a = ToWorld(feet, facing, Posed(capsule.a, posture));
        const Vector3 b = ToWorld(feet, facing, Posed(capsule.b, posture));
        float t = 0.0f;
        if (RayCapsule(ray, a, b, capsule.radius, t) && t > minDistance && t < distance)
        {
            distance = t;
            zone = capsule.zone;
            hit = true;
        }
    });
    return hit;
}

float HitZoneMultiplier(HitZone zone)
{
    switch (zone)
    {
    case HitZone::Head: return 4.0f;
    case HitZone::Stomach: return 1.25f;
    case HitZone::Leg: return 0.75f;
    default: return 1.0f;
    }
}

void DrawBodyHitbox(Vector3 feet, float facing, BodyPosture posture)
{
    ForEachCapsule(posture, [&](const Capsule& capsule) {
        const Vector3 a = ToWorld(feet, facing, Posed(capsule.a, posture));
        const Vector3 b = ToWorld(feet, facing, Posed(capsule.b, posture));
        const Color color = capsule.zone == HitZone::Head ? RED : capsule.zone == HitZone::Leg ? SKYBLUE
                          : capsule.zone == HitZone::Arm ? YELLOW : LIME;
        DrawCapsuleWires(a, b, capsule.radius, 8, 4, color);
    });
}

Vector3 BodyPartCenter(Vector3 feet, float facing, BodyPosture posture, int part)
{
    const Capsule* list = posture == BodyPosture::Crouch ? kCrouch : posture == BodyPosture::Seated ? kSeated : kStand;
    const Capsule& c = part == 0 ? list[0] : part == 1 ? list[1] : list[3];
    const Vector3 local = Vector3Scale(Vector3Add(c.a, c.b), 0.5f);
    return ToWorld(feet, facing, Posed(local, posture));
}
