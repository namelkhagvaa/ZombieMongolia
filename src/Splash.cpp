#include "Splash.h"

#include "Platform.h"
#include "UiStyle.h"

#include "raylib.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace
{
    bool active = false;
    Texture2D texture = {};
    float shown = 0.0f; // зурвасын одоо харагдаж буй хэмжээ
    double startTime = 0.0;
    std::string status;

    // Хөтөч: хуудас файлуудаа татахдаа зурвасыг 0..50% дүүргэдэг (web/shell.html); тоглоом үлдсэнийг үргэлжлүүлнэ.
#if defined(__EMSCRIPTEN__)
    const float kStartShare = 0.5f;
#else
    const float kStartShare = 0.0f;
#endif

    // Эхлэлийн дэлгэц хамгийн багадаа ингэж удаан харагдана (хэт хурдан анивчихгүй), сек
    const double kMinimumSeconds = 1.6;

    void LoadSplashTexture()
    {
#if defined(__ANDROID__)
        // APK-ийн assets-ийн үндсэнд байрлана (FileExists нь assets-ийг хардаггүй).
        texture = LoadTexture("splash.png");
#else
        const std::string application = GetApplicationDirectory();
        for (const std::string& folder : { std::string("assets/"), application + "assets/", application + "../assets/" })
        {
            const std::string path = folder + "splash.png";
            if (!FileExists(path.c_str())) continue;
            texture = LoadTexture(path.c_str());
            break;
        }
#endif
        if (texture.id != 0)
        {
            GenTextureMipmaps(&texture);
            SetTextureFilter(texture, TEXTURE_FILTER_TRILINEAR);
        }
    }

    // Нэг кадр: зургийг дэлгэцийг дүүргэхээр (cover), доор нь явцын зурвас. alpha < 1 үед бүдгэрнэ.
    void DrawFrame(float alpha)
    {
        const int width = GetScreenWidth(), height = GetScreenHeight();
        const float scale = (float)height / 720.0f; // UI-г 720 өндөрт тааруулж томруулна

        BeginDrawing();
        ClearBackground(BLACK);
        if (texture.id != 0)
        {
            const float cover = std::max((float)width / (float)texture.width, (float)height / (float)texture.height);
            const float w = (float)texture.width * cover, h = (float)texture.height * cover;
            DrawTexturePro(texture, { 0.0f, 0.0f, (float)texture.width, (float)texture.height },
                           { ((float)width - w) * 0.5f, ((float)height - h) * 0.5f, w, h }, { 0.0f, 0.0f }, 0.0f,
                           Fade(WHITE, alpha));
        }
        // Доод хэсгийг бараан болгож зурвас, бичгийг тодруулна
        const int shade = (int)(150.0f * scale);
        DrawRectangleGradientV(0, height - shade, width, shade, { 0, 0, 0, 0 }, Fade(BLACK, 0.8f * alpha));

        const float barWidth = std::min((float)width * 0.6f, 760.0f * scale);
        const Rectangle bar = { ((float)width - barWidth) * 0.5f, (float)height - 46.0f * scale, barWidth, 8.0f * scale };
        Ui::Bar(bar, shown, Fade(Ui::kAccent, alpha), Fade(WHITE, 0.16f * alpha));
        const int size = (int)(16.0f * scale);
        Ui::Text(status.c_str(), (int)bar.x, (int)(bar.y - 26.0f * scale), size, Fade(Ui::kText, alpha));
        const char* percent = TextFormat("%d%%", (int)std::lround(shown * 100.0f));
        Ui::TextBold(percent, (int)(bar.x + bar.width) - Ui::MeasureBold(percent, size), (int)(bar.y - 26.0f * scale), size,
                     Fade(Ui::kAccent, alpha));
        EndDrawing();
        FrameYield();
    }
}

namespace Splash
{
    void Begin(bool enabled)
    {
        active = enabled;
        if (!active)
        {
            WebHideLoader();
            return;
        }
        LoadSplashTexture();
        startTime = GetTime();
        shown = kStartShare;
        status = "Ачаалж байна...";
        DrawFrame(1.0f);
        WebHideLoader(); // хуудасны ачааллын дэлгэцийг ижил харагдах энэ дэлгэцээр солино
    }

    void Step(float progress, const char* text)
    {
        if (!active) return;
        shown = std::clamp(std::max(shown, kStartShare + (1.0f - kStartShare) * progress), 0.0f, 1.0f);
        status = text;
        TraceLog(LOG_INFO, "SPLASH: %3d%% %.2f s  %s", (int)(shown * 100.0f), GetTime() - startTime, text);
        DrawFrame(1.0f);
    }

    void Finish()
    {
        if (!active) return;
        active = false;
        status = "Бэлэн";
        // Үлдсэн хугацаанд зурвасыг 100% хүртэл жигд дүүргэнэ (хамгийн бага хугацаанаас богино бол)
        const float from = shown;
        const double fillStart = GetTime();
        const double fillSeconds = std::max(0.35, kMinimumSeconds - (fillStart - startTime));
        while (!AppShouldClose())
        {
            const float t = (float)std::min(1.0, (GetTime() - fillStart) / fillSeconds);
            shown = from + (1.0f - from) * t;
            DrawFrame(1.0f);
            if (t >= 1.0f) break;
        }
        // Бүдгэрэх
        const double fadeStart = GetTime();
        while (!AppShouldClose())
        {
            const float t = (float)std::min(1.0, (GetTime() - fadeStart) / 0.35);
            DrawFrame(1.0f - t);
            if (t >= 1.0f) break;
        }
        if (texture.id != 0) UnloadTexture(texture);
        texture = {};
        WebLoadingFinished(); // хөтөч: "ТОГЛОХ" товч (бүтэн дэлгэц хэрэглэгчийн үйлдлээр л нээгддэг)
    }
}
