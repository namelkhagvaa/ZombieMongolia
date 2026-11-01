#include "Humanoid.h"

#include "CharacterModel.h"
#include "MeshBuilder.h"
#include "Profile.h"
#include "VehicleModel.h"
#include "raymath.h"

#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// Буу баруун талд (+Z рүү харахад баруун тал нь -X), цээжний өндөрт.
const Vector3 kHumanoidMuzzle = { -0.24f, 1.2f, 1.04f };

namespace
{
    const Color kGun = { 34, 34, 38, 255 };
    const Color kGunMetal = { 70, 72, 78, 255 };
    const Color kGunWood = { 136, 84, 44, 255 };
    const Color kBlood = { 96, 18, 16, 255 };
    const Color kBone = { 214, 204, 176, 255 };
    const Color kEyeWhite = { 232, 228, 218, 255 };
    const Color kZombieEye = { 240, 228, 140, 255 };
    const Color kSocket = { 30, 24, 24, 255 };

    // Биеийн хэмжээсүүд (метр, scale = 1)
    const float kHipHeight = 0.9f;
    const float kThigh = 0.45f;
    const float kShin = 0.45f;
    const float kShoulderHeight = 0.6f; // ташаанаас дээш
    const float kShoulderX = 0.34f;
    const float kUpperArm = 0.3f;
    const float kForearm = 0.28f;
    const float kNeckTop = 0.75f;       // ташаанаас дээш, толгойн эргэх цэг

    Color Shade(Color color, float factor)
    {
        return { (unsigned char)std::min(255.0f, (float)color.r * factor),
                 (unsigned char)std::min(255.0f, (float)color.g * factor),
                 (unsigned char)std::min(255.0f, (float)color.b * factor), color.a };
    }

    void Cube(float x, float y, float z, float sx, float sy, float sz, Color color)
    {
        DrawCubeV({ x, y, z }, { sx, sy, sz }, color);
    }

    void Quad(Vector3 a, Vector3 b, Vector3 c, Vector3 d)
    {
        rlVertex3f(a.x, a.y, a.z);
        rlVertex3f(b.x, b.y, b.z);
        rlVertex3f(c.x, c.y, c.z);
        rlVertex3f(a.x, a.y, a.z);
        rlVertex3f(c.x, c.y, c.z);
        rlVertex3f(d.x, d.y, d.z);
    }

    // Нарийссан хайрцаг: доод тал (bw x bd) y0 өндөрт, дээд тал (tw x td) y0 + h өндөрт.
    // shiftZ нь дээд талыг урагш/хойш шилжүүлнэ. Мөч, их бие, толгойг үүгээр хэлбэржүүлнэ.
    void Tapered(float cx, float y0, float cz, float bw, float bd, float tw, float td, float h, Color color,
                 float shiftZ = 0.0f)
    {
        const float bx = bw * 0.5f, bz = bd * 0.5f, tx = tw * 0.5f, tz = td * 0.5f;
        const float y1 = y0 + h;
        const float tcz = cz + shiftZ;

        const Vector3 b00 = { cx - bx, y0, cz - bz }, b10 = { cx + bx, y0, cz - bz };
        const Vector3 b11 = { cx + bx, y0, cz + bz }, b01 = { cx - bx, y0, cz + bz };
        const Vector3 t00 = { cx - tx, y1, tcz - tz }, t10 = { cx + tx, y1, tcz - tz };
        const Vector3 t11 = { cx + tx, y1, tcz + tz }, t01 = { cx - tx, y1, tcz + tz };

        rlBegin(RL_TRIANGLES);
        rlColor4ub(color.r, color.g, color.b, color.a);
        Quad(b01, b11, t11, t01); // +Z
        Quad(b10, b00, t00, t10); // -Z
        Quad(b11, b10, t10, t11); // +X
        Quad(b00, b01, t01, t00); // -X
        Quad(t01, t11, t10, t00); // дээд
        Quad(b00, b10, b11, b01); // доод
        rlEnd();
    }

    // Эргэх цэгээс доош унжсан мөчний хэсэг (дээрээ бүдүүн, доороо нарийн).
    void Limb(float length, float topW, float topD, float bottomW, float bottomD, Color color)
    {
        Tapered(0.0f, -length, 0.0f, bottomW, bottomD, topW, topD, length, color);
    }

    // ------------------------------------------------------------------ Хөл

    // side: 0 = баруун (-X), 1 = зүүн (+X)
    void DrawLeg(int side, const HumanoidPose& pose, const HumanoidLook& look, bool detailed)
    {
        const float x = side == 0 ? -0.13f : 0.13f;
        const bool bare = look.tornLeg && side == 0; // зомбины урагдсан өмд, нүцгэн хөл
        const bool tallBoots = look.kind == HumanoidKind::Player;

        rlPushMatrix();
        rlTranslatef(x, kHipHeight, 0.0f);
        rlRotatef(pose.legSpread * (side == 0 ? -1.0f : 1.0f), 0.0f, 0.0f, 1.0f);
        rlRotatef(-pose.hip[side], 1.0f, 0.0f, 0.0f);
        Limb(kThigh, 0.21f, 0.24f, 0.17f, 0.19f, look.pants);

        rlTranslatef(0.0f, -kThigh, 0.0f);
        rlRotatef(pose.knee[side], 1.0f, 0.0f, 0.0f);

        const Color shin = bare ? look.skin : look.pants;
        Limb(0.37f, 0.165f, 0.185f, 0.12f, 0.14f, shin);
        if (bare && detailed)
        {
            Cube(0.0f, -0.02f, 0.0f, 0.19f, 0.03f, 0.21f, Shade(look.pants, 0.7f)); // урагдсан ирмэг
            Cube(0.03f, -0.2f, 0.07f, 0.05f, 0.1f, 0.012f, kBlood);
        }

        if (bare)
        {
            // Нүцгэн хөл
            Tapered(0.0f, -kShin, 0.05f, 0.11f, 0.26f, 0.1f, 0.15f, 0.08f, look.skin, -0.04f);
        }
        else
        {
            if (tallBoots)
            {
                // Монгол гутал: өвдөг хүртэлх түрий, ээтэн хоншоор
                Tapered(0.0f, -0.38f, 0.0f, 0.15f, 0.17f, 0.18f, 0.2f, 0.26f, look.boots);
                Tapered(0.0f, -kShin, 0.05f, 0.15f, 0.3f, 0.14f, 0.2f, 0.09f, look.boots, -0.03f);
                Tapered(0.0f, -kShin + 0.03f, 0.2f, 0.1f, 0.06f, 0.06f, 0.04f, 0.08f, look.boots, 0.02f); // ээтэн
            }
            else
            {
                Tapered(0.0f, -0.4f, 0.0f, 0.15f, 0.17f, 0.15f, 0.17f, 0.12f, look.boots); // түрий
                Tapered(0.0f, -kShin + 0.02f, 0.05f, 0.15f, 0.3f, 0.14f, 0.2f, 0.08f, look.boots, -0.03f);
            }
            Cube(0.0f, -kShin + 0.01f, 0.05f, 0.16f, 0.02f, 0.31f, Shade(look.boots, 0.5f)); // ул
        }

        if (look.kind == HumanoidKind::Soldier && detailed)
        {
            Cube(0.0f, -0.03f, 0.09f, 0.15f, 0.13f, 0.05f, Shade(look.vest, 0.8f)); // өвдөгний хамгаалалт
        }
        rlPopMatrix();
    }

    // Z тэнхлэгийн дагуух цилиндр (гол төмөр, хоолой, дуран). r1 = 0 бол конус.
    void BarrelZ(float x, float y, float z0, float z1, float r0, float r1, Color color, int sides = 10)
    {
        DrawCylinderEx({ x, y, z0 }, { x, y, z1 }, r0, r1, sides, color);
    }

    // ------------------------------------------------------------------ Гар

    // Баруун гарт барьсан жижиг зэвсэг. Шууны орон зайд зурна: эх нь тохой, шууны тэнхлэг (-Y) нь
    // зэвсгийн чиглэл, гар урагш сунгасан үед +Z нь дээш харна.
    void DrawHandItem(const HumanoidLook& look, bool detailed = true)
    {
        const float grip = -kForearm - 0.05f;
        if (look.held == HeldItem::Knife && detailed && HasPropModel(PropModelId::Knife))
        {
            // assets/buu/knife: толгой нь grip + 0.063 (бариулын төв grip дээр), үзүүр нь шууны -Y дагуу,
            // ирний нуруу +Z тал руу (доорх кодын хэлбэртэй ижил чиглэл, урт 0.44 м)
            rlPushMatrix();
            rlTranslatef(0.0f, grip + 0.063f, 0.01f);
            rlRotatef(90.0f, 1.0f, 0.0f, 0.0f); // загварын +Z (үзүүр) -> -Y, +Y (нуруу) -> +Z
            DrawPropModel(PropModelId::Knife);
            rlPopMatrix();
        }
        else if (look.held == HeldItem::Knife)
        {
            // Цэргийн хутга: ороосон иш, хөндлөн хамгаалалт, нуруутай ир, хурц үзүүр
            Cube(0.0f, grip, 0.01f, 0.036f, 0.13f, 0.042f, { 44, 40, 34, 255 });             // иш
            for (int i = 0; i < 4; ++i)
            {
                Cube(0.0f, grip + 0.05f - 0.03f * (float)i, 0.01f, 0.04f, 0.008f, 0.046f, { 28, 26, 22, 255 }); // ороолт
            }
            Cube(0.0f, grip + 0.07f, 0.01f, 0.04f, 0.018f, 0.046f, kGunMetal);               // толгой
            Cube(0.0f, grip - 0.07f, 0.01f, 0.03f, 0.02f, 0.1f, kGunMetal);                  // хамгаалалт
            Tapered(0.0f, grip - 0.27f, 0.01f, 0.006f, 0.014f, 0.006f, 0.046f, 0.2f, { 200, 204, 210, 255 }); // ир
            Tapered(0.0f, grip - 0.33f, 0.0f, 0.004f, 0.004f, 0.006f, 0.03f, 0.06f, { 220, 224, 230, 255 }); // үзүүр
            Cube(0.0f, grip - 0.17f, 0.033f, 0.008f, 0.2f, 0.004f, kGunMetal);               // нуруу
        }
        else if (look.held == HeldItem::Pistol)
        {
            Cube(0.0f, grip, 0.0f, 0.04f, 0.12f, 0.06f, { 70, 46, 34, 255 });                // бариулын хавтан
            Cube(0.0f, grip - 0.1f, 0.055f, 0.045f, 0.24f, 0.06f, kGunMetal);                // их бие, замаг
            Cube(0.0f, grip - 0.07f, 0.09f, 0.03f, 0.2f, 0.012f, kGun);
            Cube(0.0f, grip - 0.06f, 0.0f, 0.02f, 0.06f, 0.02f, kGun);                       // гох хамгаалалт
        }
        else if (look.held == HeldItem::Grenade)
        {
            // Ф-1 "лимонка": хэрчлээстэй өндгөн их бие, гал хамгаалагч хөшүүрэг, цагираг
            DrawSphereEx({ 0.0f, grip - 0.03f, 0.03f }, 0.058f, 6, 8, { 62, 78, 48, 255 });
            for (int i = 0; i < 3; ++i)
            {
                DrawCylinderEx({ 0.0f, grip - 0.07f + 0.04f * (float)i, 0.03f }, { 0.0f, grip - 0.065f + 0.04f * (float)i, 0.03f },
                               0.061f, 0.061f, 8, { 44, 56, 34, 255 });
            }
            Cube(0.0f, grip - 0.03f, 0.1f, 0.03f, 0.03f, 0.04f, kGunMetal);                  // гал хамгаалагч
            Cube(0.0f, grip - 0.01f, 0.095f, 0.02f, 0.1f, 0.012f, kGunMetal);                // хөшүүрэг
            DrawCylinderEx({ 0.025f, grip + 0.02f, 0.12f }, { 0.03f, grip + 0.02f, 0.12f }, 0.02f, 0.02f, 8, { 170, 170, 160, 255 }); // цагираг
        }
    }

    // Босоо бариултай гар буу (оруулсан загварын гарт): эх нь бариулын төв, +Z нь ам, +Y нь дээш.
    // Гар бууны орон зай: эх нь бариулын төв, +Z нь ам, +Y нь дээш.
    void DrawPistolUpright(bool detailed = true)
    {
        // assets/buu/pistol: ар тал z = -0.063, ам z = 0.167, гол төмөр y = 0.075 (доорх кодын хэлбэртэй ижил)
        if (detailed && HasPropModel(PropModelId::Pistol))
        {
            rlPushMatrix();
            rlTranslatef(0.0f, 0.075f, -0.063f);
            DrawPropModel(PropModelId::Pistol);
            rlPopMatrix();
            return;
        }
        Cube(0.0f, 0.085f, 0.055f, 0.034f, 0.05f, 0.21f, kGunMetal);          // замаг
        for (int i = 0; i < 5; ++i)
        {
            Cube(0.0f, 0.087f, -0.035f + 0.012f * (float)i, 0.036f, 0.04f, 0.004f, kGun); // замагны ховил
        }
        BarrelZ(0.0f, 0.075f, 0.16f, 0.167f, 0.009f, 0.009f, { 20, 20, 22, 255 }, 8); // амсар
        Cube(0.0f, 0.058f, 0.06f, 0.03f, 0.022f, 0.17f, kGun);                // их бие
        Tapered(0.0f, -0.07f, -0.02f, 0.03f, 0.05f, 0.032f, 0.055f, 0.13f, kGun, 0.025f); // бариул (хойш налуу)
        Tapered(0.0f, -0.06f, -0.02f, 0.034f, 0.04f, 0.036f, 0.045f, 0.1f, { 70, 46, 34, 255 }, 0.025f); // модон хавтан
        Cube(0.0f, 0.03f, 0.055f, 0.012f, 0.012f, 0.07f, kGun);               // гох хамгаалалт
        Cube(0.0f, 0.115f, 0.15f, 0.008f, 0.012f, 0.012f, kGun);              // урд хараа
        Cube(0.0f, 0.112f, -0.035f, 0.02f, 0.01f, 0.012f, kGun);              // хойд хараа
        Cube(0.0f, 0.1f, -0.055f, 0.012f, 0.02f, 0.012f, kGunMetal);          // алх
    }

    void DrawArm(int side, const HumanoidPose& pose, const HumanoidLook& look, bool detailed)
    {
        const float x = side == 0 ? -kShoulderX : kShoulderX;
        const float inward = side == 0 ? 1.0f : -1.0f;
        const float outward = side == 0 ? -1.0f : 1.0f;
        const bool zombie = look.kind == HumanoidKind::Zombie;
        const Color sleeve = look.shirtless ? look.skin : look.body;
        const Color forearm = zombie ? look.skin : look.body;
        const Color hand = look.kind == HumanoidKind::Soldier ? Color{ 40, 40, 38, 255 } : look.skin; // бээлий

        rlPushMatrix();
        rlTranslatef(x, kShoulderHeight, 0.0f);
        rlRotatef(pose.shoulderYaw[side] * inward, 0.0f, 1.0f, 0.0f);
        rlRotatef(-pose.shoulder[side], 1.0f, 0.0f, 0.0f);

        Tapered(0.0f, -0.07f, 0.0f, 0.17f, 0.19f, 0.13f, 0.15f, 0.13f, sleeve); // мөрний булчин
        Limb(kUpperArm, 0.145f, 0.165f, 0.12f, 0.135f, sleeve);

        if (look.kind == HumanoidKind::Soldier && side == 1 && detailed)
        {
            // Ханцуйн дээрх төрийн далбааны тэмдэг
            Cube(outward * 0.076f, -0.12f, -0.035f, 0.008f, 0.06f, 0.03f, { 190, 44, 48, 255 });
            Cube(outward * 0.076f, -0.12f, 0.0f, 0.008f, 0.06f, 0.03f, { 20, 96, 166, 255 });
            Cube(outward * 0.076f, -0.12f, 0.035f, 0.008f, 0.06f, 0.03f, { 190, 44, 48, 255 });
        }

        if (side == 1 && look.missingLeftArm)
        {
            // Тасарсан гар: цус, цухуйсан яс
            Cube(0.0f, -kUpperArm - 0.01f, 0.0f, 0.12f, 0.04f, 0.13f, kBlood);
            Cube(0.0f, -kUpperArm - 0.08f, 0.0f, 0.035f, 0.12f, 0.035f, kBone);
            rlPopMatrix();
            return;
        }

        rlTranslatef(0.0f, -kUpperArm, 0.0f);
        rlRotatef(-pose.elbow[side], 1.0f, 0.0f, 0.0f);
        Limb(kForearm, 0.115f, 0.125f, 0.085f, 0.095f, forearm);

        if (look.kind == HumanoidKind::Player)
        {
            Cube(0.0f, -kForearm + 0.03f, 0.0f, 0.105f, 0.06f, 0.115f, look.trim); // ханцуйн эмжээр
        }
        else if (zombie && !look.shirtless && detailed)
        {
            Cube(0.0f, -0.02f, 0.0f, 0.13f, 0.03f, 0.14f, Shade(look.body, 0.7f)); // урагдсан ханцуй
        }

        // Гарын сарвуу
        Cube(0.0f, -kForearm - 0.05f, 0.0f, 0.08f, 0.1f, 0.095f, hand);
        if (detailed)
        {
            Cube(inward * 0.05f, -kForearm - 0.03f, 0.02f, 0.03f, 0.05f, 0.035f, hand); // эрхий
            if (zombie)
            {
                // Сунасан хуруунууд
                for (float fx : { -0.026f, 0.0f, 0.026f })
                {
                    Cube(fx, -kForearm - 0.13f, 0.01f, 0.02f, 0.08f, 0.022f, Shade(look.skin, 0.85f));
                }
            }
        }

        if (side == 0 && !pose.hideItem)
        {
            DrawHandItem(look, detailed);
        }
        rlPopMatrix();
    }

    // Үрлэн буу: модон бөгс, их бие, хоёр давхар хоолой (гол төмөр, сумны хоолой), хавирган гүйдэг гар.
    void DrawShotgun(float kick, bool detailed)
    {
        const float x = kHumanoidMuzzle.x;
        const float y = kHumanoidMuzzle.y - kHipHeight;
        const float z = -kick;

        // assets/buu/shotgun: бөгс z - 0.055, ам z + 1.2; гол төмөр (y + 0.03) ба сумны хоолойн (y - 0.03) дунд
        if (detailed && HasPropModel(PropModelId::Shotgun))
        {
            rlPushMatrix();
            rlTranslatef(x, y, z - 0.055f);
            DrawPropModel(PropModelId::Shotgun);
            rlPopMatrix();
            return;
        }

        Tapered(x, y - 0.12f, z + 0.08f, 0.06f, 0.24f, 0.06f, 0.32f, 0.15f, kGunWood, 0.03f); // бөгс
        Cube(x, y - 0.06f, z - 0.04f, 0.065f, 0.2f, 0.03f, kGun);                              // бөгсний резин
        Cube(x, y, z + 0.42f, 0.08f, 0.12f, 0.34f, kGunMetal);                                 // их бие
        Cube(x, y + 0.062f, z + 0.42f, 0.06f, 0.012f, 0.3f, kGun);                             // дээд хавтан
        Cube(x + 0.041f, y + 0.01f, z + 0.44f, 0.004f, 0.05f, 0.14f, kGun);                    // сум гаргах нүх
        BarrelZ(x, y + 0.03f, z + 0.58f, z + 1.2f, 0.022f, 0.022f, kGun);                      // гол төмөр
        BarrelZ(x, y - 0.03f, z + 0.58f, z + 1.06f, 0.019f, 0.019f, kGunMetal);                // сумны хоолой
        // Гүйдэг гар: хавирга бүхий модон гар
        BarrelZ(x, y - 0.03f, z + 0.66f, z + 0.86f, 0.04f, 0.04f, kGunWood, 12);
        for (int i = 0; i < 5; ++i)
        {
            BarrelZ(x, y - 0.03f, z + 0.68f + 0.04f * (float)i, z + 0.69f + 0.04f * (float)i, 0.043f, 0.043f,
                    Shade(kGunWood, 0.7f), 12);
        }
        if (detailed)
        {
            Cube(x, y - 0.1f, z + 0.36f, 0.02f, 0.07f, 0.1f, kGun);          // гох хамгаалалт
            DrawSphereEx({ x, y + 0.058f, z + 1.18f }, 0.008f, 3, 4, { 220, 200, 120, 255 }); // хараа
            Cube(x, y + 0.0f, z + 1.06f, 0.035f, 0.08f, 0.02f, kGunMetal);   // хоолой холбогч
        }
    }

    // ------------------------------------------------------------------ Буу

    void DrawGun(float kick, bool detailed)
    {
        // Автомат буу (АК маягийн). Их биеийн орон зайд (ташаанаас дээш хэмжсэн өндөр), урагшаа +Z.
        const float x = kHumanoidMuzzle.x;
        const float y = kHumanoidMuzzle.y - kHipHeight;
        const float z = -kick;

        // assets/buu/ak47: ойрын дүр ба 1-р этгээдийн буу. Бөгс z - 0.05, ам z + 1.1, гол төмөр y + 0.015 дээр
        // (доорх кодоор угсарсан хэлбэртэй ижил хэмжээ). Холын дүрд хөнгөн, кодын хэлбэр.
        if (detailed && HasPropModel(PropModelId::Ak47))
        {
            rlPushMatrix();
            rlTranslatef(x, y + 0.015f, z - 0.05f);
            DrawPropModel(PropModelId::Ak47);
            rlPopMatrix();
            return;
        }

        const Color wood = kGunWood;
        const Color woodDark = Shade(kGunWood, 0.7f);

        Tapered(x, y - 0.1f, z + 0.1f, 0.06f, 0.26f, 0.06f, 0.3f, 0.13f, wood, 0.02f);        // бөгс
        Cube(x, y - 0.045f, z - 0.045f, 0.064f, 0.18f, 0.025f, kGun);                         // бөгсний ган хавтан
        Cube(x, y, z + 0.46f, 0.085f, 0.13f, 0.42f, kGun);                                    // их бие
        // Тоосны таг: дугуйвтар оройтой, хойд үзүүрт хавирга
        BarrelZ(x, y + 0.062f, z + 0.27f, z + 0.65f, 0.036f, 0.036f, kGunMetal, 8);
        for (int i = 0; i < 3; ++i)
        {
            Cube(x, y + 0.098f, z + 0.3f + 0.025f * (float)i, 0.05f, 0.006f, 0.008f, kGun);
        }
        Cube(x - 0.045f, y + 0.02f, z + 0.48f, 0.006f, 0.03f, 0.16f, kGunMetal);             // галын горим сэлгэгч
        Tapered(x, y - 0.22f, z + 0.3f, 0.055f, 0.08f, 0.06f, 0.09f, 0.16f, woodDark, 0.04f); // бариул
        // Муруй сумны хайрцаг: гурван хэсэг урагшаа нумарсан, хажуудаа хавиргатай
        const Color mag = { 92, 56, 34, 255 }; // бакелит улбар хүрэн
        Tapered(x, y - 0.17f, z + 0.52f, 0.055f, 0.1f, 0.055f, 0.105f, 0.11f, mag, -0.02f);
        Tapered(x, y - 0.27f, z + 0.555f, 0.053f, 0.095f, 0.055f, 0.1f, 0.11f, mag, -0.035f);
        Tapered(x, y - 0.36f, z + 0.6f, 0.05f, 0.09f, 0.053f, 0.095f, 0.1f, mag, -0.045f);
        Cube(x, y - 0.37f, z + 0.6f, 0.056f, 0.02f, 0.1f, Shade(mag, 0.7f));
        // Доод гар хамгаалалт: модон, хуруу тавих хоёр хонхортой
        Cube(x, y - 0.012f, z + 0.76f, 0.09f, 0.085f, 0.22f, wood);
        Cube(x + 0.046f, y - 0.012f, z + 0.76f, 0.002f, 0.012f, 0.18f, woodDark);
        Cube(x - 0.046f, y - 0.012f, z + 0.76f, 0.002f, 0.012f, 0.18f, woodDark);
        // Хийн хоолой (дээд гар хамгаалалт) ба гол төмөр
        BarrelZ(x, y + 0.055f, z + 0.66f, z + 0.9f, 0.026f, 0.026f, wood, 8);
        BarrelZ(x, y + 0.055f, z + 0.9f, z + 0.98f, 0.016f, 0.016f, kGunMetal, 8);
        BarrelZ(x, y + 0.015f, z + 0.66f, z + 1.06f, 0.013f, 0.013f, kGun, 8);
        Cube(x, y + 0.035f, z + 0.98f, 0.04f, 0.05f, 0.04f, kGun);                            // хийн блок
        if (detailed)
        {
            BarrelZ(x, y + 0.015f, z + 1.04f, z + 1.1f, 0.02f, 0.02f, kGun, 8);              // амны тоормос
            Cube(x, y + 0.025f, z + 1.075f, 0.042f, 0.01f, 0.012f, kGunMetal);
            Cube(x, y + 0.06f, z + 1.02f, 0.012f, 0.06f, 0.012f, kGun);                      // урд хараа
            BarrelZ(x, y + 0.06f, z + 1.01f, z + 1.03f, 0.022f, 0.022f, kGun, 8);            // хараагийн цагираг
            Cube(x, y + 0.1f, z + 0.66f, 0.03f, 0.02f, 0.06f, kGun);                         // хойд хараа
            Cube(x - 0.05f, y - 0.04f, z + 0.9f, 0.012f, 0.012f, 0.03f, kGunMetal);          // оосрын гогцоо
            Cube(x, y - 0.085f, z + 0.38f, 0.016f, 0.05f, 0.08f, kGun);                      // гох хамгаалалт
        }
    }

    // Танк эсэргүүцэгч пуужин харвагч (РПГ маягийн): мөрөн дээр хэвтэх хоолой, модон дулаан хамгаалалт,
    // ард нь өргөссөн юүлүүр, урд нь конус хошуутай пуужин. Хоолойн тэнхлэг бууны шугамаас арай дээр.
    void DrawLauncher(float kick, bool detailed)
    {
        const float x = kHumanoidMuzzle.x;
        const float y = kHumanoidMuzzle.y - kHipHeight;
        const float z = -kick;
        const Color tube = { 70, 80, 56, 255 };
        const Color wood = { 132, 88, 50, 255 };
        const Color warhead = { 86, 98, 66, 255 };

        BarrelZ(x, y, z - 0.25f, z + 0.88f, 0.045f, 0.045f, tube, 12);                    // хоолой
        BarrelZ(x, y, z + 0.18f, z + 0.5f, 0.058f, 0.058f, wood, 12);                     // модон дулаан хамгаалалт
        BarrelZ(x, y, z + 0.18f, z + 0.19f, 0.062f, 0.062f, Shade(wood, 0.6f), 12);
        BarrelZ(x, y, z + 0.49f, z + 0.5f, 0.062f, 0.062f, Shade(wood, 0.6f), 12);
        BarrelZ(x, y, z - 0.25f, z - 0.45f, 0.045f, 0.085f, tube, 12);                    // арын юүлүүр
        Cube(x, y - 0.13f, z + 0.42f, 0.035f, 0.15f, 0.05f, kGun);                        // гохны бариул
        Cube(x, y - 0.13f, z + 0.66f, 0.035f, 0.15f, 0.05f, wood);                        // урд бариул
        // Пуужин: хүзүү, бүдүүн толгой, конус хошуу, шовх үзүүр
        BarrelZ(x, y, z + 0.88f, z + 0.98f, 0.03f, 0.045f, warhead, 10);
        BarrelZ(x, y, z + 0.98f, z + 1.12f, 0.068f, 0.068f, warhead, 12);
        BarrelZ(x, y, z + 1.12f, z + 1.26f, 0.068f, 0.02f, warhead, 12);
        BarrelZ(x, y, z + 1.26f, z + 1.3f, 0.012f, 0.004f, kGunMetal, 6);
        if (detailed)
        {
            Cube(x + 0.07f, y + 0.07f, z + 0.42f, 0.04f, 0.07f, 0.12f, kGun);             // оптик хараа (зүүн талд)
            BarrelZ(x + 0.07f, y + 0.08f, z + 0.36f, z + 0.34f, 0.02f, 0.025f, kGun, 8);
            Cube(x, y + 0.07f, z + 0.76f, 0.016f, 0.05f, 0.02f, kGun);                    // урд хараа
        }
    }

    // Мэргэн буудагчийн буу (СВД маягийн): цонхтой модон бөгс, урт гол төмөр, дуран хараа.
    void DrawSniper(float kick, bool detailed)
    {
        const float x = kHumanoidMuzzle.x;
        const float y = kHumanoidMuzzle.y - kHipHeight;
        const float z = -kick;

        // assets/buu/sniper: бөгс z - 0.05, ам z + 1.44, гол төмөр y + 0.02 дээр (доорх кодын хэлбэртэй ижил хэмжээ)
        if (detailed && HasPropModel(PropModelId::Sniper))
        {
            rlPushMatrix();
            rlTranslatef(x, y + 0.02f, z - 0.05f);
            DrawPropModel(PropModelId::Sniper);
            rlPopMatrix();
            return;
        }

        const Color woodDark = Shade(kGunWood, 0.75f);

        // Цонхтой бөгс: дээд, доод хоёр мод, хойд хавтан, урд холбоос
        Cube(x, y + 0.0f, z + 0.12f, 0.055f, 0.05f, 0.3f, kGunWood);
        Tapered(x, y - 0.17f, z + 0.12f, 0.055f, 0.3f, 0.055f, 0.2f, 0.06f, kGunWood);
        Cube(x, y - 0.075f, z - 0.03f, 0.06f, 0.2f, 0.035f, kGun);                            // бөгсний резин
        Cube(x, y + 0.04f, z + 0.07f, 0.05f, 0.04f, 0.12f, woodDark);                        // хацрын түшлэг
        Cube(x, y - 0.06f, z + 0.27f, 0.055f, 0.2f, 0.06f, kGunWood);
        Cube(x, y - 0.01f, z + 0.5f, 0.075f, 0.11f, 0.42f, kGun);                             // их бие
        Cube(x, y - 0.13f, z + 0.52f, 0.05f, 0.14f, 0.1f, kGunMetal);                         // сумны хайрцаг
        Cube(x, y - 0.005f, z + 0.82f, 0.085f, 0.09f, 0.26f, kGunWood);                       // гар хамгаалалт
        for (int i = 0; i < 4; ++i)
        {
            Cube(x - 0.043f, y - 0.005f, z + 0.72f + 0.06f * (float)i, 0.002f, 0.05f, 0.012f, woodDark); // агааржуулах ховил
        }
        BarrelZ(x, y + 0.02f, z + 0.7f, z + 1.4f, 0.014f, 0.012f, kGun, 8);                  // урт гол төмөр
        // Дуран хараа: суурь, хоолой, өргөссөн хоёр үзүүр, хоёр бариул
        Cube(x, y + 0.075f, z + 0.5f, 0.03f, 0.04f, 0.18f, kGunMetal);
        BarrelZ(x, y + 0.13f, z + 0.34f, z + 0.66f, 0.024f, 0.024f, kGun, 12);
        BarrelZ(x, y + 0.13f, z + 0.66f, z + 0.72f, 0.024f, 0.036f, kGun, 12);
        BarrelZ(x, y + 0.13f, z + 0.72f, z + 0.78f, 0.036f, 0.036f, kGun, 12);
        BarrelZ(x, y + 0.13f, z + 0.28f, z + 0.34f, 0.032f, 0.024f, kGun, 12);
        BarrelZ(x, y + 0.13f, z + 0.22f, z + 0.28f, 0.032f, 0.032f, { 50, 46, 40, 255 }, 12);  // нүдний резин
        if (detailed)
        {
            DrawCylinderEx({ x, y + 0.13f, z + 0.5f }, { x, y + 0.17f, z + 0.5f }, 0.012f, 0.012f, 8, kGunMetal); // тохируулагч
            DrawCylinderEx({ x, y + 0.13f, z + 0.5f }, { x - 0.04f, y + 0.13f, z + 0.5f }, 0.012f, 0.012f, 8, kGunMetal);
            BarrelZ(x, y + 0.13f, z + 0.781f, z + 0.783f, 0.03f, 0.03f, { 70, 120, 170, 255 }, 12); // линз
            Cube(x, y - 0.1f, z + 0.36f, 0.02f, 0.07f, 0.1f, kGun);                          // гох хамгаалалт
            BarrelZ(x, y + 0.02f, z + 1.36f, z + 1.44f, 0.02f, 0.02f, kGun, 8);              // амны тоормос
            Cube(x, y + 0.02f, z + 1.4f, 0.044f, 0.006f, 0.04f, kGunMetal);
            Cube(x + 0.045f, y + 0.03f, z + 0.56f, 0.03f, 0.012f, 0.012f, kGunMetal);        // цоожны бариул
        }
    }

    // ------------------------------------------------------------------ Толгой

    void DrawHumanFace(const HumanoidLook& look)
    {
        for (float ex : { -0.06f, 0.06f })
        {
            Cube(ex, 0.185f, 0.129f, 0.05f, 0.026f, 0.012f, kEyeWhite);
            Cube(ex, 0.185f, 0.134f, 0.022f, 0.024f, 0.008f, { 30, 22, 18, 255 }); // хүүхэн хараа
            Cube(ex, 0.213f, 0.131f, 0.066f, 0.014f, 0.014f, look.hair);           // хөмсөг
            Cube(ex * 2.15f, 0.165f, 0.0f, 0.02f, 0.07f, 0.05f, Shade(look.skin, 0.9f)); // чих
        }
        Tapered(0.0f, 0.105f, 0.14f, 0.05f, 0.035f, 0.028f, 0.02f, 0.075f, Shade(look.skin, 0.93f)); // хамар
        Cube(0.0f, 0.062f, 0.122f, 0.085f, 0.012f, 0.012f, { 126, 76, 66, 255 });                       // ам
    }

    void DrawZombieFace(const HumanoidPose& pose, const HumanoidLook& look)
    {
        // Хонхойсон нүдний ухархай, шаргал гэрэлтсэн нүд
        for (float ex : { -0.06f, 0.06f })
        {
            Cube(ex, 0.185f, 0.129f, 0.068f, 0.05f, 0.012f, kSocket);
            if (!(look.oneEye && ex > 0.0f))
            {
                Cube(ex, 0.185f, 0.135f, 0.026f, 0.026f, 0.008f, kZombieEye);
            }
            Cube(ex * 2.15f, 0.165f, 0.0f, 0.02f, 0.07f, 0.05f, Shade(look.skin, 0.8f));
        }
        Cube(0.0f, 0.125f, 0.133f, 0.03f, 0.04f, 0.008f, Shade(look.skin, 0.45f)); // унасан хамар

        // Дээд шүд, амны орчмын цус
        Cube(0.0f, 0.082f, 0.127f, 0.09f, 0.014f, 0.012f, kBone);
        Cube(0.03f, 0.1f, 0.13f, 0.05f, 0.03f, 0.006f, kBlood);

        // Доод эрүү: онгойж, унжина
        rlPushMatrix();
        rlTranslatef(0.0f, 0.075f, -0.02f);
        rlRotatef(pose.jaw, 1.0f, 0.0f, 0.0f);
        Tapered(0.0f, -0.075f, 0.03f, 0.16f, 0.17f, 0.2f, 0.21f, 0.06f, Shade(look.skin, 0.92f));
        Cube(0.0f, -0.012f, 0.13f, 0.08f, 0.012f, 0.012f, kBone);  // доод шүд
        Cube(0.0f, -0.055f, 0.118f, 0.1f, 0.04f, 0.012f, kBlood);  // эрүүн дээрх цус
        rlPopMatrix();
        Cube(0.0f, 0.05f, 0.09f, 0.11f, 0.05f, 0.03f, { 36, 10, 10, 255 }); // амны хөндий

        if (look.headWound)
        {
            Cube(0.105f, 0.235f, 0.04f, 0.045f, 0.08f, 0.09f, kBlood);
            Cube(0.115f, 0.24f, 0.04f, 0.03f, 0.03f, 0.04f, kBone);
        }
    }

    void DrawHair(const HumanoidLook& look, bool detailed)
    {
        if (look.kind != HumanoidKind::Zombie)
        {
            Cube(0.0f, 0.2f, -0.123f, 0.245f, 0.17f, 0.03f, look.hair); // дагз
            if (look.kind == HumanoidKind::Soldier && detailed)
            {
                Cube(-0.122f, 0.21f, -0.03f, 0.02f, 0.1f, 0.16f, look.hair);
                Cube(0.122f, 0.21f, -0.03f, 0.02f, 0.1f, 0.16f, look.hair);
            }
            return;
        }

        if (look.hairStyle == 1)
        {
            // Цоохор үлдсэн үс
            Cube(-0.05f, 0.3f, -0.04f, 0.13f, 0.03f, 0.16f, look.hair);
            Cube(0.07f, 0.22f, -0.125f, 0.1f, 0.12f, 0.02f, look.hair);
        }
        else if (look.hairStyle == 2)
        {
            // Сэгсгэр урт үс
            Cube(0.0f, 0.3f, -0.01f, 0.25f, 0.035f, 0.25f, look.hair);
            Cube(0.0f, 0.16f, -0.13f, 0.25f, 0.28f, 0.035f, look.hair);
            if (detailed)
            {
                Cube(-0.128f, 0.17f, -0.03f, 0.025f, 0.24f, 0.14f, look.hair);
                Cube(0.128f, 0.19f, -0.04f, 0.025f, 0.2f, 0.12f, look.hair);
                Cube(-0.07f, 0.26f, 0.125f, 0.06f, 0.08f, 0.015f, look.hair); // духан дээр унжсан
            }
        }
    }

    void DrawHeadgear(const HumanoidLook& look, bool detailed)
    {
        if (look.kind == HumanoidKind::Soldier)
        {
            // Дуулга: хүрээ, бөмбөгөр орой, оосор, нүдний шил
            Tapered(0.0f, 0.2f, 0.0f, 0.29f, 0.31f, 0.29f, 0.31f, 0.055f, Shade(look.hat, 0.85f));
            Tapered(0.0f, 0.255f, 0.0f, 0.29f, 0.31f, 0.19f, 0.21f, 0.1f, look.hat);
            if (detailed)
            {
                Cube(0.0f, 0.275f, 0.152f, 0.2f, 0.045f, 0.02f, { 30, 34, 38, 255 });
                Cube(-0.128f, 0.1f, 0.03f, 0.014f, 0.2f, 0.02f, Shade(look.hat, 0.6f));
                Cube(0.128f, 0.1f, 0.03f, 0.014f, 0.2f, 0.02f, Shade(look.hat, 0.6f));
                Cube(0.0f, 0.005f, 0.07f, 0.1f, 0.02f, 0.09f, Shade(look.hat, 0.6f)); // эрүүний оосор
            }
        }
        else if (look.kind == HumanoidKind::Player)
        {
            // Шовгор малгай: үслэг хүрээ, улаан оройвч, сампин
            Tapered(0.0f, 0.255f, 0.0f, 0.35f, 0.35f, 0.3f, 0.3f, 0.07f, { 54, 40, 30, 255 });
            DrawCylinder({ 0.0f, 0.32f, 0.0f }, 0.0f, 0.15f, 0.2f, 8, look.hat);
            Cube(0.0f, 0.53f, 0.0f, 0.045f, 0.045f, 0.045f, look.trim);
            if (detailed)
            {
                Cube(0.0f, 0.2f, -0.16f, 0.05f, 0.2f, 0.012f, look.hat); // ард унжсан тууз
            }
        }
    }

    void DrawHead(const HumanoidPose& pose, const HumanoidLook& look, bool detailed)
    {
        const bool zombie = look.kind == HumanoidKind::Zombie;
        Tapered(0.0f, 0.66f, 0.0f, 0.15f, 0.15f, 0.12f, 0.13f, 0.11f, Shade(look.skin, 0.92f)); // хүзүү

        rlPushMatrix();
        rlTranslatef(0.0f, kNeckTop, 0.0f);
        rlRotatef(pose.headPitch, 1.0f, 0.0f, 0.0f);
        rlRotatef(pose.headRoll, 0.0f, 0.0f, 1.0f);

        // Гавал: эрүү рүүгээ нарийссан хоёр хэсэг. Зомбины доод эрүү тусдаа хөдөлнө.
        if (zombie && detailed)
        {
            Tapered(0.0f, 0.06f, 0.005f, 0.22f, 0.23f, 0.25f, 0.26f, 0.06f, look.skin);
        }
        else
        {
            Tapered(0.0f, 0.0f, 0.01f, 0.19f, 0.2f, 0.25f, 0.26f, 0.12f, look.skin);
        }
        Tapered(0.0f, 0.12f, 0.0f, 0.25f, 0.26f, 0.23f, 0.24f, 0.18f, look.skin);

        if (detailed)
        {
            if (zombie) DrawZombieFace(pose, look);
            else DrawHumanFace(look);
        }
        DrawHair(look, detailed);
        DrawHeadgear(look, detailed);
        rlPopMatrix();
    }

    // ------------------------------------------------------------------ Их биеийн хувцас

    void DrawPlayerOutfit(const HumanoidLook& look, bool detailed)
    {
        // Дээлийн хормой: ташаанаас доош
        Tapered(0.0f, -0.3f, 0.0f, 0.52f, 0.33f, 0.46f, 0.29f, 0.46f, look.body);
        Cube(0.0f, -0.29f, 0.0f, 0.53f, 0.03f, 0.34f, look.trim); // хормойн эмжээр

        // Бүс ба унжсан үзүүр
        Cube(0.0f, 0.17f, 0.0f, 0.5f, 0.1f, 0.31f, look.trim);
        Cube(0.19f, 0.05f, 0.15f, 0.06f, 0.2f, 0.03f, Shade(look.trim, 0.85f));

        // Зах, энгэрийн ташуу эмжээр (баруун мөрнөөс зүүн суга руу)
        Cube(0.0f, 0.665f, 0.0f, 0.2f, 0.05f, 0.2f, look.trim);
        rlPushMatrix();
        rlTranslatef(-0.02f, 0.45f, 0.158f);
        rlRotatef(-32.0f, 0.0f, 0.0f, 1.0f);
        Cube(0.0f, 0.0f, 0.0f, 0.045f, 0.46f, 0.02f, look.trim);
        rlPopMatrix();

        if (!detailed) return;

        // Сумны хантааз: цээжний халааснууд, мөрөвч
        Cube(-0.15f, 0.36f, 0.165f, 0.11f, 0.15f, 0.05f, look.vest);
        Cube(0.0f, 0.34f, 0.165f, 0.11f, 0.15f, 0.05f, look.vest);
        Cube(0.15f, 0.36f, 0.165f, 0.11f, 0.15f, 0.05f, look.vest);
        Cube(-0.19f, 0.52f, 0.0f, 0.06f, 0.26f, 0.33f, Shade(look.vest, 0.85f));
        Cube(0.19f, 0.52f, 0.0f, 0.06f, 0.26f, 0.33f, Shade(look.vest, 0.85f));
        // Ар дахь хутга, хэт
        Cube(-0.2f, 0.12f, -0.15f, 0.04f, 0.22f, 0.03f, { 60, 44, 30, 255 });
    }

    void DrawSoldierOutfit(const HumanoidLook& look, bool detailed)
    {
        const Color pouch = Shade(look.vest, 0.8f);

        // Хуягт хантааз
        Tapered(0.0f, 0.24f, 0.0f, 0.48f, 0.34f, 0.56f, 0.37f, 0.36f, look.vest);
        // Үүргэвч ба дээрх хуйлсан дэвсгэр
        Tapered(0.0f, 0.22f, -0.27f, 0.36f, 0.18f, 0.4f, 0.2f, 0.4f, Shade(look.vest, 0.9f));
        Cube(0.0f, 0.67f, -0.26f, 0.44f, 0.11f, 0.13f, { 132, 118, 84, 255 });
        Cube(0.0f, 0.17f, 0.0f, 0.5f, 0.07f, 0.33f, look.belt);

        if (!detailed) return;

        // Сумны халааснууд
        for (float px : { -0.16f, 0.0f, 0.16f })
        {
            Cube(px, 0.33f, 0.2f, 0.11f, 0.16f, 0.06f, pouch);
            Cube(px, 0.4f, 0.21f, 0.115f, 0.03f, 0.065f, Shade(pouch, 0.8f)); // таг
        }
        // Мөрний оосор
        Cube(-0.17f, 0.64f, 0.0f, 0.07f, 0.04f, 0.36f, pouch);
        Cube(0.17f, 0.64f, 0.0f, 0.07f, 0.04f, 0.36f, pouch);
        // Бүсний хэрэгсэл: хажуугийн уут, ус сав
        Cube(0.26f, 0.1f, 0.02f, 0.06f, 0.16f, 0.14f, pouch);
        Cube(-0.24f, 0.08f, -0.12f, 0.1f, 0.17f, 0.1f, { 70, 80, 60, 255 });
        // Радио станцын антен
        Cube(0.15f, 0.82f, -0.3f, 0.012f, 0.42f, 0.012f, { 24, 24, 24, 255 });
        // Өнгөлөн далдлах толбо
        Cube(0.22f, 0.5f, 0.186f, 0.1f, 0.08f, 0.004f, Shade(look.body, 0.7f));
        Cube(-0.2f, 0.26f, -0.172f, 0.12f, 0.07f, 0.004f, Shade(look.body, 0.7f));
    }

    void DrawZombieOutfit(const HumanoidLook& look, bool detailed)
    {
        if (look.shirtless)
        {
            // Нүцгэн цээж: товойсон хавирга
            if (detailed)
            {
                for (int i = 0; i < 4; ++i)
                {
                    Cube(0.0f, 0.34f + (float)i * 0.06f, 0.15f + (float)i * 0.006f, 0.36f + (float)i * 0.03f, 0.014f,
                         0.012f, Shade(look.skin, 0.7f));
                }
                Cube(0.0f, 0.44f, 0.158f, 0.014f, 0.26f, 0.008f, Shade(look.skin, 0.7f)); // өвчүү
            }
        }
        else
        {
            // Урагдсан цамцны хормой
            Cube(-0.12f, 0.12f, 0.0f, 0.2f, 0.12f, 0.29f, look.body);
            Cube(0.16f, 0.15f, 0.0f, 0.14f, 0.07f, 0.29f, look.body);
        }

        if (look.bloody)
        {
            Cube(0.08f, 0.47f, 0.166f, 0.28f, 0.22f, 0.012f, kBlood);
            Cube(-0.12f, 0.3f, 0.155f, 0.14f, 0.12f, 0.012f, kBlood);
        }

        if (look.openRibs && detailed)
        {
            // Хэвлийн шарх: бараан нүх, цухуйсан хавирга
            Cube(0.11f, 0.33f, 0.158f, 0.2f, 0.17f, 0.014f, { 50, 12, 12, 255 });
            for (int i = 0; i < 3; ++i)
            {
                Cube(0.11f, 0.28f + (float)i * 0.05f, 0.166f, 0.17f, 0.016f, 0.012f, kBone);
            }
        }

        if (detailed)
        {
            // Арьсны үхжсэн толбо
            Cube(-0.04f, 0.7f, 0.07f, 0.06f, 0.05f, 0.012f, Shade(look.skin, 0.6f));
            Cube(0.0f, 0.5f, -0.165f, 0.22f, 0.14f, 0.01f, Shade(look.body, 0.65f)); // нурууны бохир
        }
    }
}

HumanoidGait HumanoidGaitFor(float speed)
{
    // Хүн ~2 м/с-ээс дээш хурдад алхаанаас гүйлт рүү шилждэг. Алхам давтамж (мөчлөг/с): алхахад 0.9 орчим,
    // гүйхэд 1.4-1.8; хөл газарт тулах хугацаа алхахад мөчлөгийн 60%, хурдан гүйхэд 20% хүртэл богиносно.
    HumanoidGait gait;
    const float v = std::max(0.0f, speed);
    const float t = std::clamp((v - 1.8f) / 1.4f, 0.0f, 1.0f);
    gait.run = t * t * (3.0f - 2.0f * t);
    gait.frequency = Lerp(0.55f + 0.25f * v, 1.2f + 0.06f * v, gait.run);
    gait.duty = Lerp(std::max(0.55f, 0.64f - 0.03f * v), std::clamp(0.4f - 0.022f * v, 0.19f, 0.4f), gait.run);
    gait.contact = v * gait.duty / gait.frequency;
    return gait;
}

float HumanoidGaitRate(float speed)
{
    return speed > 0.1f ? 2.0f * PI * HumanoidGaitFor(speed).frequency : 0.0f;
}

Vector3 HumanoidMuzzleFor(HeldItem held)
{
    switch (held)
    {
    case HeldItem::Shotgun: return { kHumanoidMuzzle.x, kHumanoidMuzzle.y + 0.03f, 1.2f };
    case HeldItem::Sniper: return { kHumanoidMuzzle.x, kHumanoidMuzzle.y + 0.03f, 1.4f };
    case HeldItem::Launcher: return { -0.2f, 1.6f, 1.25f }; // мөрөн дээрх хоолойн ам
    case HeldItem::Pistol: return { -0.26f, 1.44f, 0.82f };  // урагш сунгасан гарын үзүүр
    case HeldItem::Knife: return { -0.26f, 1.25f, 0.75f };
    case HeldItem::Grenade: return { -0.3f, 1.85f, 0.3f };   // толгой дээрээс шиднэ
    default: return kHumanoidMuzzle;
    }
}

void ApplyDownedPose(HumanoidPose& pose, float time)
{
    pose.walkPhase = 0.0f;
    pose.moveAmount = 0.0f;
    pose.speed = 0.0f;
    pose.recoil = 0.0f;
    pose.gunKick = 0.0f;
    pose.aim = 0.0f;
    pose.actionProgress = -1.0f;
    pose.reload = -1.0f;
    pose.crouch = 0.0f;
    pose.prone = 0.0f;
    pose.lean = 0.0f;
    pose.bob = 0.0f;
    pose.fall = 1.0f;
    // Баруун өвдөг өргөгдсөн, зүүн гар шарх руугаа (цээж) тулна; амьсгал, бага зэрэг хөдөлгөөн
    const float slow = std::sin(time * 1.4f), slower = std::sin(time * 0.9f + 1.3f);
    pose.hip[0] = 38.0f + 6.0f * slow;
    pose.knee[0] = 70.0f + 10.0f * slow;
    pose.hip[1] = 6.0f;
    pose.knee[1] = 10.0f;
    pose.shoulder[0] = 10.0f;
    pose.elbow[0] = 20.0f;
    pose.shoulder[1] = 45.0f + 10.0f * slower;
    pose.elbow[1] = 95.0f;
    pose.shoulderYaw[1] = 30.0f;
    pose.headRoll = 12.0f * slower;
    pose.breath = slow;
}

void ApplyDeathPose(HumanoidPose& pose, float seconds, int style)
{
    pose.walkPhase = 0.0f;
    pose.moveAmount = 0.0f;
    pose.speed = 0.0f;
    pose.recoil = 0.0f;
    pose.gunKick = 0.0f;
    pose.aim = 0.0f;
    pose.actionProgress = -1.0f;
    pose.reload = -1.0f;
    pose.bob = 0.0f;
    pose.breath = 0.0f;
    pose.dead = true;
    const float t = std::max(0.0f, seconds);
    auto clamp01 = [](float x) { return std::clamp(x, 0.0f, 1.0f); };
    auto smooth = [&](float x) { x = clamp01(x); return x * x * (3.0f - 2.0f * x); };
    // Унах явц: эхлээд аажим, дараа нь хурдасна (таталцал); газар мөргөөд жаахан ойж тогтоно
    auto topple = [](float x) {
        if (x <= 0.0f) return 0.0f;
        if (x < 1.0f) return x * x;
        const float a = x - 1.0f;
        return 1.0f + 0.07f * std::sin(a * 16.0f) * std::exp(-a * 8.0f);
    };
    const float jolt = t < 0.16f ? std::sin(t / 0.16f * PI) : 0.0f; // сум тусах агшны цочрол
    // Гар: эхний агшинд дээш шидэгдэж, дараа нь сул унжина
    const float limp = smooth((t - 0.1f) / 0.5f);
    pose.shoulder[0] = Lerp(50.0f + 40.0f * jolt, 12.0f, limp);
    pose.shoulder[1] = Lerp(35.0f + 50.0f * jolt, 28.0f, limp);
    pose.shoulderYaw[0] = Lerp(0.0f, -18.0f, limp);
    pose.shoulderYaw[1] = Lerp(0.0f, 22.0f, limp);
    pose.elbow[0] = Lerp(30.0f, 12.0f, limp);
    pose.elbow[1] = Lerp(30.0f, 48.0f, limp);
    pose.hideItem = false;
    switch (style)
    {
    case 1:
    {
        // Нүүрээрээ урагшаа: өвдөг нугарч бөхийн, дараа нь урагш хэвтэнэ
        const float buckle = smooth(t / 0.28f);
        pose.crouch = 0.55f * buckle * (1.0f - smooth((t - 0.45f) / 0.3f));
        pose.lean = 10.0f * jolt + 18.0f * buckle;
        pose.prone = topple((t - 0.22f) / 0.42f);
        pose.hip[0] = 8.0f * limp;
        pose.knee[0] = 20.0f * limp;
        pose.knee[1] = 6.0f * limp;
        pose.headRoll = 25.0f * limp;
        pose.fall = 0.0f;
        break;
    }
    case 2:
    {
        // Өвдөг сөхрөөд хажуу тийш хөрвөнө
        const float kneel = smooth(t / 0.4f);
        pose.crouch = kneel;
        pose.knee[0] = pose.knee[1] = 60.0f * kneel * (1.0f - limp * 0.6f);
        pose.lean = -8.0f * jolt + 12.0f * kneel;
        const float down = topple((t - 0.42f) / 0.48f);
        pose.fall = std::min(1.0f, down) * 0.95f + (down > 1.0f ? down - 1.0f : 0.0f);
        pose.roll = 40.0f * std::min(1.0f, down);
        pose.headRoll = -20.0f * limp;
        break;
    }
    case 3:
    {
        // Унасан байснаас: хэвтсэн хэвээр, сүүлчийн хөдөлгөөн
        pose.fall = 1.0f;
        pose.hip[0] = Lerp(38.0f, 10.0f, limp);
        pose.knee[0] = Lerp(70.0f, 25.0f, limp);
        pose.headRoll = 15.0f * limp;
        break;
    }
    default:
    {
        // Нуруугаараа хойшоо: цочоод өвдөг сулраад хойш унана
        const float buckle = smooth(t / 0.22f);
        pose.crouch = 0.3f * buckle * (1.0f - smooth((t - 0.4f) / 0.3f));
        pose.lean = -14.0f * jolt;
        pose.fall = topple((t - 0.1f) / 0.48f);
        pose.hip[0] = 18.0f * buckle * (1.0f - limp * 0.5f);
        pose.knee[0] = 35.0f * buckle * (1.0f - limp * 0.4f);
        pose.knee[1] = 12.0f * buckle;
        pose.headRoll = -18.0f * limp;
        break;
    }
    }
}

void ApplySwimPose(HumanoidPose& pose, float time, float swimSpeed)
{
    pose.walkPhase = 0.0f;
    pose.moveAmount = 0.0f;
    pose.speed = 0.0f;
    pose.recoil = 0.0f;
    pose.gunKick = 0.0f;
    pose.aim = 0.0f;
    pose.actionProgress = -1.0f;
    pose.reload = -1.0f;
    pose.crouch = 0.0f;
    pose.fall = 0.0f;
    pose.prone = 0.82f; // бараг хэвтээ
    pose.lean = 0.0f;
    pose.hideItem = true;
    pose.swimming = true;
    pose.seated = true; // (гаднаас оруулсан загвар: алхааны биш, өгөгдсөн гар хөлийн өнцөг)
    // Явж байвал хурдан сэлүүрдэнэ, зогсоод хөвж байвал аажим (гараа сэлгэж хөвөрнө)
    const float rate = 2.2f + std::min(1.0f, swimSpeed / 1.6f) * 2.8f;
    const float phase = time * rate;
    pose.shoulder[0] = 95.0f + 75.0f * std::sin(phase);
    pose.shoulder[1] = 95.0f + 75.0f * std::sin(phase + PI);
    pose.elbow[0] = 25.0f + 30.0f * std::max(0.0f, std::cos(phase));
    pose.elbow[1] = 25.0f + 30.0f * std::max(0.0f, std::cos(phase + PI));
    pose.shoulderYaw[0] = pose.shoulderYaw[1] = 10.0f;
    pose.hip[0] = 12.0f * std::sin(phase * 2.0f);
    pose.hip[1] = -12.0f * std::sin(phase * 2.0f);
    pose.knee[0] = 12.0f + 10.0f * std::max(0.0f, std::sin(phase * 2.0f));
    pose.knee[1] = 12.0f + 10.0f * std::max(0.0f, -std::sin(phase * 2.0f));
    pose.headPitch = -55.0f; // хүзүүгээ гудайлгаж урагшаа харна (нүүр усан дээр)
    pose.headRoll = 6.0f * std::sin(phase);
    pose.bob = 0.04f * std::sin(time * 2.1f);
}

HumanoidPose MakeSoldierPose(float walkCycle, float moveAmount, bool grounded, float time, float recoil,
                             HeldItem held, float action)
{
    HumanoidPose pose;
    pose.walkPhase = walkCycle;
    pose.moveAmount = moveAmount;
    pose.time = time;
    pose.recoil = recoil;
    pose.grounded = grounded;
    pose.actionProgress = action > 0.0f ? 1.0f - action : -1.0f;
    const float amount = std::min(moveAmount, 1.0f);
    const float sprint = std::clamp(moveAmount - 1.0f, 0.0f, 0.6f) / 0.6f; // 0 алхаа .. 1 гүйлт

    if (!grounded)
    {
        // Агаарт: нэг хөл урагш, хоёулаа нугалсан
        pose.hip[0] = 30.0f;
        pose.hip[1] = -12.0f;
        pose.knee[0] = 50.0f;
        pose.knee[1] = 30.0f;
        pose.lean = 6.0f;
    }
    else
    {
        const float swing = 28.0f + 16.0f * sprint;
        for (int side = 0; side < 2; ++side)
        {
            const float phase = walkCycle + (float)side * PI;
            pose.hip[side] = std::sin(phase) * swing * amount;
            // Хөл хойноос урагш гарах үедээ өвдгөөрөө нугарна.
            pose.knee[side] = amount * (8.0f + (46.0f + 24.0f * sprint) * std::max(0.0f, std::cos(phase)));
        }
        pose.bob = -amount * (0.035f + 0.025f * sprint) * std::fabs(std::sin(walkCycle));
        pose.lean = amount * (4.0f + 9.0f * sprint);
        pose.roll = std::sin(walkCycle) * 2.5f * amount;

        // Зогсож байхад амьсгал нь мэдэгдэнэ: цээж тэлж, мөр өргөгдөнө (хөдөлж эхлэхэд аажим арилна).
        // Амьсгал авах нь гаргахаас богино байхаар долгионыг бага зэрэг хазайлгана.
        const float phase = time * 2.0f;
        pose.breath = (1.0f - amount) * std::sin(phase + 0.35f * std::sin(phase));
    }

    const float freeSwing = -std::sin(walkCycle) * 22.0f * amount; // чөлөөтэй гарын савлалт
    const float progress = action > 0.0f ? 1.0f - action : 0.0f;   // хөдөлгөөний явц 0 -> 1

    switch (held)
    {
    case HeldItem::Rifle:
    case HeldItem::Shotgun:
    case HeldItem::Sniper:
    case HeldItem::Launcher:
    {
        // Бууг хоёр гараар барина: баруун гар бариулд, зүүн гар урд талын модонд.
        const float kick = held == HeldItem::Rifle ? 1.0f : 1.8f; // үрлэн буу хүчтэй ухарна
        pose.shoulder[0] = 20.0f - recoil * 6.0f * kick;
        pose.shoulderYaw[0] = 14.0f;
        pose.elbow[0] = 80.0f + recoil * 8.0f * kick;
        pose.shoulder[1] = 56.0f - recoil * 5.0f * kick;
        pose.shoulderYaw[1] = 48.0f;
        pose.elbow[1] = 28.0f + recoil * 8.0f * kick;
        pose.gunKick = recoil * 0.07f * kick;
        pose.lean -= recoil * 2.0f * kick;
        break;
    }

    case HeldItem::Pistol:
        // Гар бууг хоёр гараар урагш сунган барина; буудахад дээш өргөгдөнө.
        pose.shoulder[0] = 82.0f + recoil * 16.0f;
        pose.shoulderYaw[0] = 10.0f;
        pose.elbow[0] = 6.0f + recoil * 10.0f;
        pose.shoulder[1] = 74.0f + recoil * 12.0f;
        pose.shoulderYaw[1] = 40.0f;
        pose.elbow[1] = 22.0f;
        break;

    case HeldItem::Knife:
    {
        // Хутгыг урдаа барина; хатгахдаа гараа огцом сунгаж буцаана.
        const float thrust = action > 0.0f ? std::sin(progress * PI) : 0.0f;
        pose.shoulder[0] = 34.0f + 52.0f * thrust;
        pose.shoulderYaw[0] = 16.0f - 8.0f * thrust;
        pose.elbow[0] = 82.0f - 74.0f * thrust;
        pose.shoulder[1] = 30.0f + freeSwing * 0.4f;
        pose.shoulderYaw[1] = 20.0f;
        pose.elbow[1] = 70.0f;
        pose.lean += 12.0f * thrust;
        break;
    }

    case HeldItem::Grenade:
        if (action > 0.0f)
        {
            // Шидэлт: гар толгой дээгүүр хойноос урагш савна.
            pose.shoulder[0] = 190.0f - 130.0f * progress;
            pose.elbow[0] = 50.0f * (1.0f - progress);
            pose.lean += -8.0f + 20.0f * progress;
            pose.hideItem = progress > 0.5f; // гараас гарсан
        }
        else
        {
            pose.shoulder[0] = 24.0f;
            pose.elbow[0] = 78.0f;
            pose.shoulderYaw[0] = 10.0f;
        }
        pose.shoulder[1] = 20.0f + freeSwing * 0.5f;
        pose.elbow[1] = 30.0f;
        break;

    case HeldItem::None:
    default:
        pose.shoulder[0] = -freeSwing;
        pose.shoulder[1] = freeSwing;
        pose.elbow[0] = pose.elbow[1] = 14.0f;
        break;
    }
    return pose;
}

void DrawBlobShadow(Vector3 ground, float radius)
{
    // Хөлийн яг доорх зөөлөн бараан толбо (газар хүрсэн мэдрэмж): төв нь бараан, зах руу ул мөргүй алга болно.
    // (Нарны сүүдрийг shadow map гаргана; хуучин 8 өнцөгт хатуу толбо нарны тогтмол чиглэл рүү шилжсэн байв.)
    const int segments = 18;
    const float y = ground.y + 0.13f, r = radius * 1.15f;
    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < segments; ++i)
    {
        const float a0 = 2.0f * PI * (float)i / (float)segments, a1 = 2.0f * PI * (float)(i + 1) / (float)segments;
        for (int side = 0; side < 2; ++side) // (хоёр эргэлтээр: ар талыг тасдах тохиргооноос үл хамаарна)
        {
            const float first = side ? a0 : a1, second = side ? a1 : a0;
            rlColor4ub(10, 12, 20, 110);
            rlVertex3f(ground.x, y, ground.z);
            rlColor4ub(10, 12, 20, 0);
            rlVertex3f(ground.x + std::cos(first) * r, y, ground.z + std::sin(first) * r);
            rlVertex3f(ground.x + std::cos(second) * r, y, ground.z + std::sin(second) * r);
        }
    }
    rlEnd();
}

HumanoidPose MakeGunnerPose(Vector3 feet, float facing, const Vector3 grips[2], float time)
{
    HumanoidPose pose = MakeSoldierPose(0.0f, 0.0f, true, time, 0.0f, HeldItem::None);
    // seated (өгөгдсөн өнцөг) боловч хөл шулуун: зогсож буй хэлбэр, гар нь бариулын цэг рүү (grip)
    pose.seated = true;
    pose.hip[0] = pose.hip[1] = 4.0f;
    pose.knee[0] = pose.knee[1] = 8.0f;
    pose.legSpread = 10.0f;
    pose.lean = 8.0f;
    pose.bob = 0.0f;
    pose.grip = 1.0f;
    const float sn = std::sin(facing), cs = std::cos(facing);
    for (int side = 0; side < 2; ++side)
    {
        const Vector3 d = Vector3Subtract(grips[side], feet);
        pose.gripPoint[side] = { d.x * cs - d.z * sn, d.y, d.x * sn + d.z * cs };
    }
    return pose;
}

Vector3 HumanoidLocalToWorld(Vector3 feet, float facing, Vector3 local)
{
    const float s = std::sin(facing);
    const float c = std::cos(facing);
    return {
        feet.x + local.x * c + local.z * s,
        feet.y + local.y,
        feet.z - local.x * s + local.z * c,
    };
}

namespace
{
    void DrawBuiltInFigure(const HumanoidPose& pose, const HumanoidLook& look, bool detailed, Color torso);
}

void DrawHumanoid(Vector3 feet, float facing, float scale, const HumanoidPose& pose, const HumanoidLook& look,
                  bool detailed)
{
    const Color torso = look.shirtless ? look.skin : look.body;

    // Сэлж байна: буу харагдахгүй, гар хоосон (сэлүүрдэнэ)
    if (pose.swimming && look.held != HeldItem::None)
    {
        HumanoidLook bare = look;
        bare.held = HeldItem::None;
        DrawHumanoid(feet, facing, scale, pose, bare, detailed);
        return;
    }

    // Унах: урт бууг онилсон байрлалаар нь гартаа үлдээвэл нуруугаараа хэвтсэн биеэс тэнгэр рүү босоо гозойдог.
    // Унах явцын хагасаас бие сул гартай, буу нь хажууд нь газар хажуулдаа хэвтэнэ.
    const bool longGun = look.held == HeldItem::Rifle || look.held == HeldItem::Sniper || look.held == HeldItem::Launcher ||
                         look.held == HeldItem::Shotgun;
    const bool facedown = pose.dead && pose.prone > 0.55f;
    if (longGun && (pose.fall > 0.55f || facedown))
    {
        HumanoidLook unarmed = look;
        unarmed.held = HeldItem::None;
        DrawHumanoid(feet, facing, scale, pose, unarmed, detailed);
        rlPushMatrix();
        rlTranslatef(feet.x, feet.y, feet.z);
        rlRotatef(facing * RAD2DEG, 0.0f, 1.0f, 0.0f);
        rlScalef(scale, scale, scale);
        // биеийн баруун талд, их биеийн хажууд (хойшоо унавал бие -Z, урагшаа бол +Z тийш хэвтэнэ)
        rlTranslatef(facedown ? -0.6f : -0.55f, 0.05f, facedown ? 0.9f : -0.75f);
        rlRotatef(-35.0f, 0.0f, 1.0f, 0.0f);
        rlRotatef(90.0f, 0.0f, 0.0f, 1.0f);  // хажуулдаа
        rlTranslatef(-kHumanoidMuzzle.x, -(kHumanoidMuzzle.y - kHipHeight), -0.45f);
        if (look.held == HeldItem::Rifle) DrawGun(0.0f, detailed);
        else if (look.held == HeldItem::Sniper) DrawSniper(0.0f, detailed);
        else if (look.held == HeldItem::Launcher) DrawLauncher(0.0f, detailed);
        else DrawShotgun(0.0f, detailed);
        rlPopMatrix();
        return;
    }

    rlPushMatrix();
    rlTranslatef(feet.x, feet.y, feet.z);
    rlRotatef(facing * RAD2DEG, 0.0f, 1.0f, 0.0f);
    rlScalef(scale * look.bulk, scale, scale * look.bulk);

    // Хэвтэх: бүх бие урагшаа хэвтэнэ (ташаа нь зогсож байсан цэгийнхээ дээр үлдэнэ).
    if (pose.prone > 0.0f)
    {
        rlTranslatef(0.0f, 0.26f * pose.prone, -0.85f * pose.prone);
        rlRotatef(86.0f * pose.prone, 1.0f, 0.0f, 0.0f);
    }

    // Унах: хөлөө тойрон хойшоо эргэж, нуруугаараа газар хэвтэнэ.
    if (pose.fall > 0.0f)
    {
        rlTranslatef(0.0f, 0.16f * pose.fall, 0.0f);
        rlRotatef(-88.0f * pose.fall, 1.0f, 0.0f, 0.0f);
    }
    // Агаарт шидэгдсэн: биеийн төв (ташаа)-өө тойрон хойшоо эргэлдэнэ.
    if (pose.spin != 0.0f)
    {
        rlTranslatef(0.0f, kHipHeight, 0.0f);
        rlRotatef(-pose.spin, 1.0f, 0.0f, 0.0f);
        rlTranslatef(0.0f, -kHipHeight, 0.0f);
    }
    rlTranslatef(0.0f, pose.bob, 0.0f);

    if (HasCharacterModel(look.model))
    {
        // Гаднаас оруулсан ястай загвар: бие нь загвараас, зэвсэг нь урьдын адил кодоор.
        const bool twoHanded = look.held == HeldItem::Rifle || look.held == HeldItem::Sniper ||
                               look.held == HeldItem::Launcher ||
                               look.held == HeldItem::Shotgun;
        const CharacterFrames frames = DrawCharacterModel(look.model, pose, look.held,
                                                          look.kind == HumanoidKind::Zombie, look.modelTint, look.lowDetail);

        PROFILE_CHARACTER("char 6 gun");
        if (twoHanded)
        {
            rlPushMatrix();
            rlMultMatrixf(MatrixToFloat(frames.gun));
            if (look.held == HeldItem::Rifle) DrawGun(pose.gunKick, detailed);
            else if (look.held == HeldItem::Launcher) DrawLauncher(pose.gunKick, detailed);
            else if (look.held == HeldItem::Sniper) DrawSniper(pose.gunKick, detailed);
            else DrawShotgun(pose.gunKick, detailed);
            rlPopMatrix();
        }
        else if (!pose.hideItem)
        {
            rlPushMatrix();
            rlMultMatrixf(MatrixToFloat(frames.rightForearm));
            if (look.held == HeldItem::Pistol) DrawPistolUpright(detailed);
            else DrawHandItem(look, detailed);
            rlPopMatrix();
        }
        rlPopMatrix();
        return;
    }

    // Кодоор үүсгэсэн дүрс: суухад өвдөг нугарч бие доошилно; хэвтэхэд гар, буу толгой дээгүүр урагш сунана.
    HumanoidPose bent = pose;
    if (pose.crouch > 0.0f || pose.prone > 0.0f)
    {
        const float c = pose.crouch * (1.0f - pose.prone), p = pose.prone;
        for (int side = 0; side < 2; ++side)
        {
            bent.hip[side] = pose.hip[side] * (1.0f - 0.5f * c) * (1.0f - p) + 62.0f * c;
            bent.knee[side] = pose.knee[side] * (1.0f - p) + 108.0f * c + 8.0f * p;
            bent.shoulder[side] = pose.shoulder[side] * (1.0f - p) + 150.0f * p;
            bent.elbow[side] = pose.elbow[side] * (1.0f - p) + 30.0f * p;
        }
        bent.lean = pose.lean * (1.0f - p) + 14.0f * c - 14.0f * p;
        bent.headPitch = pose.headPitch - 40.0f * p;
        rlTranslatef(0.0f, -0.33f * c, 0.0f);
    }
    DrawBuiltInFigure(bent, look, detailed, torso);
}

namespace
{
// Кодоор үүсгэсэн (хайрцган) дүрсийг зурна. Үндсэн хувиргалт rlgl-д тавигдсан байна; төгсгөлд нь буцаана.
void DrawBuiltInFigure(const HumanoidPose& pose, const HumanoidLook& look, bool detailed, Color torso)
{
    DrawLeg(0, pose, look, detailed);
    DrawLeg(1, pose, look, detailed);

    // Их бие: ташааг тойрон бөхийж, найгана.
    rlPushMatrix();
    rlTranslatef(0.0f, kHipHeight, 0.0f);
    rlRotatef(pose.lean, 1.0f, 0.0f, 0.0f);
    rlRotatef(pose.roll, 0.0f, 0.0f, 1.0f);

    // Бэлхүүсээрээ нарийн, мөрөөрөө өргөн
    Tapered(0.0f, -0.02f, 0.0f, 0.4f, 0.25f, 0.44f, 0.27f, 0.2f, look.pants);  // ташаа
    Cube(0.0f, 0.17f, 0.0f, 0.46f, 0.06f, 0.29f, look.belt);

    // Амьсгал: цээж (хувцастайгаа) бэлхүүсээс дээш тэлж агшина, мөр толгой гар нь дагаж өргөгдөнө.
    const float breath = pose.breath;
    rlPushMatrix();
    rlTranslatef(0.0f, 0.2f, 0.0f);
    rlScalef(1.0f + 0.02f * breath, 1.0f + 0.045f * breath, 1.0f + 0.1f * breath);
    rlTranslatef(0.0f, -0.2f, 0.0f);

    Tapered(0.0f, 0.2f, 0.0f, 0.43f, 0.26f, 0.6f, 0.32f, 0.42f, torso);         // цээж
    Tapered(0.0f, 0.62f, 0.0f, 0.6f, 0.32f, 0.3f, 0.22f, 0.07f, torso);         // мөрний налуу

    switch (look.kind)
    {
    case HumanoidKind::Player: DrawPlayerOutfit(look, detailed); break;
    case HumanoidKind::Soldier: DrawSoldierOutfit(look, detailed); break;
    case HumanoidKind::Zombie: DrawZombieOutfit(look, detailed); break;
    }
    rlPopMatrix();

    rlTranslatef(0.0f, 0.022f * breath, 0.0f); // мөр, толгой, гар, буу

    DrawHead(pose, look, detailed);
    DrawArm(0, pose, look, detailed);
    DrawArm(1, pose, look, detailed);
    if (pose.prone > 0.0f)
    {
        // Хэвтэж байхад буу биеийн тэнхлэгийн дагуу (толгой дээгүүр) урагш чиглэнэ.
        rlTranslatef(0.0f, kShoulderHeight, 0.0f);
        rlRotatef(-84.0f * pose.prone, 1.0f, 0.0f, 0.0f);
        rlTranslatef(0.0f, -kShoulderHeight + 0.25f * pose.prone, -0.2f * pose.prone);
    }
    if (look.held == HeldItem::Rifle)
    {
        DrawGun(pose.gunKick, detailed);
    }
    else if (look.held == HeldItem::Sniper)
    {
        DrawSniper(pose.gunKick, detailed);
    }
    else if (look.held == HeldItem::Launcher)
    {
        // Кодоор үүсгэсэн дүрс: хоолойг мөрний түвшинд өргөнө.
        rlTranslatef(0.0f, 0.34f, -0.1f);
        DrawLauncher(pose.gunKick, detailed);
    }
    else if (look.held == HeldItem::Shotgun)
    {
        DrawShotgun(pose.gunKick, detailed);
    }

    rlPopMatrix();
    rlPopMatrix();
}
} // namespace

// ---------------------------------------------------------------- 1-р этгээдийн гар, буу

namespace
{
    // Камерын орон зай -> ертөнц: +Z урагш, +Y дээш, +X зүүн (дүрсний орон зайтай ижил: баруун тал нь -X).
    Matrix ViewBasis(const Camera3D& camera)
    {
        const Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
        Vector3 right = Vector3CrossProduct(forward, { 0.0f, 1.0f, 0.0f });
        right = Vector3Length(right) > 0.001f ? Vector3Normalize(right) : Vector3{ 1.0f, 0.0f, 0.0f };
        const Vector3 up = Vector3CrossProduct(right, forward);
        const Vector3 left = Vector3Negate(right);
        Matrix m = MatrixIdentity();
        m.m0 = left.x;    m.m1 = left.y;    m.m2 = left.z;
        m.m4 = up.x;      m.m5 = up.y;      m.m6 = up.z;
        m.m8 = forward.x; m.m9 = forward.y; m.m10 = forward.z;
        m.m12 = camera.position.x; m.m13 = camera.position.y; m.m14 = camera.position.z;
        return m;
    }

    // --- Урт бууны бэлэн (онилоогүй) байрлал: камерын орон зайд (+X зүүн, +Y дээш, +Z урагш) ---
    // Counter-Strike-ийн AWP зургаас хэмжсэн дэлгэцийн цэгүүд (нормчилсон: баруун +, дээш +): бууны их бие (гол
    // төмрийн ар хэсэг, бариулын дээр) дэлгэцийн баруун доор, ам нь хараанаас бага зэрэг баруун доор. Их биеийг
    // тэр цэгт тавьж, ам нь нөгөө цэгийн туяан дээр байхаар чиглүүлнэ (камерын баруун, дээш, урд векторуудаар):
    // буу баруун доороос төв рүү ташуу, дээд тал нь харагдана; бариул (баруун гар) баруун доод буланд.
    // Бууны загварын эх 24 см зүүн тийш зөрүүтэй тул эргэлтийг бариулыг тойруулна.
    const Vector3 kLongGripLocal = { -0.24f, 0.14f, 0.3f }; // гар бариул (DrawGun-ийн орон зай: x, y - 0.16, 0.3)
    const float kLongBore = 0.16f;                          // бариулаас гол төмөр хүртэл
    const float kLongScale = 1.0f;                          // бодит хэмжээ
    const Vector2 kLongBodyScreen = { 0.56f, -0.5f }, kLongMuzzleScreen = { 0.17f, -0.17f };
    const float kLongBodyDepth = 0.55f;                    // их бие камераас урд (м)
    const float kLongCant = -6.0f;                          // бууг бага зэрэг баруун тийш хазайлгана (градус)

    struct LongGunPose
    {
        Vector3 grip;    // бариулын байрлал (камерын орон зай)
        Matrix rotation; // бууны орон зай -> камерын орон зай (байрлалгүй)
        Vector3 muzzle;  // бууны ам (камерын орон зай)
    };

    LongGunPose LongGunHip(float fovy)
    {
        const float ty = std::tan(fovy * 0.5f * DEG2RAD);
        const float tx = ty * (float)rlGetFramebufferWidth() / (float)std::max(1, rlGetFramebufferHeight());
        auto ray = [&](Vector2 n, float depth) { return Vector3{ -n.x * tx * depth, n.y * ty * depth, depth }; };
        const Vector3 bore = ray(kLongBodyScreen, kLongBodyDepth);
        const Vector3 muzzleRay = Vector3Normalize(ray(kLongMuzzleScreen, 1.0f));
        const float length = (kHumanoidMuzzle.z - kLongGripLocal.z) * kLongScale;
        Vector3 up = { 0.0f, 1.0f, 0.0f }, forward = { 0.0f, 0.0f, 1.0f }, muzzle = {};
        for (int pass = 0; pass < 4; ++pass)
        {
            // Ам нь туяан дээр, их биеэс length зайд
            const float rb = Vector3DotProduct(muzzleRay, bore);
            const float t = rb + std::sqrt(std::max(0.0f, rb * rb - Vector3DotProduct(bore, bore) + length * length));
            muzzle = Vector3Scale(muzzleRay, t);
            forward = Vector3Normalize(Vector3Subtract(muzzle, bore));
            up = Vector3Normalize(Vector3Subtract({ 0.0f, 1.0f, 0.0f }, Vector3Scale(forward, forward.y)));
            const Vector3 side = Vector3CrossProduct(up, forward);
            const float cant = kLongCant * DEG2RAD;
            up = Vector3Add(Vector3Scale(up, std::cos(cant)), Vector3Scale(side, std::sin(cant)));
        }
        const Vector3 left = Vector3CrossProduct(up, forward);
        const Vector3 grip = Vector3Subtract(bore, Vector3Scale(up, kLongBore * kLongScale)); // бариул их биеийн доор
        Matrix r = MatrixIdentity();
        r.m0 = left.x;    r.m1 = left.y;    r.m2 = left.z;
        r.m4 = up.x;      r.m5 = up.y;      r.m6 = up.z;
        r.m8 = forward.x; r.m9 = forward.y; r.m10 = forward.z;
        return { grip, r, muzzle };
    }

    const Color kSleeve = { 82, 96, 62, 255 };   // дүрэмт хувцасны ханцуй
    const Color kGlove = { 38, 36, 32, 255 };    // бээлий

    // --- 1-р этгээдийн гар: тактикийн хуруугүй бээлий (хурууны үзүүр нүцгэн), бөөрөнхий үе мөч ---
    // raylib-ийн DrawCube / DrawCylinder нормалгүй (мод шиг хавтгай) тул MeshBuilder-ийн олон талст хэлбэрээр:
    // shader гэрлийг гурвалжны байрлалаас тооцож, гар бөөрөнхий, сүүдэртэй харагдана.
    const Color kGloveDark = { 82, 78, 66, 255 };   // хүрэн саарал (койот) бээлий: гэрэл, сүүдэр нь харагдана
    const Color kGloveMid = { 104, 98, 84, 255 };
    const Color kGlovePad = { 46, 45, 42, 255 };    // үе мөчний хатуу дэвсгэр
    const Color kSkin = { 208, 160, 130, 255 };
    const Color kSkinShade = { 178, 130, 104, 255 };

    // Атгасан гар (орон зай): бариул Z тэнхлэгийн дагуу (+Z = долоовор хурууны тал), алга -X талд, бугуй +Y зүгт,
    // хуруунууд үе мөчнөөс (-X, бага зэрэг доор) доогуур нь (+X) ороож эргэнэ, эрхий хуруу дээгүүр. rh: бариулын радиус.
    float FistWrist(float rh) { return rh + 0.02f; }

    MeshBuilder BuildFist(float rh)
    {
        MeshBuilder b;
        const float sweep = std::clamp(0.014f / rh, 0.55f, 1.0f); // бөмбөрцөг (гранат) атгахад хуруу бага ороно
        auto at = [](float degrees, float r, float z) {
            const float a = degrees * DEG2RAD;
            return Vector3{ std::cos(a) * r, std::sin(a) * r, z };
        };
        const float px = -(rh + 0.02f);
        // Алга, гарын ар (бээлийн хатуу дэвсгэр), бугуй, бээлийн ам
        b.Ellipsoid({ px, 0.024f, 0.0f }, { 0.017f, 0.046f, 0.047f }, 8, 14, kGloveDark);
        b.Ellipsoid({ px - 0.014f, 0.02f, 0.0f }, { 0.007f, 0.032f, 0.04f }, 6, 12, kGlovePad);
        b.Tube({ px, 0.05f, 0.0f }, { px, 0.095f, -0.004f }, 0.027f, 0.029f, 14, kGloveDark, false);
        b.Tube({ px, 0.082f, -0.003f }, { px, 0.1f, -0.004f }, 0.032f, 0.032f, 14, kGloveMid, true);
        // Дөрвөн хуруу: үе мөч (хатуу дэвсгэр), хоёр бээлийтэй үе, нүцгэн үзүүр
        const float zs[4] = { 0.034f, 0.0115f, -0.0115f, -0.033f };
        const float thick[4] = { 1.0f, 1.05f, 0.98f, 0.86f };
        for (int i = 0; i < 4; ++i)
        {
            const float z = zs[i], t = thick[i];
            const Vector3 k = at(192.0f, rh + 0.023f, z);
            const Vector3 p1 = at(192.0f + 62.0f * sweep, rh + 0.012f, z);
            const Vector3 p2 = at(192.0f + 126.0f * sweep, rh + 0.0105f, z);
            const Vector3 tip = at(192.0f + 176.0f * sweep, rh + 0.0095f, z * 0.97f);
            const float r0 = 0.0108f * t, r1 = 0.0099f * t, r2 = 0.0089f * t;
            b.Tube({ px, -0.006f, z * 0.92f }, k, 0.0115f * t, r0, 10, kGloveDark, false); // алганаас үе мөч хүртэл
            b.Ellipsoid(k, { r0 * 1.2f, r0 * 1.2f, r0 * 1.05f }, 6, 10, kGlovePad);
            b.Tube(k, p1, r0, r1, 10, kGloveDark, false);
            b.Ellipsoid(p1, { r1, r1, r1 }, 5, 10, kGloveDark);
            b.Tube(p1, p2, r1, r2, 10, kGloveMid, false);
            b.Ellipsoid(p2, { r2, r2, r2 }, 5, 10, kSkinShade);
            b.Tube(p2, tip, r2, r2 * 0.92f, 10, kSkin, false);
            b.Ellipsoid(tip, { r2 * 0.92f, r2 * 0.92f, r2 * 0.95f }, 5, 10, kSkin);
        }
        // Эрхий хуруу: алганы долоовор талын дээд хэсгээс бариулын дээгүүр
        const Vector3 t0 = { px + 0.004f, 0.036f, 0.034f };
        const Vector3 t1 = at(128.0f, rh + 0.0135f, 0.03f);
        const Vector3 t2 = at(84.0f, rh + 0.0115f, 0.02f);
        b.Ellipsoid(t0, { 0.0145f, 0.016f, 0.014f }, 6, 10, kGloveDark);
        b.Tube(t0, t1, 0.0128f, 0.0112f, 10, kGloveDark, false);
        b.Ellipsoid(t1, { 0.0112f, 0.0112f, 0.0112f }, 5, 10, kGloveMid);
        b.Tube(t1, t2, 0.0105f, 0.0095f, 10, kSkin, false);
        b.Ellipsoid(t2, { 0.0095f, 0.0095f, 0.0095f }, 5, 10, kSkin);
        return b;
    }

    // Загварын гар (assets/fp_hands: tools/bake_fp_hands.py rigged_arms.glb-ээс атгуулж бэлтгэнэ): мөр бүр нэг
    // гурвалжин "r g b  x y z  x y z  x y z". Файл байхгүй бол хоосон (кодоор угсарсан гар зурагдана).
    MeshBuilder LoadHandMesh(const char* name)
    {
        MeshBuilder b;
        // (туршилт, ZM_MODEL_HANDS=1: rigged_arms.glb нь дөрвөлжин хэсгүүдээс бүтсэн тул кодоор угсарсан гараас
        // илүү хайрцаг шиг харагдсан; анхдагчаар кодоор угсарсан гар)
        if (!std::getenv("ZM_MODEL_HANDS")) return b;
        const std::string application = GetApplicationDirectory();
        for (const std::string& folder : { std::string("assets/fp_hands/"), application + "assets/fp_hands/", application + "../assets/fp_hands/" })
        {
            char* text = LoadFileText((folder + name).c_str());
            if (!text) continue;
            const char* p = text;
            while (*p)
            {
                int r, g, bl;
                float v[9];
                int used = 0;
                if (std::sscanf(p, "%d %d %d %f %f %f %f %f %f %f %f %f%n", &r, &g, &bl, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6],
                                &v[7], &v[8], &used) == 12)
                {
                    const Color c = { (unsigned char)r, (unsigned char)g, (unsigned char)bl, 255 };
                    b.Triangle({ v[0], v[1], v[2] }, { v[3], v[4], v[5] }, { v[6], v[7], v[8] }, c);
                }
                const char* next = std::strchr(p, '\n');
                if (!next) break;
                p = next + 1;
            }
            UnloadFileText(text);
            TraceLog(LOG_INFO, "VIEWMODEL: hand mesh %s loaded (%d triangles)", name, b.TriangleCount());
            break;
        }
        return b;
    }

    const MeshBuilder& ModelFist(bool ball)
    {
        static const MeshBuilder tight = LoadHandMesh("fist_tight.txt");
        static const MeshBuilder round = LoadHandMesh("fist_round.txt");
        return ball ? round : tight;
    }

    const MeshBuilder& ModelForearm()
    {
        static const MeshBuilder forearm = LoadHandMesh("forearm.txt");
        return forearm;
    }

    const MeshBuilder& Fist(bool ball)
    {
        static const MeshBuilder tight = BuildFist(0.015f);
        static const MeshBuilder round = BuildFist(0.03f);
        const MeshBuilder& model = ModelFist(ball);
        if (model.TriangleCount() > 0) return model;
        return ball ? round : tight;
    }

    // Ф-1 гранат (1-р этгээд): өндгөн их бие, хэрчлээс, гал хамгаалагч, хөшүүрэг, цагираг. Эх нь их биеийн төв.
    const MeshBuilder& ViewGrenade()
    {
        static const MeshBuilder grenade = [] {
            MeshBuilder b;
            const Color body = { 86, 104, 64, 255 }, groove = { 52, 64, 40, 255 }, metal = { 150, 150, 142, 255 };
            b.Ellipsoid({ 0.0f, 0.0f, 0.0f }, { 0.028f, 0.035f, 0.028f }, 10, 16, body);
            // Хэрчлээс: хэвтээ ба босоо ховилууд (бага зэрэг томоор, бараан)
            for (int i = -2; i <= 2; ++i)
            {
                const float yy = 0.011f * (float)i;
                const float r = 0.028f * std::sqrt(std::max(0.0f, 1.0f - (yy / 0.035f) * (yy / 0.035f))) + 0.0007f;
                b.Tube({ 0.0f, yy - 0.0012f, 0.0f }, { 0.0f, yy + 0.0012f, 0.0f }, r, r, 18, groove, false);
            }
            for (int k = 0; k < 4; ++k)
            {
                const float a = (float)k * PI * 0.25f;
                b.Ellipsoid({ 0.0f, 0.0f, 0.0f }, { 0.0287f * std::fabs(std::cos(a)) + 0.0012f, 0.0352f, 0.0287f * std::fabs(std::sin(a)) + 0.0012f }, 8, 4, groove);
            }
            b.Ellipsoid({ 0.0f, 0.0f, 0.0f }, { 0.0282f, 0.0353f, 0.0282f }, 10, 16, body);
            b.Tube({ 0.0f, 0.03f, 0.0f }, { 0.0f, 0.052f, 0.0f }, 0.009f, 0.009f, 12, metal);           // гал хамгаалагч
            b.Tube({ 0.0f, 0.052f, 0.0f }, { 0.0f, 0.056f, 0.0f }, 0.011f, 0.011f, 12, metal);
            b.Box({ 0.0f, 0.046f, 0.012f }, { 0.008f, 0.004f, 0.016f }, metal);                         // хөшүүргийн толгой
            b.Box({ 0.0f, 0.018f, 0.026f }, { 0.008f, 0.056f, 0.003f }, metal);                         // хөшүүрэг (хажуугаар доош)
            for (int k = 0; k < 10; ++k)                                                                // цагираг
            {
                const float a0 = (float)k / 10.0f * 2.0f * PI, a1 = (float)(k + 1) / 10.0f * 2.0f * PI;
                b.Tube({ -0.012f, 0.05f + std::sin(a0) * 0.01f, -0.006f + std::cos(a0) * 0.01f },
                       { -0.012f, 0.05f + std::sin(a1) * 0.01f, -0.006f + std::cos(a1) * 0.01f }, 0.0014f, 0.0014f, 6, metal, false);
            }
            return b;
        }();
        return grenade;
    }

    // Атгасан гарыг байрлуулна: center (бариулын төв), axis (бариулын дагуу, долоовор хурууны тал), wristDir
    // (бугуй руу). Баруун гар эдгээр хоёр чиглэлээр бүрэн тодорхойлогдоно; left: толин тусгал. Бугуйн цэгийг буцаана.
    // Загварын гар (CharacterModel: цэргийн загвар) ашиглах үед: DrawFistAt зурахгүй, бариулын зорилтыг (ертөнцийн
    // орон зайд) энд бүртгэнэ; DrawViewModel-ийн төгсгөлд DrawCharacterArms нэг дор зурна.
    bool viewModelArms = false;
    Vector3 viewLeftPole = { 0.4f, -1.0f, 0.1f }; // зүүн тохойн чиглэл (харах орон зайд), DrawViewModel бүрд анхны утга
    int viewArmsId = kCharacterFpArms; // SetViewModelArms
    Color viewArmsTint = WHITE;
    ViewArmTarget viewTargets[2];
    Matrix viewBody = MatrixIdentity(); // загварын гарын биеийн орон зай -> ертөнц (AnchorViewBody)
    bool viewBodySet = false;

    // Одоогийн rlgl орон зайд (зэвсгийн орон зай, масштабтайгаа) баруун мөрийг shoulder цэгт тавьж биеийг бэхэлнэ:
    // бие зэвсэгтэйгээ хамт масштаблагдаж, 3-р этгээдийнх шиг гар бариулд хүрнэ.
    // yaw: биеийг баруун мөрийг тойрон эргүүлнэ (градус; сөрөг бол зүүн мөр урагш: урт буу барих зогсолт)
    void AnchorViewBody(Vector3 shoulder, float yaw = 0.0f)
    {
        if (!viewModelArms) return;
        const Vector3 body = CharacterRightShoulder(viewArmsId);
        // Зэвсгийг дэлгэцэнд жижигрүүлж (урт буу 72%) зурдаг: гар түүнийг дагаж жижгэрвэл (40 см) бариулд хүрэхгүй,
        // бие урагш гарч мөрний ханцуйн нүх дэлгэцэнд ордог байв. Гар бодит хэмжээгээрээ (55 см) үлдэнэ.
        const Matrix transform = rlGetMatrixTransform();
        const float weaponScale = Vector3Length({ transform.m0, transform.m1, transform.m2 });
        const float unscale = weaponScale > 0.001f ? 1.0f / weaponScale : 1.0f;
        const Matrix local = MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixTranslate(-body.x, -body.y, -body.z), MatrixRotateY(yaw * DEG2RAD)),
                                                           MatrixScale(unscale, unscale, unscale)),
                                            MatrixTranslate(shoulder.x, shoulder.y, shoulder.z));
        viewBody = MatrixMultiply(local, transform);
        viewBodySet = true;
    }

    Vector3 DrawFistAt(Vector3 center, Vector3 axis, Vector3 wristDir, bool left, bool ball = false)
    {
        const Vector3 z = Vector3Normalize(axis);
        Vector3 y = Vector3Subtract(wristDir, Vector3Scale(z, Vector3DotProduct(wristDir, z)));
        y = Vector3Normalize(y);
        // (харах орон зайн +X нь зүүн тийш тул баруун гар нь толин тусгалаар)
        Vector3 x = Vector3CrossProduct(y, z);
        if (!left) x = Vector3Negate(x);
        Matrix m = MatrixIdentity();
        m.m0 = x.x; m.m1 = x.y; m.m2 = x.z;
        m.m4 = y.x; m.m5 = y.y; m.m6 = y.z;
        m.m8 = z.x; m.m9 = z.y; m.m10 = z.z;
        m.m12 = center.x; m.m13 = center.y; m.m14 = center.z;
        if (viewModelArms)
        {
            // Алганы төв бариулын алганы талд (-x), алга бариул руу (+x) харна. Одоогийн rlgl хувиргалтаар ертөнц рүү.
            const float rh = ball ? 0.03f : 0.015f;
            const Matrix toWorld = rlGetMatrixTransform();
            const Vector3 origin = Vector3Transform({ 0.0f, 0.0f, 0.0f }, toWorld);
            ViewArmTarget& target = viewTargets[left ? 1 : 0];
            target.active = true;
            target.palm = Vector3Transform(Vector3Subtract(center, Vector3Scale(x, rh + 0.012f)), toWorld);
            target.facing = Vector3Normalize(Vector3Subtract(Vector3Transform(x, toWorld), origin));
            target.fingers = Vector3Normalize(Vector3Subtract(Vector3Transform(Vector3Negate(y), toWorld), origin));
            target.framed = true;
            target.curl = ball ? 50.0f : 74.0f; // (гранат: хуруу хагас ороосон, гранат хурууны дээгүүр харагдана)
            return center;
        }
        // (толин тусгал гурвалжны эргэлтийн чиглэлийг урвуулна: арын нүүр хасалтыг түр унтраана)
        rlDrawRenderBatchActive();
        rlDisableBackfaceCulling();
        rlPushMatrix();
        rlMultMatrixf(MatrixToFloat(m));
        Fist(ball).DrawImmediate();
        rlPopMatrix();
        rlDrawRenderBatchActive();
        rlEnableBackfaceCulling();
        const float rh = ball ? 0.03f : 0.015f;
        if (ModelFist(ball).TriangleCount() > 0) return Vector3Add(center, Vector3Add(Vector3Scale(x, -(rh + 0.015f)), Vector3Scale(y, 0.082f)));
        return Vector3Add(center, Vector3Add(Vector3Scale(x, -FistWrist(rh)), Vector3Scale(y, 0.098f)));
    }

    // Сул гар (хутга, гранат барьж байхад зүүн гар): хуруунууд хагас нугалсан (5 см-ийн бөмбөрцөг атгаж буй мэт)
    // Загварын гар: зүүн гарын алгыг дээш харуулан буугаа алган дээрээ тавина (хуруу бага зэрэг нугалсан, бууг
    // атгахгүй). palm: алганы төв (одоогийн rlgl орон зайд), up: алганы харах зүг (буу руу), fingers: хурууны зүг.
    Vector3 DrawSupportPalmAt(Vector3 palm, Vector3 up, Vector3 fingers, float curl)
    {
        if (!viewModelArms) return palm;
        const Matrix toWorld = rlGetMatrixTransform();
        const Vector3 origin = Vector3Transform({ 0.0f, 0.0f, 0.0f }, toWorld);
        ViewArmTarget& target = viewTargets[1];
        target.active = true;
        target.palm = Vector3Transform(palm, toWorld);
        target.facing = Vector3Normalize(Vector3Subtract(Vector3Transform(up, toWorld), origin));
        target.fingers = Vector3Normalize(Vector3Subtract(Vector3Transform(fingers, toWorld), origin));
        target.framed = false; // (сарвуу шууны шугамыг үргэлжлүүлнэ: бугуй нугарч "хугарсан" мэт харагдахгүй)
        target.curl = curl;
        return palm;
    }

    Vector3 DrawRelaxedHandAt(Vector3 center, Vector3 axis, Vector3 wristDir, bool left)
    {
        if (viewModelArms)
        {
            // Загварын гар: зүүн гарыг энэ цэгт сул (хуруу бага зэрэг нугалсан) байрлуулна
            const Vector3 zAxis = Vector3Normalize(axis);
            const Vector3 yAxis = Vector3Normalize(Vector3Subtract(wristDir, Vector3Scale(zAxis, Vector3DotProduct(wristDir, zAxis))));
            Vector3 xAxis = Vector3CrossProduct(yAxis, zAxis);
            if (!left) xAxis = Vector3Negate(xAxis);
            const Matrix toWorld = rlGetMatrixTransform();
            const Vector3 origin = Vector3Transform({ 0.0f, 0.0f, 0.0f }, toWorld);
            ViewArmTarget& target = viewTargets[left ? 1 : 0];
            target.active = true;
            target.palm = Vector3Transform(center, toWorld);
            target.facing = Vector3Normalize(Vector3Subtract(Vector3Transform(xAxis, toWorld), origin));
            target.fingers = Vector3Normalize(Vector3Subtract(Vector3Transform(Vector3Negate(yAxis), toWorld), origin));
            target.framed = true;
            target.curl = 28.0f;
            return center;
        }
        static const MeshBuilder relaxed = BuildFist(0.05f);
        const Vector3 z = Vector3Normalize(axis);
        const Vector3 y = Vector3Normalize(Vector3Subtract(wristDir, Vector3Scale(z, Vector3DotProduct(wristDir, z))));
        Vector3 x = Vector3CrossProduct(y, z);
        if (!left) x = Vector3Negate(x);
        Matrix m = MatrixIdentity();
        m.m0 = x.x; m.m1 = x.y; m.m2 = x.z;
        m.m4 = y.x; m.m5 = y.y; m.m6 = y.z;
        m.m8 = z.x; m.m9 = z.y; m.m10 = z.z;
        m.m12 = center.x; m.m13 = center.y; m.m14 = center.z;
        rlDrawRenderBatchActive();
        rlDisableBackfaceCulling();
        rlPushMatrix();
        rlMultMatrixf(MatrixToFloat(m));
        relaxed.DrawImmediate();
        rlPopMatrix();
        rlDrawRenderBatchActive();
        rlEnableBackfaceCulling();
        return Vector3Add(center, Vector3Add(Vector3Scale(x, -FistWrist(0.05f)), Vector3Scale(y, 0.098f)));
    }

    // Шуу: тохойноос (дэлгэцийн гадна) бугуй хүртэл ханцуй (атираатай), ханцуйн ам, бээлийн ам
    void ViewForearm(Vector3 elbow, Vector3 wrist)
    {
        if (viewModelArms) return; // (загварын гар өөрийн ханцуйтай)
        const MeshBuilder& model = ModelForearm();
        if (model.TriangleCount() > 0)
        {
            // Загварын шуу: бугуй эх, тохой +Y (урт 0.232 м-ийг тохой хүртэл сунгана); нуруу (+Z) дээш
            const Vector3 along = Vector3Subtract(elbow, wrist);
            const float length = Vector3Length(along);
            const Vector3 y = Vector3Scale(along, 1.0f / std::max(length, 0.001f));
            Vector3 z = Vector3Subtract({ 0.0f, 1.0f, 0.0f }, Vector3Scale(y, y.y));
            z = Vector3Length(z) < 0.01f ? Vector3{ 0.0f, 0.0f, 1.0f } : Vector3Normalize(z);
            const Vector3 x = Vector3CrossProduct(y, z);
            const float stretch = length / 0.232f;
            Matrix m = MatrixIdentity();
            m.m0 = x.x; m.m1 = x.y; m.m2 = x.z;
            m.m4 = y.x * stretch; m.m5 = y.y * stretch; m.m6 = y.z * stretch;
            m.m8 = z.x; m.m9 = z.y; m.m10 = z.z;
            m.m12 = wrist.x; m.m13 = wrist.y; m.m14 = wrist.z;
            rlDrawRenderBatchActive();
            rlDisableBackfaceCulling();
            rlPushMatrix();
            rlMultMatrixf(MatrixToFloat(m));
            model.DrawImmediate();
            rlPopMatrix();
            rlDrawRenderBatchActive();
            rlEnableBackfaceCulling();
            return;
        }
        MeshBuilder b;
        const Vector3 cuff = Vector3Lerp(elbow, wrist, 0.84f);
        b.Tube(elbow, cuff, 0.047f, 0.037f, 16, kSleeve, false);
        for (const float k : { 0.3f, 0.52f, 0.7f })
        {
            const float r = Lerp(0.047f, 0.037f, k / 0.84f) + 0.0015f;
            b.Tube(Vector3Lerp(elbow, wrist, k), Vector3Lerp(elbow, wrist, k + 0.05f), r, r - 0.001f, 16, Shade(kSleeve, 0.86f), false);
        }
        b.Tube(cuff, Vector3Lerp(elbow, wrist, 0.94f), 0.041f, 0.04f, 16, Shade(kSleeve, 0.74f), true); // ханцуйн ам
        b.Tube(Vector3Lerp(elbow, wrist, 0.9f), wrist, 0.031f, 0.03f, 14, kGloveDark, false);
        b.DrawImmediate();
    }

    // Бээлийн хэсэг (тэгш өнцөгт), бага зэрэг цайвар үетэй
    void GlovePart(float x, float y, float z, float sx, float sy, float sz)
    {
        DrawCubeV({ x, y, z }, { sx, sy, sz }, kGlove);
        DrawCubeV({ x, y + sy * 0.5f - 0.002f, z }, { sx * 0.96f, 0.004f, sz * 0.96f }, Shade(kGlove, 1.35f));
    }

    void ViewArm(Vector3 from, Vector3 hand)
    {
        DrawCylinderEx(from, hand, 0.06f, 0.045f, 8, kSleeve);
        const Vector3 cuff = Vector3Lerp(from, hand, 0.82f);
        DrawCylinderEx(cuff, Vector3Lerp(from, hand, 0.9f), 0.05f, 0.05f, 8, Shade(kSleeve, 0.7f));
        DrawSphereEx(hand, 0.045f, 5, 8, kGlove);
    }
}

void SetViewModelArms(int characterModel)
{
    // Дүр өөрийн гартай бол (нүцгэн биетэй, хуягтай дүр) түүгээр, үгүй бол тактикийн бээлийтэй гараар (fp_arms)
    viewArmsId = characterModel >= 0 && HasCharacterArms(characterModel) ? characterModel : kCharacterFpArms;
    // Iron (хуягт робот): бээлийтэй гарыг хуяг шиг хүрэл өнгөөр
    viewArmsTint = characterModel == kCharacterIron ? Color{ 196, 150, 96, 255 } : WHITE;
}

void DrawViewModel(const Camera3D& camera, HeldItem held, float recoil, float walkPhase, float moveAmount, float reload,
                   float action, float aim)
{
    // Алхахад найгах, буудахад ухрах, цэнэглэхэд доошлох (онилж байхад найгалт бараг алга)
    aim = std::clamp(aim, 0.0f, 1.0f);
    const float sway = std::min(1.0f, moveAmount) * (1.0f - 0.85f * aim);
    const float time = (float)GetTime();
    const float still = (1.0f - std::min(1.0f, moveAmount)) * (1.0f - 0.85f * aim); // тайван үеийн амьсгалын найгалт
    const float bobX = std::sin(walkPhase) * 0.012f * sway + 0.004f * still * std::sin(time * 0.9f);
    const float bobY = -std::fabs(std::cos(walkPhase)) * 0.016f * sway + 0.005f * still * std::sin(time * 1.6f);
    const float reloading = reload >= 0.0f ? std::sin(reload * PI) : 0.0f;
    // Гүйж байна: зэвсэг доош, дотогш хазайна (онилоход, цэнэглэхэд, буудаж байхад үгүй: буудсанаас хойш 0.4 с
    // буу шулуун урагшаа; эс бөгөөс гүйж буудахад сум далий барьсан буунаас гардаг байв)
    static float firingHold = 0.0f, firing = 0.0f;
    const float frame = std::min(0.1f, GetFrameTime());
    firingHold = recoil > 0.05f ? 0.4f : std::max(0.0f, firingHold - frame);
    firing += ((firingHold > 0.0f ? 1.0f : 0.0f) - firing) * std::min(1.0f, 18.0f * frame);
    const float sprint = std::clamp((moveAmount - 1.1f) / 0.35f, 0.0f, 1.0f) * (1.0f - aim) * (1.0f - reloading) * (1.0f - firing);

    viewModelArms = HasCharacterArms(viewArmsId);
    viewTargets[0] = viewTargets[1] = ViewArmTarget{};
    viewLeftPole = { 0.4f, -1.0f, 0.1f };
    viewBodySet = false;

    rlPushMatrix();
    rlMultMatrixf(MatrixToFloat(ViewBasis(camera)));
    rlTranslatef(bobX, bobY - 0.12f * reloading, 0.0f);

    const bool longGun = held == HeldItem::Rifle || held == HeldItem::Shotgun || held == HeldItem::Sniper ||
                         held == HeldItem::Launcher;
    if (longGun)
    {
        // Бэлэн (онилоогүй) үед: буу дэлгэцийн баруун доороос төв рүү, ам нь хараа руу; хоёр гар, шуунууд харагдана
        // (баруун гар бариулд дэлгэцийн доод захад, зүүн гар урд модонд, ханцуйтай шуу зүүн доод булангаас).
        // Буу 72% хэмжээтэй, бага зэрэг дотогш эргэсэн (-8° эргэлт). Өмнө нь 48% хэмжээтэй, дэлгэцийн баруун доод
        // буланд жижигхэн (Counter-Strike маягийн) байж гар бараг харагддаггүй байв.
        // Онилоход (aim) гол төмөр нь дэлгэцийн төвд, хараан доор ирнэ (x = 0); ар тал нь харааг халхлахгүй
        // байхаар бага зэрэг урагш, доош байрлана. Гранат харвагч мөрөн дээр (хуучин байрлалаараа).
        const bool launcher = held == HeldItem::Launcher;
        const float hipScale = launcher ? 0.48f : 0.72f, hipX = launcher ? -0.11f : 0.02f, hipY = launcher ? -0.32f : -0.37f,
                    hipZ = launcher ? 0.13f : 0.28f, hipYaw = launcher ? 3.0f : 4.0f, hipRoll = launcher ? 0.0f : -8.0f,
                    hipPitch = launcher ? 0.0f : -2.0f;
        // Counter-Strike маягаар: буу дэлгэцийн баруун талд (10 см баруун тийш), ам нь хараа руу 9° дотогш
        // (CS-ийн AWP, AK шиг: буу бараг хэвтээ, дэлгэцийн баруун доор, ам нь төв рүү 14°; бөгс баруун доод булангаас
        // гарна, зүүн гар урд модыг доороос, баруун гарын зөвхөн ар тал буланд харагдана)
        // Дэлгэцийн цэгүүдээс тооцсон (CS-ийн AWP зураг): ам нь хараанаас бага зэрэг баруун доор, их бие дэлгэцийн
        // баруун доор (дээд тал нь харагдана), бариул баруун доод буланд. Бууны эх нь зүүн тийш 24 см зөрүүтэй тул
        // эргэлтүүд том шилжилттэй хамт.
        if (launcher)
        {
            const float scale = Lerp(hipScale, 0.55f, aim);
            rlTranslatef(Lerp(hipX, 0.132f, aim), Lerp(hipY, -0.25f, aim) - 0.06f * sprint, Lerp(hipZ, 0.24f, aim) - 0.04f * recoil - 0.03f * sprint);
            rlRotatef(hipYaw * (1.0f - aim) + 28.0f * sprint, 0.0f, 1.0f, 0.0f);
            rlRotatef(hipRoll * (1.0f - aim), 0.0f, 0.0f, 1.0f);
            rlRotatef(10.0f * sprint + hipPitch * (1.0f - aim), 1.0f, 0.0f, 0.0f);
            rlRotatef(-12.0f * sprint, 0.0f, 0.0f, 1.0f);
            rlScalef(scale, scale, scale);
            rlRotatef(-5.0f * recoil - 18.0f * reloading, 1.0f, 0.0f, 0.0f);
            rlRotatef(-25.0f * reloading, 0.0f, 0.0f, 1.0f);
        }
        else
        {
            // Бие (мөр) камерт бэхлэгдэнэ: баруун мөр дэлгэцийн баруун доод захаас гадна, зүүн мөр урагш (35 градус):
            // баруун шуу баруун доод булангаас бариул руу, зүүн шуу доороос урд мод руу орж ирнэ.
            // Онилоход мөр төв рүү, бие шулуун.
            // (автомат, дурантай буу: зүүн гар урд модыг алган дээрээ тавина: зүүн мөр илүү урагш (65 градус, 12 см урд,
            // 40 см доор: нүцгэн model2-ийн бугалга дэлгэцэнд орохгүй),
            // эс бөгөөс гар урд модонд хүрэхгүй (71 см / 53 см))
            const bool restPose = viewModelArms && (held == HeldItem::Rifle || held == HeldItem::Sniper);
            AnchorViewBody({ Lerp(-0.2f, -0.12f, aim), Lerp(restPose ? -0.4f : -0.27f, restPose ? -0.32f : -0.25f, aim), Lerp(restPose ? 0.12f : -0.06f, restPose ? 0.02f : -0.12f, aim) },
                           Lerp(restPose ? -65.0f : -35.0f, restPose ? -45.0f : -10.0f, aim));
            // Бэлэн байрлал (LongGunHip) ба онилсон байрлал (гол төмөр хараан доор, 55%) хооронд: бариулын байрлал,
            // эргэлт, хэмжээг зөөлөн шилжүүлнэ. Гүйх, ухрах, цэнэглэх хөдөлгөөн бариулыг тойрно.
            const LongGunPose hip = LongGunHip(camera.fovy);
            const Vector3 sightGrip = Vector3Add({ 0.132f, -0.25f, 0.24f }, Vector3Scale(kLongGripLocal, 0.55f));
            const Quaternion turn = QuaternionSlerp(QuaternionFromMatrix(hip.rotation), QuaternionIdentity(), aim);
            const float scale = Lerp(kLongScale, 0.55f, aim);
            const Vector3 grip = Vector3Lerp(hip.grip, sightGrip, aim);
            rlTranslatef(grip.x, grip.y - 0.06f * sprint, grip.z - 0.04f * recoil - 0.03f * sprint);
            rlMultMatrixf(MatrixToFloat(QuaternionToMatrix(turn)));
            rlScalef(scale, scale, scale);
            rlRotatef(28.0f * sprint, 0.0f, 1.0f, 0.0f);
            rlRotatef(10.0f * sprint - 5.0f * recoil - 18.0f * reloading, 1.0f, 0.0f, 0.0f);
            rlRotatef(-12.0f * sprint - 25.0f * reloading, 0.0f, 0.0f, 1.0f);
            rlTranslatef(-kLongGripLocal.x, -kLongGripLocal.y, -kLongGripLocal.z);
        }
        if (held == HeldItem::Rifle) DrawGun(0.0f, true);
        else if (held == HeldItem::Shotgun) DrawShotgun(0.0f, true);
        else if (held == HeldItem::Sniper) DrawSniper(0.0f, true);
        else DrawLauncher(0.0f, true);

        // Баруун гар бариулд, зүүн гар урд модонд (бууны орон зайд, DrawGun-ийн хэмжээсээр)
        const float x = kHumanoidMuzzle.x, y = kHumanoidMuzzle.y - kHipHeight;
        // Бөгс (z ≈ -0.05) мөрөнд. Загварын гарт: мөр бууны шугамаас 30 см доор, бие 35° эргэсэн (зүүн мөр урагш):
        // зүүн шуу дэлгэцийн зүүн доод булангаас урд мод руу налуу гарна, бугалга, мөрний ханцуй дэлгэцэнд орохгүй.
        // Онилоход (загварын гар): мөр 40 см доош, 10 см хойш, бие бараг шулуун (10°): зүүн гар бууны ард хөндлөн
        // гарахгүй, урд модыг доороос нь барина
        const float aimDrop = 0.4f, aimYaw = -10.0f, aimBack = 0.1f;
        if (launcher)
        {
            AnchorViewBody({ x - 0.02f, y + 0.02f - (viewModelArms ? Lerp(0.3f, aimDrop, aim) : 0.0f), -0.17f - (viewModelArms ? aimBack * aim : 0.0f) },
                           Lerp(-35.0f, viewModelArms ? aimYaw : -35.0f, aim));
        }
        // (онилоход загварын гарын зүүн гар урд модыг доороос нь: хуруу нь бууны дээгүүр гарч харааг халхлахгүй)
        const float lowY = viewModelArms ? -0.08f * aim : 0.0f; // (8 см доош: хуруу бууны дээгүүр гарч харааг халхлахгүй)
        const Vector3 lowWrist = { 0.0f, -0.5f * aim, 0.0f };
        if (launcher)
        {
            const Vector3 rw = DrawFistAt({ x, y - 0.13f, 0.42f }, { 0.0f, 1.0f, 0.15f }, { -0.3f, -0.6f, -1.0f }, false);
            const Vector3 lw = DrawFistAt({ x + 0.02f, y - 0.13f, 0.66f }, { 0.0f, 1.0f, 0.1f }, { 0.8f, -0.7f, -0.5f }, true);
            ViewForearm({ x - 0.18f, y - 0.5f, 0.12f }, rw);
            ViewForearm({ x + 0.32f, y - 0.5f, 0.42f }, lw);
        }
        else
        {
            // Баруун гар: гар бариулд (дээш, урагш хазайсан). Зүүн гар зэвсгээс хамаарна:
            //   автомат — урд модыг доороос нь, цэнэглэхэд магазин руу хойш;
            //   үрлэн буу — гүйдэг гар (pump) дээр, буудах бүрт хойш татна;
            //   дурантай буу — урагш, доороос нь алган дээрээ тулгана (тогтвортой онилгоо).
            // Загварын гар (CS маягаар): баруун шуу дэлгэцийн баруун доороос, гарын ар тал харагдана; зүүн гар урд модыг
            // доороос нь алгаа дээш харуулан барина (хажуугаас нь биш)
            const Vector3 rightWristDir = viewModelArms ? Vector3{ -0.6f, -0.9f, -0.4f } : Vector3{ -0.2f, -0.6f, -1.0f };
            const Vector3 underWrist = { 0.4f, -0.4f, -0.3f };
            // Загварын гартай автомат, дурантай буу: зөвхөн зүүн гар (алган дээрээ тавьсан); баруун гар дэлгэцэнд
            // харагдахгүй (доош унжина)
            const bool palmRest = viewModelArms && (held == HeldItem::Rifle || held == HeldItem::Sniper);
            const Vector3 rw = palmRest ? Vector3{ x, y - 0.16f, 0.3f } : DrawFistAt({ x, y - 0.16f, 0.3f }, { 0.0f, 1.0f, 0.3f }, rightWristDir, false);
            Vector3 lw;
            if (held == HeldItem::Shotgun)
            {
                const float pump = std::clamp(recoil * 1.6f, 0.0f, 1.0f); // буудсаны дараа гүйдэг гарыг татаж буцаана
                lw = DrawFistAt({ x, y - (viewModelArms ? 0.075f : 0.035f) + lowY, (viewModelArms ? 0.58f : 0.66f) - 0.09f * pump - 0.15f * reloading }, { 0.0f, 0.0f, 1.0f },
                                Vector3Add(viewModelArms ? underWrist : Vector3{ 0.8f, -0.8f, -0.15f }, viewModelArms ? lowWrist : Vector3{ 0.0f, 0.0f, 0.0f }),
                                true);
            }
            else if (palmRest)
            {
                // Алга урд модны (улаан модон хэсэг, z 0.8-0.9) доор, гол төмрөөс 7.5 см доош, дээш харна; хуруу бууны баруун
                // тал руу, бага зэрэг урагш. Онилоход бага зэрэг хойш (хараанаас холдуулна).
                const float restZ = 0.72f - 0.06f * aim - 0.24f * reloading;
                // (онилоход хуруу гол төмрийн дагуу урагш, бага зэрэг ороосон: ардаас харахад бууны хажуугаар цухуйхгүй)
                // Зүүн шуу дэлгэцийн доороос бараг босоо дээш (тохой доош). Алга урд модны (z 0.65-0.87, төв нь гол
                // төмрөөс 1 см доор, 9 см өргөн) ар хэсэгт, зүүн хажууд нь наалдаж, хуруу нь дээгүүр нь ороож атгана
                // (гарын ар тал камер руу харна; эрхий хуруу доогуур нь ороогдохоор хуруу 95 градус нугарна);
                // сарвуу шууны шугамаар (бугуй нугарахгүй)
                const float tp[8] = { 0.055f, -0.03f, 0.66f, -1.0f, 0.5f, 0.3f, -1.0f, -0.3f };
                viewLeftPole = { tp[5], tp[6], tp[7] };
                lw = DrawSupportPalmAt({ x + tp[0], y + tp[1] + lowY, tp[2] - 0.06f * aim - 0.24f * reloading }, { tp[3], tp[4], 0.0f }, Vector3Lerp({ -1.0f, 0.0f, 0.35f }, { -0.2f, 0.0f, 1.0f }, aim),
                                       Lerp(95.0f, 45.0f, aim));
            }
            else if (held == HeldItem::Sniper)
            {
                lw = DrawFistAt({ x + 0.01f, y - 0.075f + lowY, (viewModelArms ? 0.66f : 0.84f) - 0.22f * reloading }, { 0.0f, 0.15f, 1.0f },
                                Vector3Add(viewModelArms ? underWrist : Vector3{ 0.6f, -1.0f, -0.1f }, viewModelArms ? lowWrist : Vector3{ 0.0f, 0.0f, 0.0f }), true);
            }
            else
            {
                lw = DrawFistAt({ x + 0.005f, y - 0.085f + lowY, (viewModelArms ? 0.66f : 0.78f) - 0.24f * reloading }, { 0.0f, 0.0f, 1.0f },
                                Vector3Add(viewModelArms ? underWrist : Vector3{ 0.85f, -0.75f, -0.2f }, viewModelArms ? lowWrist : Vector3{ 0.0f, 0.0f, 0.0f }),
                                true);
            }
            ViewForearm({ x - 0.16f, y - 0.46f, 0.0f }, rw);
            ViewForearm({ x + 0.3f, y - 0.46f, 0.52f - 0.15f * reloading }, lw);
        }
    }
    else if (held == HeldItem::Pistol)
    {
        // Гар буу: хоёр гараар урагш сунган барина (CS маягаар камерт ойр: гар том харагдана)
        // Буудахад ам нь дээш үсэрч гар хойш түлхэгдэнэ; алхахад, тайван үед бага зэрэг хазайна.
        // Загварын (бодит хэмжээтэй) гар кодоор угсарсан нударгаас том: буу арай хол, гар нь бууны доор (гулсуур
        // нь гарын дээгүүр харагдана). 60 см урагш, 22 см доош: ойр байхад хоёр сарвуу бөөн болж буугаа далдалдаг байв.
        // Загварын гар: Counter-Strike маягаар нэг гараар, дэлгэцийн баруун доор, ам нь хараа руу дотогш (8°), бага
        // зэрэг хазайсан (-6°); гулсуур, ар талын хараа харагдана. Онилоход хоёр гараар төвд.
        const float px = viewModelArms ? -0.15f : -0.08f, py = viewModelArms ? -0.19f : -0.14f, pz = viewModelArms ? 0.38f : 0.32f,
                    ps = viewModelArms ? 0.85f : 0.8f, pyaw = viewModelArms ? 8.0f : 4.0f, proll = viewModelArms ? -6.0f : 0.0f;
        const float oneHand = viewModelArms ? 1.0f - std::clamp(aim * 2.0f, 0.0f, 1.0f) : 0.0f; // CS маягаар нэг гараар (онилоход хоёр гараар)
        rlTranslatef(Lerp(px, 0.0f, aim), Lerp(py, -0.128f, aim) + 0.03f * recoil - 0.07f * sprint, Lerp(pz, 0.5f, aim) - 0.08f * recoil);
        rlRotatef(-22.0f * sprint, 1.0f, 0.0f, 0.0f);
        rlRotatef(-24.0f * recoil - 30.0f * reloading, 1.0f, 0.0f, 0.0f);
        rlRotatef(3.0f * sway * std::sin(walkPhase) + 2.0f * still * std::sin(time * 0.6f), 0.0f, 0.0f, 1.0f);
        rlRotatef(Lerp(pyaw, 0.0f, aim), 0.0f, 1.0f, 0.0f); // бэлэн үед ам нь хараа руу бага зэрэг дотогш
        rlRotatef(Lerp(proll, 0.0f, aim), 0.0f, 0.0f, 1.0f);
        rlRotatef(Lerp(7.0f, 0.0f, aim), 1.0f, 0.0f, 0.0f); // бэлэн үед ам нь бага зэрэг доош
        rlScalef(Lerp(ps, 0.85f, aim), Lerp(ps, 0.85f, aim), Lerp(ps, 0.85f, aim));
        AnchorViewBody({ -0.17f, -0.3f, -0.5f });
        DrawPistolUpright();
        // Баруун гар: бариулыг атгана (бариул дээш, бага зэрэг урагш хазайсан; бугуй доош хойш)
        rlPushMatrix();

        // (загварын гар: гулсуурын яг доор атгана; доороос атгахад бариулын дээд хэсэг гарын дээгүүр цухуйдаг байв)
        const Vector3 rightWrist = DrawFistAt({ 0.0f, viewModelArms ? 0.0f : -0.035f, -0.012f }, { 0.0f, 1.0f, 0.5f }, { -0.15f, -0.7f, -1.0f }, false);
        // Зүүн гар: баруун гарыг доор, зүүн талаас нь тэвэрнэ (толин тусгал)
        const Vector3 leftWrist = oneHand < 0.5f ? DrawFistAt({ 0.03f, -0.058f, 0.006f }, { -0.15f, 1.0f, 0.55f }, { 0.9f, -0.6f, -0.7f }, true)
                                                 : Vector3{ 0.0f, 0.0f, 0.0f };
        rlPopMatrix();
        // Шуунууд: бугуйнаас дэлгэцийн доод буланд (камерын ойрын хавтгайгаас цааш)
        ViewForearm({ -0.1f, -0.52f, -0.36f }, rightWrist);
        if (oneHand < 0.5f) ViewForearm({ 0.14f, -0.52f, -0.34f }, leftWrist);
    }
    else if (held == HeldItem::Knife || held == HeldItem::Grenade)
    {
        // Хутга, гранат: баруун гарт, хатгах / шидэх үед урагш түлхэнэ
        // Хутга: тулааны байрлалд нүүдэллэнэ; хатгахдаа эхлээд хойш далайгаад урагш шиднэ
        const float p = action > 0.0f ? 1.0f - action : -1.0f;
        const float windUp = p < 0.0f ? 0.0f : p < 0.28f ? p / 0.28f : std::max(0.0f, 1.0f - (p - 0.28f) / 0.14f);
        const float lunge = p < 0.28f ? 0.0f : p < 0.5f ? (p - 0.28f) / 0.22f : std::max(0.0f, 1.0f - (p - 0.55f) / 0.45f);
        const float thrust = held == HeldItem::Knife ? lunge : (action > 0.0f ? std::sin((1.0f - action) * PI) : 0.0f);
        const float cock = held == HeldItem::Knife ? windUp : 0.0f;
        // CS маягаар камерт ойр, дэлгэцийн баруун доор: хутганы ир урагш, зүүн тийш хөндлөн (бүтнээрээ харагдана),
        // гранат босоо (дээд таг, хөшүүрэг нь харагдана). Өмнө нь хол, жижиг байж хутганы зөвхөн бариулын үзүүр харагддаг байв.
        const bool knife = held == HeldItem::Knife;
        const bool grenadeModel = !knife && viewModelArms; // (бодит хэмжээтэй гар: гранат бүтэн далдлагдахгүй)
        const float bx = grenadeModel ? -0.15f : -0.12f, by = knife ? -0.14f : grenadeModel ? -0.17f : -0.13f, bz = grenadeModel ? 0.28f : 0.26f,
                    bs = knife ? 0.9f : 1.0f;
        const float byaw = knife ? 60.0f : -10.0f, bpitch = knife ? -30.0f : -50.0f, broll = knife ? -45.0f : 40.0f;
        if (!viewModelArms)
        {
            // Зүүн гар: сул, хагас нээлттэй, дэлгэцийн зүүн доод хэсэгт (шидэх, хатгахад бага зэрэг хойш).
            // (Загварын гар: Counter-Strike маягаар зөвхөн баруун гар харагдана)
            // Загварын гар том тул арай доош, зүүн тийш (харааг халхлахгүй).
            const float lx = viewModelArms ? 0.2f : 0.14f, ly = viewModelArms ? -0.24f : -0.18f, lz = viewModelArms ? 0.32f : 0.3f;
            const Vector3 lw = DrawRelaxedHandAt({ lx, ly - 0.02f * thrust, lz }, { -0.35f, 0.25f, 1.0f }, { 0.35f, -1.0f, -0.35f }, true);
            ViewForearm({ lx + 0.08f, ly - 0.3f, lz - 0.28f }, lw);
        }
        rlTranslatef(bx + 0.09f * thrust - 0.04f * cock + 0.02f * still * std::sin(time * 1.3f),
                     by + 0.07f * thrust + 0.07f * cock + 0.015f * still * std::sin(time * 2.1f),
                     bz + 0.22f * thrust - 0.1f * cock);
        if (held == HeldItem::Knife) rlRotatef(-20.0f * cock + 8.0f * still * std::sin(time * 1.1f), 0.0f, 0.0f, 1.0f);
        if (held == HeldItem::Knife)
        {
            // Ир нь урагш, бага зэрэг дотогш (хараа руу), дээш; хатгахад шулуун урагш
            rlRotatef(byaw - 10.0f * thrust, 0.0f, 1.0f, 0.0f);
            rlRotatef(bpitch - 22.0f * cock + 10.0f * thrust, 1.0f, 0.0f, 0.0f);
            rlRotatef(broll, 0.0f, 0.0f, 1.0f);
            rlScalef(bs, bs, bs);
            AnchorViewBody({ -0.12f, -0.38f, -0.48f });
            // Нударга: хутганы бариул Z тэнхлэгийн дагуу (эх нь бариулын төв), долоовор хуруу ир рүү, бугуй доош хойш
            const Vector3 wrist = DrawFistAt({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { -0.15f, -1.0f, -0.55f }, false);
            ViewForearm({ -0.12f, -0.36f, -0.24f }, wrist);
            // Хутга: DrawHandItem-ийн бариулыг (шууны -Y дагуу) эх рүү, ирийг +Z руу эргүүлнэ
            rlRotatef(-90.0f, 1.0f, 0.0f, 0.0f);
            rlTranslatef(0.0f, kForearm + 0.05f, 0.0f);
            HumanoidLook look;
            look.held = held;
            DrawHandItem(look);
        }
        else
        {
            rlRotatef(byaw, 0.0f, 1.0f, 0.0f);
            rlRotatef(bpitch, 1.0f, 0.0f, 0.0f);
            rlRotatef(broll, 0.0f, 0.0f, 1.0f);
            rlScalef(bs, bs, bs);
            AnchorViewBody({ -0.1f, -0.4f, -0.45f });
            // Гранатыг алган дээрээ атгана (их бие дээш, хуруунууд ороосон), шидэх үед гартай хамт урагш
            if (thrust < 0.6f)
            {
                rlPushMatrix();
                rlTranslatef(0.0f, 0.01f, 0.0f);
                ViewGrenade().DrawImmediate();
                rlPopMatrix();
            }
            // (загварын гар: гранатаас доош, хойш: гранат хурууны дээгүүр харагдана)
            const Vector3 hold = { 0.0f, 0.0f, 0.0f };
            const Vector3 wrist = DrawFistAt(hold, { 0.1f, 0.25f, 1.0f }, { -0.25f, -1.0f, -0.45f }, false, true);
            ViewForearm({ -0.1f, -0.36f, -0.22f }, wrist);
        }
    }
    rlPopMatrix();

    if (viewModelArms && viewBodySet && (viewTargets[0].active || viewTargets[1].active))
    {
        // Загварын гар: бие зэвсгийн орон зайд бэхлэгдсэн (AnchorViewBody). Тохой доош, гадагш (харах орон зайн -X = баруун).
        const Matrix basis = ViewBasis(camera);
        const Matrix bodyToWorld = viewBody;
        const Vector3 origin = Vector3Transform({ 0.0f, 0.0f, 0.0f }, basis);
        for (int side = 0; side < 2; ++side)
        {
            const Vector3 local = side == 0 ? Vector3{ -0.4f, -1.0f, 0.1f } : viewLeftPole; // (тохой голдуу доош: бугалга дэлгэцэнд орохгүй)
            viewTargets[side].pole = Vector3Normalize(Vector3Subtract(Vector3Transform(local, basis), origin));
        }
        DrawCharacterArms(viewArmsId, bodyToWorld, viewTargets, viewArmsTint);
    }
}

Vector3 ViewModelMuzzle(const Camera3D& camera, HeldItem held, float aim)
{
    const Matrix basis = ViewBasis(camera);
    aim = std::clamp(aim, 0.0f, 1.0f);
    // Бууны амны цэг: бэлэн үеийн байрлал (72%, DrawViewModel-ийн шилжилт, эргэлтийн дараа); онилоход (55%) төв рүү шилжинэ.
    Vector3 hip = LongGunHip(camera.fovy).muzzle, sight = { 0.0f, -0.075f, 0.83f }; // автомат, үрлэн буу, дурантай буу (дэлгэцээс хэмжсэн)
    if (held == HeldItem::Pistol) { hip = { -0.13f, -0.09f, 0.6f }; sight = { 0.0f, -0.04f, 0.54f }; }
    else if (held == HeldItem::Launcher) { hip = { -0.223f, -0.172f, 0.69f }; sight = { 0.0f, -0.08f, 0.93f }; }
    return Vector3Transform(Vector3Lerp(hip, sight, aim), basis);
}
