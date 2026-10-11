#include "CampaignData.h"

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

        // 01 Баян-Өлгий
        stages.push_back({ "БАЯН-ӨЛГИЙ", "Хилийн заставтай холбоо тогтоож, ойрын отрын малчдыг авар.", {
            Reach("reach_post", "Заставт хүр", "Засмал замын дагуух цэргийн заставт оч.", kPost),
            Interact("radio_on", "Радио станцыг асаа", "Заставын радио станцын дэргэд очиж асаа.", kPost,
                     PropKind::Radio, "Радио станц асаах", 0),
            Clear("clear_camp", "Отрын орчмыг цэвэрлэ", "Малчны отрыг бүсэлсэн бүх зомбийг устга.", Camp(0), 1, 70),
            Rescue("rescue_herders", "Малчдыг авар", "Отор дахь 3 малчны дэргэд очиж авар.", Camp(0), 3,
                   SurvivorKind::Civilian, 2, 0),
        } });

        // 02 Увс
        stages.push_back({ "УВС", "Шатахууны нөөц бүрдүүлж, цэргийн ангийг хөдөлгөөнд оруул.", {
            Collect("fuel", "Түлш цуглуул", "Шатахуун түгээх газраас 3 сав түлш цуглуул.", kGas, 3, ItemKind::Fuel),
            Deliver("fuel_post", "Түлшийг заставт хүргэ", "Цуглуулсан түлшээ цэргийн заставын хүлээлгэн өгөх цэгт хүргэ.",
                    kPost, 0),
            Defend("hold_post", "Заставыг хамгаал", "Ачааны машин түлшээ авч дуустал заставын бүсэд 60 секунд тэсэж үлд.",
                   kPost, 60.0f, 1),
        } });

        // 03 Ховд
        stages.push_back({ "ХОВД", "Өвчтөнүүдэд эм хүргэж, суурингийн аюулгүй байдлыг ханга.", {
            Collect("medicine", "Эм цуглуул", "Агуулахын орчмоос 4 хайрцаг эм цуглуул.", kStore, 4, ItemKind::Medicine),
            Deliver("medicine_town", "Эмийг сууринд хүргэ", "Эмээ төв суурингийн хүлээлгэн өгөх цэгт хүргэ.", kTown, 0),
            Clear("clear_town", "Сууринг цэвэрлэ", "Суурингийн гудамж, хашаануудад байгаа бүх зомбийг устга.", kTown, 1),
            Rescue("rescue_patients", "Нуугдсан иргэдийг авар", "Суурин дахь 2 иргэний дэргэд очиж авар.", kTown, 2,
                   SurvivorKind::Civilian, 2, 0),
        } });

        // 04 Завхан
        stages.push_back({ "ЗАВХАН", "Алслагдсан сууринг чөлөөлж, цахилгаанаар ханга.", {
            Reach("reach_hamlet", "Сууринд хүр", "Шороон замын дагуух жижиг сууринд оч. Унаа ашиглавал хурдан.", Hamlet(0)),
            Clear("clear_hamlet", "Сууринг цэвэрлэ", "Сууринд байгаа болон шинээр гарч ирэх бүх зомбийг устга.", Hamlet(0), 0),
            Interact("generator", "Генераторыг асаа", "Суурингийн генераторын дэргэд очиж асаа.", Hamlet(0),
                     PropKind::Generator, "Генератор асаах", 1, 0),
            Rescue("rescue_villagers", "Оршин суугчдыг авар", "Сууринд үлдсэн 3 иргэнийг авар.", Hamlet(0), 3,
                   SurvivorKind::Civilian, 1, 0),
        } });

        // 05 Говь-Алтай
        stages.push_back({ "ГОВЬ-АЛТАЙ", "Сум хэрэгслийн нөөцөө нэмж, говийн аварга зомбийг устга.", {
            Reach("reach_camp", "Малчны оторт хүр", "Хамгийн ойрын малчны оторт оч.", Camp(0)),
            Collect("ammo", "Сум цуглуул", "Отрын орчмоос 3 хайрцаг сум цуглуул.", Camp(0), 3, ItemKind::Ammo, 0),
            Boss("giant", "Аварга зомбийг устга", "Дараагийн оторт байгаа аварга зомбийг олж устга. Толгой руу нь олон удаа буудах хэрэгтэй.",
                 Camp(1), 1),
        } });

        // 06 Хөвсгөл
        stages.push_back({ "ХӨВСГӨЛ", "Салж тасарсан цэргүүдийг нэгтгэж, агуулахын холбооны төвийг эзэл.", {
            Rescue("rescue_squad", "Цэргүүдийг авар", "Заставт бүслэгдсэн 2 цэргийг авар. Тэд тантай хамт тулалдана.", kPost,
                   2, SurvivorKind::Soldier),
            Interact("radio_store", "Агуулахын радиог асаа", "Агуулахын дэргэдэх радио станцыг асаа.", kStore,
                     PropKind::Radio, "Радио станц асаах", 0),
            Defend("hold_store", "Агуулахыг хамгаал", "Нэмэлт хүч ирэх хүртэл агуулахын бүсэд 75 секунд тэсэж үлд.", kStore,
                   75.0f, 1),
            Clear("clear_town", "Сууринг цэвэрлэ", "Төв суурингийн бүх зомбийг устга.", kTown, 0),
        } });

        // 07 Баянхонгор
        stages.push_back({ "БАЯНХОНГОР", "Сууринд түлш хүргэж, генераторыг ажиллуулан хамгаал.", {
            Collect("fuel", "Түлш цуглуул", "Шатахуун түгээх газраас 3 сав түлш цуглуул.", kGas, 3, ItemKind::Fuel),
            Interact("generator", "Генераторыг асаа", "Түлшээ авчирч суурингийн генераторыг асаа.", Hamlet(0),
                     PropKind::Generator, "Түлш хийж асаах", 0),
            Defend("hold_hamlet", "Сууринг хамгаал", "Генераторын дуунд зомби цугларна. Суурингийн бүсэд 60 секунд тэсэж үлд.",
                   Hamlet(0), 60.0f, 1),
        } });

        // 08 Архангай
        stages.push_back({ "АРХАНГАЙ", "Хээрийн заставыг чөлөөлж, амьд үлдсэн цэргүүдийг аюулгүй бүсэд хүргэ.", {
            Reach("reach_post", "Хээрийн заставт хүр", "Заставын хамгаалалтын хаалганд оч.", kPost),
            Interact("generator", "Генераторыг асаа", "Генераторын дэргэд очиж асаа.", kPost,
                     PropKind::Generator, "Генератор асаах", 0),
            Rescue("rescue_soldiers", "Бүслэгдсэн 3 цэргийг авар", "Цэргүүдийн дэргэд очиж авар.", kStore, 3,
                   SurvivorKind::Soldier, 1),
            Clear("clear_post", "Хамгаалалтын бүсийг чөлөөл", "Бүсэд байгаа болон шинээр гарч ирэх бүх зомбийг устга.",
                  kPost, 2),
        } });

        // 09 Булган
        stages.push_back({ "БУЛГАН", "Отрын малчдыг аварч, хөрш отрыг эзэлсэн сүргийг устга.", {
            Reach("reach_camp", "Малчны оторт хүр", "Хамгийн ойрын малчны оторт оч.", Camp(0)),
            Rescue("rescue_herders", "Малчдыг авар", "Отор дахь 3 малчныг авар.", Camp(0), 3, SurvivorKind::Civilian, 0),
            Clear("clear_camp", "Хөрш отрыг цэвэрлэ", "Дараагийн отрыг эзэлсэн бүх зомбийг устга.", Camp(1), 1),
            Boss("pack_leader", "Сүргийн толгойлогчийг устга", "Цэвэрлэсэн отрын дэргэд гарч ирэх аварга зомбийг ял.", Camp(1), 2),
        } });

        // 10 Өвөрхангай
        stages.push_back({ "ӨВӨРХАНГАЙ", "Холбоо тогтоож, алслагдсан суурингаас эм авчир.", {
            Interact("radio_town", "Суурингийн радиог асаа", "Төв суурингийн радио станцыг асаа.", kTown,
                     PropKind::Radio, "Радио станц асаах"),
            Collect("medicine", "Эм цуглуул", "Жижиг суурингаас 4 хайрцаг эм цуглуул.", Hamlet(0), 4, ItemKind::Medicine, 0),
            Deliver("medicine_post", "Эмийг заставт хүргэ", "Эмээ цэргийн заставын хүлээлгэн өгөх цэгт хүргэ.", kPost, 1),
        } });

        // 11 Орхон
        stages.push_back({ "ОРХОН", "Үйлдвэрийн агуулахыг эзэлж, цахилгааныг сэргээн хамгаал.", {
            Reach("reach_store", "Агуулахад хүр", "Суурингийн хойд захын том агуулахад оч.", kStore),
            Interact("generator", "Генераторыг асаа", "Агуулахын генераторыг асаа.", kStore, PropKind::Generator,
                     "Генератор асаах", 0),
            Clear("clear_store", "Агуулахын орчмыг цэвэрлэ", "Агуулахын бүсийн бүх зомбийг устга.", kStore, 1),
            Defend("hold_store", "Агуулахыг хамгаал", "Агуулахын бүсэд 60 секунд тэсэж үлд.", kStore, 60.0f, 2),
        } });

        // 12 Өмнөговь
        stages.push_back({ "ӨМНӨГОВЬ", "Говийн оторт түлш хүргэж, малчдыг аварч, аварга зомбийг устга.", {
            Collect("fuel", "Түлш цуглуул", "Шатахуун түгээх газраас 3 сав түлш цуглуул.", kGas, 3, ItemKind::Fuel),
            Deliver("fuel_camp", "Түлшийг оторт хүргэ", "Түлшээ малчны отрын хүлээлгэн өгөх цэгт хүргэ.", Camp(0), 0),
            Rescue("rescue_herders", "Малчдыг авар", "Отор дахь 2 малчныг авар.", Camp(0), 2, SurvivorKind::Civilian, 1),
            Boss("giant", "Говийн аваргыг устга", "Дараагийн оторт байгаа аварга зомбийг ял.", Camp(1), 2),
        } });

        // 13 Сэлэнгэ
        stages.push_back({ "СЭЛЭНГЭ", "Тариалангийн суурингаас хүнс авчирч, төв сууринг чөлөөл.", {
            Reach("reach_hamlet", "Сууринд хүр", "Шороон замын дагуух жижиг сууринд оч.", Hamlet(0)),
            Collect("food", "Хүнс цуглуул", "Суурингаас 4 уут хүнс цуглуул.", Hamlet(0), 4, ItemKind::Food, 0),
            Deliver("food_town", "Хүнсийг төв сууринд хүргэ", "Хүнсээ төв суурингийн хүлээлгэн өгөх цэгт хүргэ.", kTown, 1),
            Clear("clear_town", "Төв сууринг цэвэрлэ", "Төв суурингийн бүх зомбийг устга.", kTown, 2),
        } });

        // 14 Дархан-Уул
        stages.push_back({ "ДАРХАН-УУЛ", "Үйлдвэрийн агуулахын цахилгааныг сэргээж, ажилчдыг авар.", {
            Interact("generator", "Генераторыг асаа", "Агуулахын генераторыг асаа.", kStore, PropKind::Generator,
                     "Генератор асаах"),
            Rescue("rescue_guards", "Харуулуудыг авар", "Агуулахад бүслэгдсэн 3 цэргийг авар.", kStore, 3,
                   SurvivorKind::Soldier, 0),
            Defend("hold_store", "Агуулахыг хамгаал", "Агуулахын бүсэд 90 секунд тэсэж үлд.", kStore, 90.0f, 1),
        } });

        // 15 Төв
        stages.push_back({ "ТӨВ", "Нийслэл рүү орох замыг нээж, Улаанбаатартай холбогд.", {
            Reach("reach_post", "Заставт хүр", "Засмал замын дагуух цэргийн заставт оч.", kPost),
            Collect("ammo", "Сум цуглуул", "Заставын орчмоос 3 хайрцаг сум цуглуул.", kPost, 3, ItemKind::Ammo, 0),
            Clear("clear_hamlet", "Замын сууринг цэвэрлэ", "Нийслэл рүү хөтлөх замын суурин дахь бүх зомбийг устга.", Hamlet(0), 1),
            Interact("radio_hamlet", "Улаанбаатартай холбогд", "Суурингийн радио станцыг асаа.", Hamlet(0),
                     PropKind::Radio, "Радио станц асаах", 2, 0),
        } });

        // 16 Улаанбаатар
        stages.push_back({ "УЛААНБААТАР", "Нийслэлийн төвийг чөлөөлж, иргэдийг аварч, хотын аваргыг устга.", {
            Clear("clear_center", "Хотын төвийг цэвэрлэ", "Хотын төвд байгаа болон шинээр гарч ирэх бүх зомбийг устга.", kTown, -1, 140),
            Rescue("rescue_citizens", "Иргэдийг авар", "Төв хэсэгт нуугдсан 4 иргэнийг авар.", kTown, 4, SurvivorKind::Civilian,
                   0, 0),
            Defend("hold_post", "Заставыг хамгаал", "Иргэдийг нүүлгэж дуустал заставын бүсэд 90 секунд тэсэж үлд.", kPost,
                   90.0f, 1),
            Boss("city_giant", "Хотын аваргыг устга", "Шатахуун түгээх газрын дэргэдэх аварга зомбийг ял.", kGas, 2),
        } });

        // 17 Дундговь
        stages.push_back({ "ДУНДГОВЬ", "Говийн оторт түлш хүргэж, генераторыг ажиллуул.", {
            Collect("fuel", "Түлш цуглуул", "Шатахуун түгээх газраас 3 сав түлш цуглуул.", kGas, 3, ItemKind::Fuel),
            Deliver("fuel_camp", "Түлшийг оторт хүргэ", "Түлшээ малчны отрын хүлээлгэн өгөх цэгт хүргэ.", Camp(0), 0),
            Interact("generator", "Генераторыг асаа", "Отрын генераторыг асаа.", Camp(0), PropKind::Generator,
                     "Генератор асаах", 1),
        } });

        // 18 Говьсүмбэр
        stages.push_back({ "ГОВЬСҮМБЭР", "Төмөр замын сууринд бүслэгдсэн цэргүүдийг аварч, сууринг чөлөөл.", {
            Reach("reach_hamlet", "Сууринд хүр", "Шороон замын дагуух жижиг сууринд оч.", Hamlet(0)),
            Rescue("rescue_soldiers", "Цэргүүдийг авар", "Сууринд бүслэгдсэн 3 цэргийг авар.", Hamlet(0), 3,
                   SurvivorKind::Soldier, 0),
            Clear("clear_hamlet", "Сууринг цэвэрлэ", "Сууринд байгаа болон шинээр гарч ирэх бүх зомбийг устга.", Hamlet(0), 1),
        } });

        // 19 Дорноговь
        stages.push_back({ "ДОРНОГОВЬ", "Хилийн заставыг сум хэрэгслээр хангаж, довтолгоог няц.", {
            Interact("radio_post", "Заставын радиог асаа", "Заставын радио станцыг асаа.", kPost, PropKind::Radio,
                     "Радио станц асаах"),
            Collect("ammo", "Сум цуглуул", "Агуулахын орчмоос 4 хайрцаг сум цуглуул.", kStore, 4, ItemKind::Ammo, 0),
            Deliver("ammo_post", "Сумыг заставт хүргэ", "Сумаа заставын хүлээлгэн өгөх цэгт хүргэ.", kPost, 1),
            Defend("hold_post", "Заставыг хамгаал", "Заставын бүсэд 90 секунд тэсэж үлд.", kPost, 90.0f, 2),
        } });

        // 20 Хэнтий
        stages.push_back({ "ХЭНТИЙ", "Отруудыг чөлөөлж, уулын аварга зомбийг устга.", {
            Reach("reach_camp", "Малчны оторт хүр", "Хамгийн ойрын малчны оторт оч.", Camp(0)),
            Rescue("rescue_herders", "Малчдыг авар", "Отор дахь 3 малчныг авар.", Camp(0), 3, SurvivorKind::Civilian, 0),
            Clear("clear_camp", "Хөрш отрыг цэвэрлэ", "Дараагийн отрыг эзэлсэн бүх зомбийг устга.", Camp(1), 1),
            Boss("mountain_giant", "Уулын аваргыг устга", "Гурав дахь оторт байгаа аварга зомбийг ял.", Camp(2), 2),
        } });

        // 21 Сүхбаатар
        stages.push_back({ "СҮХБААТАР", "Суурингаас эм авчирч, оторт хүргэн хамгаал.", {
            Collect("medicine", "Эм цуглуул", "Жижиг суурингаас 4 хайрцаг эм цуглуул.", Hamlet(0), 4, ItemKind::Medicine),
            Deliver("medicine_camp", "Эмийг оторт хүргэ", "Эмээ малчны отрын хүлээлгэн өгөх цэгт хүргэ.", Camp(0), 0),
            Defend("hold_camp", "Отрыг хамгаал", "Отрын бүсэд 75 секунд тэсэж үлд.", Camp(0), 75.0f, 1),
        } });

        // 22 Дорнод
        stages.push_back({ "ДОРНОД", "Сүүлчийн аймгийг чөлөөлж, зомбийн сүүлчийн аваргыг устга.", {
            Reach("reach_hamlet", "Сууринд хүр", "Шороон замын дагуух жижиг сууринд оч.", Hamlet(0)),
            Interact("radio_hamlet", "Улс даяар мэдэгд", "Суурингийн радио станцыг асаа.", Hamlet(0),
                     PropKind::Radio, "Радио станц асаах", 0),
            Clear("clear_hamlet", "Сууринг цэвэрлэ", "Сууринд байгаа болон шинээр гарч ирэх бүх зомбийг устга.", Hamlet(0), 1, 130),
            Boss("last_giant", "Сүүлчийн аваргыг устга", "Суурингийн дэргэд гарч ирэх хамгийн хүчтэй аварга зомбийг ял.",
                 Hamlet(0), 2),
        } });

        return stages;
    }
}

const StageDef& StageFor(int provinceIndex)
{
    static const std::vector<StageDef> stages = MakeStages();
    const int last = (int)stages.size() - 1;
    return stages[provinceIndex < 0 ? 0 : provinceIndex > last ? last : provinceIndex];
}
