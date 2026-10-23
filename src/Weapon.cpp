#include "Weapon.h"

#include <algorithm>
#include <cmath>

const WeaponSpec& SpecFor(WeaponType type)
{
    //                                 name       dmg    intvl  mag +lvl reload auto   pel  spread  range
    static const WeaponSpec specs[] = {
        /* Knife   */ { "KNIFE",   0.0f,  0.45f, 0,  0, 0.0f, true,  1, 0.0f,   kKnifeReach },
        /* Pistol  */ { "PISTOL",  16.0f, 0.16f, 100, 3, 1.1f, false, 1, 0.006f, 90.0f },
        /* Rifle   */ { "RIFLE",   20.0f, 0.20f, 100, 6, 1.5f, true,  1, 0.004f, 150.0f },
        /* Shotgun */ { "SHOTGUN", 12.0f, 0.85f, 100, 1, 2.0f, false, 8, 0.06f,  40.0f },
        /* Grenade */ { "GRENADE", 0.0f,  1.0f,  kGrenadesPerMission, 0, 0.0f, false, 1, 0.0f, 0.0f },
        /* Sniper  */ { "SNIPER",  170.0f, 1.25f, 100, 1, 2.6f, false, 1, 0.0004f, 600.0f },
        /* Launcher*/ { "RPG",     0.0f,  0.6f,  1,   0, 2.4f, false, 1, 0.0f,    300.0f },
    };
    return specs[(int)type];
}

int Arsenal::MagazineSizeOf(WeaponType type) const
{
    const WeaponSpec& spec = SpecFor(type);
    return spec.magazine + spec.magazinePerLevel * levels.magazine;
}

namespace
{
    const WeaponType kFighterSlots[] = { WeaponType::Knife, WeaponType::Pistol, WeaponType::Rifle, WeaponType::Shotgun,
                                         WeaponType::Grenade };
    const WeaponType kSniperSlots[] = { WeaponType::Knife, WeaponType::Pistol, WeaponType::Sniper };
    const WeaponType kHeavySlots[] = { WeaponType::Knife, WeaponType::Pistol, WeaponType::Rifle, WeaponType::Launcher,
                                       WeaponType::Grenade };
}

int Arsenal::SlotCount() const
{
    return loadout == Loadout::Sniper ? 3 : 5;
}

WeaponType Arsenal::SlotWeapon(int slot) const
{
    if (slot < 0 || slot >= SlotCount())
    {
        return WeaponType::Count;
    }
    return loadout == Loadout::Sniper ? kSniperSlots[slot] : loadout == Loadout::Heavy ? kHeavySlots[slot] : kFighterSlots[slot];
}

int Arsenal::CurrentSlot() const
{
    for (int slot = 0; slot < SlotCount(); ++slot)
    {
        if (SlotWeapon(slot) == current) return slot;
    }
    return -1;
}

bool Arsenal::Has(WeaponType type) const
{
    for (int slot = 0; slot < SlotCount(); ++slot)
    {
        if (SlotWeapon(slot) == type) return true;
    }
    return false;
}

void Arsenal::Configure(const WeaponLevels& newLevels, Loadout newLoadout)
{
    levels = newLevels;
    loadout = newLoadout;
    for (int i = 0; i < (int)WeaponType::Count; ++i)
    {
        ammo[i] = MagazineSizeOf((WeaponType)i);
    }
    current = loadout == Loadout::Sniper ? WeaponType::Sniper : WeaponType::Rifle;
    cooldown = 0.0f;
    reloadTimer = 0.0f;
}

void Arsenal::Select(WeaponType type)
{
    if (type == current || !Has(type))
    {
        return; // энэ гишүүнд байхгүй зэвсэг
    }
    current = type;
    reloadTimer = 0.0f;                 // зэвсэг солиход цэнэглэлт тасална
    cooldown = std::max(cooldown, 0.2f); // гаргаж ирэх богино хугацаа
}

float Arsenal::Damage() const
{
    return Spec().damage * (1.0f + 0.4f * (float)levels.damage);
}

float Arsenal::Interval() const
{
    // Галын хурдны сайжруулалт зөвхөн галт зэвсэгт
    const bool firearm = current == WeaponType::Pistol || current == WeaponType::Rifle || current == WeaponType::Shotgun ||
                         current == WeaponType::Sniper;
    return Spec().interval * (firearm ? std::pow(0.88f, (float)levels.fireRate) : 1.0f);
}

int Arsenal::Ammo() const
{
    return ammo[(int)current];
}

int Arsenal::MagazineSize() const
{
    return MagazineSizeOf(current);
}

void Arsenal::Update(float dt)
{
    if (cooldown > 0.0f)
    {
        cooldown -= dt;
    }

    if (reloadTimer > 0.0f)
    {
        reloadTimer -= dt;
        if (reloadTimer <= 0.0f)
        {
            // Нөөцөөс дутуу сумыг нөхнө (нөөц дууссан бол байгаагаараа)
            int& loaded = ammo[(int)current];
            const int need = std::max(0, MagazineSize() - loaded);
            const int take = reserve ? std::min(need, *reserve) : need;
            loaded += take;
            if (reserve) *reserve -= take;
        }
    }
}

bool Arsenal::TryUse()
{
    if (IsReloading() || cooldown > 0.0f)
    {
        return false;
    }

    // Гранат хязгааргүй: тоо нь хасагдахгүй.
    if (UsesAmmo() && current != WeaponType::Grenade)
    {
        if (Ammo() <= 0)
        {
            Reload();
            return false;
        }
        --ammo[(int)current];
    }

    cooldown = Interval();
    return true;
}

void Arsenal::Reload()
{
    // Гранат цэнэглэгдэхгүй: давалгаа бүрийн дараа нэмэгдэнэ.
    if (current == WeaponType::Grenade || !UsesAmmo())
    {
        return;
    }
    if (!IsReloading() && Ammo() < MagazineSize() && (!reserve || *reserve > 0))
    {
        reloadTimer = Spec().reloadTime;
    }
}

void Arsenal::AddGrenades(int count)
{
    int& grenades = ammo[(int)WeaponType::Grenade];
    grenades = std::min(grenades + count, kGrenadesPerMission + 2);
}
