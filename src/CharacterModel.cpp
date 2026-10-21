#include "CharacterModel.h"

#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    const char* const kNames[kCharacterCount] = { "fighter1",     "fighter2",       "fighter3",
                                                  "survivor_man", "survivor_woman", "zombie_walker",
                                                  "zombie_runner", "zombie_brute",  "giant_zombie",
                                                  "model-1" };

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
        std::vector<Mesh> lod;            // хөнгөн хувилбар (холын дүрд): ясны матрицыг үндсэн mesh-тэй хуваалцана
    };

    Character characters[kCharacterCount];

    // Холын дүрд зориулсан хөнгөн mesh: оройнуудыг cell хэмжээтэй нүдээр бүлэглэж (нүд бүрд нэг орой),
    // хоёр орой нь нэг нүдэнд орсон гурвалжнуудыг хаяна. Хол харагдах тул хэлбэр хангалттай хадгалагдана.
    // Текстурын өөр хэсэгт (UV seam-ийн хоёр тал) хамаарах оройг нийлүүлэхгүй: эс бөгөөс гадаргуу шаарсан харагдана.
    Mesh SimplifyMesh(const Mesh& source, float cell)
    {
        Mesh lod = {};
        if (source.vertexCount <= 0 || !source.vertices) return lod;

        std::unordered_map<uint64_t, int> cells;
        std::vector<int> remap((size_t)source.vertexCount);
        std::vector<int> representative;
        std::vector<Vector3> sum, normalSum;
        std::vector<int> count;
        for (int v = 0; v < source.vertexCount; ++v)
        {
            const float* p = source.vertices + v * 3;
            auto quantize = [](float value, float step, uint64_t mask) {
                return (uint64_t)((int64_t)std::floor(value / step) + (int64_t)(mask / 2)) & mask;
            };
            uint64_t key = quantize(p[0], cell, 0x3FF) | (quantize(p[1], cell, 0x3FF) << 10) | (quantize(p[2], cell, 0x3FF) << 20);
            if (source.texcoords)
            {
                const float* uv = source.texcoords + v * 2;
                key |= (quantize(uv[0], 1.0f / 40.0f, 0x7FFF) << 30) | (quantize(uv[1], 1.0f / 40.0f, 0x7FFF) << 45);
            }
            auto found = cells.find(key);
            int index;
            if (found == cells.end())
            {
                index = (int)representative.size();
                cells.emplace(key, index);
                representative.push_back(v);
                sum.push_back({ 0.0f, 0.0f, 0.0f });
                normalSum.push_back({ 0.0f, 0.0f, 0.0f });
                count.push_back(0);
            }
            else
            {
                index = found->second;
            }
            remap[(size_t)v] = index;
            sum[(size_t)index] = Vector3Add(sum[(size_t)index], { p[0], p[1], p[2] });
            if (source.normals)
            {
                const float* n = source.normals + v * 3;
                normalSum[(size_t)index] = Vector3Add(normalSum[(size_t)index], { n[0], n[1], n[2] });
            }
            ++count[(size_t)index];
        }

        std::vector<unsigned short> indices;
        const int triangles = source.triangleCount;
        for (int t = 0; t < triangles; ++t)
        {
            int corner[3];
            for (int k = 0; k < 3; ++k)
            {
                const int v = source.indices ? source.indices[t * 3 + k] : t * 3 + k;
                corner[k] = remap[(size_t)v];
            }
            if (corner[0] == corner[1] || corner[1] == corner[2] || corner[0] == corner[2]) continue;
            for (int k = 0; k < 3; ++k) indices.push_back((unsigned short)corner[k]);
        }
        const int vertices = (int)representative.size();
        if (vertices >= 65535 || indices.empty()) return lod;

        lod.vertexCount = vertices;
        lod.triangleCount = (int)indices.size() / 3;
        lod.vertices = (float*)MemAlloc((unsigned int)vertices * 3 * sizeof(float));
        if (source.texcoords) lod.texcoords = (float*)MemAlloc((unsigned int)vertices * 2 * sizeof(float));
        if (source.normals) lod.normals = (float*)MemAlloc((unsigned int)vertices * 3 * sizeof(float));
        if (source.boneIds) lod.boneIds = (unsigned char*)MemAlloc((unsigned int)vertices * 4);
        if (source.boneWeights) lod.boneWeights = (float*)MemAlloc((unsigned int)vertices * 4 * sizeof(float));
        for (int i = 0; i < vertices; ++i)
        {
            const int v = representative[(size_t)i];
            const Vector3 average = Vector3Scale(sum[(size_t)i], 1.0f / (float)count[(size_t)i]);
            lod.vertices[i * 3 + 0] = average.x;
            lod.vertices[i * 3 + 1] = average.y;
            lod.vertices[i * 3 + 2] = average.z;
            if (lod.texcoords) std::memcpy(lod.texcoords + i * 2, source.texcoords + v * 2, 2 * sizeof(float));
            if (lod.normals)
            {
                Vector3 normal = normalSum[(size_t)i];
                const float length = Vector3Length(normal);
                normal = length > 1e-5f ? Vector3Scale(normal, 1.0f / length) : Vector3{ source.normals[v * 3], source.normals[v * 3 + 1], source.normals[v * 3 + 2] };
                lod.normals[i * 3 + 0] = normal.x;
                lod.normals[i * 3 + 1] = normal.y;
                lod.normals[i * 3 + 2] = normal.z;
            }
            if (lod.boneIds) std::memcpy(lod.boneIds + i * 4, source.boneIds + v * 4, 4);
            if (lod.boneWeights) std::memcpy(lod.boneWeights + i * 4, source.boneWeights + v * 4, 4 * sizeof(float));
        }
        lod.indices = (unsigned short*)MemAlloc((unsigned int)indices.size() * sizeof(unsigned short));
        std::memcpy(lod.indices, indices.data(), indices.size() * sizeof(unsigned short));
        lod.boneCount = source.boneCount;
        lod.boneMatrices = source.boneMatrices; // хуваалцана (Skin нэг удаа бичнэ)
        UploadMesh(&lod, false);
        return lod;
    }

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

    // joint-ын доорх бүх ясыг (жишээ нь мөрний доорх шуу, бугуй, хуруу) дахин тооцохоор тэмдэглэнэ: мөчийг нэг удаа
    // байрлуулсны дараа дахин (IK-аар) байрлуулахад хүүхэд яснууд хуучин байрлалаа санаж үлдэхээс сэргийлнэ.
    void Unsolve(Character& c, Joint joint)
    {
        const int root = c.joints[joint];
        for (int bone = 0; bone < (int)c.solved.size(); ++bone)
        {
            for (int parent = c.model.bones[bone].parent; parent >= 0; parent = c.model.bones[parent].parent)
            {
                if (parent == root)
                {
                    c.solved[(size_t)bone] = 0;
                    break;
                }
            }
        }
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
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
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
        // Холын дүрд: ~4.5 см нүдээр бүлэглэсэн хөнгөн хувилбар
        int lodTriangles = 0;
        for (int index = 0; index < c.model.meshCount; ++index)
        {
            c.lod.push_back(SimplifyMesh(c.model.meshes[index], std::max(0.001f, top) / 30.0f));
            lodTriangles += c.lod.back().triangleCount;
        }
        TraceLog(LOG_INFO, "CHARACTER: %s low-detail version %d triangles", kNames[id], lodTriangles);
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
            for (Mesh& mesh : c.lod)
            {
                mesh.boneMatrices = nullptr; // үндсэн mesh-ийнх
                if (mesh.vertexCount > 0) UnloadMesh(mesh);
            }
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
CharacterFrames DrawCharacterModel(int id, const HumanoidPose& pose, HeldItem held, bool direct, Color tint, bool lowDetail)
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
    // Хөлийн алхаа: явган дүр, эсвэл явж буй зомби (их бие, гар нь өөрийн хэв маягаар, хөл нь газарт тулна).
    const bool walkLegs = onFoot || (direct && pose.plantWalk && pose.fall <= 0.0f && pose.spin == 0.0f);
    const float move = onFoot ? std::min(pose.moveAmount, 1.0f) : 0.0f;                       // 0 зогссон .. 1 алхаа
    const float run = onFoot ? std::clamp(pose.moveAmount - 1.0f, 0.0f, 0.6f) / 0.6f : 0.0f;  // 0 ердийн .. 1 хурдан гүйлт
    // Алхааны хэлбэр бодит хурдаас: алхам урт нь газраар явах хурдтай яг таарч, хөл гулсахгүй.
    const HumanoidGait gait = HumanoidGaitFor(walkLegs ? pose.speed : 0.0f);
    // Бага хурдад алхаа аажим жижгэрнэ (зомби удаан ч тод гишгэнэ).
    const float stride = std::min(1.0f, pose.speed / (onFoot ? 1.2f : 0.4f)) * (walkLegs ? 1.0f : 0.0f);
    // Нэг хөлийн мөчлөгийг "урд -> хойно -> урд" долгион болгоно: 1 = хамгийн урд (газарт хүрэх мөч),
    // -1 = хамгийн хойно (газраас салах мөч). Гар, ташаа энэ долгионоор хөлтэй эсрэг савлана.
    auto legWave = [&](float cycle) {
        const float c = cycle < gait.duty ? 0.5f * cycle / gait.duty : 0.5f + 0.5f * (cycle - gait.duty) / (1.0f - gait.duty);
        return std::cos(2.0f * PI * c);
    };
    float rightCycle = pose.walkPhase / (2.0f * PI);
    rightCycle -= std::floor(rightCycle);
    const float rightWave = legWave(rightCycle);
    const float still = 1.0f - move;
    const float time = pose.time;
    const float phase = pose.walkPhase;
    const float recoil = pose.recoil;
    const float aim = twoHanded || pistol ? std::clamp(pose.aim, 0.0f, 1.0f) : 0.0f;
    // Онилсон буу хараа руу дээш доош хазайна (мөрөө тойрон).
    const float aimPitch = pose.aimPitch * aim;
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
    // Ташаа урд гарсан хөлийн талаараа урагш эргэнэ (гүйхэд их бие эсрэгээр эргэж тэнцвэржүүлнэ).
    const float gaitYaw = stride * (5.0f + 4.0f * gait.run + 3.0f * run) * rightWave;
    const float hipYaw = gaitYaw - 12.0f * stance * (twoHanded ? aim : 0.0f) - 16.0f * (knife ? still : 0.0f) + 30.0f * thrust +
                         24.0f * wind - 22.0f * release;
    const float hipRoll = move * 1.4f * std::cos(phase) + idle * 1.6f * weight;
    // Ташааны өндөр. Хөл бараг тэнийсэн байдаг тул хэдхэн см доошлоход л өвдөг их нугарна: алхаанд маш бага.
    // Хоёр хөл урагш хойш салсан мөчид хамгийн доор, нэг хөл дээрээ тулсан мөчид хамгийн дээр байна.
    // (Үндсэн хувиргалтын pose.bob нь эсрэг үетэй тул түүнийг энд хүчингүй болгоно.)
    // Алхаа: тулсан хөл дээгүүр бие "урвуу дүүжин" шиг дээш гарна (хөл дундаа байхад хамгийн өндөр).
    // Гүйлт: хөл газарт буухад бие пүрш шиг доош суугаад, хоёр хөл хөндийрөх агшинд хамгийн өндөрт хүрнэ.
    const float bounceWave = std::cos(4.0f * PI * (rightCycle - 0.5f * gait.duty));
    const float bounce = stride * Lerp(0.022f, -(0.04f + 0.015f * run), gait.run) * bounceWave;
    const float drop = stride * (0.012f + 0.035f * gait.run) - bounce + pose.bob +
                       0.02f * stance + 0.1f * thrust + 0.04f * wind + 0.04f * release +
                       // Суух: ташаа өвдөгний түвшинд хүртэл доошилно (алхахад бага зэрэг өндийнө).
                       crouch * (0.4f - 0.05f * move);
    // Жин тулсан хөл рүүгээ шилжинэ (хөл дундаа байх мөчид хамгийн их; баруун тал нь -X).
    const float sway = -stride * (0.02f - 0.012f * gait.run) * std::cos(2.0f * PI * (rightCycle - 0.5f * gait.duty)) +
                       idle * 0.022f * weight;
    const float advance = 0.2f * thrust - 0.07f * wind + 0.12f * release;

    // Хэвтэж байхад ташаа алхааны эргэлтгүй; мөлхөхөд л хоёр тийш бага зэрэг найгана.
    const float crawlRoll = prone * move * 7.0f * std::sin(phase);
    const Quaternion hipRotation = onFoot ? QuaternionMultiply(AxisAngle(0.0f, 1.0f, 0.0f, hipYaw * upright + crawlRoll),
                                                               AxisAngle(0.0f, 0.0f, 1.0f, hipRoll * upright))
                                          : AxisAngle(0.0f, 1.0f, 0.0f, pose.seatTwist); // унаан дээр: ташаа унааны зүгт
    Vector3 hipOffset = onFoot ? Vector3Scale({ sway * upright, -drop * upright, advance * upright }, 1.0f / s) : Vector3{ 0.0f, 0.0f, 0.0f };
    if (!onFoot && pose.plantFeet)
    {
        // Хөлийн өнцгөөр (доорх "Унаа, үсрэлт" хэсэгтэй ижил) шагай ташаанаас хэр доор байхыг загварын жинхэнэ
        // ясны уртаар тооцож, хамгийн доод шагай зогсож байх үеийнхээ өндөрт хүртэл биеийг доошлуулна.
        float lowest = 0.0f;
        for (int side = 0; side < 2; ++side)
        {
            const Joint upper = side == 0 ? RightUpLeg : LeftUpLeg;
            const Joint lower = side == 0 ? RightLeg : LeftLeg;
            const Joint foot = side == 0 ? RightFoot : LeftFoot;
            const float hip = pose.hip[side] * DEG2RAD, knee = pose.knee[side] * DEG2RAD;
            const float reach = BoneLength(c, upper, lower) * std::cos(hip) + BoneLength(c, lower, foot) * std::cos(hip - knee);
            lowest = std::max(lowest, reach * std::cos(pose.legSpread * DEG2RAD));
        }
        const float standing = c.bind[(size_t)c.joints[RightUpLeg]].y - c.bind[(size_t)c.joints[RightFoot]].y;
        hipOffset.y -= standing - lowest;
    }
    Place(c, Hips, hipRotation, hipOffset);

    // --- Нуруу, цээж, толгой ---
    float lean = pose.lean;
    float roll = pose.roll;
    float chestYaw = 0.0f; // цээжний эргэлт (ертөнцийн тэнхлэгт; ташааны эргэлтийг нөхнө)
    float headYaw = 0.0f, headPitch = pose.headPitch, headRoll = pose.headRoll;
    if (onFoot)
    {
        lean = move * (3.0f + 5.0f * gait.run + 7.0f * run) + stance * 7.0f + 16.0f * thrust - 12.0f * wind + 24.0f * release -
               recoil * (twoHanded ? 3.0f : 1.5f) + idle * 1.2f * std::sin(time * 0.4f);
        roll = move * 2.0f * std::sin(phase) - idle * 1.5f * weight;

        if (twoHanded) chestYaw = -26.0f - recoil * 4.0f;        // буу тогтвортой: цээж алхаанаас үл хамаарна
        else if (pistol) chestYaw = -4.0f * aim - recoil * 2.0f;
        else chestYaw = -0.8f * gaitYaw + (knife ? -20.0f * still + 46.0f * thrust : 0.0f) + 38.0f * wind - 34.0f * release;

        // Толгой: харц урагшаа тогтвортой; тайван үед эргэн тойрноо аажим харна, онилоход бууны бөгс рүү хазайна.
        headYaw = idle * (1.0f - aim) * 16.0f * std::sin(time * 0.33f + 1.3f * std::sin(time * 0.17f));
        headPitch = -lean * 0.55f + (twoHanded ? 6.0f * aim : 0.0f) - 0.6f * aimPitch + idle * 2.0f * std::sin(time * 0.23f);
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

    if (!onFoot && std::fabs(pose.seatTwist) > 0.5f)
    {
        // Унаан дээр бие эргэсэн (ташаа унааны зүгт, цээж бай руу): эргэлтийг доод нуруу, цээж хоёрт хувааж
        // (хүн бүсэлхийгээрээ нэг дор 90 градус эргэж чадахгүй), бөхийлтийг биеийн харах зүгийн тэнхлэгт тооцно.
        // Ингэхгүй бол бүсэлхий нэг цэгт мушгирч, их бие хажуу тийш хэвийнэ.
        const Quaternion bend = QuaternionMultiply(AxisAngle(1.0f, 0.0f, 0.0f, lean), AxisAngle(0.0f, 0.0f, 1.0f, roll));
        Orient(c, Spine, QuaternionMultiply(AxisAngle(0.0f, 1.0f, 0.0f, pose.seatTwist * 0.5f), bend));
        Orient(c, Spine1, QuaternionMultiply(AxisAngle(0.0f, 1.0f, 0.0f, chestYaw),
                                             QuaternionMultiply(bend, AxisAngle(1.0f, 0.0f, 0.0f, -1.8f * pose.breath))));
    }
    else
    {
        Turn(c, Spine, QuaternionMultiply(AxisAngle(1.0f, 0.0f, 0.0f, lean), AxisAngle(0.0f, 0.0f, 1.0f, roll)));
        Turn(c, Spine1, QuaternionMultiply(AxisAngle(0.0f, 1.0f, 0.0f, chestYaw - (onFoot ? hipYaw * upright : pose.seatTwist)),
                                           AxisAngle(1.0f, 0.0f, 0.0f, -1.8f * pose.breath)));
    }
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
        if (!walkLegs)
        {
            // Унаа, үсрэлт, уналт: гуя шилбийг өгөгдсөн өнцгөөр нь шууд тавина. Унаан дээр бие эргэсэн бол
            // (seatTwist) хөл ташаатайгаа хамт унааны зүгт үлдэнэ (эс бөгөөс унааны дотор руу сунана).
            const Quaternion twist = AxisAngle(0.0f, 1.0f, 0.0f, pose.seatTwist);
            const Quaternion spread = QuaternionMultiply(twist, AxisAngle(0.0f, 0.0f, 1.0f, pose.legSpread * outward));
            const Quaternion thigh = QuaternionMultiply(spread, AxisAngle(1.0f, 0.0f, 0.0f, -pose.hip[side]));
            const Quaternion shin = QuaternionMultiply(thigh, AxisAngle(1.0f, 0.0f, 0.0f, pose.knee[side]));
            Aim(c, upper, lower, Rotate(thigh, down));
            Aim(c, lower, foot, Rotate(shin, down));
            if (pose.seated)
            {
                // Суусан, бөхийсөн, авирч буй үед тавхай газар / тавцантай хэвтээ (шилбийг дагаж хазайвал
                // өвдөг нугарахад гутлын хошуу тавцангаас доош цухуйна).
                Orient(c, foot, QuaternionMultiply(twist, AxisAngle(0.0f, 1.0f, 0.0f, -outward * c.toeOut[side] * 0.8f)));
            }
            continue;
        }

        const Vector3 ankleRest = Vector3Scale(c.bind[(size_t)c.joints[foot]], s);

        // Алхааны мөчлөг: эхний duty хэсэгт тавхай газарт тулж, бие урагшлахын хэрээр яг тэр хурдаар хойш
        // шилжинэ (газраар гулсахгүй); үлдсэн хэсэгт хөл агаараар урагш зөөгдөнө. Тавхай ташааны арай урд
        // буугаад, арай хойно газраас салдаг.
        float cycle = (phase + (side == 0 ? 0.0f : PI)) / (2.0f * PI);
        cycle -= std::floor(cycle);
        const float reachForward = 0.42f * gait.contact, reachBack = 0.58f * gait.contact;
        float step = 0.0f, lift = 0.0f, pitch = 0.0f;
        if (cycle < gait.duty)
        {
            const float t = cycle / gait.duty;
            step = reachForward - t * gait.contact;
            // Алхахад өсгийгөөр буугаад (хуруу дээш) тавхай газарт налж, гүйхэд тавхайн дундаар буна.
            // Түлхэх үед өсгий өргөгдөж, хуруун дээрээ үлдэнэ.
            pitch = stride * (-16.0f * (1.0f - gait.run) * (1.0f - Smooth(t / 0.22f)) +
                              (30.0f + 8.0f * gait.run) * Smooth((t - 0.5f) / 0.5f));
        }
        else
        {
            const float t = (cycle - gait.duty) / (1.0f - gait.duty);
            step = Lerp(-reachBack, reachForward, Smooth(t));
            // Хөл газраас салмагц өвдөг нугарч өсгий дээш гарна: алхахад хуруу газар шүргэхгүй хэмжээнд,
            // гүйхэд өсгий өгзөг рүү өргөгдөнө. Оргил нь зөөх үеийн эхний хагаст.
            const float height = Lerp(0.1f, std::min(0.48f, 0.24f + 0.045f * (pose.speed - 3.0f)), gait.run);
            lift = stride * height * pose.stepLift * std::sin(PI * std::pow(t, 0.65f));
            pitch = stride * Lerp(34.0f, -12.0f * (1.0f - gait.run) - 3.0f, Smooth(t)); // хуруу доош -> буухад дээш
        }
        // Өсгий өргөгдөхөд шагай дээшилнэ (хурууны үзүүрээр тулна).
        lift += 0.11f * std::sin(std::max(0.0f, pitch) * DEG2RAD);

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
    // Баруун хөл урд байхад баруун гар хойно. Гүйхэд тохой ~90° нугарна.
    const float armSwing = stride * (24.0f + 10.0f * gait.run + 12.0f * run) * rightWave;
    const float armBend = 16.0f + stride * (8.0f + 60.0f * std::max(0.9f * gait.run, run));
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
                                                                        16.0f * reloading * upright - aimPitch)));
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
        // Амьд хөдөлгөөн: амьсгал, алхам бүрийн дээш доош савлалт, тайван үед бууг хазайлгаж эргэн тойрноо ажиглах,
        // онилоход гар бага зэрэг чичирнэ, буудахад ам нь дээш "үсэрч" гар хойш түлхэгдэнэ.
        const Vector3 middle = Vector3Scale(Vector3Add(rightShoulder, leftShoulder), 0.5f);
        const float extend = std::max(aim, prone); // хэвтсэн үед гараа үргэлж урагш сунгана
        const float lowered = (1.0f - extend);
        const float stepBob = move * std::sin(phase * 2.0f);
        const Vector3 sway = { lowered * (0.016f * move * std::sin(phase) + 0.012f * idle * std::sin(time * 0.7f)),
                               0.012f * pose.breath + lowered * (0.022f * stepBob - 0.06f * run),
                               lowered * (0.01f * idle * std::sin(time * 0.45f) - 0.05f * run) };
        const Vector3 tremor = { 0.0025f * std::sin(time * 7.3f), 0.0025f * std::sin(time * 9.1f + 1.0f), 0.0f };
        const Vector3 ready = Vector3Add(middle, Rotate(heading, Vector3Add({ -0.03f, -0.24f, 0.3f }, sway)));
        const Vector3 aimed = Vector3Add(middle, Rotate(heading, Vector3Add({ -0.02f, 0.03f + 0.07f * recoil, 0.5f - 0.09f * recoil },
                                                                            Vector3Scale(tremor, still))));
        const float reloading = pose.reload >= 0.0f ? std::sin(pose.reload * PI) : 0.0f;
        const Vector3 grip = Mix(ready, aimed, extend * (1.0f - 0.5f * reloading));
        const float pitch = lowered * (26.0f + 14.0f * run + 5.0f * stepBob) - 28.0f * recoil + pose.breath -
                            30.0f * reloading - aimPitch;
        const float cant = lowered * (9.0f * idle * std::sin(time * 0.35f) + 4.0f * move * std::sin(phase)); // бууг хажуу тийш хазайлгах
        const Quaternion gunRotation = QuaternionMultiply(
            heading, QuaternionMultiply(AxisAngle(0.0f, 1.0f, 0.0f, lowered * 6.0f * idle * std::sin(time * 0.5f + 0.8f)),
                                        QuaternionMultiply(AxisAngle(1.0f, 0.0f, 0.0f, pitch), AxisAngle(0.0f, 0.0f, 1.0f, cant))));
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
        // Бэлэн: тулааны байрлал — хутга бэлхүүсний урд, ир нь урагш дээш зорилт руу, сул гар нүүрээ хамгаална.
        // Тайван үед хоёр гар бага зэрэг "нүүдэллэж" (тулаанд бэлэн хүн шиг), алхахад алхамтай хамт савлана.
        // Дайралт: эхлээд гараа хойш татаж (далайлт), дараа нь бүх биеэрээ урагш шидэж хатгаад, буцаж бэлэн болно.
        const float p = progress >= 0.0f ? progress : -1.0f;
        const float windUp = p < 0.0f ? 0.0f : p < 0.28f ? Smooth(p / 0.28f) : 1.0f - Smooth((p - 0.28f) / 0.14f);
        const float lunge = p < 0.28f ? 0.0f : p < 0.5f ? Smooth((p - 0.28f) / 0.22f) : 1.0f - Smooth((p - 0.55f) / 0.45f);
        const float runLow = run * 0.6f;
        const Vector3 weave = { 0.028f * still * std::sin(time * 1.3f) + 0.012f * move * std::sin(phase),
                                0.022f * still * std::sin(time * 2.1f) + 0.03f * move * std::sin(phase * 2.0f) - 0.12f * runLow,
                                0.025f * still * std::sin(time * 1.7f + 1.0f) - armSwing * 0.0018f };
        const Vector3 ready = Vector3Add(rightShoulder, Rotate(heading, Vector3Add({ 0.07f, -0.3f, 0.36f }, weave)));
        const Vector3 cocked = Vector3Add(rightShoulder, Rotate(heading, { -0.06f, -0.22f, 0.08f }));
        const Vector3 strike = Vector3Add(rightShoulder, Rotate(heading, { 0.14f, -0.1f, 0.68f }));
        const Vector3 hand = Mix(Mix(ready, cocked, windUp), strike, lunge);
        reach(0, hand, Rotate(chest, { -0.6f, -1.0f, -0.5f }), Rotate(heading, { 1.0f, 0.0f, 0.0f }), true);

        // Сул гар: нүүрний өмнө хамгаалж, хатгахад хойш татагдана (тэнцвэр)
        const Vector3 guardWeave = { 0.02f * still * std::sin(time * 1.5f + 2.0f), 0.02f * still * std::sin(time * 1.9f + 0.5f), 0.0f };
        const Vector3 guard = Vector3Add(leftShoulder, Rotate(heading, Vector3Add({ -0.12f, -0.1f - 0.08f * lunge + 0.04f * windUp - 0.1f * runLow,
                                                                                    0.32f - 0.26f * lunge + 0.08f * windUp + armSwing * 0.0018f },
                                                                                  guardWeave)));
        reach(1, guard, Rotate(chest, { 0.6f, -1.0f, -0.2f }), Rotate(heading, { -1.0f, 0.2f, 0.3f }), true);

        // Ир: шуунаас үл хамааран бэлэн үед урагш дээш (бага зэрэг дотогш), далайхад дээш, хатгахад шууд урагш.
        Vector3 blade = Vector3Normalize(Mix(Mix({ 0.12f + 0.06f * still * std::sin(time * 1.1f), 0.32f + 0.05f * still * std::sin(time * 1.9f), 1.0f },
                                                 { 0.0f, 0.9f, 0.5f }, windUp),
                                             { 0.05f, -0.08f, 1.0f }, lunge));
        blade = Rotate(heading, blade);
        const Vector3 gripPoint = Vector3Scale(JointPosition(c, RightHand), s);
        const float bladeOffset = 0.33f; // DrawHandItem: бариул шууны эхнээс (тохойноос) ийм зайд
        frames.rightForearm = Frame(ForearmBasis(Vector3Add(gripPoint, Vector3Scale(blade, -0.2f)), gripPoint),
                                    Vector3Subtract(gripPoint, Vector3Scale(blade, bladeOffset - 0.05f)));
        curl[0] = 64.0f;
        curl[1] = 34.0f + 20.0f * still;
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
            if (pose.grip > 0.0f)
            {
                // Гар барих цэг рүү: тохой гадагш, хойш (тавцан түлхэх, хүрээнээс зүүгдэх).
                for (int side = 0; side < 2; ++side)
                {
                    const Vector3 hand = Vector3Scale(JointPosition(c, side == 0 ? RightHand : LeftHand), s);
                    const float outward = side == 0 ? -1.0f : 1.0f;
                    Unsolve(c, side == 0 ? RightArm : LeftArm);
                    reach(side, Mix(hand, pose.gripPoint[side], std::min(1.0f, pose.grip)),
                          Rotate(chest, { 0.7f * outward, -0.6f, -0.6f }), { 0.0f, -1.0f, 0.0f }, false);
                }
                curl[0] = curl[1] = 50.0f * pose.grip + 14.0f;
            }
        }
    }

    // Унаан дээр: зүүн гар жолоо (бариул, хазаар) барина (цэнэглэх үед л сум солихоор түр тавина).
    if (!onFoot && pose.rein > 0.0f && pose.reload < 0.0f)
    {
        const Vector3 hand = Vector3Scale(JointPosition(c, LeftHand), s);
        Unsolve(c, LeftArm);
        reach(1, Mix(hand, pose.reinPoint, std::min(1.0f, pose.rein)), Rotate(chest, { 0.7f, -0.6f, -0.6f }),
              { 0.0f, -1.0f, 0.0f }, false);
        curl[1] = 78.0f; // атгана
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
        const bool light = lowDetail && index < (int)c.lod.size() && c.lod[(size_t)index].vertexCount > 0;
        DrawMesh(light ? c.lod[(size_t)index] : c.model.meshes[index], c.material, MatrixIdentity());
    }
    rlPopMatrix();
    rlEnableBackfaceCulling();

    return frames;
}
