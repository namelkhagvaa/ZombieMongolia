#include "Procedural.h"

#include <algorithm>
#include <cmath>

uint32_t Rng::NextU()
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

float Rng::Next()
{
    return (float)(NextU() & 0xFFFFFFu) / 16777216.0f;
}

float Rng::Range(float a, float b)
{
    return a + (b - a) * Next();
}

int Rng::Int(int a, int b)
{
    return a + (int)(NextU() % (uint32_t)(b - a + 1));
}

bool Rng::Chance(float probability)
{
    return Next() < probability;
}

namespace
{
    float Hash(int ix, int iz)
    {
        uint32_t h = (uint32_t)ix * 374761393u + (uint32_t)iz * 668265263u;
        h = (h ^ (h >> 13)) * 1274126177u;
        h ^= h >> 16;
        return (float)(h & 0xFFFFFFu) / 16777215.0f;
    }

    unsigned char ToByte(float value)
    {
        return (unsigned char)std::clamp(value, 0.0f, 255.0f);
    }
}

float SmoothStep(float edge0, float edge1, float x)
{
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float ValueNoise(float x, float z)
{
    const float fx = std::floor(x);
    const float fz = std::floor(z);
    const int ix = (int)fx;
    const int iz = (int)fz;
    const float tx = SmoothStep(0.0f, 1.0f, x - fx);
    const float tz = SmoothStep(0.0f, 1.0f, z - fz);

    const float a = Hash(ix, iz);
    const float b = Hash(ix + 1, iz);
    const float c = Hash(ix, iz + 1);
    const float d = Hash(ix + 1, iz + 1);
    return (a + (b - a) * tx) * (1.0f - tz) + (c + (d - c) * tx) * tz;
}

float Fbm(float x, float z, int octaves)
{
    float sum = 0.0f;
    float amplitude = 0.5f;
    float total = 0.0f;
    for (int i = 0; i < octaves; ++i)
    {
        sum += ValueNoise(x, z) * amplitude;
        total += amplitude;
        x = x * 2.03f + 17.1f;
        z = z * 2.03f + 9.7f;
        amplitude *= 0.5f;
    }
    return sum / total;
}

Color MixColor(Color a, Color b, float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return {
        ToByte((float)a.r + ((float)b.r - (float)a.r) * t),
        ToByte((float)a.g + ((float)b.g - (float)a.g) * t),
        ToByte((float)a.b + ((float)b.b - (float)a.b) * t),
        ToByte((float)a.a + ((float)b.a - (float)a.a) * t),
    };
}

Color ScaleColor(Color color, float factor)
{
    return { ToByte((float)color.r * factor), ToByte((float)color.g * factor), ToByte((float)color.b * factor),
             color.a };
}

Color JitterColor(Color color, Rng& rng, float amount)
{
    return ScaleColor(color, 1.0f + rng.Range(-amount, amount));
}
