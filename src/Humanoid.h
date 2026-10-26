#pragma once

#include "raylib.h"

// Тоглогч, цэрэг, зомби гурвуулаа ашиглах үе мөчтэй хүн дүрс.
// Бие нь нарийссан (tapered) хэсгүүдээс бүтэх ба төрөл бүр өөрийн нүүр, хувцас, хэрэгсэлтэй.

enum class HumanoidKind
{
    Player,  // дээлтэй, шовгор малгайтай
    Soldier, // дүрэмт хувцас, дуулга, хантааз, үүргэвч
    Zombie
};

// Гартаа барьж буй зүйл
enum class HeldItem
{
    None,
    Knife,
    Pistol,
    Rifle,
    Shotgun,
    Grenade,
    Sniper,  // урт голтой, дуран хараатай буу
    Launcher // мөрөн дээр тавьж буудах пуужин харвагч
};

// Гадаад төрх
struct HumanoidLook
{
    HumanoidKind kind = HumanoidKind::Zombie;

    Color pants = { 50, 50, 50, 255 };
    Color boots = { 30, 28, 26, 255 };
    Color body = { 90, 90, 90, 255 };   // цамц / дээл, ханцуй
    Color skin = { 224, 180, 140, 255 };
    Color belt = { 60, 45, 30, 255 };
    Color hat = { 150, 30, 30, 255 };   // малгай / дуулга
    Color vest = { 60, 64, 50, 255 };   // хантааз, үүргэвч
    Color trim = { 230, 150, 30, 255 }; // дээлийн эмжээр
    Color hair = { 26, 22, 20, 255 };

    HeldItem held = HeldItem::None;
    float bulk = 1.0f; // биеийн өргөний үржүүлэгч
    int model = -1;    // гаднаас оруулсан 3D загварын дугаар (CharacterModel.h); -1 = кодоор үүсгэсэн дүрс
    Color modelTint = { 255, 255, 255, 255 }; // загварын өнгийг үржүүлнэ (ижил загвартай дүрүүдийг ялгахад)
    bool lowDetail = false; // холын дүр: загварын хөнгөн (цөөн гурвалжинтай) хувилбараар зурна

    // Зөвхөн зомби
    bool missingLeftArm = false; // зүүн гар тохойноос доош байхгүй
    bool bloody = false;         // цээжин дээр цусны толбо
    bool shirtless = false;      // цамцгүй, хавирга нь товойсон
    bool openRibs = false;       // хэвлийд урагдсан шарх, хавирга харагдана
    bool tornLeg = false;        // нэг шилбэ нүцгэн, хөл нүцгэн
    bool oneEye = false;
    bool headWound = false;
    int hairStyle = 0;           // 0 халзан, 1 цоохор үлдсэн, 2 сэгсгэр урт
};

// Биеийн байрлал. Өнцгүүд градусаар. Индекс 0 = баруун тал, 1 = зүүн тал.
struct HumanoidPose
{
    float hip[2] = { 0.0f, 0.0f };          // гуя урагш (+) / хойш (-)
    float knee[2] = { 0.0f, 0.0f };         // өвдөгний нугалалт (>= 0)
    float shoulder[2] = { 0.0f, 0.0f };     // гар урагш өргөх (+)
    float shoulderYaw[2] = { 0.0f, 0.0f };  // гарыг дотогш (+) эргүүлэх
    float elbow[2] = { 0.0f, 0.0f };        // тохойн нугалалт (>= 0)
    float legSpread = 0.0f; // хөлөө хажуу тийш алцайлгах (унаа унахад)
    float bob = 0.0f;       // биеийн босоо шилжилт, метр
    float lean = 0.0f;      // их бие урагш бөхийх
    float roll = 0.0f;      // их бие хажуу тийш найгах
    float headPitch = 0.0f; // толгой урагш бөхийх
    float headRoll = 0.0f;  // толгой хажуу тийш хазайх
    float jaw = 0.0f;       // эрүү онгойх (зомби)
    float fall = 0.0f;      // 0 = босоо, 1 = нуруугаараа унасан
    float spin = 0.0f;      // биеийн төвөө тойрон эргэлдэх өнцөг, градус (агаарт шидэгдсэн үед)
    float breath = 0.0f;    // -1..1: амьсгалын үе; цээж тэлж, мөр өргөгдөнө
    float gunKick = 0.0f;   // бууны ухралт, метр
    bool hideItem = false;  // гарт барьсан зүйлийг зурахгүй (гранат шидэгдсэний дараа)
    bool swimming = false;  // сэлж байна (ApplySwimPose): гар хоосон, гар хөл өгөгдсөн өнцгөөр (алхааны савлалт биш)
    bool dead = false;      // үхсэн (ApplyDeathPose): унасны дараа урт буу гараас мултарч хажууд нь газар хэвтэнэ
    // Гараар ямар нэг зүйлээс барих (цонхны тавцан, хүрээ): grip > 0 үед гар shoulder/elbow-ийн өнцгөөс
    // gripPoint руу (хөлийн орон зайд, метрээр; [0] баруун, [1] зүүн) шилжинэ. Гар хоосон, seated үед.
    float grip = 0.0f;
    Vector3 gripPoint[2] = {};
    // seated үед: хөл нугарахад биеийг доошлуулж, доод талын хөлийн ул хөлийн түвшинд (газар, тавцан) хүрнэ.
    bool plantFeet = false;
    // seated үед: ташаа, хөлийг биеийн харах зүгээс ийм өнцгөөр (градус, эерэг = +X тийш) эргүүлнэ. Унаан дээр
    // буудахад бие (цээж, гар) онилсон зүг рүү, ташаа хөл нь унааны зүгт үлдэнэ.
    float seatTwist = 0.0f;
    // seated үед (унаа): rein > 0 бол зүүн гар reinPoint-ийг (мотоциклийн бариул, морины хазаар; хөлийн орон
    // зайд, метрээр) барина. Баруун гар зэвсгээ ганцаараа барина.
    // Шууд (direct) байрлалтай дүр (зомби) явж байхад: хөлийг цэргийнхтэй ижил алхааны тооцоогоор ээлжлэн
    // гишгүүлж, газарт тулсан тавхайг хөдөлгөхгүй (гулсахгүй). walkPhase, speed-ийг ашиглана.
    bool plantWalk = false;
    float stepLift = 1.0f; // хөл өргөх өндрийн үржүүлэгч (зомби: хүнд, тод гишгэлт)
    float rein = 0.0f;
    Vector3 reinPoint = { 0.0f, 0.0f, 0.0f };

    // Төлөв: гаднаас оруулсан загварын хөдөлгөөний найруулагч (CharacterModel) үүнээс бүтэн биеийн
    // байрлалыг өөрөө тооцно (хөл газарт тулах, зэвсэг бүрийн зогсолт, дайралт, шидэлт).
    float walkPhase = 0.0f;       // алхааны үе, радиан
    float moveAmount = 0.0f;      // 0 зогссон, 1 алхаа, >1 гүйлт
    float speed = 0.0f;           // газраар явах хурд, м/с: алхамын уртыг үүнээс тооцож хөл гулсахгүй
    float time = 0.0f;            // секунд (тайван зогсолтын хөдөлгөөнд)
    float recoil = 0.0f;          // 1 = дөнгөж буудсан
    float aim = 0.0f;             // 0 = зэвсгээ бэлэн барьсан, 1 = онилж буй
    float aimPitch = 0.0f;        // онилсон бууны босоо өнцөг, градус (+ = дээш); aim-аар жинлэнэ
    float actionProgress = -1.0f; // хатгах / шидэх хөдөлгөөний явц 0..1; -1 = хөдөлгөөнгүй
    bool grounded = true;
    bool seated = false;          // унаанд сууж яваа: хөлийн өнцгийг гаднаас өгсөн
    float crouch = 0.0f;          // 0 = босоо .. 1 = суусан
    float prone = 0.0f;           // 0 = босоо .. 1 = хэвтсэн (бүх бие урагш хэвтэнэ)
    float moveDirection = 0.0f;   // явж буй зүг, харж буй зүгтэй харьцангуй, радиан (0 = урагш, PI = арагш)
    float reload = -1.0f;         // цэнэглэлтийн явц 0..1; -1 = цэнэглээгүй
};

// Зэвсэгтэй хүний (тоглогч, цэрэг) байрлал.
//  walkCycle  - алхааны үе (радиан), moveAmount - 0 зогсож байна, 1 алхаж байна, >1 гүйж байна
//  recoil     - 0..1, дөнгөж буудсан үед 1
//  held       - барьж буй зүйл: гарын байрлалыг тодорхойлно
//  action     - 1 -> 0: хутгаар хатгах эсвэл гранат шидэх хөдөлгөөний явц (0 = хөдөлгөөнгүй)
HumanoidPose MakeSoldierPose(float walkCycle, float moveAmount, bool grounded, float time, float recoil,
                             HeldItem held = HeldItem::Rifle, float action = 0.0f);

// Унасан (шархадсан, босгохыг хүлээж буй) дүр: нуруугаараа бүрэн хэвтэж, нэг өвдгөө өргөн, гараа аажим хөдөлгөнө.
// Алхах, онилох, суух, хэвтэх (prone) байрлалыг арилгана (тэдгээртэй хольж хагас унасан мэт харагдахгүй).
// Тоглогч, AI дайчин, нөгөө тоглогчийн дүр бүгд үүнийг хэрэглэнэ.
void ApplyDownedPose(HumanoidPose& pose, float time);

// Гүн усанд сэлэх: хэвтээ биетэй (prone), гараа ээлжлэн сэлүүрдэж, хөлөө жийнэ; толгой усан дээр, барьсан зүйл
// харагдахгүй. swimSpeed: явах хурд (м/с; 0 бол хөвж зогсоно). Хөлийн цэг (feet) нь гадаргуугаас kSwimSink доор.
constexpr float kSwimDepth = 1.15f; // хөл доорх гадаргуугаас усны гадаргуу хүртэл ийм гүн бол сэлнэ
constexpr float kSwimSink = 0.95f;  // сэлэх үед хөлийн цэг (биеийн суурь) усны гадаргуугаас доош
void ApplySwimPose(HumanoidPose& pose, float time, float swimSpeed);

// Үхэх хөдөлгөөн (seconds: үхсэнээс хойш). Сум тусахад бие цочиж, өвдөг сулраад унана, газар мөргөөд бага зэрэг
// ойж тогтоно, гар сул. style: 0 нуруугаараа хойшоо, 1 нүүрээрээ урагшаа, 2 өвдөг сөхрөөд хажуу тийш хөрвөнө,
// 3 (унасан байснаас) хэвтсэн хэвээр.
constexpr int kDeathStyles = 3;
void ApplyDeathPose(HumanoidPose& pose, float seconds, int style);

// Хурдаас хамаарах алхааны хэлбэр (хүний алхаа, гүйлтийн хэмжилтэд ойртуулсан).
struct HumanoidGait
{
    float run = 0.0f;       // 0 = алхаа (нэг хөл үргэлж газарт), 1 = гүйлт (хоёр хөл газраас хөндийрөх агшинтай)
    float duty = 0.6f;      // мөчлөгийн хэдэн хэсэгт хөл газарт тулах вэ
    float frequency = 0.0f; // нэг хөлийн бүтэн мөчлөг (хоёр алхам) секундэд хэд
    float contact = 0.0f;   // хөл газарт тулж байх хооронд бие хэдэн метр урагшлах вэ
};
HumanoidGait HumanoidGaitFor(float speed);
// walkCycle-ийг нэмэгдүүлэх хурд (радиан/с). Алхамын урт мөн үүнээс хамаардаг тул бүх дүр үүнийг ашиглана.
float HumanoidGaitRate(float speed);

// 1-р этгээдийн камерт: дэлгэцийн баруун доор гар ба барьсан зэвсэг (Counter-Strike маягийн).
// recoil 0..1, walkPhase алхааны үе, moveAmount 0..1, reload 0..1 (-1 = үгүй), action 1 -> 0 (хатгах / шидэх).
// aim 0..1: онилох (баруун товч) үед буу дэлгэцийн төвд, гол төмөр нь хараатай нэг шугамд ирнэ.
void DrawViewModel(const Camera3D& camera, HeldItem held, float recoil, float walkPhase, float moveAmount, float reload,
                   float action, float aim = 0.0f);
// 1-р этгээдийн үед сумны мөр, амны гал гарах цэг (ертөнцөд).
Vector3 ViewModelMuzzle(const Camera3D& camera, HeldItem held, float aim = 0.0f);

// Барьж буй зүйлийн амны (сум гарах цэгийн) орон нутгийн байрлал.
Vector3 HumanoidMuzzleFor(HeldItem held);

// feet: хөлийн байрлал, facing: радиан (0 = +Z).
// detailed = false үед нүүр, хуруу, халаас зэрэг жижиг хэсгийг алгасна (холын дүрд).
void DrawHumanoid(Vector3 feet, float facing, float scale, const HumanoidPose& pose, const HumanoidLook& look,
                  bool detailed = true);

// Хөл доорх бөөрөнхий сүүдэр (нарны эсрэг тал руу бага зэрэг шилжсэн).
void DrawBlobShadow(Vector3 ground, float radius);

// Дүрсийн орон нутгийн цэгийг (facing-ээр эргүүлж) ертөнцийн цэг болгоно.
Vector3 HumanoidLocalToWorld(Vector3 feet, float facing, Vector3 local);

// Бууны амны орон нутгийн байрлал (scale = 1 үед).
extern const Vector3 kHumanoidMuzzle;

// Нүүр, хуруу зэрэг нарийн хэсгийг энэ зайнаас ойр үед л зурна.
constexpr float kHumanoidDetailDistance = 24.0f;
