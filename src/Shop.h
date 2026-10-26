#pragma once

#include "Progress.h"

// Газрын зургийн дэлгүүр дээр авах байнгын сайжруулалтууд. Нэвтэрсэн үед Gold-оор серверээр (Account.h:
// түвшин, үнэ, Gold серверээс ирнэ); туршилтын горимд (данс идэвхгүй) төхөөрөмж дээрх зоосоор.
enum class ShopItem
{
    Damage,
    FireRate,
    Magazine,
    Health,
    Soldier,
    Count
};

constexpr int kMaxHealthLevel = 5;
constexpr int kMaxSoldiers = 10; // Gold-оор авах цэргийн дээд тоо (тоглолтод үхсэн нь хасагдана)
constexpr float kHealthPerLevel = 20.0f;

const char* ShopItemName(ShopItem item);
int ShopLevel(const Progress& progress, ShopItem item);
int ShopMaxLevel(ShopItem item);
int ShopCost(const Progress& progress, ShopItem item); // дараагийн түвшний үнэ
int ShopWallet(const Progress& progress);              // зарцуулж болох мөнгө: Gold (сервер) эсвэл зоос

// Худалдаж авна. Серверээр бол хүсэлт илгээгээд false (хариу ирэхэд ShopSync түвшинг шинэчилнэ);
// туршилтын горимд зоос хүрэлцэж, дээд түвшинд хүрээгүй бол шууд аваад true.
bool ShopBuy(Progress& progress, ShopItem item);

// Серверийн түвшнүүдийг progress-д хуулна (даалгаврын эхэнд, газрын зураг дээр frame бүрд). Өөрчлөгдсөн бол true.
bool ShopSync(Progress& progress);
