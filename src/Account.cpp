#include "Account.h"

#include "Input.h"
#include "Json.h"
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

    // --- Цонх ---
    Screen screen = Screen::None;
    Message message;
    Pending request, poll; // request: цонхыг түр хаадаг ("түр хүлээнэ үү"), poll: ард нь (SMS, төлбөр шалгах)
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
    bool nativeEdit = false, nativeBegun = false; // хөтөч (утас): утасны өөрийн гараар бичиж байна
    std::string nativeText;                       // хуудасны талбарын сүүлд уншсан утга
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

    std::string Money(int amount)
    {
        std::string digits = std::to_string(amount), out;
        for (size_t i = 0; i < digits.size(); ++i)
        {
            if (i > 0 && (digits.size() - i) % 3 == 0) out += ' ';
            out += digits[i];
        }
        return out + "₮";
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
        default: return {};
        }
    }

    void Focus(int index)
    {
        focus = index;
        layout = index == kPhone ? 2 : index == kPassword ? 1 : 0;
        shift = index == kName && fields[kName].empty();
    }

    const char* Placeholder(int index)
    {
        return index == kName ? "Нэр" : index == kPhone ? "Утасны дугаар" : "Нууц үг";
    }

    // Дараагийн талбар (сүүлийнх бол -1)
    int NextField()
    {
        const std::vector<int> list = FieldsOf(screen);
        auto it = std::find(list.begin(), list.end(), focus);
        return it == list.end() || it + 1 == list.end() ? -1 : *(it + 1);
    }

    // Хөтөч дээр мэдрэгчтэй горимд дэлгэцийн гарын оронд утасны өөрийн гарыг (Platform: WebEdit*) хэрэглэнэ.
    bool UseNativeKeyboard()
    {
#if defined(__EMSCRIPTEN__)
        return InTouchMode();
#else
        return false;
#endif
    }

    void BeginNativeEdit(int index)
    {
        nativeEdit = nativeBegun = true;
        nativeText = fields[index];
        WebEditBegin(fields[index].c_str(), index == kPhone ? 1 : index == kPassword ? 2 : 0, Placeholder(index), NextField() < 0);
    }

    void EndNativeEdit()
    {
        if (!nativeEdit) return;
        nativeEdit = false;
        WebEditEnd();
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
        capitalPrice = prices["capital"].Int(capitalPrice);
        maxDevices = account["max_devices"].Int(maxDevices);
    }

    void SignOut()
    {
        if (!token.empty()) Online::Send("POST", apiBase + "logout/", "{}", token); // хариуг хүлээхгүй
        token.clear();
        Online::Store("token", "");
        signedIn = false;
        owned.clear();
        province = -1;
        Open(Screen::Login);
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

    void SignIn(const Json& reply)
    {
        token = reply["token"].Str();
        Online::Store("token", token);
        Online::Store("phone", fields[kPhone]);
        ReadAccount(reply["account"]);
        signedIn = true;
        fields[kPassword].clear();
        Open(Screen::None);
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

    void StartSms(const Json& reply, bool reset)
    {
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

    void Register()
    {
        if (!CheckFields(true, true)) return;
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

    // Мессеж ирсэн эсэхийг асууна (ирээгүй бол сервер 409 буцаана).
    void PollSms()
    {
        const std::string id = std::to_string(verificationId);
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
        Call("POST", "provinces/start/", Obj({ { "province", std::to_string(province) } }), [](int status, const Json& reply) {
            if (status == 200)
            {
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

    void Paid()
    {
        ClosePay();
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
        if (drawPass)
        {
            const bool focused = focus == index;
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
            if (UseNativeKeyboard()) BeginNativeEdit(index);
            else keyboardOpen = InTouchMode();
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

    // Дээд талд нь бичиж буй талбарын харагдац (самбар гарын ард нуугдсан ч юу бичсэнээ харна), доор нь товчнууд
    const float kPreviewHeight = 60.0f;
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
        // Бичиж буй талбар
        const float previewWidth = std::min(width - 24.0f, 720.0f);
        FieldBox({ (width - previewWidth) * 0.5f, top + 8.0f, previewWidth, kPreviewHeight - 12.0f }, focus, Placeholder(focus));

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
        const Rectangle area = { std::floor(((float)ViewWidth() - width) * 0.5f), std::max(8.0f, std::floor((bottom - height) * 0.5f)),
                                 width, height };
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
            Ui::Text(reset ? "Утсаа мессежээр баталгаажуулж шинэ нууц үг тавина" : "Утсаа мессежээр баталгаажуулна", (int)x,
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
            Wrap("Утасны дугаараа баталгаажуулахын тулд " + fields[kPhone] + " дугаартай утаснаасаа доорх мессежийг илгээнэ үү.", 16, inner);
        const bool touch = InTouchMode();
        const float height = 22.0f + 40.0f + (float)lines.size() * LineHeight(16) + 14.0f + 104.0f + 16.0f + 28.0f + 34.0f +
                             (touch ? 62.0f : 0.0f) + 30.0f + 22.0f;
        const Rectangle panel = Panel(width, height);
        const float x = panel.x + 28.0f;
        float y = panel.y + 22.0f;
        Title("УТАС БАТАЛГААЖУУЛАХ", x, y);
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
        if (Button({ x, y, inner, 30.0f }, "Цуцлах", Style::Link)) Open(smsReset ? Screen::Reset : Screen::Register);
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
            Ui::Text(invoiceKind == "pack"      ? TextFormat("%d аймгийн эрх", packSize)
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
        case Screen::Message: MessageScreen(); break;
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
            if (GetTime() > smsExpires + 5.0) ShowMessage("Хугацаа дууссан", "Дахин оролдоно уу.", smsReset ? Screen::Reset : Screen::Register);
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
        return Blocking();
    }

    // Утасны гар (хөтөч): хуудасны талбарт бичсэнийг тоглоомын талбарт хуулна (Insert-ийн шүүлтүүрээр)
    nativeBegun = false;
    if (nativeEdit && focus >= 0)
    {
        char buffer[256];
        const int state = WebEditRead(buffer, (int)sizeof(buffer));
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
        if (state < 0)
        {
            nativeEdit = false; // хэрэглэгч гарыг хаасан
        }
        else if (state == 1)
        {
            // Гарын Enter: дараагийн талбар руу, сүүлийнх бол илгээнэ (НЭВТРЭХ / БҮРТГҮҮЛЭХ)
            const int next = NextField();
            if (next >= 0)
            {
                Focus(next);
                BeginNativeEdit(next);
            }
            else
            {
                EndNativeEdit();
                enter = true;
            }
        }
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
    if (tap && nativeEdit && !nativeBegun) EndNativeEdit();
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
    case Screen::Sms: Open(smsReset ? Screen::Reset : Screen::Register); return true;
    case Screen::Offer: province = -1; Open(Screen::None); return true;
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

void Account::RequestStart(int index, const char* name)
{
    province = index;
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

int Account::DrawBadge(int x, int y)
{
    if (!enabled || !signedIn)
    {
        badge = {};
        return 0;
    }
    const std::string text = playerName + "  ·  үнэгүй " + std::to_string(freeLeft) + "  ·  эрх " + std::to_string(credits);
    const int width = Ui::Chip(text.c_str(), x, y, Ui::kText, Ui::kAccent);
    const char* label = "Гарах";
    badge = { (float)(x + width + 6), (float)y, (float)(Ui::Measure(label, 15) + 24), 32.0f };
    const bool hover = !InTouchMode() && CheckCollisionPointRec(InPointer(), badge);
    Ui::Panel(badge, hover ? Color{ 40, 46, 58, 220 } : Ui::kPanel, 16.0f);
    Ui::Text(label, (int)badge.x + 12, y + 7, 15, hover ? Ui::kText : Ui::kTextDim);
    return width + 6 + (int)badge.width;
}
