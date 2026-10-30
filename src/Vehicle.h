#pragma once

#include "World.h"
#include "Zombie.h"
#include "raylib.h"

#include <vector>

// Унаж явах тээврийн хэрэгслүүд.
enum class VehicleType
{
    Jeep,       // жийп: засмал зам дээр хамгийн хурдан, хээр талд удаан
    Motorcycle, // мотоцикл: хөнгөн, хурдан, эргэлт сайн
    Trike,      // гурван дугуйт ачааны мотоцикл: удаан боловч тогтвортой
    Horse,      // морь: зам хамаарахгүй, толгод сайн давна; Shift дарахад давхина
    Truck,      // ачааны машин (ЗИЛ-131): удаан, хүнд, зомбийг бут цохино, багийг тэвшиндээ авч явна
    Suv,        // Nissan X-Trail: жийпээс хөнгөн, хээр талд арай сул
    Hatchback,  // Toyota Yaris: засмал зам дээр хамгийн хурдан, хээр, толгодод муу, эмзэг
    Helicopter, // Ми-8 (жинхэнэ хэмжээ: их бие 18 м, сэнс 21 м): заставын дэргэд; газрын хэлбэрийг дагаж нам нисэнэ
    Armored,    // дээвэр дээрээ пулемёттой хуягт машин: суухдаа жолооч / буудагч сонгоно, нөгөө суудалд AI цэрэг
    Sedan,      // Toyota Crown: урт, намхан суудлын машин; засмал зам дээр хурдан, тогтвортой, хээр талд муу
    Compact     // Toyota Aqua: жижиг гибрид; бензин хамгийн удаан дуусна, хээр талд муу
};
constexpr int kVehicleTypeCount = (int)VehicleType::Compact + 1;
// Хуягт машины пулемёт урдаасаа зүүн, баруун тийш эргэх хязгаар (радиан, 55 градус): буудагч байрнаасаа хөдлөхгүй,
// гар нь бариулд хүрнэ, ар тал руу буудахгүй
constexpr float kArmorTraverse = 55.0f * 3.14159265f / 180.0f;

// Нисдэг тэрэг: "өндөр авах" (Garage::SetLift) дарахад хөөрч, kHeliControl метрээс дээш гарсны дараа л урагш хойш
// (throttle), эргэлтээр (steer) удирдана. Нислэгийн өндрөө барина (байшин бүр дээр дээш доош үсрэхгүй): хөөрөхдөө
// газраас kHeliCruise, урд нисэх замд байгаа барилгаас kHeliClearance, газар (уул)-аас kHeliFloor дээгүүр болтол л дээш
// авч, буухгүй. Хэт хурдтай нисвэл өндөр авч амжихгүй мөргөж сүйрнэ.
constexpr float kHeliCruise = 45.0f;    // газар дээрх нислэгийн өндөр, м
constexpr float kHeliClearance = 15.0f; // хамгийн өндөр барилгын оройгоос дээш, м
constexpr float kHeliFloor = 15.0f;     // доорх газраас хамгийн багадаа, м
constexpr float kHeliControl = 6.0f;  // үүнээс өндөр гарсны дараа хөдөлж эхэлнэ
constexpr int kHeliSeats = 6;         // нисгэгчээс гадна: 2 хаалганы буудагч (баг), 4 бүхээгт (нөгөө тоглогч, түүний баг)

// Бүхээгтэй суудлын машин (жийп, X-Trail, Yaris, Crown, Aqua): жолооч зүүн, зорчигч баруун урд суудалд.
inline bool IsCar(VehicleType type)
{
    return type == VehicleType::Jeep || type == VehicleType::Suv || type == VehicleType::Hatchback || type == VehicleType::Armored ||
           type == VehicleType::Sedan || type == VehicleType::Compact;
}

// Тээврийн хэрэгслийн онцлог. Хурд нь доорх үзүүлэлт ба газрын гадаргуу, налуугаас хамаарна.
struct VehicleSpec
{
    const char* name;
    float topSpeed;     // засмал зам дээрх дээд хурд, м/с
    float acceleration; // м/с^2
    float braking;      // м/с^2
    float reverseSpeed; // ухрах дээд хурд, м/с
    float turnRate;     // бага хурдтай үеийн эргэлт, рад/с
    float radius;       // мөргөлдөөний радиус, м
    float onAsphalt;    // гадаргуу тус бүрд дээд хурдны хэдэн хувь гарах (0..1)
    float onDirt;
    float onSteppe;
    float slopePenalty; // өгсүүр налуу хурдыг хэр их бууруулах (их байх тусам толгодод муу)
    float ramDamage;    // зомбийг дайрахад хурдны нэгж тутамд учруулах хохирол
    bool enclosed;      // бүхээгтэй: жолооч харагдахгүй, буудаж чадахгүй
    Vector3 seat;       // жолоочийн ташааны орон нутгийн байрлал
    float cameraExtra;  // унасан үед камерыг холдуулах зай
    float maxHealth;    // бүрэн бүтэн байдал (морьд: амь)
    float fuelRange;    // дүүрэн савтай явах зай, км (0 = бензин хэрэггүй: морь)
    float mass;         // мөргөлдөөнд: хүнд нь хөнгөнийг түлхэнэ (жийп = 1.6)
};

const VehicleSpec& SpecFor(VehicleType type);

struct Vehicle
{
    VehicleType type;
    Vector3 position; // газар дээрх төвийн цэг
    float heading;    // радиан, 0 = +Z
    float speed = 0.0f;     // урагш эерэг, м/с
    float pitch = 0.0f;     // газрын налууг дагасан хазайлт, радиан
    float roll = 0.0f;      // хажуугийн налууг дагасан хазайлт (дугуйтай машин), радиан; эерэг = зүүн (+X) тал дээш
    float animation = 0.0f; // дугуйн эргэлт (радиан, 1 м радиустай дугуйнд) / морины давхилтын үе
    float steer = 0.0f;     // жолооны байрлал -1..1 (эерэг = баруун тийш), зөөлөн шилжинэ
    // Эвдрэл, бензин: мөргөлт бүрд гэмтэнэ, 0 болбол эвдэрч (морь үхэж) дахин унах боломжгүй.
    float health = 100.0f;
    float fuel = 1.0f;          // 0..1 (сав дүүрэн = 1)
    bool wrecked = false;
    double wreckedAt = 0.0;   // эвдэрсэн (морь үхсэн) мөч: унах хөдөлгөөнд
    Vector2 knock = { 0.0f, 0.0f }; // мөргүүлж хажуу тийш шидэгдэх хурд (м/с), аажим унтарна
    float spin = 0.0f;              // мөргүүлсний дараах эргэлт (рад/с)
    float hurtTimer = 0.0f;         // сүүлд гэмтсэнээс хойш (HUD анивчих)
    float slip = 0.0f;              // хажуу тийш гулсах хурд (м/с): дрифт, шарвалт
    bool drifting = false;          // тоормос + эргэлтээр шарвуулж байна
    float driftTimer = 0.0f;        // шарвалт дууссанаас хойш хий дарж үргэлжлүүлэх хугацаа
    // Хамтарсан тоглолт: нөгөө тоглогч (эсвэл хостын баг) жолоодож буй хэрэгслийг сүлжээгээр хөдөлгөнө.
    bool remote = false;    // байрлалыг сүлжээнээс авна (өөрөө тооцохгүй)
    bool occupied = false;  // нөгөө тал сууж яваа: энэ талын тоглогч, баг сууж болохгүй
    Vector3 netPosition = { 0.0f, 0.0f, 0.0f };
    float netHeading = 0.0f, netSpeed = 0.0f, netSteer = 0.0f;
    float netAge = 0.0f; // сүүлийн төлөв ирснээс хойших хугацаа (хурдаар нь урагш таамаглана)
    // Нисдэг тэрэг
    bool lift = false;        // нисгэгч "өндөр авах" гэсэн (false: газардана)
    float rotorSpeed = 0.0f;  // 0..1: сэнс эргэлт (хөдөлгүүр асаахад аажим эргэж эхэлнэ)
    float rotor = 0.0f;       // сэнсний далавчны өнцөг, радиан
    float climb = 0.0f;       // босоо хурд, м/с
    float holdY = 0.0f;       // нисдэг тэрэг: барьж буй нислэгийн өндөр (дэлхийн y; 0 = газар дээр)
    float altitude = 0.0f;    // доорх газар (барилгын орой) дээрх өндөр, м
    bool crashed = false;     // агаарт эвдэрч унаад газар мөргөсөн (Game: дэлбэрэлт, суусан хүмүүс)
    float crashSeverity = 0.0f; // сүйрлийн хүч 0.2..1 (унасан өндөр, мөргөсөн хурдаар): суусан хүмүүсийн хохирол
    // Хуягт машин: дээврийн пулемётын харах зүг (дэлхийн, радиан), буудагч (тоглогч, AI) онилсон тийш
    float turretYaw = 0.0f;
    float turretPitch = 0.0f;  // хошууны өндөр (радиан, + дээш)
    float turretRecoil = 0.0f; // 1 = дөнгөж буудсан (амны гал)
};

inline bool IsHelicopter(VehicleType type) { return type == VehicleType::Helicopter; }

// Талбар дээрх бүх тээврийн хэрэгсэл ба тоглогчийн унаж буй нэг.
class Garage
{
public:
    void Clear();
    void Add(VehicleType type, Vector3 position, float heading);
    void Place(int index, Vector3 position, float heading)
    {
        vehicles[index].position = position;
        vehicles[index].heading = heading;
        vehicles[index].speed = 0.0f;
    }

    int Count() const { return (int)vehicles.size(); }
    // Хамтарсан тоглолт: сүлжээнээс ирсэн төлөв (зөөлөн дагана). occupied: нөгөө тал сууж яваа.
    void SetRemote(int index, Vector3 position, float heading, float speed, float steer, bool occupied);
    // Хамтарсан тоглолт (зочин): хостын жагсаалттай тааруулна. Энэ дугаарт өөр төрлийн унаа байвал (хоёр тал
    // байрлуулахдаа зөрсөн: нурсан хашаа, зогссон машин) хостынхоор сольж, дутуу бол нэмнэ.
    void MatchRemote(int index, VehicleType type, Vector3 position, float heading);
    void ClearRemote(int index);
    // Хамтарсан тоглолт: унааг жолоодож буй тал тооцсон эрүүл мэнд (0 бол эвдэрнэ / морь үхнэ).
    // Өөрийн жолоодож буй унаанд үйлчлэхгүй.
    void SyncHealth(int index, float health);
    const Vehicle& At(int index) const { return vehicles[index]; }
    // Хашаа нураахад хурд алдана
    void Slow(int index, float factor) { vehicles[index].speed *= factor; }

    // position-д хамгийн ойр, maxDistance доторх хэрэгсэл; -1 = байхгүй.
    int Nearest(Vector3 position, float maxDistance) const;

    void Mount(int index) { driven = index; }
    // Хуягт машины пулемётын амсар (дэлхийн байрлал): сумны мөр, амны гал эндээс
    Vector3 TurretMuzzle(int index) const;
    // Пулемётын бариул (дэлхийн байрлал): side 0 баруун, 1 зүүн (буудагчийн гар)
    Vector3 TurretGrip(int index, int side) const;
    // Хуягт машины пулемётыг эргүүлнэ: машины урагшаас ±kArmorTraverse (буудагч ард нь нэг байрандаа зогсоно),
    // хошуу -25..+35 градус; буудсан бол амны гал
    void AimTurret(int index, float yaw, float pitch, bool fired);
    void Dismount();
    // Нисдэг тэрэг: өндөр авах (true) эсвэл газардах (false)
    void SetLift(int index, bool up) { vehicles[(size_t)index].lift = up; }
    // Агаарт байна (буух, явган хүн дундуур нь гарах боломжгүй)
    bool Airborne(int index) const { return vehicles[(size_t)index].altitude > 1.0f; }
    // Нисдэг тэргийн суудал (ташааны ертөнцийн байрлал): 0, 1 хаалганы буудагч (зүүн, баруун), 2..5 бүхээгт.
    // facing: суусан хүний харах зүг (хаалганы буудагч гадагш)
    Vector3 HelicopterSeat(int index, int slot, float& facing) const;
    // Нэг удаа: агаараас унаж газар мөргөсөн нисдэг тэрэг (-1 = байхгүй)
    int TakeCrash();
    int Driven() const { return driven; } // -1 = явган
    bool IsDriving() const { return driven >= 0; }

    // Багийн гишүүдийн жолоодож буй хэрэгсэл (-1 = байхгүй) ба түүний удирдлага (AI).
    void SetEscort(int index) { escort = index; }
    int Escort() const { return escort; }
    void SetEscortInput(float throttle, float steer)
    {
        escortThrottle = throttle;
        escortSteer = steer;
    }
    // index хэрэгслийн суудлын (ташааны) ертөнцийн байрлал. passenger: жолоочийн ард / хажууд / тэвшинд.
    Vector3 SeatWorld(int index, bool passenger) const;
    // Жолоочийн зүүн гарын барих цэг (ертөнцөд): мотоцикл, гурван дугуйтын зүүн бариул (жолоо эргэхэд дагана),
    // морины хазаар. hasRein = false: бүхээгтэй унаа (гар харагдахгүй).
    Vector3 ReinWorld(int index, bool& hasRein) const;

    // throttle: -1..1 (S..W), steer: -1..1 (A..D). Унаагүй хэрэгсэл аяндаа зогсоно.
    // Хурдтай явахдаа зомбийг дайрч гэмтээнэ. boost (Shift): морь давхина (үгүй бол хатирна).
    void Update(float dt, const World& world, ZombieHorde& horde, float throttle, float steer, bool handbrake,
                bool boost = false);

    // Шатахуун түгээх газар: бензин нэмж (0..1 хувиар), засварлана. Нэмсэн хэмжээг буцаана.
    float Refuel(int index, float amount);
    float Repair(int index, float amount);
    // Гадны хохирол (дэлбэрэлт г.м.). Нөгөө тоглогчийн жолоодож буй (remote) унааг тэр тал өөрөө тооцно.
    void Damage(int index, float amount);

    // Сум: туяа хэрэгслийн биеийн хайрцагт (орон нутгийн тэнхлэгээр эргүүлсэн) тусах эсэх. skipA, skipB: тоглогчийн
    // өөрийн / багийнх (буудахгүй). Эвдэрсэн хэрэгсэл ч сумыг халхална. -1 = тусаагүй.
    int Raycast(Ray ray, float minDistance, float maxDistance, int skipA, int skipB, float& distance) const;

    // Явган хүнийг зогсож буй хэрэгсэл дундуур гарахаас сэргийлнэ.
    void PushOut(Vector3& position, float radius) const;

    // Унаж буй хэрэгслийн төлөв (HUD-д)
    Surface DrivenSurface() const { return drivenSurface; }
    float DrivenMaxSpeed() const { return drivenMaxSpeed; } // одоогийн гадаргуу, налуу дээрх дээд хурд
    Vector3 SeatPosition() const;                           // жолоочийн ташааны ертөнцийн байрлал

    // material: орчны PropMaterial (унааны mesh-ийг GPU-д хадгалж зурна); nullptr бол шууд зурна.
    // shadowPass: сүүдрийн зураг (material нь гүний материал; текстуртай загварууд ч түүгээр зурагдана)
    void Draw(Vector3 viewPosition, float maxDistance, const Material* material = nullptr, bool shadowPass = false) const;

private:
    void Collide(int index, const World& world);

    void UpdateHelicopter(int index, float dt, const World& world, float throttle, float steer);

    std::vector<Vehicle> vehicles;
    int crashed = -1;
    int driven = -1;
    int escort = -1;
    float escortThrottle = 0.0f, escortSteer = 0.0f;
    Surface drivenSurface = Surface::Steppe;
    float drivenMaxSpeed = 0.0f;
};
