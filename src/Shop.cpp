#include "Shop.h"

#include "Account.h"

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
    case ShopItem::Damage: return "Зэвсгийн хүч";
    case ShopItem::FireRate: return "Буудах хурд";
    case ShopItem::Magazine: return "Сумны багтаамж";
    case ShopItem::Health: return "Амийн дээд хэмжээ";
    case ShopItem::Soldier: return "Цэрэг элсүүлэх";
    default: return "";
    }
}

int ShopLevel(const Progress& progress, ShopItem item)
{
    if (Account::ServerShop()) return Account::UpgradeLevel((int)item);
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
    if (Account::ServerShop()) return Account::UpgradePrice((int)item);
    return BaseCost(item) * (ShopLevel(progress, item) + 1);
}

int ShopWallet(const Progress& progress)
{
    return Account::ServerShop() ? Account::Gold() : progress.coins;
}

bool ShopSync(Progress& progress)
{
    if (!Account::ServerShop()) return false;
    bool changed = false;
    for (int i = 0; i < (int)ShopItem::Count; ++i)
    {
        int* level = LevelRef(progress, (ShopItem)i);
        const int server = Account::UpgradeLevel(i);
        if (level && *level != server)
        {
            *level = server;
            changed = true;
        }
    }
    return changed;
}

bool ShopBuy(Progress& progress, ShopItem item)
{
    if (Account::ServerShop())
    {
        if (ShopLevel(progress, item) < ShopMaxLevel(item) && Account::Gold() >= ShopCost(progress, item))
        {
            Account::BuyUpgrade((int)item);
        }
        return false;
    }
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
