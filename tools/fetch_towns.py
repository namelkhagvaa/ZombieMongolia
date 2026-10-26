"""Аймгийн төвүүдийн OpenStreetMap өгөгдлийг Overpass API-аас татна (tools/data/towns/<дугаар>.json.gz).

    python tools/fetch_towns.py [--refresh] [дугаар ...]

--refresh: татсан файлыг ч дахин татна (хамгийн сүүлийн OSM; хуучин нь <дугаар>.json.gz.old болж үлдэнэ).

Төв бүрээс ±2.65 км: зам, барилга, хашаа, ус, цэцэрлэг, төмөр зам (out geom), онцлох газрууд (out center).
Өгөгдөл: (c) OpenStreetMap contributors, ODbL 1.0.
"""
import gzip, json, math, os, sys, time, urllib.parse, urllib.request

# Аяны дугаар (src/CampaignData.cpp-ийн дараалал) -> аймгийн төв, OSM-ийн place зангилааны координат.
# 15 (Улаанбаатар) нь tools/data/ub_*.json.gz-д тусдаа.
TOWNS = {
    0: ('Өлгий', 48.96816, 89.96210), 1: ('Улаангом', 49.98003, 92.06885), 2: ('Ховд', 48.00064, 91.64524),
    3: ('Улиастай', 47.74156, 96.84748), 4: ('Алтай', 46.36434, 96.25325), 5: ('Мөрөн', 49.62848, 100.17456),
    6: ('Баянхонгор', 46.19366, 100.71427), 7: ('Цэцэрлэг', 47.47660, 101.45329), 8: ('Булган', 48.81292, 103.53228),
    9: ('Арвайхээр', 46.26819, 102.78631), 10: ('Эрдэнэт', 49.02760, 104.04439), 11: ('Даланзадгад', 43.57170, 104.42595),
    12: ('Сүхбаатар', 50.23589, 106.20949), 13: ('Дархан', 49.49224, 105.93616), 14: ('Зуунмод', 47.70827, 106.95449),
    16: ('Мандалговь', 45.76304, 106.27041), 17: ('Чойр', 46.35812, 108.36155), 18: ('Сайншанд', 44.88839, 110.14313),
    19: ('Чингис', 47.32561, 110.66491), 20: ('Баруун-Урт', 46.68077, 113.28455), 21: ('Чойбалсан', 48.06882, 114.52414),
}
HALF = 2650.0  # м
URLS = ['https://overpass-api.de/api/interpreter', 'https://overpass.private.coffee/api/interpreter',
        'https://maps.mail.ru/osm/tools/overpass/api/interpreter', 'https://overpass.kumi.systems/api/interpreter']


def query(lat, lon):
    dlat = HALF / 111320.0
    dlon = dlat / math.cos(math.radians(lat))
    b = '%.5f,%.5f,%.5f,%.5f' % (lat - dlat, lon - dlon, lat + dlat, lon + dlon)
    return ('[out:json][timeout:180];('
            'way[building](%(b)s);way[highway](%(b)s);way[waterway](%(b)s);way[natural=water](%(b)s);'
            'way[railway](%(b)s);way[leisure~"park|stadium|pitch"](%(b)s);way[landuse~"grass|forest|cemetery"](%(b)s);'
            'way[barrier~"^(fence|wall)$"](%(b)s);'
            'way[highway=pedestrian][area=yes](%(b)s);'
            'relation[building](%(b)s);relation[leisure~"park|stadium|pitch"](%(b)s);relation[natural=water](%(b)s);'
            'relation[landuse~"grass|forest|cemetery"](%(b)s););out geom;'
            '(nwr[amenity~"^(fuel|police|hospital|townhall|marketplace)$"](%(b)s);nwr[place=square](%(b)s);'
            'nwr[leisure=stadium](%(b)s);nwr[tourism~"museum|attraction"](%(b)s);nwr[office=government](%(b)s););'
            'out center tags;') % {'b': b}


def fetch(lat, lon):
    data = urllib.parse.urlencode({'data': query(lat, lon)}).encode()
    for attempt in range(24):
        url = URLS[attempt % len(URLS)]
        try:
            request = urllib.request.Request(url, data=data, headers={'User-Agent': 'ZombieMongolia-mapprep/1.0',
                                                                       'Accept': 'application/json'})
            with urllib.request.urlopen(request, timeout=300) as response:
                body = response.read()
            json.loads(body)
            return body
        except Exception as error:  # сервер завгүй: хүлээгээд дахин
            wait = min(120, 15 * (attempt + 1))
            print('  %s: %s, retry in %d s' % (url, error, wait), flush=True)
            time.sleep(wait)
    raise SystemExit('failed')


def main():
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'data', 'towns')
    os.makedirs(out, exist_ok=True)
    refresh = '--refresh' in sys.argv
    wanted = [int(a) for a in sys.argv[1:] if a != '--refresh'] or sorted(TOWNS)
    for index in wanted:
        name, lat, lon = TOWNS[index]
        path = os.path.join(out, '%02d.json.gz' % index)
        if os.path.exists(path) and not refresh:
            print('%2d %s: already downloaded' % (index, name), flush=True)
            continue
        body = fetch(lat, lon)
        with gzip.open(path + '.part', 'wb', 9) as f:
            f.write(body)
        if os.path.exists(path):
            os.replace(path, path + '.old')
        os.replace(path + '.part', path)
        print('%2d %s: %d KB (%d elements)' % (index, name, len(body) // 1024, len(json.loads(body)['elements'])), flush=True)
        time.sleep(8)


if __name__ == '__main__':
    main()
