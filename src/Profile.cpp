#include "Profile.h"

#include "raylib.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    struct Section
    {
        const char* name;
        double total = 0.0;
        double worst = 0.0; // нэг frame дахь хамгийн их
        double frame = 0.0; // энэ frame-д
        int worstAt = 0;    // хамгийн удаан frame-ийн дугаар
    };
    bool enabled = false;
    std::vector<Section> sections;
    int frames = 0;
}

namespace Profile
{
    void Enable(bool on) { enabled = on; }
    bool Enabled() { return enabled; }

    void Add(const char* name, double seconds)
    {
        if (!enabled) return;
        for (Section& section : sections)
        {
            if (section.name == name || std::strcmp(section.name, name) == 0)
            {
                section.frame += seconds;
                return;
            }
        }
        Section section;
        section.name = name;
        section.frame = seconds;
        sections.push_back(section);
    }

    void EndFrame()
    {
        if (!enabled) return;
        ++frames;
        for (Section& section : sections)
        {
            section.total += section.frame;
            if (section.frame > section.worst)
            {
                section.worst = section.frame;
                section.worstAt = frames;
            }
            section.frame = 0.0;
        }
    }

    void Report()
    {
        if (!enabled || frames == 0) return;
        std::vector<Section> sorted = sections;
        std::sort(sorted.begin(), sorted.end(), [](const Section& a, const Section& b) { return a.total > b.total; });
        for (const Section& section : sorted)
        {
            TraceLog(LOG_INFO, "PROFILE: %-18s %6.2f ms avg  %6.2f ms worst (frame %d)", section.name, section.total * 1000.0 / frames,
                     section.worst * 1000.0, section.worstAt);
        }
        TraceLog(LOG_INFO, "PROFILE: %d frames", frames);
    }

    Scope::Scope(const char* section) : name(section), start(enabled ? GetTime() : 0.0) {}
    Scope::~Scope()
    {
        if (enabled) Add(name, GetTime() - start);
    }
}
