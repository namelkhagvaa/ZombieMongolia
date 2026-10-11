#pragma once

#include "raylib.h"

// Орчны уур амьсгал: тэнгэр, нар, нарны чиглэлтэй гэрэлтүүлэг, зайн манан, алсын уулс.
//
// Гэрэлтүүлэг ба мананг нэг хөнгөн shader хийдэг. Гадаргуугийн чиглэлийг (normal) shader нь
// пиксел бүрд байрлалын зөрүүгээс тооцдог тул DrawCube зэрэг энгийн дүрс болон өөрсдийн
// үүсгэсэн mesh бүгд ижилхэн, low-poly маягийн хавтгай гэрэлтүүлэгтэй болно.
class Environment
{
public:
    // Цонх нээгдсэний дараа үүсгэнэ.
    Environment();
    ~Environment();

    Environment(const Environment&) = delete;
    Environment& operator=(const Environment&) = delete;

    // 2D: BeginMode3D-ээс өмнө дуудна. Тэнгэрийн өнгөний шилжилт ба нар.
    void DrawSky(const Camera3D& camera) const;

    // BeginMode3D дотор: Begin/End хоёрын хооронд зурсан бүх зүйл гэрэлтэж, мананд уусна.
    void Begin(const Camera3D& camera) const;
    void End() const;

    // Алсын уулс: камерыг дагаж шилждэг дэвсгэр.
    void DrawMountains(Vector3 viewPosition) const;

    // Mesh зурахад ашиглах material (орчны shader-тэй).
    const Material& GetMaterial() const { return material; }

    // Гаднаас оруулсан ястай загваруудын shader: текстур + жинхэнэ normal-аар гөлгөр гэрэлтүүлэг + манан.
    Shader CharacterShader() const { return characterShader; }

private:
    Shader shader = {};
    Shader characterShader = {};
    int characterViewPosLocation = -1;
    Material material = {};
    Mesh mountains = {};
    int viewPosLocation = -1;
    int offsetLocation = -1;
};
