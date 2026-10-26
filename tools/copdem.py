"""Газрын өндөр: Copernicus GLO-30 DEM (30 м, 2011-2015 оны TanDEM-X хэмжилт; SRTM-ээс нарийн, алдаа бага).

    https://copernicus-dem-30m.s3.amazonaws.com/Copernicus_DSM_COG_10_N47_00_E106_00_DEM/...tif

1x1 градусын Cloud Optimized GeoTIFF (float32, DEFLATE + хөвөгч цэгийн predictor, 1024 px хавтан). Нэмэлт сан
хэрэггүй: зөвхөн хэрэгтэй хавтангуудыг задална. Файлуудыг tools/data/copdem_cache-д хадгална (git-д оруулахгүй).
Пиксел нь цэг (PixelIsPoint): (0, 0) пикселийн төв = хавтангийн баруун хойд булан.
Лиценз: (c) DLR e.V. 2010-2014 and (c) Airbus Defence and Space GmbH 2014-2018, provided under COPERNICUS by the
European Union and ESA; all rights reserved (үнэгүй ашиглах зөвшөөрөлтэй).
"""
import math, os, struct, urllib.request, zlib
from itertools import accumulate

CACHE = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'data', 'copdem_cache')
URL = 'https://copernicus-dem-30m.s3.amazonaws.com/{name}/{name}.tif'


def _name(lat, lon):
    ns = 'N%02d' % lat if lat >= 0 else 'S%02d' % -lat
    ew = 'E%03d' % lon if lon >= 0 else 'W%03d' % -lon
    return 'Copernicus_DSM_COG_10_%s_00_%s_00_DEM' % (ns, ew)


class _Tiff:
    def __init__(self, path):
        with open(path, 'rb') as f:
            self.data = f.read()
        d = self.data
        e = '<' if d[:2] == b'II' else '>'
        self.e = e
        offset, = struct.unpack(e + 'I', d[4:8])
        count, = struct.unpack(e + 'H', d[offset:offset + 2])
        tags = {}
        sizes = {3: 2, 4: 4, 12: 8, 2: 1, 16: 8}
        fmt = {3: 'H', 4: 'I', 12: 'd', 16: 'Q'}
        for i in range(count):
            tag, typ, n, value = struct.unpack(e + 'HHII', d[offset + 2 + i * 12:offset + 14 + i * 12])
            if typ not in fmt:
                continue
            size = sizes[typ] * n
            raw = d[offset + 10 + i * 12:offset + 10 + i * 12 + size] if size <= 4 else d[value:value + size]
            tags[tag] = struct.unpack(e + fmt[typ] * n, raw)
        self.width, self.height = tags[256][0], tags[257][0]
        self.tile = tags[322][0]
        assert tags[259][0] == 8 and tags[339][0] == 3 and tags[258][0] == 32, 'unexpected GeoTIFF layout'
        self.predictor = tags.get(317, (1,))[0]
        self.offsets, self.counts = tags[324], tags[325]
        self.across = (self.width + self.tile - 1) // self.tile
        self.scale = tags[33550][:2]
        tie = tags[33922]
        self.lon0, self.lat0 = tie[3], tie[4]
        self.cache = {}

    def _block(self, index):
        if index in self.cache:
            return self.cache[index]
        raw = zlib.decompress(self.data[self.offsets[index]:self.offsets[index] + self.counts[index]])
        t = self.tile
        stride = t * 4
        values = []
        unpack = struct.Struct('>%df' % t).unpack
        for row in range(t):
            line = raw[row * stride:(row + 1) * stride]
            if self.predictor == 3:
                # Хөвөгч цэгийн predictor: байтын зөрүү (мөр дагуу), дараа нь байтын хавтгай (MSB түрүүлж)
                line = bytes(accumulate(line, lambda a, b: (a + b) & 255))
                line = bytes(b for i in range(t) for b in (line[i], line[t + i], line[2 * t + i], line[3 * t + i]))
                values.extend(unpack(line))
            else:
                values.extend(struct.unpack(self.e + '%df' % t, line))
        self.cache[index] = values
        return values

    def pixel(self, col, row):
        col = min(max(col, 0), self.width - 1)
        row = min(max(row, 0), self.height - 1)
        t = self.tile
        block = self._block((row // t) * self.across + col // t)
        return block[(row % t) * t + col % t]


class Elevation:
    """Өргөрөг, уртрагаар өндөр (м, шугаман завсарлал). tools/dem.py-ийн Elevation-тэй ижил хэрэглээ."""

    def __init__(self, zoom=None):
        self.files = {}
        os.makedirs(CACHE, exist_ok=True)

    def _file(self, lat, lon):
        key = (int(math.floor(lat)), int(math.floor(lon)))
        if key not in self.files:
            name = _name(*key)
            path = os.path.join(CACHE, name + '.tif')
            if not os.path.exists(path):
                print('  copernicus: download %s' % name, flush=True)
                request = urllib.request.Request(URL.format(name=name), headers={'User-Agent': 'ZombieMongolia-mapprep/1.0'})
                with urllib.request.urlopen(request, timeout=300) as response:
                    body = response.read()
                with open(path + '.part', 'wb') as f:
                    f.write(body)
                os.replace(path + '.part', path)
            self.files[key] = _Tiff(path)
        return self.files[key]

    def _sample(self, lat, lon):
        tiff = self._file(lat, lon)
        col = (lon - tiff.lon0) / tiff.scale[0]
        row = (tiff.lat0 - lat) / tiff.scale[1]
        c0, r0 = int(math.floor(col)), int(math.floor(row))
        fc, fr = col - c0, row - r0
        # Хавтангийн ирмэгт: хөрш хавтангаас авна (зөвхөн дотор нь бол энэ файлаас)
        def value(c, r):
            if 0 <= c < tiff.width and 0 <= r < tiff.height:
                return tiff.pixel(c, r)
            return self._file(tiff.lat0 - r * tiff.scale[1] - 1e-9 if r >= tiff.height else tiff.lat0 - r * tiff.scale[1],
                              tiff.lon0 + c * tiff.scale[0] + (1e-9 if c >= tiff.width else 0)).pixel(0 if c >= tiff.width else c,
                                                                                                    0 if r >= tiff.height else r)
        h00, h10 = value(c0, r0), value(c0 + 1, r0)
        h01, h11 = value(c0, r0 + 1), value(c0 + 1, r0 + 1)
        return (h00 * (1 - fc) + h10 * fc) * (1 - fr) + (h01 * (1 - fc) + h11 * fc) * fr

    def at(self, lat, lon):
        return self._sample(lat, lon)
