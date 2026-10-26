#pragma once

#include "World.h"
#include "raylib.h"

#include <vector>

class Player;
class SoldierSquad;

// Эмнэлгийн хайрцаг: 100 зомби устгах тутам сүүлчийн устгасан зомбины байрнаас унана.
// Багийн 3 гишүүний аль нэг нь очиж хүрвэл тэр гишүүний амь буцаад 100% болно.
// Газрын зураг дээр улаан загалмайтай цагаан дөрвөлжингөөр тод харагдана.
class MedKits
{
public:
    static constexpr int kKillsPerKit = 100;

    void Clear() { kits.clear(); }
    void Drop(const World& world, Vector3 position);

    // Хүрсэн гишүүнийг эмчилж хайрцгийг арилгана. Авсан бол true (pickedAt-д тоглогчийн авсан хайрцгийн байрлал).
    bool Update(float dt, Player& player, SoldierSquad& squad, Vector3* pickedAt = nullptr);

    // Хамтарсан тоглолт: зочин хостын жагсаалтыг авна; хост зочны авсан хайрцгийг арилгана.
    void Replace(const std::vector<Vector3>& positions) { kits = positions; }
    bool RemoveNear(Vector3 position, float radius); // олдсон (өөр хүн түрүүлж аваагүй) бол true

    const std::vector<Vector3>& Positions() const { return kits; }

    void Draw(Vector3 viewPosition) const;      // хайрцаг
    void DrawUnlit(Vector3 viewPosition) const; // алсаас харагдах гэрлийн багана

private:
    std::vector<Vector3> kits;
    float time = 0.0f;
};
