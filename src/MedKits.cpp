#include "MedKits.h"

#include "Mover.h"
#include "Player.h"
#include "Soldier.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>

namespace
{
    const float kPickupRadius = 1.6f;
    const float kSize = 0.6f;
    const float kDrawDistance = 160.0f;

    const Color kBox = { 240, 240, 236, 255 };
    const Color kCross = { 210, 30, 36, 255 };
}

void MedKits::Drop(const World& world, Vector3 position)
{
    position.y = world.GroundHeight(position.x, position.z);
    kits.push_back(position);
}

void MedKits::RemoveNear(Vector3 position, float radius)
{
    for (size_t i = 0; i < kits.size(); ++i)
    {
        if (HorizontalDistance(kits[i], position) < radius)
        {
            kits.erase(kits.begin() + (long)i);
            return;
        }
    }
}

bool MedKits::Update(float dt, Player& player, SoldierSquad& squad, Vector3* pickedAt)
{
    time += dt;
    bool picked = false;
    for (size_t i = 0; i < kits.size();)
    {
        const Vector3 kit = kits[i];
        bool used = false;
        if (player.IsAlive() && HorizontalDistance(player.Position(), kit) < kPickupRadius)
        {
            player.Heal(player.MaxHealth());
            used = true;
            if (pickedAt) *pickedAt = kit;
        }
        else
        {
            // Дагаж буй багийн гишүүд (элсүүлсэн жирийн цэрэг биш)
            for (Soldier& soldier : squad.All())
            {
                if (soldier.alive && soldier.member >= 0 && HorizontalDistance(soldier.mover.position, kit) < kPickupRadius)
                {
                    soldier.health = soldier.maxHealth;
                    used = true;
                    break;
                }
            }
        }
        if (used)
        {
            kits.erase(kits.begin() + (long)i);
            picked = true;
        }
        else
        {
            ++i;
        }
    }
    return picked;
}

void MedKits::Draw(Vector3 viewPosition) const
{
    for (const Vector3& kit : kits)
    {
        if (HorizontalDistance(viewPosition, kit) > kDrawDistance) continue;
        // Бага зэрэг дээш доош хөвж, эргэлдэнэ
        const float bob = 0.12f * std::sin(time * 2.5f);
        rlPushMatrix();
        rlTranslatef(kit.x, kit.y + 0.55f + bob, kit.z);
        rlRotatef(time * 60.0f, 0.0f, 1.0f, 0.0f);
        DrawCube({ 0.0f, 0.0f, 0.0f }, kSize, kSize * 0.7f, kSize, kBox);
        // Тал бүр ба дээд талд улаан загалмай
        const float h = kSize * 0.5f + 0.006f;
        for (float side : { -1.0f, 1.0f })
        {
            DrawCube({ 0.0f, 0.0f, side * h }, kSize * 0.5f, kSize * 0.14f, 0.01f, kCross);
            DrawCube({ 0.0f, 0.0f, side * h }, kSize * 0.14f, kSize * 0.5f, 0.01f, kCross);
            DrawCube({ side * h, 0.0f, 0.0f }, 0.01f, kSize * 0.14f, kSize * 0.5f, kCross);
            DrawCube({ side * h, 0.0f, 0.0f }, 0.01f, kSize * 0.5f, kSize * 0.14f, kCross);
        }
        const float top = kSize * 0.35f + 0.006f;
        DrawCube({ 0.0f, top, 0.0f }, kSize * 0.5f, 0.01f, kSize * 0.14f, kCross);
        DrawCube({ 0.0f, top, 0.0f }, kSize * 0.14f, 0.01f, kSize * 0.5f, kCross);
        rlPopMatrix();
    }
}

void MedKits::DrawUnlit(Vector3 viewPosition) const
{
    const float pulse = 0.5f + 0.5f * std::sin(time * 3.0f);
    for (const Vector3& kit : kits)
    {
        // Алсаас харагдах улаан гэрлийн багана: хол байх тусам бүдүүн
        const float distance = HorizontalDistance(viewPosition, kit);
        const float width = std::clamp(distance * 0.012f, 0.3f, 4.0f);
        DrawCubeV({ kit.x, kit.y + 45.0f, kit.z }, { width, 90.0f, width }, Fade(kCross, 0.25f + 0.15f * pulse));
    }
}
