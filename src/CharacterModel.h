#pragma once

#include "Humanoid.h"
#include "raylib.h"

// Гаднаас (Tripo3D) оруулсан ястай 3D загвараар зурагдах дүрүүд.
//
// Загварууд хөдөлгөөний файлгүй ирдэг тул ясыг тоглоомын өөрийн HumanoidPose (алхах, буудах, унах,
// унаанд суух өнцгүүд)-оор удирдана: мөч бүрийг хүссэн чиглэл рүү нь эргүүлж, оройнуудыг CPU дээр арьслана.
// Файл олдохгүй бол тухайн дүр хуучин кодоор үүсгэсэн дүрсээрээ зурагдана.
//
// Файлууд (tools\prepare_character.ps1 бэлтгэнэ):
//   assets\<name>_mesh.glb   - mesh + яс (Mixamo нэршил)
//   assets\<name>_color.png  - өнгөний текстур

// Загварын дугаар (HumanoidLook::model)
constexpr int kCharacterFighter1 = 0;     // assets\fighter1
constexpr int kCharacterFighter2 = 1;     // assets\fighter2
constexpr int kCharacterSniper = 2;       // assets\fighter3
constexpr int kCharacterSurvivorMan = 3;  // assets\survivor_man
constexpr int kCharacterSurvivorWoman = 4; // assets\survivor_woman
constexpr int kCharacterZombieWalker = 5; // assets\zombie_walker
constexpr int kCharacterZombieRunner = 6; // assets\zombie_runner
constexpr int kCharacterZombieBrute = 7;  // assets\zombie_brute
constexpr int kCharacterZombieBoss = 8;   // assets\giant_zombie (аварга)
constexpr int kCharacterModel1 = 9;       // assets\model-1 (1-р дайчны сонгож болох дүр)
constexpr int kCharacterModel3 = 10;      // assets\model-3 (1-р дайчны сонгож болох дүр)
constexpr int kCharacterDemon = 11;       // assets\demon (онцгой дүр, Gold-оор)
constexpr int kCharacterIron = 12;        // assets\iron (онцгой дүр, Gold-оор)
// Аврах иргэд (даалгаврын газраас хамаарч): эмнэлэгт эмч, сургуульд сурагч, хийдэд лам, бусад нь малчин
constexpr int kCharacterDoctorMan = 13;   // assets\survivor_doctor_man
constexpr int kCharacterDoctorWoman = 14; // assets\survivor_doctor_woman
constexpr int kCharacterStudentBoy = 15;  // assets\survivor_student_boy
constexpr int kCharacterStudentGirl = 16; // assets\survivor_student_girl
constexpr int kCharacterMonk = 17;        // assets\survivor_monk
constexpr int kCharacterModel2 = 18;       // assets\model2 (1-р дайчны сонгож болох дүр)
constexpr int kCharacterZombieWoman = 19;  // assets\woman_zombie (гүйгч зомбины тал нь)
// 1-р этгээдийн гар (заавал биш): Mixamo нэршилтэй, хурууны ястай хүний загвар (бүтэн бие байж болно: зөвхөн гарыг
// нь зурна). tools\prepare_character.ps1 -Name fp_arms -> assets/fp_arms_mesh.glb, assets/fp_arms_color.png.
// Байхгүй бол кодоор угсарсан бээлийтэй гар (Humanoid.cpp: BuildFist).
constexpr int kCharacterFpArms = 20;
constexpr int kCharacterCount = 21;

// shader: текстур, гэрэлтүүлэг, манантай дүрийн shader (Environment::CharacterShader).
void LoadCharacterModels(Shader shader);
// Арьслалтын матрицуудыг (mesh.boneMatrices) дүрийн shader-ийн boneRows руу (яс бүр 3 мөр) илгээнэ. DrawMesh-ийн
// өмнөхөн дуудна (морь, баавгай ч мөн).
void UploadBoneRows(Shader shader, const Matrix* bones, int count);
void UnloadCharacterModels();
bool HasCharacterModel(int id);
// Туршилт: загваруудыг хөдөлгөөнгүй, анхны (A) зогсолтоор нь зурна (загвар өөрөө ямар байгааг харахад).
void SetCharacterBindPose(bool enabled);

// --- 1-р этгээдийн гар (viewmodel) ---
// Загварын зөвхөн гарын хэсгийг (мөр, шуу, сарвуу, хуруу) ачаалахдаа тусгаарласан mesh-ээр, IK-аар бариул руу хүргэж
// зурна: жинхэнэ загварын ханцуй, бээлий, хуруу (Tripo-гийн загвар, хурууны ястай).
// Орон зай: "бие" = загварын орон зай метрээр (хөл нь эх, +Y дээш, +Z урагш, +X дүрийн зүүн тал).
struct ViewArmTarget
{
    bool active = false;
    Vector3 palm = { 0.0f, 0.0f, 0.0f };   // алганы төв хүрэх цэг (ертөнцийн орон зай)
    Vector3 facing = { 0.0f, 0.0f, 1.0f }; // алганы харах зүг (бариул руу), ертөнц
    Vector3 pole = { 0.0f, -1.0f, 0.0f };  // тохойн хазайх зүг, ертөнц
    float curl = 70.0f;                    // хуруу нугалах өнцөг (градус)
};
bool HasCharacterArms(int id);
// Нүдний байрлал биеийн орон зайд (метр): камерыг энд тавина.
Vector3 CharacterEye(int id);
// Баруун мөр (RightArm яс) биеийн орон зайд (метр): 1-р этгээдийн биеийг зэвсгийн бөгс / бариулаар бэхлэхэд.
Vector3 CharacterRightShoulder(int id);
// bodyToWorld: биеийн орон зай (метр) -> ертөнц. targets[0] баруун, [1] зүүн гар; идэвхгүй гар доош унжина.
void DrawCharacterArms(int id, const Matrix& bodyToWorld, const ViewArmTarget targets[2], Color tint);

struct CharacterFrames
{
    Matrix gun;          // хоёр гараар барих бууг зурах хувиргалт (DrawGun-ий их биеийн орон зай)
    Matrix rightForearm; // баруун шууны орон зай: эх нь тохой, -Y нь шуу дагуу (гар буу, хутга, гранат)
};

// Дүрийн үндсэн хувиргалт (байрлал, харах зүг, уналт) rlgl-д тавигдсан үед дуудна.
// held: барьж буй зэвсэг; биеийн байрлал, гарын барилт түүнээс хамаарна.
// Гар бууны үед rightForearm нь гар бууны орон зай (эх нь бариулын төв, +Z нь ам, +Y нь дээш).
// direct: байрлалын өнцгүүдийг (гуя, өвдөг, мөр, тохой) найруулагчгүйгээр шууд хэрэглэнэ (зомби: өөрийн алхаатай).
// tint: загварын өнгийг үржүүлнэ (нэг загвартай олон дүрийг ялгахад).
// lowDetail: холын дүрд загварын хөнгөн хувилбар (ачаалахдаа оройг нүдээр бүлэглэж үүсгэсэн): 0 бүтэн, 1 хөнгөн,
// 2 маш хөнгөн (манан дунд жижиг харагдах дүрд).
CharacterFrames DrawCharacterModel(int id, const HumanoidPose& pose, HeldItem held, bool direct = false,
                                   Color tint = WHITE, int lowDetail = 0);
