#include "GameLog.h"

#include "BuildStamp.h"
#include "Online.h"
#include "raylib.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <random>

namespace
{
    struct Entry
    {
        long long time = 0; // мс (энэ төхөөрөмжийн цаг; сервер өөрийн цагт тааруулна)
        int room = -1;
        int battle = 0;
        char role = 0;      // 'h', 'g', 0
        char level = 'I';   // I, W, E
        std::string text;
    };

    constexpr size_t kMaxPending = 400;  // илгээгээгүй мөр (хуучнаас нь хаяна)
    constexpr size_t kMaxStored = 300;   // төхөөрөмж дээр хадгалах
    constexpr int kMaxBatch = 150;       // нэг хүсэлтэд (сервер 200 хүртэл)
    constexpr double kSendEvery = 30.0;
    constexpr char kRecord = '\x1e', kField = '\x1f';

    std::mutex lock;
    bool enabled = false;
    std::deque<Entry> pending;
    int currentRoom = -1, currentBattle = 0;
    char currentRole = 0;
    bool dirty = false, urgent = false;
    double lastSend = 0.0, retryAt = 0.0, lastPersist = 0.0;
    std::string session, lastText;
    long long lastTextAt = 0;

    long long NowMs()
    {
        using namespace std::chrono;
        return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    }

    const char* Platform()
    {
#if defined(__EMSCRIPTEN__)
        return "web";
#elif defined(__ANDROID__)
        return "android";
#elif defined(_WIN32)
        return "windows";
#else
        return "linux";
#endif
    }

    bool StartsWith(const char* text, const char* prefix) { return std::strncmp(text, prefix, std::strlen(prefix)) == 0; }

    // Илгээх мөр эсэх: тоглолтын давталт, тулаан, сүлжээ, анхааруулга, алдаа. График ачааллын мэдээ, байнга давтагддаг
    // мөр (AI-ийн сум бүр) орохгүй.
    bool Wanted(int level, const char* text)
    {
        if (level >= LOG_WARNING)
        {
            return !std::strstr(text, "Indices data converted") && !std::strstr(text, "could not open port");
        }
        if (StartsWith(text, "BATTLE: AI hit") || StartsWith(text, "BATTLE: rammed")) return false;
        static const char* const prefixes[] = { "STEP:", "BATTLE:", "COOP:", "NET:", "ACCOUNT:", "MISSION: kill count",
                                                "GAME: GPU", "SESSION:" };
        for (const char* prefix : prefixes)
        {
            if (StartsWith(text, prefix)) return true;
        }
        return false;
    }

    // Хадгалах мөрөөс тусгаарлагч, мөр шилжүүлэгчийг хасна
    std::string Clean(const char* text)
    {
        std::string out;
        for (const char* c = text; *c && out.size() < 480; ++c)
        {
            out += (*c == kRecord || *c == kField || *c == '\n' || *c == '\r') ? ' ' : *c;
        }
        return out;
    }

    void Add(int level, const char* text)
    {
        // (lock барьсан байна)
        const long long now = NowMs();
        const std::string clean = Clean(text);
        if (clean == lastText && now - lastTextAt < 5000) return; // яг ижил мөр давтагдав
        lastText = clean;
        lastTextAt = now;
        Entry entry;
        entry.time = now;
        entry.room = currentRoom;
        entry.battle = currentBattle;
        entry.role = currentRole;
        entry.level = level >= LOG_ERROR ? 'E' : level >= LOG_WARNING ? 'W' : 'I';
        entry.text = clean;
        if (level >= LOG_ERROR || StartsWith(text, "STEP:")) urgent = true;
        pending.push_back(std::move(entry));
        while (pending.size() > kMaxPending) pending.pop_front();
        dirty = true;
    }

    void Quote(std::string& out, const std::string& text)
    {
        out += '"';
        for (unsigned char c : text)
        {
            if (c == '"' || c == '\\')
            {
                out += '\\';
                out += (char)c;
            }
            else if (c < 0x20)
            {
                char escaped[8];
                std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
                out += escaped;
            }
            else out += (char)c;
        }
        out += '"';
    }
}

void GameLog::Init(bool on)
{
    enabled = on;
    if (!enabled) return;
    {
        std::mt19937 random((unsigned)NowMs());
        char id[12];
        std::snprintf(id, sizeof(id), "%08x", (unsigned)random());
        session = id;
    }
    // Өмнөх ажиллуулалтын илгээгээгүй мөрүүд
    const std::string stored = Online::LoadLong("gamelog");
    int restored = 0;
    {
        std::lock_guard<std::mutex> guard(lock);
        size_t start = 0;
        while (start < stored.size())
        {
            size_t end = stored.find(kRecord, start);
            if (end == std::string::npos) end = stored.size();
            const std::string record = stored.substr(start, end - start);
            start = end + 1;
            // t<F>room<F>battle<F>role<F>level<F>text
            std::string fields[6];
            int field = 0;
            for (char c : record)
            {
                if (c == kField && field < 5) ++field;
                else fields[field] += c;
            }
            if (field < 5) continue;
            Entry entry;
            entry.time = std::atoll(fields[0].c_str());
            entry.room = std::atoi(fields[1].c_str());
            entry.battle = std::atoi(fields[2].c_str());
            entry.role = fields[3].empty() || fields[3][0] == '-' ? 0 : fields[3][0];
            entry.level = fields[4].empty() ? 'I' : fields[4][0];
            entry.text = fields[5];
            if (entry.text.empty()) continue;
            pending.push_back(std::move(entry));
            ++restored;
        }
    }
#if !defined(__EMSCRIPTEN__)
    // Өмнөх удаа хэвийн хаагдаагүй (унасан, хүчээр хаасан): тэмдэглэнэ (хөтчийн таб хаахад Shutdown ажилладаггүй)
    const bool crashed = Online::Load("gamelog_run") == "1";
    Online::Store("gamelog_run", "1");
#else
    const bool crashed = false;
#endif
    TraceLog(crashed ? LOG_ERROR : LOG_INFO, "SESSION: %s %s start%s (%d unsent lines from before)", Platform(), BuildStamp(),
             crashed ? ", the previous run did not close normally" : "", restored);
}

void GameLog::Shutdown()
{
    if (!enabled) return;
    Persist(1.0e12);
#if !defined(__EMSCRIPTEN__)
    Online::Store("gamelog_run", "");
#endif
}

void GameLog::Capture(int level, const char* text)
{
    if (!enabled || !text || !Wanted(level, text)) return;
    std::lock_guard<std::mutex> guard(lock);
    Add(level, text);
}

void GameLog::SetRoom(int room, int battle, char role)
{
    std::lock_guard<std::mutex> guard(lock);
    currentRoom = room >= 0 ? room : -1;
    currentBattle = room >= 0 ? battle : 0;
    currentRole = room >= 0 ? role : 0;
}

bool GameLog::Ready(double now)
{
    std::lock_guard<std::mutex> guard(lock);
    if (!enabled || pending.empty() || now < retryAt) return false;
    return urgent || now - lastSend >= kSendEvery;
}

std::string GameLog::TakeBatch(int& count)
{
    std::lock_guard<std::mutex> guard(lock);
    count = (int)std::min<size_t>(pending.size(), (size_t)kMaxBatch);
    std::string body = "{\"now\":" + std::to_string(NowMs()) + ",\"platform\":\"" + Platform() + "\",\"build\":";
    Quote(body, BuildStamp());
    body += ",\"session\":";
    Quote(body, session);
    body += ",\"entries\":[";
    for (int i = 0; i < count; ++i)
    {
        const Entry& entry = pending[(size_t)i];
        if (i > 0) body += ',';
        body += "{\"t\":" + std::to_string(entry.time);
        if (entry.room >= 0) body += ",\"room\":" + std::to_string(entry.room) + ",\"battle\":" + std::to_string(entry.battle);
        if (entry.role) body += std::string(",\"role\":\"") + entry.role + "\"";
        body += std::string(",\"level\":\"") + entry.level + "\",\"text\":";
        Quote(body, entry.text);
        body += '}';
    }
    body += "]}";
    return body;
}

void GameLog::Sent(int count)
{
    std::lock_guard<std::mutex> guard(lock);
    for (int i = 0; i < count && !pending.empty(); ++i) pending.pop_front();
    urgent = false;
    dirty = true;
    lastSend = GetTime();
}

void GameLog::Failed(double now)
{
    std::lock_guard<std::mutex> guard(lock);
    retryAt = now + kSendEvery;
    lastSend = now;
}

void GameLog::Persist(double now)
{
    std::string blob;
    {
        std::lock_guard<std::mutex> guard(lock);
        if (!enabled || !dirty || now - lastPersist < 2.0) return;
        dirty = false;
        lastPersist = now;
        const size_t from = pending.size() > kMaxStored ? pending.size() - kMaxStored : 0;
        for (size_t i = from; i < pending.size(); ++i)
        {
            const Entry& entry = pending[i];
            if (!blob.empty()) blob += kRecord;
            blob += std::to_string(entry.time) + kField + std::to_string(entry.room) + kField + std::to_string(entry.battle) + kField +
                    (entry.role ? entry.role : '-') + kField + entry.level + kField + entry.text;
        }
    }
    Online::Store("gamelog", blob); // (хоосон бол устгана)
}
