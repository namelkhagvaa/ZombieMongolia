"""Газрын өндрийн өгөгдөл (DEM): AWS Terrain Tiles (Terrarium PNG, ~30 м, SRTM ба бусад эх сурвалж).

    https://s3.amazonaws.com/elevation-tiles-prod/terrarium/{z}/{x}/{y}.png
    өндөр (м) = R * 256 + G + B / 256 - 32768

Хавтангуудыг tools/data/dem_cache-д хадгална (git-д оруулахгүй). Нэмэлт сан хэрэггүй (PNG-ийг энд задална).
Эх сурвалж: https://github.com/tilezen/joerd/blob/master/docs/attribution.md
"""
import math, os, struct, urllib.request, zlib

CACHE = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'data', 'dem_cache')
URL = 'https://s3.amazonaws.com/elevation-tiles-prod/terrarium/%d/%d/%d.png'


def _decode_png(data):
    pos, idat = 8, []
    width = height = color = 0
    while pos < len(data):
        length, = struct.unpack('>I', data[pos:pos + 4])
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b'IHDR':
            width, height, depth, color = struct.unpack('>IIBB', body[:10])
        elif kind == b'IDAT':
            idat.append(body)
    raw = zlib.decompress(b''.join(idat))
    bpp = 3 if color == 2 else 4
    stride = width * bpp
    heights = [0.0] * (width * height)
    prev = bytearray(stride)
    i = 0
    for y in range(height):
        f = raw[i]
        line = bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        if f == 1:
            for k in range(bpp, stride): line[k] = (line[k] + line[k - bpp]) & 255
        elif f == 2:
            for k in range(stride): line[k] = (line[k] + prev[k]) & 255
        elif f == 3:
            for k in range(stride): line[k] = (line[k] + ((line[k - bpp] if k >= bpp else 0) + prev[k]) // 2) & 255
        elif f == 4:
            for k in range(stride):
                a = line[k - bpp] if k >= bpp else 0
                b = prev[k]
                c = prev[k - bpp] if k >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[k] = (line[k] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        row = y * width
        for x in range(width):
            o = x * bpp
            heights[row + x] = line[o] * 256 + line[o + 1] + line[o + 2] / 256.0 - 32768.0
        prev = line
    return width, height, heights


class Elevation:
    """Нэг томруулалтын (zoom) хавтангуудаас өргөрөг, уртрагаар өндөр (шугаман завсарлал)."""

    def __init__(self, zoom):
        self.zoom = zoom
        self.tiles = {}
        os.makedirs(CACHE, exist_ok=True)

    def _tile(self, tx, ty):
        key = (tx, ty)
        if key not in self.tiles:
            path = os.path.join(CACHE, '%d_%d_%d.png' % (self.zoom, tx, ty))
            if not os.path.exists(path):
                request = urllib.request.Request(URL % (self.zoom, tx, ty), headers={'User-Agent': 'ZombieMongolia-mapprep/1.0'})
                with urllib.request.urlopen(request, timeout=60) as response:
                    body = response.read()
                with open(path, 'wb') as f:
                    f.write(body)
            with open(path, 'rb') as f:
                self.tiles[key] = _decode_png(f.read())
        return self.tiles[key]

    def _pixel(self, px, py):
        tx, ty = int(px // 256), int(py // 256)
        width, height, heights = self._tile(tx, ty)
        return heights[int(py - ty * 256) * width + int(px - tx * 256)]

    def at(self, lat, lon):
        n = 2 ** self.zoom
        px = (lon + 180.0) / 360.0 * n * 256 - 0.5
        py = (1.0 - math.asinh(math.tan(math.radians(lat))) / math.pi) / 2.0 * n * 256 - 0.5
        x0, y0 = math.floor(px), math.floor(py)
        fx, fy = px - x0, py - y0
        h00 = self._pixel(x0, y0); h10 = self._pixel(x0 + 1, y0)
        h01 = self._pixel(x0, y0 + 1); h11 = self._pixel(x0 + 1, y0 + 1)
        return (h00 * (1 - fx) + h10 * fx) * (1 - fy) + (h01 * (1 - fx) + h11 * fx) * fy
