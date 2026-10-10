#pragma once

#include "raylib.h"

#include <cstdint>

// Ертөнцийг эхлэх бүрд яг ижил үүсгэхийн тулд өөрийн санамсаргүй тоо үүсгэгч.
struct Rng
{
    uint32_t state;

    explicit Rng(uint32_t seed) : state(seed ? seed : 1u) {}

    uint32_t NextU();
    float Next();                     // [0, 1)
    float Range(float a, float b);    // [a, b)
    int Int(int a, int b);            // [a, b]
    bool Chance(float probability);
};

// Гөлгөр дуу чимээ (noise), утга нь [0, 1].
float ValueNoise(float x, float z);
float Fbm(float x, float z, int octaves);

float SmoothStep(float edge0, float edge1, float x);

// Өнгөний туслахууд
Color MixColor(Color a, Color b, float t);
Color ScaleColor(Color color, float factor);     // гэрэлтүүлэх/бараантуулах
Color JitterColor(Color color, Rng& rng, float amount); // +-amount хувиар санамсаргүй өөрчилнө
