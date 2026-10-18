#include "Net.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#else
// raylib-ийн Rectangle, CloseWindow, ShowCursor зэрэг нэр windows.h-тэй давхцахаас сэргийлнэ.
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#endif
#include <enet/enet.h>
#if defined(_WIN32)
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#endif
#endif

#include <algorithm>
#include <cstring>

namespace Net
{
    namespace
    {
        const char kProbe[4] = { 'Z', 'M', 'Q', '1' };
        const char kReply[4] = { 'Z', 'M', 'R', '1' };
        const float kProbeInterval = 1.5f;
        const float kForgetAfter = 5.0f;

#if !defined(__EMSCRIPTEN__)
        ENetHost* AsHost(void* p) { return static_cast<ENetHost*>(p); }
        ENetPeer* AsPeer(void* p) { return static_cast<ENetPeer*>(p); }
#endif

        // ---- Цуваа болгох ----
        struct Writer
        {
            std::vector<uint8_t> data;
            void U8(uint8_t v) { data.push_back(v); }
            void U16(uint16_t v) { Raw(&v, 2); }
            void U32(uint32_t v) { Raw(&v, 4); }
            void I32(int32_t v) { Raw(&v, 4); }
            void F(float v) { Raw(&v, 4); }
            void V3(Vector3 v) { F(v.x); F(v.y); F(v.z); }
            void Str(const std::string& s)
            {
                const uint16_t n = (uint16_t)std::min<size_t>(s.size(), 400);
                U16(n);
                data.insert(data.end(), s.begin(), s.begin() + n);
            }
            void Raw(const void* p, size_t n)
            {
                const uint8_t* b = static_cast<const uint8_t*>(p);
                data.insert(data.end(), b, b + n);
            }
        };
        struct Reader
        {
            const uint8_t* data;
            size_t size, at = 0;
            bool ok = true;
            bool Need(size_t n) { if (at + n > size) ok = false; return ok; }
            uint8_t U8() { if (!Need(1)) return 0; return data[at++]; }
            uint16_t U16() { uint16_t v = 0; Raw(&v, 2); return v; }
            uint32_t U32() { uint32_t v = 0; Raw(&v, 4); return v; }
            int32_t I32() { int32_t v = 0; Raw(&v, 4); return v; }
            float F() { float v = 0; Raw(&v, 4); return v; }
            Vector3 V3() { Vector3 v; v.x = F(); v.y = F(); v.z = F(); return v; }
            std::string Str()
            {
                const uint16_t n = U16();
                if (!Need(n)) return {};
                std::string s((const char*)data + at, n);
                at += n;
                return s;
            }
            void Raw(void* p, size_t n) { if (!Need(n)) { std::memset(p, 0, n); return; } std::memcpy(p, data + at, n); at += n; }
        };

        void WriteAvatar(Writer& w, const AvatarState& a)
        {
            w.V3(a.position); w.F(a.facing); w.F(a.aimPitch); w.F(a.speed);
            w.U8(a.held); w.U8(a.member); w.U8(a.flags); w.F(a.health);
            w.U8(a.shots); w.V3(a.shotEnd);
            w.U8((uint8_t)a.vehicle); w.V3(a.vehiclePosition); w.F(a.vehicleHeading); w.F(a.vehicleSpeed); w.F(a.vehicleSteer);
        }
        AvatarState ReadAvatar(Reader& r)
        {
            AvatarState a;
            a.position = r.V3(); a.facing = r.F(); a.aimPitch = r.F(); a.speed = r.F();
            a.held = r.U8(); a.member = r.U8(); a.flags = r.U8(); a.health = r.F();
            a.shots = r.U8(); a.shotEnd = r.V3();
            a.vehicle = (int8_t)r.U8(); a.vehiclePosition = r.V3(); a.vehicleHeading = r.F(); a.vehicleSpeed = r.F();
            a.vehicleSteer = r.F();
            return a;
        }

#if !defined(__EMSCRIPTEN__)
        // Ашиглах боломжгүй хаяг: 0.0.0.0, 127.x (loopback), 169.254.x (DHCP-гүй автомат хаяг).
        bool UsableAddress(uint32_t host)
        {
            const uint8_t first = (uint8_t)(host & 0xFF), second = (uint8_t)((host >> 8) & 0xFF);
            return host != 0 && first != 127 && !(first == 169 && second == 254);
        }

        // Бүх сүлжээний картын IPv4 хаяг (интернэтгүй дотоод сүлжээнд ч олдоно).
        std::vector<uint32_t> FindLocalAddresses();

        uint32_t FindLocalAddress()
        {
            // UDP сокетыг гадаад хаяг руу "холбож" (пакет илгээгдэхгүй) системийн сонгосон өөрийн хаягийг уншина.
            const ENetSocket socket = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
            if (socket == ENET_SOCKET_NULL) return 0;
            ENetAddress remote;
            enet_address_set_host_ip(&remote, "8.8.8.8");
            remote.port = 53;
            uint32_t result = 0;
            if (enet_socket_connect(socket, &remote) == 0)
            {
                ENetAddress local;
                if (enet_socket_get_address(socket, &local) == 0) result = local.host;
            }
            enet_socket_destroy(socket);
            return result;
        }
#endif
    }

#if !defined(__EMSCRIPTEN__)
    namespace
    {
        std::vector<uint32_t> FindLocalAddresses()
        {
            std::vector<uint32_t> result;
            auto add = [&](uint32_t host) {
                if (UsableAddress(host) && std::find(result.begin(), result.end(), host) == result.end()) result.push_back(host);
            };
            add(FindLocalAddress()); // интернэт рүү гарах зам (байвал эхэнд)
#if defined(_WIN32)
            char name[256] = {};
            if (gethostname(name, sizeof(name)) == 0)
            {
                addrinfo hints = {};
                hints.ai_family = AF_INET;
                addrinfo* list = nullptr;
                if (getaddrinfo(name, nullptr, &hints, &list) == 0)
                {
                    for (addrinfo* entry = list; entry; entry = entry->ai_next)
                    {
                        add(reinterpret_cast<sockaddr_in*>(entry->ai_addr)->sin_addr.s_addr);
                    }
                    freeaddrinfo(list);
                }
            }
#else
            ifaddrs* list = nullptr;
            if (getifaddrs(&list) == 0)
            {
                for (ifaddrs* entry = list; entry; entry = entry->ifa_next)
                {
                    if (!entry->ifa_addr || entry->ifa_addr->sa_family != AF_INET || !(entry->ifa_flags & IFF_UP)) continue;
                    add(reinterpret_cast<sockaddr_in*>(entry->ifa_addr)->sin_addr.s_addr);
                }
                freeifaddrs(list);
            }
#endif
            return result;
        }

    }

    Session::Session()
    {
        initialized = enet_initialize() == 0;
        if (!initialized) TraceLog(LOG_WARNING, "NET: enet_initialize failed, co-op disabled");
    }

    Session::~Session()
    {
        Stop();
        StopDiscovery();
        if (initialized) enet_deinitialize();
    }

    void Session::StartHosting(int province)
    {
        if (!initialized) return;
        hostedProvince = province;
        if (role == Role::Host && host) return;
        Stop();

        ENetAddress address;
        address.host = ENET_HOST_ANY;
        address.port = kGamePort;
        host = enet_host_create(&address, 2, 2, 0, 0);
        if (!host)
        {
            TraceLog(LOG_WARNING, "NET: could not open port %d (another game on this device?)", (int)kGamePort);
            return;
        }
        role = Role::Host;

        // Хайлтад хариулах сокет
        discoverySocket = (intptr_t)enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
        if ((ENetSocket)discoverySocket != ENET_SOCKET_NULL)
        {
            ENetAddress any;
            any.host = ENET_HOST_ANY;
            any.port = kDiscoveryPort;
            enet_socket_set_option((ENetSocket)discoverySocket, ENET_SOCKOPT_REUSEADDR, 1);
            enet_socket_set_option((ENetSocket)discoverySocket, ENET_SOCKOPT_NONBLOCK, 1);
            enet_socket_set_option((ENetSocket)discoverySocket, ENET_SOCKOPT_BROADCAST, 1);
            if (enet_socket_bind((ENetSocket)discoverySocket, &any) != 0)
            {
                enet_socket_destroy((ENetSocket)discoverySocket);
                discoverySocket = -1;
            }
        }
        TraceLog(LOG_INFO, "NET: hosting province %d on port %d", province, (int)kGamePort);
    }

    void Session::Stop()
    {
        if (peer && connected)
        {
            Writer w;
            w.U8((uint8_t)Msg::Leave);
            Send(w.data, true);
            enet_peer_disconnect(AsPeer(peer), 0);
            enet_host_flush(AsHost(host));
        }
        if (host) enet_host_destroy(AsHost(host));
        host = nullptr;
        peer = nullptr;
        connected = false;
        joinPending = false;
        hasLatest = false;
        if (role == Role::Host && (ENetSocket)discoverySocket != ENET_SOCKET_NULL && discoverySocket != -1)
        {
            enet_socket_destroy((ENetSocket)discoverySocket);
            discoverySocket = -1;
        }
        role = Role::None;
    }

    void Session::AnswerJoin(bool accept, int province)
    {
        if (!peer) return;
        Writer w;
        w.U8((uint8_t)Msg::JoinReply);
        w.U8(accept ? 1 : 0);
        w.U8((uint8_t)province);
        w.U16(kProtocolVersion);
        Send(w.data, true);
        if (accept)
        {
            connected = true;
        }
        else
        {
            enet_peer_disconnect_later(AsPeer(peer), 0);
            peer = nullptr;
            connected = false;
        }
    }

    void Session::RequestJoin(const FoundGame& game)
    {
        if (!initialized) return;
        Stop();
        host = enet_host_create(nullptr, 1, 2, 0, 0);
        if (!host) return;
        ENetAddress address;
        address.host = game.address;
        address.port = kGamePort;
        peer = enet_host_connect(AsHost(host), &address, 2, 0);
        role = Role::Guest;
        joinPending = true;
        connected = false;
    }

    void Session::StopDiscovery()
    {
        if (role != Role::Host && discoverySocket != -1)
        {
            enet_socket_destroy((ENetSocket)discoverySocket);
            discoverySocket = -1;
        }
        found.clear();
    }

    void Session::Discover(float dt)
    {
        if (!initialized || role == Role::Host) return;
        if (discoverySocket == -1)
        {
            const ENetSocket socket = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
            if (socket == ENET_SOCKET_NULL) return;
            ENetAddress any;
            any.host = ENET_HOST_ANY;
            any.port = 0;
            enet_socket_bind(socket, &any);
            enet_socket_set_option(socket, ENET_SOCKOPT_NONBLOCK, 1);
            enet_socket_set_option(socket, ENET_SOCKOPT_BROADCAST, 1);
            discoverySocket = (intptr_t)socket;
            localAddresses = FindLocalAddresses();
            probeTimer = 0.0f;
        }
        const ENetSocket socket = (ENetSocket)discoverySocket;

        for (FoundGame& game : found) game.age += dt;
        found.erase(std::remove_if(found.begin(), found.end(), [](const FoundGame& g) { return g.age > kForgetAfter; }),
                    found.end());

        probeTimer -= dt;
        if (probeTimer <= 0.0f)
        {
            probeTimer = kProbeInterval;
            ENetBuffer buffer;
            buffer.data = (void*)kProbe;
            buffer.dataLength = sizeof(kProbe);
            ENetAddress target;
            target.port = kDiscoveryPort;
            target.host = ENET_HOST_BROADCAST;
            enet_socket_send(socket, &target, &buffer, 1);
            for (uint32_t localAddress : localAddresses)
            {
                // Сүлжээний карт бүрийн дэд сүлжээний (/24) бүх хаягийг асууна: broadcast хаагдсан утсанд ч олдоно.
                for (uint32_t i = 1; i < 255; ++i)
                {
                    target.host = (localAddress & 0x00FFFFFFu) | (i << 24);
                    if (target.host == localAddress) continue;
                    enet_socket_send(socket, &target, &buffer, 1);
                }
                // Нэг төхөөрөмж дээр хоёр тоглоом (туршилт)
                target.host = localAddress;
                enet_socket_send(socket, &target, &buffer, 1);
            }
        }

        uint8_t data[512];
        for (int guard = 0; guard < 64; ++guard)
        {
            ENetAddress from;
            ENetBuffer buffer;
            buffer.data = data;
            buffer.dataLength = sizeof(data);
            const int received = enet_socket_receive(socket, &from, &buffer, 1);
            if (received <= 0) break;
            if (received < 6 || std::memcmp(data, kReply, 4) != 0) continue;
            Reader r{ data + 4, (size_t)received - 4 };
            const int province = r.U8();
            r.U8(); // зочинтой эсэх (одоогоор хэрэглэхгүй)
            const std::string hostName = r.Str();
            if (!r.ok) continue;
            auto existing = std::find_if(found.begin(), found.end(), [&](const FoundGame& g) { return g.address == from.host; });
            if (existing == found.end())
            {
                found.push_back({ hostName, from.host, province, 0.0f });
            }
            else
            {
                existing->name = hostName;
                existing->province = province;
                existing->age = 0.0f;
            }
        }
    }

    void Session::HandleDiscovery()
    {
        if (role != Role::Host || discoverySocket == -1) return;
        const ENetSocket socket = (ENetSocket)discoverySocket;
        uint8_t data[64];
        for (int guard = 0; guard < 32; ++guard)
        {
            ENetAddress from;
            ENetBuffer buffer;
            buffer.data = data;
            buffer.dataLength = sizeof(data);
            const int received = enet_socket_receive(socket, &from, &buffer, 1);
            if (received <= 0) break;
            if (received < 4 || std::memcmp(data, kProbe, 4) != 0) continue;
            Writer w;
            w.Raw(kReply, 4);
            w.U8((uint8_t)hostedProvince);
            w.U8(connected ? 1 : 0);
            w.Str(name);
            ENetBuffer reply;
            reply.data = w.data.data();
            reply.dataLength = w.data.size();
            enet_socket_send(socket, &from, &reply, 1);
        }
    }

    void Session::Poll(std::vector<Event>& events)
    {
        if (!initialized) return;
        HandleDiscovery();
        if (!host) return;

        ENetEvent event;
        for (int guard = 0; guard < 256 && enet_host_service(AsHost(host), &event, 0) > 0; ++guard)
        {
            switch (event.type)
            {
            case ENET_EVENT_TYPE_CONNECT:
                if (role == Role::Host)
                {
                    if (peer && AsPeer(peer) != event.peer)
                    {
                        enet_peer_disconnect_now(event.peer, 0); // нэг зочин л хүлээн авна
                    }
                    else
                    {
                        peer = event.peer; // хүсэлтийг хүлээнэ (JoinRequest)
                    }
                }
                else if (role == Role::Guest)
                {
                    Writer w;
                    w.U8((uint8_t)Msg::JoinRequest);
                    w.Str(name);
                    w.U16(kProtocolVersion);
                    Send(w.data, true);
                }
                break;
            case ENET_EVENT_TYPE_RECEIVE:
                HandlePacket(event.packet->data, event.packet->dataLength, events);
                enet_packet_destroy(event.packet);
                break;
            case ENET_EVENT_TYPE_DISCONNECT:
                if (AsPeer(peer) == event.peer)
                {
                    const bool wasActive = connected || joinPending || role == Role::Guest;
                    peer = nullptr;
                    connected = false;
                    if (wasActive)
                    {
                        Event e;
                        e.type = Msg::Leave;
                        events.push_back(e);
                    }
                    if (role == Role::Guest)
                    {
                        joinPending = false;
                    }
                }
                break;
            default:
                break;
            }
        }
    }

#else
    // ================================================================ Хөтөч: WebSocket relay (server/relay.js)
    // Хуудас (web/shell.html) нь Module.zmRelayUrl-д серверийн хаягийг өгнө. Ирсэн мессежүүд JS-ийн дараалалд
    // хуримтлагдаж, Poll нэг бүрчлэн авна.
    namespace
    {
        // Мессежийн дараалал дахь төрөл
        enum WebKind { kWebNone = 0, kWebOpen, kWebRoom, kWebPaired, kWebLeft, kWebError, kWebClosed, kWebData };

        EM_JS(int, WebRelayConfigured, (), {
            return (typeof Module.zmRelayUrl === 'string' && Module.zmRelayUrl.length > 0) ? 1 : 0;
        });

        EM_JS(void, WebOpen, (const char* query), {
            if (Module.zmWs) { try { Module.zmWs.close(); } catch (e) {} }
            Module.zmQueue = [];
            var queue = Module.zmQueue;
            var ws;
            try { ws = new WebSocket(Module.zmRelayUrl + UTF8ToString(query)); }
            catch (e) { queue.push({ k: 6 }); return; }
            ws.binaryType = 'arraybuffer';
            ws.onopen = function() { queue.push({ k: 1 }); };
            ws.onmessage = function(e) {
                if (typeof e.data === 'string') {
                    var m = {};
                    try { m = JSON.parse(e.data); } catch (err) {}
                    if (m.type === 'room') queue.push({ k: 2, v: m.code | 0 });
                    else if (m.type === 'paired') queue.push({ k: 3 });
                    else if (m.type === 'left') queue.push({ k: 4 });
                    else if (m.type === 'error') queue.push({ k: 5, v: m.reason === 'notfound' ? 1 : m.reason === 'full' ? 2 : 0 });
                } else {
                    queue.push({ k: 7, d: new Uint8Array(e.data) });
                }
            };
            ws.onclose = function() { queue.push({ k: 6 }); };
            Module.zmWs = ws;
        });

        EM_JS(void, WebClose, (), {
            if (Module.zmWs) { try { Module.zmWs.close(); } catch (e) {} }
            Module.zmWs = null;
            Module.zmQueue = [];
        });

        EM_JS(void, WebSend, (const uint8_t* data, int size, int reliable), {
            var ws = Module.zmWs;
            if (!ws || ws.readyState !== 1) return;
            // Тасралтгүй мессежүүдийг (snapshot, байдал) сүлжээ удаан үед хаяна: хоцрогдол хуримтлагдахгүй.
            if (!reliable && ws.bufferedAmount > 262144) return;
            ws.send(HEAPU8.slice(data, data + size));
        });

        // Дараалал дахь эхний мессежийн төрөл, утга; өгөгдлийн хэмжээг буцаана (авахгүй).
        EM_JS(int, WebPeek, (int* kind, int* value), {
            var queue = Module.zmQueue;
            if (!queue || queue.length === 0) { HEAP32[kind >> 2] = 0; return 0; }
            HEAP32[kind >> 2] = queue[0].k;
            HEAP32[value >> 2] = queue[0].v | 0;
            return queue[0].d ? queue[0].d.length : 0;
        });

        // Эхний мессежийг дарааллаас авч, өгөгдлийг нь buffer-т хуулна.
        EM_JS(void, WebPop, (uint8_t* buffer), {
            var item = Module.zmQueue.shift();
            if (item && item.d && buffer) HEAPU8.set(item.d, buffer);
        });

        const double kRetrySeconds = 5.0; // сервер хүрэхгүй бол хост ийм завсартай дахин оролдоно
    }

    Session::Session()
    {
        initialized = WebRelayConfigured() != 0;
        if (!initialized) TraceLog(LOG_INFO, "NET: no relay server configured, co-op disabled");
    }

    Session::~Session()
    {
        Stop();
    }

    void Session::StartHosting(int province)
    {
        if (!initialized) return;
        hostedProvince = province;
        if (role == Role::Host || GetTime() < retryAt) return;
        Stop();
        WebOpen("?host");
        role = Role::Host;
        roomCode = 0;
    }

    void Session::Stop()
    {
        if (partner && connected)
        {
            Writer w;
            w.U8((uint8_t)Msg::Leave);
            Send(w.data, true);
        }
        if (role != Role::None) WebClose();
        partner = false;
        connected = false;
        joinPending = false;
        hasLatest = false;
        roomCode = 0;
        role = Role::None;
    }

    void Session::AnswerJoin(bool accept, int province)
    {
        if (!partner) return;
        Writer w;
        w.U8((uint8_t)Msg::JoinReply);
        w.U8(accept ? 1 : 0);
        w.U8((uint8_t)province);
        w.U16(kProtocolVersion);
        Send(w.data, true);
        // Татгалзвал зочин өөрөө гарна (relay түүнийг өрөөнөөс гаргаж, хостод "left" илгээнэ).
        connected = accept;
    }

    void Session::RequestJoin(const FoundGame& game)
    {
        if (!initialized) return;
        Stop();
        WebOpen(TextFormat("?join=%u", game.address));
        role = Role::Guest;
        joinPending = true;
        connected = false;
    }

    void Session::StopDiscovery() { found.clear(); }
    void Session::Discover(float) {}
    void Session::HandleDiscovery() {}

    void Session::Poll(std::vector<Event>& events)
    {
        if (!initialized || role == Role::None) return;
        std::vector<uint8_t> buffer;
        for (int guard = 0; guard < 256; ++guard)
        {
            int kind = 0, value = 0;
            const int size = WebPeek(&kind, &value);
            if (kind == kWebNone) break;
            if (kind == kWebData)
            {
                buffer.resize((size_t)std::max(size, 1));
                WebPop(buffer.data());
                HandlePacket(buffer.data(), (size_t)size, events);
                continue;
            }
            WebPop(nullptr);
            switch (kind)
            {
            case kWebRoom:
                roomCode = value;
                TraceLog(LOG_INFO, "NET: hosting province %d, room %d", hostedProvince, roomCode);
                break;
            case kWebPaired:
                partner = true;
                if (role == Role::Guest)
                {
                    // ENet-ийн CONNECT-тэй адил: хостод нэгдэх хүсэлт илгээнэ.
                    Writer w;
                    w.U8((uint8_t)Msg::JoinRequest);
                    w.Str(name);
                    w.U16(kProtocolVersion);
                    Send(w.data, true);
                }
                break;
            case kWebLeft:
            {
                const bool wasActive = connected || joinPending || role == Role::Guest || partner;
                partner = false;
                connected = false;
                joinPending = false;
                if (wasActive)
                {
                    Event e;
                    e.type = Msg::Leave;
                    events.push_back(e);
                }
                break;
            }
            case kWebError:
            case kWebClosed:
            {
                // Сервер хүрэхгүй, өрөө олдсонгүй, эсвэл холболт тасарсан.
                const bool wasActive = connected || joinPending || role == Role::Guest || partner;
                if (kind == kWebClosed && role == Role::Host) retryAt = GetTime() + kRetrySeconds;
                Event e;
                e.type = Msg::Leave;
                if (kind == kWebError) e.text = value == 1 ? "Ийм кодтой тоглолт олдсонгүй" : value == 2 ? "Тоглолт дүүрсэн байна" : "";
                else if (role == Role::Guest && joinPending) e.text = "Сервертэй холбогдож чадсангүй";
                partner = false;
                connected = false;
                joinPending = false;
                roomCode = 0;
                WebClose();
                role = Role::None;
                if (wasActive) events.push_back(e);
                return;
            }
            default:
                break;
            }
        }
    }

    void Session::Send(const std::vector<uint8_t>& data, bool reliable)
    {
        if (!partner || data.empty()) return;
        WebSend(data.data(), (int)data.size(), reliable ? 1 : 0);
    }
#endif

    void Session::HandlePacket(const uint8_t* data, size_t size, std::vector<Event>& events)
    {
        if (size < 1) return;
        Reader r{ data + 1, size - 1 };
        Event e;
        e.type = (Msg)data[0];
        switch (e.type)
        {
        case Msg::JoinRequest:
            e.text = r.Str();
            e.version = r.at + 2 <= r.size ? r.U16() : 0; // хуучин хувилбар хувилбаргүй илгээдэг
            break;
        case Msg::JoinReply:
            e.accepted = r.U8() != 0;
            e.province = r.U8();
            e.version = r.at + 2 <= r.size ? r.U16() : 0;
            joinPending = false;
            connected = e.accepted;
            break;
        case Msg::GuestState:
            e.avatar = ReadAvatar(r);
            break;
        case Msg::GuestHit:
            e.id = r.U32();
            e.amount = r.F();
            e.headshot = r.U8() != 0;
            break;
        case Msg::GuestBarrel:
            e.id = r.U32();
            e.amount = r.F();
            break;
        case Msg::GuestBlast:
            e.position = r.V3();
            e.radius = r.F();
            e.amount = r.F();
            break;
        case Msg::Snapshot:
        {
            Snapshot s;
            s.host = ReadAvatar(r);
            const int soldiers = r.U16();
            for (int i = 0; i < soldiers && r.ok; ++i)
            {
                SoldierState so;
                so.position = r.V3();
                so.facing = r.F();
                so.speed = r.F();
                so.member = (int8_t)r.U8();
                so.alive = r.U8();
                so.aiming = r.U8();
                so.shots = r.U8();
                so.shotEnd = r.V3();
                so.vehicle = (int8_t)r.U8();
                so.seatStyle = r.U8();
                so.seat = r.V3();
                so.guest = r.U8();
                so.health = r.F();
                s.soldiers.push_back(so);
            }
            const int zombies = r.U16();
            for (int i = 0; i < zombies && r.ok; ++i)
            {
                ZombieState z;
                z.id = r.U32();
                z.type = r.U8();
                z.flags = r.U8();
                z.position = r.V3();
                z.facing = r.F();
                s.zombies.push_back(z);
            }
            const int vehicles = r.U16();
            for (int i = 0; i < vehicles && r.ok; ++i)
            {
                VehicleState v;
                v.index = r.U8();
                v.type = r.U8();
                v.position = r.V3();
                v.heading = r.F();
                v.speed = r.F();
                v.steer = r.F();
                v.occupied = r.U8();
                s.vehicles.push_back(v);
            }
            s.doorCount = r.U16();
            const int doorBytes = (s.doorCount + 7) / 8;
            for (int i = 0; i < doorBytes && r.ok; ++i) s.doors.push_back(r.U8());
            const int kits = r.U8();
            for (int i = 0; i < kits && r.ok; ++i) s.medKits.push_back(r.V3());
            s.objective = r.Str();
            s.detail = r.Str();
            s.hordeSmall = r.I32();
            s.hordeBig = r.I32();
            s.zombieSpeed = r.F();
            s.dayClock = r.F();
            if (r.ok)
            {
                latest = std::move(s);
                hasLatest = true;
            }
            return; // үйл явдал биш: TakeSnapshot-оор авна
        }
        case Msg::GuestDamage:
            e.amount = r.F();
            break;
        case Msg::KillReward:
            e.amount = (float)r.I32();
            e.position = r.V3();
            break;
        case Msg::GuestDoor:
            e.id = r.U16();
            e.accepted = r.U8() != 0;
            break;
        case Msg::GuestPickup:
            e.position = r.V3();
            break;
        case Msg::Projectile:
            e.id = r.U8();
            e.position = r.V3();
            e.velocity = r.V3();
            break;
        case Msg::AirstrikeStart:
            e.position = r.V3();
            e.radius = r.F();
            break;
        case Msg::BarrelExploded:
            e.id = r.U32();
            break;
        case Msg::MissionWon:
            e.amount = (float)r.I32();
            break;
        case Msg::Leave:
            break;
        default:
            return;
        }
        if (r.ok) events.push_back(e);
    }

    bool Session::TakeSnapshot(Snapshot& out)
    {
        if (!hasLatest) return false;
        out = std::move(latest);
        hasLatest = false;
        return true;
    }

#if !defined(__EMSCRIPTEN__)
    void Session::Send(const std::vector<uint8_t>& data, bool reliable)
    {
        if (!host || !peer) return;
        const enet_uint32 flags = reliable ? ENET_PACKET_FLAG_RELIABLE : ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT;
        ENetPacket* packet = enet_packet_create(data.data(), data.size(), flags);
        enet_peer_send(AsPeer(peer), reliable ? 0 : 1, packet);
        enet_host_flush(AsHost(host));
    }
#endif

    void Session::SendGuestState(const AvatarState& state)
    {
        Writer w;
        w.U8((uint8_t)Msg::GuestState);
        WriteAvatar(w, state);
        Send(w.data, false);
    }

    void Session::SendHit(uint32_t zombieId, float damage, bool headshot)
    {
        Writer w;
        w.U8((uint8_t)Msg::GuestHit);
        w.U32(zombieId);
        w.F(damage);
        w.U8(headshot ? 1 : 0);
        Send(w.data, true);
    }

    void Session::SendBarrelHit(int index, float damage)
    {
        Writer w;
        w.U8((uint8_t)Msg::GuestBarrel);
        w.U32((uint32_t)index);
        w.F(damage);
        Send(w.data, true);
    }

    void Session::SendBlast(Vector3 position, float radius, float damage)
    {
        Writer w;
        w.U8((uint8_t)Msg::GuestBlast);
        w.V3(position);
        w.F(radius);
        w.F(damage);
        Send(w.data, true);
    }

    void Session::SendSnapshot(const Snapshot& s)
    {
        Writer w;
        w.U8((uint8_t)Msg::Snapshot);
        WriteAvatar(w, s.host);
        w.U16((uint16_t)s.soldiers.size());
        for (const SoldierState& so : s.soldiers)
        {
            w.V3(so.position); w.F(so.facing); w.F(so.speed); w.U8((uint8_t)so.member); w.U8(so.alive);
            w.U8(so.aiming); w.U8(so.shots); w.V3(so.shotEnd);
            w.U8((uint8_t)so.vehicle); w.U8(so.seatStyle); w.V3(so.seat);
            w.U8(so.guest); w.F(so.health);
        }
        w.U16((uint16_t)s.zombies.size());
        for (const ZombieState& z : s.zombies)
        {
            w.U32(z.id); w.U8(z.type); w.U8(z.flags); w.V3(z.position); w.F(z.facing);
        }
        w.U16((uint16_t)s.vehicles.size());
        for (const VehicleState& v : s.vehicles)
        {
            w.U8(v.index); w.U8(v.type); w.V3(v.position); w.F(v.heading); w.F(v.speed); w.F(v.steer); w.U8(v.occupied);
        }
        w.U16((uint16_t)s.doorCount);
        for (uint8_t bits : s.doors) w.U8(bits);
        w.U8((uint8_t)std::min<size_t>(s.medKits.size(), 255));
        for (size_t i = 0; i < s.medKits.size() && i < 255; ++i) w.V3(s.medKits[i]);
        w.Str(s.objective);
        w.Str(s.detail);
        w.I32(s.hordeSmall);
        w.I32(s.hordeBig);
        w.F(s.zombieSpeed);
        w.F(s.dayClock);
        Send(w.data, false);
    }

    void Session::SendGuestDamage(float amount)
    {
        Writer w;
        w.U8((uint8_t)Msg::GuestDamage);
        w.F(amount);
        Send(w.data, true);
    }

    void Session::SendKillReward(int coins, Vector3 position)
    {
        Writer w;
        w.U8((uint8_t)Msg::KillReward);
        w.I32(coins);
        w.V3(position);
        Send(w.data, true);
    }

    void Session::SendBarrelExploded(int index)
    {
        Writer w;
        w.U8((uint8_t)Msg::BarrelExploded);
        w.U32((uint32_t)index);
        Send(w.data, true);
    }

    void Session::SendMissionWon(int reward)
    {
        Writer w;
        w.U8((uint8_t)Msg::MissionWon);
        w.I32(reward);
        Send(w.data, true);
    }

    void Session::SendDoor(int index, bool open)
    {
        Writer w;
        w.U8((uint8_t)Msg::GuestDoor);
        w.U16((uint16_t)index);
        w.U8(open ? 1 : 0);
        Send(w.data, true);
    }

    void Session::SendPickup(Vector3 position)
    {
        Writer w;
        w.U8((uint8_t)Msg::GuestPickup);
        w.V3(position);
        Send(w.data, true);
    }

    void Session::SendProjectile(int kind, Vector3 from, Vector3 velocity)
    {
        Writer w;
        w.U8((uint8_t)Msg::Projectile);
        w.U8((uint8_t)kind);
        w.V3(from);
        w.V3(velocity);
        Send(w.data, true);
    }

    void Session::SendAirstrike(Vector3 center, float heading)
    {
        Writer w;
        w.U8((uint8_t)Msg::AirstrikeStart);
        w.V3(center);
        w.F(heading);
        Send(w.data, true);
    }
}
