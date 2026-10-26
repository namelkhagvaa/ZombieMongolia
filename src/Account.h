#pragma once

#include <string>

// Тоглогчийн данс: бүртгэл, аймгийн эрх, төлбөр (backend/: Django API, сүлжээ нь Online.h).
//
// Бүх цонх тоглоом дотор (raylib) зурагдана: бүтэн дэлгэц, Windows, Android, хөтөч дээр ижил.
//   нэвтрэх, бүртгүүлэх — утсаар (баталгаажуулалтгүй, шууд); утсаа дараа нь баталгаажуулбал Gold нэмэгдэх бүх зүйл нээгдэнэ
//   нууц үг сэргээх, Gold татах — утсыг 144773 руу мессеж илгээж баталгаажуулна (verify.mn)
//   аймаг — бүгд үнэгүй (сервер бүгдийг нээлттэй гэж хариулна)
//   Gold — серверт хадгалагдана: газрын зургийн дэлгүүрийн сайжруулалтыг Gold-оор серверээр авна (Shop.cpp)
//   QPay — QR зураг, утсан дээр банкны аппын товч; төлбөр орсныг хэдэн секунд тутам шалгана
//   мэдрэгчтэй дэлгэц дээр өөрийн гар (кирилл, латин, тоо)
// Газрын зураг дээр нэвтрээгүй бол нэвтрэх цонх гарна; аймаг эхлэхэд сервер эрхийг шалгана (интернэт шаардлагатай).
namespace Account
{
    // enabled = false (туршилтын горим): бүх аймаг нээлттэй, цонх гарахгүй. apiBase хоосон бол Online::DefaultApiBase().
    void Init(bool enabled, const std::string& apiBase);
    bool Enabled();
    bool SignedIn();
    // Бүртгэлтэй нэр (хамтарсан тоглолтод харагдана); нэвтрээгүй бол хоосон.
    std::string Name();

    // Тоглоомын frame бүрд (газрын зураг, даалгавар): нэвтрэлтийг минут тутам шалгана.
    void Tick();
    // Өөр төхөөрөмж дээр нэвтэрсэн тул энд гарсан (нэг удаа true): даалгавар тоглож байсан бол зогсооно.
    bool TakeKicked();

    // Газрын зургийн frame бүрд: цонх нээлттэй (тоглоомын оролтыг хаана) бол true.
    bool Update();
    bool Blocking();
    // ESC / утасны "буцах": өмнөх цонх руу буцсан бол true (нэвтрэх цонх дээр false: тоглоомын цэс нээгдэнэ).
    bool Back();
    void Draw();

    // Энэ аймаг тоглогчид нээгдсэн эсэх (Enabled биш бол үргэлж true).
    bool Unlocked(int province);
    // Аймаг эхлүүлэх: сервер шалгана, шаардлагатай бол үнэгүй эрх ашиглах эсвэл төлөх цонх нээнэ.
    // skin: 1-р дайчны дүр (онцгой дүр бол сервер SkinPrice Gold авна).
    void RequestStart(int province, const char* name, int skin = 0);
    // Онцгой дүр (DEMON, IRON): тэдгээрээр аймаг эхлүүлэх бүрийн үнэ
    bool PremiumSkin(int skin);
    int SkinPrice(int skin);
    // Найзтайгаа кодоор хамт давсны шагнал, хүн бүрд (серверийн тохиргоо)
    int CoopReward();
    // Зөвшөөрөгдсөн (эрх нь баталгаажсан) аймаг; байхгүй бол -1. Frame бүрд асууна.
    int TakeApproved();

    // Газрын зургийн дээд мөрөнд: "Бат · 500 Gold" чип ба "Гарах" товч. Өргөнийг буцаана (нэвтрээгүй бол 0).
    // alignRight: x нь баруун ирмэг (баруун дээд буланд)
    int DrawBadge(int x, int y, bool alignRight = false);

    // Gold ба сайжруулалт (серверт). Нэвтэрсэн үед л үйлчилнэ (ServerShop); эс бөгөөс Shop.cpp төхөөрөмж дээрх
    // зоосоор (туршилтын горим). item: ShopItem-ийн дараалал (0 хохирол, 1 буудах хурд, 2 сумны хайрцаг, 3 амь, 4 цэрэг).
    bool ServerShop();
    // Утас баталгаажсан (эсвэл данс идэвхгүй). Баталгаажаагүй бол Gold нэмэгдэхгүй: шагнал, хожил, татах.
    bool Verified();
    void OpenVerify(); // баталгаажуулах санал (утасны мессежийн цонх)
    // Нэг удаа: тоглоомд харуулах мэдэгдэл (жишээ нь баталгаажаагүй тул олгоогүй шагнал)
    bool TakeNotice(std::string& text);
    int Gold();
    int UpgradeLevel(int item);
    int UpgradePrice(int item); // дараагийн түвшний үнэ (сервер: суурь үнэ x (түвшин + 1))
    // Дараагийн түвшинг серверээр авна (хариу ирэхэд түвшин, Gold шинэчлэгдэнэ; алдааг цонхоор харуулна).
    void BuyUpgrade(int item);
    // Тоглолтод Gold-оор авсан count цэрэг үхэв: шууд хасна (серверт ард нь илгээнэ, тасарвал дахин оролдоно)
    void LoseSoldiers(int count);
    // Хост: энэ тулаанд зочны Gold-оор авсан хэдэн цэрэг үхсэн (нийт): сервер зочноос хасна (зочин тасарсан ч).
    // Зочин ч өөрөө мэдэгдэнэ (SoldierLost): хоёулаа "хамгийн ихдээ ийм олон үлдэнэ" тул давхар хасагдахгүй.
    void ReportGuestSoldiersLost(int total);

    // Нээлттэй өрөө. Аймаг эхлэхэд (RequestStart) Gold хүрэлцвэл (RoomEntry: 20 + 50) сервер заавал өрөө нээж
    // Host болгоно; эс бөгөөс ганцаарчилсан (None): хэн ч орж ирэхгүй, Gold өөрчлөгдөхгүй. Зочин JoinRoom-оор
    // урилгагүй орно: сервер өрөөг сонгож, тасалбар (relay, Net::Session::RequestJoinTicket) өгнө.
    enum class RoomRole { None, Host, Guest };
    RoomRole CurrentRoomRole();
    const std::string& RoomTicket();
    int RoomProvince();
    // Өрөөний найзын код (4 орон); өөр тоглогч урилгагүй орж тулалддаг (Gold хүрэлцсэн) эсэх; энэ тоглогч (эсвэл
    // зочин нь) найзын кодоор орсон хамт тоглолт эсэх
    const std::string& RoomCode();
    bool RoomPvp();
    bool RoomCoop();
    // Найзын кодоор нэгдэх: хамт тоглолт (тулаангүй, төлбөргүй). Амжилттай бол TakeJoinedRoom, эс бөгөөс
    // TakeSearchNote-д шалтгаан.
    void JoinCoop(const std::string& code);
    int GoldAvailable(); // барьцаалаагүй Gold
    int RoomEntry();
    // Сул өрөө олдох хүртэл (60 секунд) хайж, олдмогц оруулна; CancelRoomSearch-ээр зогсоно.
    void JoinRoom();
    bool SearchingRoom();
    void CancelRoomSearch();
    bool TakeSearchNote(std::string& text); // нэг удаа: хайлтын үр дүн ("өрөө олдсонгүй" г.м.)
    bool TakeJoinedRoom(); // нэг удаа: JoinRoom амжилттай (RoomTicket, RoomProvince бэлэн)
    // Тоглоом дотроос Gold авах: багц сонгоод QPay (QR, банкны апп) — тоглоомоос гарахгүй.
    void OpenGoldShop();
    // Gold татах цонх (тоглоомоос хожсон Gold-оо банкны данс руу)
    void OpenCashout();
    // Санаатай гарах: хост даалгавар дуусаагүй байхад гарвал ялагдал (барьцаа шатна). Зочин: барьцаа суллагдана.
    void LeaveRoom();
    // Хост: сүүлийн даалгавар биелэв (+40 Gold, өрөө хаагдана).
    void CompleteRoom();
    // Хост: өрөөний одоогийн тулааны дугаар, зочны нэрийг серверээс шууд асууна (зочин орж ирэхэд).
    void RefreshRoom();
    // Хост: зочин тоглоомоор холбогдлоо (серверт мэдэгдэнэ: тулаан тоологдож эхэлнэ, түүнээс өмнө гарвал төлбөр буцна).
    // Хариунд зочны серверийн мэдээлэл: Gold-оор авсан цэрэг, амийн түвшин (-1 = хараахан мэдэхгүй).
    void ConfirmGuest();
    int RoomGuestSoldiers();
    int RoomGuestHealthLevel();
    int RoomBattle();
    // Хост: даалгаврын ахиц, амьд элсүүлсэн цэргийн тоог серверт хадгална (зочин ялбал тэр үргэлжлүүлнэ).
    void UploadStage(const std::string& stage, int recruits);
    // Шинэ хост болсон үед (зочин ялсан): өрөөний ахиц ба өмнөх хостын цэргүүд.
    const std::string& RoomStage();
    int RoomRecruits();
    // Тулааны үр дүнг мэдэгдэнэ (guestWon: зочин ялсан). Шийдэгдтэл давтан асууна (Tick).
    // opponentAbsent: нөгөө тал тасарсан (түүний багийг AI тулалдуулсан): зөрвөл сүлжээний асуудал, маргаанд тоологдохгүй
    void ReportBattle(bool guestWon, bool opponentAbsent = false);
    // Энэ тоглоомд тулаан шийдэгдээгүй (нөгөө тал ялагч мэдэгдсэн ч энд хоёр баг амьд): хүчингүй болгоно
    void ReportBattleUndecided();
    enum class BattleResult { None, Pending, Won, Lost, Void };
    // Нэг удаа: шийдэгдсэн үр дүн (Won/Lost: энэ тоглогч; Void: зөрсөн эсвэл өрөө хаагдсан). Ялсан зочин бол
    // CurrentRoomRole() нь Host болсон байна (RoomTicket, RoomStage, RoomRecruits).
    BattleResult TakeBattleResult();
    // Тулааны үр дүнг мэдэгдсэн, сервер шийдээгүй байна (Pending)
    bool BattlePending();

    // Туршилт: дараагийн Update-д цонхны үндсэн товчийг (Enter) дарсан мэт.
    void TestEnter();
}
