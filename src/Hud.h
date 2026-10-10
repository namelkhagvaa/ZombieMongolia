#pragma once

#include "Config.h"
#include "Player.h"

// Даалгаврын үеийн дэлгэцэд харуулах мэдээлэл.
struct HudInfo
{
    const char* provinceName = "";
    const char* objective = nullptr;       // одоо хийх даалгавар: "2 / 4  Генераторыг асаа" (UTF-8)
    const char* objectiveDetail = nullptr; // явц ба зай: "1 / 3    120 м"
    const char* prompt = nullptr;          // харилцах заавар: "E — Генератор асаах"
    const char* notice = nullptr;          // богино мэдэгдэл: "ДААЛГАВАР БИЕЛЛЭЭ: ..."
    int coins = 0;
    int missionKills = 0;
    int ammo = 0;
    int magazine = 0;
    bool reloading = false;
    int weaponSlot = 2;        // 0..4: хутга, гар буу, автомат, үрлэн буу, гранат
    bool usesAmmo = true;      // хутганд сум харуулахгүй
    bool showMagazine = true;  // гранатад зөвхөн үлдсэн тоо
    int squadAlive = 0;
    int squadTotal = 0;
    const char* banner = nullptr;    // дэлгэцийн төвд том бичвэр
    const char* subBanner = nullptr; // доор нь жижиг бичвэр
    const char* rideText = nullptr;  // унааны хурд, гадаргуу эсвэл "E: ride ..." санамж
    bool showHelp = true;
};

// 2D дэлгэцийн мэдээлэл: FPS, амь, сум, давалгаа, удирдлага.
// raylib-ийн үндсэн фонт кирилл дэмждэггүй тул бичвэр нь англиар.
void DrawHud(const GameConfig& config, const Player& player, const HudInfo& info, bool showDebug);
