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
#elif defined(_WIN32)
// OpenGL 1.1-ийн функцууд (opengl32.dll-ээс шууд): сүүдрийн өнгөгүй framebuffer-т draw buffer-гүй гэж заана
extern "C" __declspec(dllimport) void __stdcall glDrawBuffer(unsigned int mode);
extern "C" __declspec(dllimport) void __stdcall glReadBuffer(unsigned int mode);
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
    const int kFarShadowSlot = 10;       // холын сүүдрийн зураг
    const float kMinSunElevation = 10.0f * DEG2RAD; // гэрэл, сүүдрийн нар үүнээс доош буухгүй (хэт урт сүүдэр)
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
// Холын сүүдэр (ойрын сүүдрийн хүрээний гадна): 0 = байхгүй
uniform highp sampler2D shadowMapFar;
uniform mat4 lightVPFar;
uniform float shadowFar;
uniform float shadowTexelFar;
uniform float shadowBiasFar;
// Намхан газрын манан: нягт, газрын түвшин, зузаан (м)
uniform float mistDensity;
uniform float mistBase;
uniform float mistHeight;
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
// ACES өнгийг бага зэрэг бүдгэрүүлдэг тул ханалтыг (vibrance) нэмнэ: бүх график түвшинд өнгөлөг.
uniform float vibrance;
vec3 Tonemap(vec3 x)
{
    x *= exposure;
    vec3 c = clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
    return clamp(mix(vec3(dot(c, vec3(0.299, 0.587, 0.114))), c, vibrance), 0.0, 1.0);
}

// Аймгийн хуурайшил: ногоон өвсийг шаргал, говьд элсэрхэг хүрэн болгоно (газар, өвсөнд).
// Чийглэг (Хангай, Хөвсгөл, ойт) аймагт эсрэгээрээ: чидун ногоон өвс тод, шүүслэг ногоон болно.
vec3 Dry(vec3 albedo)
{
    float green = clamp((albedo.g - max(albedo.r, albedo.b)) * 16.0, 0.0, 1.0);
    float lum = dot(albedo, vec3(0.33));
    float lush = (1.0 - dryness) * (1.0 - dryness);
    vec3 vivid = vec3(0.62, 1.24, 0.40) * lum; // тодрол хэвээр, өнгө нь цэвэр ногоон (гольфын талбай шиг хиймэл биш)
    albedo = mix(albedo, vivid, green * lush * 0.5);
    vec3 sand = vec3(0.70, 0.62, 0.45) * (0.8 + 0.4 * lum);
    return mix(albedo, sand, dryness * green);
}

// Холын сүүдэр: 4 цэгээр (бүдүүн нягтрал тул зөөлөн ирмэг хангалттай)
float FarShadow(vec3 pos, float ndl)
{
    vec4 lp = lightVPFar * vec4(pos, 1.0);
    vec3 p = lp.xyz / lp.w * 0.5 + 0.5;
    vec2 edge = abs(p.xy - 0.5) * 2.0;
    float fade = smoothstep(0.85, 1.0, max(edge.x, edge.y));
    if (fade >= 1.0 || p.z >= 1.0) return 1.0;
    float depth = p.z - shadowBiasFar * (1.0 + 4.0 * (1.0 - ndl));
    float lit = step(depth, texture(shadowMapFar, p.xy + vec2(-0.5, -0.5) * shadowTexelFar).r);
    lit += step(depth, texture(shadowMapFar, p.xy + vec2(0.5, -0.5) * shadowTexelFar).r);
    lit += step(depth, texture(shadowMapFar, p.xy + vec2(-0.5, 0.5) * shadowTexelFar).r);
    lit += step(depth, texture(shadowMapFar, p.xy + vec2(0.5, 0.5) * shadowTexelFar).r);
    return mix(lit * 0.25, 1.0, fade);
}

float SunShadow(vec3 pos, float ndl)
{
    if (shadowStrength <= 0.0 || ndl <= 0.0) return 1.0;
    vec4 lp = lightVP * vec4(pos, 1.0);
    vec3 p = lp.xyz / lp.w * 0.5 + 0.5;
    vec2 edge = abs(p.xy - 0.5) * 2.0;
    float fade = p.z >= 1.0 ? 1.0 : smoothstep(0.82, 1.0, max(edge.x, edge.y));
    // Ойрын хүрээний захаас цааш холын сүүдэр рүү зөөлөн шилжинэ
    float far = fade > 0.0 && shadowFar > 0.5 ? FarShadow(pos, ndl) : 1.0;
    if (fade >= 1.0) return mix(1.0, far, shadowStrength);
    float depth = p.z - shadowBias * (1.0 + 4.0 * (1.0 - ndl));
    float lit = 0.0;
#ifdef SOFT_SHADOWS
    // 9 цэг зөвхөн ойрд (ирмэгийн зөөлөн байдал харагдах зайд); цааш 4 цэг хангалттай
    if (distance(viewPos, pos) < 25.0)
    {
        for (int x = -1; x <= 1; ++x)
        {
            for (int y = -1; y <= 1; ++y)
            {
                lit += step(depth, texture(shadowMap, p.xy + vec2(float(x), float(y)) * shadowTexel).r);
            }
        }
        return mix(1.0, mix(lit / 9.0, far, fade), shadowStrength);
    }
#endif
    lit += step(depth, texture(shadowMap, p.xy + vec2(-0.5, -0.5) * shadowTexel).r);
    lit += step(depth, texture(shadowMap, p.xy + vec2(0.5, -0.5) * shadowTexel).r);
    lit += step(depth, texture(shadowMap, p.xy + vec2(-0.5, 0.5) * shadowTexel).r);
    lit += step(depth, texture(shadowMap, p.xy + vec2(0.5, 0.5) * shadowTexel).r);
    lit *= 0.25;
    return mix(1.0, mix(lit, far, fade), shadowStrength);
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
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0); // (тэнгэрийн үүлтэй ижил: нүдний заагт ирмэг гарахгүй)
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

// spec: (хүч, гялбааны нарийн). Материал бүр өөрөөр гялалзана: төмөр дээвэр, будагтай төмөр тод, нарийн;
// асфальт бүдэг, өргөн; өвс, шавардлага бараг үгүй. Норсон гадаргуу илүү гялалзана.
vec3 ShadeSpec(vec3 albedo, vec3 normal, vec3 pos, vec2 spec)
{
    // Цас: дээш харсан гадаргуу (газар, дээвэр) цайрна; бороо: нойтон, бараан
    float up = smoothstep(0.5, 0.85, normal.y);
    albedo = mix(albedo, vec3(0.86, 0.88, 0.92), snowCover * up);
    albedo *= 1.0 - wetness * 0.28 * up;
    float ndl = max(dot(normal, sunDirection), 0.0);
    float sun = SunShadow(pos, ndl) * CloudShade(pos);
    vec3 ambient = mix(groundLight, ambientLight, normal.y * 0.5 + 0.5);
    vec3 color = albedo * (ambient + sunLight * (ndl * sun));
    spec.x += wetness * mix(0.06, 0.22, up) + snowCover * up * 0.12;
    if (spec.x > 0.0 && ndl > 0.0)
    {
        vec3 h = normalize(normalize(viewPos - pos) + sunDirection);
        float power = mix(spec.y, max(spec.y, 60.0), wetness);
        color += sunLight * (spec.x * ndl * sun * pow(max(dot(normal, h), 0.0), power));
    }
    return color;
}

vec3 Shade(vec3 albedo, vec3 normal, vec3 pos)
{
    return ShadeSpec(albedo, normal, pos, vec2(0.0, 8.0));
}

// Товгор: өндрийн утгын (м) дэлгэцийн деривативаар normal-ыг хазайлгана (Mikkelsen-ийн surface gradient).
// Нэмэлт текстур уншихгүй: бүтцийн бараан хэсэг (хагарал, тоосгоны завсар, асфальтын нүх) хотгор болно.
vec3 BumpFromHeight(vec3 n, vec3 pos, float h)
{
    vec3 dpdx = dFdx(pos), dpdy = dFdy(pos);
    vec3 r1 = cross(dpdy, n), r2 = cross(n, dpdx);
    float det = dot(dpdx, r1);
    vec3 grad = sign(det) * (dFdx(h) * r1 + dFdy(h) * r2);
    return normalize(abs(det) * n - grad);
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
    color = mix(color, haze, fog);
    if (mistDensity > 0.0)
    {
        // Газарт наалдсан манан: нягт нь өндөр нэмэгдэх тусам экспоненциалаар буурна; цацрагийн дагуух нийлбэр
        // (хотгор, хөндийд өтгөн, толгодын орой, барилгын дээд хэсэг илүү тод)
        float h0 = viewPos.y - mistBase;
        float k = clamp(ray.y / mistHeight, -12.0, 12.0);
        float along = abs(k) > 0.001 ? (1.0 - exp(-k)) / k : 1.0;
        float amount = fogDist * mistDensity * min(exp(-h0 / mistHeight), 6.0) * along;
        float mist = min(1.0 - exp(-amount), 0.88);
        color = mix(color, mix(fogColor, haze, 0.35) * 1.04, mist);
    }
    return Tonemap(color);
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
// concrete > 0.5 (оройн alpha 254): хэвтээ гадаргуу хавтан биш цул бетон (Сүхбаатарын талбай)
vec3 SurfaceDetail(vec3 albedo, vec3 normal, vec3 pos, float concrete)
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
            if (concrete > 0.5)
            {
                d = mix(0.5, texture(wallDetail, pos.xz * 0.3).b, 0.6);
                mask = 1.0;
            }
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

// Материалын гялбаа (хүч, нарийн): өнгө, чиглэлээр нь танина (SurfaceDetail-тэй ижил дүрэм)
vec2 MaterialSpec(vec3 albedo, vec3 n)
{
    // Хөдөлдөг унаа, тоноглол (surfaceDetail = 0): будагтай төмөр
    // (морь, баавгай ч энэ shader-ээр зурагддаг тул дунд зэрэг)
    if (surfaceDetail < 0.5) return vec2(0.24, 40.0);
    vec3 an = abs(n);
    float lum = dot(albedo, vec3(0.333));
    float sat = max(albedo.r, max(albedo.g, albedo.b)) - min(albedo.r, min(albedo.g, albedo.b));
    float green = clamp((albedo.g - max(albedo.r, albedo.b)) * 12.0, 0.0, 1.0); // өвс, мод: гялбаагүй
    float gray = 1.0 - smoothstep(0.05, 0.14, sat);
    float red = albedo.r / max(albedo.g, 0.02);
    vec2 spec;
    if (an.y < 0.3) spec = vec2(0.04, 10.0); // хана: шавардлага, тоосго
    else if (an.y < 0.85)
    {
        // Налуу дээвэр: атираат төмөр (саарал, хөх, ногоон будагтай) тод, вааран дээвэр бүдэг
        float tile = smoothstep(1.15, 1.35, red) * (1.0 - gray);
        spec = mix(vec2(0.42, 48.0), vec2(0.1, 16.0), tile);
    }
    else spec = vec2(0.09 * gray * (1.0 - smoothstep(0.4, 0.6, lum)), 12.0); // асфальт, бетон: бүдэг өргөн
    spec.x *= 1.0 - green;
    return spec;
}

void main()
{
    // Гадаргуугийн чиглэл: хөрш пикселүүдийн байрлалын зөрүүгээс (камер руу харсан тал).
    vec3 faceNormal = normalize(cross(dFdx(fragPosition), dFdy(fragPosition)));
    vec3 normal = faceNormal;
    // Оройн alpha-ийн тэмдэг: 251 шил, 252 шөнө гэрэл асдаг шил, 190 ус (хагас тунгалаг),
    // 253 гөлгөр (өнгөлсөн гантиг: бүтэц, товгоргүй), 254 цул бетон (хавтангүй талбай)
    float tag = floor(fragColor.a * 255.0 + 0.5);
    bool glass = tag > 250.5 && tag < 252.5;
    bool water = aoMode < 0.5 && abs(tag - 190.0) < 0.5;
    bool polished = abs(tag - 253.0) < 0.5;
    vec3 vertexAlbedo = fragColor.rgb * colDiffuse.rgb;
    vec3 albedo = glass || polished ? vertexAlbedo : SurfaceDetail(vertexAlbedo, normal, fragPosition, step(253.5, tag) * step(tag, 254.5));
    if (surfaceBump > 0.5 && surfaceDetail > 0.5)
    {
        // Товгор (~1.5 см): бүтцийн тодролын (оройн өнгөтэй харьцуулсан) өөрчлөлтөөс; 40 м-ээс цааш алга.
        // (Дериватив нөхцөлт салбар дотор тодорхойгүй тул шил, ус, зайг үржүүлэгчээр унтраана)
        float fade = (glass || water || polished ? 0.0 : 1.0) * (1.0 - smoothstep(20.0, 40.0, distance(viewPos, fragPosition)));
        float h = dot(albedo, vec3(0.333)) / max(dot(vertexAlbedo, vec3(0.333)), 0.02);
        normal = BumpFromHeight(normal, fragPosition, h * 0.015 * fade);
    }
    vec3 lit = glass || water ? Shade(albedo, normal, fragPosition)
                              : ShadeSpec(albedo, normal, fragPosition, MaterialSpec(vertexAlbedo, faceNormal));
    normal = faceNormal;
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
uniform float terrainBump;     // 1 = ойрын газрын бүтэц товгор ("өндөр" график)

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
    float rockSpec = 0.0; // эгц хадан налуу бага зэрэг гялалзана
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
        rockSpec = steep * 0.1;
        // (дериватив салбараас гадна: доорх нөхцөлт уншилтын mip сонголтод)
        vec2 natureUv = p * 0.22, natureDx = dFdx(natureUv), natureDy = dFdy(natureUv);
        if (natureTextures > 0.5 && green < 0.99 && dist < 220.0)
        {
            // Хуурай газар (ногоон биш): шаргал цайвар газар элс, бусад нь толбо толбоор хагарсан хөрс, хайрга,
            // жижиг чулуу ээлжилнэ (том хэмжээний толбоор: давталт харагдахгүй). Ногоон өвс, 220 м-ээс цааш уншихгүй.
            vec4 n = textureGrad(natureDetail, natureUv, natureDx, natureDy);
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
        if (terrainBump > 0.5)
        {
            // Товгор (~1 см): хагарал, хайрга, өвсний бараан завсар хотгор (нэмэлт текстур уншихгүй; 45 м-ээс цааш алга)
            float h = dot(detail, vec3(0.333)) * 0.008 * (1.0 - smoothstep(25.0, 45.0, dist));
            normal = BumpFromHeight(normal, fragPosition, h);
        }
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
    vec3 lit = ShadeSpec(Dry(albedo), normal, fragPosition, vec2(rockSpec, 18.0));
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
    // Даавуу, арьсны зөөлөн гялбаа (бороонд норсон хувцас илүү)
    vec3 h = normalize(normalize(viewPos - fragPosition) + sunDirection);
    float sheen = pow(max(dot(normal, h), 0.0), 14.0) * ndl * (0.06 + wetness * 0.12);
    lit += sunLight * sheen * SunShadow(fragPosition, ndl);
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
    // Лакан бүрээс: налуу харахад тэнгэрийг тусгана (төмөр биеийг шавардлага, даавуунаас ялгана)
    float fresnel = 0.04 + 0.96 * pow(1.0 - clamp(dot(normal, view), 0.0, 1.0), 5.0);
    lit = mix(lit, SkyReflection(reflect(-view, normal)), fresnel * (0.35 + wetness * 0.25));
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
uniform float vibrance;
out vec4 finalColor;

vec3 Tonemap(vec3 x)
{
    x *= exposure;
    vec3 c = clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
    return clamp(mix(vec3(dot(c, vec3(0.299, 0.587, 0.114))), c, vibrance), 0.0, 1.0);
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
    // Тав зэргийн интерполяц: налуу нь нүдний заагт тасрахгүй (куб интерполяцаар үүлний гэрэл/сүүдэр нь налуугаас
    // тооцогддог тул тэнгэрт шулуун ирмэгтэй олон өнцөгт харагддаг байв)
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    return mix(mix(Hash(i), Hash(i + vec2(1.0, 0.0)), u.x), mix(Hash(i + vec2(0.0, 1.0)), Hash(i + vec2(1.0, 1.0)), u.x), u.y);
}
float Fbm(vec2 p)
{
    float v = 0.0;
    float a = 0.5;
    const mat2 turn = mat2(0.8, -0.6, 0.6, 0.8); // давхарга бүр эргэнэ: нүдний заагууд нэг шугамд давхцахгүй
    for (int i = 0; i < 5; ++i)
    {
        v += a * Noise(p);
        p = turn * p * 2.03 + vec2(17.1, 9.2);
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
uniform sampler2D aoTexture;    // булан, завсрын сүүдэр (1 = нээлттэй)
uniform float aoStrength;       // 0 = унтраастай
uniform sampler2D shaftTexture; // нарны туяа (R)
uniform vec3 shaftColor;        // туяаны өнгө x хүч (0 = унтраастай)
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
    if (aoStrength > 0.0) color *= mix(1.0, texture(aoTexture, fragTexCoord).r, aoStrength);
    if (bloomStrength > 0.0) color += texture(bloomTexture, fragTexCoord).rgb * bloomStrength;
    if (shaftColor.r + shaftColor.g + shaftColor.b > 0.0)
    {
        // Нэмэх биш "screen" холилт: тод хэсэг шатахгүй
        vec3 s = texture(shaftTexture, fragTexCoord).r * shaftColor;
        color = 1.0 - (1.0 - color) * (1.0 - clamp(s, 0.0, 1.0));
    }
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

    // Булан, завсрын сүүдэр (SSAO): сценийн гүнээс харах орон зайн байрлал, гадаргуугийн чиглэлийг сэргээж,
    // эргэн тойрны ~1.2 м-т хаагч байгаа эсэхийг 12 цэгээр шалгана (Alchemy AO). Хагас нягтралтай, дараа нь бүдгэрүүлнэ.
    const char* kAoFragmentShader = R"(in vec2 fragTexCoord;
uniform highp sampler2D depthTexture;
uniform vec2 tanHalf;  // (tan(fov/2) * aspect, tan(fov/2))
uniform vec2 depthTexel;
uniform float nearPlane;
uniform float farPlane;
uniform vec2 outputSize; // (uv-г gl_FragCoord-оос: GL текстурын чиглэлтэй ижил, сценийн текстуртай давхцана)
out vec4 finalColor;

float LinearDepth(vec2 uv)
{
    float z = texture(depthTexture, uv).r * 2.0 - 1.0;
    return 2.0 * nearPlane * farPlane / (farPlane + nearPlane - z * (farPlane - nearPlane));
}
vec3 ViewPos(vec2 uv)
{
    float z = LinearDepth(uv);
    return vec3((uv * 2.0 - 1.0) * tanHalf * z, -z);
}

void main()
{
    vec2 uv = gl_FragCoord.xy / outputSize;
    if (texture(depthTexture, uv).r >= 0.999999) { finalColor = vec4(1.0); return; } // тэнгэр
    vec3 p = ViewPos(uv);
    float z = -p.z;
    if (z > 90.0) { finalColor = vec4(1.0); return; }
    // Чиглэл: хөрш пикселүүдээс (ирмэг дээр алдаагүй байх үүднээс ойрхон талыг сонгоно)
    vec3 pr = ViewPos(uv + vec2(depthTexel.x, 0.0)), pl = ViewPos(uv - vec2(depthTexel.x, 0.0));
    vec3 pu = ViewPos(uv + vec2(0.0, depthTexel.y)), pd = ViewPos(uv - vec2(0.0, depthTexel.y));
    vec3 dx = abs(pr.z - p.z) < abs(p.z - pl.z) ? pr - p : p - pl;
    vec3 dy = abs(pu.z - p.z) < abs(p.z - pd.z) ? pu - p : p - pd;
    vec3 n = normalize(cross(dx, dy));
    if (dot(n, -p) < 0.0) n = -n;

    const float radius = 1.2;
    vec2 reach = min(vec2(radius) / (2.0 * tanHalf * z), vec2(0.12));
    float noise = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    float occlusion = 0.0;
    for (int i = 0; i < 12; ++i)
    {
        float t = (float(i) + 0.5) / 12.0;
        float angle = float(i) * 2.39996 + noise * 6.2832;
        vec3 q = ViewPos(uv + vec2(cos(angle), sin(angle)) * reach * t);
        vec3 v = q - p;
        float vv = dot(v, v);
        float range = 1.0 - smoothstep(radius * radius, radius * radius * 4.0, vv); // хол зүйл хаахгүй
        // Өнцгийн хазайлт: бараг хавтгай (газрын торны үе, гүний нарийвчлал) хаагч болохгүй, судал гарахгүй
        occlusion += max(dot(v, n) - 0.25 * sqrt(vv) - 0.01 * z, 0.0) / (vv + 0.03) * range;
    }
    float ao = clamp(1.0 - occlusion * (2.6 / 12.0), 0.0, 1.0);
    ao = mix(ao, 1.0, smoothstep(45.0, 90.0, z));
    finalColor = vec4(vec3(ao), 1.0);
}
)";

    // Нарны туяа: нар руу чиглэсэн радиал бүдгэрүүлэлт. Тэнгэрийн пиксел (гүн = 1) гэрэлтэнэ, барилга, мод,
    // уулс түүнийг хааж, завсраар нь туяа тусна.
    const char* kShaftFragmentShader = R"(in vec2 fragTexCoord;
uniform highp sampler2D depthTexture;
uniform vec2 sunPosition; // нарны байрлал дэлгэц дээр (GL uv: доороос дээш)
uniform float aspect;
uniform vec2 outputSize;
out vec4 finalColor;
void main()
{
    vec2 uv = gl_FragCoord.xy / outputSize;
    vec2 delta = (uv - sunPosition) * (0.92 / 40.0);
    float noise = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    vec2 c = uv - delta * noise;
    float light = 0.0, weight = 1.0;
    for (int i = 0; i < 40; ++i)
    {
        c -= delta;
        light += step(0.999999, texture(depthTexture, c).r) * weight;
        weight *= 0.965;
    }
    light /= 26.0;
    vec2 d = (uv - sunPosition) * vec2(aspect, 1.0);
    float falloff = 1.0 - smoothstep(0.0, 1.1, length(d));
    finalColor = vec4(light * falloff * falloff, 0.0, 0.0, 1.0);
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
    const int kFarShadowMapSize = 1024;
    const float kFarShadowRadius = 180.0f;
#else
    const char* kShaderHeader = "#version 330\n";
    const char* kLightingDefines = "#define SOFT_SHADOWS\n";
    const int kShadowMapSize = 2048;
    const float kShadowRadius = 70.0f;
    const int kFarShadowMapSize = 2048;
    const float kFarShadowRadius = 300.0f;
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
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
        // Өнгөний хавсралтгүй framebuffer: draw buffer нь анхдагчаар COLOR_ATTACHMENT0 хэвээр бол Intel драйвер
        // сүүдрийн зураг руу юм зурсны дараа бүх дэлгэцийг (HUD хүртэл) хар болгодог.
        rlEnableFramebuffer(target.id);
        glDrawBuffer(0); // GL_NONE
        glReadBuffer(0);
#endif
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

    // Сцений текстур: өнгө + гүний текстур (raylib-ийн LoadRenderTexture гүнийг уншиж болохгүй renderbuffer-т хадгалдаг).
    // Гүнийг AO, нарны туяа уншина. Бүтэлгүйтвэл энгийн render texture.
    RenderTexture2D LoadSceneTarget(int width, int height, bool depthTexture, bool& hasDepth)
    {
        hasDepth = false;
        if (!depthTexture) return LoadRenderTexture(width, height);
        RenderTexture2D target = {};
        target.id = rlLoadFramebuffer();
        if (target.id == 0) return LoadRenderTexture(width, height);
        rlEnableFramebuffer(target.id);
        target.texture.id = rlLoadTexture(nullptr, width, height, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
        target.texture.width = width;
        target.texture.height = height;
        target.texture.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        target.texture.mipmaps = 1;
#if defined(__EMSCRIPTEN__)
        glGenTextures(1, &target.depth.id);
        glBindTexture(GL_TEXTURE_2D, target.depth.id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);
#else
        target.depth.id = rlLoadTextureDepth(width, height, false);
#endif
        target.depth.width = width;
        target.depth.height = height;
        target.depth.format = 19;
        target.depth.mipmaps = 1;
        rlFramebufferAttach(target.id, target.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
        rlFramebufferAttach(target.id, target.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
        const bool complete = rlFramebufferComplete(target.id);
        rlDisableFramebuffer();
        if (!complete || target.texture.id == 0 || target.depth.id == 0)
        {
            TraceLog(LOG_WARNING, "ENVIRONMENT: scene depth texture unavailable, ambient occlusion disabled");
            if (target.texture.id) rlUnloadTexture(target.texture.id);
            if (target.depth.id) rlUnloadTexture(target.depth.id);
            rlUnloadFramebuffer(target.id);
            return LoadRenderTexture(width, height);
        }
        rlTextureParameters(target.depth.id, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_NEAREST);
        rlTextureParameters(target.depth.id, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_NEAREST);
        rlTextureParameters(target.depth.id, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_CLAMP);
        rlTextureParameters(target.depth.id, RL_TEXTURE_WRAP_T, RL_TEXTURE_WRAP_CLAMP);
        hasDepth = true;
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
    characterDepthShader = LoadVersionedShader(kCharacterVertexShader, kDepthFragmentShader, false);
    characterDepthMaterial = MaterialWith(characterDepthShader);
    skyShader = LoadVersionedShader(kScreenVertexShader, kSkyFragmentShader, false);
    postShader = LoadVersionedShader(kScreenVertexShader, kPostFragmentShader, false);
    brightShader = LoadVersionedShader(kScreenVertexShader, kBrightFragmentShader, false);
    blurShader = LoadVersionedShader(kScreenVertexShader, kBlurFragmentShader, false);
    aoShader = LoadVersionedShader(kScreenVertexShader, kAoFragmentShader, false);
    shaftShader = LoadVersionedShader(kScreenVertexShader, kShaftFragmentShader, false);

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

    // AO, нарны туяа нь нэмэлт: ачаалагдаагүй бол зөвхөн тэд унтарна
    if (failed(aoShader)) { TraceLog(LOG_WARNING, "ENVIRONMENT: ambient occlusion shader failed"); aoShader = {}; }
    if (failed(shaftShader)) { TraceLog(LOG_WARNING, "ENVIRONMENT: light shaft shader failed"); shaftShader = {}; }

    sunDirection = skySunDirection = Vector3Normalize(kSunDirection);
    for (Shader s : { shader, terrainShader, detailShader, characterShader, propShader, vehicleShader })
    {
        SetVec3(s, "sunDirection", sunDirection);
        SetInt(s, "shadowMap", kShadowSlot);
        SetInt(s, "shadowMapFar", kFarShadowSlot);
        SetFloat(s, "shadowStrength", 0.0f);
        SetFloat(s, "shadowFar", 0.0f);
        SetFloat(s, "fogScale", 1.0f);
        SetFloat(s, "mistHeight", 7.0f);
    }
    SetVec3(skyShader, "sunDirection", sunDirection);
    // HDR-д тод: эргэн тойрны гэрэлт хүрээнээс (1-ээс их) бараан саарал толбо болохгүй
    SetVec3(skyShader, "sunCore", Vector3Scale(ToVector(kSunCore), 4.0f));
    // Гадаргуугийн бүтэц: зөвхөн хөдөлдөггүй геометрт (унаанд хэв нь гулсаж харагдана)
    SetFloat(shader, "surfaceDetail", 1.0f);
    if (propShader.id != shader.id) SetFloat(propShader, "surfaceDetail", 0.0f);

    shadowSize = kShadowMapSize;
    shadowRadius = kShadowRadius;
    shadowMap = LoadShadowTarget(shadowSize);
    farShadowRadius = kFarShadowRadius; // (зураг нь анх хэрэг болоход: "өндөр" графикт л)

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
    for (Material* m : { &material, &terrainMaterial, &detailMaterial, &propMaterial, &depthMaterial, &characterDepthMaterial })
    {
        RL_FREE(m->maps);
        m->maps = nullptr;
    }
    unsigned int released[16] = {};
    int count = 0;
    for (Shader s : { shader, terrainShader, detailShader, propShader, depthShader, characterDepthShader, characterShader, vehicleShader, skyShader, postShader,
                      brightShader, blurShader, aoShader, shaftShader })
    {
        if (s.id == 0 || s.id == rlGetShaderIdDefault()) continue;
        bool done = false;
        for (int i = 0; i < count; ++i) done = done || released[i] == s.id;
        if (done) continue;
        released[count++] = s.id;
        UnloadShader(s);
    }
    UnloadShadowTarget(shadowMap);
    UnloadShadowTarget(farShadowMap);
    if (scene.id) UnloadRenderTexture(scene);
    for (auto& cached : sceneCache) UnloadRenderTexture(cached.first);
    sceneCache.clear();
    if (bloomA.id) UnloadRenderTexture(bloomA);
    if (bloomB.id) UnloadRenderTexture(bloomB);
    UnloadScreenEffects();
}

void Environment::UnloadScreenEffects()
{
    for (RenderTexture2D* target : { &aoA, &aoB, &shafts })
    {
        if (target->id) UnloadRenderTexture(*target);
        *target = {};
    }
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

    // Сар: үдийн нарны эсрэг талд (шөнийн гэрлийн чиглэл, тэнгэр дэх сар)
    Vector3 MoonDirection()
    {
        const Vector3 sun = Vector3Normalize(kSunDirection);
        return Vector3Normalize({ -sun.x, 0.55f, -sun.z });
    }

    // Нарны зам: t = 0.955-д (−0.045) мандаж, 0.61-д жаргана; үдэд (t ≈ 0.28) хуучин тогтмол чиглэлтэй ижил.
    // sky: тэнгэр дэх жинхэнэ байрлал (хаяанаас доош орно), light: гэрэл, сүүдрийн (10°-аас доош буухгүй)
    void SunPath(float t, Vector3& sky, Vector3& light)
    {
        const float u = t > 0.8f ? t - 1.0f : t;
        const float rise = -0.045f, set = 0.61f;
        const float phase = (u - rise) / (set - rise); // 0 мандах, 0.5 үд, 1 жаргах
        const Vector3 noon = Vector3Normalize(kSunDirection);
        const float elevation = std::sin(PI * phase) * std::asin(noon.y);
        const float azimuth = std::atan2(noon.z, noon.x) + (0.5f - phase) * 3.3f; // ~190° эргэнэ
        auto at = [&](float e) { return Vector3{ std::cos(e) * std::cos(azimuth), std::sin(e), std::cos(e) * std::sin(azimuth) }; };
        sky = at(std::max(elevation, -0.15f));
        light = at(std::max(elevation, kMinSunElevation));
    }

    // Цаг агаараас хамаарах өнгө, гэрэл, манан (өдрийн суурь өнгөн дээр)
    struct Palette
    {
        Vector3 zenith, horizon, haze, sun, ambient, ground, cloudLit, cloudDark;
        float fogDensity, shadowStrength, cloudShade;
    };

    Palette Weathered(Palette p, WeatherKind kind, float dayLight)
    {
        auto grey = [&](Vector3 v, Vector3 to, float amount) { return Mix3(v, Vector3Scale(to, dayLight), amount); };
        switch (kind)
        {
        case WeatherKind::Clear: p.cloudShade = 0.28f; break;
        case WeatherKind::Cloudy:
            p.zenith = grey(p.zenith, { 0.46f, 0.54f, 0.64f }, 0.45f);
            p.horizon = grey(p.horizon, { 0.72f, 0.76f, 0.80f }, 0.3f);
            p.sun = Vector3Scale(p.sun, 0.62f);
            p.ambient = Vector3Scale(p.ambient, 1.08f);
            p.shadowStrength *= 0.6f;
            p.fogDensity *= 1.3f;
            p.cloudShade = 0.42f;
            break;
        case WeatherKind::Rain:
            p.zenith = grey(p.zenith, { 0.40f, 0.43f, 0.48f }, 0.8f);
            p.horizon = grey(p.horizon, { 0.56f, 0.59f, 0.63f }, 0.75f);
            p.haze = grey(p.haze, { 0.62f, 0.64f, 0.66f }, 0.7f);
            p.cloudLit = grey(p.cloudLit, { 0.62f, 0.64f, 0.68f }, 0.7f);
            p.cloudDark = grey(p.cloudDark, { 0.32f, 0.34f, 0.38f }, 0.7f);
            p.sun = Vector3Scale(p.sun, 0.32f);
            p.ambient = Vector3Scale(p.ambient, 1.05f);
            p.shadowStrength *= 0.25f;
            p.fogDensity *= 2.2f;
            p.cloudShade = 0.0f;
            break;
        case WeatherKind::Snow:
            p.zenith = grey(p.zenith, { 0.58f, 0.62f, 0.68f }, 0.75f);
            p.horizon = grey(p.horizon, { 0.82f, 0.84f, 0.88f }, 0.75f);
            p.haze = grey(p.haze, { 0.88f, 0.88f, 0.9f }, 0.6f);
            p.cloudLit = grey(p.cloudLit, { 0.86f, 0.87f, 0.9f }, 0.6f);
            p.cloudDark = grey(p.cloudDark, { 0.6f, 0.62f, 0.66f }, 0.6f);
            p.sun = Vector3Scale(p.sun, 0.42f);
            p.ambient = Vector3Scale(p.ambient, 1.18f);
            p.shadowStrength *= 0.3f;
            p.fogDensity *= 2.6f;
            p.cloudShade = 0.0f;
            break;
        case WeatherKind::Dust:
            p.zenith = grey(p.zenith, { 0.62f, 0.56f, 0.46f }, 0.55f);
            p.horizon = grey(p.horizon, { 0.78f, 0.64f, 0.46f }, 0.85f);
            p.haze = grey(p.haze, { 0.95f, 0.74f, 0.48f }, 0.7f);
            p.cloudLit = grey(p.cloudLit, { 0.86f, 0.74f, 0.58f }, 0.6f);
            p.sun = Mix3(Vector3Scale(p.sun, 0.7f), Vector3Scale({ 0.8f, 0.62f, 0.42f }, dayLight), 0.4f);
            p.ambient = Mix3(p.ambient, Vector3Scale({ 0.46f, 0.40f, 0.34f }, dayLight), 0.5f);
            p.shadowStrength *= 0.5f;
            p.fogDensity *= 3.0f;
            p.cloudShade = 0.15f;
            break;
        }
        return p;
    }

    Palette MixPalette(const Palette& a, const Palette& b, float t)
    {
        auto f = [&](float x, float y) { return x + (y - x) * t; };
        return { Mix3(a.zenith, b.zenith, t), Mix3(a.horizon, b.horizon, t), Mix3(a.haze, b.haze, t), Mix3(a.sun, b.sun, t),
                 Mix3(a.ambient, b.ambient, t), Mix3(a.ground, b.ground, t), Mix3(a.cloudLit, b.cloudLit, t),
                 Mix3(a.cloudDark, b.cloudDark, t), f(a.fogDensity, b.fogDensity), f(a.shadowStrength, b.shadowStrength),
                 f(a.cloudShade, b.cloudShade) };
    }

    // Үүлшилт, салхи (цаг агаар бүрийн)
    void WeatherSky(WeatherKind kind, float& cover, Vector2& wind)
    {
        switch (kind)
        {
        case WeatherKind::Clear: cover = 0.32f; wind = { 0.8f, 0.35f }; break;
        case WeatherKind::Cloudy: cover = 0.68f; wind = { 1.4f, 0.6f }; break;
        case WeatherKind::Rain: cover = 0.95f; wind = { 1.8f, 0.9f }; break;
        case WeatherKind::Snow: cover = 0.9f; wind = { 1.2f, 0.7f }; break;
        case WeatherKind::Dust: cover = 0.2f; wind = { 3.2f, 1.4f }; break;
        }
    }

    // Бороо, цас, шороон шуурганы хүч: шиврэх, хүчтэй орох ээлжилнэ (~1 минутын давалгаа)
    float Intensity(WeatherKind kind, float time)
    {
        switch (kind)
        {
        case WeatherKind::Rain:
            return std::clamp(0.62f + 0.26f * std::sin(time * 0.13f) + 0.14f * std::sin(time * 0.37f + 1.7f), 0.15f, 1.0f);
        case WeatherKind::Snow: return 0.75f + 0.2f * std::sin(time * 0.09f);
        case WeatherKind::Dust: return 0.75f + 0.25f * std::sin(time * 0.21f + 0.5f);
        default: return 0.0f;
        }
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
    SetFloat(terrainShader, "terrainBump", quality >= 2 || fxTest ? 1.0f : 0.0f);
    if (quality < 2)
    {
        // AO, нарны туяа, холын сүүдэр: зөвхөн "өндөр" графикт (видео санах ойг чөлөөлнө)
        UnloadScreenEffects();
        UnloadShadowTarget(farShadowMap);
        farShadowReady = false;
        if (farShadowSize > 0) farShadowSize = 0;
    }
    lastApplied = -1.0f; // (үүлний сүүдэр, нарны хөдөлгөөн түвшнээс хамаарна: SetTimeOfDay дахин тооцно)
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

void Environment::SetWeather(WeatherKind kind, bool instant)
{
    lastApplied = -1.0f; // дараагийн SetTimeOfDay дахин тооцно
    if (instant)
    {
        // Даалгаврын эхэнд: аль хэдийн бороо орж (норсон), цас хунгарласан байна
        weather = previousWeather = kind;
        weatherMix = 1.0f;
        precipitation = Intensity(kind, time);
        wetness = kind == WeatherKind::Rain ? 0.7f : 0.0f;
        snowCover = kind == WeatherKind::Snow ? 0.6f : 0.0f;
        WeatherSky(kind, cloudCover, wind);
        groundKnown = false;
        return;
    }
    if (kind == weather) return;
    previousWeather = weather;
    weather = kind;
    weatherMix = 0.0f;
}

void Environment::Update(float dt)
{
    time += dt;
    // Цаг агаарын шилжилт ~20 секунд: тэнгэр, үүл, салхи, манан аажим солигдоно
    weatherMix = std::min(1.0f, weatherMix + dt / 20.0f);
    float coverA = 0.0f, coverB = 0.0f;
    Vector2 windA = {}, windB = {};
    WeatherSky(previousWeather, coverA, windA);
    WeatherSky(weather, coverB, windB);
    cloudCover = coverA + (coverB - coverA) * weatherMix;
    wind = Vector2Lerp(windA, windB, weatherMix);

    auto share = [&](WeatherKind kind) {
        return (previousWeather == kind ? (1.0f - weatherMix) * Intensity(kind, time) : 0.0f) +
               (weather == kind ? weatherMix * Intensity(kind, time) : 0.0f);
    };
    precipitation = Intensity(previousWeather, time) * (1.0f - weatherMix) + Intensity(weather, time) * weatherMix;
    // Гадаргуу: бороонд ~40 с-д норж, зогсоход ~2.5 минутад хатна; цас ~3 минутад хунгарлаж, аажим хайлна
    const float rain = share(WeatherKind::Rain), snow = share(WeatherKind::Snow);
    const float wetTarget = rain > 0.05f ? std::min(1.0f, 0.45f + rain * 0.6f) : 0.0f;
    wetness = wetTarget > wetness ? std::min(wetTarget, wetness + dt / 40.0f) : std::max(wetTarget, wetness - dt / 150.0f);
    const float snowTarget = snow > 0.05f ? 0.85f : 0.0f;
    snowCover = snowTarget > snowCover ? std::min(snowTarget, snowCover + dt / 200.0f) : std::max(snowTarget, snowCover - dt / 300.0f);

    // Намхан газрын манангийн түвшин камерыг зөөлөн дагана (шат, хад давахад үсрэхгүй)
    if (!groundKnown)
    {
        groundLevel = groundTarget;
        groundKnown = true;
    }
    groundLevel += (groundTarget - groundLevel) * std::min(1.0f, dt * 0.5f);

    // Мэдэгдэхүйц өөрчлөгдсөн бол shader-т дахин тавина (кадр бүр биш; бороо, цасны хүч зөвхөн бөөмст)
    const float now[4] = { weatherMix, wetness, snowCover, groundLevel * 0.02f };
    for (int i = 0; i < 4; ++i)
    {
        if (std::fabs(now[i] - appliedWeather[i]) > 0.01f) lastApplied = -1.0f;
    }
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
    Palette day = {};
    day.zenith = Mix3(Mix3({ 0.16f, 0.36f, 0.72f }, { 0.20f, 0.27f, 0.52f }, golden), { 0.02f, 0.04f, 0.10f }, night);
    day.horizon = Mix3(Mix3({ 0.66f, 0.76f, 0.88f }, { 1.05f, 0.62f, 0.38f }, golden), { 0.10f, 0.13f, 0.20f }, night);
    day.haze = Mix3(Mix3({ 1.15f, 0.98f, 0.76f }, { 1.35f, 0.70f, 0.32f }, golden), { 0.12f, 0.15f, 0.24f }, night);
    day.sun = Mix3(Mix3({ 1.38f, 1.24f, 1.02f }, { 1.55f, 0.86f, 0.46f }, golden), { 0.13f, 0.17f, 0.28f }, night);
    day.ambient = Mix3(Mix3({ 0.32f, 0.42f, 0.60f }, { 0.28f, 0.31f, 0.48f }, golden), { 0.17f, 0.20f, 0.31f }, night);
    day.ground = Mix3(Mix3({ 0.30f, 0.26f, 0.18f }, { 0.34f, 0.22f, 0.14f }, golden), { 0.07f, 0.07f, 0.09f }, night);
    day.cloudLit = Mix3(Mix3({ 0.98f, 0.98f, 1.0f }, { 1.0f, 0.76f, 0.58f }, golden), { 0.16f, 0.18f, 0.25f }, night);
    day.cloudDark = Mix3(Mix3({ 0.64f, 0.68f, 0.76f }, { 0.50f, 0.40f, 0.46f }, golden), { 0.07f, 0.08f, 0.12f }, night);
    day.fogDensity = kFogDensity * (1.0f + 0.8f * night);
    day.shadowStrength = 0.72f * (1.0f - night) * (1.0f - golden * 0.3f);
    // Өнгөний шилжилтийн гэрэлтүүлэг: шөнө бага зэрэг өндөр (харанхуйд ч харагдахуйц)
    const float exposure = 0.64f + 0.5f * night;

    // --- Нар: зүүнээс баруун тийш. "Бага" графикт тогтмол (барилгын хуурамч сүүдэр тэр чиглэлтэй) ---
    const bool fxTest = std::getenv("ZM_FX") != nullptr;
    Vector3 lightSun = {};
    SunPath(t, skySunDirection, lightSun);
    if (quality == 0 && !fxTest) sunDirection = skySunDirection = Vector3Normalize(kSunDirection);
    else sunDirection = Vector3Normalize(Vector3Lerp(lightSun, MoonDirection(), night)); // шөнө сарны гэрэл
    // Нар хаяанд ойртоход сүүдэр бүдгэрнэ (жаргахдаа манан, тоосонд уусна)
    day.shadowStrength *= DayCurve(0.02f, 0.18f, skySunDirection.y) * (1.0f - night) + night;

    // --- Цаг агаар: өмнөхөөс одоогийнх руу аажим ---
    const float dayLight = 1.0f - night * 0.85f;
    const Palette p = MixPalette(Weathered(day, previousWeather, dayLight), Weathered(day, weather, dayLight), weatherMix);
    const Vector3 zenith = p.zenith, horizon = p.horizon, haze = p.haze, sun = p.sun, ambient = p.ambient, ground = p.ground;
    const float fogDensity = p.fogDensity, shadowStrength = p.shadowStrength;

    // Газар дээгүүр гүйх үүлний сүүдэр: цэлмэг, үүлэрхэг өдөр (бороо, цас, шөнө бүрэн бүрхэг/харанхуй тул үгүй;
    // хамгийн бага графикт зардал хэмнэнэ)
    float cloudShade = quality == 0 && !fxTest ? 0.0f : p.cloudShade;
    cloudShade *= 1.0f - night;
    skyZenith = ToColor(zenith);
    skyHorizon = ToColor(horizon);
    sunHaze = haze;
    sunLightColor = sun;
    // Бөөмсийн гэрэл: тэнгэрийн гэрэл + нарны хагас (утаа, тоос хагас тунгалаг тул бүтэн нар тусахгүй)
    particleLight = ToColor(Vector3Scale(Vector3Add(ambient, Vector3Scale(sun, 0.55f * (1.0f - cloudShade * 0.5f))), 0.82f));

    // Намхан газрын манан: үүр цайх үед өтгөн (~1 минутад сарнина), шөнө, бороо, цасны дараа сийрэг
    const float m = t > 0.5f ? t - 1.0f : t; // үүр цайх ≈ 0
    const float morning = 1.0f - DayCurve(0.0f, 0.16f, std::fabs(m - 0.01f));
    const float dust = (weather == WeatherKind::Dust ? weatherMix : 0.0f) + (previousWeather == WeatherKind::Dust ? 1.0f - weatherMix : 0.0f);
    const float mist = (0.0003f + 0.0035f * morning + 0.0012f * night + 0.0018f * wetness + 0.001f * snowCover) * (1.0f - dust);

    // Өнгөлөг байдал: өдөр тод (тал нутгийн тунгалаг агаар), шөнө, бороо, цасанд бүдэг
    const float vibrance = 1.22f - 0.18f * night - 0.1f * std::max(wetness, snowCover);
    SetFloat(skyShader, "vibrance", vibrance);
    for (Shader s : { shader, terrainShader, detailShader, characterShader, propShader, vehicleShader })
    {
        if (s.id == 0) continue;
        SetFloat(s, "vibrance", vibrance);
        SetVec3(s, "sunDirection", sunDirection);
        SetFloat(s, "mistDensity", mist);
        SetFloat(s, "mistBase", groundLevel);
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
    SetVec3(skyShader, "cloudLit", p.cloudLit);
    SetVec3(skyShader, "cloudDark", p.cloudDark);
    SetFloat(skyShader, "night", night);
    SetFloat(skyShader, "cloudCover", cloudCover);
    SetVec3(skyShader, "sunDirection", skySunDirection);
    dayShadow = shadowStrength;
    for (Shader s : { shader, terrainShader, detailShader, characterShader, propShader, vehicleShader })
    {
        SetFloat(s, "snowCover", snowCover);
        SetFloat(s, "wetness", wetness);
    }
    appliedWeather[0] = weatherMix;
    appliedWeather[1] = wetness;
    appliedWeather[2] = snowCover;
    appliedWeather[3] = groundLevel * 0.02f;

    // Нарны туяа: нар тэнгэрт байхад; үүлэрхэг, бороонд бүдэг, жаргах үед тод (дулаан өнгөтэй)
    const float clearSky = 1.0f - std::clamp((cloudCover - 0.35f) * 1.4f, 0.0f, 0.85f);
    shaftStrength = DayCurve(-0.02f, 0.1f, skySunDirection.y) * (1.0f - night) * clearSky * (0.32f + golden * 0.35f) * (1.0f + dust * 0.5f);

    // Дэлгэцийн өнгө: өдөр тод, үдэш дулаан, шөнө хөхөвтөр бүдэг
    const float wet = std::max(wetness, snowCover);
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
    (void)camera;
    ClearBackground(skyHorizon); // тэнгэр өөрөө DrawSkyDome-оор (газар, барилгын дараа)
}

namespace
{
    // Камер руу харсан дугуй (3D, гэрэлтүүлэггүй): төвөөс захруу өнгө шилжинэ
    void DrawDisc3D(Vector3 center, Vector3 right, Vector3 up, float radius, Color inner, Color outer)
    {
        const int segments = 20;
        rlBegin(RL_TRIANGLES);
        for (int i = 0; i < segments; ++i)
        {
            const float a0 = 2.0f * PI * (float)i / (float)segments, a1 = 2.0f * PI * (float)(i + 1) / (float)segments;
            const Vector3 p0 = Vector3Add(center, Vector3Add(Vector3Scale(right, std::cos(a0) * radius), Vector3Scale(up, std::sin(a0) * radius)));
            const Vector3 p1 = Vector3Add(center, Vector3Add(Vector3Scale(right, std::cos(a1) * radius), Vector3Scale(up, std::sin(a1) * radius)));
            rlColor4ub(inner.r, inner.g, inner.b, inner.a);
            rlVertex3f(center.x, center.y, center.z);
            rlColor4ub(outer.r, outer.g, outer.b, outer.a);
            rlVertex3f(p1.x, p1.y, p1.z);
            rlVertex3f(p0.x, p0.y, p0.z);
        }
        rlEnd();
    }
}

void Environment::DrawSkyDome(const Camera3D& camera) const
{
    // Бага нягтралтай текстурт зурж байвал түүний хэмжээгээр
    const int width = sceneActive ? scene.texture.width : ViewWidth();
    const int height = sceneActive ? scene.texture.height : ViewHeight();
    const Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    const Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera.up));
    const Vector3 up = Vector3CrossProduct(right, forward);
    const float tanHalf = std::tan(camera.fovy * DEG2RAD * 0.5f);
    const float aspect = (float)width / (float)std::max(1, height);
    // Харах зүгийн хавтгай, алсын хязгаараас (12 км) арай ойр: газар, барилга, уулсын гүнтэй харьцуулагдаж,
    // зөвхөн ил харагдах тэнгэрийн пиксел тооцогдоно. Гүнд бичихгүй (AO, нарны туяа тэнгэрийг гүн = 1-ээр танина).
    const float distance = 11500.0f, margin = 1.05f;
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    rlDisableBackfaceCulling();
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
        // Булан бүр: дэлгэцийн булан (shader uv-ээс харах чиглэлээ тооцно; захаас бага зэрэг гадуур)
        const float corners[4][2] = { { -1.0f, 1.0f }, { -1.0f, -1.0f }, { 1.0f, -1.0f }, { 1.0f, 1.0f } };
        for (const auto& c : corners)
        {
            const float x = c[0] * margin, y = c[1] * margin;
            const Vector3 p = Vector3Add(camera.position, Vector3Scale(Vector3Add(forward, Vector3Add(Vector3Scale(right, x * tanHalf * aspect),
                                                                                                      Vector3Scale(up, y * tanHalf))), distance));
            rlTexCoord2f(x * 0.5f + 0.5f, 0.5f - y * 0.5f);
            rlVertex3f(p.x, p.y, p.z);
        }
        rlEnd();
        rlSetTexture(0);
        EndShaderMode();
    }

    if (night > 0.05f)
    {
        // Од, сар (гэрэлтүүлэггүй): тэнгэрийн бөмбөрцөг дээр, уул, барилга гүнээрээ хаана
        rlDrawRenderBatchActive();
        rlSetShader(rlGetShaderIdDefault(), rlGetShaderLocsDefault());
        rlSetTexture(rlGetTextureIdDefault());
        const float pixel = 2.0f * tanHalf * distance / (float)std::max(1, height); // нэг пикселийн хэмжээ тэр зайд
        const float scale = (float)height / 720.0f;
        const float clear = 1.0f - std::clamp((cloudCover - 0.3f) * 1.6f, 0.0f, 0.9f);
        for (int i = 0; i < 260; ++i)
        {
            const float a = (float)((i * 2654435761u) % 10000u) / 10000.0f * 2.0f * PI;
            const float e = 0.08f + (float)((i * 40503u + 17u) % 10000u) / 10000.0f * 1.35f;
            const Vector3 dir = { std::cos(e) * std::sin(a), std::sin(e), std::cos(e) * std::cos(a) };
            if (Vector3DotProduct(forward, dir) < 0.2f) continue;
            const float twinkle = 0.65f + 0.35f * std::sin((float)GetTime() * (1.3f + (float)(i % 7) * 0.4f) + (float)i);
            const float size = (i % 9 == 0 ? 1.8f : 1.1f) * scale * pixel;
            const Color star = Fade(WHITE, night * twinkle * 0.9f * clear);
            DrawDisc3D(Vector3Add(camera.position, Vector3Scale(dir, distance)), right, up, size, star, star);
        }
        // Сар: үдийн нарны эсрэг талд (шөнийн гэрлийн чиглэл)
        const Vector3 moon = MoonDirection();
        if (Vector3DotProduct(forward, moon) > 0.15f)
        {
            const Vector3 p = Vector3Add(camera.position, Vector3Scale(moon, distance));
            const float alpha = night * (0.35f + 0.65f * clear), s = scale * pixel;
            auto at = [&](float dx, float dy) { return Vector3Add(p, Vector3Add(Vector3Scale(right, dx * s), Vector3Scale(up, -dy * s))); };
            DrawDisc3D(p, right, up, 90.0f * s, Fade({ 190, 205, 255, 255 }, 0.25f * alpha), Fade({ 190, 205, 255, 255 }, 0.0f));
            DrawDisc3D(p, right, up, 20.0f * s, Fade({ 232, 236, 246, 255 }, alpha), Fade({ 232, 236, 246, 255 }, alpha));
            DrawDisc3D(at(7.0f, -3.0f), right, up, 4.0f * s, Fade({ 196, 200, 214, 255 }, alpha), Fade({ 196, 200, 214, 255 }, alpha));
            DrawDisc3D(at(-6.0f, 6.0f), right, up, 3.0f * s, Fade({ 200, 204, 218, 255 }, alpha), Fade({ 200, 204, 218, 255 }, alpha));
        }
        rlDrawRenderBatchActive();
        rlSetTexture(0);
    }
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    BeginShaderMode(shader); // орчны гэрэлтүүлэг (Begin) үргэлжилнэ
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
    sceneCamera = camera;
    rlActiveTextureSlot(kShadowSlot);
    rlEnableTexture(strength > 0.0f ? shadowMap.depth.id : rlGetTextureIdDefault());
    const bool far = strength > 0.0f && farShadowReady && FarShadowsEnabled() && farShadowMap.id != 0;
    rlActiveTextureSlot(kFarShadowSlot);
    rlEnableTexture(far ? farShadowMap.depth.id : rlGetTextureIdDefault());
    rlActiveTextureSlot(0);
    const float texel = 1.0f / (float)std::max(1, shadowSize);
    // Ортографын гүн шугаман: near..far (Game: rlSetClipPlanes) → 0..1. ~15 см-ийн хазайлт (холынх нүдний хэмжээтэй
    // пропорциональ томорно).
    const float depthRange = (float)(rlGetCullDistanceFar() - rlGetCullDistanceNear());
    const float bias = 0.15f / depthRange;
    const float farBias = 0.15f * (farShadowRadius / (float)std::max(1, farShadowSize)) / (shadowRadius / (float)shadowSize) / depthRange;
    for (Shader s : { shader, terrainShader, detailShader, characterShader, propShader, vehicleShader })
    {
        if (s.id == 0) continue;
        SetVec3(s, "viewPos", camera.position);
        SetFloat(s, "shadowStrength", strength);
        SetFloat(s, "shadowFar", far ? 1.0f : 0.0f);
        if (strength > 0.0f)
        {
            SetInt(s, "shadowMap", kShadowSlot);
            const int location = GetShaderLocation(s, "lightVP");
            if (location >= 0) SetShaderValueMatrix(s, location, lightViewProj);
            SetFloat(s, "shadowTexel", texel);
            SetFloat(s, "shadowBias", bias);
        }
        if (far)
        {
            SetInt(s, "shadowMapFar", kFarShadowSlot);
            const int location = GetShaderLocation(s, "lightVPFar");
            if (location >= 0) SetShaderValueMatrix(s, location, farLightViewProj);
            SetFloat(s, "shadowTexelFar", 1.0f / (float)farShadowSize);
            SetFloat(s, "shadowBiasFar", farBias);
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

    BeginLightPass(shadowMap, kShadowSlot, center, shadowRadius, shadowSize, 300.0f, lightViewProj);
    return true;
}

bool Environment::BeginFarShadowPass(Vector3 center)
{
    if (!FarShadowsEnabled() || dayShadow <= 0.01f)
    {
        farShadowReady = false;
        return false;
    }
    if (farShadowMap.id == 0)
    {
        farShadowMap = LoadShadowTarget(kFarShadowMapSize);
        farShadowSize = farShadowMap.id != 0 ? kFarShadowMapSize : -1;
        if (farShadowMap.id == 0) return false;
    }
    // Хол зайн сүүдэр бүдүүн: 4 кадр тутам хангалттай (нар аажим хөдөлнө)
    if (farShadowReady && (farShadowFrame++ & 3) != 0) return false;
    BeginLightPass(farShadowMap, kFarShadowSlot, center, farShadowRadius, farShadowSize, 700.0f, farLightViewProj);
    farShadowReady = true;
    return true;
}

void Environment::BeginLightPass(const RenderTexture2D& map, int slot, Vector3 center, float radius, int size, float distance,
                                 Matrix& viewProj)
{
    // Өмнөх frame-ийн сүүдрийн зургийг уншихаа больж (өөр рүүгээ бичих үед уншихгүй)
    rlActiveTextureSlot(slot);
    rlDisableTexture();
    rlActiveTextureSlot(0);

    // Камер хөдлөхөд сүүдрийн ирмэг "гүйхгүй": төвийг сүүдрийн зургийн нүдний хэмжээгээр бүхэлчилнэ.
    const Vector3 sun = sunDirection;
    const Vector3 right = Vector3Normalize(Vector3CrossProduct(sun, { 0.0f, 1.0f, 0.0f }));
    const Vector3 up = Vector3CrossProduct(right, sun);
    const float texel = 2.0f * radius / (float)size;
    const float u = Vector3DotProduct(center, right), v = Vector3DotProduct(center, up);
    center = Vector3Add(center, Vector3Add(Vector3Scale(right, std::floor(u / texel) * texel - u),
                                           Vector3Scale(up, std::floor(v / texel) * texel - v)));

    Camera3D light = {};
    light.position = Vector3Add(center, Vector3Scale(sun, distance));
    light.target = center;
    light.up = { 0.0f, 1.0f, 0.0f };
    light.fovy = radius * 2.0f;
    light.projection = CAMERA_ORTHOGRAPHIC;

    BeginTextureMode(map);
    // Өмнөх frame-д (хагас тунгалаг зүйл зурахдаа) гүний бичилт унтарсан үлдсэн байж болно: эс бөгөөс
    // цэвэрлэгээ ч, бичилт ч хийгдэхгүй.
    rlEnableDepthMask();
    ClearBackground(WHITE);
    BeginMode3D(light);
    viewProj = MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection());
    for (Shader s : { shader, terrainShader, detailShader, characterShader, propShader, vehicleShader })
    {
        SetFloat(s, "shadowStrength", 0.0f);
    }
    BeginShaderMode(depthShader);
    shadowPassActive = true;
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
    // AO, нарны туяа ("өндөр" график) сценийн гүнийг уншина
    const bool effects = sceneShaded && quality >= 2 && (aoShader.id != 0 || shaftShader.id != 0);
    if (scene.id == 0 || scene.texture.width != width || scene.texture.height != height || (effects && !sceneHasDepth))
    {
        // Одоогийнхыг кэшэд хадгалж, тохирох хэмжээтэйг кэшээс авна (нягтрал хэдхэн түвшний хооронд шилждэг)
        if (scene.id) sceneCache.push_back({ scene, sceneHasDepth });
        scene = {};
        for (size_t i = 0; i < sceneCache.size(); ++i)
        {
            const RenderTexture2D& cached = sceneCache[i].first;
            if (cached.texture.width == width && cached.texture.height == height && (!effects || sceneCache[i].second))
            {
                scene = cached;
                sceneHasDepth = sceneCache[i].second;
                sceneCache.erase(sceneCache.begin() + (long)i);
                break;
            }
        }
        while (sceneCache.size() > 3)
        {
            UnloadRenderTexture(sceneCache.front().first);
            sceneCache.erase(sceneCache.begin());
        }
        if (bloomA.id) UnloadRenderTexture(bloomA);
        if (bloomB.id) UnloadRenderTexture(bloomB);
        bloomA = {};
        bloomB = {};
        UnloadScreenEffects();
        if (scene.id == 0)
        {
            scene = LoadSceneTarget(width, height, effects, sceneHasDepth);
            SetTextureFilter(scene.texture, TEXTURE_FILTER_BILINEAR);
        }
    }
    screenEffects = effects && sceneHasDepth;
    if (screenEffects && aoA.id == 0)
    {
        const int w2 = std::max(1, width / 2), h2 = std::max(1, height / 2);
        aoA = LoadRenderTexture(w2, h2);
        aoB = LoadRenderTexture(w2, h2);
        shafts = LoadRenderTexture(std::max(1, width / 4), std::max(1, height / 4));
        for (const RenderTexture2D* target : { &aoA, &aoB, &shafts }) SetTextureFilter(target->texture, TEXTURE_FILTER_BILINEAR);
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
    if (screenEffects) RenderScreenEffects();
}

// AO (хагас нягтрал, бүдгэрүүлсэн) ба нарны туяа (дөрөвний нэг): сценийн гүнээс
void Environment::RenderScreenEffects()
{
    if (aoA.id == 0 || aoB.id == 0 || shafts.id == 0) return;
    const float w = (float)scene.texture.width, h = (float)scene.texture.height;
    const float tanHalf = std::tan(sceneCamera.fovy * DEG2RAD * 0.5f);
    auto fill = [](const RenderTexture2D& target) {
        DrawRectangle(0, 0, target.texture.width, target.texture.height, WHITE);
    };

    if (aoShader.id != 0)
    {
        const float aw = (float)aoA.texture.width, ah = (float)aoA.texture.height;
        SetVec2(aoShader, "tanHalf", { tanHalf * w / h, tanHalf });
        SetVec2(aoShader, "depthTexel", { 2.0f / w, 2.0f / h }); // (хагас нягтралын нэг пиксел)
        SetFloat(aoShader, "nearPlane", (float)rlGetCullDistanceNear());
        SetFloat(aoShader, "farPlane", (float)rlGetCullDistanceFar());
        SetVec2(aoShader, "outputSize", { aw, ah });
        BeginTextureMode(aoA);
        ClearBackground(WHITE);
        BeginShaderMode(aoShader);
        SetShaderValueTexture(aoShader, GetShaderLocation(aoShader, "depthTexture"), scene.depth);
        fill(aoA);
        EndShaderMode();
        EndTextureMode();
        // Цэгийн толбыг арилгана (bloom-той ижил хоёр чиглэлийн бүдгэрүүлэлт)
        for (int pass = 0; pass < 2; ++pass)
        {
            const RenderTexture2D& from = pass == 0 ? aoA : aoB;
            const RenderTexture2D& to = pass == 0 ? aoB : aoA;
            SetVec2(blurShader, "direction", pass == 0 ? Vector2{ 1.0f / aw, 0.0f } : Vector2{ 0.0f, 1.0f / ah });
            BeginTextureMode(to);
            BeginShaderMode(blurShader);
            DrawTexturePro(from.texture, { 0.0f, 0.0f, aw, -ah }, { 0.0f, 0.0f, aw, ah }, { 0.0f, 0.0f }, 0.0f, WHITE);
            EndShaderMode();
            EndTextureMode();
        }
    }

    // Нар дэлгэц дээр (эсвэл захаас бага зэрэг гадна) байвал туяа
    shaftVisible = false;
    const Vector3 forward = Vector3Normalize(Vector3Subtract(sceneCamera.target, sceneCamera.position));
    if (shaftShader.id != 0 && shaftStrength > 0.01f && Vector3DotProduct(forward, skySunDirection) > 0.2f)
    {
        const Vector2 sun = GetWorldToScreenEx(Vector3Add(sceneCamera.position, Vector3Scale(skySunDirection, 1000.0f)), sceneCamera,
                                               (int)w, (int)h);
        sunScreen = { sun.x / w, 1.0f - sun.y / h }; // GL текстурын чиглэл (доороос дээш)
        if (sunScreen.x > -0.4f && sunScreen.x < 1.4f && sunScreen.y > -0.4f && sunScreen.y < 1.4f)
        {
            SetVec2(shaftShader, "sunPosition", sunScreen);
            SetFloat(shaftShader, "aspect", w / h);
            SetVec2(shaftShader, "outputSize", { (float)shafts.texture.width, (float)shafts.texture.height });
            BeginTextureMode(shafts);
            ClearBackground(BLACK);
            BeginShaderMode(shaftShader);
            SetShaderValueTexture(shaftShader, GetShaderLocation(shaftShader, "depthTexture"), scene.depth);
            fill(shafts);
            EndShaderMode();
            EndTextureMode();
            shaftVisible = true;
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
        const bool ao = screenEffects && aoShader.id != 0;
        SetFloat(postShader, "aoStrength", ao ? 0.75f : 0.0f);
        const float shaft = screenEffects && shaftVisible ? shaftStrength : 0.0f;
        SetVec3(postShader, "shaftColor", Vector3Scale(Mix3(sunHaze, sunLightColor, 0.5f), shaft));
        BeginShaderMode(postShader);
        if (bloomEnabled && bloomA.id)
        {
            SetShaderValueTexture(postShader, GetShaderLocation(postShader, "bloomTexture"), bloomA.texture);
        }
        if (ao) SetShaderValueTexture(postShader, GetShaderLocation(postShader, "aoTexture"), aoA.texture);
        if (shaft > 0.0f) SetShaderValueTexture(postShader, GetShaderLocation(postShader, "shaftTexture"), shafts.texture);
    }
    DrawTexturePro(scene.texture, { 0.0f, 0.0f, w, -h }, { 0.0f, 0.0f, (float)ViewWidth(), (float)ViewHeight() },
                   { 0.0f, 0.0f }, 0.0f, WHITE);
    if (sceneShaded) EndShaderMode();
}
