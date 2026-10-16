#include "Audio.h"

#include "raymath.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
    const int kRate = 22050;
    const int kAliases = 5; // нэг дуу зэрэг хэдэн удаа тоглогдох
    const float kTau = 6.2831853f;

    // ------------------------------------------------------------ Нийлэгжүүлэлтийн туслахууд

    struct Noise
    {
        uint32_t state;
        explicit Noise(uint32_t seed) : state(seed * 747796405u + 2891336453u) {}
        float Next() // -1..1
        {
            state = state * 1664525u + 1013904223u;
            return (float)(state >> 8) / 8388608.0f - 1.0f;
        }
        float Range(float a, float b) { return a + (b - a) * (Next() * 0.5f + 0.5f); }
    };

    struct OnePole
    {
        float y = 0.0f;
        float Low(float x, float cutoff)
        {
            const float a = 1.0f - std::exp(-kTau * cutoff / (float)kRate);
            y += a * (x - y);
            return y;
        }
    };

    // RBJ biquad bandpass (тогтмол оргил 0 дБ)
    struct BandPass
    {
        float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        float Process(float x, float freq, float q)
        {
            const float w = kTau * std::min(freq, (float)kRate * 0.45f) / (float)kRate;
            const float alpha = std::sin(w) / (2.0f * q);
            const float a0 = 1.0f + alpha;
            const float b0 = alpha / a0, b2 = -alpha / a0;
            const float a1 = -2.0f * std::cos(w) / a0, a2 = (1.0f - alpha) / a0;
            const float y = b0 * x + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x;
            y2 = y1; y1 = y;
            return y;
        }
    };

    std::vector<float> Buffer(float seconds) { return std::vector<float>((size_t)(seconds * (float)kRate), 0.0f); }
    float T(size_t i) { return (float)i / (float)kRate; }

    void Normalize(std::vector<float>& s, float peak = 0.9f)
    {
        float m = 0.0001f;
        for (float v : s) m = std::max(m, std::fabs(v));
        for (float& v : s) v = std::tanh(v / m * 1.2f) / std::tanh(1.2f) * peak;
        // Төгсгөлд товшилт гарахгүй
        const size_t fade = std::min(s.size(), (size_t)(0.01f * kRate));
        for (size_t i = 0; i < fade; ++i) s[s.size() - 1 - i] *= (float)i / (float)fade;
    }

    // Буу: богино "тас" + биеийн шуугиан + доод "дүнгэнэх" + цуурай
    std::vector<float> Gun(uint32_t seed, float dur, float body, float thumpHi, float thumpLo, float thumpTau, float tail,
                           float crackGain, float echoDelay)
    {
        auto s = Buffer(dur);
        Noise n(seed);
        OnePole lowBody, lowTail, high;
        float phase = 0.0f;
        for (size_t i = 0; i < s.size(); ++i)
        {
            const float t = T(i);
            const float x = n.Next();
            const float crack = (x - high.Low(x, 3000.0f)) * std::exp(-t / 0.004f) * crackGain;
            const float bodyNoise = lowBody.Low(x, 2600.0f) * std::exp(-t / body) * 1.6f;
            const float f = thumpLo + (thumpHi - thumpLo) * std::exp(-t / 0.05f);
            phase += f / (float)kRate;
            const float thump = std::sin(kTau * phase) * std::exp(-t / thumpTau) * 1.1f;
            const float tailNoise = lowTail.Low(x, 650.0f) * std::exp(-t / tail) * 1.2f;
            s[i] = crack + bodyNoise + thump + tailNoise;
        }
        if (echoDelay > 0.0f)
        {
            const size_t d = (size_t)(echoDelay * kRate);
            for (size_t i = s.size() - 1; i >= d; --i) s[i] += s[i - d] * 0.35f;
        }
        Normalize(s);
        return s;
    }

    // Шуугих (хутга, шидэх, пуужин): шүүлтүүрийн давтамж гулсана
    std::vector<float> Whoosh(uint32_t seed, float dur, float f0, float f1, float q)
    {
        auto s = Buffer(dur);
        Noise n(seed);
        BandPass bp;
        for (size_t i = 0; i < s.size(); ++i)
        {
            const float u = T(i) / dur;
            const float env = std::sin(3.14159f * std::min(1.0f, u * 1.2f)) * (1.0f - u * 0.3f);
            s[i] = bp.Process(n.Next(), f0 + (f1 - f0) * u, q) * env;
        }
        Normalize(s, 0.8f);
        return s;
    }

    std::vector<float> Reload(uint32_t seed)
    {
        auto s = Buffer(0.6f);
        Noise n(seed);
        const float clicks[3] = { 0.0f, 0.2f, 0.44f };
        for (int c = 0; c < 3; ++c)
        {
            const size_t start = (size_t)(clicks[c] * kRate);
            for (size_t i = start; i < s.size(); ++i)
            {
                const float t = T(i - start);
                if (t > 0.06f) break;
                float v = n.Next() * std::exp(-t / 0.003f) * 0.8f;
                for (float f : { 2100.0f, 3400.0f, 5200.0f }) v += std::sin(kTau * f * (1.0f + 0.04f * (float)c) * t) * std::exp(-t / 0.01f) * 0.4f;
                s[i] += v;
            }
        }
        Normalize(s, 0.7f);
        return s;
    }

    std::vector<float> Explosion(uint32_t seed)
    {
        auto s = Buffer(2.4f);
        Noise n(seed);
        OnePole boom, rumble, crackHp;
        float phase = 0.0f;
        for (size_t i = 0; i < s.size(); ++i)
        {
            const float t = T(i);
            const float x = n.Next();
            const float crack = (x - crackHp.Low(x, 1500.0f)) * std::exp(-t / 0.012f) * 1.5f;
            const float b = boom.Low(x, 120.0f + 260.0f * std::exp(-t / 0.15f)) * std::exp(-t / 0.55f) * 6.0f;
            phase += (28.0f + 30.0f * std::exp(-t / 0.2f)) / (float)kRate;
            const float sub = std::sin(kTau * phase) * std::exp(-t / 0.4f) * 1.2f;
            const float r = rumble.Low(x, 140.0f) * std::exp(-t / 1.1f) * 4.0f * (0.7f + 0.3f * std::sin(t * 9.0f));
            s[i] = crack + b + sub + r;
        }
        Normalize(s, 0.95f);
        return s;
    }

    // Зомбины дуу: дуу хоолойн хөвчний импульс (хөвчний давтамж f0) + хамар амны формантууд + амьсгал
    std::vector<float> Voice(uint32_t seed, float dur, float f0a, float f0b, float f1, float f2, float drive, float breath,
                             float gurgle)
    {
        auto s = Buffer(dur);
        Noise n(seed);
        BandPass b1, b2, b3;
        float phase = 0.0f;
        const float vibrato = n.Range(4.0f, 7.0f), wobble = n.Range(2.0f, 4.0f);
        for (size_t i = 0; i < s.size(); ++i)
        {
            const float t = T(i), u = t / dur;
            float f0 = f0a + (f0b - f0a) * u;
            f0 *= 1.0f + 0.03f * std::sin(kTau * vibrato * t) + 0.02f * n.Next();
            phase += f0 / (float)kRate;
            const float frac = phase - std::floor(phase);
            const float glottal = (frac < 0.6f ? std::sin(3.14159f * frac / 0.6f) : 0.0f) * 2.0f - 0.7f; // хөвчний импульс
            const float source = glottal + n.Next() * breath;
            // Формант: "ыы" -> "аа" -> "ыы" гулсалт
            const float open = std::sin(3.14159f * u);
            const float v = b1.Process(source, f1 * (0.85f + 0.3f * open), 4.0f) +
                            b2.Process(source, f2 * (0.85f + 0.25f * open), 6.0f) * 0.6f +
                            b3.Process(source, 2500.0f, 8.0f) * 0.15f;
            float env = std::min(1.0f, t / 0.12f) * std::min(1.0f, (dur - t) / 0.25f);
            env *= 0.75f + 0.25f * std::sin(kTau * wobble * t);
            if (gurgle > 0.0f) env *= 1.0f - gurgle * (0.5f + 0.5f * std::sin(kTau * 17.0f * t));
            s[i] = std::tanh(v * drive) * env;
        }
        Normalize(s, 0.85f);
        return s;
    }

    std::vector<float> Hit(uint32_t seed)
    {
        auto s = Buffer(0.18f);
        Noise n(seed);
        OnePole lp;
        for (size_t i = 0; i < s.size(); ++i)
        {
            const float t = T(i);
            s[i] = lp.Low(n.Next(), 900.0f) * std::exp(-t / 0.03f) * 3.0f + std::sin(kTau * 140.0f * t) * std::exp(-t / 0.04f);
        }
        Normalize(s, 0.75f);
        return s;
    }

    std::vector<float> Crash(uint32_t seed)
    {
        auto s = Buffer(1.1f);
        Noise n(seed);
        OnePole lp;
        const float partials[6] = { 310.0f, 523.0f, 798.0f, 1187.0f, 1650.0f, 2210.0f };
        float detune[6], decay[6];
        for (int k = 0; k < 6; ++k)
        {
            detune[k] = n.Range(0.94f, 1.06f);
            decay[k] = n.Range(0.18f, 0.55f);
        }
        float rattleTimer = 0.0f;
        float rattle = 0.0f;
        for (size_t i = 0; i < s.size(); ++i)
        {
            const float t = T(i);
            const float x = n.Next();
            float v = x * std::exp(-t / 0.025f) * 1.4f;                                 // цохилт
            for (int k = 0; k < 6; ++k) v += std::sin(kTau * partials[k] * detune[k] * t) * std::exp(-t / decay[k]) * 0.25f;
            v += std::sin(kTau * 70.0f * t) * std::exp(-t / 0.08f) * 1.2f;              // дүнгэнэх
            rattleTimer -= 1.0f / (float)kRate;
            if (rattleTimer <= 0.0f && t < 0.5f) { rattleTimer = n.Range(0.02f, 0.07f); rattle = 1.0f; }
            rattle *= 0.997f;
            v += lp.Low(x, 2500.0f) * rattle * 0.8f;                                  // хэлтэрхий шажигнах
            s[i] = v;
        }
        Normalize(s, 0.9f);
        return s;
    }

    std::vector<float> Thud(uint32_t seed)
    {
        auto s = Buffer(0.35f);
        Noise n(seed);
        OnePole lp;
        float phase = 0.0f;
        for (size_t i = 0; i < s.size(); ++i)
        {
            const float t = T(i);
            phase += (45.0f + 35.0f * std::exp(-t / 0.04f)) / (float)kRate;
            s[i] = std::sin(kTau * phase) * std::exp(-t / 0.07f) + lp.Low(n.Next(), 400.0f) * std::exp(-t / 0.05f) * 2.0f;
        }
        Normalize(s, 0.8f);
        return s;
    }

    std::vector<float> Hoof(uint32_t seed, float tone)
    {
        auto s = Buffer(0.14f);
        Noise n(seed);
        BandPass bp;
        for (size_t i = 0; i < s.size(); ++i)
        {
            const float t = T(i);
            s[i] = bp.Process(n.Next(), 520.0f * tone, 2.0f) * std::exp(-t / 0.018f) * 2.0f +
                   std::sin(kTau * 210.0f * tone * t) * std::exp(-t / 0.03f) * 0.6f;
        }
        Normalize(s, 0.7f);
        return s;
    }

    // Дугуй чахирах: өндөр давтамжтай, хэлбэлзэлтэй "шиих"
    std::vector<float> Screech(uint32_t seed)
    {
        auto s = Buffer(0.45f);
        Noise n(seed);
        BandPass a, b;
        float phase = 0.0f;
        for (size_t i = 0; i < s.size(); ++i)
        {
            const float t = T(i), u = t / 0.45f;
            phase += (880.0f + 60.0f * std::sin(kTau * 7.0f * t) + 30.0f * n.Next()) / (float)kRate;
            const float tone = std::sin(kTau * phase) * 0.5f + std::sin(kTau * phase * 2.02f) * 0.25f;
            const float hiss = a.Process(n.Next(), 2600.0f, 3.0f) * 1.4f + b.Process(n.Next(), 1300.0f, 4.0f) * 0.8f;
            const float env = std::sin(3.14159f * u); // давхцаж тоглогдоход тасралтгүй сонсогдоно
            s[i] = (tone + hiss) * env;
        }
        Normalize(s, 0.6f);
        return s;
    }

    // ------------------------------------------------------------ Дууны сан

    struct Voice3D
    {
        std::vector<Sound> variants; // хувилбар бүрийн эх
        std::vector<std::vector<Sound>> aliases;
        float reference = 10.0f; // энэ зайд хагас чангаар
        float maxDistance = 120.0f;
        int next = 0;
    };

    bool ready = false;
    bool enabled = true;
    float masterVolume = 1.0f;
    Voice3D bank[(int)Sfx::Count];
    Vector3 listener = { 0.0f, 0.0f, 0.0f }, listenerRight = { 1.0f, 0.0f, 0.0f };

    Sound FromSamples(const std::vector<float>& samples)
    {
        std::vector<int16_t> pcm(samples.size());
        for (size_t i = 0; i < samples.size(); ++i) pcm[i] = (int16_t)std::clamp(samples[i] * 32767.0f, -32767.0f, 32767.0f);
        Wave wave = {};
        wave.frameCount = (unsigned int)pcm.size();
        wave.sampleRate = kRate;
        wave.sampleSize = 16;
        wave.channels = 1;
        wave.data = pcm.data();
        return LoadSoundFromWave(wave); // өгөгдлийг хуулна
    }

    void Add(Sfx id, const std::vector<float>& samples)
    {
        bank[(int)id].variants.push_back(FromSamples(samples));
        if (const char* dump = std::getenv("ZM_DUMP_SOUNDS")) // хөгжүүлэлт: нийлэгжүүлсэн дууг WAV болгон гаргана
        {
            std::vector<int16_t> pcm(samples.size());
            for (size_t i = 0; i < samples.size(); ++i) pcm[i] = (int16_t)std::clamp(samples[i] * 32767.0f, -32767.0f, 32767.0f);
            Wave wave = { (unsigned int)pcm.size(), kRate, 16, 1, pcm.data() };
            ExportWave(wave, TextFormat("%s/sfx_%d_%d.wav", dump, (int)id, (int)bank[(int)id].variants.size() - 1));
        }
    }

    // assets/sounds/<name>.wav|.ogg байвал түүнийг ашиглана
    // Дууны файлын хавтас: компьютер дээр exe-ийн дэргэдэх assets/sounds, утсан дээр APK-ийн assets/sounds
    std::string SoundFolder()
    {
#if defined(__ANDROID__)
        return "sounds/";
#else
        const std::string application = GetApplicationDirectory();
        for (const std::string& folder : { std::string("assets/sounds/"), application + "assets/sounds/", application + "../assets/sounds/" })
        {
            if (DirectoryExists(folder.c_str())) return folder;
        }
        return "assets/sounds/";
#endif
    }

    // Файл ачаалах: Android дээр FileExists нь APK-ийн assets-ийг хардаггүй тул шууд оролдоно.
    bool TryLoad(const std::string& path, Sound& out)
    {
#if !defined(__ANDROID__)
        if (!FileExists(path.c_str())) return false;
#endif
        Wave wave = LoadWave(path.c_str());
        if (wave.frameCount == 0) return false;
        out = LoadSoundFromWave(wave);
        UnloadWave(wave);
        return out.frameCount > 0;
    }

    // <name>.wav, <name>_1.wav ... <name>_7.wav (жинхэнэ бичлэгүүд, хувилбар бүр бага зэрэг өөр)
    bool LoadFile(Sfx id, const char* name)
    {
        const std::string folder = SoundFolder();
        int loaded = 0;
        for (int k = 0; k < 8; ++k)
        {
            const std::string base = folder + name + (k == 0 ? std::string() : "_" + std::to_string(k));
            Sound sound = {};
            if (TryLoad(base + ".wav", sound) || TryLoad(base + ".ogg", sound))
            {
                bank[(int)id].variants.push_back(sound);
                ++loaded;
            }
            else if (k > 0)
            {
                break;
            }
        }
        if (loaded > 0) TraceLog(LOG_INFO, "AUDIO: %s: %d recordings", name, loaded);
        return loaded > 0;
    }

    void Range(Sfx id, float reference, float maxDistance)
    {
        bank[(int)id].reference = reference;
        bank[(int)id].maxDistance = maxDistance;
    }

    // ------------------------------------------------------------ Хөдөлгүүр (дуут урсгал, аудио утсанд үүсгэнэ)

    struct Engine
    {
        AudioStream stream = {};
        std::atomic<int> kind{ -1 };
        std::atomic<float> rpm{ 0.0f }, throttle{ 0.0f }, gain{ 0.0f };
        // аудио утсанд л хэрэглэгдэнэ
        float phase = 0.0f, curRpm = 0.0f, curThrottle = 0.0f, curGain = 0.0f;
        double position = 0.0;  // давталт доторх байрлал (дээж)
        int playing = -1;       // одоо тоглож буй давталт
        OnePole low, sub;
        Noise noise{ 99u };
    };
    Engine engines[2];

    // Бичлэгтэй хөдөлгүүрийн давталт (залгаасыг нь crossfade хийсэн, урсгалын давтамжаар): 0 = намхан, 1 = өндөр
    std::vector<float> engineSamples[2];
    bool engineRecorded = false;

    void Synthesize(Engine& e, float* out, unsigned int frames)
    {
        const int kind = e.kind.load();
        const float targetRpm = e.rpm.load(), targetThrottle = e.throttle.load(), targetGain = kind < 0 ? 0.0f : e.gain.load();
        if (engineRecorded)
        {
            // Мотоцикл, гурван дугуйт: өндөр давталт; машин, ачааны машин: намхан. Эргэлт өсөхөд хурдан тоглуулна.
            const int which = kind == 1 || kind == 2 ? 1 : 0;
            for (unsigned int i = 0; i < frames; ++i)
            {
                e.curRpm += (targetRpm - e.curRpm) * 0.0006f;
                e.curThrottle += (targetThrottle - e.curThrottle) * 0.0015f;
                e.curGain += (targetGain - e.curGain) * 0.0012f;
                if (e.playing != which)
                {
                    if (e.curGain > 0.002f && e.playing >= 0) { e.curGain *= 0.98f; } // солихын өмнө намсгана
                    else { e.playing = which; e.position = 0.0; }
                }
                const std::vector<float>& loop = engineSamples[e.playing < 0 ? which : e.playing];
                // Үндсэн дуу: жигд гармоник хүнгэнээ (тасралтгүй, цохилтгүй). Давтамж нь 40 Гц-ээс доош буухгүй
                // тул "дүх дүх" гэсэн тусдаа цохилт болж сонсогдохгүй.
                float base, pitch;
                switch (kind)
                {
                case 1: base = 70.0f + 190.0f * e.curRpm; pitch = 1.0f + 0.8f * e.curRpm; break;  // мотоцикл
                case 2: base = 58.0f + 140.0f * e.curRpm; pitch = 0.95f + 0.6f * e.curRpm; break; // гурван дугуйт
                case 3: base = 42.0f + 70.0f * e.curRpm; pitch = 0.9f + 0.4f * e.curRpm; break;   // ачааны машин
                default: base = 48.0f + 115.0f * e.curRpm; pitch = 1.0f + 0.6f * e.curRpm; break; // машин
                }
                e.phase += base / (float)kRate;
                if (e.phase > 1000.0f) e.phase -= 1000.0f;
                const float p = kTau * e.phase;
                float hum = std::sin(p) * 0.8f + std::sin(2.0f * p) * 0.55f + std::sin(3.0f * p) * 0.35f +
                            std::sin(4.0f * p) * 0.2f + std::sin(0.5f * p) * 0.25f;
                hum += e.noise.Next() * 0.08f;
                hum = e.low.Low(hum, 900.0f + 1600.0f * e.curThrottle);
                // Бичлэг: удаашруулахгүй (pitch >= 0.9), зөвхөн өнгө нэмнэ
                const size_t n = loop.size();
                const size_t a = (size_t)e.position % n, b = (a + 1) % n;
                const float t = (float)(e.position - std::floor(e.position));
                const float recorded = loop[a] * (1.0f - t) + loop[b] * t;
                e.position += pitch;
                if (e.position >= (double)n) e.position -= (double)n;
                const float v = std::tanh(hum * 0.55f) * 0.8f + recorded * 0.22f;
                out[i] = v * e.curGain * (0.6f + 0.4f * e.curThrottle);
            }
            return;
        }
        for (unsigned int i = 0; i < frames; ++i)
        {
            e.curRpm += (targetRpm - e.curRpm) * 0.0008f;
            e.curThrottle += (targetThrottle - e.curThrottle) * 0.0015f;
            e.curGain += (targetGain - e.curGain) * 0.0015f;
            // Төрөл бүрийн шаталтын давтамж, өнгө
            float f, sharp, noiseMix, cutoff, subMix;
            switch (kind)
            {
            case 1: f = 38.0f + 210.0f * e.curRpm; sharp = 11.0f; noiseMix = 0.35f; cutoff = 1800.0f; subMix = 0.15f; break;  // мотоцикл
            case 2: f = 30.0f + 150.0f * e.curRpm; sharp = 9.0f; noiseMix = 0.3f; cutoff = 1400.0f; subMix = 0.2f; break;    // гурван дугуйт
            case 3: f = 15.0f + 62.0f * e.curRpm; sharp = 5.0f; noiseMix = 0.55f; cutoff = 700.0f; subMix = 0.6f; break;     // дизель
            default: f = 24.0f + 120.0f * e.curRpm; sharp = 7.0f; noiseMix = 0.3f; cutoff = 1000.0f; subMix = 0.35f; break;  // машин
            }
            e.phase += f / (float)kRate;
            if (e.phase > 1000.0f) e.phase -= 1000.0f;
            const float frac = e.phase - std::floor(e.phase);
            const float pulse = std::exp(-frac * sharp);
            const float x = e.noise.Next();
            float v = pulse * (std::sin(kTau * frac * 2.0f) * (1.0f - noiseMix) + x * noiseMix);
            v += e.sub.Low(std::sin(kTau * e.phase * 0.5f), 200.0f) * subMix;
            v = e.low.Low(v, cutoff * (0.6f + 0.8f * e.curThrottle) + 400.0f * e.curRpm);
            out[i] = std::tanh(v * 2.2f) * e.curGain * (0.55f + 0.45f * e.curThrottle);
        }
    }

    void EngineCallback0(void* buffer, unsigned int frames) { Synthesize(engines[0], (float*)buffer, frames); }
    void EngineCallback1(void* buffer, unsigned int frames) { Synthesize(engines[1], (float*)buffer, frames); }

    float Attenuation(Vector3 position, float reference, float maxDistance, float& pan)
    {
        const Vector3 offset = Vector3Subtract(position, listener);
        const float distance = Vector3Length(offset);
        pan = 0.5f;
        if (distance > maxDistance) return 0.0f;
        if (distance > 0.5f)
        {
            const float side = Vector3DotProduct(Vector3Scale(offset, 1.0f / distance), listenerRight);
            pan = 0.5f - 0.42f * side * std::min(1.0f, distance / 4.0f); // pan: 1 = зүүн
        }
        const float fade = 1.0f - std::pow(distance / maxDistance, 2.0f);
        return fade / (1.0f + (distance / reference) * (distance / reference));
    }
}

namespace Audio
{
    void Init()
    {
        if (ready) return;
        InitAudioDevice();
        if (!IsAudioDeviceReady())
        {
            TraceLog(LOG_WARNING, "AUDIO: no audio device, sound disabled");
            return;
        }
        SetAudioStreamBufferSizeDefault(1024);

        // Файл байвал тэрийг, үгүй бол нийлэгжүүлнэ (хувилбар бүр бага зэрэг өөр)
        if (!LoadFile(Sfx::Rifle, "rifle")) for (uint32_t k = 0; k < 3; ++k) Add(Sfx::Rifle, Gun(11 + k, 0.65f, 0.035f, 115.0f, 48.0f, 0.07f, 0.24f, 1.4f, 0.0f));
        if (!LoadFile(Sfx::Pistol, "pistol")) for (uint32_t k = 0; k < 2; ++k) Add(Sfx::Pistol, Gun(21 + k, 0.45f, 0.022f, 170.0f, 70.0f, 0.05f, 0.14f, 1.2f, 0.0f));
        if (!LoadFile(Sfx::Shotgun, "shotgun")) for (uint32_t k = 0; k < 2; ++k) Add(Sfx::Shotgun, Gun(31 + k, 0.95f, 0.065f, 95.0f, 38.0f, 0.11f, 0.4f, 1.0f, 0.0f));
        if (!LoadFile(Sfx::Sniper, "sniper")) Add(Sfx::Sniper, Gun(41, 1.4f, 0.045f, 130.0f, 45.0f, 0.09f, 0.55f, 2.0f, 0.21f));
        if (!LoadFile(Sfx::Launcher, "launcher"))
        {
            auto w = Whoosh(51, 0.9f, 300.0f, 1600.0f, 1.5f);
            auto g = Gun(52, 0.9f, 0.05f, 90.0f, 40.0f, 0.1f, 0.3f, 0.6f, 0.0f);
            for (size_t i = 0; i < w.size(); ++i) w[i] = w[i] * 0.8f + g[i] * 0.6f;
            Normalize(w);
            Add(Sfx::Launcher, w);
        }
        if (!LoadFile(Sfx::Knife, "knife")) for (uint32_t k = 0; k < 2; ++k) Add(Sfx::Knife, Whoosh(61 + k, 0.2f, 1300.0f, 3800.0f, 2.5f));
        if (!LoadFile(Sfx::Throw, "throw")) Add(Sfx::Throw, Whoosh(71, 0.35f, 500.0f, 1500.0f, 2.0f));
        if (!LoadFile(Sfx::Reload, "reload")) Add(Sfx::Reload, Reload(81));
        if (!LoadFile(Sfx::Explosion, "explosion")) for (uint32_t k = 0; k < 2; ++k) Add(Sfx::Explosion, Explosion(91 + k));
        Add(Sfx::Rumble, Explosion(95)); // дэлбэрэлтийн гүн нүргээн (бичлэгтэй хамт давхарлана)
        if (!LoadFile(Sfx::ZombieGroan, "zombie_groan"))
        {
            Noise n(5);
            for (uint32_t k = 0; k < 6; ++k)
            {
                const float f0 = n.Range(68.0f, 105.0f);
                Add(Sfx::ZombieGroan, Voice(101 + k, n.Range(0.9f, 1.7f), f0, f0 * n.Range(0.8f, 1.1f), n.Range(430.0f, 620.0f),
                                            n.Range(900.0f, 1250.0f), n.Range(2.0f, 3.5f), 0.35f, 0.0f));
            }
        }
        if (!LoadFile(Sfx::ZombieAttack, "zombie_attack"))
        {
            Noise n(6);
            for (uint32_t k = 0; k < 3; ++k)
            {
                const float f0 = n.Range(150.0f, 210.0f);
                Add(Sfx::ZombieAttack, Voice(121 + k, n.Range(0.45f, 0.65f), f0, f0 * 0.75f, 720.0f, 1500.0f, 5.0f, 0.55f, 0.0f));
            }
        }
        if (!LoadFile(Sfx::ZombieDie, "zombie_die"))
        {
            for (uint32_t k = 0; k < 3; ++k) Add(Sfx::ZombieDie, Voice(131 + k, 0.9f, 120.0f - 10.0f * (float)k, 55.0f, 520.0f, 1000.0f, 3.0f, 0.4f, 0.5f));
        }
        if (!LoadFile(Sfx::BossRoar, "boss_roar")) Add(Sfx::BossRoar, Voice(141, 1.9f, 58.0f, 46.0f, 380.0f, 820.0f, 7.0f, 0.6f, 0.0f));
        if (!LoadFile(Sfx::Hit, "hit")) for (uint32_t k = 0; k < 3; ++k) Add(Sfx::Hit, Hit(151 + k));
        if (!LoadFile(Sfx::Crash, "crash")) for (uint32_t k = 0; k < 2; ++k) Add(Sfx::Crash, Crash(161 + k));
        if (!LoadFile(Sfx::Thud, "thud")) for (uint32_t k = 0; k < 2; ++k) Add(Sfx::Thud, Thud(171 + k));
        if (!LoadFile(Sfx::Hoof, "hoof")) for (uint32_t k = 0; k < 2; ++k) Add(Sfx::Hoof, Hoof(181 + k, 1.0f + 0.12f * (float)k));
        if (!LoadFile(Sfx::Screech, "screech")) for (uint32_t k = 0; k < 2; ++k) Add(Sfx::Screech, Screech(191 + k));

        // Сонсогдох зай
        for (Sfx id : { Sfx::Rifle, Sfx::Pistol, Sfx::Shotgun, Sfx::Sniper, Sfx::Launcher }) Range(id, 22.0f, 320.0f);
        Range(Sfx::Explosion, 40.0f, 450.0f);
        Range(Sfx::Rumble, 60.0f, 600.0f);
        Range(Sfx::Knife, 4.0f, 25.0f);
        Range(Sfx::Throw, 4.0f, 25.0f);
        Range(Sfx::Reload, 3.0f, 20.0f);
        Range(Sfx::ZombieGroan, 7.0f, 45.0f);
        Range(Sfx::ZombieAttack, 6.0f, 40.0f);
        Range(Sfx::ZombieDie, 7.0f, 50.0f);
        Range(Sfx::BossRoar, 25.0f, 160.0f);
        Range(Sfx::Hit, 6.0f, 40.0f);
        Range(Sfx::Crash, 15.0f, 140.0f);
        Range(Sfx::Thud, 6.0f, 40.0f);
        Range(Sfx::Hoof, 5.0f, 40.0f);
        Range(Sfx::Screech, 12.0f, 90.0f);

        for (Voice3D& voice : bank)
        {
            voice.aliases.resize(voice.variants.size());
            for (size_t v = 0; v < voice.variants.size(); ++v)
            {
                voice.aliases[v].push_back(voice.variants[v]);
                for (int a = 1; a < kAliases; ++a) voice.aliases[v].push_back(LoadSoundAlias(voice.variants[v]));
            }
        }

        // Хөдөлгүүр: бичлэгтэй давталт байвал түүгээр (залгаасгүй), үгүй бол нийлэгжүүлсэн дуугаар
        {
            const std::string folder = SoundFolder();
            const char* names[2] = { "engine.wav", "engine_high.wav" };
            int found = 0;
            for (int k = 0; k < 2; ++k)
            {
                const std::string path = folder + names[k];
#if !defined(__ANDROID__)
                if (!FileExists(path.c_str())) continue;
#endif
                Wave wave = LoadWave(path.c_str());
                if (wave.frameCount == 0) continue;
                WaveFormat(&wave, kRate, 32, 1);
                const float* data = (const float*)wave.data;
                std::vector<float> samples(data, data + wave.frameCount);
                UnloadWave(wave);
                // Залгаас: сүүлийн 40 мс-ийг эхлэлтэй нь хольж тасралтгүй болгоно
                const size_t fade = std::min(samples.size() / 4, (size_t)(0.04f * kRate));
                for (size_t i = 0; i < fade; ++i)
                {
                    const float t = (float)i / (float)fade;
                    samples[i] = samples[i] * t + samples[samples.size() - fade + i] * (1.0f - t);
                }
                samples.resize(samples.size() - fade);
                engineSamples[k] = samples;
                ++found;
            }
            engineRecorded = found == 2;
        }
        for (int i = 0; i < 2; ++i)
        {
            engines[i].stream = LoadAudioStream(kRate, 32, 1);
            SetAudioStreamCallback(engines[i].stream, i == 0 ? EngineCallback0 : EngineCallback1);
            PlayAudioStream(engines[i].stream);
        }
        ready = true;
        SetMasterVolume(enabled ? masterVolume : 0.0f);
        TraceLog(LOG_INFO, "AUDIO: ready");
    }

    void Shutdown()
    {
        if (!ready) return;
        for (Engine& e : engines)
        {
            if (e.stream.buffer) UnloadAudioStream(e.stream);
        }

        for (Voice3D& voice : bank)
        {
            for (std::vector<Sound>& list : voice.aliases)
            {
                for (size_t a = 1; a < list.size(); ++a) UnloadSoundAlias(list[a]);
            }
            for (Sound& s : voice.variants) UnloadSound(s);
            voice.aliases.clear();
            voice.variants.clear();
        }
        CloseAudioDevice();
        ready = false;
    }

    void SetEnabled(bool value)
    {
        enabled = value;
        if (ready) SetMasterVolume(enabled ? masterVolume : 0.0f);
    }

    void SetVolume(float volume)
    {
        masterVolume = std::clamp(volume, 0.0f, 1.0f);
        enabled = masterVolume > 0.0f;
        if (ready) SetMasterVolume(masterVolume);
    }

    bool Enabled() { return enabled; }

    void SetListener(Vector3 position, Vector3 forward)
    {
        listener = position;
        Vector3 flat = { forward.x, 0.0f, forward.z };
        if (Vector3Length(flat) < 0.001f) flat = { 0.0f, 0.0f, 1.0f };
        listenerRight = Vector3Normalize(Vector3CrossProduct(Vector3Normalize(flat), { 0.0f, 1.0f, 0.0f }));
    }

    void Play(Sfx id, Vector3 position, float volume, float pitch)
    {
        if (!ready || !enabled) return;
        if (id == Sfx::Explosion) Play(Sfx::Rumble, position, volume * 0.8f, pitch);
        Voice3D& voice = bank[(int)id];
        if (voice.variants.empty()) return;
        float pan = 0.5f;
        const float gain = volume * Attenuation(position, voice.reference, voice.maxDistance, pan);
        if (gain < 0.01f) return;
        const int variant = GetRandomValue(0, (int)voice.variants.size() - 1);
        std::vector<Sound>& list = voice.aliases[(size_t)variant];
        // Чөлөөтэй дуу хоолой; бүгд завгүй бол хамгийн эрт эхэлсэн (дараалсан) нэгийг тасална
        Sound* chosen = nullptr;
        for (Sound& s : list)
        {
            if (!IsSoundPlaying(s)) { chosen = &s; break; }
        }
        if (!chosen)
        {
            chosen = &list[(size_t)(voice.next % (int)list.size())];
            ++voice.next;
            StopSound(*chosen);
        }
        SetSoundVolume(*chosen, std::min(1.0f, gain));
        SetSoundPitch(*chosen, pitch * (1.0f + 0.04f * (float)GetRandomValue(-100, 100) / 100.0f));
        SetSoundPan(*chosen, pan);
        PlaySound(*chosen);
    }

    void SetEngine(int index, EngineSound kind, float rpm, float throttle, Vector3 position)
    {
        if (!ready || index < 0 || index > 1) return;
        float pan = 0.5f;
        const float gain = kind == EngineSound::Off ? 0.0f : Attenuation(position, index == 0 ? 30.0f : 12.0f, 90.0f, pan);
        Engine& e = engines[index];
        e.kind.store((int)kind);
        e.rpm.store(std::clamp(rpm, 0.0f, 1.0f));
        e.throttle.store(std::clamp(throttle, 0.0f, 1.0f));
        e.gain.store(gain * (engineRecorded ? (kind == EngineSound::Truck ? 0.6f : 0.48f) : (kind == EngineSound::Truck ? 0.75f : 0.6f)));
        SetAudioStreamPan(e.stream, pan);
    }
}
