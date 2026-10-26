#pragma once

#include "MeshBuilder.h"
#include "raylib.h"

#include <vector>

// Цаг агаар: үе (даалгавар) эхлэхэд аймгийн бүс нутгаас хамааран сонгогдоно.
enum class WeatherKind
{
    Clear,  // цэлмэг, цөөн үүл
    Cloudy, // үүлшинэ
    Rain,   // бороо
    Snow,   // цас
    Dust    // шороон шуурга
};

// Орчны уур амьсгал: тэнгэр (үүл, нар, сар, од), нарны чиглэлтэй гэрэлтүүлэг, зайн манан, алсын уулс,
// нарны жинхэнэ сүүдэр (shadow map), дэлгэцийн эффект (өнгө, vignette, bloom).
//
// Гэрэлтүүлэг ба мананг нэг хөнгөн shader хийдэг. Гадаргуугийн чиглэлийг (normal) shader нь
// пиксел бүрд байрлалын зөрүүгээс тооцдог тул DrawCube зэрэг энгийн дүрс болон өөрсдийн
// үүсгэсэн mesh бүгд ижилхэн, low-poly маягийн хавтгай гэрэлтүүлэгтэй болно. Газрын гадаргуу харин
// оройн normal-тай тул гөлгөр гэрэлтэнэ (TerrainMaterial), өвс салхинд найгана (DetailMaterial).
class Environment
{
public:
    // Цонх нээгдсэний дараа үүсгэнэ.
    Environment();
    ~Environment();

    Environment(const Environment&) = delete;
    Environment& operator=(const Environment&) = delete;

    // Өдөр шөнийн мөчлөг: t 0..1 (0 = өглөө, 0.3 = үд, 0.6 = үдэш, 0.8 = шөнө дунд). Тэнгэр, нарны гэрэл,
    // орчны гэрэл, манангийн өнгө, нягтыг шилжүүлж бүх shader-т тавина. Нарны чиглэл өөрчлөгдөхгүй (сүүдэр).
    void SetTimeOfDay(float t);
    float Night() const { return night; } // 0 = өдөр, 1 = шөнө

    void SetWeather(WeatherKind kind);
    // Аймгийн хуурайшил: 0 = ногоон (хойд ой), 1 = говь
    void SetDryness(float value);
    // Алсын уулсын өндөр: 1 = Алтайн цастай нуруу, 0.4 = зүүн талын намхан толгод
    void SetMountainScale(float scale);
    WeatherKind Weather() const { return weather; }

    // Үүл, салхины хөдөлгөөн (frame бүрд).
    void Update(float dt);
    float Time() const { return time; }
    Vector2 Wind() const { return wind; } // салхины чиглэл x хүч (м/с орчим)

    // 2D: BeginMode3D-ээс өмнө дуудна. Тэнгэрийн өнгөний шилжилт, үүл, нар (шөнө сар, од).
    void DrawSky(const Camera3D& camera) const;

    // BeginMode3D дотор: Begin/End хоёрын хооронд зурсан бүх зүйл гэрэлтэж, мананд уусна.
    void Begin(const Camera3D& camera) const;
    void End() const;

    // Алсын уулс: камерыг дагаж шилждэг дэвсгэр.
    void DrawMountains(Vector3 viewPosition) const;
    // Жинхэнэ газрын өндөртэй хотын эргэн тойрны газар (World::FarLand) — алсын уулсын оронд
    void DrawFarLand(const std::vector<MeshPiece>& pieces, const Camera3D& camera) const;

    // Mesh зурахад ашиглах material (орчны shader-тэй).
    const Material& GetMaterial() const { return material; }
    // Газрын гадаргуу (оройн normal-тай, гөлгөр гэрэлтүүлэг)
    const Material& TerrainMaterial() const { return terrainMaterial; }
    // Өвс, цэцэг, бут: оройн alpha = салхинд найгах хэмжээ (255 = хөдлөхгүй)
    const Material& DetailMaterial() const { return detailMaterial; }

    // Гаднаас оруулсан ястай загваруудын shader: текстур + жинхэнэ normal-аар гөлгөр гэрэлтүүлэг + манан.
    Shader CharacterShader() const { return characterShader; }
    Shader VehicleShader() const { return vehicleShader; } // текстуртай унааны загвар

    // Хөдөлдөг объектын (унаа) material: mesh-ийг rlgl-ийн одоогийн хувиргалтаар DrawMesh-ээр зурна.
    const Material& PropMaterial() const { return propMaterial; }

    // График: 2 = өндөр (сүүдэр, bloom), 1 = дунд (сүүдэр, өнгө засвар), 0 = бага (хуурамч сүүдэр, эффектгүй)
    void SetQuality(int level);
    int Quality() const { return quality; }

    // --- Нарны сүүдэр ---
    void SetShadowsEnabled(bool enabled) { shadowsEnabled = enabled; }
    bool ShadowsEnabled() const { return shadowsEnabled && shadowMap.id != 0; }
    // Өмнөх сүүдрийн зургийг хэрэглэхгүй (сүүдрийн давхаргагүй дэлгэц, жишээ нь дүр сонгох цонх)
    void ClearShadow() { shadowReady = false; }
    // Сүүдрийн зураг: center орчмыг нарны зүгээс зурна. Begin/End хооронд сүүдэр тусгах зүйлсийг
    // DepthMaterial-аар (эсвэл өөрсдийн shader-ээр) зурна. Нэг frame-д нэг удаа, Begin(camera)-с өмнө.
    bool BeginShadowPass(Vector3 center); // false: сүүдэр унтраалттай (шөнө г.м.), зурах шаардлагагүй
    void EndShadowPass();
    const Material& DepthMaterial() const { return depthMaterial; }
    float ShadowRadius() const { return shadowRadius; }

    // --- Дэлгэцийн эффект: 3D зургийг тусдаа текстурт зурж, өнгө засаад дэлгэцэнд гаргана ---
    void SetPostEnabled(bool enabled) { postEnabled = enabled; }
    // false бол шууд дэлгэцэнд зурна. lowRes: эффектгүй ("бага") үед ч бага нягтралтай текстурт зурж (FPS-ийг дээшлүүлэх)
    // DrawScene дэлгэцийн хэмжээнд сунгана; HUD нь дэлгэцийн нягтралаараа тод үлдэнэ.
    bool BeginScene(int width, int height, bool lowRes = false);
    void EndScene();
    void DrawScene() const;                 // ViewBegin дотор, HUD-оос өмнө

private:
    Shader shader = {};
    Shader characterShader = {};
    Shader vehicleShader = {};
    Shader propShader = {};
    Shader terrainShader = {};
    Shader detailShader = {};
    Shader skyShader = {};
    Shader depthShader = {};
    Shader postShader = {};
    Shader brightShader = {};
    Shader blurShader = {};
    Material material = {};
    Material propMaterial = {};
    Material terrainMaterial = {};
    Texture2D terrainTextures[4] = {}; // өвс, өвс+шороо, шороо, хад (assets/terrain)
    void LoadTerrainTextures();
    Material detailMaterial = {};
    Material depthMaterial = {};
    Mesh mountains = {};
    int offsetLocation = -1;
    float mountainScale = 0.8f;

    // Одоогийн цагийн өнгө (SetTimeOfDay)
    Color skyZenith = { 64, 112, 182, 255 };
    Color skyHorizon = { 196, 212, 226, 255 };
    Vector3 sunHaze = { 1.0f, 0.86f, 0.66f };
    float night = 0.0f;
    float golden = 0.0f;
    float lastApplied = -1.0f;
    WeatherKind weather = WeatherKind::Clear;
    float cloudCover = 0.35f;
    float time = 0.0f;
    Vector2 wind = { 0.7f, 0.35f };

    // Сүүдэр
    bool shadowsEnabled = true;
    RenderTexture2D shadowMap = {};
    int shadowSize = 0;
    float shadowRadius = 60.0f;
    Matrix lightViewProj = {};
    float dayShadow = 0.7f; // одоогийн цаг, цаг агаарын сүүдрийн хүч
    bool shadowReady = false;
    bool shadowPassActive = false;
    unsigned shadowFrame = 0;

    // Дэлгэцийн эффект
    bool postEnabled = true;
    bool bloomEnabled = true;
    RenderTexture2D scene = {};
    RenderTexture2D bloomA = {};
    RenderTexture2D bloomB = {};
    bool sceneActive = false;
    bool sceneShaded = false; // текстурыг эффектийн shader-ээр гаргана (үгүй бол зөвхөн сунгана)
    int quality = 2;
    bool shadersBroken = false; // видео карт эффектийн shader-ийг хүлээж аваагүй: зөвхөн "бага" график
};
