#include "Vehicle.h"

#include "Humanoid.h"
#include "Mover.h"
#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>

const VehicleSpec& SpecFor(VehicleType type)
{
    //                name          top   acc   brake rev  turn  rad   asph  dirt  steppe slope ram   encl   seat                   cam
    static const VehicleSpec specs[] = {
        /* Jeep  */ { "ЖИЙП",       30.0f, 9.0f, 20.0f, 7.0f, 1.9f, 1.5f, 1.00f, 0.70f, 0.45f, 1.6f, 9.0f, true,  { 0.0f, 1.0f, 0.0f },   4.0f },
        /* Moto  */ { "МОТОЦИКЛ", 26.0f, 11.0f, 18.0f, 3.0f, 2.4f, 0.7f, 1.00f, 0.80f, 0.60f, 1.2f, 3.0f, false, { 0.0f, 0.86f, -0.22f }, 1.5f },
        /* Trike */ { "ГУРВАН ДУГУЙТ",      17.0f, 6.0f, 14.0f, 4.0f, 1.8f, 1.0f, 1.00f, 0.85f, 0.70f, 1.4f, 4.5f, false, { 0.0f, 0.92f, 0.05f },  2.0f },
        /* Horse */ { "МОРЬ",      13.0f, 8.0f, 12.0f, 2.0f, 2.8f, 0.8f, 0.85f, 1.00f, 0.95f, 0.6f, 2.0f, false, { 0.0f, 1.5f, -0.05f },  1.5f },
    };
    return specs[(int)type];
}

namespace
{
    const Color kTire = { 26, 26, 28, 255 };
    const Color kRim = { 120, 122, 126, 255 };
    const Color kChrome = { 150, 154, 160, 255 };
    const Color kSeat = { 30, 28, 28, 255 };

    void Cube(float x, float y, float z, float sx, float sy, float sz, Color color)
    {
        DrawCubeV({ x, y, z }, { sx, sy, sz }, color);
    }

    Color ScaleColorDark(Color color)
    {
        return { (unsigned char)(color.r * 3 / 4), (unsigned char)(color.g * 3 / 4), (unsigned char)(color.b * 3 / 4),
                 color.a };
    }

    // Тэнхлэг нь X дагуу байрлах дугуй. spin: эргэлтийн өнцөг (хигээс нь эргэж харагдана).
    void Wheel(float x, float y, float z, float radius, float width, float spin)
    {
        DrawCylinderEx({ x - width * 0.5f, y, z }, { x + width * 0.5f, y, z }, radius, radius, 10, kTire);
        DrawCylinderEx({ x - width * 0.52f, y, z }, { x + width * 0.52f, y, z }, radius * 0.5f, radius * 0.5f, 8, kRim);

        // Хигээс: дугуйтай хамт эргэнэ
        rlPushMatrix();
        rlTranslatef(x, y, z);
        rlRotatef(spin * RAD2DEG, 1.0f, 0.0f, 0.0f);
        Cube(0.0f, 0.0f, 0.0f, width * 1.08f, radius * 1.5f, radius * 0.14f, kRim);
        Cube(0.0f, 0.0f, 0.0f, width * 1.08f, radius * 0.14f, radius * 1.5f, kRim);
        rlPopMatrix();
    }

    void DrawJeep(const Vehicle& v)
    {
        const Color body = { 78, 92, 60, 255 };   // цэргийн ногоон
        const Color top = { 126, 116, 86, 255 };  // брезент дээвэр
        const Color glass = { 40, 50, 58, 255 };
        const float spin = v.animation;

        Cube(0.0f, 0.85f, 0.0f, 1.75f, 0.55f, 3.7f, body);               // их бие
        Cube(0.0f, 1.2f, 1.25f, 1.6f, 0.2f, 1.1f, body);                 // хөдөлгүүрийн таг
        Cube(0.0f, 1.5f, -0.45f, 1.66f, 0.72f, 2.3f, top);               // бүхээг
        Cube(0.0f, 1.52f, 0.72f, 1.5f, 0.5f, 0.06f, glass);              // урд шил
        Cube(0.84f, 1.55f, -0.2f, 0.04f, 0.42f, 1.5f, glass);            // хажуугийн цонх
        Cube(-0.84f, 1.55f, -0.2f, 0.04f, 0.42f, 1.5f, glass);
        Cube(0.0f, 0.62f, 1.9f, 1.8f, 0.18f, 0.14f, kChrome);            // урд бампер
        Cube(0.0f, 0.62f, -1.9f, 1.8f, 0.18f, 0.14f, kChrome);
        Cube(0.55f, 1.02f, 1.86f, 0.3f, 0.2f, 0.06f, { 236, 230, 190, 255 }); // гэрэл
        Cube(-0.55f, 1.02f, 1.86f, 0.3f, 0.2f, 0.06f, { 236, 230, 190, 255 });
        Cube(0.0f, 0.98f, 1.87f, 0.6f, 0.24f, 0.04f, { 30, 30, 30, 255 });    // радиаторын тор
        DrawCylinderEx({ 0.0f, 1.25f, -1.9f }, { 0.0f, 1.25f, -2.08f }, 0.4f, 0.4f, 10, kTire); // нөөц дугуй

        for (float x : { -0.86f, 0.86f })
        {
            for (float z : { -1.2f, 1.2f })
            {
                Wheel(x, 0.42f, z, 0.42f, 0.28f, spin);
                Cube(x, 0.9f, z, 0.34f, 0.08f, 1.0f, ScaleColorDark(body)); // дугуйн хаалт
            }
        }
    }

    void DrawMotorcycleFront(float spin, Color paint)
    {
        // Урд дугуй, сэрээ, жолоо, гэрэл: мотоцикл, гурван дугуйт хоёрт ижил
        Wheel(0.0f, 0.34f, 0.72f, 0.34f, 0.12f, spin);
        Cube(-0.09f, 0.72f, 0.66f, 0.04f, 0.8f, 0.05f, kChrome); // сэрээ
        Cube(0.09f, 0.72f, 0.66f, 0.04f, 0.8f, 0.05f, kChrome);
        Cube(0.0f, 0.7f, 0.72f, 0.2f, 0.06f, 0.5f, paint);       // шаврын хаалт
        Cube(0.0f, 1.12f, 0.56f, 0.72f, 0.05f, 0.05f, kChrome);  // жолоо
        Cube(-0.34f, 1.12f, 0.54f, 0.1f, 0.07f, 0.07f, kSeat);
        Cube(0.34f, 1.12f, 0.54f, 0.1f, 0.07f, 0.07f, kSeat);
        Cube(0.0f, 1.0f, 0.72f, 0.2f, 0.2f, 0.14f, kChrome);     // гэрэл
        Cube(0.0f, 1.0f, 0.8f, 0.15f, 0.15f, 0.02f, { 236, 230, 190, 255 });
    }

    void DrawMotorcycle(const Vehicle& v)
    {
        const Color paint = { 150, 36, 32, 255 };
        DrawMotorcycleFront(v.animation, paint);

        Wheel(0.0f, 0.34f, -0.72f, 0.34f, 0.14f, v.animation);
        Cube(0.0f, 0.5f, 0.0f, 0.3f, 0.34f, 0.5f, { 60, 62, 66, 255 });  // хөдөлгүүр
        Cube(0.0f, 0.86f, 0.18f, 0.3f, 0.24f, 0.56f, paint);              // түлшний сав
        Cube(0.0f, 0.84f, -0.36f, 0.3f, 0.1f, 0.66f, kSeat);              // суудал
        Cube(0.0f, 0.62f, -0.4f, 0.1f, 0.1f, 0.9f, kChrome);              // арын рам
        Cube(0.0f, 0.74f, -0.76f, 0.2f, 0.06f, 0.46f, paint);             // арын шаврын хаалт
        Cube(0.17f, 0.36f, -0.5f, 0.08f, 0.08f, 0.8f, kChrome);           // яндан
    }

    void DrawTrike(const Vehicle& v)
    {
        const Color paint = { 40, 84, 140, 255 };
        const Color cargo = { 52, 96, 150, 255 };
        DrawMotorcycleFront(v.animation, paint);

        Wheel(-0.62f, 0.34f, -0.85f, 0.34f, 0.16f, v.animation);
        Wheel(0.62f, 0.34f, -0.85f, 0.34f, 0.16f, v.animation);
        Cube(0.0f, 0.5f, 0.05f, 0.3f, 0.34f, 0.5f, { 60, 62, 66, 255 });  // хөдөлгүүр
        Cube(0.0f, 0.86f, 0.24f, 0.3f, 0.24f, 0.5f, paint);               // түлшний сав
        Cube(0.0f, 0.9f, -0.12f, 0.36f, 0.1f, 0.4f, kSeat);               // суудал
        Cube(0.0f, 0.48f, -0.85f, 1.1f, 0.1f, 1.2f, { 50, 52, 56, 255 }); // арал
        // Ачааны тэвш: дөрвөн хана
        Cube(0.0f, 0.62f, -0.9f, 1.3f, 0.06f, 1.2f, cargo);
        Cube(-0.64f, 0.84f, -0.9f, 0.05f, 0.4f, 1.2f, cargo);
        Cube(0.64f, 0.84f, -0.9f, 0.05f, 0.4f, 1.2f, cargo);
        Cube(0.0f, 0.84f, -1.48f, 1.3f, 0.4f, 0.05f, cargo);
        Cube(0.0f, 0.9f, -0.32f, 1.3f, 0.5f, 0.05f, cargo);
        Cube(0.25f, 0.8f, -0.9f, 0.5f, 0.3f, 0.5f, { 130, 106, 80, 255 }); // ачаа
    }

    void DrawHorse(const Vehicle& v)
    {
        const Color coat = { 122, 82, 50, 255 };
        const Color dark = { 44, 32, 26, 255 };
        const Color saddle = { 150, 40, 36, 255 };
        const float gait = std::min(1.0f, std::fabs(v.speed) / 4.0f);
        const float bounce = std::sin(v.animation * 2.0f) * 0.05f * gait;

        rlPushMatrix();
        rlTranslatef(0.0f, bounce, 0.0f);

        Cube(0.0f, 1.18f, 0.0f, 0.46f, 0.5f, 1.4f, coat);        // их бие
        Cube(0.0f, 1.22f, 0.6f, 0.5f, 0.54f, 0.36f, coat);       // цээж
        Cube(0.0f, 1.2f, -0.6f, 0.5f, 0.52f, 0.36f, coat);       // хондлой

        // Хүзүү, толгой: урагш дээш хазайсан
        rlPushMatrix();
        rlTranslatef(0.0f, 1.35f, 0.7f);
        rlRotatef(38.0f - gait * 10.0f, 1.0f, 0.0f, 0.0f);
        Cube(0.0f, 0.3f, 0.0f, 0.26f, 0.7f, 0.34f, coat);
        Cube(0.0f, 0.42f, -0.2f, 0.07f, 0.56f, 0.08f, dark);     // дэл
        rlTranslatef(0.0f, 0.66f, 0.04f);
        rlRotatef(52.0f, 1.0f, 0.0f, 0.0f);
        Cube(0.0f, 0.2f, 0.0f, 0.22f, 0.5f, 0.24f, coat);        // толгой
        Cube(0.0f, 0.42f, 0.0f, 0.18f, 0.12f, 0.2f, dark);       // хошуу
        Cube(-0.08f, 0.02f, -0.12f, 0.05f, 0.05f, 0.12f, coat);  // чих
        Cube(0.08f, 0.02f, -0.12f, 0.05f, 0.05f, 0.12f, coat);
        rlPopMatrix();

        // Сүүл
        rlPushMatrix();
        rlTranslatef(0.0f, 1.36f, -0.78f);
        rlRotatef(-28.0f - gait * 30.0f, 1.0f, 0.0f, 0.0f);
        Cube(0.0f, -0.3f, 0.0f, 0.1f, 0.62f, 0.1f, dark);
        rlPopMatrix();

        // Монгол эмээл, тохом
        Cube(0.0f, 1.45f, -0.05f, 0.56f, 0.05f, 0.7f, { 36, 64, 120, 255 });
        Cube(0.0f, 1.5f, -0.05f, 0.4f, 0.08f, 0.46f, saddle);
        Cube(0.0f, 1.6f, 0.2f, 0.34f, 0.18f, 0.06f, saddle);     // урд бүүрэг
        Cube(0.0f, 1.6f, -0.3f, 0.34f, 0.18f, 0.06f, saddle);    // хойд бүүрэг

        // Хөл: урд, хойд хос ээлжлэн савлана
        const float swing = 34.0f * gait;
        const struct { float x, z, phase; } legs[] = {
            { -0.16f, 0.58f, 0.0f }, { 0.16f, 0.58f, 0.5f }, { -0.16f, -0.6f, 0.6f }, { 0.16f, -0.6f, 0.1f },
        };
        for (const auto& leg : legs)
        {
            const float angle = std::sin(v.animation + leg.phase * 2.0f * PI) * swing;
            const float knee = std::max(0.0f, std::cos(v.animation + leg.phase * 2.0f * PI)) * swing * 0.9f;
            rlPushMatrix();
            rlTranslatef(leg.x, 0.98f - bounce, leg.z);
            rlRotatef(-angle, 1.0f, 0.0f, 0.0f);
            Cube(0.0f, -0.24f, 0.0f, 0.15f, 0.5f, 0.17f, coat);
            rlTranslatef(0.0f, -0.48f, 0.0f);
            rlRotatef(knee, 1.0f, 0.0f, 0.0f);
            Cube(0.0f, -0.22f, 0.0f, 0.1f, 0.44f, 0.11f, coat);
            Cube(0.0f, -0.46f, 0.01f, 0.13f, 0.08f, 0.15f, dark); // туурай
            rlPopMatrix();
        }

        rlPopMatrix();
    }
}

void Garage::Clear()
{
    vehicles.clear();
    driven = -1;
    escort = -1;
}

void Garage::Add(VehicleType type, Vector3 position, float heading)
{
    Vehicle vehicle = {};
    vehicle.type = type;
    vehicle.position = position;
    vehicle.heading = heading;
    vehicles.push_back(vehicle);
}

int Garage::Nearest(Vector3 position, float maxDistance) const
{
    int best = -1;
    float bestDistance = maxDistance;
    for (int i = 0; i < Count(); ++i)
    {
        const float distance = HorizontalDistance(position, vehicles[i].position) - SpecFor(vehicles[i].type).radius;
        if (distance < bestDistance)
        {
            best = i;
            bestDistance = distance;
        }
    }
    return best;
}

void Garage::Dismount()
{
    driven = -1;
}

Vector3 Garage::SeatPosition() const
{
    if (driven < 0)
    {
        return { 0.0f, 0.0f, 0.0f };
    }
    return SeatWorld(driven, false);
}

Vector3 Garage::SeatWorld(int index, bool passenger) const
{
    const Vehicle& vehicle = vehicles[index];
    Vector3 seat = SpecFor(vehicle.type).seat;
    if (passenger)
    {
        switch (vehicle.type)
        {
        case VehicleType::Jeep: seat = { -0.45f, 1.0f, 0.0f }; break;         // жолоочийн эсрэг талын суудал
        case VehicleType::Motorcycle: seat = { 0.0f, 0.9f, -0.62f }; break;   // сундлана
        case VehicleType::Trike: seat = { -0.22f, 0.98f, -1.0f }; break;      // ачааны тэвшин дээр
        case VehicleType::Horse: seat = { 0.0f, 1.5f, -0.52f }; break;        // сундлана
        }
    }
    else if (vehicle.type == VehicleType::Jeep)
    {
        seat.x = 0.45f; // жолоочийн суудал
    }
    if (vehicle.type == VehicleType::Horse)
    {
        // Морины давхилттай хамт дэгдэнэ.
        seat.y += std::sin(vehicle.animation * 2.0f) * 0.05f * std::min(1.0f, std::fabs(vehicle.speed) / 4.0f);
    }
    return HumanoidLocalToWorld(vehicle.position, vehicle.heading, seat);
}

void Garage::Update(float dt, const World& world, ZombieHorde& horde, float throttle, float steer, bool handbrake)
{
    for (int i = 0; i < Count(); ++i)
    {
        Vehicle& vehicle = vehicles[i];
        const VehicleSpec& spec = SpecFor(vehicle.type);
        const bool byPlayer = i == driven;
        const bool controlled = byPlayer || i == escort; // тоглогч эсвэл багийн гишүүн жолоодож байгаа
        const float steerInput = byPlayer ? steer : escortSteer;
        if (!controlled && vehicle.speed == 0.0f)
        {
            continue; // зогсож буй унаа: тооцох зүйлгүй (ертөнцөд олон унаа байгаа)
        }
        const float forwardX = std::sin(vehicle.heading), forwardZ = std::cos(vehicle.heading);

        // --- Газрын онцлог: гадаргуу ба явж буй чиглэл дэх налуу ---
        const Surface surface = world.SurfaceAt(vehicle.position.x, vehicle.position.z);
        const float surfaceFactor = surface == Surface::Asphalt ? spec.onAsphalt
                                  : surface == Surface::Dirt ? spec.onDirt : spec.onSteppe;

        const float reach = spec.radius + 0.6f;
        const float ahead = world.GroundHeight(vehicle.position.x + forwardX * reach, vehicle.position.z + forwardZ * reach);
        const float behind = world.GroundHeight(vehicle.position.x - forwardX * reach, vehicle.position.z - forwardZ * reach);
        const float grade = (ahead - behind) / (reach * 2.0f); // эерэг = өгсүүр (урагш явахад)

        // Өгсөхөд удааширч, уруудахад бага зэрэг хурдасна.
        const float direction = vehicle.speed >= 0.0f ? 1.0f : -1.0f;
        const float slopeFactor = std::clamp(1.0f - grade * direction * spec.slopePenalty, 0.25f, 1.15f);
        const float maxSpeed = spec.topSpeed * surfaceFactor * slopeFactor;

        // --- Хурд ---
        const float input = byPlayer ? throttle : controlled ? escortThrottle : 0.0f;
        if (byPlayer && handbrake)
        {
            vehicle.speed -= std::clamp(vehicle.speed, -spec.braking * 1.5f * dt, spec.braking * 1.5f * dt);
        }
        else if (input > 0.0f)
        {
            // Урагш: ухарч байсан бол эхлээд тоормослоно.
            vehicle.speed += (vehicle.speed < 0.0f ? spec.braking : spec.acceleration) * input * dt;
        }
        else if (input < 0.0f)
        {
            // Тоормос, зогссон бол ухрах
            vehicle.speed += (vehicle.speed > 0.5f ? spec.braking : spec.acceleration * 0.6f) * input * dt;
            vehicle.speed = std::max(vehicle.speed, -spec.reverseSpeed * surfaceFactor);
        }
        else
        {
            // Хий гишгээгүй: үрэлтээр аажим зогсоно
            const float friction = (3.0f + std::fabs(vehicle.speed) * 0.15f) * dt;
            vehicle.speed -= std::clamp(vehicle.speed, -friction, friction);
        }

        // Одоогийн газар зөвшөөрөх хурдаас давсан бол (замаас гарах, өгсөх) хурдан удааширна.
        if (vehicle.speed > maxSpeed)
        {
            vehicle.speed = std::max(maxSpeed, vehicle.speed - spec.braking * 0.8f * dt);
        }

        // --- Эргэлт: хурдтай үед л эргэнэ, хурд их байх тусам эргэлт өргөснө ---
        if (controlled && steerInput != 0.0f)
        {
            const float grip = std::clamp(vehicle.speed / 3.0f, -1.0f, 1.0f);
            const float rate = spec.turnRate / (1.0f + std::fabs(vehicle.speed) * 0.045f);
            vehicle.heading -= steerInput * rate * grip * dt; // D = баруун тийш
        }

        // --- Хөдөлгөөн ба мөргөлдөөн ---
        if (std::fabs(vehicle.speed) > 0.001f)
        {
            const Vector3 before = vehicle.position;
            const float step = vehicle.speed * dt;
            vehicle.position.x += std::sin(vehicle.heading) * step;
            vehicle.position.z += std::cos(vehicle.heading) * step;
            world.ResolveHorizontal(vehicle.position, spec.radius, 1.6f, 0.3f);

            // Саад мөргөсөн бол (төлөвлөснөөс хамаагүй бага явсан) хурдаа алдана.
            const float moved = HorizontalDistance(before, vehicle.position);
            if (moved < std::fabs(step) * 0.6f)
            {
                vehicle.speed *= 0.3f;
            }
        }

        // Газрын гадаргуу ба налууг дагана.
        vehicle.position.y = world.GroundHeight(vehicle.position.x, vehicle.position.z);
        vehicle.pitch += (std::atan(grade) - vehicle.pitch) * std::min(1.0f, 8.0f * dt);

        // Дугуй / хөлийн хөдөлгөөн
        vehicle.animation += vehicle.speed * dt * (vehicle.type == VehicleType::Horse ? 1.1f : 2.6f);

        if (byPlayer)
        {
            drivenSurface = surface;
            drivenMaxSpeed = maxSpeed;
        }
        if (controlled)
        {

            // Зомбийг дайрах. Цохилтын хүч = хурд x унааны жин (ramDamage).
            // Хүчтэй цохилт (жийп ~16 км/ц-аас дээш, мотоцикл ~48 км/ц-аас дээш) зомбийг агаарт шидэж ална;
            // сул цохилт зөвхөн гэмтээнэ. Морь хэзээ ч шидэх хүчгүй.
            const float speed = std::fabs(vehicle.speed);
            if (speed > 4.0f)
            {
                const float impact = speed * spec.ramDamage;
                const bool launch = impact > 40.0f;
                const float direction = vehicle.speed >= 0.0f ? 1.0f : -1.0f;
                const Vector3 push = { std::sin(vehicle.heading) * speed * direction, 0.0f,
                                       std::cos(vehicle.heading) * speed * direction };

                const int hit = horde.Ram(vehicle.position, spec.radius + 0.5f, impact * dt * 6.0f, launch, push);
                if (hit > 0)
                {
                    // Мөргөх бүрд унаа удааширна: шидсэн бол нэг удаагийн цохилт, үгүй бол үргэлжилсэн үрэлт
                    // Сул цохилтын үрэлт нь хэчнээн зомби байсан ч хязгаартай: бөөгнөрөл дунд ч унаа
                    // хурдаа авч, шидэх хүчтэй болж чадна.
                    vehicle.speed *= launch ? std::max(0.5f, 1.0f - 0.06f * (float)hit)
                                            : 1.0f - 0.5f * (float)std::min(hit, 2) * dt;
                }
            }
        }
    }
}

void Garage::PushOut(Vector3& position, float radius) const
{
    for (int i = 0; i < Count(); ++i)
    {
        if (i == driven) continue;

        const Vehicle& vehicle = vehicles[i];
        const float minDistance = SpecFor(vehicle.type).radius * 0.8f + radius;
        const float dx = position.x - vehicle.position.x;
        const float dz = position.z - vehicle.position.z;
        const float distance = std::sqrt(dx * dx + dz * dz);
        if (distance < minDistance && distance > 1e-4f)
        {
            position.x = vehicle.position.x + dx / distance * minDistance;
            position.z = vehicle.position.z + dz / distance * minDistance;
        }
    }
}

void Garage::Draw(Vector3 viewPosition, float maxDistance) const
{
    for (const Vehicle& vehicle : vehicles)
    {
        if (HorizontalDistance(viewPosition, vehicle.position) > maxDistance) continue;

        DrawBlobShadow(vehicle.position, SpecFor(vehicle.type).radius * 1.15f);

        rlPushMatrix();
        rlTranslatef(vehicle.position.x, vehicle.position.y, vehicle.position.z);
        rlRotatef(vehicle.heading * RAD2DEG, 0.0f, 1.0f, 0.0f);
        rlRotatef(-vehicle.pitch * RAD2DEG, 1.0f, 0.0f, 0.0f); // өгсүүрт хошуу дээшилнэ

        switch (vehicle.type)
        {
        case VehicleType::Jeep: DrawJeep(vehicle); break;
        case VehicleType::Motorcycle: DrawMotorcycle(vehicle); break;
        case VehicleType::Trike: DrawTrike(vehicle); break;
        case VehicleType::Horse: DrawHorse(vehicle); break;
        }
        rlPopMatrix();
    }
}
