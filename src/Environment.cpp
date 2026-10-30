#include "Environment.h"

#include "Input.h"

#include "Layout.h"
#include "MeshBuilder.h"
#include "Platform.h"
#include "Procedural.h"
#include "raymath.h"
#include "rlgl.h"

#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h> // сүүдрийн гүний текстур (LoadShadowTarget)
#endif

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    // --- Уур амьсгалын тохиргоо: Монголын цэлмэг хөх тэнгэр, хуурай тунгалаг агаар ---
    const Color kSunCore = { 255, 250, 236, 255 };
    const float kFogDensity = 0.0011f;   // 300 м-т ~10%, 600 м-т ~35%, 1000 м-т ~70% манан
    const int kShadowSlot = 9;           // сүүдрийн зургийн текстурын нэгж (материалын map-уудаас хол)
    const int kSurfaceSlot = 13;         // гадаргуугийн бүтэц: 13 хана, 14 дээвэр/зам, 15 байгаль (MAX_MATERIAL_MAPS-аас хол)

    // ------------------------------------------------------------ Shader-ууд

    // Бүх гэрэлтүүлэгтэй shader-ийн нийтлэг хэсэг: нарны сүүдэр, хагас бөмбөрцгийн (тэнгэр/газар) орчны гэрэл,
    // наранд гэрэлтдэг зайн манан.
    const char* kLightingCommon = R"(
uniform vec3 viewPos;
uniform vec3 sunDirection;
uniform vec3 sunLight;
uniform vec3 ambientLight;
uniform vec3 groundLight;
uniform vec3 fogColor;
uniform vec3 fogSunColor;
uniform float fogDensity;
uniform float fogScale;
uniform float fogFarStart; // > 0 бол энэ зайнаас цааш fogScale үйлчилнэ (ойрын газартай тасралтгүй)
uniform highp sampler2D shadowMap;
uniform mat4 lightVP;
uniform float shadowStrength;
uniform float shadowTexel;
uniform float shadowBias;
uniform float snowCover;
uniform float wetness;
uniform float dryness;
uniform float exposure;
uniform vec3 skyZenith;    // тэнгэрийн оргилын өнгө (шил, ус, нойтон замын тусгалд)
uniform float nightLight;  // 0 өдөр .. 1 шөнө (цонхны гэрэл)
uniform float cloudShadow; // газар дээгүүр гүйх үүлний сүүдрийн хүч (0 = унтраастай)
uniform float time;
uniform vec2 wind;

// Кино маягийн өнгөний шилжилт (ACES, Narkowicz): тод хэсэг зөөлөн цайрч "шатахгүй", бараан хэсэг гүн.
// Гэрлийг 1-ээс их болгож болно (хүчтэй нар) - эцсийн өнгө 0..1 хооронд үлдэнэ.
vec3 Tonemap(vec3 x)
{
    x *= exposure;
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

// Аймгийн хуурайшил: ногоон өвсийг шаргал, говьд элсэрхэг хүрэн болгоно (газар, өвсөнд).
vec3 Dry(vec3 albedo)
{
    float green = clamp((albedo.g - max(albedo.r, albedo.b)) * 16.0, 0.0, 1.0);
    vec3 sand = vec3(0.70, 0.62, 0.45) * (0.8 + 0.4 * dot(albedo, vec3(0.33)));
    return mix(albedo, sand, dryness * green);
}

float SunShadow(vec3 pos, float ndl)
{
    if (shadowStrength <= 0.0 || ndl <= 0.0) return 1.0;
    vec4 lp = lightVP * vec4(pos, 1.0);
    vec3 p = lp.xyz / lp.w * 0.5 + 0.5;
    vec2 edge = abs(p.xy - 0.5) * 2.0;
    float fade = smoothstep(0.82, 1.0, max(edge.x, edge.y));
    if (fade >= 1.0 || p.z >= 1.0) return 1.0;
    float depth = p.z - shadowBias * (1.0 + 4.0 * (1.0 - ndl));
    float lit = 0.0;
#ifdef SOFT_SHADOWS
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            lit += step(depth, texture(shadowMap, p.xy + vec2(float(x), float(y)) * shadowTexel).r);
        }
    }
    lit /= 9.0;
#else
    lit += step(depth, texture(shadowMap, p.xy + vec2(-0.5, -0.5) * shadowTexel).r);
    lit += step(depth, texture(shadowMap, p.xy + vec2(0.5, -0.5) * shadowTexel).r);
    lit += step(depth, texture(shadowMap, p.xy + vec2(-0.5, 0.5) * shadowTexel).r);
    lit += step(depth, texture(shadowMap, p.xy + vec2(0.5, 0.5) * shadowTexel).r);
    lit *= 0.25;
#endif
    return mix(mix(1.0, lit, shadowStrength), 1.0, fade);
}

// Газар дээгүүр салхинд гүйх үүлний сүүдэр (тэнгэрийн үүлтэй ойролцоо хэмжээ, хурдтай)
float CloudHash(vec2 p)
{
    p = fract(p * vec2(234.34, 435.35));
    p += dot(p, p + 34.23);
    return fract(p.x * p.y);
}
float CloudNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(CloudHash(i), CloudHash(i + vec2(1.0, 0.0)), u.x), mix(CloudHash(i + vec2(0.0, 1.0)), CloudHash(i + vec2(1.0, 1.0)), u.x), u.y);
}
float CloudShade(vec3 pos)
{
    if (cloudShadow <= 0.0) return 1.0;
    vec2 p = pos.xz * 0.0045 + wind * (time * 0.02);
    float n = CloudNoise(p) * 0.65 + CloudNoise(p * 2.3 + 7.1) * 0.35;
    return 1.0 - cloudShadow * smoothstep(0.48, 0.68, n);
}

// Тусгал: харах чиглэлийн ойлтоор тэнгэр (хаяа -> оргил) эсвэл газар
vec3 SkyReflection(vec3 r)
{
    return r.y >= 0.0 ? mix(fogColor, skyZenith, sqrt(r.y)) : mix(fogColor, groundLight, min(1.0, -r.y * 3.0));
}

vec3 Shade(vec3 albedo, vec3 normal, vec3 pos)
{
    // Цас: дээш харсан гадаргуу (газар, дээвэр) цайрна; бороо: нойтон, бараан
    float up = smoothstep(0.5, 0.85, normal.y);
    albedo = mix(albedo, vec3(0.86, 0.88, 0.92), snowCover * up);
    albedo *= 1.0 - wetness * 0.28 * up;
    float ndl = max(dot(normal, sunDirection), 0.0);
    float shadow = SunShadow(pos, ndl);
    vec3 ambient = mix(groundLight, ambientLight, normal.y * 0.5 + 0.5);
    return albedo * (ambient + sunLight * (ndl * shadow * CloudShade(pos)));
}

vec3 ApplyFog(vec3 color, vec3 pos)
{
    vec3 ray = pos - viewPos;
    float dist = length(ray);
    // fogFarStart > 0: тэр зайнаас цааш зайг fogScale дахин шахна (манан тасралтгүй, алсын уулс бүдэгхэн харагдана)
    float fogDist = fogFarStart > 0.0 ? min(dist, fogFarStart) + max(dist - fogFarStart, 0.0) * fogScale : dist * fogScale;
    float d = fogDist * fogDensity;
    float fog = 1.0 - exp(-d * d);
    float toward = max(dot(ray / max(dist, 0.001), sunDirection), 0.0);
    vec3 haze = mix(fogColor, fogSunColor, pow(toward, 6.0) * 0.7);
    return Tonemap(mix(color, haze, fog));
}
)";

    const char* kVertexShader = R"(in vec3 vertexPosition;
in vec4 vertexColor;
uniform mat4 mvp;
uniform vec3 worldOffset;
out vec3 fragPosition;
out vec4 fragColor;
void main()
{
    // Бүх геометр ертөнцийн координатад өгөгддөг (mesh-үүд хувиргалтгүй зурагдана).
    // Цорын ганц үл хамаарах зүйл нь камерыг дагадаг уулс: тэдний шилжилтийг worldOffset-оор дамжуулна.
    fragPosition = vertexPosition + worldOffset;
    fragColor = vertexColor;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

    const char* kFragmentShader = R"(in vec3 fragPosition;
in vec4 fragColor;
uniform vec4 colDiffuse;
uniform float surfaceDetail; // 0 = жигд өнгө (хөдөлдөг унаа), 1 = ойрын гадаргуугийн бүтэц
uniform float surfaceBump;   // 1 = ханын бүтэц товгор (нарны зүг рүү харсан ирмэг гэрэлтэй, эсрэг тал нь сүүдэртэй)
// Бодит гадаргуугийн бүтэц (assets/surfaces, CC0): саарал, дундаж 0.5. Өнгө нь оройн өнгөнөөс, ширхэг нь эндээс.
uniform sampler2D wallDetail;  // R шавардлага, G тоосго, B бетон, A банз
uniform sampler2D roofDetail;  // R вааран дээвэр, G атираат төмөр, B асфальт, A хавтан зам
uniform float surfaceTextures; // 0 = текстургүй (файл олдоогүй)
// 1 бол оройн alpha нь газраас дээших өндөр (MeshBuilder::EncodeGroundHeight, 0.4 м-ийн нэгж): барилга
uniform float aoMode;
out vec4 finalColor;

float SurfaceHash(vec2 p)
{
    p = fract(p * vec2(127.1, 311.7));
    p += dot(p, p + 19.19);
    return fract(p.x * p.y);
}
float SurfaceNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(SurfaceHash(i), SurfaceHash(i + vec2(1.0, 0.0)), u.x),
               mix(SurfaceHash(i + vec2(0.0, 1.0)), SurfaceHash(i + vec2(1.0, 1.0)), u.x), u.y);
}

// Ойрын гадаргуугийн бүтэц: материалыг өнгө, чиглэлээр нь танина (текстургүй).
//   асфальт, бетон (саарал, хэвтээ): ширхэг, засварын толбо, хагарал
//   хана (босоо): дээрээс урссан бохирын судал
//   бүгд: том, жижиг толбо (өнгө жигд биш)
vec3 SurfaceDetail(vec3 albedo, vec3 normal, vec3 pos)
{
    float dist = distance(viewPos, pos);
    float near = (1.0 - smoothstep(30.0, 120.0, dist)) * surfaceDetail;
    if (near <= 0.0) return albedo;
    vec3 an = abs(normal);
    vec2 uv = an.y > 0.7 ? pos.xz : (an.x > an.z ? pos.zy : pos.xy);
    if (surfaceTextures > 0.5)
    {
        // Материалыг өнгө, чиглэлээр нь танина: хана (улаан = тоосго, хүрэн бараан = банз, саарал = бетон, бусад =
        // шавардлага), налуу дээвэр (улаан хүрэн = вааран, бусад = атираат төмөр), хэвтээ (бараан саарал = асфальт,
        // цайвар саарал = хавтан). Зурагнаас нэг л удаа уншина (4 материал нэг текстурт).
        float lumC = dot(albedo, vec3(0.333));
        float satC = max(albedo.r, max(albedo.g, albedo.b)) - min(albedo.r, min(albedo.g, albedo.b));
        float red = albedo.r / max(albedo.g, 0.02);
        float grayW = 1.0 - smoothstep(0.05, 0.14, satC);
        float brick = smoothstep(1.22, 1.42, red) * (1.0 - grayW);
        float wood = smoothstep(1.04, 1.16, red) * (1.0 - smoothstep(1.3, 1.45, red)) * (1.0 - smoothstep(0.36, 0.5, lumC)) * (1.0 - grayW);
        float d = 0.5, mask = 0.0;
        if (an.y < 0.3)
        {
            vec4 t = texture(wallDetail, uv * 0.6);
            vec4 w = vec4(0.0, brick, grayW, wood);
            w.x = max(0.0, 1.0 - w.y - w.z - w.w);
            w /= max(1.0, w.x + w.y + w.z + w.w);
            d = dot(t, w);
            mask = 1.0;
            if (surfaceBump > 0.5 && near > 0.3)
            {
                // Товгор: нарны чиглэлийг ханын хавтгай дээр буулгаж, тэр зүгт ~1 см-ийн цаадах бүтэцтэй харьцуулна
                vec2 toSun = an.x > an.z ? vec2(sunDirection.z, sunDirection.y) : vec2(sunDirection.x, sunDirection.y);
                float ahead = dot(texture(wallDetail, (uv + toSun * 0.025) * 0.6), w);
                albedo *= 1.0 + clamp((d - ahead) * 2.2, -0.25, 0.25) * near * max(dot(normal, sunDirection), 0.0);
            }
        }
        else if (an.y < 0.85)
        {
            vec4 t = texture(roofDetail, pos.xz * 0.5);
            d = mix(t.g, t.r, smoothstep(1.15, 1.35, red) * (1.0 - grayW));
            mask = 1.0;
        }
        else
        {
            vec4 t = texture(roofDetail, pos.xz * 0.45);
            d = mix(t.b, t.a, smoothstep(0.38, 0.5, lumC));
            mask = grayW;
        }
        albedo *= 1.0 + (d - 0.5) * 1.3 * near * mask;
    }
    float broad = SurfaceNoise(uv * 0.35);
    float fine = SurfaceNoise(uv * 3.1);
    albedo *= 1.0 + near * ((broad - 0.5) * 0.18 + (fine - 0.5) * 0.08);

    float lum = dot(albedo, vec3(0.333));
    float sat = max(albedo.r, max(albedo.g, albedo.b)) - min(albedo.r, min(albedo.g, albedo.b));
    float paved = step(0.85, an.y) * (1.0 - smoothstep(0.05, 0.12, sat)) * (1.0 - smoothstep(0.4, 0.55, lum));
    if (paved > 0.0)
    {
        float grainNear = 1.0 - smoothstep(8.0, 22.0, dist); // ширхэг холдоход анивчихгүй
        float grain = SurfaceHash(floor(uv * 20.0)) - 0.5;
        float repair = smoothstep(0.64, 0.7, SurfaceNoise(uv * 0.16 + 3.0));
        float crackLine = 1.0 - smoothstep(0.0, 0.028, abs(SurfaceNoise(uv * 0.55 + 11.0) - 0.5));
        float crackZone = smoothstep(0.45, 0.75, SurfaceNoise(uv * 0.07 + 5.0));
        albedo *= 1.0 + paved * near * (grain * 0.14 * grainNear - repair * 0.16 - crackLine * crackZone * 0.32);
    }
    if (an.y < 0.3)
    {
        float streak = SurfaceNoise(vec2(uv.x * 1.6, uv.y * 0.12 + 4.0));
        albedo *= 1.0 - near * 0.14 * smoothstep(0.55, 0.9, streak);
    }
    return albedo;
}

// Шил (цонх, бүхээг) ба усны гадаргуу: Fresnel-ээр тэнгэрийг тусгана (налуу харахад толь шиг), нарны тод гялбаа.
// lamp: шөнө дотроос гэрэл асна (цонх бүрийн өнгө, тод нь өөр).
vec3 Reflective(vec3 lit, vec3 n, vec3 pos, float base, float strength, bool lamp)
{
    vec3 view = normalize(viewPos - pos);
    if (dot(n, view) < 0.0) n = -n;
    float fresnel = base + (1.0 - base) * pow(1.0 - clamp(dot(n, view), 0.0, 1.0), 5.0);
    vec3 r = reflect(-view, n);
    float ndl = max(dot(n, sunDirection), 0.0);
    float glint = pow(max(dot(r, sunDirection), 0.0), 240.0) * SunShadow(pos, max(ndl, 0.05)) * CloudShade(pos);
    vec3 color = mix(lit, SkyReflection(r), clamp(fresnel * strength, 0.0, 0.92)) + sunLight * glint * 3.0;
    if (lamp && nightLight > 0.0)
    {
        // ~3 м-ийн нүд бүрт санамсаргүй: зарим цонх л гэрэлтэнэ (туузан цонхтой барилгад бүтэн давхар биш)
        float v = fract(sin(dot(floor(pos * 0.35), vec3(12.9898, 78.233, 37.719))) * 43758.5453);
        if (v > 0.45)
        {
            vec3 lampColor = mix(vec3(1.0, 0.66, 0.34), vec3(0.78, 0.84, 1.0), step(0.88, v)) * (0.55 + 0.5 * v);
            color = mix(color, lampColor, nightLight * 0.85);
        }
    }
    return color;
}

// Нойтон асфальт, бетон (бороо): тэнгэр, нарыг бүдэг тусгана
vec3 WetSheen(vec3 lit, vec3 vertexAlbedo, vec3 n, vec3 pos)
{
    float lum = dot(vertexAlbedo, vec3(0.333));
    float sat = max(vertexAlbedo.r, max(vertexAlbedo.g, vertexAlbedo.b)) - min(vertexAlbedo.r, min(vertexAlbedo.g, vertexAlbedo.b));
    // Асфальт (бараан) бүрэн, цайвар хавтан зам, талбай сулхан
    float paved = step(0.85, n.y) * (1.0 - smoothstep(0.05, 0.12, sat)) * (1.0 - 0.6 * smoothstep(0.4, 0.6, lum));
    if (paved <= 0.0) return lit;
    float puddle = smoothstep(0.45, 0.75, CloudNoise(pos.xz * 0.35 + 4.0)); // шалбааг илүү тод
    return mix(lit, Reflective(lit, n, pos, 0.02, 1.0, false), paved * wetness * (0.35 + 0.5 * puddle));
}

// Хананы ёроол газартай нийлэх хэсэг харанхуй (булангийн сүүдэр): height нь газраас дээших өндөр, м
float GroundOcclusion(float height, vec3 n)
{
    float wall = 1.0 - smoothstep(0.35, 0.7, abs(n.y));
    return mix(1.0, mix(0.6, 1.0, smoothstep(0.0, 1.4, height)), wall);
}

void main()
{
    // Гадаргуугийн чиглэл: хөрш пикселүүдийн байрлалын зөрүүгээс (камер руу харсан тал).
    vec3 normal = normalize(cross(dFdx(fragPosition), dFdy(fragPosition)));
    // Оройн alpha-ийн тэмдэг: 251 шил, 252 шөнө гэрэл асдаг шил, 190 ус (хагас тунгалаг)
    float tag = floor(fragColor.a * 255.0 + 0.5);
    bool glass = tag > 250.5 && tag < 252.5;
    bool water = aoMode < 0.5 && abs(tag - 190.0) < 0.5;
    vec3 vertexAlbedo = fragColor.rgb * colDiffuse.rgb;
    vec3 albedo = glass ? vertexAlbedo : SurfaceDetail(vertexAlbedo, normal, fragPosition);
    vec3 lit = Shade(albedo, normal, fragPosition);
    float alpha = fragColor.a * colDiffuse.a;
    if (glass)
    {
        // Цонхны хүрээ: хэвтээ чиглэлд 1.6 м тутам нарийн бараан босоо зураас (туузан цонх тусдаа цонхнууд болно)
        float along = abs(normal.x) > abs(normal.z) ? fragPosition.z : fragPosition.x;
        float frame = step(0.93, fract(along / 1.6)) * (1.0 - smoothstep(0.5, 0.8, abs(normal.y)));
        lit = Reflective(lit, normal, fragPosition, 0.08, 0.85, tag > 251.5);
        lit = mix(lit, Shade(vec3(0.22, 0.22, 0.23), normal, fragPosition), frame);
        alpha = colDiffuse.a;
    }
    else if (water)
    {
        lit = Reflective(lit, normal, fragPosition, 0.05, 0.9, false);
        alpha = mix(alpha, 1.0, 0.5); // тусгал ихтэй тул бараг тунгалаг бус
    }
    else
    {
        if (aoMode > 0.5)
        {
            lit *= GroundOcclusion(fragColor.a * 255.0 * 0.4, normal);
            alpha = colDiffuse.a;
        }
        if (wetness > 0.0) lit = WetSheen(lit, vertexAlbedo, normal, fragPosition);
    }
    finalColor = vec4(ApplyFog(lit, fragPosition), alpha);
}
)";

    // Газрын гадаргуу: оройн normal-тай тул толгод гөлгөр гэрэлтэнэ.
    const char* kTerrainVertexShader = R"(in vec3 vertexPosition;
in vec3 vertexNormal;
in vec4 vertexColor;
uniform mat4 mvp;
uniform vec3 worldOffset;
out vec3 fragPosition;
out vec3 fragNormal;
out vec4 fragColor;
void main()
{
    fragPosition = vertexPosition + worldOffset; // алсын уулс камерыг дагана
    fragNormal = vertexNormal;
    fragColor = vertexColor;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

    const char* kTerrainFragmentShader = R"(in vec3 fragPosition;
in vec3 fragNormal;
in vec4 fragColor;
uniform vec4 colDiffuse;
out vec4 finalColor;

float TerrainHash(vec2 p)
{
    p = fract(p * vec2(127.1, 311.7));
    p += dot(p, p + 19.19);
    return fract(p.x * p.y);
}
float TerrainNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(TerrainHash(i), TerrainHash(i + vec2(1.0, 0.0)), u.x),
               mix(TerrainHash(i + vec2(0.0, 1.0)), TerrainHash(i + vec2(1.0, 1.0)), u.x), u.y);
}

// Газрын бодит текстурууд (assets/terrain, CC0): өвс, өвс+шороо, хуурай шороо, хад.
// Текстурын өнгийг шууд биш, "дундажтайгаа харьцуулсан нарийн бүтэц" болгон оройн өнгөнд үржүүлнэ:
// аймгийн өнгө (хуурайшил, говь) хэвээр, бодит ширхэг, толбо, өнгөний хэлбэлзэл нэмэгдэнэ.
uniform sampler2D texture0; // өвс
uniform sampler2D texture1; // өвс + шороо
uniform sampler2D texture2; // хуурай шороо
uniform sampler2D texture3; // хад
uniform float terrainTextures; // 0 = текстургүй (файл олдоогүй)
// Хуурай газрын нэмэлт бүтэц (assets/surfaces, CC0, саарал): R элс, G хайрга, B жижиг чулуу, A хагарсан хөрс
uniform sampler2D natureDetail;
uniform float natureTextures;
uniform vec3 terrainMean[4];   // текстур бүрийн дундаж өнгө (ачаалахдаа нэг удаа тооцно)
// Алсын газар (60 м тор): камерын ойр, тоглох талбай дотор ойрын нарийн газар зурагддаг тул энд зурахгүй.
// Бүдүүн тор нь тэгшилсэн бүс, голын сав дээр ойрын газраас дээгүүр гарч тоглогчийг "газар доор" харагдуулдаг.
uniform float farCutRadius;    // > 0 бол камераас ийм хэвтээ зайд ...
uniform float farCutHalf;      // ... бөгөөд |x|, |z| нь үүнээс бага пикселийг хаяна

vec3 TerrainDetail(sampler2D t, vec2 uv, vec3 mean)
{
    return texture(t, uv).rgb / max(mean, vec3(0.04));
}

void main()
{
    if (farCutRadius > 0.0 && distance(viewPos.xz, fragPosition.xz) < farCutRadius &&
        max(abs(fragPosition.x), abs(fragPosition.z)) < farCutHalf) discard;
    vec3 albedo = fragColor.rgb * colDiffuse.rgb;
    vec3 normal = normalize(fragNormal);
    float dist = distance(viewPos, fragPosition);
    vec2 p = fragPosition.xz;
    if (terrainTextures > 0.5)
    {
        // Ногоон өнгөтэй газар өвс, бусад нь шороо; эгц налуу хад
        float green = clamp((albedo.g - max(albedo.r, albedo.b)) * 9.0 + 0.15, 0.0, 1.0);
        float blotch = TerrainNoise(p * 0.045 + 3.0);
        vec3 grass = mix(TerrainDetail(texture0, p * 0.33, terrainMean[0]), TerrainDetail(texture1, p * 0.11, terrainMean[1]),
                         smoothstep(0.38, 0.72, blotch));
        vec3 detail = mix(TerrainDetail(texture2, p * 0.21, terrainMean[2]), grass, green);
        float steep = smoothstep(0.86, 0.62, normal.y);
        if (steep > 0.0)
        {
            vec2 side = abs(normal.x) > abs(normal.z) ? fragPosition.zy : fragPosition.xy;
            detail = mix(detail, TerrainDetail(texture3, side * 0.12, terrainMean[3]), steep);
            albedo = mix(albedo, vec3(0.42, 0.40, 0.37), steep * 0.55);
        }
        if (natureTextures > 0.5)
        {
            // Хуурай газар (ногоон биш): шаргал цайвар газар элс, бусад нь толбо толбоор хагарсан хөрс, хайрга,
            // жижиг чулуу ээлжилнэ (том хэмжээний толбоор: давталт харагдахгүй)
            vec4 n = texture(natureDetail, p * 0.22);
            float lumT = dot(albedo, vec3(0.333));
            float sandy = smoothstep(0.42, 0.6, lumT) * clamp(((albedo.r + albedo.g) * 0.5 - albedo.b) * 5.0, 0.0, 1.0);
            float zone = TerrainNoise(p * 0.028 + 21.0), zone2 = TerrainNoise(p * 0.041 - 9.0);
            float cracked = (1.0 - sandy) * smoothstep(0.55, 0.72, zone);
            float pebbles = (1.0 - sandy) * (1.0 - cracked) * smoothstep(0.52, 0.7, zone2);
            float gravel = (1.0 - sandy) * (1.0 - cracked) * (1.0 - pebbles) * 0.6;
            float rest = max(0.0, 1.0 - sandy - cracked - pebbles - gravel);
            float nd = dot(n, vec4(sandy, gravel, pebbles, cracked)) + 0.5 * rest;
            float fade = 1.0 - smoothstep(60.0, 220.0, dist);
            detail *= 1.0 + (nd - 0.5) * 1.4 * (1.0 - green) * fade;
        }
        // Бүтцийг тодруулна (зургийн дундаж өнгөнөөс зөрүүг 1.6 дахин)
        detail = max(mix(vec3(1.0), detail, 1.6), vec3(0.2));
        // Том хэмжээний толбо: текстурын давталт харагдахгүй; ногоон ба шаргал хэсгүүд ээлжилнэ
        float macro = TerrainNoise(p * 0.018 + 11.0);
        detail *= 0.8 + 0.4 * macro;
        detail *= mix(vec3(1.06, 1.0, 0.82), vec3(0.94, 1.03, 1.02), TerrainNoise(p * 0.009 - 4.0));
        // Холдох тусам бүтэц зөөлөрнө (анивчихгүй, уулс жигд)
        albedo *= mix(detail, vec3(1.0), smoothstep(160.0, 700.0, dist) * 0.7);
    }
    else
    {
        // Ойрын газрын нарийн бүтэц (өвсний толбо, хөрс): холдох тусам арилна (анивчихгүй)
        float near = 1.0 - smoothstep(40.0, 180.0, dist);
        if (near > 0.0)
        {
            float n = TerrainNoise(p * 0.45) * 0.55 + TerrainNoise(p * 2.3) * 0.45;
            float blotch = TerrainNoise(p * 0.11 + 7.0);
            albedo *= 1.0 + near * ((n - 0.5) * 0.24);
            albedo = mix(albedo, albedo * vec3(1.08, 1.02, 0.86), near * smoothstep(0.55, 0.8, blotch) * 0.6);
        }
    }
    vec3 lit = Shade(Dry(albedo), normal, fragPosition);
    finalColor = vec4(ApplyFog(lit, fragPosition), 1.0);
}
)";

    // Өвс, цэцэг, бут: оройн alpha < 1 бол салхинд найгана (alpha 255 = үндэс, ~155 = үзүүр).
    const char* kDetailVertexShader = R"(in vec3 vertexPosition;
in vec4 vertexColor;
uniform mat4 mvp;
uniform float time;
uniform vec2 wind;
out vec3 fragPosition;
out vec4 fragColor;
void main()
{
    float sway = clamp((1.0 - vertexColor.a) * 2.5, 0.0, 1.0);
    vec3 p = vertexPosition;
    float phase = time * 1.9 + p.x * 0.37 + p.z * 0.29;
    float gust = 0.55 + 0.45 * sin(time * 0.55 + p.x * 0.021 + p.z * 0.017);
    p.xz += wind * (sway * gust * (0.55 + 0.45 * sin(phase)) * 0.16);
    p.y -= sway * gust * 0.03;
    fragPosition = p;
    fragColor = vec4(vertexColor.rgb, 1.0);
    gl_Position = mvp * vec4(p, 1.0);
}
)";

    const char* kDetailFragmentShader = R"(in vec3 fragPosition;
in vec4 fragColor;
uniform vec4 colDiffuse;
out vec4 finalColor;
void main()
{
    // Нимгэн ир: хавтгай normal-ыг дээш чиглэлтэй хольж, нарны эсрэг тал нь хэт харлахгүй.
    vec3 faceNormal = normalize(cross(dFdx(fragPosition), dFdy(fragPosition)));
    vec3 normal = normalize(mix(faceNormal, vec3(0.0, 1.0, 0.0), 0.6));
    vec3 lit = Shade(Dry(fragColor.rgb * colDiffuse.rgb), normal, fragPosition);
    finalColor = vec4(ApplyFog(lit, fragPosition), 1.0);
}
)";

    // Хөдөлдөг объект (унаа, морь): mesh нь GPU-д нэг удаа хуулагдаж, байрлал, эргэлтээ matModel-оор авна.
    // Гэрэл, манангийн тооцоо нь орчны shader-тэй ижил (ертөнцийн байрлалаар).
    const char* kPropVertexShader = R"(in vec3 vertexPosition;
in vec4 vertexColor;
uniform mat4 mvp;
uniform mat4 matModel;
out vec3 fragPosition;
out vec4 fragColor;
void main()
{
    fragPosition = vec3(matModel * vec4(vertexPosition, 1.0));
    fragColor = vertexColor;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

    // Ястай загварууд: оройн normal-тай тул гэрэлтүүлэг нь гөлгөр, өнгө нь текстураас.
    const char* kCharacterVertexShader = R"(in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexBoneIds;
in vec4 vertexBoneWeights;
uniform mat4 mvp;
uniform mat4 matModel;
// Яс бүрийн хувиргалт 3 мөрөөр (affine 3x4): 72 яс = 216 вектор. mat4-өөр (288) бол Adreno GPU-ийн (Samsung,
// Xiaomi г.м.) vertex shader-ийн 256 векторын хязгаараас хэтэрч shader ачаалагдахгүй, дүрс хөдөлгөөнгүй болдог.
// Мөрүүдийг UploadBoneRows (CharacterModel.cpp) илгээнэ.
uniform vec4 boneRows[216];
out vec3 fragPosition;
out vec2 fragTexCoord;
out vec3 fragNormal;
void main()
{
    // Арьслалт: орой бүр дөрөв хүртэлх ясны хувиргалтын жигнэсэн нийлбэрээр хөдөлнө.
    ivec4 b = ivec4(vertexBoneIds) * 3;
    vec4 w = vertexBoneWeights;
    vec4 r0 = w.x * boneRows[b.x] + w.y * boneRows[b.y] + w.z * boneRows[b.z] + w.w * boneRows[b.w];
    vec4 r1 = w.x * boneRows[b.x + 1] + w.y * boneRows[b.y + 1] + w.z * boneRows[b.z + 1] + w.w * boneRows[b.w + 1];
    vec4 r2 = w.x * boneRows[b.x + 2] + w.y * boneRows[b.y + 2] + w.z * boneRows[b.z + 2] + w.w * boneRows[b.w + 2];
    vec4 p = vec4(vertexPosition, 1.0);
    vec4 position = vec4(dot(r0, p), dot(r1, p), dot(r2, p), 1.0);
    fragPosition = vec3(matModel * position);
    fragTexCoord = vertexTexCoord;
    fragNormal = mat3(matModel) * vec3(dot(r0.xyz, vertexNormal), dot(r1.xyz, vertexNormal), dot(r2.xyz, vertexNormal));
    gl_Position = mvp * position;
}
)";

    const char* kCharacterFragmentShader = R"(in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
out vec4 finalColor;
void main()
{
    vec3 normal = normalize(fragNormal);
    if (!gl_FrontFacing) normal = -normal; // хоёр талт гадаргуугийн ар тал
    // Сүүдэр талыг бүр харлуулахгүй: нарны эсрэг тал ч бага зэрэг гэрэлтэнэ.
    float ndl = max(dot(normal, sunDirection), 0.0);
    float diffuse = ndl * 0.85 * SunShadow(fragPosition, ndl) * CloudShade(fragPosition) + 0.15 * (0.5 + 0.5 * normal.y);
    vec3 albedo = texture(texture0, fragTexCoord).rgb * colDiffuse.rgb;
    vec3 ambient = mix(groundLight, ambientLight, normal.y * 0.5 + 0.5);
    vec3 lit = albedo * (ambient + sunLight * diffuse);
    finalColor = vec4(ApplyFog(lit, fragPosition), 1.0);
}
)";

    // Текстуртай унааны загвар (assets/cars, Tripo3D): ястай биш, оройн normal ба uv-тэй.
    const char* kVehicleVertexShader = R"(in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
uniform mat4 mvp;
uniform mat4 matModel;
out vec3 fragPosition;
out vec2 fragTexCoord;
out vec3 fragNormal;
void main()
{
    fragPosition = vec3(matModel * vec4(vertexPosition, 1.0));
    fragTexCoord = vertexTexCoord;
    fragNormal = mat3(matModel) * vertexNormal;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

    const char* kVehicleFragmentShader = R"(in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
out vec4 finalColor;
void main()
{
    vec3 normal = normalize(fragNormal);
    if (!gl_FrontFacing) normal = -normal;
    vec3 albedo = texture(texture0, fragTexCoord).rgb * colDiffuse.rgb;
    vec3 lit = Shade(albedo, normal, fragPosition);
    // Будагны гялбаа: нарны тусгал (сүүдэрт бүдэгрэнэ)
    vec3 view = normalize(viewPos - fragPosition);
    float ndl = max(dot(normal, sunDirection), 0.0);
    float spec = pow(max(dot(reflect(-sunDirection, normal), view), 0.0), 40.0) * ndl;
    lit += sunLight * spec * 0.45 * SunShadow(fragPosition, ndl);
    finalColor = vec4(ApplyFog(lit, fragPosition), 1.0);
}
)";

    // Сүүдрийн зураг: зөвхөн гүн
    const char* kDepthVertexShader = R"(in vec3 vertexPosition;
uniform mat4 mvp;
void main()
{
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";
    const char* kDepthFragmentShader = R"(out vec4 finalColor;
void main()
{
    finalColor = vec4(1.0);
}
)";

    // Тэнгэр: дэлгэц дүүрэн тэгш өнцөгт; пиксел бүрийн харах чиглэлээр өнгө, нар, үүл
    const char* kScreenVertexShader = R"(in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;
uniform mat4 mvp;
out vec2 fragTexCoord;
void main()
{
    fragTexCoord = vertexTexCoord;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

    const char* kSkyFragmentShader = R"(in vec2 fragTexCoord;
uniform vec3 camForward;
uniform vec3 camRight;
uniform vec3 camUp;
uniform vec2 tanHalf;
uniform vec3 zenith;
uniform vec3 horizon;
uniform vec3 fogSunColor;
uniform vec3 sunDirection;
uniform vec3 sunCore;
uniform vec3 cloudLit;
uniform vec3 cloudDark;
uniform float night;
uniform float time;
uniform float cloudCover;
uniform vec2 wind;
uniform float exposure;
out vec4 finalColor;

vec3 Tonemap(vec3 x)
{
    x *= exposure;
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

float Hash(vec2 p)
{
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}
float Noise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(Hash(i), Hash(i + vec2(1.0, 0.0)), u.x), mix(Hash(i + vec2(0.0, 1.0)), Hash(i + vec2(1.0, 1.0)), u.x), u.y);
}
float Fbm(vec2 p)
{
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 5; ++i)
    {
        v += a * Noise(p);
        p = p * 2.03 + vec2(17.1, 9.2);
        a *= 0.5;
    }
    return v;
}

void main()
{
    vec2 ndc = vec2(fragTexCoord.x * 2.0 - 1.0, 1.0 - fragTexCoord.y * 2.0);
    vec3 dir = normalize(camForward + ndc.x * tanHalf.x * camRight + ndc.y * tanHalf.y * camUp);
    float e = dir.y;
    float s = max(dot(dir, sunDirection), 0.0);

    // Тэнгэрийн хаяа (манантай ижил өнгө, нар руу улбар) -> оргил руу гүн хөх
    vec3 haze = mix(horizon, fogSunColor, pow(s, 6.0) * 0.7);
    vec3 sky = mix(haze, zenith, pow(clamp(e, 0.0, 1.0), 0.5));
    sky = mix(sky, haze, 1.0 - smoothstep(0.0, 0.2, e));
    sky += fogSunColor * (pow(s, 4.0) * 0.1 + pow(s, 24.0) * 0.35 + pow(s, 300.0) * 0.6) * (1.0 - night);
    sky += haze * 0.12 * pow(1.0 - clamp(abs(e), 0.0, 1.0), 10.0) * (1.0 - night); // хаяаны цайвар бүс
    sky = mix(sky, sunCore, smoothstep(0.99925, 0.9996, s) * (1.0 - night));

    // Үүл: тэнгэрийн хавтгай дээрх fbm, салхинд аажим шилжинэ
    if (e > 0.0 && cloudCover > 0.0)
    {
        vec2 p = dir.xz / (e + 0.08) * 0.75 + wind * (time * 0.006);
        float n = Fbm(p);
        float c = smoothstep(1.0 - cloudCover - 0.12, 1.0 - cloudCover + 0.28, n);
        float behind = Fbm(p + sunDirection.xz * 0.12);
        vec3 cloud = mix(cloudLit, cloudDark, clamp((behind - n) * 4.0 + 0.35 + cloudCover * 0.3, 0.0, 1.0));
        cloud += fogSunColor * pow(s, 8.0) * 0.45 * (1.0 - night); // нарны дэргэд мөнгөлөг хүрээ
        c *= smoothstep(0.0, 0.16, e);
        sky = mix(sky, cloud, c * 0.94);
    }
    finalColor = vec4(Tonemap(sky), 1.0);
}
)";

    // Дэлгэцийн эффект: bloom нэмж, тодрол, ханалт, өнгөний тохиргоо, захын бараан (vignette)
    const char* kPostFragmentShader = R"(in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform sampler2D bloomTexture;
uniform float bloomStrength;
uniform float saturation;
uniform float contrast;
uniform vec3 tint;
uniform float vignette;
uniform float fxaa;  // 1 = ирмэг толийлгох (дээд график)
uniform vec2 texel;  // нэг пикселийн хэмжээ (сцений текстурт)
out vec4 finalColor;

// FXAA (Lottes-ийн хялбаршуулсан): тодролын ялгаанаас ирмэгийн чиглэлийг олж, түүний дагуу бүдгэрүүлнэ
vec3 Fxaa(vec2 uv)
{
    const vec3 L = vec3(0.299, 0.587, 0.114);
    vec3 m = texture(texture0, uv).rgb;
    float lNW = dot(texture(texture0, uv + vec2(-1.0, -1.0) * texel).rgb, L);
    float lNE = dot(texture(texture0, uv + vec2(1.0, -1.0) * texel).rgb, L);
    float lSW = dot(texture(texture0, uv + vec2(-1.0, 1.0) * texel).rgb, L);
    float lSE = dot(texture(texture0, uv + vec2(1.0, 1.0) * texel).rgb, L);
    float lM = dot(m, L);
    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));
    if (lMax - lMin < max(0.0312, lMax * 0.125)) return m; // ирмэг биш: хэмнэнэ
    vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), (lNW + lSW) - (lNE + lSE));
    float reduce = max((lNW + lNE + lSW + lSE) * 0.03125, 1.0 / 128.0);
    dir = clamp(dir / (min(abs(dir.x), abs(dir.y)) + reduce), -8.0, 8.0) * texel;
    vec3 a = 0.5 * (texture(texture0, uv - dir * (1.0 / 6.0)).rgb + texture(texture0, uv + dir * (1.0 / 6.0)).rgb);
    vec3 b = a * 0.5 + 0.25 * (texture(texture0, uv - dir * 0.5).rgb + texture(texture0, uv + dir * 0.5).rgb);
    float lB = dot(b, L);
    return (lB < lMin || lB > lMax) ? a : b;
}

void main()
{
    vec3 color = fxaa > 0.5 ? Fxaa(fragTexCoord) : texture(texture0, fragTexCoord).rgb;
    if (bloomStrength > 0.0) color += texture(bloomTexture, fragTexCoord).rgb * bloomStrength;
    color = (color - 0.5) * contrast + 0.5;
    float luma = dot(color, vec3(0.299, 0.587, 0.114));
    color = mix(vec3(luma), color, saturation) * tint;
    vec2 d = fragTexCoord - 0.5;
    color *= mix(1.0 - vignette, 1.0, 1.0 - smoothstep(0.2, 0.75, length(d) * 1.15));
    finalColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
)";

    const char* kBrightFragmentShader = R"(in vec2 fragTexCoord;
uniform sampler2D texture0;
out vec4 finalColor;
void main()
{
    vec3 color = texture(texture0, fragTexCoord).rgb;
    float luma = dot(color, vec3(0.299, 0.587, 0.114));
    finalColor = vec4(color * smoothstep(0.84, 1.0, luma), 1.0);
}
)";

    const char* kBlurFragmentShader = R"(in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform vec2 direction;
out vec4 finalColor;
void main()
{
    vec3 sum = texture(texture0, fragTexCoord).rgb * 0.227;
    sum += texture(texture0, fragTexCoord + direction * 1.38).rgb * 0.316;
    sum += texture(texture0, fragTexCoord - direction * 1.38).rgb * 0.316;
    sum += texture(texture0, fragTexCoord + direction * 3.23).rgb * 0.070;
    sum += texture(texture0, fragTexCoord - direction * 3.23).rgb * 0.070;
    finalColor = vec4(sum, 1.0);
}
)";

    // Shader-ийн эхлэл: компьютер дээр OpenGL 3.3, утсан ба хөтөч дээр OpenGL ES 3.0 (WebGL2).
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
    const char* kShaderHeader = "#version 300 es\nprecision highp float;\n";
    const char* kLightingDefines = "";
    const int kShadowMapSize = 1024;
    const float kShadowRadius = 45.0f;
#else
    const char* kShaderHeader = "#version 330\n";
    const char* kLightingDefines = "#define SOFT_SHADOWS\n";
    const int kShadowMapSize = 2048;
    const float kShadowRadius = 70.0f;
#endif

    Shader LoadVersionedShader(const char* vertex, const char* fragment, bool lighting = true)
    {
        std::string fs = kShaderHeader;
        if (lighting) fs.append(kLightingDefines).append(kLightingCommon);
        fs.append(fragment);
        return LoadShaderFromMemory(std::string(kShaderHeader).append(vertex).c_str(), fs.c_str());
    }

    Vector3 ToVector(Color color)
    {
        return { (float)color.r / 255.0f, (float)color.g / 255.0f, (float)color.b / 255.0f };
    }
    Color ToColor(Vector3 v)
    {
        return { (unsigned char)std::clamp(v.x * 255.0f, 0.0f, 255.0f), (unsigned char)std::clamp(v.y * 255.0f, 0.0f, 255.0f),
                 (unsigned char)std::clamp(v.z * 255.0f, 0.0f, 255.0f), 255 };
    }

    void SetVec3(Shader shader, const char* name, Vector3 value)
    {
        const int location = GetShaderLocation(shader, name);
        if (location >= 0) SetShaderValue(shader, location, &value, SHADER_UNIFORM_VEC3);
    }
    void SetVec2(Shader shader, const char* name, Vector2 value)
    {
        const int location = GetShaderLocation(shader, name);
        if (location >= 0) SetShaderValue(shader, location, &value, SHADER_UNIFORM_VEC2);
    }
    void SetFloat(Shader shader, const char* name, float value)
    {
        const int location = GetShaderLocation(shader, name);
        if (location >= 0) SetShaderValue(shader, location, &value, SHADER_UNIFORM_FLOAT);
    }
    void SetInt(Shader shader, const char* name, int value)
    {
        const int location = GetShaderLocation(shader, name);
        if (location >= 0) SetShaderValue(shader, location, &value, SHADER_UNIFORM_INT);
    }

    // Алсын уулс: камерыг дагадаг туйлын торон гадаргуу (оройн normal-тай, гөлгөр гэрэлтэнэ).
    // Ойрын цагираг газрын доор (харагдахгүй), холдох тусам нуруу, хөндий, цастай оргилууд.
    Mesh BuildMountains(float scale)
    {
        const int segments = 256;
        const float radii[] = { 740.0f, 800.0f, 870.0f, 950.0f, 1040.0f, 1140.0f, 1250.0f, 1370.0f, 1500.0f, 1640.0f, 1800.0f };
        const int rings = (int)(sizeof(radii) / sizeof(radii[0]));

        auto heightAt = [&](float angle, float r) {
            const float nx = std::cos(angle), nz = std::sin(angle);
            const float t = (r - radii[0]) / (radii[rings - 1] - radii[0]);
            // Нурууны хэлбэр: "ridged" fbm (хурц нуруу, зөөлөн хөндий)
            float ridge = 0.0f, amplitude = 0.55f, frequency = 0.0021f, weight = 1.0f;
            for (int octave = 0; octave < 5; ++octave)
            {
                float n = ValueNoise(nx * r * frequency + 31.0f * (float)octave, nz * r * frequency + 17.0f);
                n = 1.0f - std::fabs(n * 2.0f - 1.0f);
                n *= n * weight;
                weight = std::clamp(n * 1.6f, 0.0f, 1.0f);
                ridge += n * amplitude;
                amplitude *= 0.5f;
                frequency *= 2.1f;
            }
            // Том хэмжээний өндөрлөг: зарим зүгт өндөр нуруу, заримд нь намхан толгод
            const float massif = Fbm(nx * 1.6f + 40.0f, nz * 1.6f + 40.0f, 3);
            const float envelope = SmoothStep(0.0f, 0.3f, t) * (0.45f + 0.55f * t);
            return -25.0f + envelope * (60.0f + (170.0f + 560.0f * massif * massif) * ridge) * scale;
        };

        std::vector<Vector3> points((size_t)rings * segments);
        for (int ring = 0; ring < rings; ++ring)
        {
            for (int s = 0; s < segments; ++s)
            {
                const float angle = 2.0f * PI * (float)s / (float)segments;
                const float r = radii[ring];
                points[(size_t)ring * segments + s] = { std::cos(angle) * r, heightAt(angle, r), std::sin(angle) * r };
            }
        }
        auto P = [&](int ring, int s) { return points[(size_t)std::clamp(ring, 0, rings - 1) * segments + (size_t)((s + segments) % segments)]; };

        const Vector3 grassLow = { 0.43f, 0.47f, 0.30f }, ochre = { 0.55f, 0.49f, 0.36f };
        const Vector3 rock = { 0.42f, 0.40f, 0.38f }, rockDark = { 0.30f, 0.30f, 0.31f }, snow = { 0.92f, 0.93f, 0.96f };
        std::vector<float> positions, normals;
        std::vector<unsigned char> colors;
        auto vertex = [&](int ring, int s) {
            const Vector3 p = P(ring, s);
            Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(P(ring + 1, s), P(ring - 1, s)),
                                                             Vector3Subtract(P(ring, s + 1), P(ring, s - 1))));
            if (n.y < 0.0f) n = Vector3Negate(n);
            const float h = p.y;
            const float steep = 1.0f - n.y;
            const float streak = ValueNoise((float)s * 0.35f, (float)ring * 1.7f); // хадны судал, цасны хэлтэрхий
            Vector3 c = Vector3Lerp(grassLow, ochre, SmoothStep(10.0f, 90.0f, h) * 0.8f);
            c = Vector3Lerp(c, Vector3Lerp(rock, rockDark, streak), SmoothStep(0.25f, 0.55f, steep) * 0.85f + SmoothStep(110.0f, 200.0f, h) * 0.5f);
            const float snowLine = 190.0f + streak * 60.0f;
            c = Vector3Lerp(c, snow, SmoothStep(snowLine, snowLine + 50.0f, h) * SmoothStep(0.7f, 0.35f, steep));
            positions.insert(positions.end(), { p.x, p.y, p.z });
            normals.insert(normals.end(), { n.x, n.y, n.z });
            colors.insert(colors.end(), { (unsigned char)(c.x * 255.0f), (unsigned char)(c.y * 255.0f), (unsigned char)(c.z * 255.0f), 255 });
        };
        for (int ring = 0; ring + 1 < rings; ++ring)
        {
            for (int s = 0; s < segments; ++s)
            {
                vertex(ring, s); vertex(ring + 1, s); vertex(ring, s + 1);
                vertex(ring, s + 1); vertex(ring + 1, s); vertex(ring + 1, s + 1);
            }
        }

        Mesh mesh = {};
        mesh.vertexCount = (int)(positions.size() / 3);
        mesh.triangleCount = mesh.vertexCount / 3;
        mesh.vertices = (float*)MemAlloc((unsigned int)(positions.size() * sizeof(float)));
        mesh.normals = (float*)MemAlloc((unsigned int)(normals.size() * sizeof(float)));
        mesh.colors = (unsigned char*)MemAlloc((unsigned int)colors.size());
        std::memcpy(mesh.vertices, positions.data(), positions.size() * sizeof(float));
        std::memcpy(mesh.normals, normals.data(), normals.size() * sizeof(float));
        std::memcpy(mesh.colors, colors.data(), colors.size());
        UploadMesh(&mesh, false);
        return mesh;
    }

    RenderTexture2D LoadShadowTarget(int size)
    {
        RenderTexture2D target = {};
        target.id = rlLoadFramebuffer();
        target.texture.width = size;
        target.texture.height = size;
        if (target.id == 0) return target;
        rlEnableFramebuffer(target.id);
#if defined(__EMSCRIPTEN__)
        // WebGL2: raylib-ийн сонгосон гүний форматыг (DEPTH_COMPONENT эсвэл 32_OES) хүлээн авдаггүй тул
        // хэмжээтэй формат шууд заана.
        glGenTextures(1, &target.depth.id);
        glBindTexture(GL_TEXTURE_2D, target.depth.id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, size, size, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);
#else
        target.depth.id = rlLoadTextureDepth(size, size, false);
#endif
        target.depth.width = size;
        target.depth.height = size;
        target.depth.format = 19; // DEPTH_COMPONENT_24BIT
        target.depth.mipmaps = 1;
        rlFramebufferAttach(target.id, target.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
        const bool complete = rlFramebufferComplete(target.id);
        rlDisableFramebuffer();
        if (!complete || target.depth.id == 0)
        {
            TraceLog(LOG_WARNING, "ENVIRONMENT: shadow map framebuffer incomplete, shadows disabled");
            if (target.depth.id) rlUnloadTexture(target.depth.id);
            rlUnloadFramebuffer(target.id);
            return {};
        }
        rlTextureParameters(target.depth.id, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_NEAREST);
        rlTextureParameters(target.depth.id, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_NEAREST);
        rlTextureParameters(target.depth.id, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_CLAMP);
        rlTextureParameters(target.depth.id, RL_TEXTURE_WRAP_T, RL_TEXTURE_WRAP_CLAMP);
        TraceLog(LOG_INFO, "ENVIRONMENT: shadow map %dx%d", size, size);
        return target;
    }

    void UnloadShadowTarget(RenderTexture2D& target)
    {
        if (target.id == 0) return;
        if (target.depth.id) rlUnloadTexture(target.depth.id);
        rlUnloadFramebuffer(target.id);
        target = {};
    }

    Material MaterialWith(Shader shader)
    {
        Material material = LoadMaterialDefault();
        material.shader = shader;
        return material;
    }
}

Environment::Environment()
{
    shader = LoadVersionedShader(kVertexShader, kFragmentShader);
    if (shader.id == rlGetShaderIdDefault())
    {
        // Shader ачаалагдаагүй бол тоглоом гэрэлтүүлэг, манангүйгээр үргэлжилнэ.
        TraceLog(LOG_WARNING, "ENVIRONMENT: lighting shader failed to load, using flat colors");
    }
    offsetLocation = GetShaderLocation(shader, "worldOffset");
    material = MaterialWith(shader);

    terrainShader = LoadVersionedShader(kTerrainVertexShader, kTerrainFragmentShader);
    terrainMaterial = MaterialWith(terrainShader);
    LoadTerrainTextures();
    LoadSurfaceTextures();
    detailShader = LoadVersionedShader(kDetailVertexShader, kDetailFragmentShader);
    detailMaterial = MaterialWith(detailShader);
    characterShader = LoadVersionedShader(kCharacterVertexShader, kCharacterFragmentShader);
    vehicleShader = LoadVersionedShader(kVehicleVertexShader, kVehicleFragmentShader);
    propShader = LoadVersionedShader(kPropVertexShader, kFragmentShader);
    propMaterial = MaterialWith(propShader);
    depthShader = LoadVersionedShader(kDepthVertexShader, kDepthFragmentShader, false);
    depthMaterial = MaterialWith(depthShader);
    skyShader = LoadVersionedShader(kScreenVertexShader, kSkyFragmentShader, false);
    postShader = LoadVersionedShader(kScreenVertexShader, kPostFragmentShader, false);
    brightShader = LoadVersionedShader(kScreenVertexShader, kBrightFragmentShader, false);
    blurShader = LoadVersionedShader(kScreenVertexShader, kBlurFragmentShader, false);

    // Зарим видео картын драйвер shader-ийг хүлээж авахгүй байж болно: тэр тохиолдолд энгийн гэрэлтүүлэгтэй
    // үндсэн shader-ээр орлуулж, сүүдэр, дэлгэцийн эффектийг унтраана (тоглоом гацахгүй, унахгүй).
    auto failed = [](Shader s) { return s.id == 0 || s.id == rlGetShaderIdDefault(); };
    if (!failed(shader))
    {
        if (failed(terrainShader)) { TraceLog(LOG_WARNING, "ENVIRONMENT: terrain shader failed, fallback"); terrainShader = shader; terrainMaterial.shader = shader; }
        if (failed(detailShader)) { TraceLog(LOG_WARNING, "ENVIRONMENT: detail shader failed, fallback"); detailShader = shader; detailMaterial.shader = shader; }
        if (failed(propShader)) { TraceLog(LOG_WARNING, "ENVIRONMENT: prop shader failed, fallback"); propShader = shader; propMaterial.shader = shader; }
    }
    shadersBroken = failed(shader) || failed(depthShader) || failed(postShader) || failed(skyShader) || failed(brightShader) ||
                    failed(blurShader);
    if (shadersBroken) TraceLog(LOG_WARNING, "ENVIRONMENT: effect shaders failed, shadows and post-processing disabled");

    for (Shader s : { shader, terrainShader, detailShader, characterShader, propShader, vehicleShader })
    {
        SetVec3(s, "sunDirection", Vector3Normalize(kSunDirection));
        SetInt(s, "shadowMap", kShadowSlot);
        SetFloat(s, "shadowStrength", 0.0f);
        SetFloat(s, "fogScale", 1.0f);
    }
    SetVec3(skyShader, "sunDirection", Vector3Normalize(kSunDirection));
    SetVec3(skyShader, "sunCore", ToVector(kSunCore));
    // Гадаргуугийн бүтэц: зөвхөн хөдөлдөггүй геометрт (унаанд хэв нь гулсаж харагдана)
    SetFloat(shader, "surfaceDetail", 1.0f);
    if (propShader.id != shader.id) SetFloat(propShader, "surfaceDetail", 0.0f);

    shadowSize = kShadowMapSize;
    shadowRadius = kShadowRadius;
    shadowMap = LoadShadowTarget(shadowSize);

    mountains = BuildMountains(mountainScale);
    SetTimeOfDay(0.3f);
}

// Газрын текстурууд: материалын 0..3-р map-д (raylib DrawMesh нь texture0..3-т холбоно).
void Environment::LoadTerrainTextures()
{
    const char* names[4] = { "grass.png", "grass_dirt.png", "dirt.png", "rock.png" };
#if defined(__ANDROID__)
    const std::string folder = "terrain/";
#else
    std::string folder = "assets/terrain/";
    const std::string application = GetApplicationDirectory();
    for (const std::string& candidate : { std::string("assets/terrain/"), application + "assets/terrain/", application + "../assets/terrain/" })
    {
        if (DirectoryExists(candidate.c_str())) { folder = candidate; break; }
    }
#endif
    const int slots[4] = { SHADER_LOC_MAP_ALBEDO, SHADER_LOC_MAP_METALNESS, SHADER_LOC_MAP_NORMAL, SHADER_LOC_MAP_ROUGHNESS };
    const char* uniforms[4] = { "texture0", "texture1", "texture2", "texture3" };
    int loaded = 0;
    Vector3 means[4] = {};
    for (int i = 0; i < 4; ++i)
    {
        const std::string path = ImageFile(folder + names[i]); // (хөтөч: .jpg)
#if !defined(__ANDROID__)
        if (!FileExists(path.c_str())) continue;
#endif
        Image image = LoadImage(path.c_str());
        if (image.data == nullptr) continue;
        // Дундаж өнгө: shader-т нарийн бүтцийг (дундажтай харьцуулсан) гаргахад
        ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        const Color* pixels = (const Color*)image.data;
        double sum[3] = { 0.0, 0.0, 0.0 };
        const int count = image.width * image.height;
        for (int k = 0; k < count; ++k)
        {
            sum[0] += pixels[k].r;
            sum[1] += pixels[k].g;
            sum[2] += pixels[k].b;
        }
        means[i] = { (float)(sum[0] / count / 255.0), (float)(sum[1] / count / 255.0), (float)(sum[2] / count / 255.0) };
        Texture2D texture = LoadTextureFromImage(image);
        UnloadImage(image);
        if (texture.id == 0) continue;
        GenTextureMipmaps(&texture);
        SetTextureFilter(texture, TEXTURE_FILTER_TRILINEAR);
        SetTextureFilter(texture, TEXTURE_FILTER_ANISOTROPIC_8X); // налуу өнцгөөс харахад бүрэлзэхгүй
        SetTextureWrap(texture, TEXTURE_WRAP_REPEAT);
        terrainTextures[i] = texture;
        terrainMaterial.maps[i].texture = texture;
        terrainShader.locs[slots[i]] = GetShaderLocation(terrainShader, uniforms[i]);
        ++loaded;
    }
    terrainMaterial.shader = terrainShader;
    SetFloat(terrainShader, "terrainTextures", loaded == 4 ? 1.0f : 0.0f);
    const int meanLocation = GetShaderLocation(terrainShader, "terrainMean");
    if (meanLocation >= 0) SetShaderValueV(terrainShader, meanLocation, means, SHADER_UNIFORM_VEC3, 4);
    TraceLog(LOG_INFO, "ENVIRONMENT: %d of 4 terrain textures loaded", loaded);
}

// Гадаргуугийн бүтэц (assets/surfaces, tools/prep_surfaces.py): саарал 4 зургийг нэг RGBA текстурт нийлүүлнэ
// (shader нэг уншилтаар 4 материалын бүтцийг авна). Утас, хөтөчид 512 px (видео санах ой 4 дахин бага).
void Environment::LoadSurfaceTextures()
{
    static const char* const kSets[3][4] = {
        { "plaster", "brick", "concrete", "planks" },
        { "roof_tiles", "corrugated", "asphalt", "paving" },
        { "sand", "gravel", "pebbles", "cracked" },
    };
#if defined(__ANDROID__)
    const std::string folder = "surfaces/";
#else
    std::string folder = "assets/surfaces/";
    const std::string application = GetApplicationDirectory();
    for (const std::string& candidate : { std::string("assets/surfaces/"), application + "assets/surfaces/", application + "../assets/surfaces/" })
    {
        if (DirectoryExists(candidate.c_str())) { folder = candidate; break; }
    }
#endif
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
    const int size = 512;
#else
    const int size = 1024;
#endif
    int loaded = 0;
    for (int set = 0; set < 3; ++set)
    {
        Image packed = GenImageColor(size, size, { 128, 128, 128, 128 });
        ImageFormat(&packed, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        unsigned char* out = (unsigned char*)packed.data;
        bool complete = true;
        for (int channel = 0; channel < 4; ++channel)
        {
            Image gray = LoadImage((folder + kSets[set][channel] + ".jpg").c_str());
            if (gray.data == nullptr)
            {
                complete = false;
                continue;
            }
            ImageFormat(&gray, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE);
            if (gray.width != size || gray.height != size) ImageResize(&gray, size, size);
            const unsigned char* in = (const unsigned char*)gray.data;
            for (int k = 0; k < size * size; ++k) out[k * 4 + channel] = in[k];
            UnloadImage(gray);
        }
        if (!complete)
        {
            UnloadImage(packed);
            continue;
        }
        Texture2D texture = LoadTextureFromImage(packed);
        UnloadImage(packed);
        if (texture.id == 0) continue;
        GenTextureMipmaps(&texture);
        SetTextureFilter(texture, TEXTURE_FILTER_TRILINEAR);
        SetTextureFilter(texture, TEXTURE_FILTER_ANISOTROPIC_8X);
        SetTextureWrap(texture, TEXTURE_WRAP_REPEAT);
        surfaceTextures[set] = texture;
        ++loaded;
    }
    const bool buildings = surfaceTextures[0].id != 0 && surfaceTextures[1].id != 0;
    SetInt(shader, "wallDetail", kSurfaceSlot);
    SetInt(shader, "roofDetail", kSurfaceSlot + 1);
    SetFloat(shader, "surfaceTextures", buildings ? 1.0f : 0.0f);
    SetInt(terrainShader, "natureDetail", kSurfaceSlot + 2);
    SetFloat(terrainShader, "natureTextures", surfaceTextures[2].id != 0 ? 1.0f : 0.0f);
    TraceLog(LOG_INFO, "ENVIRONMENT: %d of 3 surface detail textures loaded (%d px)", loaded, size);
}

Environment::~Environment()
{
    for (Texture2D& texture : terrainTextures)
    {
        if (texture.id != 0) UnloadTexture(texture);
        texture = {};
    }
    for (Texture2D& texture : surfaceTextures)
    {
        if (texture.id != 0) UnloadTexture(texture);
        texture = {};
    }
    UnloadBuiltMesh(mountains);
    // Material-ууд shader-ээ хуваалцаж болох тул (орлуулсан үед) shader бүрийг нэг л удаа чөлөөлнө.
    for (Material* m : { &material, &terrainMaterial, &detailMaterial, &propMaterial, &depthMaterial })
    {
        RL_FREE(m->maps);
        m->maps = nullptr;
    }
    unsigned int released[16] = {};
    int count = 0;
    for (Shader s : { shader, terrainShader, detailShader, propShader, depthShader, characterShader, vehicleShader, skyShader, postShader,
                      brightShader, blurShader })
    {
        if (s.id == 0 || s.id == rlGetShaderIdDefault()) continue;
        bool done = false;
        for (int i = 0; i < count; ++i) done = done || released[i] == s.id;
        if (done) continue;
        released[count++] = s.id;
        UnloadShader(s);
    }
    UnloadShadowTarget(shadowMap);
    if (scene.id) UnloadRenderTexture(scene);
    if (bloomA.id) UnloadRenderTexture(bloomA);
    if (bloomB.id) UnloadRenderTexture(bloomB);
}

namespace
{
    float DayCurve(float edge0, float edge1, float x)
    {
        const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }
    Vector3 Mix3(Vector3 a, Vector3 b, float t)
    {
        return Vector3Lerp(a, b, std::clamp(t, 0.0f, 1.0f));
    }
}

void Environment::SetQuality(int level)
{
    quality = std::clamp(level, 0, 2);
    if (shadersBroken) quality = 0;
    shadowsEnabled = quality >= 1;
    postEnabled = quality >= 1;
    bloomEnabled = quality >= 2;
    if (!bloomEnabled)
    {
        if (bloomA.id) UnloadRenderTexture(bloomA);
        if (bloomB.id) UnloadRenderTexture(bloomB);
        bloomA = {};
        bloomB = {};
    }
    SetFloat(postShader, "bloomStrength", bloomEnabled ? 0.55f : 0.0f);
    // (Туршилт ZM_FX=1: бага графикийн зурагт дээд түвшний ертөнцийн эффектийг харуулна: далд цонхонд post-тай зураг
    // авагддаггүй)
    const bool fxTest = std::getenv("ZM_FX") != nullptr;
    SetFloat(postShader, "fxaa", quality >= 2 ? 1.0f : 0.0f);
    SetFloat(shader, "surfaceBump", quality >= 2 || fxTest ? 1.0f : 0.0f);
    lastApplied = -1.0f; // (үүлний сүүдэр түвшнээс хамаарна: SetTimeOfDay дахин тооцно)
}

void Environment::SetMountainScale(float scale)
{
    if (std::fabs(scale - mountainScale) < 0.01f && mountains.vertexCount > 0) return;
    mountainScale = scale;
    UnloadBuiltMesh(mountains);
    mountains = BuildMountains(scale);
}

void Environment::SetDryness(float value)
{
    for (Shader s : { terrainShader, detailShader }) SetFloat(s, "dryness", std::clamp(value, 0.0f, 1.0f));
}

void Environment::SetWeather(WeatherKind kind)
{
    weather = kind;
    lastApplied = -1.0f; // дараагийн SetTimeOfDay дахин тооцно
    switch (kind)
    {
    case WeatherKind::Clear: cloudCover = 0.32f; wind = { 0.8f, 0.35f }; break;
    case WeatherKind::Cloudy: cloudCover = 0.68f; wind = { 1.4f, 0.6f }; break;
    case WeatherKind::Rain: cloudCover = 0.95f; wind = { 1.8f, 0.9f }; break;
    case WeatherKind::Snow: cloudCover = 0.9f; wind = { 1.2f, 0.7f }; break;
    case WeatherKind::Dust: cloudCover = 0.2f; wind = { 3.2f, 1.4f }; break;
    }
}

void Environment::Update(float dt)
{
    time += dt;
}

void Environment::SetTimeOfDay(float t)
{
    t -= std::floor(t);
    if (std::fabs(t - lastApplied) < 0.0005f) return; // өөрчлөлт бага: shader-т дахин тавихгүй
    lastApplied = t;

    // Мөчлөг: 0.00-0.58 өдөр, 0.58-0.68 нар жаргах, 0.68-0.92 шөнө, 0.92-1.00 үүр цайх.
    night = DayCurve(0.58f, 0.68f, t) * (1.0f - DayCurve(0.92f, 0.99f, t));
    // Нар жаргах, мандах үеийн улбар туяа: хоёр бөмбөгөр
    auto bump = [&](float center, float width) { return DayCurve(0.0f, 1.0f, 1.0f - std::fabs(t - center) / width); };
    golden = std::max(bump(0.61f, 0.07f), bump(0.955f, 0.05f));

    // --- Өдөр / үдэш / шөнийн өнгө ---
    // Гэрэл нь HDR (1-ээс их байж болно): эцэст нь Tonemap шахна. Нар дулаан, хүчтэй; сүүдэр (тэнгэрийн
    // орчны гэрэл) хүйтэн хөхөвтөр - бодит зураг шиг дулаан/хүйтний ялгаа.
    Vector3 zenith = Mix3(Mix3({ 0.16f, 0.36f, 0.72f }, { 0.20f, 0.27f, 0.52f }, golden), { 0.02f, 0.04f, 0.10f }, night);
    Vector3 horizon = Mix3(Mix3({ 0.66f, 0.76f, 0.88f }, { 1.05f, 0.62f, 0.38f }, golden), { 0.10f, 0.13f, 0.20f }, night);
    Vector3 haze = Mix3(Mix3({ 1.15f, 0.98f, 0.76f }, { 1.35f, 0.70f, 0.32f }, golden), { 0.12f, 0.15f, 0.24f }, night);
    Vector3 sun = Mix3(Mix3({ 1.38f, 1.24f, 1.02f }, { 1.55f, 0.86f, 0.46f }, golden), { 0.13f, 0.17f, 0.28f }, night);
    Vector3 ambient = Mix3(Mix3({ 0.32f, 0.42f, 0.60f }, { 0.28f, 0.31f, 0.48f }, golden), { 0.17f, 0.20f, 0.31f }, night);
    Vector3 ground = Mix3(Mix3({ 0.30f, 0.26f, 0.18f }, { 0.34f, 0.22f, 0.14f }, golden), { 0.07f, 0.07f, 0.09f }, night);
    Vector3 cloudLit = Mix3(Mix3({ 0.98f, 0.98f, 1.0f }, { 1.0f, 0.76f, 0.58f }, golden), { 0.16f, 0.18f, 0.25f }, night);
    Vector3 cloudDark = Mix3(Mix3({ 0.64f, 0.68f, 0.76f }, { 0.50f, 0.40f, 0.46f }, golden), { 0.07f, 0.08f, 0.12f }, night);
    float fogDensity = kFogDensity * (1.0f + 0.8f * night);
    // Өнгөний шилжилтийн гэрэлтүүлэг: шөнө бага зэрэг өндөр (харанхуйд ч харагдахуйц)
    const float exposure = 0.64f + 0.5f * night;
    float shadowStrength = 0.72f * (1.0f - night) * (1.0f - golden * 0.3f);

    // --- Цаг агаар ---
    const float dayLight = 1.0f - night * 0.85f;
    auto grey = [&](Vector3 v, Vector3 to, float amount) { return Mix3(v, Vector3Scale(to, dayLight), amount); };
    switch (weather)
    {
    case WeatherKind::Clear: break;
    case WeatherKind::Cloudy:
        zenith = grey(zenith, { 0.46f, 0.54f, 0.64f }, 0.45f);
        horizon = grey(horizon, { 0.72f, 0.76f, 0.80f }, 0.3f);
        sun = Vector3Scale(sun, 0.62f);
        ambient = Vector3Scale(ambient, 1.08f);
        shadowStrength *= 0.6f;
        fogDensity *= 1.3f;
        break;
    case WeatherKind::Rain:
        zenith = grey(zenith, { 0.40f, 0.43f, 0.48f }, 0.8f);
        horizon = grey(horizon, { 0.56f, 0.59f, 0.63f }, 0.75f);
        haze = grey(haze, { 0.62f, 0.64f, 0.66f }, 0.7f);
        cloudLit = grey(cloudLit, { 0.62f, 0.64f, 0.68f }, 0.7f);
        cloudDark = grey(cloudDark, { 0.32f, 0.34f, 0.38f }, 0.7f);
        sun = Vector3Scale(sun, 0.32f);
        ambient = Vector3Scale(ambient, 1.05f);
        shadowStrength *= 0.25f;
        fogDensity *= 2.2f;
        break;
    case WeatherKind::Snow:
        zenith = grey(zenith, { 0.58f, 0.62f, 0.68f }, 0.75f);
        horizon = grey(horizon, { 0.82f, 0.84f, 0.88f }, 0.75f);
        haze = grey(haze, { 0.88f, 0.88f, 0.9f }, 0.6f);
        cloudLit = grey(cloudLit, { 0.86f, 0.87f, 0.9f }, 0.6f);
        cloudDark = grey(cloudDark, { 0.6f, 0.62f, 0.66f }, 0.6f);
        sun = Vector3Scale(sun, 0.42f);
        ambient = Vector3Scale(ambient, 1.18f);
        shadowStrength *= 0.3f;
        fogDensity *= 2.6f;
        break;
    case WeatherKind::Dust:
        zenith = grey(zenith, { 0.62f, 0.56f, 0.46f }, 0.55f);
        horizon = grey(horizon, { 0.78f, 0.64f, 0.46f }, 0.85f);
        haze = grey(haze, { 0.95f, 0.74f, 0.48f }, 0.7f);
        cloudLit = grey(cloudLit, { 0.86f, 0.74f, 0.58f }, 0.6f);
        sun = Mix3(Vector3Scale(sun, 0.7f), Vector3Scale({ 0.8f, 0.62f, 0.42f }, dayLight), 0.4f);
        ambient = Mix3(ambient, Vector3Scale({ 0.46f, 0.40f, 0.34f }, dayLight), 0.5f);
        shadowStrength *= 0.5f;
        fogDensity *= 3.0f;
        break;
    }

    // Газар дээгүүр гүйх үүлний сүүдэр: цэлмэг, үүлэрхэг өдөр (бороо, цас, шөнө бүрэн бүрхэг/харанхуй тул үгүй;
    // хамгийн бага графикт зардал хэмнэнэ)
    float cloudShade = quality == 0 && !std::getenv("ZM_FX") ? 0.0f
                     : weather == WeatherKind::Clear ? 0.28f : weather == WeatherKind::Cloudy ? 0.42f
                     : weather == WeatherKind::Dust ? 0.15f : 0.0f;
    cloudShade *= 1.0f - night;
    skyZenith = ToColor(zenith);
    skyHorizon = ToColor(horizon);
    sunHaze = haze;

    for (Shader s : { shader, terrainShader, detailShader, characterShader, propShader, vehicleShader })
    {
        if (s.id == 0) continue;
        SetVec3(s, "sunLight", sun);
        SetVec3(s, "ambientLight", ambient);
        SetVec3(s, "groundLight", ground);
        SetVec3(s, "fogColor", horizon);
        SetVec3(s, "fogSunColor", haze);
        SetFloat(s, "fogDensity", fogDensity);
        SetFloat(s, "exposure", exposure);
        SetVec3(s, "skyZenith", zenith);
        SetFloat(s, "nightLight", night);
        SetFloat(s, "cloudShadow", cloudShade);
    }
    SetFloat(skyShader, "exposure", exposure);
    SetVec3(skyShader, "zenith", zenith);
    SetVec3(skyShader, "horizon", horizon);
    SetVec3(skyShader, "fogSunColor", haze);
    SetVec3(skyShader, "cloudLit", cloudLit);
    SetVec3(skyShader, "cloudDark", cloudDark);
    SetFloat(skyShader, "night", night);
    SetFloat(skyShader, "cloudCover", cloudCover);
    dayShadow = shadowStrength;
    for (Shader s : { shader, terrainShader, detailShader, propShader })
    {
        SetFloat(s, "snowCover", weather == WeatherKind::Snow ? 0.78f : 0.0f);
        SetFloat(s, "wetness", weather == WeatherKind::Rain ? 1.0f : 0.0f);
    }

    // Дэлгэцийн өнгө: өдөр тод, үдэш дулаан, шөнө хөхөвтөр бүдэг
    const float wet = weather == WeatherKind::Rain || weather == WeatherKind::Snow ? 1.0f : 0.0f;
    const float saturation = (1.1f + golden * 0.06f) * (1.0f - night * 0.3f) * (1.0f - wet * 0.15f);
    const Vector3 tint = Mix3(Mix3({ 1.02f, 1.0f, 0.97f }, { 1.06f, 0.98f, 0.9f }, golden), { 0.88f, 0.94f, 1.1f }, night);
    SetFloat(postShader, "saturation", saturation);
    SetFloat(postShader, "contrast", 1.02f); // тодролыг Tonemap өгнө
    SetVec3(postShader, "tint", tint);
    SetFloat(postShader, "vignette", 0.28f + night * 0.12f);
    SetFloat(postShader, "bloomStrength", bloomEnabled ? 0.55f : 0.0f);
}

void Environment::DrawSky(const Camera3D& camera) const
{
    // Бага нягтралтай текстурт зурж байвал түүний хэмжээгээр (тэнгэр бүхэлдээ багтана)
    const int width = sceneActive ? scene.texture.width : ViewWidth();
    const int height = sceneActive ? scene.texture.height : ViewHeight();

    // --- Тэнгэр: shader-ээр (үүл, нар, хаяаны манан) ---
    const Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    const Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera.up));
    const Vector3 up = Vector3CrossProduct(right, forward);
    const float tanHalf = std::tan(camera.fovy * DEG2RAD * 0.5f);
    const float aspect = (float)width / (float)std::max(1, height);
    ClearBackground(skyHorizon);
    if (skyShader.id != 0 && skyShader.id != rlGetShaderIdDefault())
    {
        SetVec3(skyShader, "camForward", forward);
        SetVec3(skyShader, "camRight", right);
        SetVec3(skyShader, "camUp", up);
        SetVec2(skyShader, "tanHalf", { tanHalf * aspect, tanHalf });
        SetFloat(skyShader, "time", time);
        SetVec2(skyShader, "wind", wind);
        BeginShaderMode(skyShader);
        rlSetTexture(rlGetTextureIdDefault());
        rlBegin(RL_QUADS);
        rlColor4ub(255, 255, 255, 255);
        rlTexCoord2f(0.0f, 0.0f); rlVertex2f(0.0f, 0.0f);
        rlTexCoord2f(0.0f, 1.0f); rlVertex2f(0.0f, (float)height);
        rlTexCoord2f(1.0f, 1.0f); rlVertex2f((float)width, (float)height);
        rlTexCoord2f(1.0f, 0.0f); rlVertex2f((float)width, 0.0f);
        rlEnd();
        rlSetTexture(0);
        EndShaderMode();
    }

    if (night > 0.05f)
    {
        const float scale = (float)height / 720.0f;
        const Vector3 sun = Vector3Normalize(kSunDirection);
        Vector3 flat = Vector3Normalize({ forward.x, 0.0f, forward.z });
        const float horizon = ViewWorldToScreen(Vector3Add(camera.position, Vector3Scale(flat, 5000.0f)), camera).y;
        // Од: тэнгэрийн бөмбөрцөг дээрх тогтмол цэгүүд (хаяанаас дээш, үүл ихтэй үед бүдэг)
        const float clear = 1.0f - std::clamp((cloudCover - 0.3f) * 1.6f, 0.0f, 0.9f);
        for (int i = 0; i < 260; ++i)
        {
            const float a = (float)((i * 2654435761u) % 10000u) / 10000.0f * 2.0f * PI;
            const float e = 0.08f + (float)((i * 40503u + 17u) % 10000u) / 10000.0f * 1.35f;
            const Vector3 dir = { std::cos(e) * std::sin(a), std::sin(e), std::cos(e) * std::cos(a) };
            if (Vector3DotProduct(forward, dir) < 0.2f) continue;
            const Vector2 p = ViewWorldToScreen(Vector3Add(camera.position, Vector3Scale(dir, 5000.0f)), camera);
            if (p.y > horizon - 4.0f) continue;
            const float twinkle = 0.65f + 0.35f * std::sin((float)GetTime() * (1.3f + (float)(i % 7) * 0.4f) + (float)i);
            const float size = (i % 9 == 0 ? 1.8f : 1.1f) * scale;
            DrawCircleV(p, size, Fade(WHITE, night * twinkle * 0.9f * clear));
        }
        // Сар: нарны эсрэг талд
        const Vector3 moon = Vector3Normalize({ -sun.x, 0.55f, -sun.z });
        if (Vector3DotProduct(forward, moon) > 0.15f)
        {
            const Vector2 p = ViewWorldToScreen(Vector3Add(camera.position, Vector3Scale(moon, 5000.0f)), camera);
            const float alpha = night * (0.35f + 0.65f * clear);
            DrawCircleGradient((int)p.x, (int)p.y, 90.0f * scale, Fade({ 190, 205, 255, 255 }, 0.25f * alpha), Fade({ 190, 205, 255, 255 }, 0.0f));
            DrawCircleV(p, 20.0f * scale, Fade({ 232, 236, 246, 255 }, alpha));
            DrawCircleV({ p.x + 7.0f * scale, p.y - 3.0f * scale }, 4.0f * scale, Fade({ 196, 200, 214, 255 }, alpha));
            DrawCircleV({ p.x - 6.0f * scale, p.y + 6.0f * scale }, 3.0f * scale, Fade({ 200, 204, 218, 255 }, alpha));
        }
    }
}

void Environment::Begin(const Camera3D& camera) const
{
    // Гадаргуугийн бүтэц: материалын map-уудаас тусдаа нэгжүүдэд байнга (хөдөлдөггүй геометр бүгд нэг материалтай)
    for (int i = 0; i < 3; ++i)
    {
        if (surfaceTextures[i].id == 0) continue;
        rlActiveTextureSlot(kSurfaceSlot + i);
        rlEnableTexture(surfaceTextures[i].id);
    }
    rlActiveTextureSlot(0);
    const float strength = shadowReady && ShadowsEnabled() ? dayShadow : 0.0f;
    // Сүүдэргүй үед ч (бага график, шөнө, бороо) нэгжид жинхэнэ текстур байна: хоосон нэгжээс уншдаг shader-ийг
    // зарим драйвер (Intel) удаан замаар ажиллуулж, эхэндээ хэдэн секунд гацдаг байв (кадр 2.5 дахин удаан).
    rlActiveTextureSlot(kShadowSlot);
    rlEnableTexture(strength > 0.0f ? shadowMap.depth.id : rlGetTextureIdDefault());
    rlActiveTextureSlot(0);
    const float texel = 1.0f / (float)std::max(1, shadowSize);
    // Ортографын гүн шугаман: near..far (Game: rlSetClipPlanes) → 0..1. ~15 см-ийн хазайлт.
    const float bias = 0.15f / (float)(rlGetCullDistanceFar() - rlGetCullDistanceNear());
    for (Shader s : { shader, terrainShader, detailShader, characterShader, propShader, vehicleShader })
    {
        if (s.id == 0) continue;
        SetVec3(s, "viewPos", camera.position);
        SetFloat(s, "shadowStrength", strength);
        if (strength > 0.0f)
        {
            SetInt(s, "shadowMap", kShadowSlot);
            const int location = GetShaderLocation(s, "lightVP");
            if (location >= 0) SetShaderValueMatrix(s, location, lightViewProj);
            SetFloat(s, "shadowTexel", texel);
            SetFloat(s, "shadowBias", bias);
        }
    }
    for (Shader s : { shader, terrainShader, detailShader, characterShader, propShader, vehicleShader })
    {
        if (s.id == 0) continue;
        SetFloat(s, "time", time);
        SetVec2(s, "wind", wind);
    }
    BeginShaderMode(shader);
}

void Environment::End() const
{
    EndShaderMode();
}

void Environment::DrawFarLand(const std::vector<MeshPiece>& pieces, const Camera3D& camera) const
{
    // Хотын эргэн тойрны жинхэнэ газар (10 км): ойрын газартай ижил манан, цааш нь алсын уулс шиг бүдэгхэн
    const ViewCone view = ViewCone::From(camera);
    rlDrawRenderBatchActive();
    SetFloat(terrainShader, "fogScale", 0.015f);
    SetFloat(terrainShader, "fogFarStart", 650.0f);
    // Ойрын газар 680 м хүртэл зурагдана; түүнээс бага радиуст алсын газрыг хаяна (World::BuildFarLand-ийн
    // тоглох талбайн хязгаартай ижил).
    SetFloat(terrainShader, "farCutRadius", 600.0f);
    SetFloat(terrainShader, "farCutHalf", kWorldHalfExtent - 60.0f);
    rlDisableBackfaceCulling();
    for (const MeshPiece& piece : pieces)
    {
        if (view.Sees(piece.center, piece.radius, 14000.0f)) DrawMesh(piece.mesh, terrainMaterial, MatrixIdentity());
    }
    rlEnableBackfaceCulling();
    SetFloat(terrainShader, "fogScale", 1.0f);
    SetFloat(terrainShader, "fogFarStart", 0.0f);
    SetFloat(terrainShader, "farCutRadius", 0.0f);
}

void Environment::DrawMountains(Vector3 viewPosition) const
{
    if (mountains.vertexCount == 0)
    {
        return;
    }

    // Уулс камертай хамт хэвтээ чиглэлд шилжинэ: хэчнээн хол явсан ч тэнгэрийн хаяанд хэвээр.
    // Хол зайд уулсын хэлбэр өөрчлөгдөхгүй тул тоглогч уул руу "ойртож" чадахгүй, алсын дэвсгэр л болно.
    const Vector3 offset = { viewPosition.x, 0.0f, viewPosition.z };
    const Vector3 none = { 0.0f, 0.0f, 0.0f };

    rlDrawRenderBatchActive(); // өмнөх зураглал хуучин offset-оор дуусна
    SetVec3(terrainShader, "worldOffset", offset);
    SetFloat(terrainShader, "fogScale", 0.52f); // алсын уулсын хэлбэр мананд бүрэн уусахгүй

    rlDisableBackfaceCulling();
    DrawMesh(mountains, terrainMaterial, MatrixTranslate(offset.x, 0.0f, offset.z));
    rlEnableBackfaceCulling();

    SetVec3(terrainShader, "worldOffset", none);
    SetFloat(terrainShader, "fogScale", 1.0f);
}

// ---------------------------------------------------------------- Сүүдэр

bool Environment::BeginShadowPass(Vector3 center)
{
    if (!ShadowsEnabled() || dayShadow <= 0.01f)
    {
        shadowReady = false;
        return false;
    }
    // Сүүдрийн зургийг хоёр frame тутам шинэчилнэ (хөдөлгөөнд мэдэгдэхгүй, зардал хоёр дахин бага).
    // Хуучин зураг нь өөрийн lightViewProj-тойгоо хадгалагдах тул камер хөдөлсөн ч зөв хэвээр.
    if (shadowReady && (shadowFrame++ & 1) == 1) return false;

    // Өмнөх frame-ийн сүүдрийн зургийг уншихаа больж (өөр рүүгээ бичих үед уншихгүй)
    rlActiveTextureSlot(kShadowSlot);
    rlDisableTexture();
    rlActiveTextureSlot(0);

    // Камер хөдлөхөд сүүдрийн ирмэг "гүйхгүй": төвийг сүүдрийн зургийн нүдний хэмжээгээр бүхэлчилнэ.
    const Vector3 sun = Vector3Normalize(kSunDirection);
    const Vector3 right = Vector3Normalize(Vector3CrossProduct(sun, { 0.0f, 1.0f, 0.0f }));
    const Vector3 up = Vector3CrossProduct(right, sun);
    const float texel = 2.0f * shadowRadius / (float)shadowSize;
    const float u = Vector3DotProduct(center, right), v = Vector3DotProduct(center, up);
    center = Vector3Add(center, Vector3Add(Vector3Scale(right, std::floor(u / texel) * texel - u),
                                           Vector3Scale(up, std::floor(v / texel) * texel - v)));

    Camera3D light = {};
    light.position = Vector3Add(center, Vector3Scale(sun, 300.0f));
    light.target = center;
    light.up = { 0.0f, 1.0f, 0.0f };
    light.fovy = shadowRadius * 2.0f;
    light.projection = CAMERA_ORTHOGRAPHIC;

    BeginTextureMode(shadowMap);
    // Өмнөх frame-д (хагас тунгалаг зүйл зурахдаа) гүний бичилт унтарсан үлдсэн байж болно: эс бөгөөс
    // цэвэрлэгээ ч, бичилт ч хийгдэхгүй.
    rlEnableDepthMask();
    ClearBackground(WHITE);
    BeginMode3D(light);
    lightViewProj = MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection());
    for (Shader s : { shader, terrainShader, detailShader, characterShader, propShader, vehicleShader })
    {
        SetFloat(s, "shadowStrength", 0.0f);
    }
    BeginShaderMode(depthShader);
    shadowPassActive = true;
    return true;
}

void Environment::EndShadowPass()
{
    if (!shadowPassActive) return;
    EndShaderMode();
    EndMode3D();
    EndTextureMode();
    shadowReady = true;
    shadowPassActive = false;
}

// ---------------------------------------------------------------- Дэлгэцийн эффект

bool Environment::BeginScene(int width, int height, bool lowRes)
{
    sceneActive = false;
    sceneShaded = postEnabled && postShader.id != 0 && postShader.id != rlGetShaderIdDefault();
    if (!sceneShaded && !lowRes) return false;
    if (scene.id == 0 || scene.texture.width != width || scene.texture.height != height)
    {
        if (scene.id) UnloadRenderTexture(scene);
        if (bloomA.id) UnloadRenderTexture(bloomA);
        if (bloomB.id) UnloadRenderTexture(bloomB);
        bloomA = {};
        bloomB = {};
        scene = LoadRenderTexture(width, height);
        SetTextureFilter(scene.texture, TEXTURE_FILTER_BILINEAR);
    }
    if (sceneShaded && bloomEnabled && bloomA.id == 0)
    {
        bloomA = LoadRenderTexture(std::max(1, width / 4), std::max(1, height / 4));
        bloomB = LoadRenderTexture(std::max(1, width / 4), std::max(1, height / 4));
        SetTextureFilter(bloomA.texture, TEXTURE_FILTER_BILINEAR);
        SetTextureFilter(bloomB.texture, TEXTURE_FILTER_BILINEAR);
    }
    if (scene.id == 0) return false;
    BeginTextureMode(scene);
    sceneActive = true;
    return true;
}

void Environment::EndScene()
{
    if (!sceneActive) return;
    EndTextureMode();

    if (sceneShaded && bloomEnabled && bloomA.id && bloomB.id)
    {
        // Гэрэлтэй хэсгийг 1/4 хэмжээнд ялгаж, хоёр чиглэлд бүдгэрүүлнэ
        const float w = (float)scene.texture.width, h = (float)scene.texture.height;
        const float bw = (float)bloomA.texture.width, bh = (float)bloomA.texture.height;
        BeginTextureMode(bloomA);
        ClearBackground(BLACK);
        BeginShaderMode(brightShader);
        DrawTexturePro(scene.texture, { 0.0f, 0.0f, w, -h }, { 0.0f, 0.0f, bw, bh }, { 0.0f, 0.0f }, 0.0f, WHITE);
        EndShaderMode();
        EndTextureMode();
        for (int pass = 0; pass < 2; ++pass)
        {
            const RenderTexture2D& from = pass == 0 ? bloomA : bloomB;
            const RenderTexture2D& to = pass == 0 ? bloomB : bloomA;
            SetVec2(blurShader, "direction", pass == 0 ? Vector2{ 1.0f / bw, 0.0f } : Vector2{ 0.0f, 1.0f / bh });
            BeginTextureMode(to);
            ClearBackground(BLACK);
            BeginShaderMode(blurShader);
            DrawTexturePro(from.texture, { 0.0f, 0.0f, bw, -bh }, { 0.0f, 0.0f, bw, bh }, { 0.0f, 0.0f }, 0.0f, WHITE);
            EndShaderMode();
            EndTextureMode();
        }
    }
}

void Environment::DrawScene() const
{
    if (!sceneActive) return;
    const float w = (float)scene.texture.width, h = (float)scene.texture.height;
    if (sceneShaded)
    {
        SetVec2(postShader, "texel", { 1.0f / w, 1.0f / h });
        BeginShaderMode(postShader);
        if (bloomEnabled && bloomA.id)
        {
            SetShaderValueTexture(postShader, GetShaderLocation(postShader, "bloomTexture"), bloomA.texture);
        }
    }
    DrawTexturePro(scene.texture, { 0.0f, 0.0f, w, -h }, { 0.0f, 0.0f, (float)ViewWidth(), (float)ViewHeight() },
                   { 0.0f, 0.0f }, 0.0f, WHITE);
    if (sceneShaded) EndShaderMode();
}
