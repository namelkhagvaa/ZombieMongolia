#include "Team.h"

#include "CharacterModel.h"

const char* TeamMemberName(int member)
{
    switch (member)
    {
    case 0: return "Дайчин 1";
    case 1: return "Дайчин 2";
    default: return "Мэргэн буудагч";
    }
}

const char* LeaderSkinName(int skin)
{
    switch (skin)
    {
    case 1: return "MODEL-1";
    case 2: return "MODEL-3";
    case 3: return "DEMON";
    case 4: return "IRON";
    default: return "ЦЭРЭГ";
    }
}

HumanoidLook TeamMemberLook(int member, int leaderSkin)
{
    HumanoidLook look;
    look.kind = HumanoidKind::Player;
    look.pants = { 58, 54, 48, 255 };
    look.boots = { 52, 36, 26, 255 };   // Монгол гутал
    look.vest = { 122, 104, 70, 255 };  // савхин сумны хантааз
    look.skin = { 214, 168, 128, 255 };
    look.held = HeldItem::Rifle;

    switch (member)
    {
    case 0: // хөх дээл, улаан оройтой малгай (assets\fighter1 загвар байвал түүгээр зурагдана)
        look.model = leaderSkin == 1 ? kCharacterModel1 : leaderSkin == 2 ? kCharacterModel3 : leaderSkin == 3 ? kCharacterDemon
                   : leaderSkin == 4 ? kCharacterIron : kCharacterFighter1;
        look.body = { 40, 70, 150, 255 };
        look.trim = { 230, 150, 30, 255 };
        look.hat = { 160, 34, 34, 255 };
        break;
    case 1: // хүрэн дээл, хөх малгай (assets\fighter2 загвар байвал түүгээр зурагдана)
        look.model = kCharacterFighter2;
        look.body = { 122, 52, 44, 255 };
        look.trim = { 226, 196, 96, 255 };
        look.hat = { 40, 64, 120, 255 };
        look.skin = { 204, 156, 118, 255 };
        break;
    default: // мэргэн буудагч: 1-р дайчинтай ижил загвар (fighter1), бараан өнгөөр ялгасан
        look.model = kCharacterFighter1;
        look.modelTint = { 178, 196, 168, 255 };
        look.body = { 62, 88, 58, 255 };
        look.trim = { 96, 84, 52, 255 };
        look.hat = { 58, 62, 44, 255 };
        look.vest = { 70, 76, 54, 255 };
        look.held = HeldItem::Sniper;
        break;
    }
    look.belt = look.trim;
    return look;
}
