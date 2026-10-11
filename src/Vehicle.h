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
    Horse       // морь: зам хамаарахгүй, толгод сайн давна
};

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
};

const VehicleSpec& SpecFor(VehicleType type);

struct Vehicle
{
    VehicleType type;
    Vector3 position; // газар дээрх төвийн цэг
    float heading;    // радиан, 0 = +Z
    float speed = 0.0f;     // урагш эерэг, м/с
    float pitch = 0.0f;     // газрын налууг дагасан хазайлт, радиан
    float animation = 0.0f; // дугуйн эргэлт / морины давхилтын үе
};

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
    const Vehicle& At(int index) const { return vehicles[index]; }

    // position-д хамгийн ойр, maxDistance доторх хэрэгсэл; -1 = байхгүй.
    int Nearest(Vector3 position, float maxDistance) const;

    void Mount(int index) { driven = index; }
    void Dismount();
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

    // throttle: -1..1 (S..W), steer: -1..1 (A..D). Унаагүй хэрэгсэл аяндаа зогсоно.
    // Хурдтай явахдаа зомбийг дайрч гэмтээнэ.
    void Update(float dt, const World& world, ZombieHorde& horde, float throttle, float steer, bool handbrake);

    // Явган хүнийг зогсож буй хэрэгсэл дундуур гарахаас сэргийлнэ.
    void PushOut(Vector3& position, float radius) const;

    // Унаж буй хэрэгслийн төлөв (HUD-д)
    Surface DrivenSurface() const { return drivenSurface; }
    float DrivenMaxSpeed() const { return drivenMaxSpeed; } // одоогийн гадаргуу, налуу дээрх дээд хурд
    Vector3 SeatPosition() const;                           // жолоочийн ташааны ертөнцийн байрлал

    void Draw(Vector3 viewPosition, float maxDistance) const;

private:
    std::vector<Vehicle> vehicles;
    int driven = -1;
    int escort = -1;
    float escortThrottle = 0.0f, escortSteer = 0.0f;
    Surface drivenSurface = Surface::Steppe;
    float drivenMaxSpeed = 0.0f;
};
