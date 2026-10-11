#include "CharacterModel.h"

#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    const char* const kNames[kCharacterCount] = { "fighter1",     "fighter2",       "fighter3",
                                                  "survivor_man", "survivor_woman", "zombie_walker",
                                                  "zombie_runner", "zombie_brute",  "zombie_boss" };

    // Загварын өндөр тоглоомын хүний өндөртэй (~1.9 м) таарахаар томруулна.
    const float kTargetHeight = 1.9f;

    // Кодоор үүсгэсэн дүрийн хэмжээсүүд: буу, гарын байрлалыг түүнтэй ижил харьцаагаар тооцно.
    const float kGunX = -0.24f;      // бууны хажуу тийш байрлал (их биеийн орон зайд)
    const float kGunY = 0.3f;        // ташаанаас дээш
    const float kShoulderRise = 0.6f; // ташаанаас мөр хүртэл

    enum Joint
    {
        Hips, Spine, Spine1, Neck,
        RightArm, RightForeArm, RightHand, LeftArm, LeftForeArm, LeftHand,
        RightUpLeg, RightLeg, RightFoot, LeftUpLeg, LeftLeg, LeftFoot,
        JointCount
    };
    const char* const kJointNames[JointCount] = {
        "Hips", "Spine", "Spine1", "Neck",
        "RightArm", "RightForeArm", "RightHand", "LeftArm", "LeftForeArm", "LeftHand",
        "RightUpLeg", "RightLeg", "RightFoot", "LeftUpLeg", "LeftLeg", "LeftFoot",
    };

    struct Character
    {
        bool loaded = false;
        Model model = {};
        Texture2D texture = {};
        Material material = {};
        float scale = 1.0f;
        int joints[JointCount] = {};
        int fingers[2][5][3] = {};  // [тал][хуруу][үе]: ясны индекс, -1 = байхгүй
        Vector3 fingerAxis[2][5] = {}; // хуруу нугалах тэнхлэг (анхны байрлалд)
        Vector3 palmNormal[2] = {};    // алганы харах зүг (анхны байрлалд)
        float toeOut[2] = {};          // анхны байрлалд тавхай гадагш хэдэн градус эргэсэн
        float stepWidth = 0.07f;       // алхах үед тавхай төв шугамаас хэр зайтай гишгэх, метр
        std::vector<Vector3> bind;       // ясны үений анхны байрлал (загварын орон зай)
        std::vector<Quaternion> rotation; // одоогийн байрлал: анхныхаас хэр эргэсэн
        std::vector<Vector3> position;    // одоогийн үений байрлал
        std::vector<char> solved;
        std::vector<float> matrices;      // яс бүрийн 3x4 матриц (арьслахад)
    };

    Character characters[kCharacterCount];

    Quaternion AxisAngle(float x, float y, float z, float degrees)
    {
        return QuaternionFromAxisAngle({ x, y, z }, degrees * DEG2RAD);
    }

    Vector3 Rotate(Quaternion q, Vector3 v)
    {
        return Vector3RotateByQuaternion(v, q);
    }

    // Ясны нэрийг "mixamorig:" угтвартай, угтваргүй аль алинаар нь хайна.
    int FindBone(const Model& model, const char* name)
    {
        for (int i = 0; i < model.boneCount; ++i)
        {
            const char* bone = model.bones[i].name;
            const char* colon = std::strrchr(bone, ':');
            if (std::strcmp(colon ? colon + 1 : bone, name) == 0)
            {
                return i;
            }
        }
        return -1;
    }

    // Эцэг яснаасаа өвлөсөн эргэлтээр байрлалаа тооцно (тусгайлан эргүүлээгүй ясны хувьд).
    void Follow(Character& c, int bone)
    {
        if (c.solved[(size_t)bone])
        {
            return;
        }
        const int parent = c.model.bones[bone].parent;
        if (parent < 0)
        {
            c.rotation[(size_t)bone] = QuaternionIdentity();
            c.position[(size_t)bone] = c.bind[(size_t)bone];
        }
        else
        {
            Follow(c, parent);
            c.rotation[(size_t)bone] = c.rotation[(size_t)parent];
            c.position[(size_t)bone] =
                Vector3Add(c.position[(size_t)parent],
                           Rotate(c.rotation[(size_t)parent], Vector3Subtract(c.bind[(size_t)bone], c.bind[(size_t)parent])));
        }
        c.solved[(size_t)bone] = 1;
    }

    // Ясыг эцгийнхээ эргэлт дээр нэмж extra-аар эргүүлнэ (extra нь загварын тэнхлэгүүдээр).
    void Turn(Character& c, Joint joint, Quaternion extra)
    {
        const int bone = c.joints[joint];
        Follow(c, bone);
        c.rotation[(size_t)bone] = QuaternionMultiply(extra, c.rotation[(size_t)bone]);
    }

    // Мөчний хэсгийг (joint -> child) direction чиглэл рүү заалгана: эцгээсээ өвлөсөн чиглэлээс хамгийн
    // бага эргэлтээр, ингэснээр мөч тэнхлэгээ тойрон илүү мушгирахгүй.
    void Aim(Character& c, Joint joint, Joint child, Vector3 direction)
    {
        const int bone = c.joints[joint];
        Follow(c, bone);
        const Vector3 rest = Vector3Normalize(Vector3Subtract(c.bind[(size_t)c.joints[child]], c.bind[(size_t)bone]));
        const Vector3 current = Rotate(c.rotation[(size_t)bone], rest);
        const Quaternion swing = QuaternionFromVector3ToVector3(current, Vector3Normalize(direction));
        c.rotation[(size_t)bone] = QuaternionNormalize(QuaternionMultiply(swing, c.rotation[(size_t)bone]));
    }

    Vector3 JointPosition(Character& c, Joint joint)
    {
        Follow(c, c.joints[joint]);
        return c.position[(size_t)c.joints[joint]];
    }

    float BoneLength(const Character& c, Joint from, Joint to)
    {
        return Vector3Distance(c.bind[(size_t)c.joints[from]], c.bind[(size_t)c.joints[to]]);
    }

    // Хоёр ястай урвуу кинематик: мөр -> тохой -> бугуй. pole нь тохойн хазайх зүг.
    // palm: бугуйнаас алганы төв хүртэлх зай; зорилтод алга хүрнэ (бугуй нь түүнээс наана зогсоно).
    void Reach(Character& c, Joint upper, Joint lower, Joint hand, Vector3 target, Vector3 pole, float palm)
    {
        const Vector3 shoulder = JointPosition(c, upper);
        const float a = BoneLength(c, upper, lower);
        const float b = BoneLength(c, lower, hand) + palm;

        Vector3 toTarget = Vector3Subtract(target, shoulder);
        const float distance = std::clamp(Vector3Length(toTarget), std::fabs(a - b) + 0.001f, a + b - 0.001f);
        toTarget = Vector3Normalize(toTarget);

        // Тохойн өнцөг (косинусын теорем) ба тохойн байрлал
        const float cosine = std::clamp((a * a + distance * distance - b * b) / (2.0f * a * distance), -1.0f, 1.0f);
        const float along = a * cosine, across = a * std::sqrt(1.0f - cosine * cosine);
        Vector3 side = Vector3Subtract(pole, Vector3Scale(toTarget, Vector3DotProduct(pole, toTarget)));
        side = Vector3Length(side) > 0.001f ? Vector3Normalize(side) : Vector3{ 0.0f, -1.0f, 0.0f };

        const Vector3 elbow = Vector3Add(shoulder, Vector3Add(Vector3Scale(toTarget, along), Vector3Scale(side, across)));
        const Vector3 palmPoint = Vector3Add(shoulder, Vector3Scale(toTarget, distance));

        Aim(c, upper, lower, Vector3Subtract(elbow, shoulder));
        Aim(c, lower, hand, Vector3Subtract(palmPoint, elbow));
    }

    // Шууг тэнхлэгээ тойруулан эргүүлж, алгыг хүссэн зүг рүү харуулна (бариулыг хажуунаас, урд модыг доороос нь).
    // Гарын сарвууны байрлалыг тооцохоос өмнө дуудна.
    void FacePalm(Character& c, int side, Vector3 desired)
    {
        const int lower = c.joints[side == 0 ? RightForeArm : LeftForeArm];
        const int hand = c.joints[side == 0 ? RightHand : LeftHand];
        Follow(c, lower);
        Quaternion& rotation = c.rotation[(size_t)lower];

        const Vector3 axis = Vector3Normalize(Rotate(rotation, Vector3Subtract(c.bind[(size_t)hand], c.bind[(size_t)lower])));
        const Vector3 normal = Rotate(rotation, c.palmNormal[side]);
        const Vector3 from = Vector3Subtract(normal, Vector3Scale(axis, Vector3DotProduct(normal, axis)));
        const Vector3 to = Vector3Subtract(desired, Vector3Scale(axis, Vector3DotProduct(desired, axis)));
        if (Vector3Length(from) < 0.05f || Vector3Length(to) < 0.05f)
        {
            return;
        }
        const float angle = std::atan2(Vector3DotProduct(Vector3CrossProduct(from, to), axis), Vector3DotProduct(from, to));
        rotation = QuaternionNormalize(QuaternionMultiply(QuaternionFromAxisAngle(axis, angle), rotation));
    }

    // Хуруунуудыг алга руу нугална: үе бүрийг degrees өнцгөөр (эрхий хурууг хагасаар).
    void CurlFingers(Character& c, int side, float degrees)
    {
        const int hand = c.joints[side == 0 ? RightHand : LeftHand];
        Follow(c, hand);
        const Quaternion handRotation = c.rotation[(size_t)hand];
        for (int finger = 0; finger < 5; ++finger)
        {
            const Vector3 axis = Rotate(handRotation, c.fingerAxis[side][finger]);
            const Quaternion bend = QuaternionFromAxisAngle(axis, (finger == 4 ? 0.45f : 1.0f) * degrees * DEG2RAD);
            for (int segment = 0; segment < 3; ++segment)
            {
                const int bone = c.fingers[side][finger][segment];
                if (bone < 0)
                {
                    continue;
                }
                Follow(c, bone); // эцгийнхээ (аль хэдийн нугарсан) эргэлтийг өвлөнө
                c.rotation[(size_t)bone] = QuaternionMultiply(bend, c.rotation[(size_t)bone]);
            }
        }
    }

    // Гарын хуруунуудын яс, нугалах тэнхлэг, алганы зүгийг анхны байрлалаас тооцно.
    void SetupHands(Character& c)
    {
        static const char* const fingerNames[5] = { "Index", "Middle", "Ring", "Pinky", "Thumb" };
        for (int side = 0; side < 2; ++side)
        {
            const std::string prefix = side == 0 ? "RightHand" : "LeftHand";
            for (int finger = 0; finger < 5; ++finger)
            {
                for (int segment = 0; segment < 3; ++segment)
                {
                    c.fingers[side][finger][segment] =
                        FindBone(c.model, (prefix + fingerNames[finger] + std::to_string(segment + 1)).c_str());
                }
            }

            const Vector3 hand = c.bind[(size_t)c.joints[side == 0 ? RightHand : LeftHand]];
            const int index = c.fingers[side][0][0], pinky = c.fingers[side][3][0];
            Vector3 normal = { side == 0 ? 1.0f : -1.0f, -1.0f, 0.0f };
            if (index >= 0 && pinky >= 0)
            {
                normal = Vector3CrossProduct(Vector3Subtract(c.bind[(size_t)index], hand),
                                             Vector3Subtract(c.bind[(size_t)pinky], hand));
                // A зогсолтод алга доош, бие рүү харна: тэр талыг сонгоно.
                const Vector3 inwardDown = { hand.x > 0.0f ? -0.5f : 0.5f, -1.0f, 0.0f };
                if (Vector3DotProduct(normal, inwardDown) < 0.0f)
                {
                    normal = Vector3Negate(normal);
                }
            }
            c.palmNormal[side] = Vector3Normalize(normal);

            for (int finger = 0; finger < 5; ++finger)
            {
                const int first = c.fingers[side][finger][0], second = c.fingers[side][finger][1];
                Vector3 direction = { 0.0f, -1.0f, 0.0f };
                if (first >= 0 && second >= 0)
                {
                    direction = Vector3Subtract(c.bind[(size_t)second], c.bind[(size_t)first]);
                }
                // Энэ тэнхлэгийг тойрон эергээр эргэхэд хурууны үзүүр алга руу ойртоно.
                c.fingerAxis[side][finger] = Vector3Normalize(Vector3CrossProduct(direction, c.palmNormal[side]));
            }
        }
    }

    // Ясны одоогийн байрлалаас арьслалтын матрицуудыг тооцно. Оройнуудыг видео карт өөрөө хувиргана
    // (shader дахь boneMatrices), тиймээс нэг загварыг олон дүрд өөр өөр байрлалаар хямд зурж болно.
    void Skin(Character& c)
    {
        const int boneCount = c.model.boneCount;
        for (int bone = 0; bone < boneCount; ++bone)
        {
            Follow(c, bone);
            // v' = position + R * (v - bind)
            const Quaternion rotation = c.rotation[(size_t)bone];
            const Vector3 offset = Vector3Subtract(c.position[(size_t)bone], Rotate(rotation, c.bind[(size_t)bone]));
            Matrix matrix = QuaternionToMatrix(rotation);
            matrix.m12 = offset.x;
            matrix.m13 = offset.y;
            matrix.m14 = offset.z;
#if defined(__ANDROID__)
            // raylib нь OpenGL ES дээр матрицын массивыг хөрвүүлэлгүй (transpose хийлгүй) илгээдэг:
            // компьютер дээрхтэй ижил үр дүн гарахын тулд энд урьдчилж хөрвүүлнэ.
            matrix = MatrixTranspose(matrix);
#endif

            for (int index = 0; index < c.model.meshCount; ++index)
            {
                Mesh& mesh = c.model.meshes[index];
                if (mesh.boneMatrices && bone < mesh.boneCount)
                {
                    mesh.boneMatrices[bone] = matrix;
                }
            }
        }
    }

    // Мөчний орон зай -> загварын орон зай: эх нь origin, тэнхлэгүүд нь rotation-оор эргэсэн, метрээр.
    Matrix Frame(Quaternion rotation, Vector3 origin)
    {
        Matrix m = QuaternionToMatrix(rotation);
        m.m12 = origin.x;
        m.m13 = origin.y;
        m.m14 = origin.z;
        return m;
    }
}

void LoadCharacterModels(Shader shader)
{
    for (int id = 0; id < kCharacterCount; ++id)
    {
        Character& c = characters[id];
        // assets хавтсыг ажлын хавтаст, эсвэл exe-ийн дэргэд / нэг түвшин дээр (build\..ssets) хайна.
        const std::string application = GetApplicationDirectory();
        std::string meshPath, texturePath;
        for (const std::string& folder : { std::string("assets/"), application + "assets/", application + "../assets/" })
        {
            meshPath = folder + kNames[id] + "_mesh.glb";
            texturePath = folder + kNames[id] + "_color.png";
            if (FileExists(meshPath.c_str()) && FileExists(texturePath.c_str()))
            {
                break;
            }
        }
#if defined(__ANDROID__)
        // APK-ийн assets-ийн үндсэнд байрлана (FileExists нь assets-ийг хардаггүй тул шууд ачаална).
        meshPath = std::string(kNames[id]) + "_mesh.glb";
        texturePath = std::string(kNames[id]) + "_color.png";
#else
        if (!FileExists(meshPath.c_str()) || !FileExists(texturePath.c_str()))
        {
            TraceLog(LOG_INFO, "CHARACTER: %s not found, using the built-in figure", meshPath.c_str());
            continue;
        }
#endif

        c.model = LoadModel(meshPath.c_str());
        bool valid = c.model.meshCount > 0 && c.model.boneCount > 0 && c.model.bindPose != nullptr;
        for (int joint = 0; joint < JointCount && valid; ++joint)
        {
            c.joints[joint] = FindBone(c.model, kJointNames[joint]);
            if (c.joints[joint] < 0)
            {
                TraceLog(LOG_WARNING, "CHARACTER: %s has no bone named %s", meshPath.c_str(), kJointNames[joint]);
                valid = false;
            }
        }
        if (!valid)
        {
            TraceLog(LOG_WARNING, "CHARACTER: %s is not a rigged humanoid, using the built-in figure", meshPath.c_str());
            UnloadModel(c.model);
            c.model = {};
            continue;
        }

        const size_t bones = (size_t)c.model.boneCount;
        c.bind.resize(bones);
        c.rotation.resize(bones);
        c.position.resize(bones);
        c.solved.resize(bones);
        c.matrices.resize(bones * 12);
        for (size_t bone = 0; bone < bones; ++bone)
        {
            c.bind[bone] = c.model.bindPose[bone].translation;
        }
        SetupHands(c);
        for (int side = 0; side < 2; ++side)
        {
            const int toe = FindBone(c.model, side == 0 ? "RightToeBase" : "LeftToeBase");
            const int foot = c.joints[side == 0 ? RightFoot : LeftFoot];
            if (toe >= 0)
            {
                const Vector3 direction = Vector3Subtract(c.bind[(size_t)toe], c.bind[(size_t)foot]);
                c.toeOut[side] = std::fabs(std::atan2(direction.x, direction.z)) * RAD2DEG;
            }
        }

        int triangles = 0;
        float top = 0.0f;
        for (int index = 0; index < c.model.meshCount; ++index)
        {
            Mesh& mesh = c.model.meshes[index];
            triangles += mesh.triangleCount;
            for (int v = 0; v < mesh.vertexCount; ++v)
            {
                top = std::max(top, mesh.vertices[v * 3 + 1]);
            }
            // Арьсласан оройнуудыг хадгалах зай (ачаалагч үүсгээгүй бол)
            if (!mesh.animVertices)
            {
                mesh.animVertices = (float*)MemAlloc((unsigned int)(mesh.vertexCount * 3) * sizeof(float));
            }
            if (!mesh.animNormals && mesh.normals)
            {
                mesh.animNormals = (float*)MemAlloc((unsigned int)(mesh.vertexCount * 3) * sizeof(float));
            }
        }
        c.scale = top > 0.01f ? kTargetHeight / top : 1.0f;
        {
            // Алхааны өргөн загвар бүрд өөр: ташаа өргөн, өмд бүдүүн (анхны зогсолтод өвдөг ташаанаас илүү
            // гадагш гарсан) загвар өргөн гишгэнэ, эс бөгөөс гуя, өвдөг хоорондоо шүргэлцэнэ.
            const float hip = std::fabs(c.bind[(size_t)c.joints[LeftUpLeg]].x) * c.scale;
            const float knee = std::fabs(c.bind[(size_t)c.joints[LeftLeg]].x) * c.scale;
            c.stepWidth = hip * 0.5f + std::max(0.0f, knee - hip) * 1.6f;
        }

        c.texture = LoadTexture(texturePath.c_str());
        SetTextureFilter(c.texture, TEXTURE_FILTER_BILINEAR);
        c.material = LoadMaterialDefault();
        c.material.shader = shader;
        c.material.maps[MATERIAL_MAP_ALBEDO].texture = c.texture;
        c.loaded = true;

        TraceLog(LOG_INFO, "CHARACTER: %s loaded: %d triangles, %d bones, scale %.2f", kNames[id], triangles,
                 c.model.boneCount, c.scale);
    }
}

void UnloadCharacterModels()
{
    for (Character& c : characters)
    {
        if (c.loaded)
        {
            c.material.shader = {}; // shader нь Environment-ийнх
            c.material.maps[MATERIAL_MAP_ALBEDO].texture = {};
            UnloadMaterial(c.material);
            UnloadTexture(c.texture);
            UnloadModel(c.model);
            c = Character{};
        }
    }
}

namespace
{
    bool bindPoseOnly = false;
}

void SetCharacterBindPose(bool enabled)
{
    bindPoseOnly = enabled;
}

bool HasCharacterModel(int id)
{
    return id >= 0 && id < kCharacterCount && characters[id].loaded;
}

namespace
{
    float Smooth(float t)
    {
        t = std::clamp(t, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    Vector3 Mix(Vector3 a, Vector3 b, float t)
    {
        return Vector3Lerp(a, b, std::clamp(t, 0.0f, 1.0f));
    }

    // Ясыг (ихэвчлэн ташаа) шилжүүлж, эргүүлнэ. offset нь загварын нэгжээр.
    void Place(Character& c, Joint joint, Quaternion rotation, Vector3 offset)
    {
        const int bone = c.joints[joint];
        Follow(c, bone);
        c.rotation[(size_t)bone] = QuaternionMultiply(rotation, c.rotation[(size_t)bone]);
        c.position[(size_t)bone] = Vector3Add(c.position[(size_t)bone], offset);
    }

    // Ясны эргэлтийг эцгээс үл хамааран шууд тавина (тавхайг газартай зэрэгцүүлэхэд).
    void Orient(Character& c, Joint joint, Quaternion rotation)
    {
        const int bone = c.joints[joint];
        Follow(c, bone);
        c.rotation[(size_t)bone] = rotation;
    }

    // Шууны орон зай: -Y нь тохойноос бугуй руу, +Z нь аль болох дээш (хутга, гранатыг зурахад).
    Quaternion ForearmBasis(Vector3 elbow, Vector3 wrist)
    {
        const Vector3 y = Vector3Normalize(Vector3Subtract(elbow, wrist));
        Vector3 z = Vector3Subtract({ 0.0f, 1.0f, 0.0f }, Vector3Scale(y, y.y));
        z = Vector3Length(z) > 0.05f ? Vector3Normalize(z) : Vector3{ 0.0f, 0.0f, 1.0f };
        const Vector3 x = Vector3CrossProduct(y, z);
        Matrix m = MatrixIdentity();
        m.m0 = x.x; m.m1 = x.y; m.m2 = x.z;
        m.m4 = y.x; m.m5 = y.y; m.m6 = y.z;
        m.m8 = z.x; m.m9 = z.y; m.m10 = z.z;
        return QuaternionFromMatrix(m);
    }
}

// Дүрийн "хөдөлгөөний найруулагч": HumanoidPose дахь төлөвөөс (алхааны үе, хурд, онилж буй эсэх, ухралт,
// үйлдлийн явц) биеийн бүх хэсгийн байрлалыг тооцно.
//  - Хөл: тавхай бүр алхааны замаараа явж, гуя шилбэ нь урвуу кинематикаар дагана; ингэснээр ташаа
//    хөдлөхөд (суух, найгах, урагш дайрах) тавхай газартаа үлдэнэ.
//  - Их бие: ташаа, цээж эсрэг чиглэлд эргэж, алхам бүрд найгана; зогсож байхад жингээ шилжүүлж, амьсгална.
//  - Зэвсэг бүр "бэлэн" ба "онилсон" хоёр байрлалтай; хутгаар дайрах, гранат шидэх нь бүтэн биеийн хөдөлгөөн.
CharacterFrames DrawCharacterModel(int id, const HumanoidPose& pose, HeldItem held, bool direct, Color tint)
{
    Character& c = characters[id];
    std::fill(c.solved.begin(), c.solved.end(), 0);
    const float s = c.scale;

    const bool launcher = held == HeldItem::Launcher;
    const bool twoHanded = held == HeldItem::Rifle || held == HeldItem::Sniper || held == HeldItem::Shotgun || launcher;
    const bool pistol = held == HeldItem::Pistol;
    const bool knife = held == HeldItem::Knife;
    const bool grenade = held == HeldItem::Grenade;

    // --- Төлөв ---
    const bool onFoot = !direct && pose.grounded && !pose.seated && pose.fall <= 0.0f && pose.spin == 0.0f;
    const float move = onFoot ? std::min(pose.moveAmount, 1.0f) : 0.0f;                       // 0 зогссон .. 1 алхаа
    const float run = onFoot ? std::clamp(pose.moveAmount - 1.0f, 0.0f, 0.6f) / 0.6f : 0.0f;  // 0 алхаа .. 1 гүйлт
    const float still = 1.0f - move;
    const float time = pose.time;
    const float phase = pose.walkPhase;
    const float recoil = pose.recoil;
    const float aim = twoHanded || pistol ? std::clamp(pose.aim, 0.0f, 1.0f) : 0.0f;
    const float progress = pose.actionProgress;
    const float prone = onFoot ? std::clamp(pose.prone, 0.0f, 1.0f) : 0.0f;
    const float crouch = onFoot ? std::clamp(pose.crouch, 0.0f, 1.0f) * (1.0f - prone) : 0.0f;
    const float upright = 1.0f - prone;
    const bool lying = prone > 0.5f; // хөл газраар сунасан: алхааны оронд мөлхөх хөдөлгөөн

    // Хутга: дайралтын хүч 0 -> 1 -> 0. Гранат: далайлт, шидэлт, төгсгөл.
    const float thrust = knife && progress >= 0.0f ? std::sin(progress * PI) : 0.0f;
    float wind = 0.0f, release = 0.0f;
    if (grenade && progress >= 0.0f)
    {
        wind = progress < 0.3f ? Smooth(progress / 0.3f) : 1.0f - Smooth((progress - 0.3f) / 0.2f);
        release = Smooth((progress - 0.32f) / 0.28f) * (1.0f - Smooth((progress - 0.8f) / 0.2f));
    }

    // Зогсолт: онилох, хутга барих үед хөлөө урагш хойш тавьж, өвдгөө бага зэрэг нугална.
    const float stance = still * std::max({ aim, knife ? 1.0f : 0.0f, wind, release }) * (onFoot ? 1.0f : 0.0f) *
                         (1.0f - crouch) * upright;
    // Тайван зогсолтын "амьд" хөдөлгөөн: жин шилжүүлэх, эргэн тойрноо харах.
    const float idle = still * (1.0f - stance) * (onFoot ? 1.0f : 0.0f) * (1.0f - crouch) * upright;
    const float weight = std::sin(time * 0.55f);

    // --- Ташаа ---
    const float gaitYaw = move * (6.0f + 5.0f * run) * std::sin(phase);
    const float hipYaw = gaitYaw - 12.0f * stance * (twoHanded ? aim : 0.0f) - 16.0f * (knife ? still : 0.0f) + 30.0f * thrust +
                         24.0f * wind - 22.0f * release;
    const float hipRoll = move * 1.4f * std::cos(phase) + idle * 1.6f * weight;
    // Ташааны өндөр. Хөл бараг тэнийсэн байдаг тул хэдхэн см доошлоход л өвдөг их нугарна: алхаанд маш бага.
    // Хоёр хөл урагш хойш салсан мөчид хамгийн доор, нэг хөл дээрээ тулсан мөчид хамгийн дээр байна.
    // (Үндсэн хувиргалтын pose.bob нь эсрэг үетэй тул түүнийг энд хүчингүй болгоно.)
    const float drop = move * (0.008f + 0.02f * run + (0.026f + 0.02f * run) * std::fabs(std::cos(phase))) + pose.bob +
                       0.02f * stance + 0.1f * thrust + 0.04f * wind + 0.04f * release +
                       // Суух: ташаа өвдөгний түвшинд хүртэл доошилно (алхахад бага зэрэг өндийнө).
                       crouch * (0.4f - 0.05f * move);
    const float sway = move * 0.014f * std::cos(phase) + idle * 0.022f * weight;
    const float advance = 0.2f * thrust - 0.07f * wind + 0.12f * release;

    // Хэвтэж байхад ташаа алхааны эргэлтгүй; мөлхөхөд л хоёр тийш бага зэрэг найгана.
    const float crawlRoll = prone * move * 7.0f * std::sin(phase);
    const Quaternion hipRotation = onFoot ? QuaternionMultiply(AxisAngle(0.0f, 1.0f, 0.0f, hipYaw * upright + crawlRoll),
                                                               AxisAngle(0.0f, 0.0f, 1.0f, hipRoll * upright))
                                          : QuaternionIdentity();
    Place(c, Hips, hipRotation,
          onFoot ? Vector3Scale({ sway * upright, -drop * upright, advance * upright }, 1.0f / s) : Vector3{ 0.0f, 0.0f, 0.0f });

    // --- Нуруу, цээж, толгой ---
    float lean = pose.lean;
    float roll = pose.roll;
    float chestYaw = 0.0f; // цээжний эргэлт (ертөнцийн тэнхлэгт; ташааны эргэлтийг нөхнө)
    float headYaw = 0.0f, headPitch = pose.headPitch, headRoll = pose.headRoll;
    if (onFoot)
    {
        lean = move * (3.0f + 11.0f * run) + stance * 7.0f + 16.0f * thrust - 12.0f * wind + 24.0f * release -
               recoil * (twoHanded ? 3.0f : 1.5f) + idle * 1.2f * std::sin(time * 0.4f);
        roll = move * 2.0f * std::sin(phase) - idle * 1.5f * weight;

        if (twoHanded) chestYaw = -26.0f - recoil * 4.0f;        // буу тогтвортой: цээж алхаанаас үл хамаарна
        else if (pistol) chestYaw = -4.0f * aim - recoil * 2.0f;
        else chestYaw = -0.8f * gaitYaw + (knife ? -20.0f * still + 46.0f * thrust : 0.0f) + 38.0f * wind - 34.0f * release;

        // Толгой: харц урагшаа тогтвортой; тайван үед эргэн тойрноо аажим харна, онилоход бууны бөгс рүү хазайна.
        headYaw = idle * (1.0f - aim) * 16.0f * std::sin(time * 0.33f + 1.3f * std::sin(time * 0.17f));
        headPitch = -lean * 0.55f + (twoHanded ? 6.0f * aim : 0.0f) + idle * 2.0f * std::sin(time * 0.23f);
        headRoll = (twoHanded ? -7.0f * aim : 0.0f) - roll * 0.6f;

        // Суусан: урагш бөхийнө. Хэвтсэн: цээжээ тохойгоороо өргөж (нуруу хойш нумарна), толгойгоо өргөж урагш харна.
        lean = lean * upright + 15.0f * crouch - 14.0f * prone;
        roll *= upright;
        chestYaw = chestYaw * upright + (twoHanded ? -6.0f : 0.0f) * prone;
        headPitch = headPitch * upright - 6.0f * crouch - 44.0f * prone;
        headRoll *= upright;
    }
    else if (twoHanded)
    {
        chestYaw = -26.0f;
    }

    Turn(c, Spine, QuaternionMultiply(AxisAngle(1.0f, 0.0f, 0.0f, lean), AxisAngle(0.0f, 0.0f, 1.0f, roll)));
    Turn(c, Spine1, QuaternionMultiply(AxisAngle(0.0f, 1.0f, 0.0f, chestYaw - (onFoot ? hipYaw * upright : 0.0f)),
                                       AxisAngle(1.0f, 0.0f, 0.0f, -1.8f * pose.breath)));
    const Quaternion chest = c.rotation[(size_t)c.joints[Spine1]];
    Turn(c, Neck, QuaternionMultiply(AxisAngle(0.0f, 1.0f, 0.0f, headYaw - chestYaw * 0.85f),
                                     QuaternionMultiply(AxisAngle(1.0f, 0.0f, 0.0f, headPitch),
                                                        AxisAngle(0.0f, 0.0f, 1.0f, headRoll))));

    // --- Хөл ---
    const Vector3 down = { 0.0f, -1.0f, 0.0f };
    for (int side = 0; side < 2; ++side)
    {
        const Joint upper = side == 0 ? RightUpLeg : LeftUpLeg;
        const Joint lower = side == 0 ? RightLeg : LeftLeg;
        const Joint foot = side == 0 ? RightFoot : LeftFoot;
        const float outward = side == 0 ? -1.0f : 1.0f;

        if (lying)
        {
            // Хэвтсэн: хөл газраар хойш сунана. Мөлхөхөд хөл ээлжлэн хажуу тийш нугарч түлхэнэ.
            const float push = move * std::max(0.0f, std::sin(phase + (side == 0 ? 0.0f : PI)));
            const Quaternion spread = AxisAngle(0.0f, 0.0f, 1.0f, (9.0f + 30.0f * push) * outward);
            const Quaternion thigh = QuaternionMultiply(spread, AxisAngle(1.0f, 0.0f, 0.0f, -6.0f - 22.0f * push));
            const Quaternion shin = QuaternionMultiply(thigh, AxisAngle(1.0f, 0.0f, 0.0f, 10.0f + 62.0f * push));
            Aim(c, upper, lower, Rotate(thigh, down));
            Aim(c, lower, foot, Rotate(shin, down));
            continue;
        }
        if (!onFoot)
        {
            // Унаа, үсрэлт, уналт: гуя шилбийг өгөгдсөн өнцгөөр нь шууд тавина.
            const Quaternion spread = AxisAngle(0.0f, 0.0f, 1.0f, pose.legSpread * outward);
            const Quaternion thigh = QuaternionMultiply(spread, AxisAngle(1.0f, 0.0f, 0.0f, -pose.hip[side]));
            const Quaternion shin = QuaternionMultiply(thigh, AxisAngle(1.0f, 0.0f, 0.0f, pose.knee[side]));
            Aim(c, upper, lower, Rotate(thigh, down));
            Aim(c, lower, foot, Rotate(shin, down));
            continue;
        }

        const Vector3 ankleRest = Vector3Scale(c.bind[(size_t)c.joints[foot]], s);

        // Алхааны мөчлөг: эхний 60% нь тулах үе (тавхай газраар хойш гулсана), үлдсэн нь урагш зөөх үе.
        float cycle = (phase + (side == 0 ? 0.0f : PI)) / (2.0f * PI);
        cycle -= std::floor(cycle);
        const float stride = move * (0.55f + 0.5f * run) * (1.0f - 0.4f * crouch);
        float step = 0.0f, lift = 0.0f, pitch = 0.0f;
        if (cycle < 0.6f)
        {
            const float t = cycle / 0.6f;
            step = stride * (0.5f - t);
            pitch = move * 24.0f * Smooth((t - 0.7f) / 0.3f); // түлхэх үед өсгий өргөгдөнө
        }
        else
        {
            const float t = (cycle - 0.6f) / 0.4f;
            step = stride * (Smooth(t) - 0.5f);
            lift = move * (0.08f + 0.12f * run) * std::sin(t * PI);
            pitch = move * (24.0f - 42.0f * t);              // хуруугаа доош -> өсгийгөөр буух
        }

        // Тавхайн хоорондох зай: загварын анхны зогсолт хөлөө алцайлгасан байдаг тул нарийсгана.
        // Зогсохдоо мөрний өргөнөөс нарийн, алхахдаа бараг нэг шугам дээр гишгэнэ.
        // Зогсолтын тавил: зүүн хөл урд, баруун хөл хойно, арай өргөн. Тайван үед нэг өвдөг сулардаг.
        // Алхам явж буй зүг рүүгээ гишгэнэ: хажуу тийш, арагш алхахад хөл тэр зүг рүү зөөгдөнө.
        const float along = std::cos(pose.moveDirection), across = std::sin(pose.moveDirection);
        float x = outward * (c.stepWidth + 0.045f * (1.0f - move * std::fabs(along)) + 0.04f * stance + 0.05f * crouch) +
                  step * across;
        float z = ankleRest.z + step * along + (side == 0 ? -0.13f : 0.15f) * stance;
        // Суусан: зүүн хөл урд, баруун хөл хойно өсгийгөө өргөсөн (нэг өвдөг доор).
        z += (side == 0 ? -0.2f : 0.16f) * crouch;
        if (side == 0) pitch += 34.0f * crouch * still;
        if (side == 0) z += 0.44f * thrust - 0.1f * wind;   // хутгаар дайрах: баруун хөл урагш гишгэнэ
        else z += 0.26f * release;                           // шидэлтийн төгсгөлд зүүн хөл урагш
        const float relax = idle * 0.035f * std::max(0.0f, side == 0 ? weight : -weight);
        z += relax;
        lift += relax * 0.5f;

        // Үндсэн хувиргалт биеийг pose.bob-оор дээш доош хөдөлгөдөг: тавхайг газарт үлдээхийн тулд нөхнө.
        const Vector3 target = { x, ankleRest.y + lift - pose.bob, z };
        // Өвдөг эгц урагш нугарна; тавхай урагшаа харна (загварын анхны зогсолтод хуруу нь гадагш эргэсэн).
        Reach(c, upper, lower, foot, Vector3Scale(target, 1.0f / s), { outward * 0.03f, 0.1f, 1.0f }, 0.0f);
        Orient(c, foot, QuaternionMultiply(AxisAngle(0.0f, 1.0f, 0.0f, hipYaw * 0.6f - outward * c.toeOut[side] * 0.8f),
                                           AxisAngle(1.0f, 0.0f, 0.0f, pitch)));
    }

    const Vector3 rightShoulder = Vector3Scale(JointPosition(c, RightArm), s);
    const Vector3 leftShoulder = Vector3Scale(JointPosition(c, LeftArm), s);
    const Quaternion heading = onFoot ? AxisAngle(1.0f, 0.0f, 0.0f, lean * 0.35f * upright - 84.0f * prone)
                                      : QuaternionMultiply(AxisAngle(1.0f, 0.0f, 0.0f, lean), AxisAngle(0.0f, 0.0f, 1.0f, roll));

    CharacterFrames frames = {};
    frames.gun = MatrixIdentity();
    frames.rightForearm = MatrixIdentity();

    // Гараар зорилтод хүрэх: point нь дүрийн орон зайд, метрээр.
    auto reach = [&](int side, Vector3 point, Vector3 pole, Vector3 palm, bool facePalm) {
        const Joint upper = side == 0 ? RightArm : LeftArm;
        const Joint lower = side == 0 ? RightForeArm : LeftForeArm;
        const Joint hand = side == 0 ? RightHand : LeftHand;
        Reach(c, upper, lower, hand, Vector3Scale(point, 1.0f / s), pole, 0.08f / s);
        if (facePalm) FacePalm(c, side, palm);
    };
    // Гарыг өнцгөөр нь тавих (чөлөөтэй савлах гар).
    auto swing = [&](int side, float shoulderAngle, float yaw, float elbowAngle) {
        const float inward = side == 0 ? 1.0f : -1.0f;
        const Quaternion shoulder = QuaternionMultiply(AxisAngle(0.0f, 1.0f, 0.0f, yaw * inward),
                                                       AxisAngle(1.0f, 0.0f, 0.0f, -shoulderAngle));
        const Quaternion elbow = QuaternionMultiply(shoulder, AxisAngle(1.0f, 0.0f, 0.0f, -elbowAngle));
        Aim(c, side == 0 ? RightArm : LeftArm, side == 0 ? RightForeArm : LeftForeArm, Rotate(chest, Rotate(shoulder, down)));
        Aim(c, side == 0 ? RightForeArm : LeftForeArm, side == 0 ? RightHand : LeftHand, Rotate(chest, Rotate(elbow, down)));
    };
    // Алхаа, гүйлтийн үед чөлөөтэй гар хөлийн эсрэг савлана; гүйхэд тохой нугарна.
    const float armSwing = move * (26.0f + 16.0f * run) * std::sin(phase);
    const float armBend = 16.0f + move * (10.0f + 52.0f * run);
    float curl[2] = { 14.0f, 14.0f };

    if (twoHanded)
    {
        // --- Автомат, үрлэн буу, дурантай буу ---
        // Онилсон: бөгс мөрөнд, буу нүдний шугамд. Бэлэн (онилоогүй): ам нь доош зүүн тийш, гүйхэд бүр доор.
        // Хэвтсэн үед буу үргэлж урагш чиглэнэ. Цэнэглэх үед ам нь дээш өргөгдөнө.
        const float reload = pose.reload;
        const float reloading = reload >= 0.0f ? std::sin(reload * PI) : 0.0f;
        // Пуужин харвагч үргэлж мөрөн дээр: онилоогүй үед л ам нь бага зэрэг доошилно.
        const float low = (1.0f - aim) * (0.75f + 0.25f * run) * upright * (1.0f - 0.6f * reloading) * (launcher ? 0.3f : 1.0f);
        const float breathSway = 0.7f * pose.breath + (onFoot ? move * 1.2f * std::sin(phase * 2.0f) : 0.0f);
        const Quaternion gunRotation = QuaternionMultiply(
            heading, QuaternionMultiply(AxisAngle(0.0f, 1.0f, 0.0f, (12.0f + 22.0f * run) * low),
                                        AxisAngle(1.0f, 0.0f, 0.0f, 22.0f * low + breathSway - 3.5f * recoil -
                                                                        16.0f * reloading * upright)));
        // Бөгсний үзүүр (бууны орон зайд) баруун мөрний урд доор байрлана.
        // (Хэвтсэн үед бөгс мөрний урд, биеийн тэнхлэгийн дагуу.)
        // (Пуужин харвагч: хоолойн доод тал мөрний орой дээр хэвтэнэ, хоолой нь мөрнөөс хойш цухуйна.)
        const Vector3 butt = launcher
                                 ? Vector3Add(rightShoulder, Rotate(chest, { 0.03f, 0.09f, 0.02f }))
                                 : Vector3Add(rightShoulder, Rotate(chest, Mix({ 0.07f, -0.07f - 0.07f * low, 0.07f },
                                                                               { 0.06f, 0.05f, 0.06f }, prone)));
        const Vector3 buttLocal = launcher ? Vector3{ kGunX, kGunY - 0.05f, 0.2f - pose.gunKick }
                                           : Vector3{ kGunX, kGunY, 0.04f - pose.gunKick };
        const Vector3 gunOrigin = Vector3Subtract(butt, Rotate(gunRotation, buttLocal));
        frames.gun = Frame(gunRotation, gunOrigin);

        const Vector3 gripLocal = launcher ? Vector3{ kGunX, kGunY - 0.17f, 0.5f - pose.gunKick }
                                           : Vector3{ kGunX, kGunY - 0.14f, 0.3f - pose.gunKick };
        const Vector3 foreLocal = launcher ? Vector3{ kGunX, kGunY - 0.17f, 0.72f - pose.gunKick }
                                           : Vector3{ kGunX, kGunY - 0.075f, 0.66f - pose.gunKick };
        const Vector3 grip = Vector3Add(gunOrigin, Rotate(gunRotation, gripLocal));
        Vector3 fore = Vector3Add(gunOrigin, Rotate(gunRotation, foreLocal));
        if (reload >= 0.0f)
        {
            // Цэнэглэх: зүүн гар сумны хайрцгийг авч бүс рүүгээ буулгаад, шинийг авчирч хийгээд буцаж урд модоо барина.
            const Vector3 magazine = Vector3Add(gunOrigin, Rotate(gunRotation, { kGunX, kGunY - 0.3f, 0.54f }));
            const Vector3 belt = Vector3Add(Vector3Scale(JointPosition(c, LeftUpLeg), s), Rotate(chest, { 0.1f, 0.08f, 0.14f }));
            if (reload < 0.2f) fore = Mix(fore, magazine, Smooth(reload / 0.2f));
            else if (reload < 0.45f) fore = Mix(magazine, belt, Smooth((reload - 0.2f) / 0.25f));
            else if (reload < 0.72f) fore = Mix(belt, magazine, Smooth((reload - 0.45f) / 0.27f));
            else fore = Mix(magazine, fore, Smooth((reload - 0.72f) / 0.2f));
        }
        // Хэвтсэн үед тохой газарт тулна: хажуу тийш, доош (биеийн орон зайд урагш) чиглэнэ.
        reach(0, grip, Rotate(chest, Mix({ -0.7f, -1.0f, -0.3f }, { -0.9f, -0.4f, 0.8f }, prone)),
              Rotate(gunRotation, { 1.0f, 0.0f, -0.2f }), true);
        reach(1, fore, Rotate(chest, Mix({ 0.35f, -1.0f, 0.0f }, { 0.8f, -0.4f, 0.8f }, prone)),
              Rotate(gunRotation, { -0.25f, 1.0f, 0.0f }), true);
        curl[0] = curl[1] = 52.0f;
    }
    else if (pistol)
    {
        // --- Гар буу ---
        // Онилсон: хоёр гараа нүдний түвшинд урагш сунгана. Бэлэн: цээжиндээ ойр, ам нь доош.
        const Vector3 middle = Vector3Scale(Vector3Add(rightShoulder, leftShoulder), 0.5f);
        const Vector3 ready = Vector3Add(middle, Rotate(heading, { -0.03f, -0.24f, 0.3f }));
        const Vector3 aimed = Vector3Add(middle, Rotate(heading, { -0.02f, 0.03f + 0.05f * recoil, 0.5f - 0.04f * recoil }));
        const float extend = std::max(aim, prone); // хэвтсэн үед гараа үргэлж урагш сунгана
        const float reloading = pose.reload >= 0.0f ? std::sin(pose.reload * PI) : 0.0f;
        const Vector3 grip = Mix(ready, aimed, extend * (1.0f - 0.5f * reloading));
        const Quaternion gunRotation = QuaternionMultiply(
            heading, AxisAngle(1.0f, 0.0f, 0.0f, 26.0f * (1.0f - extend) - 16.0f * recoil + pose.breath - 30.0f * reloading));
        frames.rightForearm = Frame(gunRotation, grip);

        reach(0, Vector3Add(grip, Rotate(gunRotation, { -0.015f, 0.0f, 0.0f })), Rotate(chest, { -0.8f, -1.0f, 0.0f }),
              Rotate(gunRotation, { 1.0f, 0.0f, 0.0f }), true);
        // Цэнэглэх: зүүн гар бариулын доор шинэ сум түлхэж оруулна.
        reach(1, Vector3Add(grip, Rotate(gunRotation, { 0.035f, -0.025f - 0.16f * reloading, 0.01f })),
              Rotate(chest, { 0.8f, -1.0f, 0.0f }), Rotate(gunRotation, { -1.0f + reloading, reloading, 0.0f }), true);
        curl[0] = curl[1] = 58.0f;
    }
    else if (knife)
    {
        // --- Хутга ---
        // Бэлэн: хутгатай гар бэлхүүсний урд, сул гар цээжээ хамгаална. Дайралт: бүх биеэрээ урагш түлхэж хатгана.
        const Vector3 ready = Vector3Add(rightShoulder, Rotate(heading, { 0.1f, -0.32f + 0.02f * std::sin(time * 1.7f), 0.3f }));
        const Vector3 strike = Vector3Add(rightShoulder, Rotate(heading, { 0.16f, -0.1f, 0.62f }));
        const Vector3 hand = Mix(ready, strike, thrust);
        reach(0, hand, Rotate(chest, { -0.5f, -1.0f, -0.6f }), Rotate(heading, { 1.0f, 0.0f, 0.0f }), true);

        const Vector3 guard = Vector3Add(leftShoulder, Rotate(heading, { -0.14f, -0.16f - 0.1f * thrust, 0.3f - 0.22f * thrust }));
        reach(1, guard, Rotate(chest, { 0.6f, -1.0f, -0.2f }), Rotate(heading, { -1.0f, 0.0f, 0.3f }), true);

        const Vector3 elbow = Vector3Scale(JointPosition(c, RightForeArm), s);
        const Vector3 wrist = Vector3Scale(JointPosition(c, RightHand), s);
        frames.rightForearm = Frame(ForearmBasis(elbow, wrist), elbow);
        curl[0] = 60.0f;
        curl[1] = 30.0f;
    }
    else if (grenade)
    {
        // --- Гранат ---
        // Далайлт: бие хойш эргэж, шидэх гар толгойн ард, сул гар зорилт руу заана.
        // Шидэлт: бие урагш эргэж бөхийн, гар толгой дээгүүр урагш доош савна, зүүн хөл урагш гишгэнэ.
        const Vector3 hold = Vector3Add(rightShoulder, Rotate(heading, { 0.08f, -0.3f, 0.26f }));
        const Vector3 cocked = Vector3Add(rightShoulder, Rotate(chest, { -0.1f, 0.26f, -0.2f }));
        const Vector3 thrown = Vector3Add(rightShoulder, Rotate(heading, { 0.14f, 0.12f - 0.3f * Smooth((progress - 0.5f) / 0.3f), 0.58f }));
        const Vector3 hand = Mix(Mix(hold, cocked, wind), thrown, release);
        reach(0, hand, Rotate(chest, { -0.8f, -0.6f + 1.2f * wind, -0.4f }), Rotate(heading, { 0.3f, 0.0f, 1.0f }), wind + release > 0.1f);

        if (wind + release > 0.02f)
        {
            const Vector3 rest = Vector3Add(leftShoulder, Rotate(chest, { 0.05f, -0.5f, 0.05f }));
            const Vector3 point = Vector3Add(leftShoulder, Rotate(heading, { -0.08f, 0.02f, 0.52f }));
            const Vector3 back = Vector3Add(leftShoulder, Rotate(chest, { 0.1f, -0.38f, -0.16f }));
            reach(1, Mix(Mix(rest, point, wind), back, release), Rotate(chest, { 0.7f, -1.0f, -0.3f }), {}, false);
        }
        else
        {
            swing(1, armSwing, 4.0f, armBend);
        }

        const Vector3 elbow = Vector3Scale(JointPosition(c, RightForeArm), s);
        const Vector3 wrist = Vector3Scale(JointPosition(c, RightHand), s);
        frames.rightForearm = Frame(ForearmBasis(elbow, wrist), elbow);
        curl[0] = pose.hideItem ? 10.0f : 55.0f;
    }
    else
    {
        // --- Гар хоосон: унаа жолоодох зэрэг өгөгдсөн өнцгийг, эс бөгөөс алхааны чөлөөт савлалтыг ашиглана ---
        if (onFoot)
        {
            swing(0, -armSwing, 4.0f, armBend);
            swing(1, armSwing, 4.0f, armBend);
        }
        else
        {
            swing(0, pose.shoulder[0], pose.shoulderYaw[0], pose.elbow[0]);
            swing(1, pose.shoulder[1], pose.shoulderYaw[1], pose.elbow[1]);
        }
    }

    CurlFingers(c, 0, curl[0]);
    CurlFingers(c, 1, curl[1]);

    if (bindPoseOnly)
    {
        // Бүх ясыг анхны байрлалд нь буцаана.
        for (size_t bone = 0; bone < c.bind.size(); ++bone)
        {
            c.rotation[bone] = QuaternionIdentity();
            c.position[bone] = c.bind[bone];
            c.solved[bone] = 1;
        }
    }
    Skin(c);
    c.material.maps[MATERIAL_MAP_ALBEDO].color = tint;

    // Tripo-гийн загварууд хоёр талт материалтай: нөмрөг, малгайн хүрээ, хувцасны захууд нэг давхар
    // гадаргуугаас бүтдэг тул ар талыг нь ч зурна (эс бөгөөс нэг талаас нь нэвт харагдана).
    rlDisableBackfaceCulling();
    rlPushMatrix();
    rlScalef(s, s, s);
    for (int index = 0; index < c.model.meshCount; ++index)
    {
        DrawMesh(c.model.meshes[index], c.material, MatrixIdentity());
    }
    rlPopMatrix();
    rlEnableBackfaceCulling();

    return frames;
}
