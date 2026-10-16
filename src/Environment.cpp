#include "Environment.h"

#include "Input.h"

#include "Layout.h"
#include "MeshBuilder.h"
#include "Procedural.h"
#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace
{
    // --- Уур амьсгалын тохиргоо: Монголын цэлмэг хөх тэнгэр, хуурай тунгалаг агаар ---
    const Color kSunCore = { 255, 250, 236, 255 };
    const float kFogDensity = 0.0011f;   // 300 м-т ~10%, 600 м-т ~35%, 1000 м-т ~70% манан
    const int kShadowSlot = 9;           // сүүдрийн зургийн текстурын нэгж (материалын map-уудаас хол)

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
uniform highp sampler2D shadowMap;
uniform mat4 lightVP;
uniform float shadowStrength;
uniform float shadowTexel;
uniform float shadowBias;
uniform float snowCover;
uniform float wetness;

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

vec3 Shade(vec3 albedo, vec3 normal, vec3 pos)
{
    // Цас: дээш харсан гадаргуу (газар, дээвэр) цайрна; бороо: нойтон, бараан
    float up = smoothstep(0.5, 0.85, normal.y);
    albedo = mix(albedo, vec3(0.86, 0.88, 0.92), snowCover * up);
    albedo *= 1.0 - wetness * 0.28 * up;
    float ndl = max(dot(normal, sunDirection), 0.0);
    float shadow = SunShadow(pos, ndl);
    vec3 ambient = mix(groundLight, ambientLight, normal.y * 0.5 + 0.5);
    return albedo * (ambient + sunLight * (ndl * shadow));
}

vec3 ApplyFog(vec3 color, vec3 pos)
{
    vec3 ray = pos - viewPos;
    float dist = length(ray);
    float d = dist * fogDensity;
    float fog = 1.0 - exp(-d * d);
    float toward = max(dot(ray / max(dist, 0.001), sunDirection), 0.0);
    vec3 haze = mix(fogColor, fogSunColor, pow(toward, 6.0) * 0.7);
    return mix(color, haze, fog);
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
out vec4 finalColor;
void main()
{
    // Гадаргуугийн чиглэл: хөрш пикселүүдийн байрлалын зөрүүгээс (камер руу харсан тал).
    vec3 normal = normalize(cross(dFdx(fragPosition), dFdy(fragPosition)));
    vec3 lit = Shade(fragColor.rgb * colDiffuse.rgb, normal, fragPosition);
    finalColor = vec4(ApplyFog(lit, fragPosition), fragColor.a * colDiffuse.a);
}
)";

    // Газрын гадаргуу: оройн normal-тай тул толгод гөлгөр гэрэлтэнэ.
    const char* kTerrainVertexShader = R"(in vec3 vertexPosition;
in vec3 vertexNormal;
in vec4 vertexColor;
uniform mat4 mvp;
out vec3 fragPosition;
out vec3 fragNormal;
out vec4 fragColor;
void main()
{
    fragPosition = vertexPosition;
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
void main()
{
    vec3 lit = Shade(fragColor.rgb * colDiffuse.rgb, normalize(fragNormal), fragPosition);
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
    vec3 lit = Shade(fragColor.rgb * colDiffuse.rgb, normal, fragPosition);
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
uniform mat4 boneMatrices[72];
out vec3 fragPosition;
out vec2 fragTexCoord;
out vec3 fragNormal;
void main()
{
    // Арьслалт: орой бүр дөрөв хүртэлх ясны хувиргалтын жигнэсэн нийлбэрээр хөдөлнө.
    mat4 skin = vertexBoneWeights.x * boneMatrices[int(vertexBoneIds.x)] +
                vertexBoneWeights.y * boneMatrices[int(vertexBoneIds.y)] +
                vertexBoneWeights.z * boneMatrices[int(vertexBoneIds.z)] +
                vertexBoneWeights.w * boneMatrices[int(vertexBoneIds.w)];
    vec4 position = skin * vec4(vertexPosition, 1.0);
    fragPosition = vec3(matModel * position);
    fragTexCoord = vertexTexCoord;
    fragNormal = mat3(matModel) * (mat3(skin) * vertexNormal);
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
    float diffuse = ndl * 0.85 * SunShadow(fragPosition, ndl) + 0.15 * (0.5 + 0.5 * normal.y);
    vec3 albedo = texture(texture0, fragTexCoord).rgb * colDiffuse.rgb;
    vec3 ambient = mix(groundLight, ambientLight, normal.y * 0.5 + 0.5);
    vec3 lit = albedo * (ambient + sunLight * diffuse);
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
out vec4 finalColor;

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
    sky += fogSunColor * (pow(s, 24.0) * 0.35 + pow(s, 300.0) * 0.6) * (1.0 - night);
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
    finalColor = vec4(sky, 1.0);
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
out vec4 finalColor;
void main()
{
    vec3 color = texture(texture0, fragTexCoord).rgb;
    if (bloomStrength > 0.0) color += texture(bloomTexture, fragTexCoord).rgb * bloomStrength;
    color = (color - 0.5) * contrast + 0.5;
    float luma = dot(color, vec3(0.299, 0.587, 0.114));
    color = mix(vec3(luma), color, saturation) * tint;
    vec2 d = fragTexCoord - 0.5;
    color *= mix(1.0 - vignette, 1.0, smoothstep(0.75, 0.2, length(d) * 1.15));
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
    finalColor = vec4(color * smoothstep(0.68, 0.95, luma), 1.0);
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

    // Shader-ийн эхлэл: компьютер дээр OpenGL 3.3, утсан дээр OpenGL ES 3.0.
#if defined(__ANDROID__)
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

    // Талбарыг тойрсон low-poly уулсын цагираг.
    Mesh BuildMountains()
    {
        const int segments = 72;
        const Color foothill = { 120, 118, 96, 255 };
        const Color rock = { 104, 110, 124, 255 };
        const Color snow = { 230, 234, 240, 255 };

        MeshBuilder builder;
        Rng rng(4242u);

        // Гурван цагираг: бэл, дунд нуруу, оргилууд. Өндрийг өнцгөөр нь noise-оос авна (эхлэл төгсгөл нь нийлнэ).
        auto ringPoint = [&](int index, int ring) {
            const float angle = 2.0f * PI * (float)(index % segments) / (float)segments;
            const float nx = std::cos(angle), nz = std::sin(angle);
            const float ridge = Fbm(nx * 3.0f + 20.0f, nz * 3.0f + 20.0f, 3);
            const float jag = ValueNoise(nx * 11.0f + 5.0f, nz * 11.0f + 5.0f);

            // Газрыг 680 м хүртэл зурдаг; уулсын цагираг камерыг дагаж, үргэлж түүний цаана байна.
            float radius = 770.0f, height = -15.0f;
            if (ring == 1)
            {
                radius = 900.0f + jag * 60.0f;
                height = 40.0f + ridge * 85.0f + jag * 30.0f;
            }
            else if (ring == 2)
            {
                radius = 1120.0f + jag * 80.0f;
                height = 65.0f + ridge * ridge * 270.0f + jag * 85.0f;
            }
            return Vector3{ nx * radius, height, nz * radius };
        };
        auto faceColor = [&](Vector3 a, Vector3 b, Vector3 c) {
            const float height = (a.y + b.y + c.y) / 3.0f;
            Color color = MixColor(foothill, rock, SmoothStep(30.0f, 115.0f, height));
            color = MixColor(color, snow, SmoothStep(180.0f, 255.0f, height));
            return JitterColor(color, rng, 0.07f);
        };

        for (int i = 0; i < segments; ++i)
        {
            for (int ring = 0; ring < 2; ++ring)
            {
                const Vector3 a = ringPoint(i, ring);
                const Vector3 b = ringPoint(i + 1, ring);
                const Vector3 c = ringPoint(i, ring + 1);
                const Vector3 d = ringPoint(i + 1, ring + 1);
                builder.Triangle(a, b, c, faceColor(a, b, c));
                builder.Triangle(b, d, c, faceColor(b, d, c));
            }
        }
        return builder.Upload();
    }

    RenderTexture2D LoadShadowTarget(int size)
    {
        RenderTexture2D target = {};
        target.id = rlLoadFramebuffer();
        target.texture.width = size;
        target.texture.height = size;
        if (target.id == 0) return target;
        rlEnableFramebuffer(target.id);
        target.depth.id = rlLoadTextureDepth(size, size, false);
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
    detailShader = LoadVersionedShader(kDetailVertexShader, kDetailFragmentShader);
    detailMaterial = MaterialWith(detailShader);
    characterShader = LoadVersionedShader(kCharacterVertexShader, kCharacterFragmentShader);
    propShader = LoadVersionedShader(kPropVertexShader, kFragmentShader);
    propMaterial = MaterialWith(propShader);
    depthShader = LoadVersionedShader(kDepthVertexShader, kDepthFragmentShader, false);
    depthMaterial = MaterialWith(depthShader);
    skyShader = LoadVersionedShader(kScreenVertexShader, kSkyFragmentShader, false);
    postShader = LoadVersionedShader(kScreenVertexShader, kPostFragmentShader, false);
    brightShader = LoadVersionedShader(kScreenVertexShader, kBrightFragmentShader, false);
    blurShader = LoadVersionedShader(kScreenVertexShader, kBlurFragmentShader, false);

    for (Shader s : { shader, terrainShader, detailShader, characterShader, propShader })
    {
        SetVec3(s, "sunDirection", Vector3Normalize(kSunDirection));
        SetInt(s, "shadowMap", kShadowSlot);
        SetFloat(s, "shadowStrength", 0.0f);
    }
    SetVec3(skyShader, "sunDirection", Vector3Normalize(kSunDirection));
    SetVec3(skyShader, "sunCore", ToVector(kSunCore));

    shadowSize = kShadowMapSize;
    shadowRadius = kShadowRadius;
    shadowMap = LoadShadowTarget(shadowSize);

    mountains = BuildMountains();
    SetTimeOfDay(0.3f);
}

Environment::~Environment()
{
    UnloadBuiltMesh(mountains);
    UnloadMaterial(material); // shader-ийг мөн чөлөөлнө
    UnloadMaterial(terrainMaterial);
    UnloadMaterial(detailMaterial);
    UnloadMaterial(propMaterial);
    UnloadMaterial(depthMaterial);
    for (Shader s : { characterShader, skyShader, postShader, brightShader, blurShader })
    {
        if (s.id != 0 && s.id != rlGetShaderIdDefault()) UnloadShader(s);
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
    Vector3 zenith = Mix3(Mix3({ 0.20f, 0.41f, 0.70f }, { 0.27f, 0.32f, 0.55f }, golden), { 0.02f, 0.04f, 0.10f }, night);
    Vector3 horizon = Mix3(Mix3({ 0.73f, 0.81f, 0.87f }, { 0.91f, 0.63f, 0.43f }, golden), { 0.10f, 0.13f, 0.20f }, night);
    Vector3 haze = Mix3(Mix3({ 1.0f, 0.90f, 0.74f }, { 1.0f, 0.60f, 0.34f }, golden), { 0.12f, 0.15f, 0.24f }, night);
    Vector3 sun = Mix3(Mix3({ 0.93f, 0.86f, 0.74f }, { 0.95f, 0.58f, 0.36f }, golden), { 0.13f, 0.17f, 0.28f }, night);
    Vector3 ambient = Mix3(Mix3({ 0.40f, 0.47f, 0.58f }, { 0.40f, 0.36f, 0.43f }, golden), { 0.17f, 0.20f, 0.31f }, night);
    Vector3 ground = Mix3(Mix3({ 0.32f, 0.29f, 0.23f }, { 0.32f, 0.24f, 0.19f }, golden), { 0.07f, 0.07f, 0.09f }, night);
    Vector3 cloudLit = Mix3(Mix3({ 0.98f, 0.98f, 1.0f }, { 1.0f, 0.76f, 0.58f }, golden), { 0.16f, 0.18f, 0.25f }, night);
    Vector3 cloudDark = Mix3(Mix3({ 0.64f, 0.68f, 0.76f }, { 0.50f, 0.40f, 0.46f }, golden), { 0.07f, 0.08f, 0.12f }, night);
    float fogDensity = kFogDensity * (1.0f + 0.8f * night);
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

    skyZenith = ToColor(zenith);
    skyHorizon = ToColor(horizon);
    sunHaze = haze;

    for (Shader s : { shader, terrainShader, detailShader, characterShader, propShader })
    {
        if (s.id == 0) continue;
        SetVec3(s, "sunLight", sun);
        SetVec3(s, "ambientLight", ambient);
        SetVec3(s, "groundLight", ground);
        SetVec3(s, "fogColor", horizon);
        SetVec3(s, "fogSunColor", haze);
        SetFloat(s, "fogDensity", fogDensity);
    }
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
    const float saturation = (1.04f + golden * 0.08f) * (1.0f - night * 0.3f) * (1.0f - wet * 0.15f);
    const Vector3 tint = Mix3(Mix3({ 1.02f, 1.0f, 0.97f }, { 1.06f, 0.98f, 0.9f }, golden), { 0.88f, 0.94f, 1.1f }, night);
    SetFloat(postShader, "saturation", saturation);
    SetFloat(postShader, "contrast", 1.06f);
    SetVec3(postShader, "tint", tint);
    SetFloat(postShader, "vignette", 0.28f + night * 0.12f);
    SetFloat(postShader, "bloomStrength", bloomEnabled ? 0.55f : 0.0f);
}

void Environment::DrawSky(const Camera3D& camera) const
{
    const int width = ViewWidth();
    const int height = ViewHeight();

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
    const float strength = shadowReady && ShadowsEnabled() ? dayShadow : 0.0f;
    if (strength > 0.0f)
    {
        rlActiveTextureSlot(kShadowSlot);
        rlEnableTexture(shadowMap.depth.id);
        rlActiveTextureSlot(0);
    }
    const float texel = 1.0f / (float)std::max(1, shadowSize);
    // Ортографын гүн шугаман: near..far (Game: rlSetClipPlanes) → 0..1. ~15 см-ийн хазайлт.
    const float bias = 0.15f / (float)(rlGetCullDistanceFar() - rlGetCullDistanceNear());
    for (Shader s : { shader, terrainShader, detailShader, characterShader, propShader })
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
    SetFloat(detailShader, "time", time);
    SetVec2(detailShader, "wind", wind);
    BeginShaderMode(shader);
}

void Environment::End() const
{
    EndShaderMode();
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
    if (offsetLocation >= 0) SetShaderValue(shader, offsetLocation, &offset, SHADER_UNIFORM_VEC3);

    rlDisableBackfaceCulling();
    DrawMesh(mountains, material, MatrixTranslate(offset.x, 0.0f, offset.z));
    rlEnableBackfaceCulling();

    if (offsetLocation >= 0) SetShaderValue(shader, offsetLocation, &none, SHADER_UNIFORM_VEC3);
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
    for (Shader s : { shader, terrainShader, detailShader, characterShader, propShader })
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

bool Environment::BeginScene(int width, int height)
{
    sceneActive = false;
    if (!postEnabled || postShader.id == 0 || postShader.id == rlGetShaderIdDefault()) return false;
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
    if (bloomEnabled && bloomA.id == 0)
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

    if (bloomEnabled && bloomA.id && bloomB.id)
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
    BeginShaderMode(postShader);
    if (bloomEnabled && bloomA.id)
    {
        SetShaderValueTexture(postShader, GetShaderLocation(postShader, "bloomTexture"), bloomA.texture);
    }
    DrawTexturePro(scene.texture, { 0.0f, 0.0f, w, -h }, { 0.0f, 0.0f, (float)ViewWidth(), (float)ViewHeight() },
                   { 0.0f, 0.0f }, 0.0f, WHITE);
    EndShaderMode();
}
