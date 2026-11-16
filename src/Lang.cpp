#include "Lang.h"

#include <algorithm>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <unordered_map>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
#elif defined(__ANDROID__)
#include <android_native_app_glue.h>
#include <jni.h>
extern "C" struct android_app* GetAndroidApp(void); // raylib-ийн Android давхаргаас
#elif defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

// Англи орчуулга (src/LangEn.cpp): { монгол, англи } хос
extern const char* const kEnglishPairs[][2];
extern const int kEnglishCount;

#if defined(ZM_IOS)
// src/PlatformIos.mm: App Store-ийн улс (SKStorefront, "MNG"), төхөөрөмжийн бүс, цагийн бүс, хэл
std::string IosRegionInfo();
#endif

namespace
{
    Language current = Language::Mongolian;
    bool inMongolia = true;
    int saved = -1;

    bool MongolianTimeZone(const std::string& zone)
    {
        return zone == "Asia/Ulaanbaatar" || zone == "Asia/Ulan_Bator" || zone == "Asia/Hovd" || zone == "Asia/Choibalsan";
    }

    // Бүс: { монголд байгаа эсэх, төхөөрөмжийн хэл монгол эсэх }
    struct Region
    {
        bool mongolia = true;
        bool mongolianLanguage = true;
    };

#if defined(__EMSCRIPTEN__)
    // Хөтөч: цагийн бүс (Монгол: Улаанбаатар, Ховд, Чойбалсан) 1, хөтчийн хэл монгол 2
    EM_JS(int, BrowserRegion, (), {
        var flags = 0;
        try {
            var zone = Intl.DateTimeFormat().resolvedOptions().timeZone || '';
            if (/^Asia\/(Ulaanbaatar|Ulan_Bator|Hovd|Choibalsan)$/.test(zone)) flags |= 1;
        } catch (e) {}
        try {
            var langs = navigator.languages || [navigator.language || ''];
            for (var i = 0; i < langs.length; i++) if (/^mn\b/i.test(langs[i])) flags |= 2;
        } catch (e) {}
        return flags;
    });
#endif

#if defined(__ANDROID__)
    std::string JavaString(JNIEnv* env, jobject text)
    {
        if (!text) return std::string();
        const char* chars = env->GetStringUTFChars((jstring)text, nullptr);
        std::string result = chars ? chars : "";
        env->ReleaseStringUTFChars((jstring)text, chars);
        return result;
    }
#endif

    Region DetectRegion()
    {
        Region region;
#if defined(_WIN32)
        // Windows-ийн бүс (Тохиргоо -> Цаг ба хэл -> Бүс), цагийн бүс, хэрэглэгчийн хэл
        bool mongolia = false;
        using GeoNameFn = int(WINAPI*)(LPWSTR, int);
        if (auto geoName = (GeoNameFn)(void*)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetUserDefaultGeoName"))
        {
            wchar_t geo[16] = {};
            if (geoName(geo, 16) > 0 && std::wcscmp(geo, L"MN") == 0) mongolia = true;
        }
        DYNAMIC_TIME_ZONE_INFORMATION zone = {};
        if (GetDynamicTimeZoneInformation(&zone) != TIME_ZONE_ID_INVALID &&
            (std::wcsstr(zone.TimeZoneKeyName, L"Ulaanbaatar") || std::wcsstr(zone.TimeZoneKeyName, L"Ulan Bator")))
        {
            mongolia = true;
        }
        wchar_t locale[LOCALE_NAME_MAX_LENGTH] = {};
        const bool language = GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH) > 0 && std::wcsncmp(locale, L"mn", 2) == 0;
        region.mongolia = mongolia;
        region.mongolianLanguage = language || mongolia;
#elif defined(__ANDROID__)
        // Утасны бүс, хэл (java.util.Locale), цагийн бүс (java.util.TimeZone)
        JNIEnv* env = nullptr;
        JavaVM* vm = GetAndroidApp()->activity->vm;
        if (vm->AttachCurrentThread(&env, nullptr) == JNI_OK && env)
        {
            env->PushLocalFrame(16);
            jclass localeClass = env->FindClass("java/util/Locale");
            jobject locale = env->CallStaticObjectMethod(localeClass, env->GetStaticMethodID(localeClass, "getDefault", "()Ljava/util/Locale;"));
            const std::string country = JavaString(env, env->CallObjectMethod(locale, env->GetMethodID(localeClass, "getCountry", "()Ljava/lang/String;")));
            const std::string language = JavaString(env, env->CallObjectMethod(locale, env->GetMethodID(localeClass, "getLanguage", "()Ljava/lang/String;")));
            jclass zoneClass = env->FindClass("java/util/TimeZone");
            jobject zone = env->CallStaticObjectMethod(zoneClass, env->GetStaticMethodID(zoneClass, "getDefault", "()Ljava/util/TimeZone;"));
            const std::string zoneId = JavaString(env, env->CallObjectMethod(zone, env->GetMethodID(zoneClass, "getID", "()Ljava/lang/String;")));
            if (env->ExceptionCheck()) env->ExceptionClear();
            env->PopLocalFrame(nullptr);
            region.mongolia = country == "MN" || MongolianTimeZone(zoneId);
            region.mongolianLanguage = language == "mn" || region.mongolia;
        }
#elif defined(__EMSCRIPTEN__)
        const int flags = BrowserRegion();
        region.mongolia = (flags & 1) != 0;
        region.mongolianLanguage = (flags & 3) != 0;
#elif defined(ZM_IOS)
        // "<App Store-ийн улс> <бүс> <цагийн бүс> <хэл>", жишээ нь "MNG MN Asia/Ulaanbaatar mn"
        const std::string info = IosRegionInfo();
        char store[8] = {}, country[8] = {}, zone[64] = {}, language[8] = {};
        std::sscanf(info.c_str(), "%7s %7s %63s %7s", store, country, zone, language);
        region.mongolia = std::strcmp(store, "MNG") == 0 || std::strcmp(country, "MN") == 0 || MongolianTimeZone(zone);
        region.mongolianLanguage = std::strcmp(language, "mn") == 0 || region.mongolia;
#endif
        return region;
    }

    const std::unordered_map<std::string, const char*>& EnglishTable()
    {
        static const std::unordered_map<std::string, const char*> table = [] {
            std::unordered_map<std::string, const char*> map;
            map.reserve((size_t)kEnglishCount * 2);
            for (int i = 0; i < kEnglishCount; ++i) map.emplace(kEnglishPairs[i][0], kEnglishPairs[i][1]);
            return map;
        }();
        return table;
    }
}

void LangInit(int savedValue)
{
    saved = savedValue;
    const Region region = DetectRegion();
    inMongolia = region.mongolia;
    if (const char* forced = std::getenv("ZM_LANG")) // туршилт: ZM_LANG=en (Монголоос гадуурх мэт), ZM_LANG=mn
    {
        inMongolia = std::strcmp(forced, "mn") == 0;
        current = inMongolia ? Language::Mongolian : Language::English;
        return;
    }
    if (inMongolia) current = Language::Mongolian; // Монголд үргэлж монгол
    else if (saved == 0 || saved == 1) current = saved == 1 ? Language::English : Language::Mongolian;
    else current = region.mongolianLanguage ? Language::Mongolian : Language::English;
}

Language CurrentLanguage() { return current; }
bool English() { return current == Language::English; }
bool LanguageSelectable() { return !inMongolia; }

void SetLanguage(Language language)
{
    if (inMongolia) return;
    current = language;
    saved = language == Language::English ? 1 : 0;
}

int LanguageSaveValue() { return saved; }

const char* Tr(const char* mongolian)
{
    if (current != Language::English || !mongolian) return mongolian;
    const auto& table = EnglishTable();
    const auto found = table.find(mongolian);
    if (found != table.end()) return found->second;
    // Туршилт (ZM_LANG_MISSES=1): орчуулгагүй монгол текстийг нэг удаа хэвлэнэ (нийлмэл мөр, шинэ текст олох)
    static const bool report = std::getenv("ZM_LANG_MISSES") != nullptr;
    if (report)
    {
        static std::unordered_map<std::string, bool> seen;
        bool cyrillic = false;
        for (const unsigned char* p = (const unsigned char*)mongolian; *p; ++p) cyrillic = cyrillic || (*p >= 0xD0 && *p <= 0xD3);
        if (cyrillic && !seen[mongolian])
        {
            seen[mongolian] = true;
            std::printf("LANG: missing \"%s\"\n", mongolian);
            std::fflush(stdout);
        }
    }
    return mongolian;
}

namespace
{
    // Англи горимд: загвар, мөн %s аргумент бүр (аймаг, даалгаврын нэр г.м.) хүснэгтээр орчуулагдана. Хөрвүүлэлт бүрийг
    // тусад нь snprintf-ээр (ижил тодорхойлолт, ижил төрлийн утга); монгол горимд энгийн vsnprintf.
    int LangVFormat(char* out, size_t size, const char* format, va_list args)
    {
        if (current != Language::English || !format) return std::vsnprintf(out, size, format ? format : "", args);
        const char* f = Tr(format);
        std::string result;
        char spec[32], piece[1024];
        for (const char* p = f; *p;)
        {
            if (*p != '%')
            {
                result += *p++;
                continue;
            }
            if (p[1] == '%')
            {
                result += '%';
                p += 2;
                continue;
            }
            const char* start = p++;
            while (*p && std::strchr("-+ #0", *p)) ++p;
            while (*p >= '0' && *p <= '9') ++p;
            if (*p == '.')
            {
                ++p;
                while (*p >= '0' && *p <= '9') ++p;
            }
            int longs = 0;
            bool sizeType = false;
            while (*p == 'l') { ++longs; ++p; }
            while (*p == 'h') ++p;
            if (*p == 'z') { sizeType = true; ++p; }
            const char conversion = *p ? *p++ : 0;
            const size_t length = std::min((size_t)(p - start), sizeof(spec) - 1);
            std::memcpy(spec, start, length);
            spec[length] = 0;
            switch (conversion)
            {
            case 'd': case 'i':
                if (longs >= 2) std::snprintf(piece, sizeof(piece), spec, va_arg(args, long long));
                else if (longs == 1) std::snprintf(piece, sizeof(piece), spec, va_arg(args, long));
                else if (sizeType) std::snprintf(piece, sizeof(piece), spec, va_arg(args, size_t));
                else std::snprintf(piece, sizeof(piece), spec, va_arg(args, int));
                break;
            case 'u': case 'x': case 'X': case 'o':
                if (longs >= 2) std::snprintf(piece, sizeof(piece), spec, va_arg(args, unsigned long long));
                else if (longs == 1) std::snprintf(piece, sizeof(piece), spec, va_arg(args, unsigned long));
                else if (sizeType) std::snprintf(piece, sizeof(piece), spec, va_arg(args, size_t));
                else std::snprintf(piece, sizeof(piece), spec, va_arg(args, unsigned int));
                break;
            case 'c': std::snprintf(piece, sizeof(piece), spec, va_arg(args, int)); break;
            case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A':
                std::snprintf(piece, sizeof(piece), spec, va_arg(args, double));
                break;
            case 's': std::snprintf(piece, sizeof(piece), spec, Tr(va_arg(args, const char*))); break;
            case 'p': std::snprintf(piece, sizeof(piece), spec, va_arg(args, void*)); break;
            default: std::snprintf(piece, sizeof(piece), "%s", spec); break;
            }
            result += piece;
        }
        if (size > 0)
        {
            const size_t count = std::min(result.size(), size - 1);
            std::memcpy(out, result.data(), count);
            out[count] = 0;
        }
        return (int)result.size();
    }
}

extern "C" const char* LangTextFormat(const char* format, ...)
{
    // raylib-ийн TextFormat шиг ээлжилсэн буфер (заагч дараагийн хэдэн дуудлага хүртэл хүчинтэй)
    static char buffers[16][2048];
    static int next = 0;
    char* buffer = buffers[next];
    next = (next + 1) % 16;
    va_list args;
    va_start(args, format);
    LangVFormat(buffer, sizeof(buffers[0]), format, args);
    va_end(args);
    return buffer;
}

extern "C" int LangSnprintf(char* out, size_t size, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    const int length = LangVFormat(out, size, format, args);
    va_end(args);
    return length;
}
