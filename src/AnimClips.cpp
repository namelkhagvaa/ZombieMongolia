#include "AnimClips.h"

#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace
{
    struct Reader
    {
        const unsigned char* data;
        int size;
        int at = 0;
        bool ok = true;

        bool Take(void* out, int count)
        {
            if (!ok || at + count > size) return ok = false;
            std::memcpy(out, data + at, (size_t)count);
            at += count;
            return true;
        }
        uint32_t U32() { uint32_t v = 0; Take(&v, 4); return v; }
        float F32() { float v = 0.0f; Take(&v, 4); return v; }
        uint8_t U8() { uint8_t v = 0; Take(&v, 1); return v; }
        std::string Name()
        {
            const int length = U8();
            std::string s((size_t)length, '\0');
            if (length > 0) Take(&s[0], length);
            return s;
        }
    };

    float Dot(Quaternion a, Quaternion b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
}

int AnimSet::Find(const char* name) const
{
    for (size_t i = 0; i < clips.size(); ++i)
    {
        if (clips[i].name == name) return (int)i;
    }
    return -1;
}

bool LoadAnimSet(const char* path, AnimSet& out)
{
    int size = 0;
    unsigned char* data = LoadFileData(path, &size);
    if (!data) return false;
    Reader r{ data, size };
    char magic[4] = {};
    r.Take(magic, 4);
    const uint32_t version = r.U32();
    if (std::memcmp(magic, "ZMAN", 4) != 0 || version != 3)
    {
        UnloadFileData(data);
        TraceLog(LOG_WARNING, "ANIM: %s: unknown format", path);
        return false;
    }
    const uint32_t boneCount = r.U32();
    out.bones.clear();
    out.rest.clear();
    for (uint32_t i = 0; i < boneCount && r.ok; ++i)
    {
        out.bones.push_back(r.Name());
        Vector3 p;
        p.x = r.F32();
        p.y = r.F32();
        p.z = r.F32();
        out.rest.push_back(p);
    }
    const uint32_t clipCount = r.U32();
    out.clips.clear();
    for (uint32_t c = 0; c < clipCount && r.ok; ++c)
    {
        AnimClip clip;
        clip.name = r.Name();
        clip.frames = (int)r.U32();
        clip.fps = r.F32();
        clip.loop = r.U8() != 0;
        clip.velocity.x = r.F32();
        clip.velocity.y = r.F32();
        clip.velocity.z = r.F32();
        clip.phase = r.F32();
        if (clip.frames < 2 || clip.frames > 100000 || clip.fps <= 0.0f) { r.ok = false; break; }
        clip.hips.resize((size_t)clip.frames);
        clip.rotations.resize((size_t)clip.frames * boneCount);
        std::vector<int16_t> packed(boneCount * 4);
        for (int f = 0; f < clip.frames && r.ok; ++f)
        {
            Vector3& hip = clip.hips[(size_t)f];
            hip.x = r.F32();
            hip.y = r.F32();
            hip.z = r.F32();
            r.Take(packed.data(), (int)(packed.size() * sizeof(int16_t)));
            for (uint32_t b = 0; b < boneCount; ++b)
            {
                const int16_t* q = &packed[b * 4];
                clip.rotations[(size_t)f * boneCount + b] =
                    QuaternionNormalize({ q[0] / 32767.0f, q[1] / 32767.0f, q[2] / 32767.0f, q[3] / 32767.0f });
            }
        }
        out.clips.push_back(std::move(clip));
    }
    UnloadFileData(data);
    if (!r.ok)
    {
        TraceLog(LOG_WARNING, "ANIM: %s: truncated", path);
        out.clips.clear();
        return false;
    }
    TraceLog(LOG_INFO, "ANIM: %s: %d bones, %d clips", path, (int)boneCount, (int)clipCount);
    return true;
}

void AnimBlend::Reset(int boneCount)
{
    sum.assign((size_t)boneCount, Quaternion{ 0.0f, 0.0f, 0.0f, 0.0f });
    hipSum = { 0.0f, 0.0f, 0.0f };
    total = 0.0f;
}

void AnimBlend::Add(const AnimClip& clip, float time, float weight)
{
    if (weight <= 0.0f || clip.frames < 2) return;
    const int bones = (int)(clip.rotations.size() / (size_t)clip.frames);
    const float duration = clip.Duration();
    time = clip.loop ? time - std::floor(time / duration) * duration : std::clamp(time, 0.0f, duration);
    const float at = time * clip.fps;
    const int a = std::min((int)at, clip.frames - 1);
    const int b = clip.loop ? (a + 1) % clip.frames : std::min(a + 1, clip.frames - 1);
    const float t = at - (float)a;
    const int count = std::min(bones, (int)sum.size());
    for (int bone = 0; bone < count; ++bone)
    {
        const Quaternion qa = clip.rotations[(size_t)a * bones + bone];
        Quaternion qb = clip.rotations[(size_t)b * bones + bone];
        if (Dot(qa, qb) < 0.0f) qb = QuaternionScale(qb, -1.0f);
        Quaternion q = QuaternionLerp(qa, qb, t);
        // Жинлэсэн нийлбэр (nlerp): эхний клипийн тэмдэгтэй ижил тал руу
        Quaternion& s = sum[(size_t)bone];
        if (total > 0.0f && Dot(s, q) < 0.0f) q = QuaternionScale(q, -1.0f);
        s = QuaternionAdd(s, QuaternionScale(q, weight));
    }
    hipSum = Vector3Add(hipSum, Vector3Scale(Vector3Lerp(clip.hips[(size_t)a], clip.hips[(size_t)b], t), weight));
    total += weight;
}

bool AnimBlend::Result(std::vector<Quaternion>& rotations, Vector3& hips) const
{
    if (total <= 0.0f) return false;
    rotations.resize(sum.size());
    for (size_t bone = 0; bone < sum.size(); ++bone)
    {
        rotations[bone] = QuaternionLength(sum[bone]) > 1e-6f ? QuaternionNormalize(sum[bone]) : QuaternionIdentity();
    }
    hips = Vector3Scale(hipSum, 1.0f / total);
    return true;
}
