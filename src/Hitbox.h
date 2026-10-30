#pragma once

#include "raylib.h"

// Хүний онох хүрээ (тулаан: тоглогч, багийн дайчин): дүрсний байрлалтай (зогсох, суух, хэвтэх, унасан, унаанд сууж
// яваа) тааруулсан толгой, цээж, гэдэс, гар, хөлийн капсулууд. Хохирол хаана оносноос хамаарна (Counter-Strike шиг).
enum class HitZone : unsigned char
{
    None,
    Head,
    Chest,
    Stomach,
    Arm,
    Leg
};

enum class BodyPosture : unsigned char
{
    Stand,
    Crouch,
    Prone,  // урагшаа хэвтсэн (DrawHumanoid: хөл нь зогсож байсан цэгээс хойш, толгой урагш)
    Downed, // нуруугаараа унасан (босгохыг хүлээж буй)
    Seated  // унаанд сууж яваа: feet = суудлаас 0.9 м доор
};

// feet, facing: DrawHumanoid-д өгдөгтэй ижил. Туяа minDistance-аас хойш, distance-аас ойр биед тусвал true:
// distance-д туссан зай, zone-д биеийн хэсэг.
bool RaycastBody(Ray ray, Vector3 feet, float facing, BodyPosture posture, float minDistance, float& distance, HitZone& zone);

// Хохирлын үржүүлэгч: толгой 4, гэдэс 1.25, цээж ба гар 1, хөл 0.75
float HitZoneMultiplier(HitZone zone);

// Туршилт: онох хүрээг утсан хүрээгээр зурна
void DrawBodyHitbox(Vector3 feet, float facing, BodyPosture posture);

// AI-д: биеийн хэсгийн төв (ертөнцийн байрлал). part 0 толгой, 1 цээж, 2 хөл.
Vector3 BodyPartCenter(Vector3 feet, float facing, BodyPosture posture, int part);
