#include "Game.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#if !defined(__ANDROID__)
namespace
{
    // Тоглоомын лог: %LOCALAPPDATA%\ZombieMongolia\log.txt (сүүлийн ажиллуулалт). Өөр компьютер дээр гарсан
    // асуудлыг (shader, видео карт, гацалт) оношлоход хэрэгтэй. Консол руу ч бичнэ.
    FILE* logFile = nullptr;

    void LogToFile(int level, const char* text, va_list args)
    {
        static const char* const names[] = { "ALL", "TRACE", "DEBUG", "INFO", "WARNING", "ERROR", "FATAL", "NONE" };
        const char* name = level >= 0 && level < 8 ? names[level] : "LOG";
        va_list copy;
        va_copy(copy, args);
        std::printf("%s: ", name);
        std::vprintf(text, args);
        std::printf("\n");
        if (logFile)
        {
            std::fprintf(logFile, "%s: ", name);
            std::vfprintf(logFile, text, copy);
            std::fprintf(logFile, "\n");
            std::fflush(logFile);
        }
        va_end(copy);
    }

    void OpenLog()
    {
        const char* local = std::getenv("LOCALAPPDATA");
        if (!local || !*local) return;
        const std::string path = std::string(local) + "\\ZombieMongolia\\log.txt";
        logFile = std::fopen(path.c_str(), "w");
        SetTraceLogCallback(LogToFile);
    }
}
#endif

// Сонголтгүй ажиллуулбал ердийн тоглоом. Сонголтуудыг Game.h-ийн LaunchOptions-оос үз.
int main(int argc, char** argv)
{
#if !defined(__ANDROID__)
    OpenLog();
#endif
    LaunchOptions options;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--msaa") == 0)
        {
            options.forceMsaa = true;
        }
        else if (std::strcmp(argv[i], "--zombies") == 0 && i + 1 < argc)
        {
            options.zombies = std::atoi(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--soldiers") == 0 && i + 1 < argc)
        {
            options.soldiers = std::atoi(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--zoom") == 0 && i + 1 < argc)
        {
            options.cameraDistance = (float)std::atof(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--weapon") == 0 && i + 1 < argc)
        {
            options.weapon = std::atoi(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--fullmap") == 0 && i + 1 < argc)
        {
            options.fullMapZoom = (float)std::atof(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--province") == 0 && i + 1 < argc)
        {
            options.province = std::atoi(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--ride") == 0 && i + 1 < argc)
        {
            options.ride = std::atoi(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--timescale") == 0 && i + 1 < argc)
        {
            options.timeScale = (float)std::atof(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--member") == 0 && i + 1 < argc)
        {
            options.member = std::atoi(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--demo") == 0 && i + 4 < argc)
        {
            // Тоглогчийн дүрийг тогтсон байрлалд зурна: хурд, алхааны үе, онилгоо, үйлдлийн явц (-1 = үгүй)
            options.demo = true;
            options.demoMove = (float)std::atof(argv[++i]);
            options.demoPhase = (float)std::atof(argv[++i]);
            options.demoAim = (float)std::atof(argv[++i]);
            options.demoAction = (float)std::atof(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--use-frame") == 0 && i + 1 < argc)
        {
            options.useFrame = std::atoi(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--move") == 0 && i + 2 < argc)
        {
            options.moveForward = (float)std::atof(argv[++i]);
            options.moveRight = (float)std::atof(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--quota") == 0 && i + 2 < argc)
        {
            options.quotaSmall = std::atoi(argv[++i]);
            options.quotaBig = std::atoi(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--bindpose") == 0)
        {
            options.bindPose = true;
        }
        else if (std::strcmp(argv[i], "--touch") == 0)
        {
            options.touch = true;
        }
        else if (std::strcmp(argv[i], "--view-height") == 0 && i + 1 < argc)
        {
            options.viewHeight = std::atoi(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--stance") == 0 && i + 1 < argc)
        {
            options.stance = std::atoi(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--demo-extra") == 0 && i + 2 < argc)
        {
            options.demoDirection = (float)std::atof(argv[++i]);
            options.demoReload = (float)std::atof(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--hold") == 0 && i + 1 < argc)
        {
            options.hold = std::atoi(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--coop") == 0)
        {
            options.coopTest = true;
        }
        else if (std::strcmp(argv[i], "--scope") == 0)
        {
            options.scope = true;
        }
        else if (std::strcmp(argv[i], "--journal") == 0)
        {
            options.journal = true;
        }
        else if (std::strcmp(argv[i], "--shop") == 0)
        {
            options.shop = true;
        }
        else if (std::strcmp(argv[i], "--select") == 0)
        {
            options.characterSelect = true;
        }
        else if (std::strcmp(argv[i], "--airstrike") == 0)
        {
            options.airstrike = true;
        }
        else if (std::strcmp(argv[i], "--daytime") == 0 && i + 1 < argc)
        {
            options.dayTime = (float)std::atof(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--pause") == 0)
        {
            options.pause = true;
        }
        else if (std::strcmp(argv[i], "--autoplay") == 0)
        {
            options.autoPlay = true;
        }
        else if (std::strcmp(argv[i], "--stop-after") == 0 && i + 1 < argc)
        {
            options.stopAfter = std::atoi(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--save") == 0 && i + 1 < argc)
        {
            options.savePath = argv[++i];
        }
        else if (std::strcmp(argv[i], "--relay") == 0 && i + 1 < argc)
        {
            options.relay = argv[++i];
        }
        else if (std::strcmp(argv[i], "--join") == 0 && i + 1 < argc)
        {
            options.joinCode = std::atoi(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--campaign") == 0)
        {
            options.campaignMap = true;
        }
        else if (std::strcmp(argv[i], "--drift") == 0 && i + 1 < argc)
        {
            options.driftFrame = std::atoi(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--crash-test") == 0 && i + 1 < argc)
        {
            options.crashTest = std::atoi(argv[++i]) - 1;
        }
        else if (std::strcmp(argv[i], "--ride-heading") == 0 && i + 1 < argc)
        {
            options.rideHeading = (float)std::atof(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--graphics") == 0 && i + 1 < argc)
        {
            options.graphics = std::atoi(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--weather") == 0 && i + 1 < argc)
        {
            options.weather = std::atoi(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
        {
            options.frames = std::atoi(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc)
        {
            options.screenshotPath = argv[++i];
            // Заавал биш: x z yaw pitch
            if (i + 4 < argc && std::strncmp(argv[i + 1], "--", 2) != 0)
            {
                options.hasPose = true;
                options.x = (float)std::atof(argv[i + 1]);
                options.z = (float)std::atof(argv[i + 2]);
                options.yawDegrees = (float)std::atof(argv[i + 3]);
                options.pitchDegrees = (float)std::atof(argv[i + 4]);
                i += 4;
            }
        }
    }

    Game game(options);
    game.Run();
    return 0;
}
