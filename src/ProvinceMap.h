#pragma once

#include "Polygon.h"

#include <vector>

// Аяны газрын зургийн аймгуудын бодит хил (уртраг, өргөрөг), tools/gen_province_map.py үүсгэнэ.
// Дараалал нь Provinces()-тэй ижил. Зөвхөн газрын зургийн дэлгэцэд: даалгаврын талбар Regions.cpp-ийн бүдүүвчээр.
struct ProvinceMapShape
{
    std::vector<Polygon> parts; // хэсгүүд (эхнийх нь хамгийн том; Улаанбаатарын Багануур, Багахангай тусдаа)
    std::vector<Polygon> holes; // дотор орших өөр аймаг (Төв доторх Улаанбаатар, Сэлэнгэ доторх Дархан-Уул)
};

const std::vector<ProvinceMapShape>& ProvinceMapShapes();
