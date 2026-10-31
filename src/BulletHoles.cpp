#include "BulletHoles.h"

#include "raymath.h"
#include "rlgl.h"

#include <cmath>
#include <cstdint>

namespace
{
    const size_t kMaxHoles = 600;
    const float kDrawDistance = 90.0f;

    float Hash(int x, int y)
    {
        uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return (float)((h ^ (h >> 16)) & 0xffff) / 65535.0f;
    }

    // 64 x 64: голдоо хар нүх, эргэн тойронд нь цайвар (өнгөөр будна: хана тоосорхог, төмөр гялтгар) хагархай
    // хүрээ, гадна нь бүдэг тортог. RGB нь гэрэлтэлт (оройн өнгөөр үржинэ), A нь тунгалаг.
    Texture2D MakeTexture()
    {
        const int size = 64;
        Image image = GenImageColor(size, size, { 0, 0, 0, 0 });
        Color* pixels = (Color*)image.data;
        for (int y = 0; y < size; ++y)
        {
            for (int x = 0; x < size; ++x)
            {
                const float dx = ((float)x + 0.5f) / size * 2.0f - 1.0f, dy = ((float)y + 0.5f) / size * 2.0f - 1.0f;
                const float angle = std::atan2(dy, dx);
                // Ирмэг нь жигд биш: өнцгөөр ба цэг бүрээр санамсаргүй
                const float jag = 1.0f + 0.07f * std::sin(angle * 5.0f + 1.3f) + 0.06f * std::sin(angle * 11.0f) +
                                  0.12f * (Hash(x, y) - 0.5f);
                const float r = std::sqrt(dx * dx + dy * dy) / jag;
                float lum = 0.0f, alpha = 0.0f;
                if (r < 0.26f)
                {
                    lum = 0.06f; // нүх
                    alpha = 1.0f;
                }
                else if (r < 0.58f)
                {
                    // Цууралт: гаднаа бүдгэрнэ, ширхэгтэй
                    const float t = (r - 0.26f) / 0.32f;
                    lum = 0.95f - 0.35f * t + 0.15f * (Hash(x + 17, y + 3) - 0.5f);
                    alpha = 0.95f - 0.55f * t * t;
                }
                else if (r < 1.0f)
                {
                    // Тортог, тоос
                    const float t = (r - 0.58f) / 0.42f;
                    lum = 0.32f;
                    alpha = 0.32f * (1.0f - t) * (0.6f + 0.8f * Hash(x + 5, y + 29));
                }
                const unsigned char v = (unsigned char)(std::fmin(1.0f, std::fmax(0.0f, lum)) * 255.0f);
                pixels[y * size + x] = { v, v, v, (unsigned char)(std::fmin(1.0f, alpha) * 255.0f) };
            }
        }
        Texture2D texture = LoadTextureFromImage(image);
        UnloadImage(image);
        GenTextureMipmaps(&texture);
        SetTextureWrap(texture, TEXTURE_WRAP_CLAMP);
        SetTextureFilter(texture, TEXTURE_FILTER_TRILINEAR);
        return texture;
    }
}

BulletHole BulletHoles::Make(Vector3 position, Vector3 normal, bool metal)
{
    BulletHole hole;
    hole.position = position;
    hole.normal = Vector3Normalize(normal);
    hole.size = (metal ? 0.1f : 0.16f) * (0.8f + 0.4f * (float)GetRandomValue(0, 100) / 100.0f);
    hole.spin = (float)GetRandomValue(0, 628) / 100.0f;
    hole.metal = metal;
    return hole;
}

void BulletHoles::Add(Vector3 position, Vector3 normal, bool metal)
{
    const BulletHole hole = Make(position, normal, metal);
    if (holes.size() < kMaxHoles)
    {
        holes.push_back(hole);
        return;
    }
    holes[next] = hole;
    next = (next + 1) % kMaxHoles;
}

void BulletHoles::Draw(const Camera3D& camera, Color light, const std::vector<BulletHole>& moving) const
{
    if ((holes.empty() && moving.empty()) || texture.id == 0) return;

    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    auto quad = [&](const BulletHole& hole) {
        if (Vector3Distance(hole.position, camera.position) > kDrawDistance) return;
        const Vector3 toCamera = Vector3Subtract(camera.position, hole.position);
        if (Vector3DotProduct(toCamera, hole.normal) <= 0.0f) return; // ард талаас
        // Гадаргуугаас бага зэрэг гадагш, камер руу (хананы нарийн зөрүүд шингэхгүй)
        const Vector3 lift = Vector3Add(Vector3Scale(hole.normal, 0.012f), Vector3Scale(Vector3Normalize(toCamera), 0.05f));
        const Vector3 center = Vector3Add(hole.position, lift);
        const Vector3 helper = std::fabs(hole.normal.y) < 0.9f ? Vector3{ 0.0f, 1.0f, 0.0f } : Vector3{ 1.0f, 0.0f, 0.0f };
        const Vector3 u = Vector3Normalize(Vector3CrossProduct(helper, hole.normal));
        const Vector3 v = Vector3CrossProduct(hole.normal, u);
        const float c = std::cos(hole.spin), s = std::sin(hole.spin), h = hole.size * 0.5f;
        const Vector3 a = Vector3Scale(Vector3Add(Vector3Scale(u, c), Vector3Scale(v, s)), h);
        const Vector3 b = Vector3Scale(Vector3Subtract(Vector3Scale(v, c), Vector3Scale(u, s)), h);
        // Хана: цайвар тоосорхог; төмөр: будаг цуурч гялтганасан
        const Color base = hole.metal ? Color{ 214, 216, 222, 255 } : Color{ 196, 188, 176, 255 };
        const Vector3 p0 = Vector3Subtract(Vector3Subtract(center, a), b), p1 = Vector3Subtract(Vector3Add(center, a), b);
        const Vector3 p2 = Vector3Add(Vector3Add(center, a), b), p3 = Vector3Add(Vector3Subtract(center, a), b);
        // (raylib DrawBillboardPro-гийн дарааллаар: дөрвөлжин бүрд текстур, begin/end; нэг том begin дотор
        // текстур нь өмнөх зурлагынхаар үлдэж байсан)
        rlSetTexture(texture.id);
        rlBegin(RL_QUADS);
        rlColor4ub((unsigned char)(base.r * light.r / 255), (unsigned char)(base.g * light.g / 255),
                   (unsigned char)(base.b * light.b / 255), 255);
        rlTexCoord2f(0.0f, 0.0f); rlVertex3f(p0.x, p0.y, p0.z);
        rlTexCoord2f(1.0f, 0.0f); rlVertex3f(p1.x, p1.y, p1.z);
        rlTexCoord2f(1.0f, 1.0f); rlVertex3f(p2.x, p2.y, p2.z);
        rlTexCoord2f(0.0f, 1.0f); rlVertex3f(p3.x, p3.y, p3.z);
        rlEnd();
        rlSetTexture(0);
    };
    for (const BulletHole& hole : holes) quad(hole);
    for (const BulletHole& hole : moving) quad(hole);
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
}

void BulletHoles::Load()
{
    if (texture.id == 0) texture = MakeTexture();
}

void BulletHoles::Unload()
{
    if (texture.id != 0) UnloadTexture(texture);
    texture = {};
}
