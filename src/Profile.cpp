#include "Profile.h"

#include "raylib.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// glFinish (--gpu-sync): систем дээрх GL сан (Windows: opengl32, Android: GLESv3, хөтөч: WebGL) аль хэдийн холбогдсон.
#if defined(_WIN32)
extern "C" __declspec(dllimport) void __stdcall glFinish(void);
#elif defined(__ANDROID__) || defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#else
#include <GL/gl.h>
#endif

// GPU-ийн хугацаа (--gpu-timer): зөвхөн компьютер дээр, GLFW-ээр функцээ ачаална (raylib-ийн дотоод glad-д хүрэхгүй).
// Android (GL_EXT_disjoint_timer_query) ба WebGL2 (EXT_disjoint_timer_query_webgl2) дээр баталгаагүй тул дэмжихгүй.
#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
#define ZM_GPU_TIMER 1
extern "C" void (*glfwGetProcAddress(const char* name))(void);
#if defined(_WIN32)
#define ZM_APIENTRY __stdcall
#else
#define ZM_APIENTRY
#endif
#endif

namespace
{
    struct Section
    {
        const char* name;
        double frame = 0.0;        // энэ кадрт (секунд)
        int frameCalls = 0;
        long long calls = 0;       // хэмжилтийн цонхонд нийт дуудлага
        std::vector<float> samples; // кадр бүрийн хугацаа (мс), хэмжилтийн цонхны кадрын дугаараар
        std::vector<float> gpu;     // GPU хугацаа (мс), --gpu-timer
        std::vector<char> gpuSeen;  // тухайн кадрт GPU хэмжилт байсан эсэх
    };

    bool enabled = false;
    bool measuring = false;
    bool gpuSync = false;
    bool gpuTimer = false;
    bool shadowPass = false;
    std::vector<Section> sections;
    std::vector<float> frameTimes; // мс
    int frameIndex = 0;            // хэмжилтийн цонхны кадрын дугаар
    double lastFrameEnd = 0.0;

    int FindSection(const char* name)
    {
        for (size_t i = 0; i < sections.size(); ++i)
        {
            if (sections[i].name == name || std::strcmp(sections[i].name, name) == 0) return (int)i;
        }
        Section section;
        section.name = name;
        section.samples.assign((size_t)frameIndex, 0.0f); // (дараа гарч ирсэн хэсэг: өмнөх кадруудад 0)
        sections.push_back(section);
        return (int)sections.size() - 1;
    }

    void Synchronize()
    {
        rlDrawRenderBatchActive(); // (хуримтлагдсан rlgl геометрийг энэ хэсэгт тооцно)
        glFinish();
    }

#if defined(ZM_GPU_TIMER)
    constexpr unsigned int kTimestamp = 0x8E28, kQueryResult = 0x8866, kQueryAvailable = 0x8867;
    typedef void(ZM_APIENTRY* GenQueries)(int, unsigned int*);
    typedef void(ZM_APIENTRY* QueryCounter)(unsigned int, unsigned int);
    typedef void(ZM_APIENTRY* GetQueryObjectiv)(unsigned int, unsigned int, int*);
    typedef void(ZM_APIENTRY* GetQueryObjectui64v)(unsigned int, unsigned int, uint64_t*);
    GenQueries genQueries = nullptr;
    QueryCounter queryCounter = nullptr;
    GetQueryObjectiv getQueryObjectiv = nullptr;
    GetQueryObjectui64v getQueryObjectui64v = nullptr;

    struct Pending
    {
        int frame, section;
        unsigned int begin, end;
    };
    std::vector<Pending> pending;
    std::vector<unsigned int> freeQueries;

    unsigned int TakeQuery()
    {
        if (freeQueries.empty())
        {
            unsigned int ids[64];
            genQueries(64, ids);
            freeQueries.assign(ids, ids + 64);
        }
        const unsigned int id = freeQueries.back();
        freeQueries.pop_back();
        return id;
    }

    // Хүлээлгүй: өмнөх кадруудын бэлэн болсон асуулгыг уншина (wait = true: тайлангийн өмнө бүгдийг)
    void Resolve(bool wait)
    {
        size_t kept = 0;
        for (size_t i = 0; i < pending.size(); ++i)
        {
            const Pending& p = pending[i];
            if (!wait && frameIndex - p.frame < 3)
            {
                pending[kept++] = p;
                continue;
            }
            int ready = 1;
            if (!wait) getQueryObjectiv(p.end, kQueryAvailable, &ready);
            if (!ready)
            {
                pending[kept++] = p;
                continue;
            }
            uint64_t t0 = 0, t1 = 0;
            getQueryObjectui64v(p.begin, kQueryResult, &t0);
            getQueryObjectui64v(p.end, kQueryResult, &t1);
            Section& s = sections[(size_t)p.section];
            if (s.gpu.size() <= (size_t)p.frame)
            {
                s.gpu.resize((size_t)p.frame + 1, 0.0f);
                s.gpuSeen.resize((size_t)p.frame + 1, 0);
            }
            s.gpu[(size_t)p.frame] += (float)((double)(t1 - t0) * 1e-6);
            s.gpuSeen[(size_t)p.frame] = 1;
            freeQueries.push_back(p.begin);
            freeQueries.push_back(p.end);
        }
        pending.resize(kept);
    }
#endif

    struct Stats
    {
        float median = 0, p95 = 0, minimum = 0, maximum = 0, average = 0;
        int count = 0;
    };

    Stats Compute(std::vector<float> values)
    {
        Stats s;
        s.count = (int)values.size();
        if (values.empty()) return s;
        std::sort(values.begin(), values.end());
        double sum = 0.0;
        for (float v : values) sum += v;
        const size_t n = values.size();
        s.median = n % 2 ? values[n / 2] : 0.5f * (values[n / 2 - 1] + values[n / 2]);
        s.p95 = values[(size_t)std::max(0, (int)std::ceil(0.95 * (double)n) - 1)];
        s.minimum = values.front();
        s.maximum = values.back();
        s.average = (float)(sum / (double)n);
        return s;
    }
}

namespace Profile
{
    void Enable(bool on) { enabled = on; }
    bool Enabled() { return enabled; }

    void SetGpuSync(bool on) { gpuSync = on; }

    bool SetGpuTimer(bool on)
    {
        gpuTimer = false;
        if (!on) return false;
#if defined(ZM_GPU_TIMER)
        if (rlGetVersion() != RL_OPENGL_33 && rlGetVersion() != RL_OPENGL_43)
        {
            TraceLog(LOG_WARNING, "PROFILE: --gpu-timer needs desktop OpenGL 3.3+, disabled");
            return false;
        }
        genQueries = (GenQueries)glfwGetProcAddress("glGenQueries");
        queryCounter = (QueryCounter)glfwGetProcAddress("glQueryCounter");
        getQueryObjectiv = (GetQueryObjectiv)glfwGetProcAddress("glGetQueryObjectiv");
        getQueryObjectui64v = (GetQueryObjectui64v)glfwGetProcAddress("glGetQueryObjectui64v");
        if (!genQueries || !queryCounter || !getQueryObjectiv || !getQueryObjectui64v)
        {
            TraceLog(LOG_WARNING, "PROFILE: GL timestamp queries unavailable, --gpu-timer disabled");
            return false;
        }
        gpuTimer = true;
        return true;
#else
        TraceLog(LOG_WARNING, "PROFILE: --gpu-timer is not supported on this platform (Android/WebGL), disabled");
        return false;
#endif
    }

    void BeginMeasurement()
    {
        if (!enabled) return;
        sections.clear();
        frameTimes.clear();
        frameIndex = 0;
#if defined(ZM_GPU_TIMER)
        pending.clear(); // (warm-up-ийн асуулгууд хэзээ ч үүсээгүй: хэмжилтийн үед л)
#endif
        measuring = true;
        lastFrameEnd = GetTime();
    }

    bool Measuring() { return measuring; }

    void SetShadowPass(bool on) { shadowPass = on; }
    bool ShadowPass() { return shadowPass; }

    void Add(const char* name, double seconds)
    {
        if (!enabled || !measuring) return;
        Section& section = sections[(size_t)FindSection(name)];
        section.frame += seconds;
        ++section.frameCalls;
    }

    Token Begin(const char* section)
    {
        Token token;
        if (!enabled || !measuring) return token;
        token.name = section;
        if (gpuSync) Synchronize();
#if defined(ZM_GPU_TIMER)
        if (gpuTimer)
        {
            rlDrawRenderBatchActive();
            token.gpu = (int)TakeQuery();
            queryCounter((unsigned int)token.gpu, kTimestamp);
        }
#endif
        token.start = GetTime();
        return token;
    }

    void End(Token& token)
    {
        if (!token.name) return;
        if (gpuSync) Synchronize();
        const double elapsed = GetTime() - token.start;
#if defined(ZM_GPU_TIMER)
        if (gpuTimer && token.gpu >= 0)
        {
            rlDrawRenderBatchActive();
            const unsigned int end = TakeQuery();
            queryCounter(end, kTimestamp);
            pending.push_back({ frameIndex, FindSection(token.name), (unsigned int)token.gpu, end });
        }
#endif
        Add(token.name, elapsed);
        token.name = nullptr;
    }

    void EndFrame()
    {
        if (!enabled || !measuring) return;
        const double now = GetTime();
        frameTimes.push_back((float)((now - lastFrameEnd) * 1000.0));
        lastFrameEnd = now;
        for (Section& section : sections)
        {
            section.samples.push_back((float)(section.frame * 1000.0));
            section.calls += section.frameCalls;
            section.frame = 0.0;
            section.frameCalls = 0;
        }
        ++frameIndex;
#if defined(ZM_GPU_TIMER)
        if (gpuTimer) Resolve(false);
#endif
    }

    void Report(int warmupFrames)
    {
        if (!enabled || frameTimes.empty()) return;
        const char* mode = gpuSync ? "GPU-SYNC (glFinish at section boundaries: synchronized CPU time, NOT gameplay performance)"
                         : gpuTimer ? "GPU-TIMER (rlgl batch flushed at section boundaries; CPU and GPU reported separately)"
                                    : "NORMAL (CPU submission time; GPU waits land wherever the driver blocks)";
        TraceLog(LOG_INFO, "PROFILE: mode %s", mode);
        TraceLog(LOG_INFO, "PROFILE: %d measured frames after %d warm-up frames", (int)frameTimes.size(), warmupFrames);

        // Кадрын хугацаа ба FPS (тусад нь)
        const Stats frame = Compute(frameTimes);
        double total = 0.0;
        for (float t : frameTimes) total += t;
        TraceLog(LOG_INFO, "FRAME: time median %.2f ms, p95 %.2f ms, min %.2f ms, max %.2f ms, avg %.2f ms (n=%d)", frame.median,
                 frame.p95, frame.minimum, frame.maximum, frame.average, frame.count);
        TraceLog(LOG_INFO, "FRAME: FPS average %.1f (frames / total time), minimum %.1f (1000 / slowest frame), "
                           "median-derived %.1f (1000 / median frame time)",
                 1000.0 * (double)frame.count / total, 1000.0 / std::max(0.001f, frame.maximum), 1000.0 / std::max(0.001f, frame.median));

        // Хэсгүүд (дундажаар эрэмбэлсэн). Кадр бүрт тухайн хэсэг дуудагдаагүй бол 0.
        std::vector<std::pair<Stats, const Section*>> rows;
        for (const Section& section : sections) rows.push_back({ Compute(section.samples), &section });
        std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.first.average > b.first.average; });
        TraceLog(LOG_INFO, "PROFILE: %-30s %8s %8s %8s %8s %8s %6s %8s", "section (CPU ms per frame)", "median", "p95", "min", "max",
                 "avg", "n", "calls/f");
        for (const auto& row : rows)
        {
            const Stats& s = row.first;
            TraceLog(LOG_INFO, "PROFILE: %-30s %8.3f %8.3f %8.3f %8.3f %8.3f %6d %8.1f", row.second->name, s.median, s.p95, s.minimum,
                     s.maximum, s.average, s.count, (double)row.second->calls / (double)std::max(1, s.count));
        }

#if defined(ZM_GPU_TIMER)
        if (gpuTimer)
        {
            Resolve(true);
            TraceLog(LOG_INFO, "PROFILE GPU: %-26s %8s %8s %8s %8s %8s %6s", "section (GPU ms per frame)", "median", "p95", "min", "max",
                     "avg", "n");
            for (const auto& row : rows)
            {
                const Section& section = *row.second;
                std::vector<float> values;
                for (size_t f = 0; f < section.gpu.size(); ++f)
                {
                    if (section.gpuSeen[f]) values.push_back(section.gpu[f]);
                }
                if (values.empty()) continue;
                const Stats s = Compute(values);
                TraceLog(LOG_INFO, "PROFILE GPU: %-26s %8.3f %8.3f %8.3f %8.3f %8.3f %6d", section.name, s.median, s.p95, s.minimum,
                         s.maximum, s.average, s.count);
            }
        }
#endif

        // Зогсолтууд: хаяхгүй, тусад нь. Тухайн кадрын хамгийн их хугацаатай хоёр хэсэг.
        const float threshold = std::max(100.0f, frame.median * 4.0f);
        int stalls = 0;
        for (size_t f = 0; f < frameTimes.size(); ++f)
        {
            if (frameTimes[f] < threshold) continue;
            ++stalls;
            const Section* top[2] = { nullptr, nullptr };
            for (const Section& section : sections)
            {
                if (section.samples.size() <= f) continue;
                const float v = section.samples[f];
                if (!top[0] || v > top[0]->samples[f])
                {
                    top[1] = top[0];
                    top[0] = &section;
                }
                else if (!top[1] || v > top[1]->samples[f])
                {
                    top[1] = &section;
                }
            }
            TraceLog(LOG_INFO, "STALL: measured frame %d (overall frame %d) %.1f ms; largest sections: %s %.1f ms, %s %.1f ms%s", (int)f,
                     (int)f + warmupFrames, frameTimes[f], top[0] ? top[0]->name : "-", top[0] ? top[0]->samples[f] : 0.0f,
                     top[1] ? top[1]->name : "-", top[1] ? top[1]->samples[f] : 0.0f,
                     f < 5 ? " (right after warm-up: likely loading)" : " (unexplained by profiled sections if their sum is small)");
        }
        TraceLog(LOG_INFO, "STALL: %d frames over %.0f ms (threshold = max(100 ms, 4 x median))", stalls, threshold);
    }
}
