#include "Humanoid.h"

#include "CharacterModel.h"
#include "VehicleModel.h"
#include "raymath.h"

#include "rlgl.h"

#include <algorithm>
#include <cmath>

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
    DrawCylinder({ ground.x + 0.18f, ground.y + 0.13f, ground.z + 0.13f }, radius, radius, 0.01f, 8,
                 { 10, 12, 22, 90 });
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

    const Color kSleeve = { 82, 96, 62, 255 };   // дүрэмт хувцасны ханцуй
    const Color kGlove = { 38, 36, 32, 255 };    // бээлий

    // Ханцуйтай шуу ба бээлийтэй гар: from (тохой, дэлгэцийн гадна) -> hand (бариул).
    // Шуу: тохойноос (дэлгэцийн доод ирмэгийн гадна) бугуй хүртэл ханцуй, бугуйнд бээлийн ам.
    void ViewForearm(Vector3 elbow, Vector3 wrist)
    {
        DrawCylinderEx(elbow, wrist, 0.042f, 0.031f, 10, kSleeve);
        const Vector3 cuff = Vector3Lerp(elbow, wrist, 0.86f);
        DrawCylinderEx(cuff, wrist, 0.035f, 0.033f, 10, Shade(kSleeve, 0.7f));
        DrawCylinderEx(wrist, Vector3Lerp(elbow, wrist, 1.08f), 0.031f, 0.03f, 10, kGlove);
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

    rlPushMatrix();
    rlMultMatrixf(MatrixToFloat(ViewBasis(camera)));
    rlTranslatef(bobX, bobY - 0.12f * reloading, 0.0f);

    const bool longGun = held == HeldItem::Rifle || held == HeldItem::Shotgun || held == HeldItem::Sniper ||
                         held == HeldItem::Launcher;
    if (longGun)
    {
        // Буу дэлгэцийн баруун доор, гол төмөр нь хараа руу бага зэрэг чиглэнэ (Counter-Strike маягийн).
        // Жинхэнэ хэмжээгээр (1.1 м) зурвал дэлгэцийг дүүргэнэ: 48% болгож, дэлгэцийн баруун доод хэсэгт
        // (зэвсгийн үүрнүүд ба мини газрын зургийн хооронд) байрлуулна. Бөгс нь дэлгэцийн гадна үлдэнэ.
        // Онилоход (aim) гол төмөр нь дэлгэцийн төвд, хараан доор ирнэ (x = 0); ар тал нь харааг халхлахгүй
        // байхаар бага зэрэг урагш, доош байрлана.
        const bool launcher = held == HeldItem::Launcher;
        const float scale = Lerp(0.48f, 0.55f, aim);
        rlTranslatef(Lerp(-0.11f, 0.132f, aim), Lerp(launcher ? -0.32f : -0.375f, launcher ? -0.25f : -0.25f, aim),
                     Lerp(0.13f, 0.24f, aim) - 0.04f * recoil);
        rlRotatef(3.0f * (1.0f - aim), 0.0f, 1.0f, 0.0f);
        rlScalef(scale, scale, scale);
        rlRotatef(-5.0f * recoil - 18.0f * reloading, 1.0f, 0.0f, 0.0f);
        rlRotatef(-25.0f * reloading, 0.0f, 0.0f, 1.0f);
        if (held == HeldItem::Rifle) DrawGun(0.0f, true);
        else if (held == HeldItem::Shotgun) DrawShotgun(0.0f, true);
        else if (held == HeldItem::Sniper) DrawSniper(0.0f, true);
        else DrawLauncher(0.0f, true);

        // Баруун гар бариулд, зүүн гар урд модонд (бууны орон зайд, DrawGun-ийн хэмжээсээр)
        const float x = kHumanoidMuzzle.x, y = kHumanoidMuzzle.y - kHipHeight;
        if (launcher)
        {
            ViewArm({ x - 0.18f, y - 0.45f, 0.18f }, { x, y - 0.14f, 0.42f });
            ViewArm({ x + 0.3f, y - 0.45f, 0.45f }, { x + 0.02f, y - 0.14f, 0.66f });
        }
        else
        {
            ViewArm({ x - 0.16f, y - 0.42f, 0.02f }, { x, y - 0.17f, 0.3f });
            ViewArm({ x + 0.3f, y - 0.42f, 0.5f - 0.15f * reloading }, { x + 0.02f, y - 0.07f, 0.74f - 0.2f * reloading });
        }
    }
    else if (held == HeldItem::Pistol)
    {
        // Гар буу: хоёр гараар урагш сунган барина
        // Буудахад ам нь дээш үсэрч гар хойш түлхэгдэнэ; алхахад, тайван үед бага зэрэг хазайна
        rlTranslatef(Lerp(-0.15f, 0.0f, aim), Lerp(-0.2f, -0.128f, aim) + 0.03f * recoil, 0.54f - 0.04f * aim - 0.08f * recoil);
        rlRotatef(-24.0f * recoil - 30.0f * reloading, 1.0f, 0.0f, 0.0f);
        rlRotatef(3.0f * sway * std::sin(walkPhase) + 2.0f * still * std::sin(time * 0.6f), 0.0f, 0.0f, 1.0f);
        rlRotatef(Lerp(4.0f, 0.0f, aim), 0.0f, 1.0f, 0.0f); // бэлэн үед ам нь хараа руу бага зэрэг дотогш
        rlRotatef(Lerp(7.0f, 0.0f, aim), 1.0f, 0.0f, 0.0f); // бэлэн үед ам нь бага зэрэг доош
        rlScalef(0.85f, 0.85f, 0.85f);
        DrawPistolUpright();
        // Баруун гар: бариулыг атгасан нударга, долоовор гохон дээр, эрхий хуруу зүүн талаар
        GlovePart(-0.03f, -0.015f, -0.03f, 0.026f, 0.09f, 0.06f);                  // алга (баруун талд)
        for (int i = 0; i < 3; ++i)
        {
            GlovePart(-0.004f, -0.045f + 0.024f * (float)i, 0.014f, 0.05f, 0.021f, 0.026f); // бариулын урдуур ороосон хуруу
        }
        GlovePart(-0.012f, 0.03f, 0.045f, 0.016f, 0.015f, 0.05f);                 // долоовор: гохны хамгаалалт дотор
        GlovePart(0.022f, 0.034f, 0.0f, 0.015f, 0.016f, 0.055f);                  // эрхий хуруу
        // Зүүн гар: баруун гарыг доороос нь тэвэрч, эрхий хуруу нь замагны дагуу урагш
        GlovePart(0.034f, -0.035f, -0.01f, 0.024f, 0.08f, 0.06f);
        for (int i = 0; i < 3; ++i)
        {
            GlovePart(0.0f, -0.052f + 0.022f * (float)i, 0.038f, 0.07f, 0.02f, 0.024f);
        }
        GlovePart(0.032f, 0.04f, 0.05f, 0.014f, 0.015f, 0.065f);
        // Шуунууд: бугуйнаас дэлгэцийн доод буланд (камерын ойрын хавтгайгаас цааш)
        ViewForearm({ -0.14f, -0.34f, -0.1f }, { -0.035f, -0.065f, -0.07f });
        ViewForearm({ 0.17f, -0.34f, -0.08f }, { 0.04f, -0.075f, -0.05f });
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
        rlTranslatef(-0.16f + 0.09f * thrust - 0.04f * cock + 0.02f * still * std::sin(time * 1.3f),
                     -0.17f + 0.07f * thrust + 0.07f * cock + 0.015f * still * std::sin(time * 2.1f),
                     0.44f + 0.22f * thrust - 0.1f * cock);
        if (held == HeldItem::Knife) rlRotatef(-20.0f * cock + 8.0f * still * std::sin(time * 1.1f), 0.0f, 0.0f, 1.0f);
        if (held == HeldItem::Knife)
        {
            // Ир нь урагш, бага зэрэг дотогш (хараа руу), дээш; хатгахад шулуун урагш
            rlRotatef(14.0f - 10.0f * thrust, 0.0f, 1.0f, 0.0f);
            rlRotatef(-8.0f - 22.0f * cock + 10.0f * thrust, 1.0f, 0.0f, 0.0f);
            rlRotatef(-18.0f, 0.0f, 0.0f, 1.0f);
            rlScalef(0.9f, 0.9f, 0.9f);
            // Нударга: бариулыг доороос нь атгана (хутганы бариул Z тэнхлэгийн дагуу, эх нь бариулын төв)
            GlovePart(-0.028f, -0.004f, -0.004f, 0.026f, 0.07f, 0.1f);           // алга
            for (int i = 0; i < 4; ++i)
            {
                GlovePart(0.004f, -0.03f, -0.036f + 0.024f * (float)i, 0.05f, 0.024f, 0.021f); // ороосон хуруу
            }
            GlovePart(0.018f, 0.028f, 0.018f, 0.016f, 0.016f, 0.05f);            // эрхий хуруу
            ViewForearm({ -0.1f, -0.3f, -0.26f }, { -0.025f, -0.035f, -0.07f });
            // Хутга: DrawHandItem-ийн бариулыг (шууны -Y дагуу) эх рүү, ирийг +Z руу эргүүлнэ
            rlRotatef(-90.0f, 1.0f, 0.0f, 0.0f);
            rlTranslatef(0.0f, kForearm + 0.05f, 0.0f);
            HumanoidLook look;
            look.held = held;
            DrawHandItem(look);
        }
        else
        {
            rlScalef(0.8f, 0.8f, 0.8f);
            ViewForearm({ -0.08f, -0.3f, -0.2f }, { 0.0f, -0.03f, -0.02f });
            DrawSphereEx({ 0.0f, 0.0f, 0.0f }, 0.045f, 5, 8, kGlove);
            rlRotatef(-90.0f + 20.0f * thrust, 1.0f, 0.0f, 0.0f); // гарын тэнхлэг (-Y) урагш
            rlTranslatef(0.0f, kForearm + 0.05f, 0.0f);
            HumanoidLook look;
            look.held = held;
            DrawHandItem(look);
        }
    }
    rlPopMatrix();
}

Vector3 ViewModelMuzzle(const Camera3D& camera, HeldItem held, float aim)
{
    const Matrix basis = ViewBasis(camera);
    aim = std::clamp(aim, 0.0f, 1.0f);
    // Бууны амны цэг: 48%-ийн хэмжээ ба шилжилтийн дараа (DrawViewModel); онилоход (55%) төв рүү шилжинэ.
    Vector3 hip = { -0.224f, -0.222f, 0.64f }, sight = { 0.0f, -0.075f, 0.83f }; // автомат, үрлэн буу, дурантай буу
    if (held == HeldItem::Pistol) { hip = { -0.13f, -0.09f, 0.6f }; sight = { 0.0f, -0.04f, 0.54f }; }
    else if (held == HeldItem::Launcher) { hip = { -0.223f, -0.172f, 0.69f }; sight = { 0.0f, -0.08f, 0.93f }; }
    return Vector3Transform(Vector3Lerp(hip, sight, aim), basis);
}
