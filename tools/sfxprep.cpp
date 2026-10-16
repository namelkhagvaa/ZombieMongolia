// Дууны файл бэлтгэх хэрэгсэл (raylib): CC0 бичлэгүүдийг тоглоомд тохируулна.
//   sfxprep info  <in>
//   sfxprep slice <in> <outPrefix> <maxClips> <clipSeconds> [threshold]  — буудлага бүрийг тусгаарлана
//   sfxprep clip  <in> <out.wav> <startSec> <seconds>                     — хэсгийг таслана
//   sfxprep loop  <in> <out.wav>                                          — давталт (зөвхөн моно, 44.1 кГц)
//   sfxprep slow  <in> <out.wav> <factor> <startSec> <seconds>           — удаашруулж намсгана (0.4 = 2.5 дахин удаан)
//   sfxprep mix   <base.wav> <over> <out.wav> <gain>                     — over-ийг эхэнд нь хольж нэмнэ
// Гаралт: моно, 44100 Гц, 16 бит, чангыг тэгшилсэн, төгсгөл нь бүдгэрсэн WAV.
#include "raylib.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
    const int kRate = 44100;

    std::vector<float> LoadMono(const char* path, float& seconds)
    {
        Wave wave = LoadWave(path);
        if (wave.frameCount == 0) { std::printf("cannot load %s\n", path); std::exit(1); }
        WaveFormat(&wave, kRate, 32, 1);
        const float* data = (const float*)wave.data;
        std::vector<float> s(data, data + wave.frameCount);
        UnloadWave(wave);
        seconds = (float)s.size() / kRate;
        return s;
    }

    void Save(std::vector<float> s, const std::string& path, float peak = 0.92f, float fadeOut = 0.08f)
    {
        float m = 1e-6f;
        for (float v : s) m = std::max(m, std::fabs(v));
        for (float& v : s) v = v / m * peak;
        const size_t fade = std::min(s.size(), (size_t)(fadeOut * kRate));
        for (size_t i = 0; i < fade; ++i) s[s.size() - 1 - i] *= (float)i / (float)fade;
        const size_t fadeIn = std::min(s.size(), (size_t)(0.002f * kRate));
        for (size_t i = 0; i < fadeIn; ++i) s[i] *= (float)i / (float)fadeIn;
        std::vector<short> pcm(s.size());
        for (size_t i = 0; i < s.size(); ++i) pcm[i] = (short)std::clamp(s[i] * 32767.0f, -32767.0f, 32767.0f);
        Wave out = { (unsigned int)pcm.size(), kRate, 16, 1, pcm.data() };
        ExportWave(out, path.c_str());
        std::printf("  -> %s (%.2f s)\n", path.c_str(), (float)s.size() / kRate);
    }

    // Энергийн огцом өсөлт (онсет): цонх бүрийн RMS
    std::vector<size_t> Onsets(const std::vector<float>& s, float threshold, float minGap)
    {
        const size_t win = kRate / 200; // 5 мс
        std::vector<float> env;
        for (size_t i = 0; i + win < s.size(); i += win)
        {
            float e = 0.0f;
            for (size_t k = 0; k < win; ++k) e += s[i + k] * s[i + k];
            env.push_back(std::sqrt(e / (float)win));
        }
        float peak = 1e-6f;
        for (float e : env) peak = std::max(peak, e);
        std::vector<size_t> onsets;
        size_t last = 0;
        bool armed = true;
        for (size_t i = 1; i < env.size(); ++i)
        {
            const size_t at = i * win;
            if (armed && env[i] > threshold * peak && env[i] > env[i - 1] * 3.0f)
            {
                if (onsets.empty() || at - last > (size_t)(minGap * kRate))
                {
                    // хамгийн эхний огцом өсөлт рүү бага зэрэг ухарна
                    onsets.push_back(at > win * 2 ? at - win * 2 : 0);
                    last = at;
                }
            }
        }
        return onsets;
    }
}

int main(int argc, char** argv)
{
    SetTraceLogLevel(LOG_WARNING);
    if (argc < 3) { std::printf("usage: see source\n"); return 1; }
    const std::string cmd = argv[1];
    float seconds = 0.0f;
    std::vector<float> s = LoadMono(argv[2], seconds);
    if (cmd == "info")
    {
        const auto on = Onsets(s, 0.25f, 0.4f);
        std::printf("%s: %.2f s, onsets:", argv[2], seconds);
        for (size_t o : on) std::printf(" %.2f", (float)o / kRate);
        std::printf("\n");
    }
    else if (cmd == "slice" && argc >= 6)
    {
        const int maxClips = std::atoi(argv[4]);
        const float clip = (float)std::atof(argv[5]);
        const float threshold = argc >= 7 ? (float)std::atof(argv[6]) : 0.25f;
        const auto on = Onsets(s, threshold, clip * 0.8f);
        int n = 0;
        for (size_t o : on)
        {
            if (n >= maxClips) break;
            const size_t end = std::min(s.size(), o + (size_t)(clip * kRate));
            std::vector<float> part(s.begin() + (long)o, s.begin() + (long)end);
            Save(part, std::string(argv[3]) + (n == 0 ? "" : "_" + std::to_string(n)) + ".wav", 0.92f, clip * 0.35f);
            ++n;
        }
    }
    else if (cmd == "clip" && argc >= 6)
    {
        const size_t start = (size_t)(std::atof(argv[4]) * kRate);
        const size_t end = std::min(s.size(), start + (size_t)(std::atof(argv[5]) * kRate));
        std::vector<float> part(s.begin() + (long)std::min(start, s.size()), s.begin() + (long)end);
        Save(part, argv[3], 0.9f, std::min(0.25f, (float)part.size() / kRate * 0.3f));
    }
    else if (cmd == "slow" && argc >= 7)
    {
        const float factor = (float)std::atof(argv[4]);
        const size_t start = (size_t)(std::atof(argv[5]) * kRate);
        const size_t count = (size_t)(std::atof(argv[6]) * kRate);
        std::vector<float> out(count, 0.0f);
        for (size_t i = 0; i < count; ++i)
        {
            const float src = (float)start + (float)i * factor;
            const size_t a = (size_t)src;
            if (a + 1 >= s.size()) break;
            const float t = src - (float)a;
            out[i] = s[a] * (1.0f - t) + s[a + 1] * t;
        }
        Save(out, argv[3], 0.95f, (float)count / kRate * 0.4f);
    }
    else if (cmd == "mix" && argc >= 6)
    {
        float other = 0.0f;
        std::vector<float> over = LoadMono(argv[3], other);
        float m1 = 1e-6f, m2 = 1e-6f;
        for (float v : s) m1 = std::max(m1, std::fabs(v));
        for (float v : over) m2 = std::max(m2, std::fabs(v));
        const float gain = (float)std::atof(argv[5]);
        for (size_t i = 0; i < s.size() && i < over.size(); ++i) s[i] = s[i] / m1 + over[i] / m2 * gain;
        Save(s, argv[4], 0.95f, 0.3f);
    }
    else if (cmd == "loop" && argc >= 4)
    {
        // давталт: төгсгөлийг бүдгэрүүлэхгүй (тасралтгүй давтагдана)
        float m = 1e-6f;
        for (float v : s) m = std::max(m, std::fabs(v));
        std::vector<short> pcm(s.size());
        for (size_t i = 0; i < s.size(); ++i) pcm[i] = (short)std::clamp(s[i] / m * 0.85f * 32767.0f, -32767.0f, 32767.0f);
        Wave out = { (unsigned int)pcm.size(), kRate, 16, 1, pcm.data() };
        ExportWave(out, argv[3]);
        std::printf("  -> %s (%.2f s loop)\n", argv[3], seconds);
    }
    return 0;
}
