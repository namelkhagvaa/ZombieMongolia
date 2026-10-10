#include "Game.h"

#include <cstdlib>
#include <cstring>

// Сонголтгүй ажиллуулбал ердийн тоглоом. Сонголтуудыг Game.h-ийн LaunchOptions-оос үз.
int main(int argc, char** argv)
{
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
        else if (std::strcmp(argv[i], "--journal") == 0)
        {
            options.journal = true;
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
        else if (std::strcmp(argv[i], "--campaign") == 0)
        {
            options.campaignMap = true;
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
