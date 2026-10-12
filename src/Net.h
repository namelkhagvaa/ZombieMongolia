#pragma once

#include "raylib.h"

#include <cstdint>
#include <string>
#include <vector>

// Нэг Wi-Fi сүлжээн дэх хамтарсан тоглолт (co-op). Хост нь зомби, даалгаврыг тооцоолж, зочин нь өөрийн дүрийг
// хөдөлгөж, буудсан сумаа илгээнэ. ENet (UDP) ашиглана.
//
// Хайлт: даалгавар тоглож буй тоглоом бүр kDiscoveryPort дээр сонсож, асуултад хариулна. Зочин өөрийн сүлжээний
// бүх хаяг (x.x.x.1-254) болон broadcast руу асуулт илгээнэ (Android дээр broadcast найдваргүй тул).
namespace Net
{
    constexpr uint16_t kGamePort = 47810;
    constexpr uint16_t kDiscoveryPort = 47811;

    // Мессежийн төрөл (пакетийн эхний байт)
    enum class Msg : uint8_t
    {
        JoinRequest = 1, // зочин -> хост: нэр
        JoinReply,       // хост -> зочин: зөвшөөрсөн эсэх, аймаг
        GuestState,      // зочин -> хост: дүрийн байдал (тасралтгүй)
        GuestHit,        // зочин -> хост: зомбид оносон (id, хохирол, толгой)
        GuestBarrel,     // зочин -> хост: бензинтэй торхонд оносон (индекс, хохирол)
        GuestBlast,      // зочин -> хост: гранат, пуужингийн дэлбэрэлт (байрлал, радиус, хохирол)
        Snapshot,        // хост -> зочин: хост, цэргүүд, ойрын зомбинууд, даалгаврын бичиг
        GuestDamage,     // хост -> зочин: зомби цохисон
        KillReward,      // хост -> зочин: зочны устгасан зомбины зоос
        BarrelExploded,  // хост -> зочин: торх дэлбэрсэн
        MissionWon,      // хост -> зочин: аймаг чөлөөлөгдсөн
        Leave            // аль аль нь: тоглолтоос гарсан
    };

    // Хоёр талын дүрийн байдал (буудлага, хөдөлгөөн)
    struct AvatarState
    {
        Vector3 position = { 0.0f, 0.0f, 0.0f };
        float facing = 0.0f;
        float aimPitch = 0.0f;
        float speed = 0.0f;
        uint8_t held = 0;
        uint8_t member = 0;  // гадаад төрх (багийн гишүүний дугаар)
        uint8_t flags = 0;   // kAlive, kCrouch, kProne, kAiming, kFired, kRiding
        float health = 100.0f;
    };
    constexpr uint8_t kAlive = 1, kCrouch = 2, kProne = 4, kAiming = 8, kFired = 16, kRiding = 32;

    struct ZombieState
    {
        uint32_t id = 0;
        uint8_t type = 0;
        uint8_t flags = 0;   // kZombieAlive, kZombieBoss, kZombieAttack
        Vector3 position = { 0.0f, 0.0f, 0.0f };
        float facing = 0.0f;
    };
    constexpr uint8_t kZombieAlive = 1, kZombieBoss = 2, kZombieAttack = 4;

    struct SoldierState
    {
        Vector3 position = { 0.0f, 0.0f, 0.0f };
        float facing = 0.0f;
        float speed = 0.0f;
        int8_t member = -1;
        uint8_t alive = 1;
    };

    struct Snapshot
    {
        AvatarState host;
        std::vector<SoldierState> soldiers;
        std::vector<ZombieState> zombies;
        std::string objective, detail;
        int hordeSmall = 0, hordeBig = 0;
        float zombieSpeed = 1.0f;
    };

    // Хайлтаар олдсон хост
    struct FoundGame
    {
        std::string name;
        uint32_t address = 0; // IPv4 (ENet-ийн хэлбэр)
        int province = 0;
        float age = 0.0f;     // сүүлд хариулснаас хойш
    };

    // Нэг холболтын хооронд солилцох үйл явдлууд (Game Poll()-оор авна)
    struct Event
    {
        Msg type;
        std::string text;      // JoinRequest: нэр
        bool accepted = false; // JoinReply
        int province = 0;      // JoinReply
        AvatarState avatar;    // GuestState
        uint32_t id = 0;       // GuestHit: зомби, GuestBarrel / BarrelExploded: торх
        float amount = 0.0f;   // хохирол, зоос
        bool headshot = false;
        Vector3 position = { 0.0f, 0.0f, 0.0f }; // GuestBlast, KillReward
        float radius = 0.0f;
    };

    enum class Role
    {
        None,
        Host,   // даалгавар тоглож байна, хүсэлт хүлээн авна (холбогдсон зочинтой эсвэл үгүй)
        Guest   // өөр хүний тоглолтод нэгдсэн / нэгдэх хүсэлт илгээсэн
    };

    class Session
    {
    public:
        Session();
        ~Session();
        Session(const Session&) = delete;
        Session& operator=(const Session&) = delete;

        bool Available() const { return initialized; }
        Role GetRole() const { return role; }
        bool Connected() const { return peer != nullptr && connected; }
        const std::string& LocalName() const { return name; }
        void SetName(const std::string& value) { name = value; }

        // Хост: даалгаврын үед сонсож эхэлнэ (хайлтад хариулна, хүсэлт хүлээнэ).
        void StartHosting(int province);
        // Хост: хүсэлтэд хариулна.
        void AnswerJoin(bool accept, int province);
        // Зочин: олдсон хост руу нэгдэх хүсэлт илгээнэ.
        void RequestJoin(const FoundGame& game);
        // Бүгдийг хааж, холболтыг таслана.
        void Stop();

        // Зочин (газрын зургийн дэлгэц): ойролцоох хостуудыг хайна.
        void Discover(float dt);
        const std::vector<FoundGame>& Found() const { return found; }
        void StopDiscovery();

        // Сүлжээний ажил: ирсэн мессежүүдийг events-д нэмнэ.
        void Poll(std::vector<Event>& events);

        // Илгээх
        void SendGuestState(const AvatarState& state);
        void SendHit(uint32_t zombieId, float damage, bool headshot);
        void SendBarrelHit(int index, float damage);
        void SendBlast(Vector3 position, float radius, float damage);
        void SendSnapshot(const Snapshot& snapshot);
        void SendGuestDamage(float amount);
        void SendKillReward(int coins, Vector3 position);
        void SendBarrelExploded(int index);
        void SendMissionWon(int reward);

        // Хүлээн авсан сүүлийн snapshot (зочин)
        bool TakeSnapshot(Snapshot& out);

    private:
        void Send(const std::vector<uint8_t>& data, bool reliable);
        void HandleDiscovery();
        void HandlePacket(const uint8_t* data, size_t size, std::vector<Event>& events);

        bool initialized = false;
        Role role = Role::None;
        std::string name;
        int hostedProvince = 0;
        void* host = nullptr;      // ENetHost*
        void* peer = nullptr;      // ENetPeer*
        bool connected = false;
        bool joinPending = false;  // зочин: хүсэлт илгээсэн, хариу хүлээж байна
        intptr_t discoverySocket = -1;
        float probeTimer = 0.0f;
        uint32_t localAddress = 0;
        std::vector<FoundGame> found;
        Snapshot latest;
        bool hasLatest = false;
    };
}
