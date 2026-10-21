#include "CampaignData.h"

#include "Progress.h"
#include "raylib.h"

#include <algorithm>

// Аяны 22 үеийн даалгаврын өгөгдөл.
//
// Даалгавар бүрийн сүүлчийн параметр "after" нь урьдчилсан нөхцөл: тэр индекстэй даалгавар биелсний
// дараа идэвхжинэ. Хоёр даалгавар ижил "after"-тай бол зэрэг идэвхжиж, аль нь ч түрүүлж биелж болно.
// -1 бол үе эхлэхэд шууд идэвхтэй.

namespace
{
    const SiteRef kPost = { SiteKind::Checkpoint, 0 };
    const SiteRef kTown = { SiteKind::Settlement, 0 };
    const SiteRef kGas = { SiteKind::GasStation, 0 };
    const SiteRef kStore = { SiteKind::Warehouse, 0 };
    SiteRef Camp(int ordinal) { return { SiteKind::Camp, ordinal }; }
    // Хотын жинхэнэ газрууд (хамгийн ойрын нь)
    const SiteRef kHospital = { SiteKind::Hospital, 0 };
    const SiteRef kPolice = { SiteKind::Police, 0 };
    const SiteRef kGovernment = { SiteKind::Government, 0 };
    const SiteRef kMuseum = { SiteKind::Museum, 0 };
    const SiteRef kStadium = { SiteKind::Stadium, 0 };
    const SiteRef kMarket = { SiteKind::Market, 0 };
    const SiteRef kSchool = { SiteKind::School, 0 };
    const SiteRef kTemple = { SiteKind::Temple, 0 };
    SiteRef Hamlet(int ordinal) { return { SiteKind::Hamlet, ordinal }; }

    ObjectiveDef Base(const char* id, const char* title, const char* description, ObjectiveType type, SiteRef site,
                      int after)
    {
        ObjectiveDef def = {};
        def.id = id;
        def.title = title;
        def.description = description;
        def.type = type;
        def.site = site;
        def.after = after;
        return def;
    }

    ObjectiveDef Reach(const char* id, const char* title, const char* description, SiteRef site, int after = -1)
    {
        ObjectiveDef def = Base(id, title, description, ObjectiveType::ReachLocation, site, after);
        def.reward = 25;
        return def;
    }

    ObjectiveDef Interact(const char* id, const char* title, const char* description, SiteRef site, PropKind prop,
                          const char* action, int after = -1, int guards = 4)
    {
        ObjectiveDef def = Base(id, title, description, ObjectiveType::InteractObject, site, after);
        def.prop = prop;
        def.action = action;
        def.guards = guards;
        return def;
    }

    ObjectiveDef Rescue(const char* id, const char* title, const char* description, SiteRef site, int count,
                        SurvivorKind kind, int after = -1, int guards = 5)
    {
        ObjectiveDef def = Base(id, title, description, ObjectiveType::RescueSurvivors, site, after);
        def.count = count;
        def.survivor = kind;
        def.action = kind == SurvivorKind::Soldier ? "Цэргийг аврах" : "Иргэнийг аврах";
        def.guards = guards;
        def.reward = 60;
        return def;
    }

    ObjectiveDef Collect(const char* id, const char* title, const char* description, SiteRef site, int count,
                         ItemKind item, int after = -1, int guards = 4)
    {
        ObjectiveDef def = Base(id, title, description, ObjectiveType::CollectItems, site, after);
        def.count = count;
        def.item = item;
        def.action = item == ItemKind::Fuel ? "Түлш авах" : item == ItemKind::Medicine ? "Эм авах"
                   : item == ItemKind::Ammo ? "Сум авах" : "Хүнс авах";
        def.guards = guards;
        return def;
    }

    ObjectiveDef Deliver(const char* id, const char* title, const char* description, SiteRef site, int after)
    {
        ObjectiveDef def = Base(id, title, description, ObjectiveType::DeliverItems, site, after);
        def.action = "Хангамж хүлээлгэн өгөх";
        def.reward = 50;
        return def;
    }

    // percent: бүсийн зомбины нөөцийг үеийн хүндрэлээс тооцсон тооны хэдэн хувиар авах.
    ObjectiveDef Clear(const char* id, const char* title, const char* description, SiteRef site, int after = -1,
                       int percent = 100)
    {
        ObjectiveDef def = Base(id, title, description, ObjectiveType::ClearZone, site, after);
        def.count = percent;
        def.reward = 80;
        return def;
    }

    ObjectiveDef Defend(const char* id, const char* title, const char* description, SiteRef site, float seconds,
                        int after = -1)
    {
        ObjectiveDef def = Base(id, title, description, ObjectiveType::DefendLocation, site, after);
        def.seconds = seconds;
        def.reward = 80;
        return def;
    }

    ObjectiveDef Boss(const char* id, const char* title, const char* description, SiteRef site, int after = -1)
    {
        ObjectiveDef def = Base(id, title, description, ObjectiveType::DefeatBoss, site, after);
        def.reward = 120;
        return def;
    }

    std::vector<StageDef> MakeStages()
    {
        std::vector<StageDef> stages;

        // Үе бүр нэг түүхтэй: даалгавар бүр өмнөхийнхөө үр дүнгээр нээгдэнэ (after). Газрууд нь тухайн аймгийн
        // төвийн жинхэнэ газрууд (эмнэлэг, цагдаа, засаг дарга, музей, хийд, зах, цэнгэлдэх) ба хотоос гадуурх хот айл,
        // суурин. Жинхэнэ газар байхгүй аймагт ойролцоо газраар орлоно (Mission.cpp ResolveSite).

        // 01 Баян-Өлгий — Өлгий
        stages.push_back({ "БАЯН-ӨЛГИЙ", "Өлгий хотыг баруун хилээс тасалсан халдвараас хамгаалж, эмнэлгийг сэргээ.", {
            Reach("reach_post", "Заставт хүр", "Ховд голын эрэг дээрх цэргийн заставт оч.", kPost),
            Interact("radio_post", "Холбоо тогтоо", "Заставын радиогоор хилийн отрядтай холбогд.", kPost, PropKind::Radio,
                     "Радио станц асаах", 0),
            Reach("reach_hospital", "Эмнэлэгт оч", "Аймгийн нэгдсэн эмнэлгээс тусламж хүссэн дохио ирлээ.", kHospital, 1),
            Rescue("rescue_doctors", "Эмч нарыг авар", "Эмнэлэгт хоригдсон 3 эмчийг авар.", kHospital, 3, SurvivorKind::Civilian, 2),
            Collect("medicine", "Эм цуглуул", "Эмнэлгийн агуулахаас 4 хайрцаг эм цуглуул.", kStore, 4, ItemKind::Medicine, 3),
            Deliver("medicine_hospital", "Эмийг эмнэлэгт хүргэ", "Эмээ эмч нарт хүргэ.", kHospital, 4),
            Clear("clear_camp", "Хот айлыг чөлөөл", "Хотын захын хот айлыг бүсэлсэн зомбиудыг устга.", Camp(0), 5, 70),
            Rescue("rescue_herders", "Малчдыг авар", "Хот айлын 3 малчныг аврагдсан эмнэлэг рүү хүргэ.", Camp(0), 3,
                   SurvivorKind::Civilian, 6, 0),
            Boss("altai_giant", "Алтайн аваргыг устга", "Малчдын хэлсэн уулын аварга зомби хот руу ойртож байна. Түүнийг ял.",
                 Camp(1), 7),
        } });

        // 02 Увс — Улаангом
        stages.push_back({ "УВС", "Улаангомын шатахууны нөөцийг хамгаалж, цэргийн ангийг хөдөлгөөнд оруул.", {
            Reach("reach_gov", "Засаг даргын тамгын газарт оч", "Аймгийн удирдлагаас үүрэг ав.", kGovernment),
            Collect("fuel", "Түлш цуглуул", "Шатахуун түгээх газраас 3 сав түлш цуглуул.", kGas, 3, ItemKind::Fuel, 0),
            Deliver("fuel_post", "Түлшийг заставт хүргэ", "Түлшээ цэргийн заставт хүргэ.", kPost, 1),
            Interact("generator_post", "Генераторыг асаа", "Заставын генераторыг түлшээр асаа.", kPost, PropKind::Generator,
                     "Генератор асаах", 2),
            Defend("hold_post", "Заставыг хамгаал", "Генераторын дуунд зомби цугларна. Ачааны машинууд түлшээ авч дуустал 60 секунд тэс.",
                   kPost, 60.0f, 3),
            Rescue("rescue_police", "Цагдаа нарыг авар", "Цагдаагийн газарт бүслэгдсэн 2 офицерыг авар. Тэд тантай хамт тулалдана.",
                   kPolice, 2, SurvivorKind::Soldier, 4),
            Clear("clear_market", "Захыг цэвэрлэ", "Хотын төвийн захад цугларсан зомбиудыг устга.", kMarket, 5),
            Boss("uvs_giant", "Увсын аваргыг устга", "Цэнгэлдэх хүрээлэнд нуугдсан аварга зомбийг ял.", kStadium, 6),
        } });

        // 03 Ховд — Ховд
        stages.push_back({ "ХОВД", "Ховд хотын өвчтөнүүдэд эм хүргэж, хотын төвийг чөлөөл.", {
            Reach("reach_hospital", "Эмнэлэгт оч", "Аймгийн эмнэлгээс сүүлчийн дохио ирсэн.", kHospital),
            Rescue("rescue_patients", "Өвчтөнүүдийг авар", "Эмнэлэгт үлдсэн 3 өвчтөнийг авар.", kHospital, 3, SurvivorKind::Civilian, 0),
            Collect("medicine", "Эм цуглуул", "Агуулахаас 4 хайрцаг эм цуглуул.", kStore, 4, ItemKind::Medicine, 1),
            Deliver("medicine_hospital", "Эмийг хүргэ", "Эмээ эмнэлэгт хүргэ.", kHospital, 2),
            Interact("radio_museum", "Музейн радиог асаа", "Музейн дээврийн радиогоор хотын иргэдэд мэдээлэл түгээ.", kMuseum,
                     PropKind::Radio, "Радио станц асаах", 3),
            Clear("clear_town", "Хотын төвийг цэвэрлэ", "Радиогийн дуунд цугласан зомбиудыг төвийн талбайд устга.", kTown, 4),
            Rescue("rescue_villagers", "Суурингийн иргэдийг авар", "Хотын захын сууринд үлдсэн 3 иргэнийг авар.", Hamlet(0), 3,
                   SurvivorKind::Civilian, 5),
            Boss("khovd_giant", "Ховдын аваргыг устга", "Суурингийн дэргэдэх аварга зомбийг ял.", Hamlet(0), 6),
        } });

        // 04 Завхан — Улиастай
        stages.push_back({ "ЗАВХАН", "Улиастайн цахилгааныг сэргээж, алслагдсан суурингуудыг чөлөөл.", {
            Reach("reach_post", "Заставт хүр", "Чигэстэйн голын хөндий дэх заставт оч.", kPost),
            Collect("fuel", "Түлш цуглуул", "Шатахуун түгээх газраас 3 сав түлш цуглуул.", kGas, 3, ItemKind::Fuel, 0),
            Reach("reach_hamlet", "Сууринд хүр", "Түлшээ аваад хотын захын сууринд оч.", Hamlet(0), 1),
            Clear("clear_hamlet", "Сууринг цэвэрлэ", "Сууринд байгаа бүх зомбийг устга.", Hamlet(0), 2),
            Interact("generator", "Генераторыг асаа", "Суурингийн генераторыг асаа.", Hamlet(0), PropKind::Generator,
                     "Түлш хийж асаах", 3, 0),
            Rescue("rescue_villagers", "Оршин суугчдыг авар", "Гэрэл асахад нуугдаж байсан 3 иргэн гарч ирлээ. Тэднийг авар.",
                   Hamlet(0), 3, SurvivorKind::Civilian, 4, 0),
            Rescue("rescue_students", "Сурагчдыг авар", "Сургуульд хоргодсон 3 сурагчийг авар.", kSchool, 3, SurvivorKind::Civilian, 5),
            Boss("zavkhan_giant", "Завханы аваргыг устга", "Сургуулийн дэргэд гарч ирсэн аварга зомбийг ял.", kSchool, 6),
        } });

        // 05 Говь-Алтай — Алтай
        stages.push_back({ "ГОВЬ-АЛТАЙ", "Алтай хотын сум хэрэгслийн нөөцийг нэмж, говийн аваргыг устга.", {
            Reach("reach_police", "Цагдаагийн газарт оч", "Цагдаагийн газрын зэвсгийн өрөөнд хүр.", kPolice),
            Collect("ammo", "Сум цуглуул", "Цагдаагийн газрын орчмоос 3 хайрцаг сум цуглуул.", kPolice, 3, ItemKind::Ammo, 0),
            Deliver("ammo_post", "Сумыг заставт хүргэ", "Сумаа заставт хүргэ.", kPost, 1),
            Reach("reach_camp", "Хот айлд хүр", "Хотын захын хот айлаас тусламж хүссэн.", Camp(0), 2),
            Rescue("rescue_herders", "Малчдыг авар", "Хот айлын 3 малчныг авар.", Camp(0), 3, SurvivorKind::Civilian, 3),
            Collect("food", "Хүнс цуглуул", "Хот айлаас 3 уут хүнс цуглуулж хотод авчир.", Camp(0), 3, ItemKind::Food, 4),
            Deliver("food_hospital", "Хүнсийг эмнэлэгт хүргэ", "Хүнсээ эмнэлэгт хүргэ.", kHospital, 5),
            Boss("gobi_giant", "Говийн аваргыг устга", "Дараагийн хот айлд байгаа аварга зомбийг ял.", Camp(1), 6),
        } });

        // 06 Хөвсгөл — Мөрөн
        stages.push_back({ "ХӨВСГӨЛ", "Мөрөн хотод салж тасарсан цэргүүдийг нэгтгэж, холбооны төвийг эзэл.", {
            Rescue("rescue_squad", "Цэргүүдийг авар", "Заставт бүслэгдсэн 2 цэргийг авар. Тэд тантай хамт тулалдана.", kPost,
                   2, SurvivorKind::Soldier),
            Reach("reach_police", "Цагдаагийн газарт оч", "Цагдаагийн газраас хотын байдлыг тодруул.", kPolice, 0),
            Interact("radio_store", "Агуулахын радиог асаа", "Агуулахын радио станцаар Улаанбаатартай холбогд.", kStore,
                     PropKind::Radio, "Радио станц асаах", 1),
            Defend("hold_store", "Агуулахыг хамгаал", "Нэмэлт хүч ирэх хүртэл агуулахын бүсэд 75 секунд тэс.", kStore, 75.0f, 2),
            Collect("medicine", "Эм цуглуул", "Эмнэлгээс 3 хайрцаг эм цуглуул.", kHospital, 3, ItemKind::Medicine, 3),
            Deliver("medicine_temple", "Эмийг хийдэд хүргэ", "Хийдэд хоргодсон иргэдэд эм хүргэ.", kTemple, 4),
            Clear("clear_town", "Хотын төвийг цэвэрлэ", "Мөрөнгийн төвийн бүх зомбийг устга.", kTown, 5),
            Boss("khuvsgul_giant", "Хөвсгөлийн аваргыг устга", "Цэнгэлдэх хүрээлэнд гарч ирсэн аварга зомбийг ял.", kStadium, 6),
        } });

        // 07 Баянхонгор — Баянхонгор
        stages.push_back({ "БАЯНХОНГОР", "Баянхонгор хотод түлш хүргэж, генераторыг ажиллуулан хамгаал.", {
            Reach("reach_gov", "Засаг даргын тамгын газарт оч", "Аймгийн удирдлагаас үүрэг ав.", kGovernment),
            Collect("fuel", "Түлш цуглуул", "Шатахуун түгээх газраас 3 сав түлш цуглуул.", kGas, 3, ItemKind::Fuel, 0),
            Interact("generator", "Генераторыг асаа", "Түлшээ авчирч суурингийн генераторыг асаа.", Hamlet(0),
                     PropKind::Generator, "Түлш хийж асаах", 1),
            Defend("hold_hamlet", "Сууринг хамгаал", "Генераторын дуунд зомби цугларна. Суурингийн бүсэд 60 секунд тэс.",
                   Hamlet(0), 60.0f, 2),
            Rescue("rescue_villagers", "Иргэдийг авар", "Сууринд нуугдсан 3 иргэнийг авар.", Hamlet(0), 3, SurvivorKind::Civilian, 3, 0),
            Clear("clear_market", "Захыг цэвэрлэ", "Хотын төвийн захад цугларсан зомбиудыг устга.", kMarket, 4),
            Rescue("rescue_police", "Цагдаа нарыг авар", "Цагдаагийн газарт бүслэгдсэн 2 офицерыг авар.", kPolice, 2,
                   SurvivorKind::Soldier, 5),
            Boss("bayankhongor_giant", "Аваргыг устга", "Цэнгэлдэх хүрээлэнд нуугдсан аварга зомбийг ял.", kStadium, 6),
        } });

        // 08 Архангай — Цэцэрлэг
        stages.push_back({ "АРХАНГАЙ", "Цэцэрлэг хотыг чөлөөлж, Заяын хүрээнд хоргодсон иргэдийг авар.", {
            Reach("reach_post", "Хээрийн заставт хүр", "Заставын хамгаалалтын хаалганд оч.", kPost),
            Interact("generator", "Генераторыг асаа", "Заставын генераторыг асаа.", kPost, PropKind::Generator, "Генератор асаах", 0),
            Reach("reach_temple", "Заяын хүрээнд оч", "Булган уулын бэлийн хийдээс тусламж хүссэн дохио ирлээ.", kTemple, 1),
            Rescue("rescue_monks", "Лам нарыг авар", "Хийдэд хоргодсон 3 ламыг авар.", kTemple, 3, SurvivorKind::Civilian, 2),
            Clear("clear_temple", "Хийдийн орчмыг цэвэрлэ", "Хийдийг бүсэлсэн зомбиудыг устга.", kTemple, 3, 80),
            Collect("food", "Хүнс цуглуул", "Захаас 3 уут хүнс цуглуул.", kMarket, 3, ItemKind::Food, 4),
            Deliver("food_temple", "Хүнсийг хийдэд хүргэ", "Хүнсээ хийдэд хоргодсон иргэдэд хүргэ.", kTemple, 5),
            Rescue("rescue_soldiers", "Цэргүүдийг авар", "Агуулахад бүслэгдсэн 2 цэргийг авар.", kStore, 2, SurvivorKind::Soldier, 6),
            Boss("arkhangai_giant", "Булган уулын аваргыг устга", "Уулаас бууж ирсэн аварга зомбийг хотын төвд ял.", kTown, 7),
        } });

        // 09 Булган — Булган
        stages.push_back({ "БУЛГАН", "Булган хотын малчдыг аварч, хот айлуудыг эзэлсэн сүргийг устга.", {
            Reach("reach_hospital", "Эмнэлэгт оч", "Эмнэлэгт шархадсан малчид ирсэн гэнэ.", kHospital),
            Rescue("rescue_herder", "Малчныг авар", "Эмнэлэгт байгаа 2 малчныг авар.", kHospital, 2, SurvivorKind::Civilian, 0),
            Reach("reach_camp", "Хот айлд хүр", "Малчдын хэлсэн хот айлд оч.", Camp(0), 1),
            Rescue("rescue_herders", "Хот айлын малчдыг авар", "Хот айлд үлдсэн 3 малчныг авар.", Camp(0), 3, SurvivorKind::Civilian, 2),
            Clear("clear_camp", "Хөрш хот айлыг цэвэрлэ", "Дараагийн хот айлыг эзэлсэн бүх зомбийг устга.", Camp(1), 3),
            Collect("ammo", "Сум цуглуул", "Цагдаагийн газраас 3 хайрцаг сум цуглуул.", kPolice, 3, ItemKind::Ammo, 4),
            Boss("pack_leader", "Сүргийн толгойлогчийг устга", "Хот айлын дэргэд гарч ирэх аварга зомбийг ял.", Camp(1), 5),
        } });

        // 10 Өвөрхангай — Арвайхээр
        stages.push_back({ "ӨВӨРХАНГАЙ", "Арвайхээрт холбоо тогтоож, алслагдсан суурингаас эм авчир.", {
            Interact("radio_town", "Хотын радиог асаа", "Төвийн талбайн радио станцыг асаа.", kTown, PropKind::Radio, "Радио станц асаах"),
            Reach("reach_gov", "Засаг даргын тамгын газарт оч", "Радиогоор хариу ирлээ: удирдлагатай уулз.", kGovernment, 0),
            Collect("medicine", "Эм цуглуул", "Жижиг суурингаас 4 хайрцаг эм цуглуул.", Hamlet(0), 4, ItemKind::Medicine, 1),
            Deliver("medicine_hospital", "Эмийг эмнэлэгт хүргэ", "Эмээ аймгийн эмнэлэгт хүргэ.", kHospital, 2),
            Rescue("rescue_students", "Сурагчдыг авар", "Сургуульд хоргодсон 3 сурагчийг авар.", kSchool, 3, SurvivorKind::Civilian, 3),
            Defend("hold_school", "Сургуулийг хамгаал", "Сурагчдыг нүүлгэж дуустал сургуулийн бүсэд 60 секунд тэс.", kSchool, 60.0f, 4),
            Boss("uvurkhangai_giant", "Аваргыг устга", "Захын талбайд гарч ирсэн аварга зомбийг ял.", kMarket, 5),
        } });

        // 11 Орхон — Эрдэнэт
        stages.push_back({ "ОРХОН", "Эрдэнэт хотын үйлдвэрийн агуулахыг эзэлж, цахилгааныг сэргээн хамгаал.", {
            Reach("reach_store", "Агуулахад хүр", "Хотын захын том агуулахад оч.", kStore),
            Interact("generator", "Генераторыг асаа", "Агуулахын генераторыг асаа.", kStore, PropKind::Generator, "Генератор асаах", 0),
            Clear("clear_store", "Агуулахын орчмыг цэвэрлэ", "Агуулахын бүсийн бүх зомбийг устга.", kStore, 1),
            Defend("hold_store", "Агуулахыг хамгаал", "Агуулахын бүсэд 60 секунд тэс.", kStore, 60.0f, 2),
            Rescue("rescue_police", "Цагдаа нарыг авар", "Цагдаагийн газарт бүслэгдсэн 3 офицерыг авар.", kPolice, 3,
                   SurvivorKind::Soldier, 3),
            Collect("medicine", "Эм цуглуул", "Эмнэлгээс 3 хайрцаг эм цуглуул.", kHospital, 3, ItemKind::Medicine, 4),
            Deliver("medicine_gov", "Эмийг засаг даргад хүргэ", "Засаг даргын тамгын газарт хоргодсон иргэдэд эм хүргэ.",
                    kGovernment, 5),
            Clear("clear_center", "Хотын төвийг цэвэрлэ", "Эрдэнэтийн төвийн бүх зомбийг устга.", kTown, 6, 120),
            Boss("erdenet_giant", "Уурхайн аваргыг устга", "Цэнгэлдэх хүрээлэнд гарч ирсэн аварга зомбийг ял.", kStadium, 7),
        } });

        // 12 Өмнөговь — Даланзадгад
        stages.push_back({ "ӨМНӨГОВЬ", "Даланзадгадаас говийн хот айлуудад түлш, ус хүргэж, аваргыг устга.", {
            Collect("fuel", "Түлш цуглуул", "Шатахуун түгээх газраас 3 сав түлш цуглуул.", kGas, 3, ItemKind::Fuel),
            Deliver("fuel_camp", "Түлшийг хот айлд хүргэ", "Түлшээ хот айлд хүргэ.", Camp(0), 0),
            Interact("generator_camp", "Хот айлын генераторыг асаа", "Худгийн насосны генераторыг асаа.", Camp(0),
                     PropKind::Generator, "Генератор асаах", 1),
            Rescue("rescue_herders", "Малчдыг авар", "Хот айлын 2 малчныг авар.", Camp(0), 2, SurvivorKind::Civilian, 2),
            Collect("medicine", "Эм цуглуул", "Эмнэлгээс 3 хайрцаг эм цуглуул.", kHospital, 3, ItemKind::Medicine, 3),
            Deliver("medicine_camp", "Эмийг хүргэ", "Малчдад эм хүргэ.", Camp(0), 4),
            Boss("gobi_giant", "Говийн аваргыг устга", "Дараагийн хот айлд байгаа аварга зомбийг ял.", Camp(1), 5),
        } });

        // 13 Сэлэнгэ — Сүхбаатар
        stages.push_back({ "СЭЛЭНГЭ", "Сүхбаатар хотод хүнс хүргэж, хилийн боомтын замыг нээ.", {
            Reach("reach_hamlet", "Сууринд хүр", "Тариалангийн сууринд оч.", Hamlet(0)),
            Collect("food", "Хүнс цуглуул", "Суурингаас 4 уут хүнс цуглуул.", Hamlet(0), 4, ItemKind::Food, 0),
            Deliver("food_town", "Хүнсийг хотод хүргэ", "Хүнсээ хотын төвийн хүлээлгэн өгөх цэгт хүргэ.", kTown, 1),
            Clear("clear_town", "Хотын төвийг цэвэрлэ", "Хүнсний үнэрт цугласан зомбиудыг устга.", kTown, 2),
            Rescue("rescue_police", "Хилийн цэргүүдийг авар", "Цагдаагийн газарт бүслэгдсэн 2 хилчнийг авар.", kPolice, 2,
                   SurvivorKind::Soldier, 3),
            Interact("radio_post", "Боомттой холбогд", "Заставын радиогоор хилийн боомттой холбогд.", kPost, PropKind::Radio,
                     "Радио станц асаах", 4),
            Defend("hold_post", "Заставыг хамгаал", "Боомтоос ирэх машиныг хүлээж 75 секунд тэс.", kPost, 75.0f, 5),
            Boss("selenge_giant", "Аваргыг устга", "Цэнгэлдэх хүрээлэнд гарч ирсэн аварга зомбийг ял.", kStadium, 6),
        } });

        // 14 Дархан-Уул — Дархан
        stages.push_back({ "ДАРХАН-УУЛ", "Дархан хотын үйлдвэрийн цахилгааныг сэргээж, ажилчдыг авар.", {
            Interact("generator", "Генераторыг асаа", "Агуулахын генераторыг асаа.", kStore, PropKind::Generator, "Генератор асаах"),
            Rescue("rescue_guards", "Харуулуудыг авар", "Агуулахад бүслэгдсэн 3 цэргийг авар.", kStore, 3, SurvivorKind::Soldier, 0),
            Defend("hold_store", "Агуулахыг хамгаал", "Агуулахын бүсэд 90 секунд тэс.", kStore, 90.0f, 1),
            Reach("reach_hospital", "Эмнэлэгт оч", "Шархадсан ажилчдыг эмнэлэгт хүргэх хэрэгтэй.", kHospital, 2),
            Collect("medicine", "Эм цуглуул", "Эмнэлгээс 4 хайрцаг эм цуглуул.", kHospital, 4, ItemKind::Medicine, 3),
            Deliver("medicine_school", "Эмийг сургуульд хүргэ", "Сургуулийг түр эмнэлэг болгосон. Эмээ хүргэ.", kSchool, 4),
            Clear("clear_market", "Захыг цэвэрлэ", "Хотын төвийн захад цугларсан зомбиудыг устга.", kMarket, 5),
            Boss("darkhan_giant", "Дарханы аваргыг устга", "Захын дэргэд гарч ирсэн аварга зомбийг ял.", kMarket, 6),
        } });

        // 15 Төв — Зуунмод
        stages.push_back({ "ТӨВ", "Зуунмодоос нийслэл рүү орох замыг нээж, Улаанбаатартай холбогд.", {
            Reach("reach_post", "Заставт хүр", "Засмал замын дагуух цэргийн заставт оч.", kPost),
            Collect("ammo", "Сум цуглуул", "Заставын орчмоос 3 хайрцаг сум цуглуул.", kPost, 3, ItemKind::Ammo, 0),
            Reach("reach_gov", "Засаг даргын тамгын газарт оч", "Аймгийн удирдлагаас нийслэлийн мэдээ ав.", kGovernment, 1),
            Clear("clear_hamlet", "Замын сууринг цэвэрлэ", "Нийслэл рүү хөтлөх замын суурин дахь бүх зомбийг устга.", Hamlet(0), 2),
            Interact("radio_hamlet", "Улаанбаатартай холбогд", "Суурингийн радио станцыг асаа.", Hamlet(0), PropKind::Radio,
                     "Радио станц асаах", 3, 0),
            Rescue("rescue_herders", "Хот айлын иргэдийг авар", "Замын дагуух хот айлын 3 иргэнийг авар.", Camp(0), 3,
                   SurvivorKind::Civilian, 4),
            Boss("tuv_giant", "Аваргыг устга", "Замыг хаасан аварга зомбийг ял.", Hamlet(0), 5),
        } });

        // 16 Улаанбаатар
        stages.push_back({ "УЛААНБААТАР", "Нийслэлийн төвийг чөлөөлж, иргэдийг аварч, хотын аваргыг устга.", {
            Defend("hold_square", "Сүхбаатарын талбайг хамгаал", "Застав байгуулж дуустал талбайд 60 секунд тэс.", kPost, 60.0f),
            Clear("clear_center", "Чойжин ламын сүмийн орчмыг цэвэрлэ", "Хотын төвд байгаа болон шинээр гарч ирэх бүх зомбийг устга.",
                  kTown, 0, 140),
            Rescue("rescue_citizens", "Иргэдийг авар", "Сүмийн орчимд нуугдсан 4 иргэнийг авар.", kTown, 4, SurvivorKind::Civilian, 1, 0),
            Reach("reach_hospital", "Эмнэлэгт оч", "Хотын төвийн эмнэлгээс тусламж хүссэн дохио ирлээ.", kHospital, 2),
            Collect("medicine", "Эм цуглуул", "Эмнэлгээс 4 хайрцаг эм цуглуул.", kHospital, 4, ItemKind::Medicine, 3),
            Deliver("medicine_museum", "Эмийг музейд хүргэ", "Музейд хоргодсон иргэдэд эм хүргэ.", kMuseum, 4),
            Rescue("rescue_police", "Цагдаа нарыг авар", "Цагдаагийн газарт бүслэгдсэн 3 офицерыг авар.", kPolice, 3,
                   SurvivorKind::Soldier, 5),
            Collect("fuel", "Түлш цуглуул", "Шатахуун түгээх газраас 3 сав түлш цуглуул.", kGas, 3, ItemKind::Fuel, 6),
            Boss("city_giant", "Хотын аваргыг устга", "Төв цэнгэлдэх хүрээлэнд гарч ирсэн аварга зомбийг ял.", kStadium, 7),
        } });

        // 17 Дундговь — Мандалговь
        stages.push_back({ "ДУНДГОВЬ", "Мандалговиос говийн хот айлуудад түлш хүргэж, генераторыг ажиллуул.", {
            Collect("fuel", "Түлш цуглуул", "Шатахуун түгээх газраас 3 сав түлш цуглуул.", kGas, 3, ItemKind::Fuel),
            Deliver("fuel_camp", "Түлшийг хот айлд хүргэ", "Түлшээ хот айлын хүлээлгэн өгөх цэгт хүргэ.", Camp(0), 0),
            Interact("generator", "Генераторыг асаа", "Хот айлын генераторыг асаа.", Camp(0), PropKind::Generator, "Генератор асаах", 1),
            Defend("hold_camp", "Хот айлыг хамгаал", "Генераторын дуунд зомби цугларна. 60 секунд тэс.", Camp(0), 60.0f, 2),
            Rescue("rescue_patients", "Өвчтөнүүдийг авар", "Эмнэлэгт үлдсэн 3 өвчтөнийг авар.", kHospital, 3, SurvivorKind::Civilian, 3),
            Clear("clear_museum", "Музейн орчмыг цэвэрлэ", "Музейн дэргэд цугласан зомбиудыг устга.", kMuseum, 4),
            Boss("dundgovi_giant", "Аваргыг устга", "Хотын төвд гарч ирсэн аварга зомбийг ял.", kTown, 5),
        } });

        // 18 Говьсүмбэр — Чойр
        stages.push_back({ "ГОВЬСҮМБЭР", "Чойрын төмөр замын сууринд бүслэгдсэн цэргүүдийг аварч, хотыг чөлөөл.", {
            Reach("reach_hamlet", "Сууринд хүр", "Төмөр замын дагуух сууринд оч.", Hamlet(0)),
            Rescue("rescue_soldiers", "Цэргүүдийг авар", "Сууринд бүслэгдсэн 3 цэргийг авар.", Hamlet(0), 3, SurvivorKind::Soldier, 0),
            Clear("clear_hamlet", "Сууринг цэвэрлэ", "Сууринд байгаа бүх зомбийг устга.", Hamlet(0), 1),
            Collect("ammo", "Сум цуглуул", "Цагдаагийн газраас 3 хайрцаг сум цуглуул.", kPolice, 3, ItemKind::Ammo, 2),
            Deliver("ammo_post", "Сумыг заставт хүргэ", "Сумаа заставт хүргэ.", kPost, 3),
            Defend("hold_post", "Заставыг хамгаал", "Галт тэрэг ирэх хүртэл 75 секунд тэс.", kPost, 75.0f, 4),
            Boss("choir_giant", "Аваргыг устга", "Захын талбайд гарч ирсэн аварга зомбийг ял.", kMarket, 5),
        } });

        // 19 Дорноговь — Сайншанд
        stages.push_back({ "ДОРНОГОВЬ", "Сайншандын хилийн заставыг сум хэрэгслээр хангаж, довтолгоог няц.", {
            Interact("radio_post", "Заставын радиог асаа", "Заставын радио станцыг асаа.", kPost, PropKind::Radio, "Радио станц асаах"),
            Collect("ammo", "Сум цуглуул", "Агуулахын орчмоос 4 хайрцаг сум цуглуул.", kStore, 4, ItemKind::Ammo, 0),
            Deliver("ammo_post", "Сумыг заставт хүргэ", "Сумаа заставт хүргэ.", kPost, 1),
            Defend("hold_post", "Заставыг хамгаал", "Заставын бүсэд 90 секунд тэс.", kPost, 90.0f, 2),
            Rescue("rescue_monks", "Лам нарыг авар", "Музей, хийдэд хоргодсон 3 иргэнийг авар.", kMuseum, 3, SurvivorKind::Civilian, 3),
            Clear("clear_school", "Сургуулийг цэвэрлэ", "Сургуулийн орчмын бүх зомбийг устга.", kSchool, 4),
            Boss("dornogovi_giant", "Аваргыг устга", "Цэнгэлдэх хүрээлэнд гарч ирсэн аварга зомбийг ял.", kStadium, 5),
        } });

        // 20 Хэнтий — Чингис хот
        stages.push_back({ "ХЭНТИЙ", "Чингис хотын хот айлуудыг чөлөөлж, уулын аварга зомбийг устга.", {
            Reach("reach_museum", "Музейд оч", "Аймгийн музейд хоргодсон иргэдээс мэдээ ав.", kMuseum),
            Rescue("rescue_citizens", "Иргэдийг авар", "Музейд үлдсэн 2 иргэнийг авар.", kMuseum, 2, SurvivorKind::Civilian, 0),
            Reach("reach_camp", "Хот айлд хүр", "Хамгийн ойрын хот айлд оч.", Camp(0), 1),
            Rescue("rescue_herders", "Малчдыг авар", "Хот айлын 3 малчныг авар.", Camp(0), 3, SurvivorKind::Civilian, 2),
            Clear("clear_camp", "Хөрш хот айлыг цэвэрлэ", "Дараагийн хот айлыг эзэлсэн бүх зомбийг устга.", Camp(1), 3),
            Collect("food", "Хүнс цуглуул", "Хот айлаас 3 уут хүнс цуглуул.", Camp(1), 3, ItemKind::Food, 4),
            Deliver("food_hospital", "Хүнсийг эмнэлэгт хүргэ", "Хүнсээ эмнэлэгт хүргэ.", kHospital, 5),
            Boss("mountain_giant", "Уулын аваргыг устга", "Гурав дахь хот айлд байгаа аварга зомбийг ял.", Camp(2), 6),
        } });

        // 21 Сүхбаатар — Баруун-Урт
        stages.push_back({ "СҮХБААТАР", "Баруун-Урт хотоос эм авчирч, хот айлуудад хүргэн хамгаал.", {
            Reach("reach_hospital", "Эмнэлэгт оч", "Аймгийн эмнэлэг эм дутагдаж байна.", kHospital),
            Collect("medicine", "Эм цуглуул", "Жижиг суурингаас 4 хайрцаг эм цуглуул.", Hamlet(0), 4, ItemKind::Medicine, 0),
            Deliver("medicine_hospital", "Эмийг эмнэлэгт хүргэ", "Эмээ эмнэлэгт хүргэ.", kHospital, 1),
            Deliver("medicine_camp", "Эмийг хот айлд хүргэ", "Үлдсэн эмээ хот айлд хүргэ.", Camp(0), 2),
            Defend("hold_camp", "Хот айлыг хамгаал", "Хот айлын бүсэд 75 секунд тэс.", Camp(0), 75.0f, 3),
            Rescue("rescue_police", "Цагдаа нарыг авар", "Цагдаагийн газарт бүслэгдсэн 2 офицерыг авар.", kPolice, 2,
                   SurvivorKind::Soldier, 4),
            Boss("sukhbaatar_giant", "Аваргыг устга", "Засаг даргын тамгын газрын дэргэд гарч ирсэн аварга зомбийг ял.", kGovernment, 5),
        } });

        // 22 Дорнод — Чойбалсан
        stages.push_back({ "ДОРНОД", "Чойбалсанд сүүлчийн аймгийг чөлөөлж, зомбийн сүүлчийн аваргыг устга.", {
            Reach("reach_gov", "Засаг даргын тамгын газарт оч", "Сүүлчийн аймгийн удирдлагатай уулз.", kGovernment),
            Interact("radio_gov", "Улс даяар мэдэгд", "Радиогоор бүх аймагт мэдээ түгээ.", kGovernment, PropKind::Radio,
                     "Радио станц асаах", 0),
            Rescue("rescue_patients", "Өвчтөнүүдийг авар", "Эмнэлэгт үлдсэн 3 өвчтөнийг авар.", kHospital, 3, SurvivorKind::Civilian, 1),
            Collect("fuel", "Түлш цуглуул", "Шатахуун түгээх газраас 3 сав түлш цуглуул.", kGas, 3, ItemKind::Fuel, 2),
            Reach("reach_hamlet", "Сууринд хүр", "Хэрлэн голын эргийн сууринд оч.", Hamlet(0), 3),
            Clear("clear_hamlet", "Сууринг цэвэрлэ", "Сууринд байгаа болон шинээр гарч ирэх бүх зомбийг устга.", Hamlet(0), 4, 130),
            Defend("hold_town", "Хотын төвийг хамгаал", "Сүүлчийн давалгааг хотын төвд 90 секунд тэсэж няц.", kTown, 90.0f, 5),
            Boss("last_giant", "Сүүлчийн аваргыг устга", "Хотын төвд гарч ирэх хамгийн хүчтэй аварга зомбийг ял.", kTown, 6),
        } });

        // Аймаг бүрт аварга зомби: аваргагүй үе шатад сүүлийн даалгаврын дараа, түүний газарт гарч ирнэ.
        for (StageDef& stage : stages)
        {
            const bool hasBoss = std::any_of(stage.objectives.begin(), stage.objectives.end(),
                                             [](const ObjectiveDef& o) { return o.type == ObjectiveType::DefeatBoss; });
            if (hasBoss || stage.objectives.empty()) continue;
            const int last = (int)stage.objectives.size() - 1;
            stage.objectives.push_back(Boss("province_giant", "Аварга зомбийг устга",
                                            "Даалгавар биелсэн газрын дэргэд гарч ирэх аварга зомбийг ял. Толгой руу нь олон удаа буудах хэрэгтэй.",
                                            stage.objectives[(size_t)last].site, last));
        }
        // Өгөгдлийн шалгалт: урьдчилсан нөхцөл нь өмнөх даалгавар, тоо нь хадгалалтын хязгаарт багтана
        for (size_t k = 0; k < stages.size(); ++k)
        {
            const std::vector<ObjectiveDef>& list = stages[k].objectives;
            if (list.size() > (size_t)StageSave::kMaxObjectives)
                TraceLog(LOG_WARNING, "CAMPAIGN: stage %d has %d objectives (max %d)", (int)k, (int)list.size(), StageSave::kMaxObjectives);
            for (size_t i = 0; i < list.size(); ++i)
            {
                if (list[i].after >= (int)i) TraceLog(LOG_WARNING, "CAMPAIGN: stage %d objective %d waits for %d", (int)k, (int)i, list[i].after);
            }
            TraceLog(LOG_DEBUG, "CAMPAIGN: stage %d: %d objectives", (int)k, (int)list.size());
        }
        return stages;
    }
}

const StageDef& StageFor(int provinceIndex)
{
    static const std::vector<StageDef> stages = MakeStages();
    const int last = (int)stages.size() - 1;
    return stages[provinceIndex < 0 ? 0 : provinceIndex > last ? last : provinceIndex];
}
