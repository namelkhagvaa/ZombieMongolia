#include "City.h"

#include <cstring>

namespace
{
    // Бага-эндиан хоёртын урсгал уншигч: дуусвал (эсвэл эвдэрсэн бол) ok = false.
    struct Reader
    {
        const unsigned char* data;
        int size;
        int at = 0;
        bool ok = true;

        bool Need(int bytes)
        {
            if (!ok || at + bytes > size) ok = false;
            return ok;
        }
        uint8_t U8() { return Need(1) ? data[at++] : 0; }
        uint16_t U16()
        {
            if (!Need(2)) return 0;
            const uint16_t v = (uint16_t)(data[at] | data[at + 1] << 8);
            at += 2;
            return v;
        }
        uint32_t U32()
        {
            if (!Need(4)) return 0;
            uint32_t v;
            std::memcpy(&v, data + at, 4);
            at += 4;
            return v;
        }
        float F32()
        {
            if (!Need(4)) return 0.0f;
            float v;
            std::memcpy(&v, data + at, 4);
            at += 4;
            return v;
        }
        std::vector<Vector2> Points()
        {
            const int count = U16();
            std::vector<Vector2> points;
            if (!Need(count * 8)) return points;
            points.resize((size_t)count);
            for (Vector2& p : points)
            {
                p.x = F32();
                p.y = F32();
            }
            return points;
        }
    };

    CityData Load(const char* path)
    {
        CityData city;
        int size = 0;
#if defined(__ANDROID__)
        unsigned char* bytes = LoadFileData(path, &size); // APK-ийн assets-ийг FileExists хардаггүй
#else
        unsigned char* bytes = FileExists(path) ? LoadFileData(path, &size) : nullptr;
#endif
        if (!bytes)
        {
            TraceLog(LOG_WARNING, "CITY: %s not found", path);
            return city;
        }

        Reader r = { bytes, size };
        const bool header = r.Need(4) && std::memcmp(bytes, "CITY", 4) == 0;
        r.at = 4;
        if (!header || r.U32() != 1)
        {
            TraceLog(LOG_WARNING, "CITY: %s has an unknown format", path);
            UnloadFileData(bytes);
            return city;
        }

        city.roads.resize(r.U32());
        for (CityRoad& road : city.roads)
        {
            road.klass = (CityRoadClass)r.U8();
            road.dirt = r.U8() != 0;
            road.halfWidth = r.F32();
            road.points = r.Points();
        }
        city.buildings.resize(r.U32());
        for (CityBuilding& building : city.buildings)
        {
            building.type = (CityBuildingType)r.U8();
            building.levels = r.U8();
            building.height = r.F32();
            building.outline = r.Points();
        }
        city.areas.resize(r.U32());
        for (CityArea& area : city.areas)
        {
            area.kind = (CityAreaKind)r.U8();
            area.outline = r.Points();
        }
        city.lines.resize(r.U32());
        for (CityLine& line : city.lines)
        {
            line.kind = (CityLineKind)r.U8();
            line.halfWidth = r.F32();
            line.points = r.Points();
        }
        city.pois.resize(r.U32());
        for (CityPoi& poi : city.pois)
        {
            poi.kind = (CityPoiKind)r.U8();
            poi.position.x = r.F32();
            poi.position.y = r.F32();
            const int length = r.U16();
            if (r.Need(length)) poi.name.assign((const char*)bytes + r.at, (size_t)length);
            r.at += length;
        }
        UnloadFileData(bytes);

        if (!r.ok)
        {
            TraceLog(LOG_WARNING, "CITY: %s is truncated", path);
            return CityData{};
        }
        TraceLog(LOG_INFO, "CITY: %d roads, %d buildings, %d areas, %d lines, %d places", (int)city.roads.size(),
                 (int)city.buildings.size(), (int)city.areas.size(), (int)city.lines.size(), (int)city.pois.size());
        return city;
    }
}

const CityData& UlaanbaatarCity()
{
    static const CityData city = [] {
#if defined(__ANDROID__)
        return Load("city/ub.bin"); // APK-ийн assets-ийн үндсэнд
#endif
        // Ажлын хавтас, exe-ийн хавтас эсвэл түүний эх хавтас (хөгжүүлэлтийн build)
        const std::string application = GetApplicationDirectory();
        for (const std::string& folder : { std::string("assets/"), application + "assets/", application + "../assets/" })
        {
            const std::string path = folder + "city/ub.bin";
            if (FileExists(path.c_str())) return Load(path.c_str());
        }
        return Load("assets/city/ub.bin");
    }();
    return city;
}
