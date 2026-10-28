#include "Account.h"
#include "GameLog.h"

#include "Input.h"
#include "Json.h"
#include "Net.h"
#include "Online.h"
#include "Platform.h"
#include "UiStyle.h"

#include "raylib.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <utility>
#include <vector>

namespace
{
    enum class Screen
    {
        None,
        Login,
        Register,
        Reset,   // нууц үг сэргээх
        Sms,     // 144773 руу мессеж илгээхийг хүлээж буй
        Offer,   // аймаг нээх: үнэгүй эрх, худалдаж авсан эрх, багц худалдаж авах, төхөөрөмж солих
        Pay,     // QPay: QR, банкны апп
        Gold,    // Gold авах: багц сонгох (дараа нь Pay)
        Cashout, // Gold татах: банк, данс, дүн (дараа нь Sms баталгаажуулалт)
        Verify,  // утсаа баталгаажуулахыг урих цонх (давуу тал, заавар)
        Message
    };
    enum class Offer
    {
        Free,
        Credit,
        Pack,
        Device,
        Capital // Улаанбаатар: тусдаа үнэтэй (эрхээр нээгдэхгүй)
    };
    enum Field
    {
        kName,
        kPhone,
        kPassword,
        kAccount, // Gold татах: дансны дугаар
        kHolder,  // данс эзэмшигчийн нэр
        kAmount,  // татах Gold
        kFieldCount
    };
    enum class Style
    {
        Primary,
        Secondary,
        Link
    };

    using Handler = std::function<void(int status, const Json& reply)>;
    struct Pending
    {
        int id = -1;
        Handler handler;
    };

    struct Message
    {
        std::string title, text, primary, secondary;
        std::function<void()> onPrimary, onSecondary;
    };

    // --- Тохиргоо, данс ---
    bool enabled = false;
    std::string apiBase, token, deviceId;
    bool signedIn = false;
    std::string playerName;
    std::vector<int> owned;
    int freeLeft = 0, credits = 0, capital = 15, packPrice = 3000, packSize = 3, devicePrice = 5000, maxDevices = 2,
        capitalPrice = 5000;
    bool deviceOk = true;
    // Gold ба сайжруулалт (серверийн "upgrades", "shop"): ShopItem-ийн дараалал
    constexpr int kUpgradeCount = 5;
    const char* const kUpgradeNames[kUpgradeCount] = { "damage", "fire_rate", "magazine", "health", "soldier" };
    int gold = 0, goldLocked = 0, roomEntry = 70;
    int skinPrice = 50, startSkin = 0, coopReward = 30;
    int skinPrices[8] = { 50, 50, 50, 50, 50, 50, 50, 50 }; // дүр тус бүрийн үнэ (серверийн "skin_prices")
    std::vector<int> premiumSkins = { 3, 4 };
    std::vector<std::pair<int, int>> goldPacks = { { 300, 3000 }, { 550, 5000 }, { 1200, 10000 } }; // Gold, ₮ (серверээс)
    int invoiceGold = 0;
    // Gold татах (зөвхөн тоглоомоос хожсон Gold, серверийн "cashout")
    bool cashoutEnabled = false;
    bool cashoutKnown = false; // сервер Gold татахыг дэмждэг (хариунд "cashout" ирсэн)
    int cashoutMin = 20000, cashoutWithdrawable = 0;
    double cashoutRate = 1.0;
    std::vector<std::string> cashoutBanks;
    int cashoutBank = -1;  // сонгосон банк
    int cashoutId = 0;     // утсаар баталгаажуулж буй хүсэлт
    int activeCashout = 0, activeGold = 0; // нээлттэй (баталгаажуулж / шилжүүлэг хүлээж буй) хүсэлт
    std::string activeStatus, activeBank;
    bool smsCashout = false; // Sms цонх: Gold татах хүсэлтийг баталгаажуулж байна
    // Өрөө хайх: олдох хүртэл давтана
    bool roomSearching = false;
    std::string roomSearchLast; // сүүлийн "өрөө алга" хариуны тайлбар (хайлт дуусахад харуулна)
    double roomSearchUntil = 0.0, nextRoomSearch = 0.0;
    std::string roomSearchNote;
    // Нээлттэй өрөө (сервер нээсэн / орсон)
    int roomId = -1, roomProvince = -1, roomBattle = 0, roomRecruits = 0;
    int roomGuestSoldiers = -1, roomGuestHealthLevel = -1; // хост: серверийн мэдэх зочны цэрэг, амийн түвшин (-1 = мэдэхгүй)
    std::string roomStage, roomCode;
    bool roomPvp = false, roomCoop = false;
    // Тулааны мэдэгдэл: ялагч (host/guest), шийдэгдтэл давтана
    std::string battleReport;
    bool battleAbsent = false; // нөгөө тал тасарсан үеийн мэдэгдэл (сервер маргаанд тоолохгүй)
    int battleReportFor = 0;
    double nextBattlePoll = 0.0, battleReportDeadline = 0.0;
    Account::BattleResult battleResult = Account::BattleResult::None;
    Account::RoomRole roomRole = Account::RoomRole::None;
    std::string roomTicket;
    bool roomJoined = false;
    double nextRoomBeat = 0.0;
    int upgradeLevels[kUpgradeCount] = {};
    int upgradePrices[kUpgradeCount] = { 60, 60, 40, 50, 120 };
    constexpr int kSoldierUpgrade = 4;   // kUpgradeNames дахь "soldier"
    int soldierTarget = 0;               // алдсаны дараах үлдэх цэрэг (серверт илгээх)
    bool soldierLossDirty = false;       // илгээгээгүй алдагдал байна

    // --- Цонх ---
    Screen screen = Screen::None;
    Message message;
    Pending request, poll; // request: цонхыг түр хаадаг ("түр хүлээнэ үү"), poll: ард нь (SMS, төлбөр шалгах)
    // Утас баталгаажсан эсэх (баталгаажаагүй бол Gold нэмэгдэхгүй), баталгаажуулсны урамшуулал
    bool verified = true;
    int verifyBonus = 0;
    bool smsVerify = false;  // SMS цонх: нэвтэрсэн тоглогч утсаа баталгаажуулж буй
    std::string pendingNote; // тоглоомд нэг удаа харуулах мэдэгдэл (алдсан шагнал)
    std::string accountPhone; // (нуусан) дансны утас: баталгаажуулах цонхыг данс бүрд нэг л удаа автоматаар харуулна
    Pending beat;          // нэвтрэлт хүчинтэй эсэхийг үе үе шалгана (өөр төхөөрөмж дээр нэвтэрвэл энд гарна)
    double nextBeat = 0.0;
    bool kicked = false;
    std::string busyText;
    double nextPoll = 0.0;

    std::string fields[kFieldCount];
    int focus = -1;
    bool keyboardOpen = false;
    int layout = 0; // дэлгэцийн гар: 0 кирилл, 1 латин, 2 тоо
    bool shift = false;
    bool nativeEdit = false;  // хөтөч (утас): утасны өөрийн гараар бичиж байна
    std::string nativeText;                       // хуудасны талбарын сүүлд уншсан утга
    float panelShift = 0.0f;  // утасны гар талбарыг дарахгүйн тулд цонхыг дээш нь шилжүүлэх (пиксел, <= 0)
    float focusBottom = 0.0f; // сүүлд зурсан идэвхтэй талбарын доод ирмэг
    int litKey = 0;           // сүүлд дарсан товч (товших мэдрэгдэл)
    double litAt = -1.0;
    double eraseHeld = -1.0, eraseNext = 0.0; // "←" дарж байхад давтан устгана

    bool smsReset = false;
    int verificationId = 0;
    std::string smsNumber, smsText, smsUri;
    double smsExpires = 0.0;

    int province = -1, approved = -1;
    std::string provinceName;
    Offer offer = Offer::Free;

    int invoiceId = 0, invoiceAmount = 0;
    std::string invoiceKind, qrText;
    Texture2D qr = {};
    std::vector<std::pair<std::string, std::string>> banks; // нэр, апп нээх холбоос

    Rectangle badge = {};
    Rectangle topupBadge = {}; // "Gold авах": вэбээр цэнэглэх хуудас (/api/topup/)
    Rectangle cashoutBadge = {}; // "Gold татах": хожсон Gold-оо данс руу

    // --- Шууд зурах (immediate mode) UI: нэг функц Update-д товшилт шалгаж, Draw-д зурна ---
    bool drawPass = false, tapped = false, enter = false, testEnter = false;
    Vector2 pointer = {};

    const Color kInk = { 34, 24, 6, 255 };
    const Color kFrame = { 255, 200, 90, 110 };

    const char* Platform()
    {
#if defined(__EMSCRIPTEN__)
        return "web";
#elif defined(__ANDROID__)
        return "android";
#elif defined(_WIN32)
        return "windows";
#else
        return "other";
#endif
    }

    // ---------------------------------------------------------------- Туслах

    std::string Obj(std::initializer_list<std::pair<const char*, std::string>> items) // утгууд нь JSON
    {
        std::string out = "{";
        for (const auto& item : items)
        {
            if (out.size() > 1) out += ",";
            out += JsonQuote(item.first) + ":" + item.second;
        }
        return out + "}";
    }

    std::string Thousands(long long amount)
    {
        std::string digits = std::to_string(amount), out;
        for (size_t i = 0; i < digits.size(); ++i)
        {
            if (i > 0 && (digits.size() - i) % 3 == 0) out += ' ';
            out += digits[i];
        }
        return out;
    }

    std::string Money(long long amount) { return Thousands(amount) + "₮"; }

    // Ханш: бүхэл бол "1", үгүй бол "0.5"
    std::string RateText()
    {
        if (std::fabs(cashoutRate - std::round(cashoutRate)) < 0.001) return std::to_string((long long)std::llround(cashoutRate));
        std::string text = TextFormat("%.2f", cashoutRate);
        while (text.back() == '0') text.pop_back();
        return text;
    }

    void AppendUtf8(std::string& out, int code)
    {
        if (code < 0x80) out += (char)code;
        else if (code < 0x800)
        {
            out += (char)(0xC0 | (code >> 6));
            out += (char)(0x80 | (code & 0x3F));
        }
        else
        {
            out += (char)(0xE0 | (code >> 12));
            out += (char)(0x80 | ((code >> 6) & 0x3F));
            out += (char)(0x80 | (code & 0x3F));
        }
    }

    int CodepointCount(const std::string& text)
    {
        int count = 0;
        for (const unsigned char c : text) count += (c & 0xC0) != 0x80;
        return count;
    }

    void EraseLast(std::string& text)
    {
        while (!text.empty() && ((unsigned char)text.back() & 0xC0) == 0x80) text.pop_back();
        if (!text.empty()) text.pop_back();
    }

    int Upper(int code)
    {
        if (code >= 'a' && code <= 'z') return code - 32;
        if (code >= 0x430 && code <= 0x44F) return code - 0x20;
        if (code >= 0x450 && code <= 0x45F) return code - 0x50;
        if (code == 0x4AF || code == 0x4E9) return code - 1; // ү, ө
        return code;
    }

    bool Owns(int index) { return std::find(owned.begin(), owned.end(), index) != owned.end(); }

    std::vector<int> FieldsOf(Screen s)
    {
        switch (s)
        {
        case Screen::Login: return { kPhone, kPassword };
        case Screen::Register: return { kName, kPhone, kPassword };
        case Screen::Reset: return { kPhone, kPassword };
        case Screen::Cashout: return { kAccount, kHolder, kAmount };
        default: return {};
        }
    }

    void Focus(int index)
    {
        focus = index;
        layout = index == kPhone || index == kAccount || index == kAmount ? 2 : index == kPassword ? 1 : 0;
        shift = (index == kName || index == kHolder) && fields[index].empty();
    }

    // Дараагийн талбар (сүүлийнх бол -1)
    int NextAfter(int index)
    {
        const std::vector<int> list = FieldsOf(screen);
        auto it = std::find(list.begin(), list.end(), index);
        return it == list.end() || it + 1 == list.end() ? -1 : *(it + 1);
    }

    int NextField() { return NextAfter(focus); }

    // Хөтөч дээр мэдрэгчтэй горимд дэлгэцийн гарын оронд утасны өөрийн гарыг (Platform: WebField*) хэрэглэнэ.
    bool UseNativeKeyboard()
    {
#if defined(__EMSCRIPTEN__)
        return InTouchMode();
#else
        return false;
#endif
    }

    void EndNativeEdit()
    {
        if (!nativeEdit) return;
        nativeEdit = false;
        WebFieldBlur();
    }

    void Open(Screen s)
    {
        EndNativeEdit();
        screen = s;
        keyboardOpen = false;
        focus = -1;
        const std::vector<int> list = FieldsOf(s);
        if (list.empty()) return;
        // Компьютер дээр эхний хоосон талбарт шууд бичнэ; утсан дээр талбарыг товшиход гар гарна
        int first = list[0];
        for (int index : list)
        {
            if (fields[index].empty())
            {
                first = index;
                break;
            }
        }
        if (!InTouchMode()) Focus(first);
    }

    void Insert(int code)
    {
        if (focus < 0) return;
        std::string& text = fields[focus];
        if (focus == kPhone)
        {
            if (code < '0' || code > '9' || text.size() >= 8) return;
        }
        else if (focus == kAccount || focus == kAmount)
        {
            if (code < '0' || code > '9' || text.size() >= (focus == kAccount ? 20u : 9u)) return;
        }
        else if (focus == kPassword)
        {
            if (code < 33 || code > 126 || text.size() >= 32) return;
        }
        else if (code < 32 || (code == ' ' && text.empty()) || CodepointCount(text) >= 30)
        {
            return;
        }
        AppendUtf8(text, code);
    }

    // ---------------------------------------------------------------- Сүлжээ

    void Call(const char* method, const std::string& path, const std::string& body, Handler handler, const char* busy)
    {
        request.id = Online::Send(method, apiBase + path, body, token);
        request.handler = std::move(handler);
        busyText = busy;
    }

    void Background(const char* method, const std::string& path, const std::string& body, Handler handler)
    {
        poll.id = Online::Send(method, apiBase + path, body, token);
        poll.handler = std::move(handler);
    }

    void Finish(Pending& pending)
    {
        int status = 0;
        std::string body;
        if (pending.id < 0 || !Online::Poll(pending.id, status, body)) return;
        pending.id = -1;
        Handler handler = std::move(pending.handler);
        pending.handler = nullptr;
        if (handler) handler(status, Json::Parse(body));
    }

    void ShowMessage(const std::string& title, const std::string& text, Screen back = Screen::Message)
    {
        if (back == Screen::Message) back = screen == Screen::Message ? Screen::None : screen;
        message = {};
        message.title = title;
        message.text = text;
        message.primary = "ОК";
        message.onPrimary = [back] { Open(back); };
        Open(Screen::Message);
    }

    void ReadAccount(const Json& account)
    {
        if (account.type != Json::Type::Object) return;
        playerName = account["name"].Str();
        owned.clear();
        const Json& list = account["provinces"];
        for (size_t i = 0; i < list.Size(); ++i) owned.push_back(list[i].Int(-1));
        freeLeft = account["free_left"].Int();
        credits = account["credits"].Int();
        deviceOk = account["device_ok"].Bool();
        capital = account["capital"].Int(15);
        const Json& prices = account["prices"];
        packPrice = prices["pack"].Int(packPrice);
        packSize = prices["pack_size"].Int(packSize);
        devicePrice = prices["device_change"].Int(devicePrice);
        verified = account.Has("verified") ? account["verified"].Bool() : true; // (хуучин сервер: бүгд баталгаажсан)
        accountPhone = account["phone"].Str();
        verifyBonus = account["verify_bonus"].Int(0);
        capitalPrice = prices["capital"].Int(capitalPrice);
        maxDevices = account["max_devices"].Int(maxDevices);
        gold = account["gold"].Int(gold);
        goldLocked = account["gold_locked"].Int(goldLocked);
        roomEntry = account["room_entry"].Int(roomEntry);
        skinPrice = account["skin_price"].Int(skinPrice);
        coopReward = account["coop_reward"].Int(coopReward);
        for (int skin = 0; skin < 8; ++skin)
        {
            // Шинэ сервер дүр тус бүрийн үнэ өгнө; хуучин нь нэг үнэ
            const char* key = TextFormat("%d", skin);
            skinPrices[skin] = account["skin_prices"].Has(key) ? account["skin_prices"][key].Int(skinPrice) : skinPrice;
        }
        if (account["premium_skins"].type == Json::Type::Array)
        {
            premiumSkins.clear();
            for (size_t i = 0; i < account["premium_skins"].Size(); ++i) premiumSkins.push_back(account["premium_skins"][i].Int());
        }
        const Json& packs = account["gold_packs"];
        if (packs.Size() > 0)
        {
            goldPacks.clear();
            for (size_t i = 0; i < packs.Size(); ++i) goldPacks.emplace_back(packs[i]["gold"].Int(), packs[i]["price"].Int());
        }
        for (int i = 0; i < kUpgradeCount; ++i)
        {
            upgradeLevels[i] = account["upgrades"][kUpgradeNames[i]].Int(upgradeLevels[i]);
            upgradePrices[i] = account["shop"][kUpgradeNames[i]]["price"].Int(upgradePrices[i]);
        }
        // Серверт хүрээгүй алдагдал: хуучин тоог буцааж харуулахгүй
        if (soldierLossDirty) upgradeLevels[kSoldierUpgrade] = std::min(upgradeLevels[kSoldierUpgrade], soldierTarget);
        const Json& cashout = account["cashout"];
        cashoutKnown = cashout.type == Json::Type::Object;
        if (cashout.type == Json::Type::Object)
        {
            cashoutEnabled = cashout["enabled"].Bool();
            cashoutMin = cashout["min"].Int(cashoutMin);
            if (cashout["rate"].type == Json::Type::Number) cashoutRate = cashout["rate"].number;
            cashoutWithdrawable = cashout["withdrawable"].Int();
            const Json& list = cashout["banks"];
            if (list.Size() > 0)
            {
                cashoutBanks.clear();
                for (size_t i = 0; i < list.Size(); ++i) cashoutBanks.push_back(list[i].Str());
            }
            const Json& active = cashout["active"];
            activeCashout = active["id"].Int();
            activeGold = active["gold"].Int();
            activeStatus = active["status"].Str();
            activeBank = active["bank"].Str();
        }
    }

    void SignOut()
    {
        if (!token.empty()) Online::Send("POST", apiBase + "logout/", "{}", token); // хариуг хүлээхгүй
        token.clear();
        Online::Store("token", "");
        signedIn = false;
        owned.clear();
        province = -1;
        roomId = -1;
        roomRole = Account::RoomRole::None;
        Open(Screen::Login);
    }

    void SetRoom(const Json& room)
    {
        if (room.type != Json::Type::Object)
        {
            roomId = -1;
            roomRole = Account::RoomRole::None;
            roomTicket.clear();
            roomCode.clear();
            roomPvp = roomCoop = false;
            return;
        }
        roomId = room["id"].Int(-1);
        roomProvince = room["province"].Int(-1);
        roomRole = room["role"].Str() == "host" ? Account::RoomRole::Host : Account::RoomRole::Guest;
        roomTicket = room["ticket"].Str();
        roomBattle = room["battle"].Int(roomBattle);
        roomStage = room["stage"].Str();
        roomRecruits = room["recruits"].Int(0);
        roomCode = room["code"].Str();
        roomPvp = room["pvp"].Bool();
        roomCoop = room["coop"].Bool();
        nextRoomBeat = GetTime() + 60.0;
    }

    // Өрөөний хүсэлт (гарах, дуусгах, дохио): цонхыг хаахгүй, даалгаврын үеэр ч (Tick) хариуг авна.
    Pending roomRequest, roomBeat, roomStageCall, battleCall, roomSearchCall, soldierLossCall, guestLostCall, logCall;
    int guestLostTotal = 0, guestLostBattle = 0; // хост: зочны алдсан Gold-ийн цэрэг (энэ тулаанд, нийт)
    bool guestLostDirty = false;

    bool guestConfirmPending = false; // хост: "зочин ирлээ" (rooms/accept) амжилттай болтол давтана
    double nextGuestConfirm = 0.0;

    void BeatHandler(int status, const Json& reply);

    void SendGuestConfirm()
    {
        // (roomBeat-тэй ижил хариу: тулааны дугаар, зочны серверийн цэрэг, амийн түвшин)
        roomBeat.id = Online::Send("POST", apiBase + "rooms/" + std::to_string(roomId) + "/accept/", "{}", token);
        roomBeat.handler = [](int status, const Json& reply) {
            if (status == 0 || status >= 500)
            {
                nextGuestConfirm = GetTime() + 3.0; // сүлжээ: дахин (эс бөгөөс сервер тулалдаж буй зочныг "ирээгүй" гэж гаргана)
                return;
            }
            guestConfirmPending = false;
            BeatHandler(status, reply);
        };
    }

    void BeatHandler(int status, const Json& reply)
    {
        if (status == 410) SetRoom(Json()); // сервер өрөөг хаасан
        else if (status == 200)
        {
            roomBattle = reply["battle"].Int(roomBattle);
            roomCoop = reply["coop"].Bool();
            roomGuestSoldiers = reply["guest_soldiers"].Int(-1);
            roomGuestHealthLevel = reply["guest_health_level"].Int(-1);
        }
    }

    void SendBattleReport()
    {
        battleCall.id = Online::Send("POST", apiBase + "rooms/" + std::to_string(roomId) + "/result/",
                                     Obj({ { "battle", std::to_string(battleReportFor) }, { "winner", JsonQuote(battleReport) },
                                           { "absent", battleAbsent ? "true" : "false" } }),
                                     token);
        battleCall.handler = [](int status, const Json& reply) {
            if (status == 200 && reply["settled"].Bool())
            {
                ReadAccount(reply["account"]);
                if (reply["missed"].Int(0) > 0)
                {
                    pendingNote = "Тулаанд яллаа! Утсаа баталгаажуулсан бол +" + std::to_string(reply["missed"].Int(0)) + " Gold авах байсан";
                }
                const std::string result = reply["result"].Str();
                // Ялагч өрөөтэй (зочин ялсан бол шинэ хостын тасалбар), ялагдагч өрөөгүй
                if (result == "won") SetRoom(reply["room"]);
                else if (result == "lost") SetRoom(Json());
                battleResult = result == "won" ? Account::BattleResult::Won
                             : result == "lost" ? Account::BattleResult::Lost : Account::BattleResult::Void;
                battleReport.clear();
                return;
            }
            if (status == 410 || status == 403 || status == 404)
            {
                // Нөгөө тал шийдүүлсэн, энэ тоглогч өрөөнд байхгүй: ялагдсан. Өрөө хаагдсан бол хүчингүй.
                Background("GET", "account/", "", [](int code, const Json& fresh) {
                    if (code == 200) ReadAccount(fresh["account"]);
                });
                SetRoom(Json());
                battleResult = reply["error"].Str() == "room_closed" ? Account::BattleResult::Void : Account::BattleResult::Lost;
                battleReport.clear();
            }
            // бусад (сүлжээ, шийдэгдээгүй): Tick дахин илгээнэ
        };
    }

    void RoomCall(const char* action)
    {
        if (roomId < 0) return;
        roomRequest.id = Online::Send("POST", apiBase + "rooms/" + std::to_string(roomId) + "/" + action + "/", "{}", token);
        roomRequest.handler = [](int status, const Json& reply) {
            if (status != 200) return;
            ReadAccount(reply["account"]);
            if (reply["missed"].Int(0) > 0)
            {
                pendingNote = "Утсаа баталгаажуулсан бол +" + std::to_string(reply["missed"].Int(0)) + " Gold авах байсан";
            }
        };
        roomId = -1; // орон нутагт шууд гарсан (хариу ирэхэд Gold шинэчлэгдэнэ)
        roomRole = Account::RoomRole::None;
        roomTicket.clear();
    }

    void Fail(int status, const Json& reply)
    {
        if (status == 0)
        {
            ShowMessage("Холболт алга", "Сервертэй холбогдож чадсангүй. Интернэт холболтоо шалгаад дахин оролдоно уу.");
            return;
        }
        if (status == 404 && reply.type != Json::Type::Object)
        {
            ShowMessage("Сервер олдсонгүй", "Данс, төлбөрийн сервер (" + apiBase + ") ажиллахгүй байна. Дараа дахин оролдоно уу.");
            return;
        }
        if (status == 401 && reply["error"].Str() == "session_replaced")
        {
            SignOut();
            ShowMessage("Өөр төхөөрөмж дээр нэвтэрсэн", reply["message"].Str(), Screen::Login);
            return;
        }
        if (status == 401 && reply["error"].Str() == "auth")
        {
            SignOut();
            ShowMessage("Дахин нэвтэрнэ үү", "Нэвтрэлтийн хугацаа дууссан байна.", Screen::Login);
            return;
        }
        std::string text = reply["message"].Str();
        if (text.empty()) text = "Алдаа гарлаа (" + std::to_string(status) + "). Дахин оролдоно уу.";
        ShowMessage("Анхааруулга", text);
    }

    // Баталгаажаагүй тоглогч нэвтэрмэгц баталгаажуулах цонх: энэ төхөөрөмж дээр данс бүрд нэг л удаа
    void MaybeOfferVerify()
    {
        if (verified) return;
        const std::string key = playerName + "|" + accountPhone;
        if (Online::Load("verify_seen") == key) return;
        Online::Store("verify_seen", key);
        Open(Screen::Verify);
    }

    void SignIn(const Json& reply)
    {
        token = reply["token"].Str();
        Online::Store("token", token);
        Online::Store("phone", fields[kPhone]);
        ReadAccount(reply["account"]);
        signedIn = true;
        fields[kPassword].clear();
        Open(Screen::None);
        MaybeOfferVerify();
    }

    void CheckSession()
    {
        if (token.empty())
        {
            Open(Screen::Login);
            return;
        }
        Call("GET", "account/", "", [](int status, const Json& reply) {
            if (status == 200)
            {
                ReadAccount(reply["account"]);
                signedIn = true;
                Open(Screen::None);
                MaybeOfferVerify();
            }
            else if (status == 401)
            {
                token.clear();
                Online::Store("token", "");
                Open(Screen::Login);
            }
            else
            {
                // Интернэтгүй: тоглоом эхлэхэд нэвтрэлт шаардлагатай
                message = {};
                message.title = "Холболт алга";
                message.text = "Сервертэй холбогдож чадсангүй. Интернэт холболтоо шалгаад дахин оролдоно уу.";
                message.primary = "ДАХИН ОРОЛДОХ";
                message.onPrimary = [] { CheckSession(); };
                Open(Screen::Message);
            }
        }, "Холбогдож байна...");
    }

    // ---------------------------------------------------------------- Бүртгэл, нэвтрэлт

    bool CheckFields(bool name, bool password)
    {
        if (name && CodepointCount(fields[kName]) < 2)
        {
            ShowMessage("Нэр", "Нэрээ оруулна уу (2-оос дээш үсэг).");
            return false;
        }
        if (fields[kPhone].size() != 8)
        {
            ShowMessage("Утасны дугаар", "Утасны дугаар 8 оронтой байна.");
            return false;
        }
        if (password && fields[kPassword].size() < 6)
        {
            ShowMessage("Нууц үг", "Нууц үг 6-аас дээш тэмдэгт байна.");
            return false;
        }
        return true;
    }

    void Login()
    {
        if (!CheckFields(false, false)) return;
        if (fields[kPassword].empty())
        {
            ShowMessage("Нууц үг", "Нууц үгээ оруулна уу.");
            return;
        }
        Call("POST", "login/",
             Obj({ { "phone", JsonQuote(fields[kPhone]) }, { "password", JsonQuote(fields[kPassword]) },
                   { "device_id", JsonQuote(deviceId) }, { "platform", JsonQuote(Platform()) } }),
             [](int status, const Json& reply) {
                 if (status == 200) SignIn(reply);
                 else Fail(status, reply);
             },
             "Нэвтэрч байна...");
    }

    void StartSms(const Json& reply, bool reset, bool cashout = false, bool verify = false)
    {
        smsCashout = cashout;
        smsVerify = verify;
        verificationId = reply["verification_id"].Int();
        const Json& sms = reply["sms"];
        smsNumber = sms["shortcode"].Str();
        smsText = sms["text"].Str();
        smsUri = sms["sms_uri"].Str();
        smsExpires = GetTime() + (double)reply["expires_in"].Int(300);
        smsReset = reset;
        nextPoll = GetTime() + 3.0;
        Open(Screen::Sms);
    }

    // Дугаарыг өөр хүн баталгаажуулалгүй бүртгүүлсэн: эзэн нь мессежээр баталгаажуулж бүртгүүлнэ (тэр данс дугааргүй болно)
    void RegisterWithSms()
    {
        Call("POST", "register/start/",
             Obj({ { "name", JsonQuote(fields[kName]) }, { "phone", JsonQuote(fields[kPhone]) },
                   { "password", JsonQuote(fields[kPassword]) }, { "device_id", JsonQuote(deviceId) },
                   { "platform", JsonQuote(Platform()) } }),
             [](int status, const Json& reply) {
                 if (status == 200) StartSms(reply, false);
                 else Fail(status, reply);
             },
             "Илгээж байна...");
    }

    void OfferClaim()
    {
        message = {};
        message.title = "Дугаар бүртгэлтэй";
        message.text = "Энэ дугаараар өөр хүн баталгаажуулалгүй бүртгүүлсэн байна. Дугаар таных бол утаснаасаа мессеж илгээж "
                       "баталгаажуулаад бүртгүүлнэ үү (тэр данс дугааргүй болно).";
        message.primary = "МЕССЕЖЭЭР БАТАЛГААЖУУЛАХ";
        message.onPrimary = [] { RegisterWithSms(); };
        message.secondary = "Буцах";
        message.onSecondary = [] { Open(Screen::Register); };
        Open(Screen::Message);
    }

    void StartVerify()
    {
        Call("POST", "phone/verify/start/", "{}", [](int status, const Json& reply) {
            if (status == 200) StartSms(reply, false, false, true);
            else Fail(status, reply);
        }, "Илгээж байна...");
    }

    // Баталгаажаагүй тоглогчид: юу авахыг тайлбарлаж баталгаажуулахыг санал болгоно (VerifyScreen)
    void OfferVerify() { Open(Screen::Verify); }

    void Register()
    {
        if (!CheckFields(true, true)) return;
        // SMS баталгаажуулалтгүй: данс шууд нээгдэж нэвтэрнэ (утсыг нууц үг сэргээх, Gold татахад баталгаажуулна)
        Call("POST", "register/",
             Obj({ { "name", JsonQuote(fields[kName]) }, { "phone", JsonQuote(fields[kPhone]) },
                   { "password", JsonQuote(fields[kPassword]) }, { "device_id", JsonQuote(deviceId) },
                   { "platform", JsonQuote(Platform()) } }),
             [](int status, const Json& reply) {
                 if (status == 200) SignIn(reply);
                 else if (status == 409 && reply["error"].Str() == "phone_claimable") OfferClaim();
                 else Fail(status, reply);
             },
             "Бүртгэж байна...");
    }

    void Reset()
    {
        if (!CheckFields(false, true)) return;
        Call("POST", "password/reset/start/", Obj({ { "phone", JsonQuote(fields[kPhone]) }, { "device_id", JsonQuote(deviceId) } }),
             [](int status, const Json& reply) {
                 if (status == 200) StartSms(reply, true);
                 else Fail(status, reply);
             },
             "Илгээж байна...");
    }

    // ---------------------------------------------------------------- Gold татах

    void RequestCashout()
    {
        const std::string& account = fields[kAccount];
        const long long gold = fields[kAmount].empty() ? 0 : std::atoll(fields[kAmount].c_str());
        if (cashoutBank < 0 || cashoutBank >= (int)cashoutBanks.size())
        {
            ShowMessage("Банк", "Мөнгө хүлээн авах банкаа сонгоно уу.");
            return;
        }
        if (account.size() < 6)
        {
            ShowMessage("Дансны дугаар", "Дансны дугаараа (6-20 оронтой) оруулна уу.");
            return;
        }
        if (CodepointCount(fields[kHolder]) < 2)
        {
            ShowMessage("Данс эзэмшигч", "Данс эзэмшигчийн нэрийг оруулна уу.");
            return;
        }
        if (gold < cashoutMin)
        {
            ShowMessage("Татах дүн", "Хамгийн багадаа " + Thousands(cashoutMin) + " Gold татна.");
            return;
        }
        if (gold > cashoutWithdrawable)
        {
            ShowMessage("Татах дүн", "Зөвхөн тоглоомоос хожсон Gold-оо татна. Одоо " + Thousands(cashoutWithdrawable) +
                                         " Gold татах боломжтой (цэнэглэсэн Gold татагдахгүй).");
            return;
        }
        Call("POST", "cashouts/",
             Obj({ { "gold", std::to_string(gold) }, { "bank", JsonQuote(cashoutBanks[(size_t)cashoutBank]) },
                   { "account", JsonQuote(account) }, { "holder", JsonQuote(fields[kHolder]) } }),
             [](int status, const Json& reply) {
                 if (status != 200)
                 {
                     Fail(status, reply);
                     return;
                 }
                 ReadAccount(reply["account"]);
                 cashoutId = reply["cashout"]["id"].Int();
                 StartSms(reply, false, true);
             },
             "Хүсэлт илгээж байна...");
    }

    // Баталгаажаагүй хүсэлтийг цуцлана: Gold буцна
    void CancelCashout(int id)
    {
        Call("POST", "cashouts/" + std::to_string(id) + "/cancel/", "{}",
             [](int status, const Json& reply) {
                 if (status == 200) ReadAccount(reply["account"]);
                 if (status == 200 || status == 409) Open(Screen::Cashout);
                 else Fail(status, reply);
             },
             "Цуцалж байна...");
    }

    void PollCashout()
    {
        Background("GET", "cashouts/" + std::to_string(cashoutId) + "/", "", [](int status, const Json& reply) {
            nextPoll = GetTime() + 3.0;
            if (screen != Screen::Sms || !smsCashout || status == 0) return;
            if (status != 200)
            {
                Fail(status, reply);
                return;
            }
            ReadAccount(reply["account"]);
            const Json& cashout = reply["cashout"];
            const std::string state = cashout["status"].Str();
            if (state == "pending")
            {
                fields[kAmount].clear();
                ShowMessage("Хүсэлт илгээгдлээ",
                            Thousands(cashout["gold"].Int()) + " Gold (" + Money(cashout["amount"].Int()) + ") " + cashout["bank"].Str() + " " +
                                cashout["account"].Str() + " данс руу шилжинэ. Админ шалгаад шилжүүлэхэд түр хугацаа шаардагдана.",
                            Screen::None);
            }
            else if (state != "verifying")
            {
                ShowMessage("Хүсэлт цуцлагдлаа", "Мессеж ирээгүй тул хүсэлт цуцлагдаж, Gold буцлаа. Дахин оролдоно уу.", Screen::Cashout);
            }
        });
    }

    // Мессеж ирсэн эсэхийг асууна (ирээгүй бол сервер 409 буцаана).
    void PollSms()
    {
        if (smsCashout)
        {
            PollCashout();
            return;
        }
        const std::string id = std::to_string(verificationId);
        if (smsVerify)
        {
            // Нэвтэрсэн тоглогч утсаа баталгаажуулж байна
            Background("POST", "phone/verify/confirm/", Obj({ { "verification_id", id } }), [](int status, const Json& reply) {
                nextPoll = GetTime() + 3.0;
                if (screen != Screen::Sms || status == 409 || status == 0) return;
                if (status == 200)
                {
                    ReadAccount(reply["account"]);
                    ShowMessage("Утас баталгаажлаа",
                                verifyBonus > 0 ? "Баярлалаа! +" + std::to_string(verifyBonus) +
                                                      " Gold урамшуулал авлаа. Одоо шагнал, тулааны хожил, Gold татах бүгд нээлттэй."
                                                : "Баярлалаа! Одоо шагнал, тулааны хожил, Gold татах бүгд нээлттэй.",
                                Screen::None);
                }
                else if (status == 410) ShowMessage("Хугацаа дууссан", "Мессеж ирээгүй тул хугацаа дууссан. Дахин оролдоно уу.", Screen::None);
                else Fail(status, reply);
            });
            return;
        }
        const std::string path = smsReset ? "password/reset/confirm/" : "register/confirm/";
        const std::string body = smsReset ? Obj({ { "verification_id", id }, { "password", JsonQuote(fields[kPassword]) } })
                                          : Obj({ { "verification_id", id } });
        Background("POST", path, body, [](int status, const Json& reply) {
            nextPoll = GetTime() + 3.0;
            if (screen != Screen::Sms || status == 409 || status == 0) return;
            if (status == 200 && smsReset)
            {
                ShowMessage("Нууц үг солигдлоо", "Шинэ нууц үгээрээ нэвтэрнэ үү.", Screen::Login);
            }
            else if (status == 200)
            {
                SignIn(reply);
            }
            else if (status == 410)
            {
                ShowMessage("Хугацаа дууссан", "Мессеж ирээгүй тул хугацаа дууссан. Дахин оролдоно уу.",
                            smsReset ? Screen::Reset : Screen::Register);
            }
            else
            {
                Fail(status, reply);
            }
        });
    }

    // ---------------------------------------------------------------- Аймаг, төлбөр

    void Evaluate();

    void StartProvince()
    {
        // protocol: сервер зөвхөн ижил хувилбартай тоглогчдыг нэг өрөөнд тааруулна
        Call("POST", "provinces/start/", Obj({ { "province", std::to_string(province) }, { "skin", std::to_string(startSkin) },
                                               { "protocol", std::to_string(Net::kProtocolVersion) } }),
             [](int status, const Json& reply) {
            if (status == 200)
            {
                ReadAccount(reply["account"]);
                SetRoom(reply["room"]); // Gold хүрэлцвэл нээлттэй өрөө, эс бөгөөс ганцаарчилсан
                approved = province;
                province = -1;
                Open(Screen::None);
            }
            else if (status == 402 && reply["error"].Str() == "device_change_required")
            {
                deviceOk = false;
                Evaluate();
            }
            else
            {
                Fail(status, reply);
            }
        }, "Аймгийг шалгаж байна...");
    }

    void Claim()
    {
        Call("POST", "provinces/claim/", Obj({ { "province", std::to_string(province) } }), [](int status, const Json& reply) {
            if (status == 200)
            {
                ReadAccount(reply["account"]);
                StartProvince();
            }
            else if (status == 402)
            {
                if (reply["error"].Str() == "device_change_required") deviceOk = false;
                else credits = freeLeft = 0;
                Evaluate();
            }
            else
            {
                Fail(status, reply);
            }
        }, "Аймгийг нээж байна...");
    }

    // Аймаг эхлүүлэх хүсэлтийг дансны төлөвөөр шийднэ.
    // Тулаан, найзын өрөөнд орох: энэ төхөөрөмж дансанд бүртгэлгүй (шинэ төхөөрөмж, incognito г.м.): шалтгааныг тайлбарлаж
    // төхөөрөмж солих цонхыг нээнэ (зөвхөн "орж чадсангүй" гэхийн оронд)
    bool OfferDeviceForRoom(int status, const Json& reply)
    {
        if (status != 402 || reply["error"].Str() != "device_change_required") return false;
        deviceOk = false;
        province = -1; // (төлсний дараа аймаг эхлүүлэхгүй: тоглогч өрөөг дахин хайна)
        offer = Offer::Device;
        Open(Screen::Offer);
        return true;
    }

    void Evaluate()
    {
        if (!signedIn)
        {
            Open(Screen::Login);
            return;
        }
        if (!deviceOk) offer = Offer::Device;
        else if (Owns(province))
        {
            StartProvince();
            return;
        }
        else if (province == capital) offer = Offer::Capital;
        else if (freeLeft > 0) offer = Offer::Free;
        else if (credits > 0) offer = Offer::Credit;
        else offer = Offer::Pack;
        Open(Screen::Offer);
    }

    void ClosePay()
    {
        if (qr.id != 0) UnloadTexture(qr);
        qr = {};
        banks.clear();
    }

    void OpenPay(const Json& invoice)
    {
        ClosePay();
        invoiceId = invoice["id"].Int();
        invoiceKind = invoice["kind"].Str();
        invoiceAmount = invoice["amount"].Int();
        invoiceGold = invoice["gold"].Int();
        qrText = invoice["qr_text"].Str();
        const std::string& image = invoice["qr_image"].Str();
        if (!image.empty())
        {
            int size = 0;
            unsigned char* data = DecodeDataBase64((const unsigned char*)image.c_str(), &size);
            if (data)
            {
                Image picture = LoadImageFromMemory(".png", data, size);
                MemFree(data);
                if (picture.data)
                {
                    qr = LoadTextureFromImage(picture);
                    UnloadImage(picture);
                }
            }
        }
        const Json& urls = invoice["urls"];
        for (size_t i = 0; i < urls.Size(); ++i) banks.emplace_back(urls[i]["name"].Str(), urls[i]["link"].Str());
        nextPoll = GetTime() + 3.0;
        Open(Screen::Pay);
    }

    void Buy(const char* kind)
    {
        Call("POST", "invoices/", Obj({ { "kind", JsonQuote(kind) }, { "platform", JsonQuote(Platform()) } }),
             [](int status, const Json& reply) {
                 if (status == 200) OpenPay(reply["invoice"]);
                 else Fail(status, reply);
             },
             "Нэхэмжлэх үүсгэж байна...");
    }

    void BuyGold(int pack)
    {
        Call("POST", "invoices/",
             Obj({ { "kind", JsonQuote("gold") }, { "pack", std::to_string(pack) }, { "platform", JsonQuote(Platform()) } }),
             [](int status, const Json& reply) {
                 if (status == 200) OpenPay(reply["invoice"]);
                 else Fail(status, reply);
             },
             "Нэхэмжлэх үүсгэж байна...");
    }

    void Paid()
    {
        ClosePay();
        if (invoiceKind == "gold")
        {
            ShowMessage("Төлбөр орлоо", std::to_string(invoiceGold) + " Gold нэмэгдлээ. Одоо танд " + std::to_string(gold) + " Gold байна.",
                        Screen::None);
            return;
        }
        if (province < 0)
        {
            ShowMessage("Төлбөр орлоо", "Баярлалаа!", Screen::None);
            return;
        }
        // Багц: сонгосон аймгийг шууд нээнэ. Төхөөрөмж: дахин шалгана (үнэгүй эрх, багц г.м.).
        if (invoiceKind == "pack" && !Owns(province)) Claim();
        else Evaluate();
    }

    void PollPay()
    {
        Background("GET", "invoices/" + std::to_string(invoiceId) + "/", "", [](int status, const Json& reply) {
            nextPoll = GetTime() + 3.0;
            if (screen != Screen::Pay || status != 200) return;
            ReadAccount(reply["account"]);
            const Json& invoice = reply["invoice"];
            if (invoice["applied"].Bool()) Paid();
            else if (invoice["status"].Str() == "cancelled")
            {
                ClosePay();
                ShowMessage("Нэхэмжлэх цуцлагдсан", "Дахин оролдоно уу.", Screen::None);
            }
        });
    }

    // ---------------------------------------------------------------- Зурах хэрэгсэл

    bool Hit(Rectangle area)
    {
        if (drawPass || !tapped || !CheckCollisionPointRec(pointer, area)) return false;
        tapped = false;
        return true;
    }

    void Centered(const char* text, Rectangle area, int size, Color color, bool bold)
    {
        const int width = bold ? Ui::MeasureBold(text, size) : Ui::Measure(text, size);
        const int x = (int)(area.x + (area.width - (float)width) * 0.5f);
        const int y = (int)(area.y + (area.height - (float)size * 1.3f) * 0.5f);
        if (bold) Ui::TextBold(text, x, y, size, color);
        else Ui::Text(text, x, y, size, color);
    }

    bool Button(Rectangle area, const char* label, Style style, bool enterKey = false)
    {
        if (drawPass)
        {
            const bool hover = !InTouchMode() && CheckCollisionPointRec(InPointer(), area);
            if (style == Style::Primary)
            {
                DrawRectangleRounded(area, 0.3f, 8, hover ? Color{ 255, 218, 110, 255 } : Color{ 242, 178, 48, 255 });
                DrawRectangleRoundedLinesEx(area, 0.3f, 8, 1.5f, { 255, 240, 190, 200 });
                Centered(label, area, 20, kInk, true);
            }
            else if (style == Style::Secondary)
            {
                DrawRectangleRounded(area, 0.3f, 8, { 255, 255, 255, (unsigned char)(hover ? 44 : 22) });
                DrawRectangleRoundedLinesEx(area, 0.3f, 8, 1.0f, { 255, 255, 255, 70 });
                Centered(label, area, 17, Ui::kText, true);
            }
            else
            {
                Centered(label, area, 16, hover ? Ui::kText : Ui::kAccent, false);
            }
        }
        if (Hit(area)) return true;
        if (enterKey && enter && !drawPass)
        {
            enter = false;
            return true;
        }
        return false;
    }

    void FieldBox(Rectangle area, int index, const char* placeholder)
    {
        if (drawPass && UseNativeKeyboard())
        {
            // Утасны гар: талбарын дээр үл үзэгдэх <input> (хуруу шууд түүнд хүрнэ)
            const float w = (float)ViewWidth(), h = (float)ViewHeight();
            WebFieldPlace(index, area.x / w, area.y / h, area.width / w, area.height / h,
                          index == kPhone ? 1 : index == kPassword ? 2 : index == kAccount || index == kAmount ? 3 : 0, NextAfter(index),
                          fields[index].c_str());
        }
        if (drawPass)
        {
            const bool focused = focus == index && (nativeEdit || !UseNativeKeyboard());
            if (focused) focusBottom = area.y + area.height;
            DrawRectangleRounded(area, 0.25f, 8, { 0, 0, 0, 130 });
            DrawRectangleRoundedLinesEx(area, 0.25f, 8, focused ? 2.0f : 1.0f, focused ? Ui::kAccent : Color{ 255, 255, 255, 60 });
            const int size = 20;
            const int y = (int)(area.y + (area.height - (float)size * 1.3f) * 0.5f);
            std::string shown;
            if (index == kPassword)
            {
                for (size_t i = 0; i < fields[index].size(); ++i) shown += "•";
            }
            else
            {
                shown = fields[index];
            }
            // Урт бол эхнээс нь хасаж сүүлийг харуулна
            while (!shown.empty() && (float)Ui::Measure(shown.c_str(), size) > area.width - 32.0f)
            {
                size_t cut = 1;
                while (cut < shown.size() && ((unsigned char)shown[cut] & 0xC0) == 0x80) ++cut;
                shown.erase(0, cut);
            }
            if (shown.empty() && !focused) Ui::Text(placeholder, (int)area.x + 14, y, size, { 255, 255, 255, 90 });
            else Ui::Text(shown.c_str(), (int)area.x + 14, y, size, Ui::kText);
            if (focused && std::fmod(GetTime(), 1.0) < 0.6)
            {
                const float x = area.x + 15.0f + (float)Ui::Measure(shown.c_str(), size);
                DrawRectangle((int)x, (int)area.y + 12, 2, (int)area.height - 24, Ui::kAccent);
            }
        }
        if (Hit(area))
        {
            Focus(index);
            if (!UseNativeKeyboard()) keyboardOpen = InTouchMode();
        }
    }

    std::vector<std::string> Wrap(const std::string& text, int size, float width)
    {
        std::vector<std::string> lines;
        std::string line, word;
        auto flushWord = [&] {
            if (word.empty()) return;
            const std::string candidate = line.empty() ? word : line + " " + word;
            if (!line.empty() && (float)Ui::Measure(candidate.c_str(), size) > width)
            {
                lines.push_back(line);
                line = word;
            }
            else
            {
                line = candidate;
            }
            word.clear();
        };
        for (const char c : text)
        {
            if (c == ' ' || c == '\n')
            {
                flushWord();
                if (c == '\n')
                {
                    lines.push_back(line);
                    line.clear();
                }
            }
            else
            {
                word += c;
            }
        }
        flushWord();
        if (!line.empty()) lines.push_back(line);
        return lines;
    }

    float LineHeight(int size) { return (float)size * 1.45f; }

    void Paragraph(const std::vector<std::string>& lines, float x, float y, int size, Color color)
    {
        if (!drawPass) return;
        for (size_t i = 0; i < lines.size(); ++i) Ui::Text(lines[i].c_str(), (int)x, (int)(y + (float)i * LineHeight(size)), size, color);
    }

    void Title(const char* text, float x, float y)
    {
        if (drawPass) Ui::TextBold(text, (int)x, (int)y, 26, Ui::kText);
    }

    void Spinner(Vector2 center, float radius)
    {
        if (!drawPass) return;
        const float angle = (float)std::fmod(GetTime() * 300.0, 360.0);
        DrawRing(center, radius - 3.0f, radius, angle, angle + 270.0f, 24, Ui::kAccent);
    }

    // ---------------------------------------------------------------- Дэлгэцийн гар (мэдрэгчтэй дэлгэц)

    const int kShift = -1, kErase = -2, kLayout = -3, kDone = -4;

    // Гарын дээд зай (бичиж буй талбарыг давхар харуулахгүй: цонх өөрөө дээш шилжиж талбар харагдана)
    const float kPreviewHeight = 10.0f;
    float KeyboardHeight() { return std::clamp((float)ViewHeight() * 0.56f, 320.0f, 460.0f); }
    bool KeyboardVisible() { return InTouchMode() && keyboardOpen && focus >= 0 && !FieldsOf(screen).empty(); }

    struct Key
    {
        int code;
        float units;
    };

    std::vector<Key> Letters(const char* text)
    {
        std::vector<Key> keys;
        for (const char* p = text; *p;)
        {
            int size = 0;
            keys.push_back({ GetCodepointNext(p, &size), 1.0f });
            p += size > 0 ? size : 1;
        }
        return keys;
    }

    void Press(int code)
    {
        if (code == kShift) shift = !shift;
        else if (code == kErase)
        {
            if (focus >= 0) EraseLast(fields[focus]);
        }
        else if (code == kLayout) layout = layout == 0 ? 1 : 0;
        else if (code == kDone)
        {
            const int next = NextField();
            if (next >= 0) Focus(next);
            else keyboardOpen = false;
        }
        else
        {
            Insert(shift ? Upper(code) : code);
            if (shift && code != ' ') shift = false;
        }
    }

    void Keyboard()
    {
        if (!KeyboardVisible()) return;
        const float width = (float)ViewWidth(), height = KeyboardHeight(), top = (float)ViewHeight() - height;
        std::vector<std::vector<Key>> rows;
        if (layout == 2)
        {
            rows = { Letters("123"), Letters("456"), Letters("789"), { { kErase, 1.0f }, { '0', 1.0f }, { kDone, 1.0f } } };
        }
        else
        {
            const bool cyrillic = layout == 0;
            rows.push_back(Letters("1234567890"));
            rows.push_back(Letters(cyrillic ? "фцужэнгшүзкъ" : "qwertyuiop"));
            rows.push_back(Letters(cyrillic ? "йыбөахролдпе" : "asdfghjkl"));
            std::vector<Key> last = { { kShift, 1.5f } };
            for (const Key& key : Letters(cyrillic ? "ячёсмитьвющ" : "zxcvbnm")) last.push_back(key);
            last.push_back({ kErase, 1.5f });
            rows.push_back(last);
            rows.push_back({ { kLayout, 2.0f }, { '.', 1.0f }, { ' ', 6.0f }, { '-', 1.0f }, { kDone, 2.5f } });
        }
        if (drawPass)
        {
            DrawRectangle(0, (int)top, (int)width, (int)height, { 8, 10, 14, 250 });
            DrawRectangle(0, (int)top, (int)width, 2, kFrame);
        }
        const float rowsTop = top + kPreviewHeight;
        const float rowHeight = (height - kPreviewHeight - 8.0f) / (float)rows.size();
        const float maxKey = layout == 2 ? 220.0f : 132.0f;
        const bool erasing = !drawPass && InMouseDown(MOUSE_BUTTON_LEFT);
        bool overErase = false;
        Rectangle popup = {};
        std::string popupLabel;
        for (size_t r = 0; r < rows.size(); ++r)
        {
            float units = 0.0f;
            for (const Key& key : rows[r]) units += key.units;
            const float keyWidth = std::min(maxKey, (width - 12.0f) / std::max(units, layout == 2 ? 3.0f : 12.5f));
            float x = (width - keyWidth * units) * 0.5f;
            const float y = rowsTop + (float)r * rowHeight;
            for (const Key& key : rows[r])
            {
                // Товших талбар нь товчны хоорондох зайг ч хамарна (хуруу бага зэрэг хазайсан ч дарагдана)
                const Rectangle cell = { x, y, keyWidth * key.units, rowHeight };
                const Rectangle area = { x + 3.0f, y + 3.0f, keyWidth * key.units - 6.0f, rowHeight - 6.0f };
                x += keyWidth * key.units;
                std::string label;
                if (key.code == kShift) label = "↑";
                else if (key.code == kErase) label = "←";
                else if (key.code == kLayout) label = layout == 0 ? "ABC" : "АБВ";
                else if (key.code == kDone) label = NextField() >= 0 ? "Дараах" : "Болсон";
                else if (key.code == ' ') label = "зай";
                else AppendUtf8(label, shift ? Upper(key.code) : key.code);
                if (drawPass)
                {
                    const bool special = key.code < 0;
                    const bool flash = key.code == litKey && GetTime() - litAt < 0.15;
                    const bool lit = (key.code == kShift && shift) || key.code == kDone;
                    const Color fill = flash ? Color{ 255, 255, 255, 90 } : lit ? Ui::kAccentDark
                                     : special ? Color{ 255, 255, 255, 34 } : Color{ 255, 255, 255, 22 };
                    DrawRectangleRounded(area, 0.2f, 6, fill);
                    Centered(label.c_str(), area, key.code == kShift || key.code == kErase ? 30 : special || key.code == ' ' ? 20 : 28, key.code == ' ' ? Ui::kTextDim : Ui::kText, true);
                    // Үсэг дарахад хурууны дээр томоор харуулна
                    if (flash && key.code > ' ')
                    {
                        popup = { area.x + area.width * 0.5f - 34.0f, area.y - 78.0f, 68.0f, 74.0f };
                        popupLabel = label;
                    }
                }
                if (key.code == kErase && erasing && CheckCollisionPointRec(pointer, cell)) overErase = true;
                if (Hit(cell))
                {
                    Press(key.code);
                    litKey = key.code;
                    litAt = GetTime();
                    if (key.code == kErase) eraseHeld = eraseNext = GetTime() + 0.45;
                }
            }
        }
        if (!drawPass)
        {
            // "←" дарсаар байвал тасралтгүй устгана
            if (!overErase) eraseHeld = -1.0;
            else if (eraseHeld > 0.0 && GetTime() >= eraseNext && focus >= 0)
            {
                EraseLast(fields[focus]);
                eraseNext = GetTime() + 0.07;
            }
            // Гарын хоосон хэсэгт товшилт цааш (ард байгаа самбар руу) дамжихгүй
            if (tapped && pointer.y >= top) tapped = false;
        }
        if (drawPass && popupLabel.size())
        {
            DrawRectangleRounded(popup, 0.25f, 8, { 60, 64, 74, 255 });
            DrawRectangleRoundedLinesEx(popup, 0.25f, 8, 1.5f, kFrame);
            Centered(popupLabel.c_str(), popup, 40, Ui::kText, true);
        }
    }

    // ---------------------------------------------------------------- Цонхнууд

    Rectangle Panel(float width, float height)
    {
        const float bottom = KeyboardVisible() ? (float)ViewHeight() - KeyboardHeight() : (float)ViewHeight();
        const Rectangle area = { std::floor(((float)ViewWidth() - width) * 0.5f),
                                 std::max(8.0f, std::floor((bottom - height) * 0.5f)) + panelShift, width, height };
        if (drawPass)
        {
            Ui::Panel(area, { 12, 16, 22, 242 }, 14.0f);
            Ui::Outline(area, kFrame, 1.5f, 14.0f);
        }
        return area;
    }

    void LoginScreen()
    {
        const float width = 460.0f, inner = width - 56.0f;
        const Rectangle panel = Panel(width, 330.0f);
        const float x = panel.x + 28.0f;
        float y = panel.y + 22.0f;
        Title("НЭВТРЭХ", x, y);
        y += 38.0f;
        if (drawPass) Ui::Text("Утасны дугаар, нууц үгээрээ нэвтэрнэ үү", (int)x, (int)y, 15, Ui::kTextDim);
        y += 30.0f;
        FieldBox({ x, y, inner, 48.0f }, kPhone, "Утасны дугаар");
        y += 60.0f;
        FieldBox({ x, y, inner, 48.0f }, kPassword, "Нууц үг");
        y += 64.0f;
        if (Button({ x, y, inner, 50.0f }, "НЭВТРЭХ", Style::Primary, true)) Login();
        y += 62.0f;
        if (Button({ x, y, inner * 0.5f, 30.0f }, "Бүртгүүлэх", Style::Link)) Open(Screen::Register);
        if (Button({ x + inner * 0.5f, y, inner * 0.5f, 30.0f }, "Нууц үг мартсан?", Style::Link)) Open(Screen::Reset);
    }

    void RegisterScreen(bool reset)
    {
        const float width = 460.0f, inner = width - 56.0f;
        const Rectangle panel = Panel(width, reset ? 330.0f : 390.0f);
        const float x = panel.x + 28.0f;
        float y = panel.y + 22.0f;
        Title(reset ? "НУУЦ ҮГ СЭРГЭЭХ" : "БҮРТГҮҮЛЭХ", x, y);
        y += 38.0f;
        if (drawPass)
        {
            Ui::Text(reset ? "Утсаа мессежээр баталгаажуулж шинэ нууц үг тавина" : "Нэр, утас, нууц үгээрээ бүртгүүлнэ", (int)x,
                     (int)y, 15, Ui::kTextDim);
        }
        y += 30.0f;
        if (!reset)
        {
            FieldBox({ x, y, inner, 48.0f }, kName, "Нэр");
            y += 60.0f;
        }
        FieldBox({ x, y, inner, 48.0f }, kPhone, "Утасны дугаар");
        y += 60.0f;
        FieldBox({ x, y, inner, 48.0f }, kPassword, reset ? "Шинэ нууц үг (6+ тэмдэгт)" : "Нууц үг (6+ тэмдэгт)");
        y += 64.0f;
        if (Button({ x, y, inner, 50.0f }, "ҮРГЭЛЖЛҮҮЛЭХ", Style::Primary, true))
        {
            if (reset) Reset();
            else Register();
        }
        y += 62.0f;
        if (Button({ x, y, inner, 30.0f }, "Буцах", Style::Link)) Open(Screen::Login);
    }

    void SmsScreen()
    {
        const float width = 540.0f, inner = width - 56.0f;
        const std::vector<std::string> lines =
            Wrap(smsCashout ? "Gold татах хүсэлтээ баталгаажуулахын тулд бүртгэлтэй утаснаасаа доорх мессежийг илгээнэ үү. "
                              "Баталгаажсаны дараа мөнгийг таны данс руу шилжүүлнэ."
                 : smsVerify ? std::string("Дансандаа бүртгэлтэй утаснаасаа доорх мессежийг илгээнэ үү. Мессеж ирмэгц утас тань баталгаажна.")
                            : "Утасны дугаараа баталгаажуулахын тулд " + fields[kPhone] + " дугаартай утаснаасаа доорх мессежийг илгээнэ үү.",
                 16, inner);
        const bool touch = InTouchMode();
        const float height = 22.0f + 40.0f + (float)lines.size() * LineHeight(16) + 14.0f + 104.0f + 16.0f + 28.0f + 34.0f +
                             (touch ? 62.0f : 0.0f) + 30.0f + 22.0f;
        const Rectangle panel = Panel(width, height);
        const float x = panel.x + 28.0f;
        float y = panel.y + 22.0f;
        Title(smsCashout ? "ХҮСЭЛТ БАТАЛГААЖУУЛАХ" : "УТАС БАТАЛГААЖУУЛАХ", x, y);
        y += 40.0f;
        Paragraph(lines, x, y, 16, Ui::kText);
        y += (float)lines.size() * LineHeight(16) + 14.0f;
        if (drawPass)
        {
            const Rectangle box = { x, y, inner, 104.0f };
            DrawRectangleRounded(box, 0.15f, 8, { 0, 0, 0, 130 });
            DrawRectangleRoundedLinesEx(box, 0.15f, 8, 1.0f, kFrame);
            Ui::Text("Хүлээн авагч", (int)x + 20, (int)y + 14, 15, Ui::kTextDim);
            Ui::TextBold(smsNumber.c_str(), (int)x + 20, (int)y + 38, 40, Ui::kText);
            Ui::Text("Мессеж", (int)(x + inner * 0.5f) + 20, (int)y + 14, 15, Ui::kTextDim);
            Ui::TextBold(smsText.c_str(), (int)(x + inner * 0.5f) + 20, (int)y + 38, 40, Ui::kAccent);
            DrawLine((int)(x + inner * 0.5f), (int)y + 16, (int)(x + inner * 0.5f), (int)y + 88, { 255, 255, 255, 40 });
        }
        y += 120.0f;
        if (drawPass)
        {
            const int left = std::max(0, (int)(smsExpires - GetTime()));
            Ui::Text(TextFormat("Үлдсэн хугацаа %d:%02d", left / 60, left % 60), (int)x, (int)y, 15, Ui::kTextDim);
        }
        y += 28.0f;
        Spinner({ x + 10.0f, y + 11.0f }, 10.0f);
        if (drawPass) Ui::Text("Мессеж ирэхийг хүлээж байна...", (int)x + 30, (int)y, 16, Ui::kText);
        y += 34.0f;
        if (touch)
        {
            if (Button({ x, y, inner, 50.0f }, "МЕССЕЖ БИЧИХ", Style::Primary) && !smsUri.empty()) Online::OpenUrl(smsUri);
            y += 62.0f;
        }
        if (Button({ x, y, inner, 30.0f }, "Цуцлах", Style::Link))
        {
            if (smsCashout) CancelCashout(cashoutId);
            else Open(smsVerify ? Screen::None : smsReset ? Screen::Reset : Screen::Register);
        }
    }

    void OfferScreen()
    {
        const float width = 500.0f, inner = width - 56.0f;
        std::string title = provinceName, text, note, item, button = "НЭЭХ";
        int price = 0;
        switch (offer)
        {
        case Offer::Free:
            text = "Энэ аймгийг үнэгүй эрхээрээ нээх үү?";
            note = "Үнэгүй эрх " + std::to_string(freeLeft) + " үлдсэн. Нээсэн аймаг тань үргэлж нээлттэй байна.";
            break;
        case Offer::Credit:
            text = "1 эрх ашиглан энэ аймгийг нээх үү?";
            note = "Танд " + std::to_string(credits) + " эрх байна.";
            break;
        case Offer::Pack:
            text = "Аймаг нээх эрх тань дууссан байна.";
            item = std::to_string(packSize) + " аймгийн эрх";
            note = "Улаанбаатараас бусад дурын аймгийг сонгон нээнэ.";
            price = packPrice;
            button = "QPAY-ЭЭР ТӨЛӨХ";
            break;
        case Offer::Capital:
            text = "Улаанбаатар хотыг тусдаа худалдаж авна.";
            item = "Улаанбаатар хот";
            note = "Нэг удаа төлөөд үргэлж нээлттэй.";
            price = capitalPrice;
            button = "QPAY-ЭЭР ТӨЛӨХ";
            break;
        case Offer::Device:
            title = "ТӨХӨӨРӨМЖ СОЛИХ";
            text = "Таны дансанд " + std::to_string(maxDevices) + " төхөөрөмж аль хэдийн бүртгэлтэй байна.";
            item = "Энэ төхөөрөмжийг бүртгэх";
            note = "Хамгийн эрт бүртгэсэн төхөөрөмж тань чөлөөлөгдөнө (тэнд дахин тоглоход мөн төлбөртэй).";
            price = devicePrice;
            button = "QPAY-ЭЭР ТӨЛӨХ";
            break;
        }
        const std::vector<std::string> textLines = Wrap(text, 17, inner), noteLines = Wrap(note, 15, inner);
        const float height = 22.0f + 42.0f + (float)textLines.size() * LineHeight(17) + 10.0f + (price > 0 ? 70.0f : 0.0f) +
                             (float)noteLines.size() * LineHeight(15) + 18.0f + 62.0f + 30.0f + 20.0f;
        const Rectangle panel = Panel(width, height);
        const float x = panel.x + 28.0f;
        float y = panel.y + 22.0f;
        Title(title.c_str(), x, y);
        y += 42.0f;
        Paragraph(textLines, x, y, 17, Ui::kText);
        y += (float)textLines.size() * LineHeight(17) + 10.0f;
        if (price > 0)
        {
            if (drawPass)
            {
                const Rectangle box = { x, y, inner, 58.0f };
                DrawRectangleRounded(box, 0.2f, 8, { 0, 0, 0, 130 });
                DrawRectangleRoundedLinesEx(box, 0.2f, 8, 1.0f, kFrame);
                Ui::TextBold(item.c_str(), (int)x + 16, (int)y + 17, 18, Ui::kText);
                const std::string money = Money(price);
                Ui::TextBold(money.c_str(), (int)(x + inner) - 16 - Ui::MeasureBold(money.c_str(), 24), (int)y + 13, 24, Ui::kAccent);
            }
            y += 70.0f;
        }
        Paragraph(noteLines, x, y, 15, Ui::kTextDim);
        y += (float)noteLines.size() * LineHeight(15) + 18.0f;
        if (Button({ x, y, inner, 50.0f }, button.c_str(), Style::Primary, true))
        {
            if (offer == Offer::Free || offer == Offer::Credit) Claim();
            else Buy(offer == Offer::Pack ? "pack" : offer == Offer::Capital ? "capital" : "device");
        }
        y += 62.0f;
        if (Button({ x, y, inner, 30.0f }, "Болих", Style::Link))
        {
            province = -1;
            Open(Screen::None);
        }
    }

    void GoldScreen()
    {
        // Gold авах: багц сонгоход QPay-ийн цонх (QR, банкны апп) нээгдэнэ, тоглоомоос гарахгүй
        const float width = 500.0f, inner = width - 56.0f, row = 64.0f;
        const float height = 22.0f + 42.0f + 30.0f + (float)goldPacks.size() * (row + 10.0f) + 10.0f + (cashoutEnabled ? 56.0f : 0.0f) +
                             30.0f + 20.0f;
        const Rectangle panel = Panel(width, height);
        const float x = panel.x + 28.0f;
        float y = panel.y + 22.0f;
        Title("GOLD АВАХ", x, y);
        y += 42.0f;
        if (drawPass) Ui::Text(TextFormat("Танд одоо %d Gold байна. Багцаа сонгоно уу:", gold), (int)x, (int)y, 16, Ui::kTextDim);
        y += 30.0f;
        for (size_t i = 0; i < goldPacks.size(); ++i)
        {
            const Rectangle box = { x, y, inner, row };
            if (drawPass)
            {
                const bool hover = !InTouchMode() && CheckCollisionPointRec(InPointer(), box);
                DrawRectangleRounded(box, 0.2f, 8, hover ? Color{ 60, 46, 14, 220 } : Color{ 0, 0, 0, 130 });
                DrawRectangleRoundedLinesEx(box, 0.2f, 8, 1.5f, kFrame);
                Ui::Coin({ x + 26.0f, y + row * 0.5f }, 10.0f);
                Ui::TextBold(TextFormat("%d Gold", goldPacks[i].first), (int)x + 46, (int)(y + row * 0.5f) - 13, 22, Ui::kText);
                const std::string money = Money(goldPacks[i].second);
                Ui::TextBold(money.c_str(), (int)(x + inner) - 16 - Ui::MeasureBold(money.c_str(), 22), (int)(y + row * 0.5f) - 13, 22,
                             Ui::kAccent);
            }
            if (Hit(box))
            {
                BuyGold((int)i);
                return;
            }
            y += row + 10.0f;
        }
        y += 10.0f;
        if (cashoutEnabled)
        {
            const std::string label = "GOLD ТАТАХ  (хожсон " + Thousands(cashoutWithdrawable) + " Gold)"; // (дээд мөрөнд ч товч бий)
            if (Button({ x, y, inner, 44.0f }, label.c_str(), Style::Secondary))
            {
                Open(Screen::Cashout);
                return;
            }
            y += 56.0f;
        }
        if (Button({ x, y, inner, 30.0f }, "Болих", Style::Link)) Open(Screen::None);
    }

    void CashoutScreen()
    {
        // Gold татах: зөвхөн тоглоомоос хожсон Gold. Банк, данс, дүн -> 144773 руу SMS-ээр баталгаажуулна.
        const float width = std::min(640.0f, (float)ViewWidth() - 16.0f), inner = width - 56.0f;
        const int columns = 4, rows = std::max(1, ((int)cashoutBanks.size() + columns - 1) / columns);
        const bool active = activeCashout > 0;
        const float height = 22.0f + 40.0f + 26.0f + 24.0f + (active ? 28.0f : 0.0f) + 10.0f + 24.0f + (float)rows * 42.0f + 10.0f + 60.0f +
                             60.0f + 14.0f + 56.0f + 30.0f + 18.0f;
        const Rectangle panel = Panel(width, height);
        const float x = panel.x + 28.0f;
        float y = panel.y + 22.0f;
        Title("GOLD ТАТАХ", x, y);
        y += 40.0f;
        if (drawPass)
        {
            Ui::Coin({ x + 9.0f, y + 11.0f }, 9.0f);
            const std::string text = "Татах боломжтой: " + Thousands(cashoutWithdrawable) + " Gold (тоглоомоос хожсон)";
            Ui::TextBold(text.c_str(), (int)x + 26, (int)y, 18, cashoutWithdrawable >= cashoutMin ? Ui::kAccent : Ui::kText);
        }
        y += 26.0f;
        if (drawPass)
        {
            const std::string text = "Доод хэмжээ " + Thousands(cashoutMin) + " Gold · 1 Gold = " + RateText() + "₮ · Цэнэглэсэн Gold татагдахгүй";
            Ui::Text(text.c_str(), (int)x, (int)y, 15, Ui::kTextDim);
        }
        y += 24.0f;
        if (active)
        {
            const bool verifying = activeStatus == "verifying";
            if (drawPass)
            {
                const std::string text = verifying ? "Баталгаажаагүй хүсэлт байна: " + Thousands(activeGold) + " Gold"
                                                   : "Шилжүүлэг хүлээж буй: " + Thousands(activeGold) + " Gold → " + activeBank;
                Ui::Text(text.c_str(), (int)x, (int)y + 4, 15, verifying ? Ui::kText : Ui::kAccent);
            }
            if (verifying && Button({ x + inner - 140.0f, y, 140.0f, 26.0f }, "Цуцлах", Style::Link))
            {
                CancelCashout(activeCashout);
                return;
            }
            y += 28.0f;
        }
        y += 10.0f;
        if (drawPass) Ui::Text("Хүлээн авах банк", (int)x, (int)y, 15, Ui::kTextDim);
        y += 24.0f;
        const float cell = inner / (float)columns;
        for (size_t i = 0; i < cashoutBanks.size(); ++i)
        {
            const Rectangle area = { x + (float)(i % columns) * cell, y + (float)(i / columns) * 42.0f, cell - 8.0f, 36.0f };
            const bool chosen = (int)i == cashoutBank;
            if (drawPass)
            {
                std::string label = cashoutBanks[i];
                const std::string suffix = " банк";
                if (label.size() > suffix.size() && label.compare(label.size() - suffix.size(), suffix.size(), suffix) == 0)
                {
                    label.erase(label.size() - suffix.size()); // "Хаан банк" -> "Хаан"
                }
                bool cut = false;
                while (!label.empty() && (float)Ui::Measure((label + (cut ? ".." : "")).c_str(), 14) > area.width - 10.0f)
                {
                    EraseLast(label);
                    cut = true;
                }
                if (cut) label += "..";
                const bool hover = !InTouchMode() && CheckCollisionPointRec(InPointer(), area);
                DrawRectangleRounded(area, 0.3f, 8, chosen ? Color{ 242, 178, 48, 255 } : Color{ 255, 255, 255, (unsigned char)(hover ? 40 : 20) });
                if (!chosen) DrawRectangleRoundedLinesEx(area, 0.3f, 8, 1.0f, { 255, 255, 255, 50 });
                Centered(label.c_str(), area, 14, chosen ? kInk : Ui::kText, chosen);
            }
            if (Hit(area)) cashoutBank = (int)i;
        }
        y += (float)rows * 42.0f + 10.0f;
        const float half = inner * 0.5f - 6.0f;
        FieldBox({ x, y, half, 48.0f }, kAccount, "Дансны дугаар");
        FieldBox({ x + half + 12.0f, y, half, 48.0f }, kHolder, "Данс эзэмшигчийн нэр");
        y += 60.0f;
        FieldBox({ x, y, half, 48.0f }, kAmount, "Татах Gold");
        if (Button({ x + half + 12.0f, y + 6.0f, 110.0f, 36.0f }, "Бүгдийг", Style::Secondary))
        {
            fields[kAmount] = std::to_string(cashoutWithdrawable);
        }
        if (drawPass)
        {
            const long long gold = fields[kAmount].empty() ? 0 : std::atoll(fields[kAmount].c_str());
            const std::string money = "= " + Money((long long)std::floor((double)gold * cashoutRate));
            Ui::TextBold(money.c_str(), (int)(x + inner) - Ui::MeasureBold(money.c_str(), 22), (int)y + 11, 22, Ui::kAccent);
        }
        y += 60.0f + 14.0f;
        if (Button({ x, y, inner, 50.0f }, "ТАТАХ ХҮСЭЛТ ИЛГЭЭХ", Style::Primary, true)) RequestCashout();
        y += 56.0f;
        if (Button({ x, y, inner, 30.0f }, "Болих", Style::Link)) Open(Screen::Gold);
    }

    void PayScreen()
    {
        const bool touch = InTouchMode() && !banks.empty();
        const float width = 660.0f, inner = width - 56.0f, qrSize = 236.0f;
        const int bankRows = touch ? (int)std::min<size_t>(3, (banks.size() + 3) / 4) : 0;
        const float height = 22.0f + 44.0f + qrSize + 50.0f + (touch ? 30.0f + (float)bankRows * 46.0f + 6.0f : 0.0f);
        const Rectangle panel = Panel(width, height);
        const float x = panel.x + 28.0f;
        float y = panel.y + 22.0f;
        Title("QPAY ТӨЛБӨР", x, y);
        y += 44.0f;
        if (drawPass)
        {
            const Rectangle box = { x, y, qrSize, qrSize };
            DrawRectangleRounded(box, 0.06f, 8, WHITE);
            if (qr.id != 0)
            {
                DrawTexturePro(qr, { 0.0f, 0.0f, (float)qr.width, (float)qr.height }, { x + 10.0f, y + 10.0f, qrSize - 20.0f, qrSize - 20.0f },
                               { 0.0f, 0.0f }, 0.0f, WHITE);
            }
            else
            {
                Centered("QR (туршилт)", box, 18, { 60, 60, 60, 255 }, true);
            }
        }
        const float rx = x + qrSize + 24.0f, rw = inner - qrSize - 24.0f;
        float ry = y;
        if (drawPass)
        {
            Ui::Text("Төлөх дүн", (int)rx, (int)ry, 15, Ui::kTextDim);
            const std::string money = Money(invoiceAmount);
            Ui::TextBold(money.c_str(), (int)rx, (int)ry + 20, 36, Ui::kAccent);
            Ui::Text(invoiceKind == "gold"      ? TextFormat("%d Gold", invoiceGold)
                     : invoiceKind == "pack"    ? TextFormat("%d аймгийн эрх", packSize)
                     : invoiceKind == "capital" ? "Улаанбаатар хот"
                                                : "Төхөөрөмж солих",
                     (int)rx, (int)ry + 68, 16,
                     Ui::kText);
        }
        ry += 100.0f;
        const std::vector<std::string> lines = Wrap("Банкны аппаараа QR кодыг уншуулж төлнө үү. Төлбөр ормогц автоматаар үргэлжилнэ.", 15, rw);
        Paragraph(lines, rx, ry, 15, Ui::kTextDim);
        ry += (float)lines.size() * LineHeight(15) + 8.0f;
        Spinner({ rx + 9.0f, ry + 10.0f }, 9.0f);
        if (drawPass) Ui::Text("Төлбөр хүлээж байна...", (int)rx + 26, (int)ry, 15, Ui::kText);
        const float by = y + qrSize - 40.0f;
        if (Button({ rx, by, rw, 40.0f }, "ШАЛГАХ", Style::Secondary)) nextPoll = 0.0;
        y += qrSize + 10.0f;
        if (Button({ x, y, inner, 30.0f }, "Болих", Style::Link))
        {
            ClosePay();
            province = -1;
            Open(Screen::None);
            return;
        }
        y += 40.0f;
        if (!touch) return;
        if (drawPass) Ui::Text("Эсвэл банкны аппаа сонгоно уу:", (int)x, (int)y, 15, Ui::kTextDim);
        y += 30.0f;
        const float cell = inner / 4.0f;
        for (int i = 0; i < bankRows * 4 && i < (int)banks.size(); ++i)
        {
            const Rectangle area = { x + (float)(i % 4) * cell, y + (float)(i / 4) * 46.0f, cell - 8.0f, 40.0f };
            std::string label = banks[(size_t)i].first;
            while (!label.empty() && (float)Ui::Measure(label.c_str(), 15) > area.width - 12.0f) EraseLast(label);
            if (drawPass)
            {
                DrawRectangleRounded(area, 0.3f, 8, { 255, 255, 255, 22 });
                Centered(label.c_str(), area, 15, Ui::kText, false);
            }
            if (Hit(area)) Online::OpenUrl(banks[(size_t)i].second);
        }
    }

    // Утсаа баталгаажуулах урилга (тоглоомын бараан загвар): давуу талууд, анхааруулга, алхмууд, хоёр товч
    void VerifyScreen()
    {
        const float width = std::min(600.0f, (float)ViewWidth() - 24.0f), inner = width - 56.0f;
        const std::string bonusText = "Баталгаажуулсны дараа +" + std::to_string(verifyBonus) + " Gold авна.";
        struct Row { Ui::Icon icon; Color tint; const char* title; std::string text; };
        const Row rows[] = {
            { Ui::Icon::Reward, Ui::kAccent, "Нэг удаагийн Gold урамшуулал", bonusText },
            { Ui::Icon::MissionComplete, Ui::kGood, "Үе давах шагнал",
              "Үеийг амжилттай давсан болон найзтайгаа хамт үе давсан шагналаа авна." },
            { Ui::Icon::WeaponMelee, Ui::kTextDim, "PvP тулааны шагнал", "Бусад тоглогчтой тулалдаж, ялалтаараа Gold хожно." },
            { Ui::Icon::Currency, Ui::kAccent, "Gold-оо мөнгө болгон авах",
              "Тоглоомын нөхцлийн дагуу Gold-оо мөнгө болгон татах боломжтой." },
        };
        const std::vector<std::string> warning =
            Wrap("Утсаа баталгаажуулаагүй бол Gold шинээр авах боломжгүй бөгөөд зөвхөн өөрт байгаа Gold-оо зарцуулна. "
                 "Тулаанд ялагдвал барьцаалсан Gold-оо алдана.", 14, inner - 56.0f);
        const char* steps[] = { "Доорх товчийг дарж баталгаажуулах кодоо авна.",
                                "Тоглоомд харуулсан 6 оронтой кодыг 144773 дугаарт SMS-ээр илгээнэ.",
                                "Баталгаажуулалт амжилттай болсны дараа урамшууллаа авна." };
        const float warningH = 42.0f + (float)warning.size() * LineHeight(14) + 10.0f;
        const float height = 22.0f + 106.0f + 4.0f * 48.0f + 14.0f + warningH + 20.0f + 30.0f + 3.0f * 26.0f + 14.0f +
                             50.0f + 10.0f + 44.0f + 22.0f;
        const Rectangle panel = Panel(width, height);
        const float x = panel.x + 28.0f;
        float y = panel.y + 22.0f;
        if (drawPass)
        {
            // Гарчиг: алтан утасны дүрс + бичиг, голлуулсан
            const char* title = "Утсаа баталгаажуулаад Gold цуглуул!";
            const int titleW = Ui::MeasureBold(title, 22);
            const float start = panel.x + (width - (float)titleW - 30.0f) * 0.5f;
            DrawRectangleRounded({ start, y + 1.0f, 17.0f, 26.0f }, 0.35f, 6, Ui::kAccent);
            DrawRectangleRounded({ start + 3.0f, y + 5.0f, 11.0f, 15.0f }, 0.2f, 4, { 40, 30, 10, 255 });
            DrawCircleV({ start + 8.5f, y + 23.0f }, 1.6f, { 40, 30, 10, 255 });
            Ui::TextBold(title, (int)(start + 30.0f), (int)y, 22, Ui::kText);
            const char* subtitle = "Баталгаажуулснаар тоглоомын бүх боломжийг нээгээрэй.";
            Ui::Text(subtitle, (int)(panel.x + (width - (float)Ui::Measure(subtitle, 15)) * 0.5f), (int)y + 38, 15, Ui::kTextDim);
            DrawLineEx({ x, y + 70.0f }, { x + inner, y + 70.0f }, 1.0f, Ui::kBorder);
            Ui::TextBold("Баталгаажуулсан тоглогч дараах эрх, урамшууллыг авна:", (int)x, (int)y + 84, 15, Ui::kText);
        }
        y += 106.0f + 6.0f;
        for (const Row& row : rows)
        {
            if (drawPass)
            {
                const Rectangle box = { x, y + 1.0f, 28.0f, 28.0f };
                DrawCircleV({ box.x + 14.0f, box.y + 14.0f }, 18.0f, Fade(row.tint, 0.12f));
                if (!Ui::DrawIcon(row.icon, box)) DrawCircleV({ box.x + 14.0f, box.y + 14.0f }, 11.0f, row.tint);
                Ui::TextBold(row.title, (int)x + 44, (int)y - 1, 16, Ui::kText);
                Ui::Text(row.text.c_str(), (int)x + 44, (int)y + 21, 13, Ui::kTextDim);
            }
            y += 48.0f;
        }
        if (drawPass) DrawLineEx({ x, y }, { x + inner, y }, 1.0f, Ui::kBorder);
        y += 14.0f;
        if (drawPass)
        {
            // Анхааруулга: алтан хүрээтэй бараан хайрцаг, (i) дүрс
            const Rectangle box = { x, y, inner, warningH };
            DrawRectangleRounded(box, 0.12f, 8, { 255, 192, 64, 20 });
            DrawRectangleRoundedLinesEx(box, 0.12f, 8, 1.0f, { 255, 192, 64, 90 });
            DrawRing({ x + 24.0f, y + 22.0f }, 7.5f, 9.2f, 0.0f, 360.0f, 24, Ui::kAccent);
            DrawRectangle((int)x + 23, (int)y + 20, 2, 7, Ui::kAccent);
            DrawRectangle((int)x + 23, (int)y + 16, 2, 2, Ui::kAccent);
            Ui::TextBold("Анхааруулга", (int)x + 44, (int)y + 12, 15, Ui::kAccent);
            Paragraph(warning, x + 44.0f, y + 38.0f, 14, Ui::kText);
        }
        y += warningH + 20.0f;
        if (drawPass) Ui::TextBold("Хэрхэн баталгаажуулах вэ?", (int)x, (int)y, 16, Ui::kText);
        y += 30.0f;
        for (int i = 0; i < 3; ++i)
        {
            if (drawPass)
            {
                // Дугаар: алтан тойрог
                DrawCircleV({ x + 10.0f, y + 9.0f }, 10.0f, Ui::kAccent);
                const char* number = TextFormat("%d", i + 1);
                Ui::TextBold(number, (int)(x + 10.0f - (float)Ui::MeasureBold(number, 13) * 0.5f), (int)y + 1, 13, kInk);
                Ui::Text(steps[i], (int)x + 30, (int)y, 14, Ui::kText);
            }
            y += 26.0f;
        }
        y += 14.0f;
        if (Button({ x, y, inner, 50.0f }, "УТСАА БАТАЛГААЖУУЛАХ", Style::Primary, true))
        {
            StartVerify();
            return;
        }
        if (Button({ x, y + 60.0f, inner, 44.0f }, "Дараа сануулах", Style::Secondary)) Open(Screen::None);
    }

    void MessageScreen()
    {
        const float width = 480.0f, inner = width - 56.0f;
        const std::vector<std::string> lines = Wrap(message.text, 17, inner);
        const Rectangle panel = Panel(width, 22.0f + 42.0f + (float)lines.size() * LineHeight(17) + 20.0f + 50.0f + 24.0f);
        const float x = panel.x + 28.0f;
        float y = panel.y + 22.0f;
        Title(message.title.c_str(), x, y);
        y += 42.0f;
        Paragraph(lines, x, y, 17, Ui::kText);
        y += (float)lines.size() * LineHeight(17) + 20.0f;
        const bool two = !message.secondary.empty();
        const float primaryWidth = two ? inner * 0.5f - 6.0f : inner;
        if (two && Button({ x + inner * 0.5f + 6.0f, y, primaryWidth, 50.0f }, message.secondary.c_str(), Style::Secondary))
        {
            if (message.onSecondary) message.onSecondary();
            return;
        }
        if (Button({ x, y, primaryWidth, 50.0f }, message.primary.c_str(), Style::Primary, true) && message.onPrimary)
        {
            const std::function<void()> action = message.onPrimary; // үйлдэл message-ийг сольж болно
            action();
        }
    }

    void Frame()
    {
        if (!drawPass) Keyboard();
        if (screen != Screen::None && drawPass) DrawRectangle(0, 0, ViewWidth(), ViewHeight(), { 0, 0, 0, 170 });
        switch (screen)
        {
        case Screen::Login: LoginScreen(); break;
        case Screen::Register: RegisterScreen(false); break;
        case Screen::Reset: RegisterScreen(true); break;
        case Screen::Sms: SmsScreen(); break;
        case Screen::Offer: OfferScreen(); break;
        case Screen::Pay: PayScreen(); break;
        case Screen::Gold: GoldScreen(); break;
        case Screen::Cashout: CashoutScreen(); break;
        case Screen::Message: MessageScreen(); break;
        case Screen::Verify: VerifyScreen(); break;
        case Screen::None: break;
        }
        // Гар самбрын дээр зурагдана, харин товшилтыг түрүүлж авна: эс бөгөөс гарын ард нуугдсан товч/талбар
        // (жишээ нь "НЭВТРЭХ") үсэг дарахад идэвхжинэ.
        if (drawPass) Keyboard();
    }

    void ConfirmLogout()
    {
        message = {};
        message.title = "Гарах уу?";
        message.text = playerName + " дансаас гарна.";
        message.primary = "ГАРАХ";
        message.onPrimary = [] { SignOut(); };
        message.secondary = "Болих";
        message.onSecondary = [] { Open(Screen::None); };
        Open(Screen::Message);
    }
}

void Account::Init(bool on, const std::string& base)
{
    enabled = on;
    if (!enabled) return;
    apiBase = base;
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    // Компьютер: exe-ийн хажууд api.txt байвал серверийн хаягийг тэндээс (жишээ нь http://127.0.0.1:8010/api/)
    if (apiBase.empty())
    {
        const std::string path = std::string(GetApplicationDirectory()) + "api.txt";
        if (FileExists(path.c_str()))
        {
            if (char* text = LoadFileText(path.c_str()))
            {
                apiBase = text;
                UnloadFileText(text);
                while (!apiBase.empty() && (unsigned char)apiBase.back() <= ' ') apiBase.pop_back();
                while (!apiBase.empty() && (unsigned char)apiBase.front() <= ' ') apiBase.erase(0, 1);
            }
        }
    }
#endif
    if (apiBase.empty()) apiBase = Online::DefaultApiBase();
    TraceLog(LOG_INFO, "ACCOUNT: server %s", apiBase.c_str());
    if (apiBase.back() != '/') apiBase += '/';
    deviceId = Online::DeviceId();
    token = Online::Load("token");
    fields[kPhone] = Online::Load("phone");
    CheckSession();
}

bool Account::Enabled() { return enabled; }
bool Account::SignedIn() { return signedIn; }
std::string Account::Name() { return enabled && signedIn ? playerName : std::string(); }
bool Account::Blocking() { return enabled && (screen != Screen::None || request.id >= 0); }

void Account::Tick()
{
    if (!enabled) return;
    Finish(beat);
    Finish(roomRequest);
    Finish(roomBeat);
    Finish(roomStageCall);
    Finish(battleCall);
    Finish(roomSearchCall);
    Finish(soldierLossCall);
    Finish(guestLostCall);
    Finish(logCall);
    // Тоглолтын лог: мөр бүрт одоогийн өрөө, тулаан, үүрэг; 30 секунд тутам (алхам, алдаа гарвал даруй) серверт
    GameLog::SetRoom(roomId, roomBattle, roomRole == Account::RoomRole::Host ? 'h' : roomRole == Account::RoomRole::Guest ? 'g' : 0);
    GameLog::Persist(GetTime());
    if (signedIn && !token.empty() && logCall.id < 0 && GameLog::Ready(GetTime()))
    {
        int count = 0;
        const std::string body = GameLog::TakeBatch(count);
        if (count > 0)
        {
            logCall.id = Online::Send("POST", apiBase + "logs/", body, token);
            logCall.handler = [count](int status, const Json&) {
                // 400 (буруу), 429 (хэт олон): дахин илгээхгүй, хаяна
                if (status == 200 || status == 400 || status == 429) GameLog::Sent(count);
                else GameLog::Failed(GetTime());
            };
        }
    }
    if (guestConfirmPending && roomBeat.id < 0 && GetTime() >= nextGuestConfirm)
    {
        if (roomId < 0 || roomRole != Account::RoomRole::Host) guestConfirmPending = false;
        else SendGuestConfirm();
    }
    if (guestLostDirty && guestLostCall.id < 0 && roomId >= 0 && roomRole == Account::RoomRole::Host)
    {
        guestLostDirty = false;
        const int room = roomId, battle = guestLostBattle, total = guestLostTotal;
        guestLostCall.id = Online::Send("POST", apiBase + "rooms/" + std::to_string(room) + "/guest-lost/",
                                        Obj({ { "battle", std::to_string(battle) }, { "lost", std::to_string(total) } }), token);
        guestLostCall.handler = [](int status, const Json&) {
            if (status == 0 || status >= 500) guestLostDirty = true; // сүлжээ тасарсан: дахин оролдоно
        };
    }
    if (soldierLossDirty && soldierLossCall.id < 0 && ServerShop())
    {
        // Үлдсэн тоог илгээнэ (давтан илгээвэл дахин хасахгүй)
        soldierLossDirty = false;
        const int target = soldierTarget;
        soldierLossCall.id = Online::Send("POST", apiBase + "soldiers/lost/", Obj({ { "soldiers", std::to_string(target) } }), token);
        soldierLossCall.handler = [target](int status, const Json& reply) {
            if (status == 200)
            {
                ReadAccount(reply["account"]);
                return;
            }
            if (status == 0 || status >= 500)
            {
                soldierLossDirty = true; // сүлжээ тасарсан: дахин оролдоно
                soldierTarget = std::min(soldierTarget, target);
            }
        };
    }
    if (roomSearching && roomSearchCall.id < 0 && GetTime() >= nextRoomSearch)
    {
        // Сул өрөө хайна: олдтол 3 секунд тутам (60 секунд хүртэл)
        if (GetTime() > roomSearchUntil)
        {
            roomSearching = false;
            roomSearchNote = roomSearchLast.empty() ? "Одоогоор сул өрөө олдсонгүй. Дараа дахин оролдоно уу" : roomSearchLast;
        }
        else
        {
            nextRoomSearch = GetTime() + 3.0;
            roomSearchCall.id = Online::Send("POST", apiBase + "rooms/join/",
                                             Obj({ { "protocol", std::to_string(Net::kProtocolVersion) } }), token);
            roomSearchCall.handler = [](int status, const Json& reply) {
                if (!roomSearching) return; // цуцалсан (сервер өрөөнд оруулсан бол дохиогүй тул 10 минутад гаргана)
                if (status == 200)
                {
                    ReadAccount(reply["account"]);
                    SetRoom(reply["room"]);
                    roomJoined = roomRole == Account::RoomRole::Guest;
                    roomSearching = false;
                }
                else if (status == 404)
                {
                    roomSearchLast = reply["message"].Str(); // (хувилбар зөрсөн г.м.: хайлт дуусахад харуулна)
                }
                else if (status != 0)
                {
                    roomSearching = false;
                    if (OfferDeviceForRoom(status, reply)) return;
                    const std::string message = reply["message"].Str();
                    roomSearchNote = message.empty() ? "Өрөөнд орж чадсангүй" : message;
                }
            };
        }
    }
    if (roomId >= 0 && roomBeat.id < 0 && GetTime() >= nextRoomBeat)
    {
        // Өрөөнд байгаагаа минут тутам мэдэгдэнэ (тасарвал сервер шийтгэлгүй гаргана)
        nextRoomBeat = GetTime() + 60.0;
        roomBeat.id = Online::Send("POST", apiBase + "rooms/" + std::to_string(roomId) + "/beat/", "{}", token);
        roomBeat.handler = BeatHandler;
    }
    if (!battleReport.empty() && roomId >= 0 && battleCall.id < 0 && GetTime() >= nextBattlePoll)
    {
        // Нөгөө тал мэдэгдтэл (эсвэл сервер 60 секунд хүлээж шийдтэл) давтана
        nextBattlePoll = GetTime() + 5.0;
        if (GetTime() > battleReportDeadline)
        {
            battleReport.clear();
            battleResult = Account::BattleResult::Void;
        }
        else
        {
            SendBattleReport();
        }
    }
    if (!signedIn || token.empty() || beat.id >= 0 || request.id >= 0 || GetTime() < nextBeat) return;
    nextBeat = GetTime() + 60.0;
    beat.id = Online::Send("GET", apiBase + "account/", "", token);
    beat.handler = [](int status, const Json& reply) {
        if (status == 200)
        {
            ReadAccount(reply["account"]);
        }
        else if (status == 401 && signedIn)
        {
            kicked = true; // тоглож байсан бол газрын зураг руу буцна
            Fail(status, reply);
        }
    };
}

bool Account::TakeKicked() { return std::exchange(kicked, false); }

bool Account::Update()
{
    if (!enabled) return false;
    Finish(request);
    Finish(poll);
    if (request.id < 0 && poll.id < 0 && GetTime() >= nextPoll)
    {
        if (screen == Screen::Sms)
        {
            // Gold татах: хугацаа дууссаныг сервер шийдэж Gold-ийг буцаана (асуусаар байна)
            if (GetTime() > smsExpires + 5.0 && !smsCashout)
            {
                ShowMessage("Хугацаа дууссан", "Дахин оролдоно уу.", smsVerify ? Screen::None : smsReset ? Screen::Reset : Screen::Register);
            }
            else PollSms();
        }
        else if (screen == Screen::Pay)
        {
            PollPay();
        }
    }

    pointer = InPointer();
    tapped = InMousePressed(MOUSE_BUTTON_LEFT);
    enter = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || std::exchange(testEnter, false);
    if (request.id >= 0)
    {
        while (GetCharPressed() != 0) {}
        return true; // хүлээж байх хооронд товшилт хүлээж авахгүй
    }
    if (screen == Screen::None)
    {
        if (Hit(badge)) ConfirmLogout();
        if (Hit(topupBadge)) Open(Screen::Gold); // тоглоом дотроос QPay
        if (Hit(cashoutBadge))
        {
            if (verified) OpenCashout();
            else OfferVerify();
        }
        return Blocking();
    }

    // Утасны гар (хөтөч): хуудасны талбарт бичсэнийг тоглоомын талбарт хуулна (Insert-ийн шүүлтүүрээр)
    if (UseNativeKeyboard())
    {
        char buffer[256];
        bool submit = false;
        const int slot = WebFieldPoll(buffer, (int)sizeof(buffer), submit);
        nativeEdit = slot >= 0 && slot < kFieldCount;
        if (nativeEdit)
        {
            if (focus != slot)
            {
                Focus(slot);
                nativeText = fields[slot];
            }
            if (nativeText != buffer)
            {
                nativeText = buffer;
                fields[focus].clear();
                for (const char* p = buffer; *p;)
                {
                    int size = 0;
                    Insert(GetCodepointNext(p, &size));
                    p += size > 0 ? size : 1;
                }
            }
        }
        if (submit) enter = true; // сүүлийн талбарт гарын Enter: НЭВТРЭХ / БҮРТГҮҮЛЭХ
    }

    // Утасны гар дэлгэцийн доод хэсгийг дардаг: идэвхтэй талбар харагдах хэсгээс доош орвол цонхыг дээш шилжүүлнэ
    if (nativeEdit || KeyboardVisible())
    {
        // Гар (утасны эсвэл тоглоомын) идэвхтэй талбарыг дарвал цонхыг дээш шилжүүлнэ
        const float visible = nativeEdit ? (float)ViewHeight() * WebVisibleFraction() - 12.0f
                                         : (float)ViewHeight() - KeyboardHeight() - 12.0f;
        if (focusBottom > visible) panelShift -= focusBottom - visible;
        else if (panelShift < 0.0f && focusBottom < visible - 60.0f) panelShift = std::min(0.0f, panelShift + (visible - 60.0f - focusBottom));
    }
    else
    {
        panelShift = 0.0f;
    }

    // Компьютерийн гар
    if (focus >= 0)
    {
        for (int code = GetCharPressed(); code != 0; code = GetCharPressed()) Insert(code);
        if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) EraseLast(fields[focus]);
        if (IsKeyPressed(KEY_TAB))
        {
            const std::vector<int> list = FieldsOf(screen);
            auto it = std::find(list.begin(), list.end(), focus);
            if (!list.empty()) Focus(it == list.end() || it + 1 == list.end() ? list[0] : *(it + 1));
        }
    }
    else
    {
        while (GetCharPressed() != 0) {}
    }

    drawPass = false;
    const bool tap = tapped;
    Frame();
    // Утасны гараар бичиж байхад талбараас өөр газар товшвол гарыг хаана
    if (tap && nativeEdit) EndNativeEdit();
    // Цонхны гадна (гарын хэсэг биш) товшвол утасны гарыг нууна
    if (tapped && KeyboardVisible()) keyboardOpen = false;
    return Blocking();
}

bool Account::Back()
{
    if (!enabled) return false;
    if (request.id >= 0) return true;
    if (KeyboardVisible())
    {
        keyboardOpen = false;
        return true;
    }
    switch (screen)
    {
    case Screen::Register:
    case Screen::Reset: Open(Screen::Login); return true;
    case Screen::Sms:
        if (smsCashout) CancelCashout(cashoutId);
        else Open(smsVerify ? Screen::None : smsReset ? Screen::Reset : Screen::Register);
        return true;
    case Screen::Cashout: Open(Screen::Gold); return true;
    case Screen::Offer: province = -1; Open(Screen::None); return true;
    case Screen::Gold: Open(Screen::None); return true;
    case Screen::Verify: Open(Screen::None); return true;
    case Screen::Pay:
        ClosePay();
        province = -1;
        Open(Screen::None);
        return true;
    case Screen::Message:
    {
        // Нэвтрэх шаардлагатай үеийн алдаа: цэс нээгдэнэ (тоглоомоос гарах боломжтой)
        if (!signedIn && message.secondary.empty() && message.primary != "ОК") return false;
        const std::function<void()> action = message.secondary.empty() ? message.onPrimary : message.onSecondary;
        if (action) action();
        return true;
    }
    case Screen::Login:
    case Screen::None: return false;
    }
    return false;
}

void Account::Draw()
{
    if (!enabled) return;
    drawPass = true;
    Frame();
    drawPass = false;
    if (UseNativeKeyboard()) WebFieldsEnd();
    if (request.id >= 0)
    {
        // "Түр хүлээнэ үү": доорх цонхны дээгүүр жижиг самбар
        if (screen == Screen::None) DrawRectangle(0, 0, ViewWidth(), ViewHeight(), { 0, 0, 0, 150 });
        const int textWidth = Ui::Measure(busyText.c_str(), 18);
        const Rectangle box = { std::floor(((float)ViewWidth() - (float)textWidth - 90.0f) * 0.5f), (float)ViewHeight() * 0.5f - 34.0f,
                                (float)textWidth + 90.0f, 68.0f };
        Ui::Panel(box, { 12, 16, 22, 250 }, 12.0f);
        Ui::Outline(box, kFrame, 1.5f, 12.0f);
        drawPass = true;
        Spinner({ box.x + 36.0f, box.y + 34.0f }, 13.0f);
        drawPass = false;
        Ui::Text(busyText.c_str(), (int)box.x + 62, (int)box.y + 22, 18, Ui::kText);
    }
}

bool Account::Unlocked(int index) { return !enabled || Owns(index); }

void Account::RequestStart(int index, const char* name, int skin)
{
    province = index;
    startSkin = skin;
    provinceName = name;
    approved = -1;
    if (!signedIn)
    {
        Open(Screen::Login);
        return;
    }
    // Өөр төхөөрөмжөөс төлсөн, админ нээсэн зэргийг тооцохын тулд дансыг шинэчилж байж шийднэ
    Call("GET", "account/", "", [](int status, const Json& reply) {
        if (status == 200)
        {
            ReadAccount(reply["account"]);
            Evaluate();
        }
        else
        {
            Fail(status, reply);
        }
    }, "Шалгаж байна...");
}

int Account::TakeApproved()
{
    const int result = approved;
    approved = -1;
    return result;
}

void Account::TestEnter() { testEnter = true; }

int Account::DrawBadge(int x, int y, bool alignRight)
{
    if (!enabled || !signedIn)
    {
        badge = topupBadge = cashoutBadge = {};
        return 0;
    }
    // Нэг багц: [тоглогч · Gold] [+ Gold авах] [Gold татах] [гарах]: бүгд 36 px өндөр, дүрстэй
    const float h = 36.0f, cy = (float)y + h * 0.5f;
    const Color ink = { 34, 24, 6, 255 }, goldInk = { 255, 205, 90, 255 };
    {
        // Нийт өргөн (баруун ирмэгт тэгшлэхэд), ард нь нэг шилэн самбар
        const int chipW = Ui::Measure(playerName.c_str(), 15) + Ui::MeasureBold(Thousands(gold).c_str(), 17) + 76;
        const int total = chipW + 8 + Ui::MeasureBold("Gold авах", 15) + 56 + 8 +
                          Ui::MeasureBold(verified ? "Gold татах" : "Баталгаажуулах", 15) + 54 + 8 + (int)h;
        if (alignRight) x -= total;
        const Rectangle bar = { (float)x - 7.0f, (float)y - 7.0f, (float)total + 14.0f, h + 14.0f };
        Ui::Panel(bar, { 8, 10, 14, 175 }, bar.height * 0.5f);
        Ui::Outline(bar, { 255, 200, 90, 80 }, 1.0f, bar.height * 0.5f);
    }
    int width = 0;
    // Тоглогч ба Gold: бараан хавтан, Gold-ын зоос, нэр бүдэг, тоо тод алтан
    {
        const std::string amount = Thousands(gold);
        const int nameW = Ui::Measure(playerName.c_str(), 15), amountW = Ui::MeasureBold(amount.c_str(), 17);
        const Rectangle chip = { (float)x, (float)y, (float)(nameW + amountW) + 76.0f, h };
        Ui::Panel(chip, { 14, 18, 26, 232 }, h * 0.5f);
        Ui::Outline(chip, { 255, 255, 255, 34 }, 1.0f, h * 0.5f);
        Ui::Coin({ chip.x + 20.0f, cy }, 9.0f);
        Ui::Text(playerName.c_str(), (int)chip.x + 36, (int)cy - 10, 15, Ui::kTextDim);
        DrawCircleV({ chip.x + 44.0f + (float)nameW, cy }, 2.0f, Fade(Ui::kTextDim, 0.7f));
        Ui::TextBold(amount.c_str(), (int)(chip.x + 54.0f + (float)nameW), (int)cy - 11, 17, goldInk);
        Ui::Text("G", (int)(chip.x + 57.0f + (float)(nameW + amountW)), (int)cy - 8, 13, Fade(goldInk, 0.75f));
        width = (int)chip.width;
    }
    // + Gold авах: алтан, дээд хагас нь тод (гэрэлтсэн), "+" тойрог дүрстэй
    {
        const char* label = "Gold авах";
        topupBadge = { (float)(x + width + 8), (float)y, (float)Ui::MeasureBold(label, 15) + 56.0f, h };
        const bool hover = !InTouchMode() && CheckCollisionPointRec(InPointer(), topupBadge);
        DrawRectangleRounded(topupBadge, 0.5f, 10, hover ? Color{ 255, 222, 120, 255 } : Color{ 242, 178, 48, 255 });
        DrawRectangleRounded({ topupBadge.x + 3.0f, topupBadge.y + 2.0f, topupBadge.width - 6.0f, h * 0.45f }, 0.6f, 8, { 255, 255, 255, 46 });
        const Vector2 icon = { topupBadge.x + 20.0f, cy };
        DrawCircleV(icon, 10.0f, ink);
        DrawRectangleRec({ icon.x - 5.0f, icon.y - 1.2f, 10.0f, 2.4f }, goldInk);
        DrawRectangleRec({ icon.x - 1.2f, icon.y - 5.0f, 2.4f, 10.0f }, goldInk);
        Ui::TextBold(label, (int)topupBadge.x + 38, (int)cy - 10, 15, ink);
        width += 8 + (int)topupBadge.width;
    }
    // Утас баталгаажаагүй: бараан, улбар шар хүрээ нь лугшина, "!" тойрог (Gold нэмэгдэх бүх зүйл баталгаажуулалтын дараа)
    if (!verified)
    {
        const char* label = "Баталгаажуулах";
        const Color orange = { 255, 140, 56, 255 };
        cashoutBadge = { (float)(x + width + 8), (float)y, (float)Ui::MeasureBold(label, 15) + 54.0f, h };
        const bool hover = !InTouchMode() && CheckCollisionPointRec(InPointer(), cashoutBadge);
        const float pulse = 0.5f + 0.5f * std::sin((float)GetTime() * 3.0f);
        Ui::Panel(cashoutBadge, hover ? Color{ 70, 34, 12, 240 } : Color{ 36, 20, 10, 232 }, h * 0.5f);
        Ui::Outline(cashoutBadge, Fade(orange, hover ? 1.0f : 0.55f + 0.45f * pulse), 1.5f, h * 0.5f);
        const Vector2 icon = { cashoutBadge.x + 20.0f, cy };
        DrawCircleV(icon, 10.0f, orange);
        Ui::TextBold("!", (int)icon.x - 2, (int)cy - 11, 18, { 36, 20, 10, 255 });
        Ui::TextBold(label, (int)cashoutBadge.x + 36, (int)cy - 10, 15, { 255, 176, 112, 255 });
        width += 8 + (int)cashoutBadge.width;
    }
    // Gold татах: бараан, алтан хүрээтэй, "доош сум тавиур руу" дүрс
    else
    {
        const char* label = "Gold татах";
        cashoutBadge = { (float)(x + width + 8), (float)y, (float)Ui::MeasureBold(label, 15) + 54.0f, h };
        const bool hover = !InTouchMode() && CheckCollisionPointRec(InPointer(), cashoutBadge);
        Ui::Panel(cashoutBadge, hover ? Color{ 64, 48, 14, 240 } : Color{ 30, 24, 10, 232 }, h * 0.5f);
        Ui::Outline(cashoutBadge, Fade(goldInk, hover ? 1.0f : 0.75f), 1.5f, h * 0.5f);
        const float ix = cashoutBadge.x + 20.0f;
        DrawLineEx({ ix, cy - 8.0f }, { ix, cy + 3.0f }, 2.4f, goldInk);
        DrawLineEx({ ix - 5.0f, cy - 2.0f }, { ix, cy + 3.5f }, 2.4f, goldInk);
        DrawLineEx({ ix + 5.0f, cy - 2.0f }, { ix, cy + 3.5f }, 2.4f, goldInk);
        DrawLineEx({ ix - 8.0f, cy + 4.0f }, { ix - 8.0f, cy + 8.0f }, 2.0f, goldInk);
        DrawLineEx({ ix - 8.0f, cy + 8.0f }, { ix + 8.0f, cy + 8.0f }, 2.0f, goldInk);
        DrawLineEx({ ix + 8.0f, cy + 8.0f }, { ix + 8.0f, cy + 4.0f }, 2.0f, goldInk);
        Ui::TextBold(label, (int)cashoutBadge.x + 36, (int)cy - 10, 15, goldInk);
        width += 8 + (int)cashoutBadge.width;
    }
    // Гарах: дугуй дүрст товч (хаалганаас гарах сум)
    {
        badge = { (float)(x + width + 8), (float)y, h, h };
        const bool hover = !InTouchMode() && CheckCollisionPointRec(InPointer(), badge);
        Ui::Panel(badge, hover ? Color{ 70, 30, 30, 235 } : Color{ 14, 18, 26, 232 }, h * 0.5f);
        Ui::Outline(badge, hover ? Color{ 240, 110, 100, 220 } : Color{ 255, 255, 255, 40 }, 1.0f, h * 0.5f);
        const Color c = hover ? Color{ 255, 160, 150, 255 } : Ui::kTextDim;
        const float bx = badge.x + h * 0.5f;
        DrawRectangleLinesEx({ bx - 9.0f, cy - 8.0f, 9.0f, 16.0f }, 2.0f, c); // хаалга
        DrawLineEx({ bx - 3.0f, cy }, { bx + 9.0f, cy }, 2.2f, c);
        DrawLineEx({ bx + 5.0f, cy - 4.0f }, { bx + 9.0f, cy }, 2.2f, c);
        DrawLineEx({ bx + 5.0f, cy + 4.0f }, { bx + 9.0f, cy }, 2.2f, c);
        width += 8 + (int)badge.width;
    }
    return width;
}

bool Account::ServerShop() { return enabled && signedIn; }
bool Account::Verified() { return !enabled || !signedIn || verified; }
void Account::OpenVerify()
{
    if (ServerShop() && !verified) OfferVerify();
}
bool Account::TakeNotice(std::string& text)
{
    if (pendingNote.empty()) return false;
    text = std::exchange(pendingNote, std::string());
    return true;
}
int Account::Gold() { return gold; }
int Account::UpgradeLevel(int item) { return item >= 0 && item < kUpgradeCount ? upgradeLevels[item] : 0; }

int Account::UpgradePrice(int item)
{
    if (item == kSoldierUpgrade) return upgradePrices[item]; // цэрэг бүр ижил үнэтэй
    return item >= 0 && item < kUpgradeCount ? upgradePrices[item] * (upgradeLevels[item] + 1) : 0;
}

void Account::LoseSoldiers(int count)
{
    if (!ServerShop() || count <= 0) return;
    upgradeLevels[kSoldierUpgrade] = std::max(0, upgradeLevels[kSoldierUpgrade] - count);
    soldierTarget = upgradeLevels[kSoldierUpgrade];
    soldierLossDirty = true;
}

void Account::BuyUpgrade(int item)
{
    if (!ServerShop() || item < 0 || item >= kUpgradeCount || request.id >= 0) return;
    // Тоглоомын харж буй түвшинг илгээнэ: сервер таарахгүй бол (давхар дарсан) дахин авахгүй
    Call("POST", "shop/buy/",
         Obj({ { "item", JsonQuote(kUpgradeNames[item]) }, { "level", std::to_string(upgradeLevels[item]) } }),
         [](int status, const Json& reply) {
             if (status == 200)
             {
                 ReadAccount(reply["account"]);
                 return;
             }
             if (status == 409) Background("GET", "account/", "", [](int code, const Json& fresh) {
                 if (code == 200) ReadAccount(fresh["account"]);
             });
             Fail(status, reply);
         }, "Худалдаж авч байна...");
}

Account::RoomRole Account::CurrentRoomRole() { return enabled ? roomRole : RoomRole::None; }
const std::string& Account::RoomTicket() { return roomTicket; }
int Account::RoomProvince() { return roomProvince; }
const std::string& Account::RoomCode() { return roomCode; }
bool Account::RoomPvp() { return enabled && roomPvp; }
bool Account::RoomCoop() { return enabled && roomCoop; }

void Account::JoinCoop(const std::string& code)
{
    if (!ServerShop() || roomSearchCall.id >= 0) return;
    roomSearching = false;
    roomSearchNote.clear();
    roomSearchCall.id = Online::Send("POST", apiBase + "rooms/coop/",
                                     Obj({ { "code", JsonQuote(code) }, { "protocol", std::to_string(Net::kProtocolVersion) } }),
                                     token);
    roomSearchCall.handler = [](int status, const Json& reply) {
        if (status == 200)
        {
            ReadAccount(reply["account"]);
            SetRoom(reply["room"]);
            roomJoined = roomRole == Account::RoomRole::Guest;
            return;
        }
        if (OfferDeviceForRoom(status, reply)) return;
        const std::string message = reply["message"].Str();
        roomSearchNote = status == 0 ? "Сервертэй холбогдож чадсангүй. Интернэтээ шалгана уу"
                       : message.empty() ? "Найзын өрөөнд орж чадсангүй" : message;
    };
}
int Account::GoldAvailable() { return std::max(0, gold - goldLocked); }
int Account::RoomEntry() { return roomEntry; }
bool Account::PremiumSkin(int skin) { return std::find(premiumSkins.begin(), premiumSkins.end(), skin) != premiumSkins.end(); }
int Account::SkinPrice(int skin) { return skin >= 0 && skin < 8 ? skinPrices[skin] : skinPrice; }
int Account::CoopReward() { return coopReward; }

void Account::JoinRoom()
{
    if (!ServerShop() || roomSearching) return;
    roomSearching = true;
    roomSearchUntil = GetTime() + 60.0;
    nextRoomSearch = 0.0;
    roomSearchNote.clear();
    roomSearchLast.clear();
}

bool Account::SearchingRoom() { return roomSearching; }
void Account::CancelRoomSearch() { roomSearching = false; }

bool Account::TakeSearchNote(std::string& text)
{
    if (roomSearchNote.empty()) return false;
    text = std::exchange(roomSearchNote, std::string());
    return true;
}

void Account::OpenGoldShop()
{
    if (ServerShop()) Open(Screen::Gold);
}

void Account::OpenCashout()
{
    if (!ServerShop()) return;
    if (!cashoutKnown) ShowMessage("Gold татах", "Gold татах үйлчилгээ одоогоор ажиллахгүй байна. Дараа дахин оролдоно уу.", Screen::None);
    else if (!cashoutEnabled) ShowMessage("Gold татах", "Gold татах түр хаалттай байна.", Screen::None);
    else Open(Screen::Cashout);
}

bool Account::TakeJoinedRoom() { return std::exchange(roomJoined, false); }
void Account::LeaveRoom() { RoomCall("leave"); }
void Account::CompleteRoom() { RoomCall("complete"); }

void Account::ConfirmGuest()
{
    if (roomId < 0 || roomRole != RoomRole::Host) return;
    roomGuestSoldiers = roomGuestHealthLevel = -1;
    guestConfirmPending = true;
    SendGuestConfirm();
}

int Account::RoomGuestSoldiers() { return roomGuestSoldiers; }
int Account::RoomGuestHealthLevel() { return roomGuestHealthLevel; }

void Account::RefreshRoom()
{
    if (roomId < 0 || roomBeat.id >= 0) return;
    nextRoomBeat = GetTime() + 60.0;
    roomBeat.id = Online::Send("POST", apiBase + "rooms/" + std::to_string(roomId) + "/beat/", "{}", token);
    roomBeat.handler = BeatHandler;
}

int Account::RoomBattle() { return roomBattle; }

void Account::ReportGuestSoldiersLost(int total)
{
    if (roomId < 0 || roomRole != RoomRole::Host || total <= 0) return;
    if (guestLostBattle != roomBattle) guestLostTotal = 0; // (шинэ тулаан)
    guestLostBattle = roomBattle;
    if (total <= guestLostTotal) return;
    guestLostTotal = total;
    guestLostDirty = true;
}
const std::string& Account::RoomStage() { return roomStage; }
int Account::RoomRecruits() { return roomRecruits; }

void Account::UploadStage(const std::string& stage, int recruits)
{
    if (roomId < 0 || roomRole != RoomRole::Host || roomStageCall.id >= 0) return;
    roomStageCall.id = Online::Send("POST", apiBase + "rooms/" + std::to_string(roomId) + "/stage/",
                                    Obj({ { "stage", JsonQuote(stage) }, { "recruits", std::to_string(recruits) } }), token);
    roomStageCall.handler = [](int, const Json&) {};
}

void Account::ReportBattleUndecided()
{
    if (roomId < 0 || !battleReport.empty()) return;
    battleReport = "none"; // энэ тоглоомд тулаан шийдэгдээгүй: нөгөө талын мэдэгдэлтэй зөрж хүчингүй болно
    battleAbsent = false;
    battleReportFor = roomBattle;
    battleReportDeadline = GetTime() + 180.0;
    nextBattlePoll = 0.0;
    battleResult = BattleResult::Pending;
}

void Account::ReportBattle(bool guestWon, bool opponentAbsent)
{
    if (roomId < 0 || !battleReport.empty()) return;
    battleReport = guestWon ? "guest" : "host";
    battleAbsent = opponentAbsent;
    battleReportFor = roomBattle;
    battleReportDeadline = GetTime() + 180.0; // сервер 60 секунд хүлээгээд нэг талын мэдэгдлээр шийднэ
    nextBattlePoll = 0.0;
    battleResult = BattleResult::Pending;
}

bool Account::BattlePending() { return battleResult == BattleResult::Pending; }

Account::BattleResult Account::TakeBattleResult()
{
    if (battleResult == BattleResult::Pending || battleResult == BattleResult::None) return battleResult;
    return std::exchange(battleResult, BattleResult::None);
}
