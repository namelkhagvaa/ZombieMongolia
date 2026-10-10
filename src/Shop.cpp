#include "Shop.h"

namespace
{
    int* LevelRef(Progress& progress, ShopItem item)
    {
        switch (item)
        {
        case ShopItem::Damage: return &progress.weapon.damage;
        case ShopItem::FireRate: return &progress.weapon.fireRate;
        case ShopItem::Magazine: return &progress.weapon.magazine;
        case ShopItem::Health: return &progress.healthLevel;
        case ShopItem::Soldier: return &progress.soldiers;
        default: return nullptr;
        }
    }

    int BaseCost(ShopItem item)
    {
        switch (item)
        {
        case ShopItem::Damage: return 60;
        case ShopItem::FireRate: return 60;
        case ShopItem::Magazine: return 40;
        case ShopItem::Health: return 50;
        case ShopItem::Soldier: return 120;
        default: return 0;
        }
    }
}

const char* ShopItemName(ShopItem item)
{
    switch (item)
    {
    case ShopItem::Damage: return "Weapon damage";
    case ShopItem::FireRate: return "Fire rate";
    case ShopItem::Magazine: return "Magazine size";
    case ShopItem::Health: return "Max health";
    case ShopItem::Soldier: return "Recruit soldier";
    default: return "";
    }
}

int ShopLevel(const Progress& progress, ShopItem item)
{
    const int* level = LevelRef(const_cast<Progress&>(progress), item);
    return level ? *level : 0;
}

int ShopMaxLevel(ShopItem item)
{
    switch (item)
    {
    case ShopItem::Health: return kMaxHealthLevel;
    case ShopItem::Soldier: return kMaxSoldiers;
    default: return kMaxWeaponLevel;
    }
}

int ShopCost(const Progress& progress, ShopItem item)
{
    return BaseCost(item) * (ShopLevel(progress, item) + 1);
}

bool ShopBuy(Progress& progress, ShopItem item)
{
    int* level = LevelRef(progress, item);
    if (!level || *level >= ShopMaxLevel(item))
    {
        return false;
    }

    const int cost = ShopCost(progress, item);
    if (progress.coins < cost)
    {
        return false;
    }

    progress.coins -= cost;
    ++(*level);
    return true;
}
