#include "UiFont.h"

#include <vector>

namespace
{
    Font font = {};
    bool loaded = false;
    Font boldFont = {};
    bool boldLoaded = false;
    Font displayFont = {};
    bool displayLoaded = false;
    const int kBaseSize = 40; // ачаалах хэмжээ; жижиг томоор зурахад гөлгөр шүүлтээр масштаблагдана

    float Spacing(int size)
    {
        return loaded ? 0.0f : (float)size / 10.0f; // үндсэн фонтод raylib-ийн ердийн үсэг хоорондын зай
    }
}

void LoadUiFont()
{
    // Латин + кирилл (монгол хэлний Ө, Ү-г оролцуулаад) + цөөн тэмдэг
    std::vector<int> codepoints;
    for (int c = 32; c <= 126; ++c) codepoints.push_back(c);
    for (int c = 0x0400; c <= 0x045F; ++c) codepoints.push_back(c);
    for (int c : { 0x04AE, 0x04AF, 0x04E8, 0x04E9, 0x2014, 0x2013, 0x2116, 0x00B7, 0x00D7, 0x2022, 0x2190, 0x2191, 0x2192, 0x2193, 0x20AE })
        codepoints.push_back(c);

#if defined(__ANDROID__)
    // APK-ийн assets дотор хамт ирсэн чөлөөт лицензтэй фонт (FileExists нь assets-ийг хардаггүй тул шалгахгүй).
    const char* candidates[] = { "ui_font.ttf" };
    const bool checkExists = false;
#elif defined(__EMSCRIPTEN__)
    // Хөтөч: Android-тай ижил фонтыг хуудасны файлын системд урьдчилан ачаалсан (CMakeLists.txt)
    const char* candidates[] = { "ui_font.ttf" };
    const bool checkExists = true;
#else
    const char* candidates[] = {
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/tahoma.ttf",
    };
    const bool checkExists = true;
#endif
#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
    // Гарчгийн фонт: Bahnschrift (Windows 10+), нарийн DIN үсэг
    if (FileExists("C:/Windows/Fonts/bahnschrift.ttf"))
    {
        displayFont = LoadFontEx("C:/Windows/Fonts/bahnschrift.ttf", 48, codepoints.data(), (int)codepoints.size());
        if (displayFont.texture.id != 0 && displayFont.glyphCount > 100)
        {
            SetTextureFilter(displayFont.texture, TEXTURE_FILTER_BILINEAR);
            displayLoaded = true;
        }
    }
    // Тод фонт: Segoe UI Semibold, эсвэл Bold
    for (const char* path : { "C:/Windows/Fonts/seguisb.ttf", "C:/Windows/Fonts/segoeuib.ttf", "C:/Windows/Fonts/arialbd.ttf" })
    {
        if (!FileExists(path)) continue;
        boldFont = LoadFontEx(path, kBaseSize, codepoints.data(), (int)codepoints.size());
        if (boldFont.texture.id != 0 && boldFont.glyphCount > 100)
        {
            SetTextureFilter(boldFont.texture, TEXTURE_FILTER_BILINEAR);
            boldLoaded = true;
            break;
        }
    }
#endif

    for (const char* path : candidates)
    {
        if (checkExists && !FileExists(path)) continue;

        font = LoadFontEx(path, kBaseSize, codepoints.data(), (int)codepoints.size());
        if (font.texture.id != 0 && font.glyphCount > 100)
        {
            SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
            loaded = true;
            TraceLog(LOG_INFO, "UI: font %s loaded with Cyrillic glyphs", path);
            return;
        }
    }

    font = GetFontDefault();
    loaded = false;
    TraceLog(LOG_WARNING, "UI: no system font with Cyrillic found, Mongolian text will not display");
}

void UnloadUiFont()
{
    if (loaded)
    {
        UnloadFont(font);
        loaded = false;
    }
    if (boldLoaded)
    {
        UnloadFont(boldFont);
        boldLoaded = false;
    }
    if (displayLoaded)
    {
        UnloadFont(displayFont);
        displayLoaded = false;
    }
}

bool UiHasCyrillic()
{
    return loaded;
}

void UiText(const char* text, int x, int y, int size, Color color)
{
    DrawTextEx(loaded ? font : GetFontDefault(), text, { (float)x, (float)y }, (float)size, Spacing(size), color);
}

void UiTextShadow(const char* text, int x, int y, int size, Color color)
{
    UiText(text, x + 1, y + 1, size, Fade(BLACK, 0.8f));
    UiText(text, x, y, size, color);
}

int UiMeasure(const char* text, int size)
{
    return (int)MeasureTextEx(loaded ? font : GetFontDefault(), text, (float)size, Spacing(size)).x;
}

void UiTextBold(const char* text, int x, int y, int size, Color color)
{
    if (!boldLoaded)
    {
        UiText(text, x, y, size, color);
        return;
    }
    DrawTextEx(boldFont, text, { (float)x, (float)y }, (float)size, 0.0f, color);
}

void UiTextDisplay(const char* text, int x, int y, int size, Color color, float tracking)
{
    if (!displayLoaded)
    {
        UiTextBold(text, x, y, size, color);
        return;
    }
    // Bahnschrift-ийн үсгийн өндөр Segoe-оос бага: ижил хэмжээнд харагдуулахаар бага зэрэг томруулна
    const float scaled = (float)size * 1.08f;
    DrawTextEx(displayFont, text, { (float)x, (float)y - (float)size * 0.04f }, scaled, scaled * tracking, color);
}

int UiMeasureDisplay(const char* text, int size, float tracking)
{
    if (!displayLoaded) return UiMeasureBold(text, size);
    const float scaled = (float)size * 1.08f;
    return (int)MeasureTextEx(displayFont, text, scaled, scaled * tracking).x;
}

int UiMeasureBold(const char* text, int size)
{
    if (!boldLoaded)
    {
        return UiMeasure(text, size);
    }
    return (int)MeasureTextEx(boldFont, text, (float)size, 0.0f).x;
}
