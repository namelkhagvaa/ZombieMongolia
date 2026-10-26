#pragma once

#include "Humanoid.h"

// Даалгаврыг гурван гишүүнтэй баг биелүүлнэ: F1, F2 = дайчин, F3 = мэргэн буудагч.
// Тоглогч нэг гишүүнийг удирдаж, үлдсэн хоёр нь түүнийг дагаж, хамгаална (SoldierSquad).
constexpr int kTeamSize = 3;
constexpr int kSniperMember = 2;
// Багийн гишүүд хохирлыг ийм дахин бага авна: амь 100% хэвээр ч 5 дахин удаан үхнэ.
constexpr float kTeamToughness = 5.0f;
// Тулаан (тоглогч хооронд, нээлттэй өрөө): хүн хүнд учруулах хохирлыг энэ дахин өсгөнө, тэгэхээр багийн бат бөхөөр
// хуваахад үр дүндээ 1.5-д хуваагдана (5-д биш): автомат ~7-8 сумаар унагаана. Зомбины хохирол өөрчлөгдөхгүй.
constexpr float kPvpDamageScale = kTeamToughness / 1.5f;
constexpr float kPvpDownTime = 8.0f;   // тулаанд унасан хүн босгохыг хүлээх хугацаа (энгийн үед Player::kDownTime)
constexpr float kPvpReviveTime = 5.0f; // тулаанд AI нөхөр босгох хугацаа (энгийн үед 3 с); босгогч буудуулбал тасарна

inline bool IsSniperMember(int member) { return member == kSniperMember; }

// Дэлгэцэд харуулах нэр (UTF-8).
const char* TeamMemberName(int member);

// Гишүүн бүр өөр өнгийн дээл, малгайтай: хэнийг удирдаж байгаа нь ялгагдана.
// leaderSkin: 1-р дайчны (member 0) сонгосон дүр (0 = цэрэг, 1 = Model-1, 2 = Model-3, 3 = Demon, 4 = Iron;
// 3, 4 нь онцгой: Account::SkinPrice Gold-оор); бусад гишүүдэд нөлөөгүй.
HumanoidLook TeamMemberLook(int member, int leaderSkin = 0);

// 1-р дайчны сонгож болох дүрүүд (тоглоом эхлэхэд дүр сонгох цонх)
constexpr int kLeaderSkinCount = 5;
const char* LeaderSkinName(int skin);
