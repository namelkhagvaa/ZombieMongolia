#pragma once

// Хурдны хэмжилт (зөвхөн туршилтын горим, --screenshot): хэсэг бүрийн кадар тутмын хугацааг хадгалж, хэмжилтийн
// цонхны (warm-up-ийн дараах) төгсгөлд медиан, p95, min, max, дундаж, тоог "PROFILE: ..." мөрөөр хэвлэнэ. Мөн
// кадрын хугацаа, 100 мс-ээс (эсвэл медианы 4 дахинаас) урт зогсолтуудыг тусад нь жагсаана.
//
// Оношлогооны горимууд (анхдагчаар унтраалттай; ердийн тоглолтод хэзээ ч асахгүй):
//   --gpu-sync  : хэсэг бүрийн эхэнд, төгсгөлд rlgl batch-ийг хоосолж glFinish хийнэ. Хэсгийн CPU хугацаанд тэр
//                 хэсгийн GPU ажил бүрэн орно ("synchronized CPU" гэж тэмдэглэнэ). Кадрын нийт хугацаа удаашрана.
//   --gpu-timer : GL_TIMESTAMP асуулгаар хэсэг бүрийн GPU хугацаа (зөвхөн компьютер, OpenGL 3.3). Үр дүнг 3 кадрын
//                 дараа хүлээлгүйгээр уншина. rlgl batch-ийг хэсгийн хил дээр хоосолно (draw call бүлэглэлт өөрчлөгдөнө).
// Хэмжилт хийхгүй үед (Enable хийгээгүй) Scope нь зөвхөн нэг bool шалгалт.
namespace Profile
{
    void Enable(bool on);
    bool Enabled();
    // Оношлогооны горим: Enable-ийн дараа, хэмжилт эхлэхээс өмнө
    void SetGpuSync(bool on);
    bool SetGpuTimer(bool on); // дэмжигдэхгүй бол false (лог бичнэ)
    // Warm-up дууссан: бүх хуримтлалыг цэвэрлэж, энэ мөчөөс хойших кадруудыг л тоолно.
    void BeginMeasurement();
    bool Measuring();

    // Дүрийн сүүдрийн зурлага (Game::RenderShadows): дүрийн үе шатуудын нэрийг "(shadow)" тэмдэгтэй болгоно.
    void SetShadowPass(bool on);
    bool ShadowPass();

    void Add(const char* name, double seconds);
    void EndFrame();
    void Report(int warmupFrames);

    // Гараар эхлүүлж дуусгах хэмжилт (функцын дунд хэсэгт): Begin-ийн буцаасныг End-д өгнө.
    struct Token
    {
        const char* name = nullptr;
        double start = 0.0;
        int gpu = -1;
    };
    Token Begin(const char* section);
    void End(Token& token);

    struct Scope
    {
        Token token;
        explicit Scope(const char* section) : token(Begin(section)) {}
        ~Scope() { End(token); }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
    };
}

#define PROFILE_CAT2(a, b) a##b
#define PROFILE_CAT(a, b) PROFILE_CAT2(a, b)
#define PROFILE_SCOPE(name) Profile::Scope PROFILE_CAT(profileScope, __LINE__)(name)
// Дүрийн үе шат: сүүдрийн зурлагад өөр нэрээр (нэр нь тогтмол мөр байх ёстой)
#define PROFILE_CHARACTER(name) PROFILE_SCOPE(Profile::ShadowPass() ? name " (shadow)" : name)
#define PROFILE_CHARACTER_NAME(name) (Profile::ShadowPass() ? name " (shadow)" : name)
