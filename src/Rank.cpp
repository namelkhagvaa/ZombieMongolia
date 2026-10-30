#include "Rank.h"

#include <algorithm>
#include <cmath>

int RankXpToNext(int level)
{
    return (int)std::lround((150.0 + 25.0 * (double)(std::max(1, level) - 1)) / 3.0);
}

int RankLevel(int xp)
{
    int level = 1;
    while (xp >= RankXpToNext(level))
    {
        xp -= RankXpToNext(level);
        ++level;
    }
    return level;
}

int RankXp(int level)
{
    int total = 0;
    for (int k = 1; k < level; ++k) total += RankXpToNext(k);
    return total;
}

const char* RankTitle(int level, int& stars)
{
    stars = 0;
    if (level < 5) return "Байлдагч";
    if (level < 10) return "Ахлах байлдагч";
    if (level < 20) return "Түрүүч";
    if (level < 35) return "Ахлагч";
    if (level < 50) return "Дэслэгч";
    if (level < 75) return "Ахмад";
    if (level < 100) return "Хошууч";
    if (level < 150) return "Хурандаа";
    stars = 1 + (level - 150) / 50;
    return "Генерал";
}

float RankBonus(int level)
{
    return 1.0f + 0.25f * std::log(1.0f + (float)(std::max(1, level) - 1) / 10.0f);
}

float ZombieRankScale(int level)
{
    return 1.0f + 0.3f * std::log(1.0f + (float)(std::max(1, level) - 1) / 10.0f);
}
