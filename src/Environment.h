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
    // орчны гэрэл, манангийн өнгө, нягтыг шилжүүлж бүх shader-т тавина. Нар зүүнээс баруун тийш тэнгэрээр явна
    // (өглөө, орой урт сүүдэр); "бага" графикт нар хөдлөхгүй (барилгын хуурамч сүүдэр тогтмол чиглэлтэй).
    void SetTimeOfDay(float t);
    float Night() const { return night; } // 0 = өдөр, 1 = шөнө
    Vector3 SunDirection() const { return sunDirection; } // гэрлийн чиглэл (шөнө сарных)
    // Гэрэлтүүлэггүй зурдаг утаа, тоос, цусны өнгөнд үржүүлэх гэрэл (өдөр ~цагаан, үдэш дулаан, шөнө бараан хөх)
    Color ParticleLight() const { return particleLight; }

    // instant: даалгаврын эхэнд шууд (үгүй бол тэнгэр, манан, гадаргуу ~20 секундэд аажмаар шилжинэ)
    void SetWeather(WeatherKind kind, bool instant = false);
    // Бороо, цасны одоогийн хүч 0..1 (бороо шиврээд, хүчтэй орж, ээлжилнэ)
    float PrecipitationAmount() const { return precipitation; }
    // Камерын доорх газрын өндөр: намхан газрын манан (хотгор, голын хөндийд өтгөн)
    void SetGroundLevel(float y) { groundTarget = y; }
    // Аймгийн хуурайшил: 0 = ногоон (хойд ой), 1 = говь
    void SetDryness(float value);
    // Алсын уулсын өндөр: 1 = Алтайн цастай нуруу, 0.4 = зүүн талын намхан толгод
    void SetMountainScale(float scale);
    WeatherKind Weather() const { return weather; }

    // Үүл, салхины хөдөлгөөн (frame бүрд).
    void Update(float dt);
    float Time() const { return time; }
    Vector2 Wind() const { return wind; } // салхины чиглэл x хүч (м/с орчим)

    // 2D: BeginMode3D-ээс өмнө дуудна. Дэвсгэрийг тэнгэрийн хаяаны өнгөөр цэвэрлэнэ.
    void DrawSky(const Camera3D& camera) const;
    // Begin/End хооронд, бүх тунгалаг бус зүйлийн (газар, барилга, дүрс) дараа, хагас тунгалагаас өмнө:
    // тэнгэр (үүл, нар), шөнө од, сар. Гүнээр далдлагдсан пикселийг тооцохгүй.
    void DrawSkyDome(const Camera3D& camera) const;

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
    // Ойрын өтгөн өвс: материал (nullptr = унтраастай), зурах радиус, м2-д ногдох ир (графикийн түвшнээр)
    const Material* GrassMaterial() const { return grassShader.id ? &grassMaterial : nullptr; }
    float GrassRadius() const;
    float GrassDensity() const { return quality >= 2 ? 200.0f : 110.0f; }
    // Өвсийг түлхэх цэг (тоглогч): ир нь хажуу тийш налж зам гаргана
    void SetGrassPusher(Vector3 position) const;

    // Гаднаас оруулсан ястай загваруудын shader: текстур + жинхэнэ normal-аар гөлгөр гэрэлтүүлэг + манан.
    Shader CharacterShader() const { return characterShader; }
    Shader VehicleShader() const { return vehicleShader; } // текстуртай унааны загвар
    // Холын мод (instancing): nullptr = дэмжигдэхгүй (холын модыг хөнгөн хувилбараар үлдээнэ)
    const Material* TreeMaterial() const { return treeShader.id ? &treeMaterial : nullptr; }

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
    // Холын сүүдэр (зөвхөн "өндөр" график): ойрын сүүдрийн гадна ~4 дахин том хүрээ, бүдүүн нягтралтай.
    // Зөвхөн хөдөлдөггүй зүйлс (барилга, мод, газар); хэд хэдэн frame тутам. EndShadowPass-аар дуусгана.
    bool BeginFarShadowPass(Vector3 center);
    void EndShadowPass();
    const Material& DepthMaterial() const { return depthMaterial; }
    // Гүний буферийг цэвэрлэнэ (өнгийг үлдээнэ): 1-р этгээдийн зэвсэг, гарыг ертөнцөөс тусад нь зурахад
    void ClearDepth() const;
    // Сүүдрийн зурлагад дүрүүд: араг ястай, гэрэлтүүлэггүй (бүтэн дүрийн shader-ийн оронд)
    const Material& CharacterDepthMaterial() const { return characterDepthMaterial; }
    float ShadowRadius() const { return shadowRadius; }
    float FarShadowRadius() const { return farShadowRadius; }

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
    Shader treeShader = {};
    Material treeMaterial = {};
    Shader propShader = {};
    Shader terrainShader = {};
    Shader detailShader = {};
    Shader grassShader = {};
    Shader skyShader = {};
    Shader depthShader = {};
    Shader characterDepthShader = {};
    Shader postShader = {};
    Shader brightShader = {};
    Shader blurShader = {};
    Material material = {};
    Material propMaterial = {};
    Material terrainMaterial = {};
    Texture2D terrainTextures[4] = {}; // өвс, өвс+шороо, шороо, хад (assets/terrain)
    void LoadTerrainTextures();
    Texture2D surfaceTextures[3] = {}; // гадаргуугийн бүтэц: хана, дээвэр/зам, байгаль (assets/surfaces)
    void LoadSurfaceTextures();
    Material detailMaterial = {};
    Material grassMaterial = {};
    Material depthMaterial = {};
    Material characterDepthMaterial = {};
    Mesh mountains = {};
    int offsetLocation = -1;
    float mountainScale = 0.8f;

    // Одоогийн цагийн өнгө (SetTimeOfDay)
    Color skyZenith = { 64, 112, 182, 255 };
    Color skyHorizon = { 196, 212, 226, 255 };
    Vector3 sunHaze = { 1.0f, 0.86f, 0.66f };
    Vector3 sunLightColor = { 1.38f, 1.24f, 1.02f };
    Color particleLight = WHITE;
    Vector3 sunDirection = { 0.0f, 1.0f, 0.0f };    // гэрэл, сүүдрийн чиглэл (нар бага байхад 10°-аас доош буухгүй)
    Vector3 skySunDirection = { 0.0f, 1.0f, 0.0f }; // тэнгэр дэх нарны жинхэнэ байрлал (хаяанд жаргана)
    float night = 0.0f;
    float golden = 0.0f;
    float lastApplied = -1.0f;
    WeatherKind weather = WeatherKind::Clear;
    float cloudCover = 0.35f;
    float time = 0.0f;
    Vector2 wind = { 0.7f, 0.35f };

    // Цаг агаарын аажим шилжилт: өмнөх -> одоогийн (weatherMix 0..1), гадаргуу аажим норж, хатаж, цас хунгарлана
    WeatherKind previousWeather = WeatherKind::Clear;
    float weatherMix = 1.0f;
    float precipitation = 0.0f; // бороо, цасны одоогийн хүч
    float wetness = 0.0f;
    float snowCover = 0.0f;
    float appliedWeather[4] = { -1.0f, -1.0f, -1.0f, -1.0f }; // shader-т сүүлд тавьсан (mix, норолт, цас, газар)
    // Намхан газрын манан
    float groundLevel = 0.0f;
    float groundTarget = 0.0f;
    bool groundKnown = false;

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
    // Холын сүүдэр
    RenderTexture2D farShadowMap = {};
    int farShadowSize = 0;
    float farShadowRadius = 280.0f;
    Matrix farLightViewProj = {};
    bool farShadowReady = false;
    unsigned farShadowFrame = 0;
    bool FarShadowsEnabled() const { return ShadowsEnabled() && quality >= 2 && farShadowSize >= 0; } // (-1: ачаалж чадаагүй)
    // Сүүдрийн зураг руу нарны зүгээс зурж эхэлнэ (ойрын ба холын pass-ын нийтлэг хэсэг)
    void BeginLightPass(const RenderTexture2D& map, int slot, Vector3 center, float radius, int size, float distance, Matrix& viewProj);

    // Дэлгэцийн эффект
    bool postEnabled = true;
    bool bloomEnabled = true;
    RenderTexture2D scene = {};
    // Нягтрал (render scale) өөрчлөгдөхөд өмнөх хэмжээний сценийн зураг: буцаж ирэхэд дахин үүсгэхгүй (үүсгэх нь вэб,
    // сул төхөөрөмж дээр кадрыг гацаадаг байв). (зураг, гүн уншигдах эсэх)
    std::vector<std::pair<RenderTexture2D, bool>> sceneCache;
    RenderTexture2D bloomA = {};
    RenderTexture2D bloomB = {};
    bool sceneActive = false;
    bool sceneShaded = false; // текстурыг эффектийн shader-ээр гаргана (үгүй бол зөвхөн сунгана)
    int quality = 2;
    bool shadersBroken = false; // видео карт эффектийн shader-ийг хүлээж аваагүй: зөвхөн "бага" график

    // "Өндөр" график: булан, завсрын сүүдэр (SSAO) ба нарны туяа (сценийн гүнээс, хагас/дөрөвний нэг нягтралтай)
    Shader aoShader = {};
    Shader shaftShader = {};
    RenderTexture2D aoA = {};
    RenderTexture2D aoB = {};
    RenderTexture2D shafts = {};
    bool sceneHasDepth = false; // сцений гүн уншигдах текстурт
    bool screenEffects = false; // энэ кадрт AO, туяа тооцсон
    float shaftStrength = 0.0f;
    bool shaftVisible = false;  // энэ кадрт туяа зурсан (нар харагдаж байна)
    Vector2 sunScreen = {};
    mutable Camera3D sceneCamera = {}; // Begin()-д дамжсан камер (AO, нарны туяанд)
    void UnloadScreenEffects();
    void RenderScreenEffects();
};
