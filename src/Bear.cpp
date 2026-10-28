#include "Bear.h"

#include "MeshBuilder.h"
#include "Platform.h"
#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    const Material* bearMaterial = nullptr;
    std::unordered_map<const MeshBuilder*, Mesh> uploaded;

    const Color kFur = { 86, 58, 38, 255 };
    const Color kFurDark = { 58, 38, 24, 255 };
    const Color kFurLight = { 128, 96, 64, 255 };
    const Color kNose = { 22, 18, 16, 255 };
    const Color kClaw = { 200, 190, 170, 255 };

    // Хөлийн үе (мөр / ташаа): энэ цэгийг тойрон хөл савлана
    const float kLegTop = 0.95f;
    const float kFrontZ = 0.6f, kHindZ = -0.72f;

    void DrawPart(const MeshBuilder& builder)
    {
        if (!bearMaterial)
        {
            builder.DrawImmediate();
            return;
        }
        auto found = uploaded.find(&builder);
        if (found == uploaded.end()) found = uploaded.emplace(&builder, builder.Upload()).first;
        DrawBuiltMesh(found->second, *bearMaterial);
    }

    // Их бие: ууц, бөхтэй мөр, гэдэс, сүүл (хөлийн дээд үеэс дээш)
    const MeshBuilder& Body()
    {
        static MeshBuilder b;
        if (!b.Empty()) return b;
        b.Ellipsoid({ 0.0f, 0.9f, -0.08f }, { 0.5f, 0.48f, 0.92f }, 8, 12, kFur);
        b.Ellipsoid({ 0.0f, 1.12f, 0.36f }, { 0.42f, 0.34f, 0.46f }, 7, 10, kFurDark); // мөрний бөх
        b.Ellipsoid({ 0.0f, 0.95f, -0.72f }, { 0.46f, 0.44f, 0.4f }, 7, 10, kFur);     // ууц
        b.Ellipsoid({ 0.0f, 0.68f, 0.0f }, { 0.4f, 0.26f, 0.7f }, 6, 10, kFurDark);     // гэдэс
        b.Ellipsoid({ 0.0f, 1.0f, -1.08f }, { 0.08f, 0.07f, 0.08f }, 4, 6, kFurDark);    // сүүл
        return b;
    }

    // Хүзүү, толгой: хүзүүний суурь (0, 0, 0)-ээс урагш
    const MeshBuilder& Head()
    {
        static MeshBuilder b;
        if (!b.Empty()) return b;
        b.Tube({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.02f, 0.32f }, 0.33f, 0.26f, 10, kFur);
        b.Ellipsoid({ 0.0f, 0.06f, 0.42f }, { 0.27f, 0.25f, 0.28f }, 7, 10, kFur);
        b.Tube({ 0.0f, 0.0f, 0.6f }, { 0.0f, -0.05f, 0.84f }, 0.13f, 0.1f, 8, kFurLight); // хошуу
        for (float x : { -0.17f, 0.17f })
        {
            b.Ellipsoid({ x, 0.28f, 0.36f }, { 0.075f, 0.075f, 0.05f }, 4, 6, kFurDark); // чих
        }
        return b;
    }

    // Нүд, хамар (ойроос л)
    const MeshBuilder& Face()
    {
        static MeshBuilder b;
        if (!b.Empty()) return b;
        b.Ellipsoid({ 0.0f, -0.03f, 0.86f }, { 0.07f, 0.05f, 0.05f }, 4, 6, kNose);
        for (float x : { -0.12f, 0.12f })
        {
            b.Ellipsoid({ x, 0.12f, 0.62f }, { 0.03f, 0.03f, 0.02f }, 3, 5, kNose);
        }
        return b;
    }

    // Хөл: үенээс (0, 0, 0) доош 0.95 м, тавхай урагшаа
    const MeshBuilder& Leg(bool hind)
    {
        static MeshBuilder front, back;
        MeshBuilder& b = hind ? back : front;
        if (!b.Empty()) return b;
        if (hind)
        {
            b.Ellipsoid({ 0.0f, -0.18f, 0.0f }, { 0.2f, 0.34f, 0.27f }, 6, 8, kFur); // гуя
            b.Tube({ 0.0f, -0.4f, -0.02f }, { 0.0f, -0.88f, 0.02f }, 0.15f, 0.12f, 8, kFurDark);
        }
        else
        {
            b.Tube({ 0.0f, 0.05f, 0.0f }, { 0.0f, -0.46f, 0.03f }, 0.18f, 0.14f, 8, kFur);
            b.Tube({ 0.0f, -0.46f, 0.03f }, { 0.0f, -0.88f, 0.04f }, 0.14f, 0.12f, 8, kFurDark);
        }
        b.Ellipsoid({ 0.0f, -0.9f, 0.1f }, { 0.13f, 0.06f, 0.19f }, 4, 8, kFurDark); // тавхай
        return b;
    }

    const MeshBuilder& Claws()
    {
        static MeshBuilder b;
        if (!b.Empty()) return b;
        for (float x : { -0.08f, -0.03f, 0.03f, 0.08f })
        {
            b.Tube({ x, -0.9f, 0.24f }, { x, -0.94f, 0.32f }, 0.018f, 0.004f, 4, kClaw);
        }
        return b;
    }
}

namespace
{
    float SmoothStepUnit(float t)
    {
        t = std::clamp(t, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    // ---------------------------------------------------------------- Ястай загвар
    // Загвар +Z зүг рүү харсан (raylib glTF-ийн зангилааны эргэлтийг хэрэглэсэн), урт ~1 нэгж: тоглоомын баавгайн хэмжээнд (урт ~2.05 м, мөр ~1.2 м) томруулна.
    const float kModelScale = 2.1f;

    struct BearModel
    {
        bool loaded = false;
        Model model = {};
        Texture2D texture = {};
        Material material = {};
        std::vector<int> parent;
        std::vector<Vector3> bind;
        std::vector<Quaternion> local;    // ясны эргэлт (загварын орон зайд)
        std::vector<Quaternion> rotation; // тооцоолсон дэлхийн эргэлт
        std::vector<Vector3> position;    // тооцоолсон үений байрлал
        std::vector<char> solved;
    };
    BearModel bear;
    bool shadowPass = false;

    int Bone(const char* name)
    {
        for (int i = 0; i < bear.model.boneCount; ++i)
        {
            if (std::strcmp(bear.model.bones[i].name, name) == 0) return i;
        }
        return -1;
    }

    void Turn(const char* name, Vector3 axis, float radians)
    {
        const int bone = Bone(name);
        if (bone >= 0) bear.local[(size_t)bone] = QuaternionMultiply(bear.local[(size_t)bone], QuaternionFromAxisAngle(axis, radians));
    }

    // Эцгээс нь эхлэн: үений байрлал = эцгийн байрлал + эцгийн эргэлт * (анхны зай) (HorseModel-тэй адил).
    void Solve(int bone)
    {
        if (bear.solved[(size_t)bone]) return;
        const int up = bear.parent[(size_t)bone];
        if (up < 0)
        {
            bear.rotation[(size_t)bone] = bear.local[(size_t)bone];
            bear.position[(size_t)bone] = bear.bind[(size_t)bone];
        }
        else
        {
            Solve(up);
            const Quaternion parentRotation = bear.rotation[(size_t)up];
            bear.rotation[(size_t)bone] = QuaternionMultiply(parentRotation, bear.local[(size_t)bone]);
            const Vector3 offset = Vector3Subtract(bear.bind[(size_t)bone], bear.bind[(size_t)up]);
            bear.position[(size_t)bone] = Vector3Add(bear.position[(size_t)up], Vector3RotateByQuaternion(offset, parentRotation));
        }
        bear.solved[(size_t)bone] = 1;
    }

    struct ModelLeg
    {
        const char* upper;  // мөр / ташаа: гол савлалт
        const char* middle; // тохой / өвдөг
        const char* lower;  // бугуй / шагай
        bool hind;
    };
    // FL, FR, HL, HR (DrawBear-ийн legs[]-тэй ижил дараалал биш: доор фазаар нь тааруулна)
    const ModelLeg kModelLegs[] = {
        { "FL_Shoulder", "FL_Elbow", "FL_Wrist", false },
        { "FR_Shoulder", "FR_Elbow", "FR_Wrist", false },
        { "HL_Hip", "HL_Knee", "HL_Ankle", true },
        { "HR_Hip", "HR_Knee", "HR_Ankle", true },
    };

    // Загварын орон зай (raylib ачаалсны дараа): урагш +Z, дээш +Y. -X тэнхлэгийг тойрох эерэг эргэлт доош унжсан
    // ясыг урагш (+Z) савлуулж, урагш харсан толгойг дээш өргөнө.
    // fold 0..1: үхэхдээ хөл нугарч бие суух, lying 0..1: хажуулдаа хэвтсэн (хөл сулрана)
    void DrawModel(float phase, float gait, float run, float swipe, float fold, float lying, float seed)
    {
        std::fill(bear.local.begin(), bear.local.end(), QuaternionIdentity());
        std::fill(bear.solved.begin(), bear.solved.end(), 0);
        const Vector3 side = { -1.0f, 0.0f, 0.0f }, up = { 0.0f, 1.0f, 0.0f };
        const float dead = std::max(fold, lying);

        // Алхах (хажуугийн дараалал) -> давхих (хойд хос, урд хос); фаз нь кодоор үүсгэсэн баавгайнхтай ижил
        const float walkPhase[4] = { 0.25f, 0.75f, 0.0f, 0.5f };   // FL, FR, HL, HR
        const float gallopPhase[4] = { 0.0f, 0.1f, 0.5f, 0.6f };
        for (int index = 0; index < 4; ++index)
        {
            const ModelLeg& leg = kModelLegs[index];
            const float offset = walkPhase[index] + (gallopPhase[index] - walkPhase[index]) * run;
            const float wave = 2.0f * PI * (phase + offset);
            const float swing = std::sin(wave) * (0.36f + 0.24f * run) * gait;
            const float lift = std::max(0.0f, std::cos(wave)) * gait * (0.7f + 0.5f * run);
            Turn(leg.upper, side, swing);
            Turn(leg.middle, side, leg.hind ? -0.7f * lift : 0.9f * lift);
            Turn(leg.lower, side, leg.hind ? 0.6f * lift : -0.7f * lift);
            if (dead > 0.0f)
            {
                // Үхэх: урд хөл тохойгоороо хойш, шуу нь урагш хумигдаж; хойд хөл ташаагаараа урагш, шилбэ хойш нугарна
                // (бие доошоо суух). Хажуулдаа хэвтсэний дараа хөл хагас тэнийж сулрана. Хөл бүр бага зэрэг өөр.
                const float amount = fold * (1.0f - 0.55f * lying);
                const float vary = 1.0f + 0.12f * (float)((index * 7) % 3 - 1);
                Turn(leg.upper, side, amount * vary * (leg.hind ? 0.55f : -0.6f));
                Turn(leg.middle, side, amount * vary * (leg.hind ? -1.1f : 1.25f));
                Turn(leg.lower, side, amount * (leg.hind ? 0.5f : -0.4f));
            }
        }
        // Шүүрэх: баруун урд савраа урагш дээш өргөж буулгана, толгойгоо доош урагш
        Turn("FR_Shoulder", side, 1.5f * swipe);
        Turn("FR_Elbow", side, 0.5f * swipe);

        const float time = (float)GetTime() + seed * 7.3f;
        const float idle = (1.0f - std::min(1.0f, gait * 1.5f)) * (1.0f - dead);
        // Толгой (төв нь үргэлж урагш, нэг тийш гацахгүй):
        //  - зогсож байхад: хэдэн секунд тутам хоёр тийш эргэн тойрноо харж, хааяа доош газар үнэрлэнэ;
        //  - алхахад: алхам бүрд дохиж, хоёр тийш бага зэрэг найгана; давхихад толгойгоо намдуулж урагш сунгана.
        const float look = idle * (0.32f * std::sin(time * 0.9f) + 0.14f * std::sin(time * 2.1f + 1.3f));
        const float sniffWave = std::max(0.0f, std::sin(time * 0.55f + seed));
        const float sniff = idle * sniffWave * sniffWave * 0.45f;
        const float sway = gait * (0.1f - 0.05f * run) * std::sin(2.0f * PI * phase);
        const float nod = gait * (0.09f + 0.06f * run) * std::sin(4.0f * PI * phase + 0.6f);
        Turn("Neck", up, look + sway);
        Turn("Neck", side, -sniff + nod - 0.14f * run - 0.25f * swipe - 0.35f * fold + 0.15f * lying); // үхэхэд толгой унжина
        Turn("Head", side, idle * 0.08f * std::sin(time * 1.7f) + 0.3f * swipe);
        Turn("Head", up, idle * 0.12f * std::sin(time * 1.3f + 0.7f));
        // Сүүл: жижиг, бага зэрэг савлана
        Turn("Tail_0", up, (0.15f * gait + 0.1f * idle) * std::sin(time * 2.0f) * (1.0f - dead));

        // Арьсны матриц: v' = position + R * (v - bind)
        Mesh& mesh = bear.model.meshes[0];
        for (int bone = 0; bone < bear.model.boneCount; ++bone)
        {
            Solve(bone);
            const Quaternion rotation = bear.rotation[(size_t)bone];
            const Vector3 offset = Vector3Subtract(bear.position[(size_t)bone], Vector3RotateByQuaternion(bear.bind[(size_t)bone], rotation));
            Matrix matrix = QuaternionToMatrix(rotation);
            matrix.m12 = offset.x;
            matrix.m13 = offset.y;
            matrix.m14 = offset.z;
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
            matrix = MatrixTranspose(matrix); // CharacterModel-тэй адил (OpenGL ES)
#endif
            if (mesh.boneMatrices && bone < mesh.boneCount) mesh.boneMatrices[bone] = matrix;
        }

        rlDisableBackfaceCulling(); // загвар хоёр талт материалтай
        rlPushMatrix();
        rlScalef(kModelScale, kModelScale, kModelScale);
        DrawMesh(mesh, bear.material, MatrixIdentity());
        rlPopMatrix();
        rlEnableBackfaceCulling();
    }
}

bool LoadBearModel(Shader shader)
{
    if (bear.loaded) return true;
    std::string meshPath, texturePath;
#if defined(__ANDROID__)
    meshPath = "bear_mesh.glb";
    texturePath = "bear_color.png";
#else
    const std::string application = GetApplicationDirectory();
    for (const std::string& folder : { std::string("assets/"), application + "assets/", application + "../assets/" })
    {
        meshPath = folder + "bear_mesh.glb";
        texturePath = ImageFile(folder + "bear_color.png"); // (хөтөч: .jpg)
        if (FileExists(meshPath.c_str()) && FileExists(texturePath.c_str())) break;
    }
    if (!FileExists(meshPath.c_str()) || !FileExists(texturePath.c_str()))
    {
        TraceLog(LOG_INFO, "BEAR: %s not found, using the built-in bear", meshPath.c_str());
        return false;
    }
#endif
    bear.model = LoadModel(meshPath.c_str());
    if (bear.model.meshCount <= 0 || bear.model.boneCount <= 0 || !bear.model.bindPose || Bone("HL_Hip") < 0)
    {
        TraceLog(LOG_WARNING, "BEAR: %s has no prepared rig (run tools/prep_bear.py)", meshPath.c_str());
        UnloadModel(bear.model);
        bear.model = {};
        return false;
    }
    const size_t bones = (size_t)bear.model.boneCount;
    bear.parent.resize(bones);
    bear.bind.resize(bones);
    bear.local.assign(bones, QuaternionIdentity());
    bear.rotation.resize(bones);
    bear.position.resize(bones);
    bear.solved.resize(bones);
    for (size_t bone = 0; bone < bones; ++bone)
    {
        bear.parent[bone] = bear.model.bones[bone].parent;
        bear.bind[bone] = bear.model.bindPose[bone].translation;
    }
    bear.texture = LoadTexture(texturePath.c_str());
    SetTextureFilter(bear.texture, TEXTURE_FILTER_BILINEAR);
    bear.material = LoadMaterialDefault();
    bear.material.shader = shader;
    bear.material.maps[MATERIAL_MAP_ALBEDO].texture = bear.texture;
    bear.loaded = true;
    TraceLog(LOG_INFO, "BEAR: loaded %d triangles, %d bones", bear.model.meshes[0].triangleCount, bear.model.boneCount);
    return true;
}

void UnloadBearModel()
{
    if (!bear.loaded) return;
    bear.material.shader = {};
    bear.material.maps[MATERIAL_MAP_ALBEDO].texture = {};
    UnloadMaterial(bear.material);
    UnloadTexture(bear.texture);
    UnloadModel(bear.model);
    bear = BearModel{};
}

void SetBearMaterial(const Material* material, bool shadow)
{
    bearMaterial = material;
    shadowPass = shadow;
}

void DrawBear(Vector3 feet, float facing, float scale, float cycle, float speed, float attack, float dead, bool detailed,
              float seed)
{
    // Алхах (хажуугийн дараалал: зүүн хойд, зүүн урд, баруун хойд, баруун урд) -> давхих (хойд хос, урд хос)
    const float gait = dead > 0.0f ? 0.0f : std::clamp(speed / 1.2f, 0.0f, 1.0f);
    const float run = dead > 0.0f ? 0.0f : std::clamp((speed - 2.5f) / 2.5f, 0.0f, 1.0f);
    const float phase = cycle / (2.0f * PI);
    const float bob = (0.03f + 0.05f * run) * std::sin(4.0f * PI * phase) * gait;
    const float rock = run * 7.0f * std::sin(2.0f * PI * phase);
    const float swipe = attack > 0.0f ? std::sin(PI * (1.0f - attack)) : 0.0f; // 0 -> 1 -> 0

    rlPushMatrix();
    rlTranslatef(feet.x, feet.y, feet.z);
    rlRotatef(facing * RAD2DEG, 0.0f, 1.0f, 0.0f);
    rlScalef(scale, scale, scale);
    // Үхэх (dead 0..1, ~1.4 с): эхлээд хөл нугарч бие доош сууна (урд тал түрүүлж), дараа нь жингээрээ хурдсан
    // хажуу тийшээ хөмөрч, газарт тулахдаа бага зэрэг ойж тогтоно.
    const float fold = SmoothStepUnit(dead / 0.4f);
    const float tip = std::clamp((dead - 0.28f) / 0.5f, 0.0f, 1.0f);
    const float bounce = std::sin(PI * std::clamp((dead - 0.78f) / 0.22f, 0.0f, 1.0f)) * 7.0f;
    const float roll = dead > 0.0f ? 84.0f * tip * tip - bounce : 0.0f;
    if (dead > 0.0f)
    {
        // Зүүн хажуугийн ирмэгийг (газар дээр) тойрно
        rlTranslatef(-0.46f, 0.0f, 0.0f);
        rlRotatef(roll, 0.0f, 0.0f, 1.0f);
        rlTranslatef(0.46f, 0.0f, 0.0f);
        rlTranslatef(0.0f, -0.3f * fold * (1.0f - tip), 0.0f);
        rlTranslatef(0.0f, kLegTop, kHindZ);
        rlRotatef(9.0f * fold * (1.0f - tip), 1.0f, 0.0f, 0.0f); // урд тал түрүүлж сууна
        rlTranslatef(0.0f, -kLegTop, -kHindZ);
    }
    rlTranslatef(0.0f, bob, 0.0f);
    // Давхихад ба шүүрэхэд хойд хөлөө тулгуур болгон урдаа өндийнө
    rlTranslatef(0.0f, kLegTop, kHindZ);
    rlRotatef(-rock - swipe * 16.0f, 1.0f, 0.0f, 0.0f);
    rlTranslatef(0.0f, -kLegTop, -kHindZ);

    if (bear.loaded && !shadowPass)
    {
        DrawModel(phase - std::floor(phase), gait, run, swipe, fold, tip, seed);
        rlPopMatrix();
        return;
    }
    DrawPart(Body());

    rlPushMatrix();
    rlTranslatef(0.0f, 1.02f, 0.82f);
    rlRotatef(8.0f * std::sin(2.0f * PI * phase + 0.8f) * gait - swipe * 12.0f, 1.0f, 0.0f, 0.0f); // толгой дохино
    DrawPart(Head());
    if (detailed) DrawPart(Face());
    rlPopMatrix();

    const struct { float x, z, walk, gallop; bool hind; } legs[] = {
        { 0.28f, kHindZ, 0.0f, 0.5f, true },    // зүүн хойд
        { 0.28f, kFrontZ, 0.25f, 0.0f, false }, // зүүн урд
        { -0.28f, kHindZ, 0.5f, 0.6f, true },   // баруун хойд
        { -0.28f, kFrontZ, 0.75f, 0.1f, false } // баруун урд
    };
    for (const auto& leg : legs)
    {
        const float offset = leg.walk + (leg.gallop - leg.walk) * run;
        float angle = std::sin(2.0f * PI * (phase + offset)) * (24.0f + 14.0f * run) * gait;
        if (!leg.hind && leg.x < 0.0f) angle += swipe * 75.0f; // баруун урд савраар шүүрнэ
        rlPushMatrix();
        rlTranslatef(leg.x, kLegTop - bob, leg.z);
        rlRotatef(-angle, 1.0f, 0.0f, 0.0f);
        DrawPart(Leg(leg.hind));
        if (detailed) DrawPart(Claws());
        rlPopMatrix();
    }
    rlPopMatrix();
}
