#pragma once

// Хурдны хэмжилт: frame-ийн хэсэг бүрт зарцуулсан хугацааг хуримтлуулж, туршилтын (benchmark) төгсгөлд
// "PROFILE: ..." мөрөөр дундаж мс-ийг хэвлэнэ. Хэмжилт хийхгүй үед (Enable хийгээгүй) зардалгүй.
namespace Profile
{
    void Enable(bool on);
    bool Enabled();
    void Add(const char* name, double seconds);
    void EndFrame();
    void Report(); // TraceLog руу

    struct Scope
    {
        const char* name;
        double start;
        explicit Scope(const char* section);
        ~Scope();
    };
}

#define PROFILE_CAT2(a, b) a##b
#define PROFILE_CAT(a, b) PROFILE_CAT2(a, b)
#define PROFILE_SCOPE(name) Profile::Scope PROFILE_CAT(profileScope, __LINE__)(name)
