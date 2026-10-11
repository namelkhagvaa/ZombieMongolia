#include "Humanoid.h"

#include "CharacterModel.h"
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

    // ------------------------------------------------------------------ Гар

    // Баруун гарт барьсан жижиг зэвсэг. Шууны орон зайд зурна: эх нь тохой, шууны тэнхлэг (-Y) нь
    // зэвсгийн чиглэл, гар урагш сунгасан үед +Z нь дээш харна.
    void DrawHandItem(const HumanoidLook& look)
    {
        const float grip = -kForearm - 0.05f;
        if (look.held == HeldItem::Knife)
        {
            Cube(0.0f, grip, 0.01f, 0.035f, 0.13f, 0.04f, { 40, 30, 24, 255 });              // иш
            Cube(0.0f, grip - 0.07f, 0.01f, 0.03f, 0.02f, 0.09f, kGunMetal);                 // хамгаалалт
            Tapered(0.0f, grip - 0.3f, 0.01f, 0.006f, 0.012f, 0.014f, 0.05f, 0.23f, { 196, 200, 206, 255 }); // ир
        }
        else if (look.held == HeldItem::Pistol)
        {
            Cube(0.0f, grip, 0.0f, 0.04f, 0.12f, 0.06f, kGun);                               // бариул
            Cube(0.0f, grip - 0.1f, 0.055f, 0.045f, 0.24f, 0.06f, kGunMetal);                // их бие, замаг
            Cube(0.0f, grip - 0.07f, 0.09f, 0.03f, 0.2f, 0.012f, kGun);
            Cube(0.0f, grip - 0.06f, 0.0f, 0.02f, 0.06f, 0.02f, kGun);                       // гох хамгаалалт
        }
        else if (look.held == HeldItem::Grenade)
        {
            DrawSphereEx({ 0.0f, grip - 0.03f, 0.03f }, 0.06f, 4, 6, { 62, 78, 48, 255 });
            Cube(0.0f, grip - 0.03f, 0.1f, 0.03f, 0.03f, 0.04f, kGunMetal);                  // гал хамгаалагч
        }
    }

    // Босоо бариултай гар буу (оруулсан загварын гарт): эх нь бариулын төв, +Z нь ам, +Y нь дээш.
    void DrawPistolUpright()
    {
        Cube(0.0f, 0.085f, 0.055f, 0.034f, 0.05f, 0.21f, kGunMetal);          // замаг
        Cube(0.0f, 0.058f, 0.06f, 0.03f, 0.022f, 0.17f, kGun);                // их бие
        Tapered(0.0f, -0.07f, -0.02f, 0.03f, 0.05f, 0.032f, 0.055f, 0.13f, kGun, 0.025f); // бариул (хойш налуу)
        Cube(0.0f, 0.03f, 0.055f, 0.012f, 0.012f, 0.07f, kGun);               // гох хамгаалалт
        Cube(0.0f, 0.115f, 0.15f, 0.008f, 0.012f, 0.012f, kGun);              // урд хараа
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
            DrawHandItem(look);
        }
        rlPopMatrix();
    }

    // Үрлэн буу: урт гол төмөр, доор нь гүйдэг гар, модон бөгс. Сумны хайрцаггүй.
    void DrawShotgun(float kick, bool detailed)
    {
        const float x = kHumanoidMuzzle.x;
        const float y = kHumanoidMuzzle.y - kHipHeight;
        const float z = -kick;

        Tapered(x, y - 0.12f, z + 0.08f, 0.06f, 0.24f, 0.06f, 0.32f, 0.15f, kGunWood, 0.03f); // бөгс
        Cube(x, y, z + 0.42f, 0.08f, 0.12f, 0.34f, kGunMetal);                                 // их бие
        Cube(x, y + 0.03f, z + 0.88f, 0.05f, 0.05f, 0.62f, kGun);                              // гол төмөр
        Cube(x, y - 0.035f, z + 0.82f, 0.045f, 0.045f, 0.48f, kGun);                           // сумны хоолой
        Cube(x, y - 0.04f, z + 0.74f, 0.085f, 0.085f, 0.2f, kGunWood);                         // гүйдэг гар
        if (detailed)
        {
            Cube(x, y - 0.1f, z + 0.36f, 0.02f, 0.07f, 0.1f, kGun);          // гох хамгаалалт
            Cube(x, y + 0.065f, z + 1.17f, 0.014f, 0.025f, 0.02f, kGunWood); // хараа
        }
    }

    // ------------------------------------------------------------------ Буу

    void DrawGun(float kick, bool detailed)
    {
        // Автомат буу. Их биеийн орон зайд (ташаанаас дээш хэмжсэн өндөр), урагшаа +Z.
        const float x = kHumanoidMuzzle.x;
        const float y = kHumanoidMuzzle.y - kHipHeight;
        const float z = -kick;

        Tapered(x, y - 0.1f, z + 0.1f, 0.06f, 0.26f, 0.06f, 0.3f, 0.13f, kGunWood, 0.02f); // бөгс
        Cube(x, y, z + 0.46f, 0.085f, 0.13f, 0.42f, kGun);                                    // их бие
        Cube(x, y + 0.075f, z + 0.42f, 0.05f, 0.03f, 0.3f, kGunMetal);                        // таг
        Tapered(x, y - 0.22f, z + 0.3f, 0.055f, 0.08f, 0.06f, 0.09f, 0.16f, kGunWood, 0.04f); // бариул
        // Муруй сумны хайрцаг: хоёр хэсэг урагшаа хазайсан
        Tapered(x, y - 0.2f, z + 0.52f, 0.06f, 0.1f, 0.06f, 0.11f, 0.14f, kGunMetal, -0.03f);
        Tapered(x, y - 0.32f, z + 0.57f, 0.058f, 0.09f, 0.06f, 0.1f, 0.12f, kGunMetal, -0.05f);
        Cube(x, y - 0.005f, z + 0.76f, 0.095f, 0.1f, 0.22f, kGunWood);                        // гар хамгаалалт
        Cube(x, y + 0.02f, z + 0.95f, 0.04f, 0.04f, 0.2f, kGunMetal);                         // гол төмөр
        if (detailed)
        {
            Cube(x, y + 0.02f, z + 1.03f, 0.055f, 0.055f, 0.05f, kGun);        // амны тоормос
            Cube(x, y + 0.075f, z + 0.98f, 0.016f, 0.07f, 0.02f, kGun);        // урд хараа
            Cube(x, y + 0.095f, z + 0.3f, 0.03f, 0.03f, 0.04f, kGun);          // хойд хараа
            Cube(x, y - 0.075f, z + 0.82f, 0.03f, 0.03f, 0.3f, kGunMetal);     // хийн хоолой
        }
    }

    // Танк эсэргүүцэгч пуужин харвагч: мөрөн дээр хэвтэх урт хоолой, урд үзүүрт нь шовх пуужин,
    // доор нь хоёр бариул, ард нь өргөссөн яндан. Хоолойн тэнхлэг бууны шугамаас арай дээр.
    void DrawLauncher(float kick, bool detailed)
    {
        const float x = kHumanoidMuzzle.x;
        const float y = kHumanoidMuzzle.y - kHipHeight;
        const float z = -kick;
        const Color tube = { 78, 88, 62, 255 };
        const Color wood = { 120, 84, 50, 255 };
        const Color warhead = { 96, 106, 74, 255 };

        Cube(x, y, z + 0.3f, 0.09f, 0.09f, 1.15f, tube);                                  // хоолой
        Cube(x, y, z + 0.34f, 0.105f, 0.105f, 0.3f, wood);                                // модон дулаан хамгаалалт
        Tapered(x, y - 0.075f, z - 0.36f, 0.15f, 0.15f, 0.09f, 0.09f, 0.15f, tube);       // арын өргөссөн яндан
        Cube(x, y - 0.14f, z + 0.5f, 0.04f, 0.17f, 0.05f, kGun);                          // гохны бариул
        Cube(x, y - 0.14f, z + 0.72f, 0.04f, 0.17f, 0.05f, wood);                         // урд бариул
        // Пуужингийн толгой: бүдүүн их бие, шовх хошуу
        Cube(x, y, z + 1.0f, 0.13f, 0.13f, 0.26f, warhead);
        Tapered(x - 0.0f, y - 0.065f, z + 1.2f, 0.13f, 0.14f, 0.03f, 0.03f, 0.13f, warhead);
        if (detailed)
        {
            Cube(x, y + 0.09f, z + 0.42f, 0.03f, 0.09f, 0.12f, kGun);                     // хараа
            Cube(x, y + 0.075f, z + 0.82f, 0.016f, 0.06f, 0.02f, kGun);                   // урд хараа
        }
    }

    // Мэргэн буудагчийн буу: модон бөгс, урт гол төмөр, дээрээ дуран хараатай.
    void DrawSniper(float kick, bool detailed)
    {
        const float x = kHumanoidMuzzle.x;
        const float y = kHumanoidMuzzle.y - kHipHeight;
        const float z = -kick;

        Tapered(x, y - 0.1f, z + 0.1f, 0.06f, 0.26f, 0.06f, 0.3f, 0.14f, kGunWood, 0.02f); // бөгс
        Cube(x, y - 0.01f, z + 0.5f, 0.075f, 0.11f, 0.5f, kGunWood);                          // их бие
        Cube(x, y + 0.04f, z + 0.5f, 0.05f, 0.04f, 0.42f, kGunMetal);                         // замаг
        Cube(x, y + 0.03f, z + 1.05f, 0.035f, 0.035f, 0.66f, kGun);                           // урт гол төмөр
        Cube(x, y - 0.12f, z + 0.52f, 0.05f, 0.1f, 0.1f, kGunMetal);                          // сумны хайрцаг
        // Дуран хараа: хоёр суурь дээрх хоолой, хоёр үзүүр нь бүдүүн
        Cube(x, y + 0.13f, z + 0.5f, 0.05f, 0.05f, 0.36f, kGun);
        Cube(x, y + 0.13f, z + 0.3f, 0.066f, 0.066f, 0.07f, kGun);
        Cube(x, y + 0.13f, z + 0.7f, 0.074f, 0.074f, 0.09f, kGun);
        if (detailed)
        {
            Cube(x, y + 0.085f, z + 0.4f, 0.025f, 0.05f, 0.03f, kGunMetal);  // хараагийн суурь
            Cube(x, y + 0.085f, z + 0.6f, 0.025f, 0.05f, 0.03f, kGunMetal);
            Cube(x, y + 0.13f, z + 0.746f, 0.05f, 0.05f, 0.004f, { 70, 110, 150, 255 }); // линз
            Cube(x, y - 0.1f, z + 0.36f, 0.02f, 0.07f, 0.1f, kGun);          // гох хамгаалалт
            Cube(x, y + 0.03f, z + 1.36f, 0.05f, 0.05f, 0.06f, kGun);        // амны тоормос
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
                                                          look.kind == HumanoidKind::Zombie, look.modelTint);

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
            if (look.held == HeldItem::Pistol) DrawPistolUpright();
            else DrawHandItem(look);
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
