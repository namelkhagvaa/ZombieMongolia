#include "CharacterModel.h"

#include "AnimClips.h"
#include "MeshBuilder.h"
#include "Platform.h"
#include "Profile.h"

#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <map>
#include <array>
#include <cstdio>
#include <cstdlib>
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
                                                  "model-1",       "model-3",       "demon",
                                                  "iron",          "survivor_doctor_man", "survivor_doctor_woman",
                                                  "survivor_student_boy", "survivor_student_girl", "survivor_monk",
                                                  "model2",        "woman_zombie", "fp_arms",
                                                  "emgen" };

    // Загварын өндөр тоглоомын хүний өндөртэй (~1.9 м) таарахаар томруулна. Аврах иргэд бодит харьцаагаар:
    // эрэгтэй цэргээс бага зэрэг намхан, эмэгтэй, сурагч (15 настай) нь түүнээс намхан.
    float TargetHeight(int id)
    {
        switch (id)
        {
        case kCharacterSurvivorMan:
        case kCharacterDoctorMan: return 1.84f;
        case kCharacterSurvivorWoman:
        case kCharacterDoctorWoman:
        case kCharacterZombieWoman: return 1.72f;
        case kCharacterZombieElder: return 1.62f; // (хөгшин эмэгтэй, бөгтөр)
        case kCharacterStudentBoy: return 1.70f;
        case kCharacterStudentGirl: return 1.64f;
        case kCharacterMonk: return 1.78f;
        default: return 1.9f;
        }
    }

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
        std::vector<Mesh> lodFar;         // маш хөнгөн (манан дунд жижиг харагдах дүрд)
        std::vector<Mesh> arms;           // зөвхөн гарын гурвалжнууд (1-р этгээд): ясны матрицыг үндсэн mesh-тэй хуваалцана
        std::vector<Mesh> glove;          // зүүн сарвуу (1-р этгээд): бээлийн өнгөөр тусад нь зурна (arms-аас хасагдсан)
        // Сарвууны (бугуй, хуруу) хэмжээ: загварын сарвуу гартаа харьцуулахад хэт том бол (Tripo-гийн булчинлаг дүр)
        // бугуйг тойрон жижигрүүлнэ. handBone: яс сарвууны (RightHand/LeftHand ба доорх) хэсэгт байгаа эсэх.
        float handScale = 1.0f;
        std::vector<char> handBone;
        // Гарын зузаан (1-р этгээд): бугалга, шууг ясны тэнхлэгт перпендикуляр томруулна (урт нь хэвээр).
        // girthAxis: ясны анхны чиглэл (дараагийн үе рүү); тэг бол тэр ясанд хэрэглэхгүй.
        float girth = 1.0f;
        std::vector<Vector3> girthAxis;
        // Mixamo-гийн хөдөлгөөн (цэргүүд хуваалцана): их бие, хөлийг клипээр, гарыг зэвсгийн кодоор (IK)
        const AnimSet* anims = nullptr;
        std::vector<int> animBone;       // клипийн яс -> загварын яс (-1 = байхгүй)
        std::vector<int> animParent;     // клипийн ясны эцэг (клипийн дугаар, -1 = үндэс)
        // Анхны байрлалын засвар: дүрийн яс эх дүрийнхээс өөр чиглэлтэй (A-pose, T-pose) байж болно. Яс бүрийн чиглэлийг
        // эх дүрийнх рүү эргүүлсний дараа клипийн эргэлтийг хэрэглэнэ: R = D(эх) * restFix
        std::vector<Quaternion> restFix;
        float clipScale = 1.0f;          // эх дүрээс хэмжээний харьцаа (ташааны өндрөөр)
        int clipHips = 0;
        int clipSpine[4] = { -1, -1, -1, -1 }; // клипийн яс: Spine, Spine1, Spine2, Neck (бөхийлт, эргэлт, харц)
        int clipHead = -1;
        // Клипт байхгүй хуруунууд (дунд, ядам, чигчий) долоовор хурууны хөдөлгөөнийг хуулна
        struct FingerCopy
        {
            int bone;        // загварын яс
            int clip;        // клипийн яс (долоовор)
            int parentClip;  // эцэг нь клипийн яс бол дугаар, эс бөгөөс -1
            int parentCopy;  // эцэг нь хуулсан хуруу бол copy-н дугаар
            Quaternion fix;
        };
        std::vector<FingerCopy> fingerCopy;
        std::vector<Mesh> clipArms;      // 1-р этгээдэд: цэргийн өөрийн гар (ханцуйтай)
        std::vector<Mesh> clipHands;     // fp_arms: зөвхөн сарвуу (Iron-ы төмөр гарт)
        int idleClip[2] = { -1, -1 }, crouchIdleClip[2] = { -1, -1 }; // [0] бэлэн, [1] онилсон
        int jumpClip = -1;
        int gaitClip[4][8] = {};   // [алхах, гүйх, хурдан гүйх, суугаад алхах][чиглэл: f fl l bl b br r fr]
        float gaitSpeed[4] = {};   // м/с (тоглоомын хэмжээгээр)
        float gaitStride[4] = {};  // нэг мөчлөгийн (хоёр алхам) урт, м
        // Хувцас (SetCharacterOutfit): бараа бүр биеийн ясны матрицыг хуваалцана
        struct Wear
        {
            Model model = {};
            std::string name;
            Texture2D texture = {};
            std::string texturePath;
            bool ownsTexture = true; // (иж бүрдлийн бараанууд нэг зургийг хуваалцана)
            Material material = {};
            std::vector<Mesh> lod, lodFar;
        };
        std::vector<Wear> wear;
        std::vector<std::vector<unsigned short>> bodyIndices; // биеийн mesh бүрийн бүх гурвалжин (нуухаас өмнө)
    };

    Character characters[kCharacterCount];

    // 1-р этгээдэд өөрийн гараараа харагдах дүрүүд: нүцгэн биетэй, хуягтай, hoodie-той (бусад цэргүүд fp_arms-ийн
    // тактикийн бээлий, ногоон ханцуйг хэрэглэнэ)
    bool WantsArms(int id)
    {
        // (iron-ы өөрийн гар: асар том хуяг дэлгэцийг дүүргэж, мөрөвч хөвж харагдана: хүрэл өнгөтэй бээлийтэй гараар)
        return id == kCharacterFpArms || id == kCharacterModel2 || id == kCharacterModel3 || id == kCharacterDemon;
    }

    // 1-р этгээдэд өөрийн ханцуйтай гараар урт буу барих дүрүүд (DrawCharacterClipArms); demon-ы мөч өөдөстэй тул бээлийтэй
    // гараар (fp_arms).
    bool WantsClipArms(int id)
    {
        return id == kCharacterFighter1 || id == kCharacterFighter2 || id == kCharacterSniper || id == kCharacterModel1 ||
               id == kCharacterModel2 || id == kCharacterModel3 || id == kCharacterIron || id == kCharacterFpArms;
    }

    // Сарвууны жижигрүүлэлт зөвхөн 1-р этгээдийн гарыг зурах үед (DrawCharacterArms): 3-р этгээдийн дүр хэвээр
    bool armsPass = false;

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
            Vector3 offset = Vector3Subtract(c.bind[(size_t)bone], c.bind[(size_t)parent]);
            if (armsPass && !c.handBone.empty() && c.handBone[(size_t)parent]) offset = Vector3Scale(offset, c.handScale);
            c.position[(size_t)bone] = Vector3Add(c.position[(size_t)parent], Rotate(c.rotation[(size_t)parent], offset));
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

    // Шууны тэнхлэгийн эргэлтийг (FacePalm) бугалгад хуваана: эс бөгөөс бүх эргэлт тохой дээр гарч мах нь мушгирна.
    // amount: 1 бол тохойд ердөө нугалалт, мушгиралт бүгд мөрөнд. Бугалганы чиглэл, үений байрлал өөрчлөгдөхгүй.
    void ShareTwist(Character& c, Joint upper, Joint lower, float amount)
    {
        const int bone = c.joints[upper];
        Follow(c, c.joints[lower]);
        const Vector3 rest = Vector3Normalize(Vector3Subtract(c.bind[(size_t)c.joints[lower]], c.bind[(size_t)bone]));
        const Quaternion forearm = c.rotation[(size_t)c.joints[lower]];
        const Vector3 direction = Rotate(c.rotation[(size_t)bone], rest);
        const Quaternion hinged = QuaternionMultiply(QuaternionFromVector3ToVector3(Rotate(forearm, rest), direction), forearm);
        c.rotation[(size_t)bone] = QuaternionNormalize(QuaternionSlerp(c.rotation[(size_t)bone], hinged, amount));
    }

    // Хуруунуудыг алга руу нугална: үе бүрийг degrees өнцгөөр (эрхий хурууг хагасаар).
    // indexDegrees >= 0: долоовор хуруу тусдаа (гох дээр).
    void CurlFingers(Character& c, int side, float degrees, float thumbDegrees = -1.0f, float indexDegrees = -1.0f)
    {
        const int hand = c.joints[side == 0 ? RightHand : LeftHand];
        Follow(c, hand);
        const Quaternion handRotation = c.rotation[(size_t)hand];
        for (int finger = 0; finger < 5; ++finger)
        {
            const Vector3 axis = Rotate(handRotation, c.fingerAxis[side][finger]);
            // (эрхий хуруу 1.3 дахин: 0.45 байхад нүцгэн гартай дүрийн эрхий хуруу бариулыг ороолгүй шулуун цухуйдаг байв)
            const float angle = finger == 0 && indexDegrees >= 0.0f ? indexDegrees
                                : finger != 4                       ? degrees
                                : thumbDegrees >= 0.0f              ? thumbDegrees : 1.3f * degrees;
            const Quaternion bend = QuaternionFromAxisAngle(axis, angle * DEG2RAD);
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

    // Автомат араг ясжуулалтын алдаа: зарим орой биеийн огт өөр хэсгийн ясанд жинтэй байдаг (Model-1-ийн гутлын
    // ул баруун гарт 59%). Анхны байрлалд үл харагдах ч гар хөдлөхөд тэр оройнууд гарыг дагаж, гутлаас бүс хүртэл
    // нарийн "шон" сунана. Оройд хамгийн ойр ясаас хамаагүй хол ясны нөлөөг хасаж жинг дахин хэвийн болгоно; юу ч
    // үлдэхгүй бол хамгийн ойрын ясанд бүрэн онооно. (Том биетэй зомбины гэдэс шиг бүх яснаас хол оройд хүрэхгүй.)
    void CleanStrayWeights(Model& model, const char* name)
    {
        // Ясны хэсгүүд: яс -> хүүхэд бүр (хүүхэдгүй бол цэг), анхны байрлалд
        struct Segment
        {
            int bone;
            Vector3 a, b;
        };
        std::vector<Segment> segments;
        for (int bone = 0; bone < model.boneCount; ++bone)
        {
            const Vector3 origin = model.bindPose[bone].translation;
            bool leaf = true;
            for (int child = 0; child < model.boneCount; ++child)
            {
                if (model.bones[child].parent != bone) continue;
                segments.push_back({ bone, origin, model.bindPose[child].translation });
                leaf = false;
            }
            if (leaf) segments.push_back({ bone, origin, origin });
        }
        auto segmentDistance = [](const Segment& s, Vector3 point) {
            const Vector3 axis = Vector3Subtract(s.b, s.a);
            const float length = Vector3DotProduct(axis, axis);
            const float t = length > 1e-8f ? std::clamp(Vector3DotProduct(Vector3Subtract(point, s.a), axis) / length, 0.0f, 1.0f) : 0.0f;
            return Vector3Distance(point, Vector3Add(s.a, Vector3Scale(axis, t)));
        };

        // Мөчний бүлэг: 1 баруун гар, 2 зүүн гар, 3 баруун хөл, 4 зүүн хөл, 0 их бие (Mixamo нэршлээр).
        // Анхны зогсолтод гар нь гуяны дэргэд унжсан загварт (iron, demon, дээлтэй хүмүүс) автомат жин нэг оройг
        // гар, хөл хоёуланд нь холбодог: гар хөл зөрж хөдлөхөд тэдгээрийн хоорондох арьс ир шиг сунана.
        std::vector<int> limb((size_t)model.boneCount, 0);
        for (int bone = 0; bone < model.boneCount; ++bone)
        {
            std::string boneName = model.bones[bone].name;
            const size_t colon = boneName.rfind(':');
            if (colon != std::string::npos) boneName = boneName.substr(colon + 1);
            for (int side = 0; side < 2; ++side)
            {
                const std::string prefix = side == 0 ? "Right" : "Left";
                for (const char* part : { "Arm", "ForeArm", "Hand" })
                {
                    if (boneName.rfind(prefix + part, 0) == 0) limb[(size_t)bone] = 1 + side;
                }
                for (const char* part : { "UpLeg", "Leg", "Foot", "Toe" })
                {
                    if (boneName.rfind(prefix + part, 0) == 0) limb[(size_t)bone] = 3 + side;
                }
            }
        }

        std::vector<float> reach((size_t)model.boneCount);
        for (int index = 0; index < model.meshCount; ++index)
        {
            Mesh& mesh = model.meshes[index];
            if (!mesh.boneWeights || !mesh.boneIds) continue;

            float top = 0.0f;
            std::vector<uint8_t> deforming((size_t)model.boneCount, 0);
            for (int v = 0; v < mesh.vertexCount; ++v)
            {
                top = std::max(top, mesh.vertices[v * 3 + 1]);
                for (int k = 0; k < 4; ++k)
                {
                    if (mesh.boneWeights[v * 4 + k] > 0.0f && mesh.boneIds[v * 4 + k] < model.boneCount)
                    {
                        deforming[mesh.boneIds[v * 4 + k]] = 1;
                    }
                }
            }
            const float margin = 0.29f * std::max(top, 0.001f); // ~56 см хүний өндөрт

            int fixed = 0;
            for (int v = 0; v < mesh.vertexCount; ++v)
            {
                const Vector3 point = { mesh.vertices[v * 3], mesh.vertices[v * 3 + 1], mesh.vertices[v * 3 + 2] };
                std::fill(reach.begin(), reach.end(), 1e9f);
                float nearestDistance = 1e9f;
                int nearest = 0;
                for (const Segment& s : segments)
                {
                    const float distance = segmentDistance(s, point);
                    reach[(size_t)s.bone] = std::min(reach[(size_t)s.bone], distance);
                    if (deforming[(size_t)s.bone] && distance < nearestDistance)
                    {
                        nearestDistance = distance;
                        nearest = s.bone;
                    }
                }

                float* weights = &mesh.boneWeights[v * 4];
                unsigned char* ids = &mesh.boneIds[v * 4];
                bool changed = false;
                // Тохойноос доош, гараас илүү их биед (бүс, хормой) ойр оройг гараас салгана: анхны зогсолтод гар нь
                // хувцастай нийлсэн загварт (model-1) гараа өргөхөд гараас бүс рүү хавтан сунадаг байв
                {
                {
                    float armNear = 1e9f, torsoNear = 1e9f, elbowLimit = -1e9f;
                    int torsoBone = -1;
                    for (int bone = 0; bone < model.boneCount; ++bone)
                    {
                        const int g = limb[(size_t)bone];
                        if (g == 1 || g == 2) armNear = std::min(armNear, reach[(size_t)bone]);
                        else if (g == 0 && deforming[(size_t)bone] && reach[(size_t)bone] < torsoNear)
                        {
                            torsoNear = reach[(size_t)bone];
                            torsoBone = bone;
                        }
                    }
                    for (const char* e : { "RightForeArm", "LeftForeArm" })
                    {
                        const int b = FindBone(model, e);
                        if (b >= 0) elbowLimit = std::max(elbowLimit, model.bindPose[b].translation.y);
                    }
                    float armShare = 0.0f;
                    for (int k = 0; k < 4; ++k)
                    {
                        if (ids[k] < model.boneCount && (limb[ids[k]] == 1 || limb[ids[k]] == 2)) armShare += weights[k];
                    }
                    if (armShare > 0.0f && torsoBone >= 0 && point.y < elbowLimit && torsoNear < armNear)
                    {
                        ids[0] = (unsigned char)torsoBone;
                        weights[0] = 1.0f;
                        for (int k = 1; k < 4; ++k) ids[k] = 0, weights[k] = 0.0f;
                        ++fixed;
                        continue;
                    }
                }
                // Нэг орой хоёр өөр мөчид (гар ба хөл, хоёр гар, хоёр хөл) холбогдсон бол илүү жинтэйг нь үлдээнэ
                float limbWeight[5] = {};
                for (int k = 0; k < 4; ++k)
                {
                    if (weights[k] > 0.0f && ids[k] < model.boneCount) limbWeight[limb[ids[k]]] += weights[k];
                }
                // Хоёр хөлийн хоорондох (бэлхүүс, өмдний дунд) холимог хэвийн тул хөлийг нэг бүлэг гэж үзнэ:
                // гар ба хөл холилдвол илүү жинтэйг нь, хоёр гар холилдвол илүү жинтэй гарыг үлдээнэ.
                const float legs = limbWeight[3] + limbWeight[4];
                const int arm = limbWeight[1] >= limbWeight[2] ? 1 : 2;
                const bool bothArms = limbWeight[1] > 0.0f && limbWeight[2] > 0.0f;
                // Аль бүлгийг үлдээх: жингээр биш, ясны хэсэгт аль нь ойр байгаагаар (iron: гар нь ташааны хажууд унжсан
                // анхны зогсолтод нударганы оройнууд хөлд илүү жинтэй байж, гар хөдлөхөд нударга ташаанд үлдэж, нүх гардаг байв)
                float groupDistance[5] = { 1e9f, 1e9f, 1e9f, 1e9f, 1e9f };
                for (int bone = 0; bone < model.boneCount; ++bone)
                {
                    // (жингүй хурууны ясыг ч тооцно: нударга хурууны ясны дэргэд, бугуйнаас хол)
                    groupDistance[limb[(size_t)bone]] = std::min(groupDistance[limb[(size_t)bone]], reach[(size_t)bone]);
                }
                const float legDistance = std::min(groupDistance[3], groupDistance[4]);
                const int nearArm = groupDistance[1] <= groupDistance[2] ? 1 : 2;
                int keep = 0; // 0: хөндөхгүй, 1/2: тэр гар, 5: хөлнүүд
                if (limbWeight[arm] > 0.0f && legs > 0.0f) keep = groupDistance[arm] < legDistance ? arm : 5;
                else if (bothArms) keep = nearArm;
                auto dropped = [&](int bone) {
                    const int g = limb[(size_t)bone];
                    if (keep == 0 || g == 0) return false;
                    if (keep == 5) return g == 1 || g == 2;
                    return g != keep;
                };
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k)
                {
                    if (weights[k] > 0.0f && (ids[k] >= model.boneCount || reach[ids[k]] > nearestDistance + margin ||
                                              dropped(ids[k])))
                    {
                        weights[k] = 0.0f;
                        changed = true;
                    }
                    sum += weights[k];
                }
                if (!changed) continue;
                ++fixed;
                if (sum > 1e-4f)
                {
                    for (int k = 0; k < 4; ++k) weights[k] /= sum;
                    continue;
                }
                ids[0] = (unsigned char)nearest;
                weights[0] = 1.0f;
                for (int k = 1; k < 4; ++k) ids[k] = 0, weights[k] = 0.0f;
            }

            // Гар, хөл хоёрыг (эсвэл хоёр гарыг) холбосон гурвалжин: анхны зогсолтод гар нь гуятай шүргэлцэж нэг
            // гадаргуу болсон загварт (iron) гар хөдлөхөд энэ "гүүр" том хавтан шиг сунана. Тийм гурвалжныг нууна.
            if (mesh.indices)
            {
                auto dominant = [&](int v) {
                    float best = 0.0f;
                    int group = 0;
                    for (int k = 0; k < 4; ++k)
                    {
                        const float w = mesh.boneWeights[v * 4 + k];
                        const int id = mesh.boneIds[v * 4 + k];
                        if (w > best && id < model.boneCount)
                        {
                            best = w;
                            group = limb[(size_t)id];
                        }
                    }
                    return group == 4 ? 3 : group; // хоёр хөл нэг бүлэг (бэлхүүс)
                };
                const int hips = FindBone(model, "Hips");
                float elbowY[2] = { -1e9f, -1e9f }; // тохойн өндөр (анхны зогсолтод): баруун, зүүн
                for (int side = 0; side < 2; ++side)
                {
                    const int elbow = FindBone(model, side == 0 ? "RightForeArm" : "LeftForeArm");
                    if (elbow >= 0) elbowY[side] = model.bindPose[elbow].translation.y;
                }
                int hidden = 0;
                std::vector<std::array<int, 3>> bridged; // нуусан гурвалжнууд (анхны оройнууд)
                std::vector<int> free;                    // нуусан гурвалжны байр
                for (int t = 0; t < mesh.triangleCount; ++t)
                {
                    unsigned short* tri = &mesh.indices[t * 3];
                    // 6: бэлхүүсний ясанд давамгай (гар түүнтэй шууд холбогдох ёсгүй; хөл, нуруу холбогдоно)
                    auto group = [&](int v) {
                        const int g = dominant(v);
                        if (g != 0 || hips < 0) return g;
                        float best = 0.0f;
                        int bone = -1;
                        for (int k = 0; k < 4; ++k)
                        {
                            if (mesh.boneWeights[v * 4 + k] > best)
                            {
                                best = mesh.boneWeights[v * 4 + k];
                                bone = mesh.boneIds[v * 4 + k];
                            }
                        }
                        return bone == hips ? 6 : 0;
                    };
                    const int g0 = group(tri[0]), g1 = group(tri[1]), g2 = group(tri[2]);
                    auto apart = [](int a, int b) {
                        if (a == 6 || b == 6) return (a == 6 ? b : a) == 1 || (a == 6 ? b : a) == 2; // гар ба бэлхүүс
                        return a != 0 && b != 0 && a != b;
                    };
                    // Гар ба их биеийн доод хэсэг (тохойноос доош: бүс, хормой): анхны зогсолтод гар нь хувцастай нийлсэн
                    // загварт (model-1) гараа өргөхөд гараас бүс хүртэл хавтан сунадаг. Мөр, суганы холбоос (тохойноос
                    // дээш) хэвээр.
                    auto lowTorso = [&](int a, int va, int b, int vb) {
                        if (a != 1 && a != 2) return false;
                        if (b != 0 && b != 6) return false;
                        const float elbow = elbowY[a == 1 ? 0 : 1];
                        return elbow > -1e8f && mesh.vertices[vb * 3 + 1] < elbow;
                    };
                    const int v0 = tri[0], v1 = tri[1], v2 = tri[2];
                    const bool waist = lowTorso(g0, v0, g1, v1) || lowTorso(g1, v1, g0, v0) || lowTorso(g1, v1, g2, v2) ||
                                       lowTorso(g2, v2, g1, v1) || lowTorso(g0, v0, g2, v2) || lowTorso(g2, v2, g0, v0);
                    if (apart(g0, g1) || apart(g1, g2) || apart(g0, g2) || waist)
                    {
                        bridged.push_back({ tri[0], tri[1], tri[2] });
                        tri[1] = tri[2] = tri[0];
                        free.push_back(t);
                        ++hidden;
                    }
                }
                // Нуусан гурвалжны оронд үлдсэн нүхийг (гар нь гуятай нийлсэн газар: гуяны гадаргуу дээр) бөглөнө:
                // нуусан гурвалжнуудын хөл / бэлхүүсний талын оройнуудаар хүрээлэгдсэн ирмэгийн гогцоо бүрийг
                // сэнс хэлбэрээр гурвалжилна (шинэ орой нэмэхгүй, нуусан гурвалжны байрыг ашиглана)
                int filled = 0;
                if (!bridged.empty())
                {
                    // байрлалаар нэгтгэсэн оройн дугаар (uv-ийн заагаар хуваагдсан оройнууд нэг цэг)
                    std::unordered_map<long long, int> weldKey;
                    std::vector<int> weldOf((size_t)mesh.vertexCount);
                    for (int v = 0; v < mesh.vertexCount; ++v)
                    {
                        const long long qx = std::llround(mesh.vertices[v * 3] * 4000.0f), qy = std::llround(mesh.vertices[v * 3 + 1] * 4000.0f),
                                        qz = std::llround(mesh.vertices[v * 3 + 2] * 4000.0f);
                        const long long key = (qx * 73856093LL) ^ (qy * 19349663LL) ^ (qz * 83492791LL);
                        weldOf[(size_t)v] = weldKey.emplace(key, v).first->second;
                    }
                    auto legSide = [&](int v) {
                        const int g = dominant(v);
                        if (g == 3) return true;
                        // (бүс, хормой: тохойноос доош их бие)
                        if (g == 0 && mesh.vertices[v * 3 + 1] < std::max(elbowY[0], elbowY[1])) return true;
                        if (g != 0 || hips < 0) return false;
                        int best = 0;
                        for (int k = 1; k < 4; ++k) if (mesh.boneWeights[v * 4 + k] > mesh.boneWeights[v * 4 + best]) best = k;
                        return mesh.boneIds[v * 4 + best] == hips;
                    };
                    std::vector<char> touched((size_t)mesh.vertexCount, 0);
                    for (const auto& b : bridged)
                    {
                        for (int k = 0; k < 3; ++k) if (legSide(b[k])) touched[(size_t)weldOf[(size_t)b[k]]] = 1;
                    }
                    // Ирмэг (нэгтгэсэн оройгоор) хэдэн гурвалжинд орсон: 1 бол хил
                    std::map<std::pair<int, int>, int> uses;
                    std::map<std::pair<int, int>, std::pair<int, int>> directed; // хилийн ирмэг: (a, b) анхны оройгоор, гурвалжны чиглэлээр
                    for (int t = 0; t < mesh.triangleCount; ++t)
                    {
                        const unsigned short* tri = &mesh.indices[t * 3];
                        if (tri[0] == tri[1]) continue;
                        for (int k = 0; k < 3; ++k)
                        {
                            const int a = tri[k], b = tri[(k + 1) % 3];
                            const int wa = weldOf[(size_t)a], wb = weldOf[(size_t)b];
                            const auto key = std::minmax(wa, wb);
                            ++uses[key];
                            directed[key] = { a, b };
                        }
                    }
                    // Хүрсэн оройнуудын хилийн ирмэгүүд: дараагийн орой руу (гурвалжны эсрэг чиглэлээр: бөглөх гурвалжин зөв тал руу харна)
                    std::unordered_map<int, int> next, original;
                    for (const auto& [key, count] : uses)
                    {
                        if (count != 1 || !touched[(size_t)key.first] || !touched[(size_t)key.second]) continue;
                        const auto [a, b] = directed[key];
                        next[weldOf[(size_t)b]] = weldOf[(size_t)a];
                        original[weldOf[(size_t)a]] = a;
                        original[weldOf[(size_t)b]] = b;
                    }
                    std::unordered_map<int, char> done;
                    size_t slot = 0;
                    for (const auto& [start, unused] : next)
                    {
                        if (done[start]) continue;
                        std::vector<int> loop;
                        int v = start;
                        while (!done[v] && next.count(v) && loop.size() < 400)
                        {
                            done[v] = 1;
                            loop.push_back(original[v]);
                            v = next[v];
                        }
                        if (v != start || loop.size() < 3) continue; // хаалттай гогцоо л
                        for (size_t k = 1; k + 1 < loop.size() && slot < free.size(); ++k)
                        {
                            unsigned short* tri = &mesh.indices[free[slot++] * 3];
                            tri[0] = (unsigned short)loop[0];
                            tri[1] = (unsigned short)loop[k];
                            tri[2] = (unsigned short)loop[k + 1];
                            ++filled;
                        }
                    }
                }
                if (hidden > 0)
                {
                    rlUpdateVertexBufferElements(mesh.vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_INDICES], mesh.indices,
                                                 mesh.triangleCount * 3 * (int)sizeof(unsigned short), 0);
                    TraceLog(LOG_INFO, "CHARACTER: %s: hid %d triangles bridging an arm and a leg, filled the holes with %d", name, hidden, filled);
                }
            }

            if (fixed > 0)
            {
                // LoadModel аль хэдийн видео карт руу хуулсан тул засварласан утгуудыг дахин илгээнэ.
                // Буферийн дугаар: raylib config.h-ийн ATTRIB_LOCATION_BONEIDS (7), BONEWEIGHTS (8).
                const int boneIdsBuffer = 7, boneWeightsBuffer = 8;
                UpdateMeshBuffer(mesh, boneIdsBuffer, mesh.boneIds, mesh.vertexCount * 4, 0);
                UpdateMeshBuffer(mesh, boneWeightsBuffer, mesh.boneWeights, mesh.vertexCount * 4 * (int)sizeof(float), 0);
                TraceLog(LOG_INFO, "CHARACTER: %s: fixed %d vertices weighted to distant bones", name, fixed);
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
            // Алга нь гадагш харсан анхны байрлалтай загвар (tools/fbx_hands.py "PalmsOut" ясаар тэмдэглэнэ): эсрэг тал
            if (FindBone(c.model, "PalmsOut") >= 0) normal = Vector3Negate(normal);
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
            // v' = position + R * s * (v - bind)   (s: сарвууны хэмжээ, бусад ясанд 1)
            const Quaternion rotation = c.rotation[(size_t)bone];
            const float s = armsPass && !c.handBone.empty() && c.handBone[(size_t)bone] ? c.handScale : 1.0f;
            Vector3 offset = Vector3Subtract(c.position[(size_t)bone], Vector3Scale(Rotate(rotation, c.bind[(size_t)bone]), s));
            Matrix matrix = QuaternionToMatrix(rotation);
            if (s != 1.0f)
            {
                matrix.m0 *= s; matrix.m1 *= s; matrix.m2 *= s;
                matrix.m4 *= s; matrix.m5 *= s; matrix.m6 *= s;
                matrix.m8 *= s; matrix.m9 *= s; matrix.m10 *= s;
            }
            const Vector3 axis = armsPass && c.girth != 1.0f && (size_t)bone < c.girthAxis.size() ? c.girthAxis[(size_t)bone] : Vector3{ 0.0f, 0.0f, 0.0f };
            if (Vector3LengthSqr(axis) > 0.5f)
            {
                // v' = position + R * G * (v - bind), G = I + (k - 1)(I - a aᵀ): ясны тэнхлэгт перпендикуляр томруулна
                const float k = c.girth - 1.0f;
                Vector3 column[3];
                for (int i = 0; i < 3; ++i)
                {
                    const Vector3 e = { i == 0 ? 1.0f : 0.0f, i == 1 ? 1.0f : 0.0f, i == 2 ? 1.0f : 0.0f };
                    const float along = i == 0 ? axis.x : i == 1 ? axis.y : axis.z;
                    column[i] = Rotate(rotation, Vector3Add(e, Vector3Scale(Vector3Subtract(e, Vector3Scale(axis, along)), k)));
                }
                matrix.m0 = column[0].x; matrix.m1 = column[0].y; matrix.m2 = column[0].z;
                matrix.m4 = column[1].x; matrix.m5 = column[1].y; matrix.m6 = column[1].z;
                matrix.m8 = column[2].x; matrix.m9 = column[2].y; matrix.m10 = column[2].z;
                const Vector3 b = c.bind[(size_t)bone];
                offset = Vector3Subtract(c.position[(size_t)bone], { matrix.m0 * b.x + matrix.m4 * b.y + matrix.m8 * b.z,
                                                                     matrix.m1 * b.x + matrix.m5 * b.y + matrix.m9 * b.z,
                                                                     matrix.m2 * b.x + matrix.m6 * b.y + matrix.m10 * b.z });
            }
            matrix.m12 = offset.x;
            matrix.m13 = offset.y;
            matrix.m14 = offset.z;

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

    // Клипийн нэрсийг дугаар болгож, алхааны хурд, урт тооцно.
    void SetupClips(Character& c)
    {
        const AnimSet& set = *c.anims;
        const int count = (int)set.bones.size();
        c.animBone.assign((size_t)count, -1);
        for (int i = 0; i < count; ++i) c.animBone[(size_t)i] = FindBone(c.model, set.bones[(size_t)i].c_str());
        const auto clipOf = [&](int bone) {
            for (int i = 0; i < count; ++i)
            {
                if (c.animBone[(size_t)i] == bone) return i;
            }
            return -1;
        };
        c.animParent.assign((size_t)count, -1);
        c.restFix.assign((size_t)count, QuaternionIdentity());
        for (int i = 0; i < count; ++i)
        {
            const int bone = c.animBone[(size_t)i];
            if (bone < 0) continue;
            const int parent = c.model.bones[bone].parent;
            c.animParent[(size_t)i] = parent >= 0 ? clipOf(parent) : -1;
            // Гол хүүхэд: цээж -> хүзүү, сарвуу -> долоовор, бусад нь эхний хүүхэд. Ташаа засваргүй.
            const std::string& name = set.bones[(size_t)i];
            if (name == "Hips") continue;
            const char* wanted = name == "Spine2" ? "Neck" : name == "LeftHand" ? "LeftHandIndex1" : name == "RightHand" ? "RightHandIndex1" : nullptr;
            int child = -1;
            for (int k = 0; k < count && child < 0; ++k)
            {
                const int other = c.animBone[(size_t)k];
                if (other < 0 || c.model.bones[other].parent != bone) continue;
                if (!wanted || set.bones[(size_t)k] == wanted) child = k;
            }
            if (child < 0)
            {
                // Төгсгөлийн яс: эцгийн засвартай ижил
                if (c.animParent[(size_t)i] >= 0) c.restFix[(size_t)i] = c.restFix[(size_t)c.animParent[(size_t)i]];
                continue;
            }
            const Vector3 own = Vector3Subtract(c.bind[(size_t)c.animBone[(size_t)child]], c.bind[(size_t)bone]);
            const Vector3 source = Vector3Subtract(set.rest[(size_t)child], set.rest[(size_t)i]);
            if (Vector3Length(own) < 1e-5f || Vector3Length(source) < 1e-5f) continue;
            c.restFix[(size_t)i] = QuaternionFromVector3ToVector3(Vector3Normalize(own), Vector3Normalize(source));
        }
        static const char* const spineNames[4] = { "Spine", "Spine1", "Spine2", "Neck" };
        for (int k = 0; k < 4; ++k)
        {
            c.clipSpine[k] = -1;
            for (int i = 0; i < count; ++i)
            {
                if (set.bones[(size_t)i] == spineNames[k]) c.clipSpine[k] = i;
            }
        }
        c.clipHead = -1;
        for (int i = 0; i < count; ++i)
        {
            if (set.bones[(size_t)i] == "Head") c.clipHead = i;
        }
        // Хэмжээ: ташаанаас шагай хүртэлх өндрийн харьцаа
        const int hipsClip = clipOf(c.joints[Hips]), footClip = clipOf(c.joints[LeftFoot]);
        c.clipHips = std::max(0, hipsClip);
        if (hipsClip >= 0 && footClip >= 0)
        {
            const float own = c.bind[(size_t)c.joints[Hips]].y - c.bind[(size_t)c.joints[LeftFoot]].y;
            const float source = set.rest[(size_t)hipsClip].y - set.rest[(size_t)footClip].y;
            if (source > 1e-4f) c.clipScale = own / source;
        }
        // Клипт байхгүй хуруу: долоовор хурууг дагана
        c.fingerCopy.clear();
        for (const char* side : { "Left", "Right" })
        {
            for (const char* finger : { "Middle", "Ring", "Pinky" })
            {
                int previousCopy = -1;
                for (int segment = 1; segment <= 4; ++segment)
                {
                    const std::string base = std::string(side) + "Hand";
                    const int bone = FindBone(c.model, (base + finger + std::to_string(segment)).c_str());
                    if (bone < 0 || clipOf(bone) >= 0) { previousCopy = -1; continue; }
                    int index = -1;
                    const std::string indexName = base + "Index" + std::to_string(segment);
                    for (int i = 0; i < count; ++i)
                    {
                        if (set.bones[(size_t)i] == indexName) index = i;
                    }
                    if (index < 0) continue;
                    Character::FingerCopy copy = { bone, index, clipOf(c.model.bones[bone].parent), previousCopy, QuaternionIdentity() };
                    if (copy.parentClip < 0 && copy.parentCopy < 0) continue;
                    const int next = FindBone(c.model, (base + finger + std::to_string(segment + 1)).c_str());
                    int nextIndex = -1;
                    const std::string nextName = base + "Index" + std::to_string(segment + 1);
                    for (int i = 0; i < count; ++i)
                    {
                        if (set.bones[(size_t)i] == nextName) nextIndex = i;
                    }
                    if (next >= 0 && nextIndex >= 0)
                    {
                        const Vector3 own = Vector3Subtract(c.bind[(size_t)next], c.bind[(size_t)bone]);
                        const Vector3 source = Vector3Subtract(set.rest[(size_t)nextIndex], set.rest[(size_t)index]);
                        if (Vector3Length(own) > 1e-5f && Vector3Length(source) > 1e-5f)
                        {
                            copy.fix = QuaternionFromVector3ToVector3(Vector3Normalize(own), Vector3Normalize(source));
                        }
                    }
                    else if (previousCopy >= 0)
                    {
                        copy.fix = c.fingerCopy[(size_t)previousCopy].fix;
                    }
                    c.fingerCopy.push_back(copy);
                    previousCopy = (int)c.fingerCopy.size() - 1;
                }
            }
        }
        c.idleClip[0] = c.anims->Find("idle");
        c.idleClip[1] = c.anims->Find("idle_aim");
        c.crouchIdleClip[0] = c.anims->Find("crouch_idle");
        c.crouchIdleClip[1] = c.anims->Find("crouch_aim");
        c.jumpClip = c.anims->Find("jump_loop");
        static const char* const gaits[4] = { "walk", "run", "sprint", "crouch" };
        static const char* const directions[8] = { "f", "fl", "l", "bl", "b", "br", "r", "fr" };
        bool complete = c.idleClip[0] >= 0 && c.idleClip[1] >= 0;
        for (int g = 0; g < 4; ++g)
        {
            for (int d = 0; d < 8; ++d)
            {
                c.gaitClip[g][d] = c.anims->Find(TextFormat("%s_%s", gaits[g], directions[d]));
                complete = complete && c.gaitClip[g][d] >= 0;
            }
            if (c.gaitClip[g][0] < 0) continue;
            const AnimClip& clip = c.anims->clips[(size_t)c.gaitClip[g][0]];
            c.gaitSpeed[g] = Vector3Length(clip.velocity) * c.clipScale * c.scale;
            c.gaitStride[g] = c.gaitSpeed[g] * clip.Duration();
        }
        if (!complete)
        {
            TraceLog(LOG_WARNING, "ANIM: idle or locomotion clips missing, using the procedural pose");
            c.anims = nullptr;
            return;
        }
        TraceLog(LOG_INFO, "ANIM: walk %.2f m/s stride %.2f m, run %.2f / %.2f, sprint %.2f / %.2f, crouch %.2f / %.2f",
                 c.gaitSpeed[0], c.gaitStride[0], c.gaitSpeed[1], c.gaitStride[1], c.gaitSpeed[2], c.gaitStride[2],
                 c.gaitSpeed[3], c.gaitStride[3]);
    }

    // Хурдаар алхах / гүйх / хурдан гүйхийг жинлэнэ (нийлбэр 1).
    void GaitWeights(const Character& c, float speed, float weights[3])
    {
        weights[0] = weights[1] = weights[2] = 0.0f;
        if (speed <= c.gaitSpeed[0])
        {
            weights[0] = 1.0f;
        }
        else if (speed <= c.gaitSpeed[1])
        {
            const float t = (speed - c.gaitSpeed[0]) / std::max(0.01f, c.gaitSpeed[1] - c.gaitSpeed[0]);
            weights[0] = 1.0f - t;
            weights[1] = t;
        }
        else
        {
            const float t = std::min(1.0f, (speed - c.gaitSpeed[1]) / std::max(0.01f, c.gaitSpeed[2] - c.gaitSpeed[1]));
            weights[1] = 1.0f - t;
            weights[2] = t;
        }
    }

    // Нэг алхааны (gait) 8 чиглэлээс явах зүгийн хоёр хөршийг жинлэн нэмнэ. cycle: алхааны үе 0..1.
    void AddGait(const Character& c, AnimBlend& blend, int gait, float direction, float cycle, float weight)
    {
        if (weight <= 0.0f) return;
        float sector = direction / (0.25f * PI); // + нь зүүн тийш (+X)
        sector -= 8.0f * std::floor(sector / 8.0f);
        const int a = (int)sector % 8, b = (a + 1) % 8;
        const float t = sector - std::floor(sector);
        for (int k = 0; k < 2; ++k)
        {
            const AnimClip& clip = c.anims->clips[(size_t)c.gaitClip[gait][k == 0 ? a : b]];
            float at = cycle + clip.phase;
            at -= std::floor(at);
            blend.Add(clip, at * clip.Duration(), weight * (k == 0 ? 1.0f - t : t));
        }
    }

    // Клипийн дээр нэмэх биеийн засвар (зэвсгээс хамаарна)
    struct ClipBody
    {
        float yaw = 0.0f;       // цээжний эргэлт, градус (+ = биеийн зүүн тийш)
        float lean = 0.0f;      // нэмэлт бөхийлт, градус (+ = урагш): онилгоо, ухралт, хатгах, шидэх
        float gazePitch = 0.0f; // харцны босоо өнцөг, градус (+ = дээш)
    };

    // Хөлийн гинжийг (тавхай, хуруу) хадгалсан эргэлтээр дахин байрлуулна.
    void RestoreLegChain(Character& c, int bone, const std::vector<Quaternion>& saved)
    {
        const int parent = c.model.bones[bone].parent;
        c.position[(size_t)bone] = Vector3Add(c.position[(size_t)parent], Rotate(c.rotation[(size_t)parent],
                                                                                 Vector3Subtract(c.bind[(size_t)bone], c.bind[(size_t)parent])));
        c.rotation[(size_t)bone] = saved[(size_t)bone];
        c.solved[(size_t)bone] = 1;
        for (int child = 0; child < c.model.boneCount; ++child)
        {
            if (c.model.bones[child].parent == bone) RestoreLegChain(c, child, saved);
        }
    }

    // Mixamo-гийн клипүүдээр бүх ясыг байрлуулна (их бие, хөл, толгой; гарыг дуудагч дараа нь зэвсгээр тавина).
    // move: 0 зогссон .. 1 явж буй; crouch, aim: 0..1; airborne: агаарт (үсрэлт, уналт).
    void PoseFromClips(Character& c, const HumanoidPose& pose, float move, float crouch, float aim, bool airborne, const ClipBody& body)
    {
        static AnimBlend blend;
        static std::vector<Quaternion> local;
        blend.Reset((int)c.anims->bones.size());
        const auto clip = [&](int index) -> const AnimClip& { return c.anims->clips[(size_t)index]; };
        if (airborne && c.jumpClip >= 0)
        {
            blend.Add(clip(c.jumpClip), pose.time, 1.0f);
        }
        else
        {
            float cycle = pose.walkPhase / (2.0f * PI);
            cycle -= std::floor(cycle);
            const float still = 1.0f - move;
            for (int k = 0; k < 2; ++k)
            {
                const float w = (k == 0 ? 1.0f - aim : aim) * still;
                blend.Add(clip(c.idleClip[k]), pose.time, w * (1.0f - crouch));
                blend.Add(clip(c.crouchIdleClip[k] >= 0 ? c.crouchIdleClip[k] : c.idleClip[k]), pose.time, w * crouch);
            }
            float weights[3];
            GaitWeights(c, pose.speed, weights);
            for (int g = 0; g < 3; ++g) AddGait(c, blend, g, pose.moveDirection, cycle, move * (1.0f - crouch) * weights[g]);
            AddGait(c, blend, 3, pose.moveDirection, cycle, move * crouch);
        }
        Vector3 hips;
        if (!blend.Result(local, hips)) return;
        // Ташаа: эх дүрийн анхны байрлалаас хэр шилжсэнийг энэ дүрийн хэмжээгээр
        const Vector3 hipsBind = c.bind[(size_t)c.joints[Hips]];
        hips = Vector3Add(hipsBind, Vector3Scale(Vector3Subtract(hips, c.anims->rest[(size_t)c.clipHips]), c.clipScale));
        hips.y -= pose.bob / c.scale; // (үндсэн хувиргалтын дээш доош хөдөлгөөнийг нөхнө: хөл газарт)
        // Явахад ташааг өргөнө: клипийн байлдааны алхаа намхан (анхны өндрийн 85%, өвдөг 90°). Тавхайг доор нь IK-аар буцаана.
        const float lift = airborne ? 0.0f : 0.06f * move * (1.0f - crouch) / c.scale;
        hips.y += lift;

        // Яс бүрийн эргэлт ба байрлал. source: эх дүрийн D; дүрийн эргэлт = source * restFix
        const auto place = [&](int bone, Quaternion rotation) {
            const int parent = c.model.bones[bone].parent;
            c.rotation[(size_t)bone] = rotation;
            if (parent < 0 || !c.solved[(size_t)parent])
            {
                c.position[(size_t)bone] = parent < 0 || bone == c.joints[Hips] ? hips : c.bind[(size_t)bone];
            }
            else
            {
                c.position[(size_t)bone] = Vector3Add(c.position[(size_t)parent],
                                                      Rotate(c.rotation[(size_t)parent],
                                                             Vector3Subtract(c.bind[(size_t)bone], c.bind[(size_t)parent])));
            }
            c.solved[(size_t)bone] = 1;
        };
        // Клипийн яс эцгээсээ хойно ирнэ (tools/mixamo_import.py): D(яс) = D(эцэг) * L
        static std::vector<Quaternion> source;
        source.resize(local.size());
        for (size_t i = 0; i < local.size(); ++i)
        {
            const int parentClip = c.animParent[i];
            source[i] = parentClip >= 0 ? QuaternionNormalize(QuaternionMultiply(source[(size_t)parentClip], local[i])) : local[i];
        }
        // Их бие: клипийн цээж зогсохдоо 50° эргэсэн, явахад 45° хүртэл бөгтөр, клип хооронд эргэлт нь их зөрүүтэй (-46..+3°).
        // Цээжийг нэг эргэлтэд (body.yaw), бөхийлтийг багасгаж (зэвсэг, онилгоо, ухралт нэмнэ) нуруунд тэгш хуваана.
        const int* spine = c.clipSpine;
        Quaternion spineStep = QuaternionIdentity();
        if (spine[0] >= 0 && spine[1] >= 0 && spine[2] >= 0 && spine[3] >= 0)
        {
            const Quaternion chest = source[(size_t)spine[2]];
            const Vector3 up = Vector3Subtract(c.anims->rest[(size_t)spine[3]], c.anims->rest[(size_t)spine[0]]);
            const Vector3 now = Rotate(chest, up);
            const float lean = std::atan2(now.z, now.y) - std::atan2(up.z, up.y); // + = урагш
            const Vector3 forward = Rotate(chest, { 0.0f, 0.0f, 1.0f });
            const float straighten = airborne ? 0.0f : move * Lerp(0.6f, 0.4f, crouch);
            const float leanFix = -std::max(lean, 0.0f) * straighten + body.lean * DEG2RAD;
            const float yawFix = std::remainder(body.yaw * DEG2RAD - std::atan2(forward.x, forward.z), 2.0f * PI);
            const Quaternion total = QuaternionMultiply(QuaternionFromAxisAngle({ 0.0f, 1.0f, 0.0f }, yawFix),
                                                        QuaternionFromAxisAngle({ 1.0f, 0.0f, 0.0f }, leanFix));
            spineStep = QuaternionSlerp(QuaternionIdentity(), total, 1.0f / 3.0f);
        }
        // Толгой: клипт 10-23° доош харсан. Харц урагш, онилгооны зүгт (хүзүү, толгойд хуваана)
        const Vector3 gaze = Rotate(QuaternionFromAxisAngle({ 1.0f, 0.0f, 0.0f }, -body.gazePitch * DEG2RAD), { 0.0f, 0.0f, 1.0f });
        const Vector3 zAxis = { 0.0f, 0.0f, 1.0f };
        for (size_t i = 0; i < local.size(); ++i)
        {
            const int parentClip = c.animParent[i];
            source[i] = parentClip >= 0 ? QuaternionNormalize(QuaternionMultiply(source[(size_t)parentClip], local[i])) : local[i];
            const int index = (int)i;
            if (index == spine[0] || index == spine[1] || index == spine[2])
            {
                source[i] = QuaternionNormalize(QuaternionMultiply(spineStep, source[i]));
            }
            else if (index == spine[3] && c.clipHead >= 0)
            {
                const Vector3 now = Rotate(QuaternionMultiply(source[i], local[(size_t)c.clipHead]), zAxis);
                const Quaternion half = QuaternionSlerp(QuaternionIdentity(), QuaternionFromVector3ToVector3(now, gaze), 0.5f);
                source[i] = QuaternionNormalize(QuaternionMultiply(half, source[i]));
            }
            else if (index == c.clipHead)
            {
                const Quaternion most = QuaternionSlerp(QuaternionIdentity(), QuaternionFromVector3ToVector3(Rotate(source[i], zAxis), gaze), 0.9f);
                source[i] = QuaternionNormalize(QuaternionMultiply(most, source[i]));
            }
            if (c.animBone[i] >= 0) place(c.animBone[i], QuaternionNormalize(QuaternionMultiply(source[i], c.restFix[i])));
        }
        static std::vector<Quaternion> copied;
        copied.resize(c.fingerCopy.size());
        for (size_t i = 0; i < c.fingerCopy.size(); ++i)
        {
            const Character::FingerCopy& f = c.fingerCopy[i];
            const Quaternion parent = f.parentClip >= 0 ? source[(size_t)f.parentClip] : copied[(size_t)f.parentCopy];
            copied[i] = QuaternionNormalize(QuaternionMultiply(parent, local[(size_t)f.clip]));
            place(f.bone, QuaternionNormalize(QuaternionMultiply(copied[i], f.fix)));
        }
        // Хөл: тавхай клипийн байрлалдаа, газраас доош орохгүй (клипт гүйхэд 3.5 см орно); эргэлт нь клипийнх
        if (!airborne && move > 0.01f)
        {
            static std::vector<Quaternion> saved;
            saved = c.rotation;
            for (int side = 0; side < 2; ++side)
            {
                const Joint upper = side == 0 ? RightUpLeg : LeftUpLeg, lower = side == 0 ? RightLeg : LeftLeg, foot = side == 0 ? RightFoot : LeftFoot;
                const int footBone = c.joints[foot];
                Vector3 ankle = c.position[(size_t)footBone];
                ankle.y = std::max(ankle.y - lift, c.bind[(size_t)footBone].y);
                const Vector3 middle = Vector3Scale(Vector3Add(c.position[(size_t)c.joints[upper]], ankle), 0.5f);
                Vector3 pole = Vector3Subtract(c.position[(size_t)c.joints[lower]], middle);
                if (Vector3Length(pole) < 0.005f) pole = Rotate(c.rotation[(size_t)c.joints[Hips]], { 0.0f, 0.0f, 1.0f });
                Unsolve(c, upper);
                Reach(c, upper, lower, foot, ankle, pole, 0.0f);
                Follow(c, c.joints[lower]);
                RestoreLegChain(c, footBone, saved);
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

void UploadBoneRows(Shader shader, const Matrix* bones, int count)
{
    if (!bones || count <= 0) return;
    static std::vector<Vector4> rows;
    static unsigned int cachedShader = 0;
    static int location = -1;
    if (shader.id != cachedShader)
    {
        cachedShader = shader.id;
        location = GetShaderLocation(shader, "boneRows");
    }
    if (location < 0) return;
    count = std::min(count, 72);
    rows.resize((size_t)count * 3);
    for (int i = 0; i < count; ++i)
    {
        const Matrix& m = bones[i];
        rows[(size_t)i * 3 + 0] = { m.m0, m.m4, m.m8, m.m12 };
        rows[(size_t)i * 3 + 1] = { m.m1, m.m5, m.m9, m.m13 };
        rows[(size_t)i * 3 + 2] = { m.m2, m.m6, m.m10, m.m14 };
    }
    SetShaderValueV(shader, location, rows.data(), SHADER_UNIFORM_VEC4, count * 3);
}

namespace
{
    // bone нь root эсвэл түүний удам мөн эсэх
    bool UnderBone(const Model& model, int bone, int root)
    {
        for (int b = bone; b >= 0; b = model.bones[b].parent)
        {
            if (b == root) return true;
        }
        return false;
    }

    // Гурвалжны гурван орой бүгд гарын (мөрнөөс доош) ясанд голлон жинлэгдсэн бол үлдээж, шинэ mesh үүсгэнэ.
    // Ясны матрицыг үндсэн mesh-тэй хуваалцана (Skin нэг удаа бичнэ).
    // glove >= 0: тэр ясны (зүүн сарвуу) доорх гурвалжнуудыг part 0-оос хасаж, part 1-д зөвхөн тэднийг авна.
    Mesh ExtractArms(const Model& model, const Mesh& source, int rightArm, int leftArm, int glove = -1, int part = 0, bool armWeightsOnly = false, int rightHand = -1, int leftHand = -1, int handFilter = 0)
    {
        Mesh arms = {};
        if (!source.boneIds || !source.boneWeights || source.vertexCount <= 0) return arms;
        auto weightUnder = [&](int v, int root) {
            float weight = 0.0f;
            for (int k = 0; k < 4; ++k)
            {
                const int bone = source.boneIds[v * 4 + k];
                if (root >= 0 && bone < model.boneCount && UnderBone(model, bone, root)) weight += source.boneWeights[v * 4 + k];
            }
            return weight;
        };
        auto armVertex = [&](int v) { return weightUnder(v, rightArm) + weightUnder(v, leftArm) > 0.6f; };
        auto gloveVertex = [&](int v) { return glove >= 0 && weightUnder(v, glove) > 0.5f; };
        std::vector<int> remap((size_t)source.vertexCount, -1);
        std::vector<int> used;
        std::vector<unsigned short> indices;
        for (int t = 0; t < source.triangleCount; ++t)
        {
            int corner[3];
            bool keep = true, hand = glove >= 0, palm = true, touchesHand = false;
            for (int k = 0; k < 3; ++k)
            {
                corner[k] = source.indices ? source.indices[t * 3 + k] : t * 3 + k;
                keep = keep && armVertex(corner[k]);
                hand = hand && gloveVertex(corner[k]);
                const float handWeight = weightUnder(corner[k], rightHand) + weightUnder(corner[k], leftHand);
                palm = palm && handWeight > 0.5f;
                touchesHand = touchesHand || handWeight > 0.3f;
            }
            // 1: сарвуугүй (сарвуунд мэдэгдэхүйц жинтэй хэсэг бугуйгаас салж хөвнө), 2: зөвхөн сарвуу
            if ((handFilter == 1 && touchesHand) || (handFilter == 2 && !palm)) continue;
            if (!keep || (glove >= 0 && hand != (part == 1))) continue;
            for (int k = 0; k < 3; ++k)
            {
                int& slot = remap[(size_t)corner[k]];
                if (slot < 0)
                {
                    slot = (int)used.size();
                    used.push_back(corner[k]);
                }
                indices.push_back((unsigned short)slot);
            }
        }
        const int vertices = (int)used.size();
        if (indices.empty() || vertices >= 65535) return arms;
        arms.vertexCount = vertices;
        arms.triangleCount = (int)indices.size() / 3;
        arms.vertices = (float*)MemAlloc((unsigned int)vertices * 3 * sizeof(float));
        if (source.texcoords) arms.texcoords = (float*)MemAlloc((unsigned int)vertices * 2 * sizeof(float));
        if (source.normals) arms.normals = (float*)MemAlloc((unsigned int)vertices * 3 * sizeof(float));
        arms.boneIds = (unsigned char*)MemAlloc((unsigned int)vertices * 4);
        arms.boneWeights = (float*)MemAlloc((unsigned int)vertices * 4 * sizeof(float));
        for (int i = 0; i < vertices; ++i)
        {
            const int v = used[(size_t)i];
            std::memcpy(arms.vertices + i * 3, source.vertices + v * 3, 3 * sizeof(float));
            if (arms.texcoords) std::memcpy(arms.texcoords + i * 2, source.texcoords + v * 2, 2 * sizeof(float));
            if (arms.normals) std::memcpy(arms.normals + i * 3, source.normals + v * 3, 3 * sizeof(float));
            std::memcpy(arms.boneIds + i * 4, source.boneIds + v * 4, 4);
            std::memcpy(arms.boneWeights + i * 4, source.boneWeights + v * 4, 4 * sizeof(float));
            if (armWeightsOnly)
            {
                // Биеийн ясны жинг хасаж гарынхыг хэвийн болгоно: бие хөдөлгөөнгүй тул ханцуй, хантааз сунаж шовх болно
                float total = 0.0f;
                for (int k = 0; k < 4; ++k)
                {
                    const int bone = arms.boneIds[i * 4 + k];
                    const bool arm = bone < model.boneCount && (UnderBone(model, bone, rightArm) || UnderBone(model, bone, leftArm));
                    if (!arm) arms.boneWeights[i * 4 + k] = 0.0f;
                    total += arms.boneWeights[i * 4 + k];
                }
                for (int k = 0; k < 4 && total > 0.0f; ++k) arms.boneWeights[i * 4 + k] /= total;
            }
        }
        arms.indices = (unsigned short*)MemAlloc((unsigned int)indices.size() * sizeof(unsigned short));
        std::memcpy(arms.indices, indices.data(), indices.size() * sizeof(unsigned short));
        arms.boneCount = source.boneCount;
        arms.boneMatrices = source.boneMatrices;
        UploadMesh(&arms, false);
        return arms;
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
            texturePath = ImageFile(folder + kNames[id] + "_color.png"); // (хөтөч: .jpg)
            if (FileExists(meshPath.c_str()) && FileExists(texturePath.c_str()))
            {
                break;
            }
        }
#if defined(__ANDROID__)
        // APK-ийн assets-ийн үндсэнд байрлана (FileExists нь assets-ийг хардаггүй тул шууд ачаална).
        meshPath = std::string(kNames[id]) + "_mesh.glb";
        texturePath = ImageFile(std::string(kNames[id]) + "_color.png");
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
        CleanStrayWeights(c.model, kNames[id]);
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
        c.scale = top > 0.01f ? TargetHeight(id) / top : 1.0f;
        if ((WantsArms(id) || WantsClipArms(id)) && c.joints[RightArm] >= 0 && c.joints[RightHand] >= 0)
        {
            // 1-р этгээдийн гар: загвар нь бүтэн хүн биш байж болно (зөвхөн гар, хөл): өндрөөр биш, мөрнөөс бугуй
            // хүртэлх уртаар (0.55 м) томруулна (бүтэн биетэй дүрүүд өндрөөрөө аль хэдийн бодит хэмжээтэй)
            const float arm = Vector3Distance(c.bind[(size_t)c.joints[RightArm]], c.bind[(size_t)c.joints[RightHand]]);
            if (id == kCharacterFpArms && arm > 0.001f) c.scale = 0.55f / arm;
            // Сарвуу (бугуйнаас дунд хурууны үзүүр) бодит хүнд гарын уртын ~35% (19 / 55 см). Загварынх 61% байсан
            // (сарвуу 1.75 дахин том: бууны дэргэд хэт том харагдана) тул бугуйг тойрон жижигрүүлнэ.
            int tip = FindBone(c.model, "RightHandMiddle4");
            if (tip < 0) tip = FindBone(c.model, "RightHandMiddle3");
            if (tip >= 0 && arm > 0.001f)
            {
                const float hand = Vector3Distance(c.bind[(size_t)c.joints[RightHand]], c.bind[(size_t)tip]);
                c.handScale = std::clamp(0.35f * arm / std::max(hand, 0.001f), 0.4f, 1.0f);
                if (id == kCharacterIron) c.handScale = 1.0f; // (хуягт бээлий: жижигрүүлбэл төмөр хэсгүүд салж хөвнө)
                c.handBone.assign((size_t)c.model.boneCount, 0);
                for (int bone = 0; bone < c.model.boneCount; ++bone)
                {
                    for (int b = bone; b >= 0; b = c.model.bones[b].parent)
                    {
                        if (b == c.joints[RightHand] || b == c.joints[LeftHand]) { c.handBone[(size_t)bone] = 1; break; }
                    }
                }
                TraceLog(LOG_INFO, "CHARACTER: %s hand %.0f%% of arm length: scaled by %.2f", kNames[id], 100.0f * hand / arm, c.handScale);
            }
            {
                // Буутай камерын гар бүдүүн, булчинлаг: "Realistic FPS Hands"-ийн гар туранхай (шуу ~6 см) тул илүү,
                // бусад дүрийн өөрийн гар (ханцуйтай) бага зэрэг

                c.girth = id == kCharacterFpArms ? 2.1f : 1.3f;
                if (id == kCharacterFpArms) c.handScale = 1.15f; // (CS-ийн бээлийтэй гар шиг бага зэрэг том сарвуу)
                c.girthAxis.assign((size_t)c.model.boneCount, Vector3{ 0.0f, 0.0f, 0.0f });
                for (int bone = 0; bone < c.model.boneCount; ++bone)
                {
                    std::string name = c.model.bones[bone].name;
                    const size_t colon = name.rfind(':');
                    if (colon != std::string::npos) name = name.substr(colon + 1);
                    const bool limb = name == "RightArm" || name == "LeftArm" || name.rfind("RightForeArm", 0) == 0 || name.rfind("LeftForeArm", 0) == 0;
                    if (!limb) continue;
                    for (int child = 0; child < c.model.boneCount; ++child)
                    {
                        if (c.model.bones[child].parent != bone) continue;
                        const Vector3 d = Vector3Subtract(c.bind[(size_t)child], c.bind[(size_t)bone]);
                        if (Vector3Length(d) > 1e-5f) c.girthAxis[(size_t)bone] = Vector3Normalize(d);
                        break;
                    }
                }
            }
        }
        // Холын дүрд: ~4.5 см нүдээр бүлэглэсэн хөнгөн хувилбар
        int lodTriangles = 0;
        for (int index = 0; index < c.model.meshCount; ++index)
        {
            c.lod.push_back(SimplifyMesh(c.model.meshes[index], std::max(0.001f, top) / 30.0f));
            lodTriangles += c.lod.back().triangleCount;
            c.lodFar.push_back(SimplifyMesh(c.model.meshes[index], std::max(0.001f, top) / 12.0f, 1.0f / 12.0f));
        }
        int farTriangles = 0;
        for (const Mesh& mesh : c.lodFar) farTriangles += mesh.triangleCount;
        if (WantsArms(id))
        {
            // 1-р этгээдийн гар: зөвхөн гарын хэсгийг тусгаарлана
            int armTriangles = 0;
            for (int index = 0; index < c.model.meshCount; ++index)
            {
                // Бүтэн биетэй дүр (нүцгэн, хуягтай): зөвхөн шуу, сарвуу (тохойноос доош). Бугалга, мөр 1-р этгээдэд
                // харагддаггүй; хуягтай дүрийн мөрөвч дэлгэцийн захад хөвж харагддаг байв. fp_arms бүтнээрээ (ханцуйтай).
                // (нүцгэн биетэй model2: шуу тохойн дээр тасарч, урт бууны зүүн гарт тасархай нь дэлгэцэнд харагддаг байв;
                // ханцуйтай дүрүүд шуугаараа: бугалгын ханцуй, хуяг дэлгэцэнд хөвдөг)
                const bool full = id == kCharacterFpArms || id == kCharacterModel2;
                // Зүүн сарвуу: бээлий (тусдаа mesh, бээлийн өнгөөр)
                const int right = c.joints[full ? RightArm : RightForeArm], left = c.joints[full ? LeftArm : LeftForeArm];
                c.arms.push_back(ExtractArms(c.model, c.model.meshes[index], right, left, c.joints[LeftHand], 0));
                c.glove.push_back(ExtractArms(c.model, c.model.meshes[index], right, left, c.joints[LeftHand], 1));
                armTriangles += c.arms.back().triangleCount + c.glove.back().triangleCount;
            }
            TraceLog(LOG_INFO, "CHARACTER: %s first-person arms %d triangles", kNames[id], armTriangles);
        }
        TraceLog(LOG_INFO, "CHARACTER: %s low-detail version %d triangles, far %d", kNames[id], lodTriangles, farTriangles);
        {
            // Алхааны өргөн загвар бүрд өөр: ташаа өргөн, өмд бүдүүн (анхны зогсолтод өвдөг ташаанаас илүү
            // гадагш гарсан) загвар өргөн гишгэнэ, эс бөгөөс гуя, өвдөг хоорондоо шүргэлцэнэ.
            const float hip = std::fabs(c.bind[(size_t)c.joints[LeftUpLeg]].x) * c.scale;
            const float knee = std::fabs(c.bind[(size_t)c.joints[LeftLeg]].x) * c.scale;
            // Хүрээ: хөлөө алцайлгаж зогссон (demon) загвар хэт өргөн гишгэж алцгандаг, хуяг өмссөн (iron) нь хэт
            // нарийн гишгэж гуя нь шүргэлцдэг байсан (бусад загвар 0.11-0.15 м).
            c.stepWidth = std::clamp(hip * 0.5f + std::max(0.0f, knee - hip) * 1.6f, 0.10f, 0.15f);
        }

        c.texture = LoadTexture(texturePath.c_str());
        SetTextureFilter(c.texture, TEXTURE_FILTER_BILINEAR);
        c.material = LoadMaterialDefault();
        c.material.shader = shader;
        c.material.maps[MATERIAL_MAP_ALBEDO].texture = c.texture;
        c.loaded = true;

        TraceLog(LOG_INFO, "CHARACTER: %s loaded: %d triangles, %d bones, scale %.2f", kNames[id], triangles,
                 c.model.boneCount, c.scale);

        // 1-р этгээдийн гар (урт буу): тоглогчийн удирдаж болох дүр бүрийн өөрийн гар, ханцуй
        if (WantsClipArms(id))
        {
            for (int index = 0; index < c.model.meshCount; ++index)
            {
                // Iron-ы сарвуу шовх хэлтэрхий хэлбэртэй, атгаж чадахгүй: сарвуугүй (хүрэл бээлийн сарвуу, DrawCharacterClipArms)
                c.clipArms.push_back(ExtractArms(c.model, c.model.meshes[index], c.joints[RightArm], c.joints[LeftArm], -1, 0, true,
                                                 c.joints[RightHand], c.joints[LeftHand], id == kCharacterIron ? 1 : 0));
                if (id == kCharacterFpArms)
                {
                    c.clipHands.push_back(ExtractArms(c.model, c.model.meshes[index], c.joints[RightArm], c.joints[LeftArm], -1, 0, true,
                                                      c.joints[RightHand], c.joints[LeftHand], 2));
                }
            }
        }
        // Цэргүүд, сонгож болох дүрүүд Mixamo-гийн нэг хөдөлгөөнийг хуваалцана (файл байхгүй бол кодоор тооцсон хөдөлгөөн).
        // Demon, Iron үгүй: биеийн хэлбэр нь хөдөлгөөнийг даахгүй.
        if (id == kCharacterFighter1 || id == kCharacterFighter2 || id == kCharacterSniper || id == kCharacterModel1 ||
            id == kCharacterModel2 || id == kCharacterModel3)
        {
            static AnimSet soldierClips;
            static int soldierClipsState = 0; // 0 уншаагүй, 1 бэлэн, -1 алга
            if (soldierClipsState == 0)
            {
                const std::string animPath = meshPath.substr(0, meshPath.size() - std::strlen(kNames[id]) - std::strlen("_mesh.glb")) + "soldier_anims.bin";
#if defined(__ANDROID__)
                const bool exists = true; // (APK-ийн assets-ийг FileExists хардаггүй)
#else
                const bool exists = FileExists(animPath.c_str());
#endif
                soldierClipsState = exists && LoadAnimSet(animPath.c_str(), soldierClips) ? 1 : -1;
            }
            if (soldierClipsState > 0)
            {
                c.anims = &soldierClips;
                SetupClips(c);
            }
        }
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
            for (std::vector<Mesh>* list : { &c.lod, &c.lodFar, &c.arms, &c.glove, &c.clipArms, &c.clipHands })
            {
                for (Mesh& mesh : *list)
                {
                    mesh.boneMatrices = nullptr; // үндсэн mesh-ийнх
                    if (mesh.vertexCount > 0) UnloadMesh(mesh);
                }
            }
            SetCharacterOutfit((int)(&c - characters), {});
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

namespace
{
    const Material* shadowMaterial = nullptr;
}

void SetCharacterShadowMaterial(const Material* material) { shadowMaterial = material; }

namespace
{
    void UnloadWears(std::vector<Character::Wear>& list)
    {
        for (Character::Wear& w : list)
        {
            for (std::vector<Mesh>* lods : { &w.lod, &w.lodFar })
            {
                for (Mesh& mesh : *lods)
                {
                    mesh.boneMatrices = nullptr;
                    if (mesh.vertexCount > 0) UnloadMesh(mesh);
                }
            }
            for (int i = 0; i < w.model.meshCount; ++i) w.model.meshes[i].boneMatrices = nullptr; // (биеийнх эсвэл анхны)
            UnloadModel(w.model);
            w.material.shader = {}; // (Environment-ийнх)
            w.material.maps[MATERIAL_MAP_ALBEDO].texture = {};
            UnloadMaterial(w.material);
            if (w.ownsTexture) UnloadTexture(w.texture);
        }
        list.clear();
    }

    // Барааг ачаалж list-д нэмнэ. bones: биеийн ясны матрицууд; hidden: дор нь далдлагдах биеийн гурвалжнуудыг тэмдэглэнэ.
    bool LoadWear(Character& c, int id, const std::string& item, std::vector<Character::Wear>& list, Matrix* bones,
                  bool lods, std::vector<char>* hidden)
    {
        std::string base;
        const std::string application = GetApplicationDirectory();
        for (const std::string& folder : { std::string("assets/items/"), application + "assets/items/", application + "../assets/items/" })
        {
            base = folder + item;
            if (FileExists((base + "_mesh.glb").c_str())) break;
        }
#if defined(__ANDROID__)
        base = "items/" + item;
#endif
        Character::Wear w;
        w.name = item;
        w.model = LoadModel((base + "_mesh.glb").c_str());
        if (w.model.meshCount <= 0 || w.model.boneCount != c.model.boneCount)
        {
            TraceLog(LOG_WARNING, "OUTFIT: %s does not fit %s (%d bones, body %d)", item.c_str(), kNames[id], w.model.boneCount, c.model.boneCount);
            UnloadModel(w.model);
            return false;
        }
        // Өнгө: <бараа>_color.png, эсвэл иж бүрдлийнх (set1_helmet -> set1_color.png)
        w.texturePath = ImageFile(base + "_color.png");
        const size_t cut = base.rfind('_');
        if (!FileExists(w.texturePath.c_str()) && cut != std::string::npos && cut > base.rfind('/') + 1)
        {
            w.texturePath = ImageFile(base.substr(0, cut) + "_color.png");
        }
        for (const Character::Wear& other : list)
        {
            if (other.texturePath != w.texturePath) continue;
            w.texture = other.texture;
            w.ownsTexture = false;
            break;
        }
        if (w.ownsTexture)
        {
            w.texture = LoadTexture(w.texturePath.c_str());
            SetTextureFilter(w.texture, TEXTURE_FILTER_BILINEAR);
        }
        w.material = LoadMaterialDefault();
        w.material.shader = c.material.shader;
        w.material.maps[MATERIAL_MAP_ALBEDO].texture = w.texture;
        for (int i = 0; i < w.model.meshCount; ++i)
        {
            Mesh& mesh = w.model.meshes[i];
            if (mesh.boneMatrices) MemFree(mesh.boneMatrices);
            mesh.boneMatrices = bones;
            if (!lods) continue;
            const float height = TargetHeight(id) / std::max(0.001f, c.scale); // (биеийн өндөр, загварын нэгжээр)
            w.lod.push_back(SimplifyMesh(mesh, height / 30.0f));
            w.lodFar.push_back(SimplifyMesh(mesh, height / 12.0f, 1.0f / 12.0f));
        }
        // Далдлагдах биеийн гурвалжнууд
        int size = 0;
        unsigned char* data = hidden ? LoadFileData((base + "_hide.bin").c_str(), &size) : nullptr;
        if (data)
        {
            uint32_t count = 0;
            if (size >= 4) std::memcpy(&count, data, 4);
            for (uint32_t k = 0; k < count && 4 + (int)(k + 1) * 4 <= size; ++k)
            {
                uint32_t triangle = 0;
                std::memcpy(&triangle, data + 4 + k * 4, 4);
                if (triangle < hidden->size()) (*hidden)[triangle] = 1;
            }
            UnloadFileData(data);
        }
        list.push_back(std::move(w));
        return true;
    }
}

void SetCharacterOutfit(int id, const std::vector<std::string>& items)
{
    if (id < 0 || id >= kCharacterCount || !characters[id].loaded) return;
    Character& c = characters[id];
    UnloadWears(c.wear);
    if (c.bodyIndices.empty())
    {
        for (int i = 0; i < c.model.meshCount; ++i)
        {
            const Mesh& mesh = c.model.meshes[i];
            c.bodyIndices.emplace_back(mesh.indices, mesh.indices ? mesh.indices + mesh.triangleCount * 3 : mesh.indices);
        }
    }

    std::vector<char> hidden(c.bodyIndices.empty() ? 0 : c.bodyIndices[0].size() / 3, 0);
    for (const std::string& item : items)
    {
        if (!LoadWear(c, id, item, c.wear, c.model.meshes[0].boneMatrices, true, &hidden)) continue; // биеийн арьслалтаар хөдөлнө
        TraceLog(LOG_INFO, "OUTFIT: %s wears %s (%d triangles)", kNames[id], item.c_str(), c.wear.back().model.meshes[0].triangleCount);
    }

    // Биеийн индексийг дахин бичнэ: харагдах гурвалжнууд эхэнд, triangleCount-оор таслана
    if (!c.bodyIndices.empty() && c.model.meshCount > 0 && c.model.meshes[0].indices)
    {
        Mesh& mesh = c.model.meshes[0];
        const std::vector<unsigned short>& all = c.bodyIndices[0];
        int kept = 0;
        for (size_t t = 0; t < all.size() / 3; ++t)
        {
            if (hidden[t]) continue;
            std::memcpy(mesh.indices + kept * 3, &all[t * 3], 3 * sizeof(unsigned short));
            ++kept;
        }
        mesh.triangleCount = kept;
        UpdateMeshBuffer(mesh, 6, mesh.indices, kept * 3 * (int)sizeof(unsigned short), 0);
    }
}

float CharacterGaitRate(int id, float speed, float crouch)
{
    if (id < 0 || id >= kCharacterCount || !characters[id].loaded || !characters[id].anims)
    {
        return HumanoidGaitRate(speed);
    }
    if (speed <= 0.1f) return 0.0f;
    const Character& c = characters[id];
    float weights[3];
    GaitWeights(c, speed, weights);
    const float standing = weights[0] * c.gaitStride[0] + weights[1] * c.gaitStride[1] + weights[2] * c.gaitStride[2];
    const float stride = Lerp(standing, c.gaitStride[3], std::clamp(crouch, 0.0f, 1.0f));
    return 2.0f * PI * speed / std::max(0.2f, stride);
}

bool HasCharacterModel(int id)
{
    return id >= 0 && id < kCharacterCount && characters[id].loaded;
}

bool HasCharacterArms(int id)
{
    if (!HasCharacterModel(id)) return false;
    for (const Mesh& mesh : characters[id].arms)
    {
        if (mesh.vertexCount > 0) return true;
    }
    return false;
}

Vector3 CharacterEye(int id)
{
    if (!HasCharacterModel(id)) return { 0.0f, 1.62f, 0.08f };
    const Character& c = characters[id];
    const Vector3 neck = Vector3Scale(c.bind[(size_t)c.joints[Neck]], c.scale);
    return { 0.0f, neck.y + 0.1f, neck.z + 0.09f };
}

Vector3 CharacterRightShoulder(int id)
{
    if (!HasCharacterModel(id)) return { -0.18f, 1.45f, 0.0f };
    const Character& c = characters[id];
    return Vector3Scale(c.bind[(size_t)c.joints[RightArm]], c.scale);
}

void DrawCharacterArms(int id, const Matrix& bodyToWorld, const ViewArmTarget targets[2], Color tint)
{
    if (!HasCharacterArms(id)) return;
    Character& c = characters[id];
    const float s = c.scale;
    armsPass = true;
    std::fill(c.solved.begin(), c.solved.end(), 0); // (3-р этгээдийн тооцооноос үлдсэн байрлал жижигрүүлэлтгүй)
    const Matrix worldToBody = MatrixInvert(bodyToWorld);
    auto point = [&](Vector3 p) { return Vector3Scale(Vector3Transform(p, worldToBody), 1.0f / s); }; // -> загварын нэгж
    auto direction = [&](Vector3 d) {
        return Vector3Normalize(Vector3Subtract(Vector3Transform(d, worldToBody), Vector3Transform({ 0.0f, 0.0f, 0.0f }, worldToBody)));
    };

    // Анхны (bind) байрлалаас эхэлнэ: бие хөдөлгөөнгүй, зөвхөн гар IK-аар
    std::fill(c.solved.begin(), c.solved.end(), 0);
    for (int bone = 0; bone < c.model.boneCount; ++bone) Follow(c, bone);

    // Бүрэн чиглэлтэй зорилт (framed): сарвууг яг тэр чиглэлд эргүүлж, алганы төвөөс бугуйн цэгийг тооцно
    Quaternion orient[2] = { QuaternionIdentity(), QuaternionIdentity() };
    Vector3 wristTarget[2] = {}, palmFacing[2] = {};
    bool framed[2] = { false, false };
    Vector3 shift = { 0.0f, 0.0f, 0.0f };
    for (int side = 0; side < 2; ++side)
    {
        const ViewArmTarget& target = targets[side];
        const int middle = c.fingers[side][1][0];
        if (!target.active || !target.framed || middle < 0) continue;
        framed[side] = true;
        const Joint upper = side == 0 ? RightArm : LeftArm, lower = side == 0 ? RightForeArm : LeftForeArm;
        const Joint hand = side == 0 ? RightHand : LeftHand;
        // Сарвууны анхны чиглэл: бугуйнаас дунд хурууны үе мөч рүү (f), алганы зүг (n)
        const Vector3 wrist = c.bind[(size_t)c.joints[hand]];
        const Vector3 knuckle = c.bind[(size_t)middle];
        const Vector3 f = Vector3Normalize(Vector3Subtract(knuckle, wrist));
        Vector3 n = Vector3Subtract(c.palmNormal[side], Vector3Scale(f, Vector3DotProduct(c.palmNormal[side], f)));
        n = Vector3Normalize(n);
        // Хүссэн чиглэл (загварын орон зайд)
        const Vector3 tf = direction(target.fingers);
        Vector3 tn = direction(target.facing);
        tn = Vector3Subtract(tn, Vector3Scale(tf, Vector3DotProduct(tn, tf)));
        tn = Vector3Length(tn) > 0.01f ? Vector3Normalize(tn) : n;
        palmFacing[side] = tn;
        // (f, n, f x n) -> (tf, tn, tf x tn) эргэлт
        const Vector3 b = Vector3CrossProduct(f, n), tb = Vector3CrossProduct(tf, tn);
        Matrix from = MatrixIdentity(), to = MatrixIdentity();
        from.m0 = f.x; from.m1 = f.y; from.m2 = f.z; from.m4 = n.x; from.m5 = n.y; from.m6 = n.z; from.m8 = b.x; from.m9 = b.y; from.m10 = b.z;
        to.m0 = tf.x; to.m1 = tf.y; to.m2 = tf.z; to.m4 = tn.x; to.m5 = tn.y; to.m6 = tn.z; to.m8 = tb.x; to.m9 = tb.y; to.m10 = tb.z;
        orient[side] = QuaternionNormalize(QuaternionFromMatrix(MatrixMultiply(MatrixTranspose(from), to)));
        // Алганы төв: бугуйнаас үе мөч хүртэлх замын дунд, алганы гадаргуу руу (зузааны тал) шилжсэн
        const Vector3 palmLocal = Vector3Add(Vector3Scale(Vector3Subtract(knuckle, wrist), 0.5f * c.handScale), Vector3Scale(n, 0.014f / s));
        wristTarget[side] = Vector3Subtract(point(target.palm), Rotate(orient[side], palmLocal));
        // Гар хүрэхгүй бол (автоматын урд мод) бүх биеийг (дэлгэцийн гадна) зорилт руу шилжүүлнэ: гар агаарт
        // унжихгүй, мөрний хувцас сунахгүй
        const Vector3 shoulder = c.position[(size_t)c.joints[upper]];
        const float reach = (BoneLength(c, upper, lower) + BoneLength(c, lower, hand)) * 0.96f;
        const Vector3 toWrist = Vector3Subtract(wristTarget[side], shoulder);
        const float distance = Vector3Length(toWrist);
        if (distance > reach)
        {
            const Vector3 need = Vector3Scale(toWrist, (distance - reach) / distance);
            if (Vector3Length(need) > Vector3Length(shift)) shift = need;
        }
    }
    if (Vector3Length(shift) > 0.0f)
    {
        // (дээд тал нь 8 см: илүү бол мөрний ханцуйн нүх дэлгэцэнд орно)
        const float limit = 0.08f / s;
        if (Vector3Length(shift) > limit) shift = Vector3Scale(Vector3Normalize(shift), limit);
        for (Vector3& position : c.position) position = Vector3Add(position, shift);
    }
    Unsolve(c, RightArm);
    Unsolve(c, LeftArm);
    for (int side = 0; side < 2; ++side)
    {
        const Joint upper = side == 0 ? RightArm : LeftArm;
        const Joint lower = side == 0 ? RightForeArm : LeftForeArm;
        const Joint hand = side == 0 ? RightHand : LeftHand;
        const ViewArmTarget& target = targets[side];
        if (!target.active)
        {
            // Хэрэглэхгүй гар: доош унжина (дэлгэцэнд харагдахгүй)
            Aim(c, upper, lower, { side == 0 ? -0.15f : 0.15f, -1.0f, 0.05f });
            Aim(c, lower, hand, { 0.0f, -1.0f, 0.15f });
            continue;
        }
        if (framed[side])
        {
            const Vector3 tn = palmFacing[side];
            Reach(c, upper, lower, hand, wristTarget[side], direction(target.pole), 0.0f);
            FacePalm(c, side, tn); // (шууг сарвуутай нь зэрэг эргүүлнэ: бугуй мушгирахгүй)
            {
                const int handBone = c.joints[hand];
                Follow(c, handBone);
                c.rotation[(size_t)handBone] = orient[side]; // (анхныхаас шууд: эцгийн эргэлтээс үл хамаарна)
            }
            CurlFingers(c, side, target.curl, target.thumb);
            continue;
        }
        Reach(c, upper, lower, hand, point(target.palm), direction(target.pole), 0.08f / s);
        FacePalm(c, side, direction(target.facing));
        CurlFingers(c, side, target.curl, target.thumb);
    }
    // Зүүн гар бээлийтэй (зөвхөн эрхий хуруутай): долоовор, ядам, чигчий хурууг дунд хуруу руу 70% ойртуулж
    // нэг хэсэг болгоно (хуруунуудын хоорондох зай алга)
    if (targets[1].active)
    {
        const int middle = c.fingers[1][1][0];
        for (int finger : { 0, 2, 3 })
        {
            const int base = c.fingers[1][finger][0];
            if (base < 0 || middle < 0) continue;
            Follow(c, middle);
            Follow(c, base);
            const Vector3 shift = Vector3Scale(Vector3Subtract(c.position[(size_t)middle], c.position[(size_t)base]), 0.7f);
            for (int bone = 0; bone < c.model.boneCount; ++bone)
            {
                if (!UnderBone(c.model, bone, base)) continue;
                Follow(c, bone);
                c.position[(size_t)bone] = Vector3Add(c.position[(size_t)bone], shift);
            }
        }
    }
    Skin(c);

    c.material.maps[MATERIAL_MAP_ALBEDO].color = tint;
    rlDisableBackfaceCulling();
    rlPushMatrix();
    rlMultMatrixf(MatrixToFloat(bodyToWorld));
    rlScalef(s, s, s);
    for (const Mesh& mesh : c.arms)
    {
        if (mesh.vertexCount <= 0) continue;
        UploadBoneRows(c.material.shader, mesh.boneMatrices, mesh.boneCount);
        DrawMesh(mesh, c.material, MatrixIdentity());
    }
    // Зүүн гарт зөвхөн эрхий хуруутай бээлий: шаргал хүрэн (арьсны текстурыг үржүүлнэ)
    // (арьсны текстургүй: цагаан текстур дээр шаргал хүрэн өнгө, гэрлээр сүүдэрлэнэ)
    const unsigned int skinTexture = c.material.maps[MATERIAL_MAP_ALBEDO].texture.id;
    c.material.maps[MATERIAL_MAP_ALBEDO].texture.id = rlGetTextureIdDefault();
    c.material.maps[MATERIAL_MAP_ALBEDO].color = { 132, 112, 80, 255 }; // (CS-ийн тактикийн бээлий шиг шаргал хүрэн; арьснаас ялгарахаар бараан)
    for (const Mesh& mesh : c.glove)
    {
        if (mesh.vertexCount <= 0) continue;
        UploadBoneRows(c.material.shader, mesh.boneMatrices, mesh.boneCount);
        DrawMesh(mesh, c.material, MatrixIdentity());
    }
    c.material.maps[MATERIAL_MAP_ALBEDO].texture.id = skinTexture;
    c.material.maps[MATERIAL_MAP_ALBEDO].color = tint;
    rlPopMatrix();
    rlEnableBackfaceCulling();
    armsPass = false;
    std::fill(c.solved.begin(), c.solved.end(), 0); // (дараагийн 3-р этгээдийн зураглал жижигрүүлэлтгүй байрлалаас эхэлнэ)
}

bool HasCharacterClipArms(int id)
{
    return id >= 0 && id < kCharacterCount && characters[id].loaded && !characters[id].clipArms.empty();
}

void DrawCharacterClipArms(int id, Vector3 rightPalm, Vector3 leftPalm, Color tint, bool handsOnly)
{
    if (!HasCharacterClipArms(id)) return;
    Character& c = characters[id];
    const float s = c.scale;
    armsPass = true; // (сарвуу гартаа тохирсон хэмжээтэй: Tripo-гийн дүрийн сарвуу хэт том, хуруу бууг ороож чадахгүй)
    std::fill(c.solved.begin(), c.solved.end(), 0);
    for (int bone = 0; bone < c.model.boneCount; ++bone) Follow(c, bone);
    Unsolve(c, RightArm);
    Unsolve(c, LeftArm);
    // Бууны орон зайд (DrawGun: +X дүрийн зүүн, +Y дээш, +Z ам), метрээр. Гар дэлгэцийн доороос орж ирнэ: мөр дэлгэцэнд орохгүй.
    struct Grip
    {
        Vector3 palm, fingers, facing, toElbow, toShoulder;
        float curl, thumb;
    };
    const Grip grips[2] = {
        // Баруун: алга бариулын баруун талд, хуруу урагш (нугарч бариулыг ороно)
        { rightPalm, { 0.0f, -0.1f, 1.0f }, { 1.0f, 0.0f, 0.0f }, { -0.35f, -0.75f, -0.55f }, { -0.25f, -0.9f, -0.35f }, 75.0f, 60.0f },
        // Зүүн: алга урд модны доор дээш харсан, хуруу баруун тийш бага зэрэг урагш (нугарч модыг ороно), бугуй зүүн доор
        { leftPalm, { -0.85f, 0.0f, 0.5f }, { 0.0f, 1.0f, 0.0f }, { 0.6f, -0.65f, -0.45f }, { 0.3f, -0.9f, -0.3f }, 62.0f, 40.0f },
    };
    for (int side = 0; side < 2; ++side)
    {
        const Grip& grip = grips[side];
        const Joint upper = side == 0 ? RightArm : LeftArm, lower = side == 0 ? RightForeArm : LeftForeArm;
        const Joint hand = side == 0 ? RightHand : LeftHand;
        const int middle = c.fingers[side][1][0];
        if (middle < 0) continue;
        // Сарвууны чиглэл: (хуруу, алга) анхны байрлалаас хүссэн рүү
        const Vector3 wristBind = c.bind[(size_t)c.joints[hand]], knuckle = c.bind[(size_t)middle];
        const Vector3 f = Vector3Normalize(Vector3Subtract(knuckle, wristBind));
        const Vector3 n = Vector3Normalize(Vector3Subtract(c.palmNormal[side], Vector3Scale(f, Vector3DotProduct(c.palmNormal[side], f))));
        const Vector3 tf = Vector3Normalize(grip.fingers);
        const Vector3 tn = Vector3Normalize(Vector3Subtract(grip.facing, Vector3Scale(tf, Vector3DotProduct(grip.facing, tf))));
        const Vector3 b = Vector3CrossProduct(f, n), tb = Vector3CrossProduct(tf, tn);
        Matrix from = MatrixIdentity(), to = MatrixIdentity();
        from.m0 = f.x; from.m1 = f.y; from.m2 = f.z; from.m4 = n.x; from.m5 = n.y; from.m6 = n.z; from.m8 = b.x; from.m9 = b.y; from.m10 = b.z;
        to.m0 = tf.x; to.m1 = tf.y; to.m2 = tf.z; to.m4 = tn.x; to.m5 = tn.y; to.m6 = tn.z; to.m8 = tb.x; to.m9 = tb.y; to.m10 = tb.z;
        const Quaternion orient = QuaternionNormalize(QuaternionFromMatrix(MatrixMultiply(MatrixTranspose(from), to)));
        // Алганы төв бариул дээр: бугуй түүнээс тооцогдоно
        const Vector3 palmLocal = Vector3Add(Vector3Scale(Vector3Subtract(knuckle, wristBind), 0.5f * c.handScale), Vector3Scale(n, 0.014f / s));
        const Vector3 wrist = Vector3Subtract(Vector3Scale(grip.palm, 1.0f / s), Rotate(orient, palmLocal));
        const Vector3 toElbow = Vector3Normalize(grip.toElbow), toShoulder = Vector3Normalize(grip.toShoulder);
        const Vector3 elbow = Vector3Add(wrist, Vector3Scale(toElbow, BoneLength(c, lower, hand)));
        const auto place = [&](Joint joint, Joint child, Vector3 direction, Vector3 position) {
            const int bone = c.joints[joint];
            const Vector3 rest = Vector3Normalize(Vector3Subtract(c.bind[(size_t)c.joints[child]], c.bind[(size_t)bone]));
            c.rotation[(size_t)bone] = QuaternionFromVector3ToVector3(rest, direction);
            c.position[(size_t)bone] = position;
            c.solved[(size_t)bone] = 1;
        };
        place(lower, hand, Vector3Negate(toElbow), elbow);
        FacePalm(c, side, tn); // (шуу сарвуутай зэрэг эргэнэ: бугуй мушгирахгүй)
        // Бугалга шуугаас хамгийн бага эргэлтээр: тохойд мушгирахгүй (эс бөгөөс чихэр шиг нарийсна); мушгиралт мөрөнд, дэлгэцийн гадна
        {
            const int bone = c.joints[upper];
            const Quaternion forearm = c.rotation[(size_t)c.joints[lower]];
            const Vector3 rest = Vector3Normalize(Vector3Subtract(c.bind[(size_t)c.joints[lower]], c.bind[(size_t)bone]));
            const Quaternion bend = QuaternionFromVector3ToVector3(Rotate(forearm, rest), Vector3Negate(toShoulder));
            c.rotation[(size_t)bone] = QuaternionNormalize(QuaternionMultiply(bend, forearm));
            c.position[(size_t)bone] = Vector3Add(elbow, Vector3Scale(toShoulder, BoneLength(c, upper, lower)));
            c.solved[(size_t)bone] = 1;
        }
        const int handBone = c.joints[hand];
        c.rotation[(size_t)handBone] = orient;
        c.position[(size_t)handBone] = wrist;
        c.solved[(size_t)handBone] = 1;
        CurlFingers(c, side, grip.curl, grip.thumb, side == 0 ? 30.0f : -1.0f); // (баруун долоовор гох дээр)
    }
    Skin(c);
    c.material.maps[MATERIAL_MAP_ALBEDO].color = tint;
    rlDisableBackfaceCulling();
    rlPushMatrix();
    rlScalef(s, s, s);
    for (const Mesh& mesh : handsOnly ? c.clipHands : c.clipArms)
    {
        if (mesh.vertexCount <= 0) continue;
        UploadBoneRows(c.material.shader, mesh.boneMatrices, mesh.boneCount);
        DrawMesh(mesh, c.material, MatrixIdentity());
    }
    rlPopMatrix();
    rlEnableBackfaceCulling();
    armsPass = false;
    std::fill(c.solved.begin(), c.solved.end(), 0);
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
CharacterFrames DrawCharacterModel(int id, const HumanoidPose& pose, HeldItem held, bool direct, Color tint, int lowDetail)
{
    Profile::Token ikTiming = Profile::Begin(PROFILE_CHARACTER_NAME("char 2 ik")); // (Skin хүртэлх бүх байрлалын тооцоо)
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

    // Mixamo-гийн хөдөлгөөнтэй дүр: явган (хэвтээгүй) эсвэл агаарт (үсрэлт) үед их бие, хөл, толгойг клипээр.
    // Гарыг доорх зэвсгийн код (IK) дахин тавина: бууны байрлал, онилгоо, цэнэглэлт хэвээр.
    const bool airborne = !direct && !pose.grounded && !pose.seated && pose.fall <= 0.0f && pose.spin == 0.0f &&
                          !pose.swimming && !pose.dead && pose.grip <= 0.0f;
    const bool clipBody = c.anims && !bindPoseOnly && ((onFoot && !lying) || airborne);
    if (clipBody)
    {
        ClipBody body;
        // Цээж: урт буу баруун мөрөө урагш (хуучин хөдөлгөөний -26°-тай ижил), гар буу бараг шулуун, хатгах, шидэх хөдөлгөөн биеэрээ
        body.yaw = (held == HeldItem::Rifle ? -34.0f : twoHanded ? -26.0f : pistol ? -6.0f : knife ? -15.0f : grenade ? -8.0f : -4.0f) + 46.0f * thrust + 38.0f * wind - 34.0f * release;
        body.lean = -0.5f * aimPitch - recoil * (twoHanded ? 3.0f : 1.5f) + 16.0f * thrust - 12.0f * wind + 24.0f * release;
        body.gazePitch = pose.aimPitch * (0.35f + 0.65f * aim);
        PoseFromClips(c, pose, move, crouch, aim, airborne, body);
        Unsolve(c, RightArm);
        Unsolve(c, LeftArm);
    }

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
    if (!clipBody) Place(c, Hips, hipRotation, hipOffset);

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

    if (clipBody)
    {
        // (клипээр байрлуулсан)
    }
    else if (!onFoot && std::fabs(pose.seatTwist) > 0.5f)
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
    if (!clipBody) Turn(c, Neck, QuaternionMultiply(AxisAngle(0.0f, 1.0f, 0.0f, headYaw - chestYaw * 0.85f),
                                     QuaternionMultiply(AxisAngle(1.0f, 0.0f, 0.0f, headPitch),
                                                        AxisAngle(0.0f, 0.0f, 1.0f, headRoll))));

    // --- Хөл ---
    const Vector3 down = { 0.0f, -1.0f, 0.0f };
    for (int side = 0; side < 2 && !clipBody; ++side)
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
        if (facePalm)
        {
            FacePalm(c, side, palm);
            if (clipBody) ShareTwist(c, upper, lower, 0.5f); // (их бол мөр шовх сунана)
        }
    };
    // Гарыг өнцгөөр нь тавих (чөлөөтэй савлах гар).
    // abduct: гарыг биеэс хажуу тийш холдуулах өнцөг (гуя өргөн дүрд сарвуу гуяны дотор орж "халаасандаа" мэт харагдана)
    auto swing = [&](int side, float shoulderAngle, float yaw, float elbowAngle, float abduct = 0.0f) {
        const float inward = side == 0 ? 1.0f : -1.0f;
        const Quaternion shoulder = QuaternionMultiply(AxisAngle(0.0f, 1.0f, 0.0f, yaw * inward),
                                                       QuaternionMultiply(AxisAngle(0.0f, 0.0f, 1.0f, -inward * abduct),
                                                                          AxisAngle(1.0f, 0.0f, 0.0f, -shoulderAngle)));
        const Quaternion elbow = QuaternionMultiply(shoulder, AxisAngle(1.0f, 0.0f, 0.0f, -elbowAngle));
        Aim(c, side == 0 ? RightArm : LeftArm, side == 0 ? RightForeArm : LeftForeArm, Rotate(chest, Rotate(shoulder, down)));
        Aim(c, side == 0 ? RightForeArm : LeftForeArm, side == 0 ? RightHand : LeftHand, Rotate(chest, Rotate(elbow, down)));
    };
    // Алхаа, гүйлтийн үед чөлөөтэй гар хөлийн эсрэг савлана; гүйхэд тохой нугарна.
    // Баруун хөл урд байхад баруун гар хойно. Гүйхэд тохой ~90° нугарна.
    const float armSwing = stride * (24.0f + 10.0f * gait.run + 12.0f * run) * rightWave;
    const float armBend = 16.0f + stride * (8.0f + 60.0f * std::max(0.9f * gait.run, run));
    float curl[2] = { 14.0f, 14.0f };
    float triggerCurl = -1.0f; // баруун долоовор гох дээр

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
            heading, QuaternionMultiply(AxisAngle(0.0f, 1.0f, 0.0f, (clipBody ? 6.0f : 12.0f + 22.0f * run) * low),
                                        AxisAngle(1.0f, 0.0f, 0.0f, 10.0f * low + breathSway - 3.5f * recoil -
                                                                        16.0f * reloading * upright - aimPitch)));
        // Бөгсний үзүүр (бууны орон зайд) баруун мөрний урд доор байрлана.
        // (Хэвтсэн үед бөгс мөрний урд, биеийн тэнхлэгийн дагуу.)
        // (Пуужин харвагч: хоолойн доод тал мөрний орой дээр хэвтэнэ, хоолой нь мөрнөөс хойш цухуйна.)
        const Vector3 butt = launcher
                                 ? Vector3Add(rightShoulder, Rotate(chest, { 0.03f, 0.09f, 0.02f }))
                                 : clipBody // (их бие клипээр: бөгсний үзүүр суганд, бугалга түүний гадна талаар)
                                 ? Vector3Add(rightShoulder, Rotate(heading, { 0.03f, -0.08f - 0.02f * low, 0.05f }))
                                 : Vector3Add(rightShoulder, Rotate(chest, Mix({ 0.07f, -0.07f - 0.07f * low, 0.07f },
                                                                               { 0.06f, 0.05f, 0.06f }, prone)));
        const Vector3 buttLocal = launcher ? Vector3{ kGunX, kGunY - 0.05f, 0.2f - pose.gunKick }
                                           : Vector3{ kGunX, kGunY, (clipBody ? -0.05f : 0.04f) - pose.gunKick };
        // Автомат: загвар 1.15 м (бодит АК 0.88 м), ийм урттай бол зүүн гар урд модонд хүрэхгүй: 3-р этгээдэд бодит хэмжээ рүү
        const bool rifle = held == HeldItem::Rifle;
        const float gunScale = rifle ? 0.85f : 1.0f;
        const Vector3 gunOrigin = Vector3Subtract(butt, Rotate(gunRotation, Vector3Scale(buttLocal, gunScale)));
        frames.gun = MatrixMultiply(MatrixScale(gunScale, gunScale, gunScale), Frame(gunRotation, gunOrigin));

        // (автомат: загварын жинхэнэ бариул, урд модны доод тал, загвараас хэмжсэн)
        const Vector3 gripLocal = rifle ? Vector3{ kGunX - 0.015f, kGunY - 0.15f, 0.28f - pose.gunKick }
                                : launcher ? Vector3{ kGunX, kGunY - 0.17f, 0.5f - pose.gunKick }
                                           : Vector3{ kGunX, kGunY - 0.14f, 0.3f - pose.gunKick };
        const Vector3 foreLocal = rifle ? Vector3{ kGunX + 0.005f, kGunY - 0.03f, 0.74f - pose.gunKick }
                                : launcher ? Vector3{ kGunX, kGunY - 0.17f, 0.72f - pose.gunKick }
                                           : Vector3{ kGunX, kGunY - 0.075f, 0.66f - pose.gunKick };
        const Vector3 grip = Vector3Add(gunOrigin, Rotate(gunRotation, Vector3Scale(gripLocal, gunScale)));
        Vector3 fore = Vector3Add(gunOrigin, Rotate(gunRotation, Vector3Scale(foreLocal, gunScale)));
        if (reload >= 0.0f)
        {
            // Цэнэглэх: зүүн гар сумны хайрцгийг авч бүс рүүгээ буулгаад, шинийг авчирч хийгээд буцаж урд модоо барина.
            const Vector3 magazine = Vector3Add(gunOrigin, Rotate(gunRotation, Vector3Scale({ kGunX, kGunY - 0.3f, rifle ? 0.6f : 0.54f }, gunScale)));
            const Vector3 belt = Vector3Add(Vector3Scale(JointPosition(c, LeftUpLeg), s), Rotate(chest, { 0.1f, 0.08f, 0.14f }));
            if (reload < 0.2f) fore = Mix(fore, magazine, Smooth(reload / 0.2f));
            else if (reload < 0.45f) fore = Mix(magazine, belt, Smooth((reload - 0.2f) / 0.25f));
            else if (reload < 0.72f) fore = Mix(belt, magazine, Smooth((reload - 0.45f) / 0.27f));
            else fore = Mix(magazine, fore, Smooth((reload - 0.72f) / 0.2f));
        }
        // Хэвтсэн үед тохой газарт тулна: хажуу тийш, доош (биеийн орон зайд урагш) чиглэнэ.
        // (сугалсан буу: тохой гадагш, хойш, бугалга бөгсний гадна талаар)
        reach(0, grip, Rotate(chest, clipBody ? Vector3{ -1.0f, -0.7f, -0.5f } : Mix({ -0.7f, -1.0f, -0.3f }, { -0.9f, -0.4f, 0.8f }, prone)),
              Rotate(gunRotation, { 1.0f, 0.0f, -0.2f }), true);
        reach(1, fore, Rotate(chest, Mix({ 0.35f, -1.0f, 0.0f }, { 0.8f, -0.4f, 0.8f }, prone)),
              Rotate(gunRotation, { -0.25f, 1.0f, 0.0f }), true);
        curl[0] = curl[1] = 52.0f;
        if (rifle)
        {
            curl[0] = 66.0f; // бариулыг бүрэн атгана
            curl[1] = 60.0f; // урд модыг ороож атгана
            triggerCurl = 30.0f;
        }
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
            // (Mixamo-гийн дүр: биеийн хэлбэр нь бодит тул гарыг гуянаас холдуулна)
            const float abduct = clipBody ? 11.0f : 0.0f;
            swing(0, -armSwing, 4.0f, armBend, abduct);
            swing(1, armSwing, 4.0f, armBend, abduct);
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

    CurlFingers(c, 0, curl[0], -1.0f, triggerCurl);
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
    Profile::End(ikTiming);
    {
        PROFILE_CHARACTER("char 3 skin");
        Skin(c);
    }
    c.material.maps[MATERIAL_MAP_ALBEDO].color = tint;

    // Tripo-гийн загварууд хоёр талт материалтай: нөмрөг, малгайн хүрээ, хувцасны захууд нэг давхар
    // гадаргуугаас бүтдэг тул ар талыг нь ч зурна (эс бөгөөс нэг талаас нь нэвт харагдана).
    rlDisableBackfaceCulling();
    rlPushMatrix();
    rlScalef(s, s, s);
    for (int index = 0; index < c.model.meshCount; ++index)
    {
        // Сүүдэрт: хөнгөн хувилбар (сүүдрийн хэлбэрт ялгаагүй), гэрэлтүүлэггүй shader
        const int detail = shadowMaterial ? std::max(lowDetail, 1) : lowDetail;
        const std::vector<Mesh>& level = detail >= 2 ? c.lodFar : c.lod;
        const bool light = detail > 0 && index < (int)level.size() && level[(size_t)index].vertexCount > 0;
        const Mesh& mesh = light ? level[(size_t)index] : c.model.meshes[index];
        const Material& material = shadowMaterial ? *shadowMaterial : c.material;
        {
            PROFILE_CHARACTER("char 4 bone upload");
            UploadBoneRows(material.shader, mesh.boneMatrices, mesh.boneCount);
        }
        PROFILE_CHARACTER("char 5 drawmesh");
        DrawMesh(mesh, material, MatrixIdentity());
    }
    for (Character::Wear& w : c.wear)
    {
        const int detail = shadowMaterial ? std::max(lowDetail, 1) : lowDetail;
        w.material.maps[MATERIAL_MAP_ALBEDO].color = tint;
        const Material& material = shadowMaterial ? *shadowMaterial : w.material;
        for (int index = 0; index < w.model.meshCount; ++index)
        {
            const std::vector<Mesh>& level = detail >= 2 ? w.lodFar : w.lod;
            const bool light = detail > 0 && index < (int)level.size() && level[(size_t)index].vertexCount > 0;
            const Mesh& mesh = light ? level[(size_t)index] : w.model.meshes[index];
            UploadBoneRows(material.shader, mesh.boneMatrices, mesh.boneCount);
            DrawMesh(mesh, material, MatrixIdentity());
        }
    }
    rlPopMatrix();
    rlEnableBackfaceCulling();

    return frames;
}
