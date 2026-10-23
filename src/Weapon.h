#pragma once

// Зэвсгийн сайжруулалтын түвшнүүд (хадгалагдана). Галт зэвсэг бүгдэд үйлчилнэ.
struct WeaponLevels
{
    int damage = 0;
    int fireRate = 0;
    int magazine = 0;
};

constexpr int kMaxWeaponLevel = 5;

// Гарын 1..5 товчоор сонгох зэвсгүүд (дараалал нь товчны дараалал).
enum class WeaponType
{
    Knife,   // 1: ойрын тулаан
    Pistol,  // 2: гар буу, товшилт бүрд нэг сум
    Rifle,   // 3: автомат
    Shotgun, // 4: үрлэн буу, ойроос хүчтэй
    Grenade, // 5: шидэх гранат
    Sniper,  // мэргэн буудагчийн дурантай буу (түүний 3-р товч)
    Launcher, // 2-р дайчны танк эсэргүүцэгч пуужин харвагч (түүний 4-р товч, үрлэн бууны оронд)
    Count
};

// Багийн гишүүний зэвсгийн бүрдэл.
enum class Loadout
{
    Fighter, // хутга, гар буу, автомат, үрлэн буу, гранат
    Sniper,  // хутга, гар буу, дурантай буу
    Heavy    // хутга, гар буу, автомат, пуужин харвагч, гранат
};

struct WeaponSpec
{
    const char* name;
    float damage;      // нэг сум / үрлийн хохирол
    float interval;    // хоёр хэрэглээний хоорондох хугацаа, сек
    int magazine;      // 0 = сумгүй зэвсэг (хутга)
    int magazinePerLevel;
    float reloadTime;
    bool automatic;    // товчийг дарж байхад үргэлжлүүлэн буудах эсэх
    int pellets;       // нэг буудалтад гарах сум/үрлийн тоо
    float spread;      // тархалт, радиан
    float range;       // тусах дээд зай, метр
};

const WeaponSpec& SpecFor(WeaponType type);

// Хутганы дүрэм: зомбины бүрэн амины хэдэн хувийг авах вэ.
constexpr float kKnifeBodyFraction = 0.2f; // биед 5 удаа
constexpr float kKnifeHeadFraction = 0.5f; // толгойд 2 удаа
constexpr float kKnifeReach = 2.1f;        // метр

// Гранат
constexpr int kGrenadesPerMission = 4;
constexpr float kGrenadeRadius = 7.5f;     // дэлбэрэлтийн хүрэх зай
constexpr float kGrenadeMaxDamage = 320.0f; // төвд; зах руугаа шугаман буурна

// Пуужин: шулуун нисэж, туссан газраа дэлбэрнэ.
constexpr float kRocketSpeed = 46.0f;      // м/с
constexpr float kRocketRadius = 9.5f;      // дэлбэрэлтийн хүрэх зай
constexpr float kRocketMaxDamage = 950.0f; // төвд; зах руугаа шугаман буурна

// Тоглогчийн бүх зэвсэг: сонголт, сум, цэнэглэлт.
class Arsenal
{
public:
    // Түвшнийг тохируулж, бүх зэвсгийг дүүргэнэ (даалгаврын эхэнд).
    void Configure(const WeaponLevels& newLevels, Loadout newLoadout = Loadout::Fighter);
    // Даалгаврын дундуур сайжруулалт авахад: түвшинг шинэчилж, сумыг хөндөхгүй.
    void SetLevels(const WeaponLevels& newLevels) { levels = newLevels; }

    // Гарын 1.. товчнуудад харгалзах зэвсгүүд (бүрдлээс хамаарна).
    int SlotCount() const;
    WeaponType SlotWeapon(int slot) const;
    int CurrentSlot() const;
    bool Has(WeaponType type) const;
    void Update(float dt);

    void Select(WeaponType type);
    WeaponType Current() const { return current; }
    const WeaponSpec& Spec() const { return SpecFor(current); }

    // Одоогийн зэвсгийг хэрэглэж чадсан бол true (сум/гранат зарцуулна).
    // Сум дууссан бол өөрөө цэнэглэж эхэлнэ.
    bool TryUse();
    void Reload();

    float Damage() const;       // сайжруулалтыг тооцсон нэг сумны хохирол
    float Interval() const;
    int Ammo() const;           // хутганд 0; гранатад үлдсэн тоо
    int MagazineSize() const;
    bool IsReloading() const { return reloadTimer > 0.0f; }
    // Цэнэглэлтийн явц 0..1; цэнэглээгүй бол -1 (гарын хөдөлгөөнд).
    float ReloadProgress() const { return reloadTimer > 0.0f ? 1.0f - reloadTimer / Spec().reloadTime : -1.0f; }
    bool UsesAmmo() const { return Spec().magazine > 0; }

    void AddGrenades(int count);

    // Цэнэглэхэд хасагдах нөөц сум (багийн гишүүд нэгийг хуваалцана). nullptr = хязгааргүй.
    void SetReserve(int* pool) { reserve = pool; }

private:
    int MagazineSizeOf(WeaponType type) const;

    WeaponLevels levels;
    int* reserve = nullptr;
    Loadout loadout = Loadout::Fighter;
    WeaponType current = WeaponType::Rifle;
    int ammo[(int)WeaponType::Count] = {};
    float cooldown = 0.0f;
    float reloadTimer = 0.0f;
};
