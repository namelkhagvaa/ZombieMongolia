#pragma once

#include "Config.h"
#include "Player.h"

// Даалгаврын үеийн дэлгэцэд харуулах мэдээлэл.
struct HudInfo
{
    const char* provinceName = "";
    const char* objective = nullptr;       // одоо хийх даалгавар: "2 / 4  Генераторыг асаа" (UTF-8)
    const char* objectiveDetail = nullptr; // явц ба зай: "1 / 3    120 м"
    int hordeSmall = -1;                   // үлдсэн жирийн зомби (-1 = харуулахгүй)
    int hordeBig = 0;                      // үлдсэн том зомби
    float zombieSpeed = 1.0f;              // зомбины хурдны үржүүлэгч (> 1 бол чип харуулна)
    const char* prompt = nullptr;          // харилцах заавар: "E — Генератор асаах"
    const char* notice = nullptr;          // богино мэдэгдэл: "ДААЛГАВАР БИЕЛЛЭЭ: ..."
    int coins = 0;
    // Өдөр шөнө (Dota маягийн): одоо шөнө эсэх, дараагийн солигдох хүртэлх секунд, тухайн үеийн явц 0..1
    bool night = false;
    float phaseLeft = -1.0f; // < 0: харуулахгүй
    float phaseProgress = 0.0f;
    int missionKills = 0;
    int ammo = 0;
    int magazine = 0;
    bool reloading = false;
    int weaponSlot = 2;        // сонгогдсон зэвсгийн нүд (0-ээс)
    const char* const* slotNames = nullptr; // зэвсгийн нүднүүдийн бичвэр (гишүүний бүрдлээс хамаарна)
    int slotWeapons[8] = {};   // нүд бүрийн зэвсгийн төрөл (WeaponType), дүрс зурахад
    int slotCount = 0;
    const char* stance = nullptr; // "CROUCH" / "PRONE"; босоо үед nullptr
    bool scoped = false;       // мэргэн буудагчийн дуран хараа дэлгэцэд гарна
    struct TeamEntry
    {
        const char* name = "";
        int health = 0;
        bool alive = true;
        bool active = false; // тоглогчийн удирдаж буй гишүүн
        const char* tag = nullptr; // картын зүүн талд: "БАЙРАНДАА" (энд бай) г.м.
    };
    TeamEntry team[3]; // F1, F2, F3
    int teamCount = 3;
    const char* partnerName = nullptr; // хост: хамт тоглож буй хүн (чип)
    int partnerHealth = -1;            // 0 = унасан  // хамтарсан тоглолтын зочин: өөрөө ба хост (2)
    const char* teamKeys[3] = { "F1", "F2", "F3" };
    bool usesAmmo = true;      // хутганд сум харуулахгүй
    bool showMagazine = true;  // гранатад зөвхөн үлдсэн тоо
    int squadAlive = 0;
    int squadTotal = 0;
    const char* banner = nullptr;    // дэлгэцийн төвд том бичвэр
    const char* subBanner = nullptr; // доор нь жижиг бичвэр
    const char* rideText = nullptr;  // унааны хурд, гадаргуу эсвэл "E: ride ..." санамж
    bool showHelp = true;
    bool touch = false; // мэдрэгчтэй дэлгэц: зэвсэг, баг, газрын зургийг товчлуурын давхарга зурна
};

// 2D дэлгэцийн мэдээлэл: FPS, амь, сум, давалгаа, удирдлага.
// raylib-ийн үндсэн фонт кирилл дэмждэггүй тул бичвэр нь англиар.
void DrawHud(const GameConfig& config, const Player& player, const HudInfo& info, bool showDebug);
