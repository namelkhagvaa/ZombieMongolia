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
    // --- Уур амьсгалын тохиргоо: тоостой, дулаан өнгөтэй үдээс хойш ---
    const Color kSkyZenith = { 92, 124, 160, 255 };
    const Color kSkyHorizon = { 198, 190, 170, 255 }; // манангийн өнгөтэй ижил байх ёстой
    const Color kSunCore = { 255, 246, 224, 255 };
    const Color kSunGlow = { 255, 226, 170, 255 };

    const Vector3 kSunLight = { 0.78f, 0.72f, 0.60f };  // нарны шууд гэрэл (дулаан)
    const Vector3 kAmbient = { 0.44f, 0.47f, 0.54f };   // тэнгэрээс ойсон гэрэл (сэрүүн)
    const float kFogDensity = 0.0013f;                  // 300 м-т ~14%, 600 м-т ~45%, 1000 м-т ~80% манан

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
uniform vec3 viewPos;
uniform vec3 sunDirection;
uniform vec3 sunLight;
uniform vec3 ambientLight;
uniform vec3 fogColor;
uniform float fogDensity;
out vec4 finalColor;
void main()
{
    // Гадаргуугийн чиглэл: хөрш пикселүүдийн байрлалын зөрүүгээс (камер руу харсан тал).
    vec3 normal = normalize(cross(dFdx(fragPosition), dFdy(fragPosition)));
    float diffuse = max(dot(normal, sunDirection), 0.0);
    vec3 lit = fragColor.rgb * colDiffuse.rgb * (ambientLight + sunLight * diffuse);

    // Зайн манан: холдох тусам тэнгэрийн хаяаны өнгөнд уусна.
    float d = distance(viewPos, fragPosition) * fogDensity;
    float fog = 1.0 - exp(-d * d);
    finalColor = vec4(mix(lit, fogColor, fog), fragColor.a * colDiffuse.a);
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
uniform vec3 viewPos;
uniform vec3 sunDirection;
uniform vec3 sunLight;
uniform vec3 ambientLight;
uniform vec3 fogColor;
uniform float fogDensity;
out vec4 finalColor;
void main()
{
    vec3 normal = normalize(fragNormal);
    if (!gl_FrontFacing) normal = -normal; // хоёр талт гадаргуугийн ар тал
    // Сүүдэр талыг бүр харлуулахгүй: нарны эсрэг тал ч бага зэрэг гэрэлтэнэ.
    float diffuse = max(dot(normal, sunDirection), 0.0) * 0.85 + 0.15 * (0.5 + 0.5 * normal.y);
    vec3 albedo = texture(texture0, fragTexCoord).rgb * colDiffuse.rgb;
    vec3 lit = albedo * (ambientLight + sunLight * diffuse);

    float d = distance(viewPos, fragPosition) * fogDensity;
    float fog = 1.0 - exp(-d * d);
    finalColor = vec4(mix(lit, fogColor, fog), 1.0);
}
)";

    // Shader-ийн эхлэл: компьютер дээр OpenGL 3.3, утсан дээр OpenGL ES 3.0.
#if defined(__ANDROID__)
    const char* kShaderHeader = "#version 300 es\nprecision highp float;\n";
#else
    const char* kShaderHeader = "#version 330\n";
#endif

    Shader LoadVersionedShader(const char* vertex, const char* fragment)
    {
        return LoadShaderFromMemory(TextFormat("%s%s", kShaderHeader, vertex), std::string(kShaderHeader).append(fragment).c_str());
    }

    Vector3 ToVector(Color color)
    {
        return { (float)color.r / 255.0f, (float)color.g / 255.0f, (float)color.b / 255.0f };
    }

    void SetVec3(Shader shader, const char* name, Vector3 value)
    {
        const int location = GetShaderLocation(shader, name);
        if (location >= 0)
        {
            SetShaderValue(shader, location, &value, SHADER_UNIFORM_VEC3);
        }
    }

    // Талбарыг тойрсон low-poly уулсын цагираг.
    Mesh BuildMountains()
    {
        const int segments = 72;
        const Color foothill = { 124, 116, 90, 255 };
        const Color rock = { 108, 110, 120, 255 };
        const Color snow = { 226, 230, 236, 255 };

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
}

Environment::Environment()
{
    shader = LoadVersionedShader(kVertexShader, kFragmentShader);
    if (shader.id == rlGetShaderIdDefault())
    {
        // Shader ачаалагдаагүй бол тоглоом гэрэлтүүлэг, манангүйгээр үргэлжилнэ.
        TraceLog(LOG_WARNING, "ENVIRONMENT: lighting shader failed to load, using flat colors");
    }

    viewPosLocation = GetShaderLocation(shader, "viewPos");
    offsetLocation = GetShaderLocation(shader, "worldOffset");
    SetVec3(shader, "sunDirection", Vector3Normalize(kSunDirection));
    SetVec3(shader, "sunLight", kSunLight);
    SetVec3(shader, "ambientLight", kAmbient);
    SetVec3(shader, "fogColor", ToVector(kSkyHorizon));
    const int densityLocation = GetShaderLocation(shader, "fogDensity");
    if (densityLocation >= 0)
    {
        SetShaderValue(shader, densityLocation, &kFogDensity, SHADER_UNIFORM_FLOAT);
    }

    material = LoadMaterialDefault();
    material.shader = shader;

    characterShader = LoadVersionedShader(kCharacterVertexShader, kCharacterFragmentShader);
    characterViewPosLocation = GetShaderLocation(characterShader, "viewPos");
    SetVec3(characterShader, "sunDirection", Vector3Normalize(kSunDirection));
    SetVec3(characterShader, "sunLight", kSunLight);
    SetVec3(characterShader, "ambientLight", kAmbient);
    SetVec3(characterShader, "fogColor", ToVector(kSkyHorizon));
    const int characterDensity = GetShaderLocation(characterShader, "fogDensity");
    if (characterDensity >= 0)
    {
        SetShaderValue(characterShader, characterDensity, &kFogDensity, SHADER_UNIFORM_FLOAT);
    }

    mountains = BuildMountains();
}

Environment::~Environment()
{
    UnloadBuiltMesh(mountains);
    UnloadMaterial(material); // shader-ийг мөн чөлөөлнө
    if (characterShader.id != rlGetShaderIdDefault())
    {
        UnloadShader(characterShader);
    }
}

void Environment::DrawSky(const Camera3D& camera) const
{
    const int width = ViewWidth();
    const int height = ViewHeight();

    // Тэнгэрийн хаяаны дэлгэц дээрх өндөр: камерын өндөрт, хол байгаа цэгийн проекц.
    Vector3 forward = Vector3Subtract(camera.target, camera.position);
    const Vector3 flat = Vector3Normalize({ forward.x, 0.0f, forward.z });
    const Vector2 horizonPoint = ViewWorldToScreen(Vector3Add(camera.position, Vector3Scale(flat, 5000.0f)), camera);
    const int horizon = (int)std::clamp(horizonPoint.y, -4.0f * (float)height, 5.0f * (float)height);

    // Хаяанаас доош манангийн өнгө, дээш оргил руу аажим хөхөрнө.
    ClearBackground(kSkyHorizon);
    const int band = (int)((float)height * 0.8f);
    if (horizon - band > 0)
    {
        DrawRectangle(0, 0, width, horizon - band, kSkyZenith);
    }
    DrawRectangleGradientV(0, horizon - band, width, band, kSkyZenith, kSkyHorizon);

    // Нар: камерын урд байгаа үед л
    forward = Vector3Normalize(forward);
    const Vector3 sun = Vector3Normalize(kSunDirection);
    if (Vector3DotProduct(forward, sun) > 0.15f)
    {
        const Vector2 p = ViewWorldToScreen(Vector3Add(camera.position, Vector3Scale(sun, 5000.0f)), camera);
        const float scale = (float)height / 720.0f;
        DrawCircleGradient((int)p.x, (int)p.y, 150.0f * scale, Fade(kSunGlow, 0.55f), Fade(kSunGlow, 0.0f));
        DrawCircleV(p, 26.0f * scale, kSunCore);
    }
}

void Environment::Begin(const Camera3D& camera) const
{
    if (viewPosLocation >= 0)
    {
        SetShaderValue(shader, viewPosLocation, &camera.position, SHADER_UNIFORM_VEC3);
    }
    if (characterViewPosLocation >= 0)
    {
        SetShaderValue(characterShader, characterViewPosLocation, &camera.position, SHADER_UNIFORM_VEC3);
    }
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
