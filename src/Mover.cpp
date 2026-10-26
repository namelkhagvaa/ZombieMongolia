#include "Mover.h"

#include <algorithm>
#include <cmath>

float HorizontalDistance(Vector3 a, Vector3 b)
{
    const float dx = a.x - b.x;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

void MoveToward(Mover& mover, Vector3 target, float speed, float dt, const World& world, float radius,
                float height, float stepHeight)
{
    const float dist = HorizontalDistance(mover.position, target);
    if (dist < 0.01f)
    {
        return;
    }

    float dirX = (target.x - mover.position.x) / dist;
    float dirZ = (target.z - mover.position.z) / dist;
    mover.wishDir = { dirX, dirZ };

    const bool detouring = mover.detourTimer > 0.0f;
    if (detouring)
    {
        // Зорилтын чиглэлд перпендикуляр, бага зэрэг урагш: хана дагаж булан хүртэл гулсана.
        mover.detourTimer -= dt;
        const float sideX = -dirZ * mover.detourSign + dirX * 0.25f;
        const float sideZ = dirX * mover.detourSign + dirZ * 0.25f;
        const float len = std::sqrt(sideX * sideX + sideZ * sideZ);
        dirX = sideX / len;
        dirZ = sideZ / len;
    }

    const Vector3 before = mover.position;
    speed *= world.WadeFactor(mover.position); // усанд удаан (гүнд сэлнэ)
    const float step = std::min(speed * dt, dist); // урт кадр (хөтөч, утас) дээр зорилтоо давж эргэлдэхгүй
    mover.position.x += dirX * step;
    mover.position.z += dirZ * step;
    world.ResolveHorizontal(mover.position, radius, height, stepHeight);
    mover.wishStep = step;
    mover.moved = HorizontalDistance(before, mover.position);

    if (!detouring && HorizontalDistance(before, mover.position) < step * 0.35f)
    {
        mover.detourTimer = 0.9f;
    }

    const float targetFacing = std::atan2(dirX, dirZ);
    const float diff = std::remainder(targetFacing - mover.facing, 2.0f * PI);
    mover.facing += diff * std::min(1.0f, 10.0f * dt);
}
