#pragma once

// Эхлэлийн дэлгэц: тоглоом ачаалж байх хооронд assets/splash.png зураг, доор нь явцын зурвас.
// Ачаалал нь үндсэн урсгалыг хаадаг тул алхам бүрт нэг кадр зурна.
namespace Splash
{
    void Begin(bool enabled);                    // цонх нээгдсэний дараа (фонт ачаалсны дараа)
    void Step(float progress, const char* text); // progress 0..1
    void Finish();                               // 100% хүргээд бүдгэрч алга болно, зургийг суллана
}

// Гишүүдийн жагсаалтад тавьж, дараагийн гишүүнийг үүсгэхийн өмнө явцыг харуулна.
struct SplashMark
{
    SplashMark(float progress, const char* text) { Splash::Step(progress, text); }
};
