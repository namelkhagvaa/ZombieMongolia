#pragma once

#include "Progress.h"

// Газрын зургийн дэлгэц дээр зоосоор авах сайжруулалтууд.
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
constexpr int kMaxSoldiers = 4;
constexpr float kHealthPerLevel = 20.0f;

const char* ShopItemName(ShopItem item);
int ShopLevel(const Progress& progress, ShopItem item);
int ShopMaxLevel(ShopItem item);
int ShopCost(const Progress& progress, ShopItem item); // дараагийн түвшний үнэ

// Зоос хүрэлцэж, дээд түвшинд хүрээгүй бол худалдаж аваад true буцаана.
bool ShopBuy(Progress& progress, ShopItem item);
