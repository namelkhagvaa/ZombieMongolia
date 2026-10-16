#include "HorseModel.h"

#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    // Загвар -X зүг рүү харсан, урт нь ~1 нэгж: тоглоомын морины хэмжээнд (нуруу ~1.45 м) томруулж,
    // эмээл нурууны голд таарахаар бага зэрэг шилжүүлнэ.
    const float kScale = 2.45f;
    const float kForwardOffset = 0.14f; // метр, унааны +Z тийш

    struct Horse
    {
        bool loaded = false;
        Model model = {};
        Texture2D texture = {};
        Material material = {};
        std::vector<int> parent;
        std::vector<Vector3> bind;
        std::vector<Quaternion> local;   // ясны эргэлт (загварын орон зайд, эцгийнхтэйгээ харьцангуй)
        std::vector<Quaternion> rotation; // тооцоолсон дэлхийн эргэлт
        std::vector<Vector3> position;    // тооцоолсон үений байрлал
        std::vector<char> solved;
    };

    Horse horse;

    struct Leg
    {
        const char* upper;  // гол савлах үе (урд: тохой, хойд: ташаа)
        const char* middle; // урд: өвдөг (доод хэсэг хойш нугарна), хойд: тахилзуур (урагш)
        const char* lower;  // шагай
        float phase;
        bool hind;
    };
    const Leg kLegs[] = {
        { "FL_Elbow", "FL_Knee", "FL_Fetlock", 0.0f, false },
        { "FR_Elbow", "FR_Knee", "FR_Fetlock", 0.5f, false },
        { "HL_Hip", "HL_Hock", "HL_Fetlock", 0.6f, true },
        { "HR_Hip", "HR_Hock", "HR_Fetlock", 0.1f, true },
    };

    int Bone(const char* name)
    {
        for (int i = 0; i < horse.model.boneCount; ++i)
        {
            if (std::strcmp(horse.model.bones[i].name, name) == 0) return i;
        }
        return -1;
    }

    void Turn(const char* name, Vector3 axis, float radians)
    {
        const int bone = Bone(name);
        if (bone >= 0) horse.local[(size_t)bone] = QuaternionMultiply(horse.local[(size_t)bone], QuaternionFromAxisAngle(axis, radians));
    }

    // Эцгээс нь эхлэн: үений байрлал = эцгийн байрлал + эцгийн эргэлт * (анхны зай).
    void Solve(int bone)
    {
        if (horse.solved[(size_t)bone]) return;
        const int up = horse.parent[(size_t)bone];
        if (up < 0)
        {
            horse.rotation[(size_t)bone] = horse.local[(size_t)bone];
            horse.position[(size_t)bone] = horse.bind[(size_t)bone];
        }
        else
        {
            Solve(up);
            const Quaternion parentRotation = horse.rotation[(size_t)up];
            horse.rotation[(size_t)bone] = QuaternionMultiply(parentRotation, horse.local[(size_t)bone]);
            const Vector3 offset = Vector3Subtract(horse.bind[(size_t)bone], horse.bind[(size_t)up]);
            horse.position[(size_t)bone] = Vector3Add(horse.position[(size_t)up], Vector3RotateByQuaternion(offset, parentRotation));
        }
        horse.solved[(size_t)bone] = 1;
    }
}

bool LoadHorseModel(Shader shader)
{
    if (horse.loaded) return true;
    std::string meshPath, texturePath;
#if defined(__ANDROID__)
    meshPath = "horse_mesh.glb";
    texturePath = "horse_color.png";
#else
    const std::string application = GetApplicationDirectory();
    for (const std::string& folder : { std::string("assets/"), application + "assets/", application + "../assets/" })
    {
        meshPath = folder + "horse_mesh.glb";
        texturePath = folder + "horse_color.png";
        if (FileExists(meshPath.c_str()) && FileExists(texturePath.c_str())) break;
    }
    if (!FileExists(meshPath.c_str()) || !FileExists(texturePath.c_str()))
    {
        TraceLog(LOG_INFO, "HORSE: %s not found, using the built-in horse", meshPath.c_str());
        return false;
    }
#endif
    horse.model = LoadModel(meshPath.c_str());
    if (horse.model.meshCount <= 0 || horse.model.boneCount <= 0 || !horse.model.bindPose || Bone("HL_Hip") < 0)
    {
        TraceLog(LOG_WARNING, "HORSE: %s has no fixed rig (run tools/fix_horse_rig.py)", meshPath.c_str());
        UnloadModel(horse.model);
        horse.model = {};
        return false;
    }
    const size_t bones = (size_t)horse.model.boneCount;
    horse.parent.resize(bones);
    horse.bind.resize(bones);
    horse.local.assign(bones, QuaternionIdentity());
    horse.rotation.resize(bones);
    horse.position.resize(bones);
    horse.solved.resize(bones);
    for (size_t bone = 0; bone < bones; ++bone)
    {
        horse.parent[bone] = horse.model.bones[bone].parent;
        horse.bind[bone] = horse.model.bindPose[bone].translation;
    }
    horse.texture = LoadTexture(texturePath.c_str());
    // Жинхэнэ текстур: зөөлөн шүүлтүүр; текстургүй загварын 4 өнгийн палитр: өнгө хоорондоо холилдохгүй.
    SetTextureFilter(horse.texture, horse.texture.width > 16 ? TEXTURE_FILTER_BILINEAR : TEXTURE_FILTER_POINT);
    horse.material = LoadMaterialDefault();
    horse.material.shader = shader;
    horse.material.maps[MATERIAL_MAP_ALBEDO].texture = horse.texture;
    horse.loaded = true;
    TraceLog(LOG_INFO, "HORSE: loaded %d triangles, %d bones", horse.model.meshes[0].triangleCount, horse.model.boneCount);
    return true;
}

void UnloadHorseModel()
{
    if (!horse.loaded) return;
    horse.material.shader = {};
    horse.material.maps[MATERIAL_MAP_ALBEDO].texture = {};
    UnloadMaterial(horse.material);
    UnloadTexture(horse.texture);
    UnloadModel(horse.model);
    horse = Horse{};
}

bool HasHorseModel()
{
    return horse.loaded;
}

float HorseStridePhase(float animation)
{
    const float p = animation / (2.0f * PI);
    return p - std::floor(p);
}

void DrawHorseModel(float animation, float gait, float steer, float seed, float gallop)
{
    if (!horse.loaded) return;
    std::fill(horse.local.begin(), horse.local.end(), QuaternionIdentity());
    std::fill(horse.solved.begin(), horse.solved.end(), 0);

    // Загварын орон зай: урагш -X, дээш +Y. Z тэнхлэгийг тойрох эерэг эргэлт доош унжсан ясыг хойш (+X) савлуулна.
    const Vector3 side = { 0.0f, 0.0f, 1.0f }, up = { 0.0f, 1.0f, 0.0f };
    // Давхилт (4 цохилттой, баруун хөлөөр тэргүүлсэн): хойд зүүн -> хойд баруун -> урд зүүн -> урд баруун,
    // дараа нь бүх хөл агаарт (биеийн доор хумигдана). Алхмын үе p = 0..1 (HorseStridePhase).
    static const float kGallopPhase[4] = { 0.35f, 0.45f, 0.0f, 0.1f }; // FL, FR, HL, HR
    const float stride = HorseStridePhase(animation);
    for (int index = 0; index < 4; ++index)
    {
        const Leg& leg = kLegs[index];

        // Хатирах үе: синус савлалт (хуучин хэв маяг)
        const float wave = animation + leg.phase * 2.0f * PI;
        const float trotSwing = std::sin(wave) * (leg.hind ? 0.42f : 0.55f) * gait;
        const float trotLift = std::max(0.0f, std::cos(wave)) * gait;

        // Давхилт: туурай газарт (тулгуур, 38%) хөл шулуун, урагшаас хойш жигд түлхэнэ; газраас хөндийрөхдөө
        // үеэ хүчтэй нугалж, хурдан урагш шидэгдэнэ.
        float p = stride - kGallopPhase[index];
        p -= std::floor(p);
        const float duty = 0.38f;
        const float forward = leg.hind ? 0.5f : 0.78f, back = leg.hind ? 0.78f : 0.5f;
        float gallopSwing, gallopLift;
        if (p < duty)
        {
            const float s = p / duty;
            gallopSwing = forward + (-back - forward) * s;
            gallopLift = 0.0f;
        }
        else
        {
            const float s = (p - duty) / (1.0f - duty);
            const float eased = s * s * (3.0f - 2.0f * s);
            gallopSwing = -back + (forward + back) * eased;
            gallopLift = std::sin(PI * std::min(1.0f, s * 1.15f)) * (leg.hind ? 1.2f : 1.0f);
        }

        const float swing = trotSwing + (gallopSwing * gait - trotSwing) * gallop;
        const float lift = trotLift + (gallopLift * gait - trotLift) * gallop;
        Turn(leg.upper, side, -swing);
        Turn(leg.middle, side, leg.hind ? -0.75f * lift : 0.95f * lift);
        Turn(leg.lower, side, 0.6f * lift);
    }
    // Хүзүү, толгой: давхихад дохиж, эргэх зүг рүү эргэнэ.
    // Зогсолт (gait 0) ба явдлын хооронд зөөлөн шилжинэ.
    const float time = (float)GetTime() + seed * 7.3f;
    const float idle = 1.0f - std::min(1.0f, gait * 1.5f);

    // Хүзүү, толгой: явахад алхам бүрд дохиж, эргэх зүг рүү эргэнэ. Зогсож байхад аажим толгойгоо өргөж
    // буулгаж, хажуу тийш эргэн тойрноо харна; хааяа толгойгоо сэгсэрнэ.
    const float look = idle * (0.28f * std::sin(time * 0.31f) + 0.12f * std::sin(time * 0.73f + 1.1f));
    const float graze = idle * (0.06f + 0.1f * std::sin(time * 0.47f + 0.5f));
    const float shake = idle * std::pow(std::max(0.0f, std::sin(time * 0.23f + seed)), 24.0f) * 0.12f * std::sin(time * 22.0f);
    Turn("Neck_0", up, -steer * 0.35f + look);
    // Давхихад урд хөл газарт буух үед толгой доош дохиж, хойд хөл түлхэхэд өргөгдөнө (алхамд нэг удаа).
    const float trotNod = 0.08f * std::sin(animation * 2.0f + 0.6f);
    const float gallopNod = 0.17f * std::sin(2.0f * PI * (stride - 0.35f));
    Turn("Neck_0", side, 0.05f + graze + gait * (0.12f + trotNod + (gallopNod - trotNod) * gallop));
    Turn("Head", side, idle * 0.05f * std::sin(time * 1.1f + 0.3f));
    Turn("Head", Vector3{ 1.0f, 0.0f, 0.0f }, shake);
    // Сүүл: хурдлах тусам өргөгдөж, хоёр тийш найгана.
    // Сүүл (6 үе, дээрээс доош): үе бүр дээд үеэсээ хоцорч хөдөлдөг тул хөдөлгөөн суурийнаас үзүүр рүү
    // долгио шиг урсаж, үс намирна. Доод үеүүд хөнгөн тул илүү их хазайна.
    //  - Явахад: салхинд хойш хийсэж (хурдлах тусам өргөгдөнө), алхам бүрт хоёр тийш намирна; эргэхэд
    //    инерцээр эсрэг тийш шидэгдэнэ.
    //  - Зогсоход: морь өөрөө сүүлээ суурийнаас нь хоёр тийш савлана (ялаа хөөнө), хооронд нь сул унжина.
    const char* const tail[] = { "Tail_0", "Tail_1", "Tail_2", "Tail_3", "Tail_4", "Tail_5" };
    const float swishPhase = time * 0.42f + seed * 2.1f;                       // савлах үе (хэдэн секунд тутам)
    const float swishing = std::pow(std::max(0.0f, std::sin(swishPhase)), 2.0f); // 0 = тайван, 1 = савлаж байна
    for (int k = 0; k < 6; ++k)
    {
        const float depth = (float)k / 5.0f; // 0 = суурь, 1 = үзүүр
        const float lag = 0.55f * (float)k;  // доошлох тусам хоцорно
        const float loose = 0.35f + 0.65f * depth;

        // Явдал: салхинд хойш (суурь нь өргөгдөж, доод үеүүд хойш нумарна) + алхмын хэмнэлээр намирна
        float lift = gait * (k == 0 ? 0.55f : 0.07f + 0.05f * depth) + gait * 0.06f * loose * std::sin(animation * 2.0f - lag);
        float sway = gait * 0.11f * loose * std::sin(animation - lag) + steer * gait * 0.12f * loose;

        // Зогсолт: суурь нь идэвхтэй савлана, доод үеүүд хоцорч дагана (ташуурдах мэт)
        const float wave = std::sin(time * 3.2f - lag);
        sway += idle * (swishing * (k == 0 ? 0.45f : 0.16f * loose) * wave + 0.03f * loose * std::sin(time * 0.8f - lag));
        lift += idle * (k == 0 ? 0.08f + 0.06f * swishing : 0.0f);

        Turn(tail[k], up, sway);
        Turn(tail[k], side, lift);
    }

    // Арьсны матриц: v' = position + R * (v - bind)
    const int bones = horse.model.boneCount;
    Mesh& mesh = horse.model.meshes[0];
    for (int bone = 0; bone < bones; ++bone)
    {
        Solve(bone);
        const Quaternion rotation = horse.rotation[(size_t)bone];
        const Vector3 offset = Vector3Subtract(horse.position[(size_t)bone], Vector3RotateByQuaternion(horse.bind[(size_t)bone], rotation));
        Matrix matrix = QuaternionToMatrix(rotation);
        matrix.m12 = offset.x;
        matrix.m13 = offset.y;
        matrix.m14 = offset.z;
#if defined(__ANDROID__)
        matrix = MatrixTranspose(matrix); // CharacterModel-тэй адил (OpenGL ES)
#endif
        if (mesh.boneMatrices && bone < mesh.boneCount) mesh.boneMatrices[bone] = matrix;
    }

    rlDisableBackfaceCulling(); // загвар хоёр талт материалтай
    rlPushMatrix();
    rlTranslatef(0.0f, 0.0f, kForwardOffset);
    rlScalef(kScale, kScale, kScale);
    rlRotatef(90.0f, 0.0f, 1.0f, 0.0f); // загварын -X (толгой) -> унааны +Z
    DrawMesh(mesh, horse.material, MatrixIdentity());
    rlPopMatrix();
    // Culling-ийг дахин асаахгүй: дуудагч (Garage::Draw) төгсгөлдөө өөрөө асаана.
}
