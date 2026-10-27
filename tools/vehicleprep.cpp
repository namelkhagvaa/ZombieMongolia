// Tripo3D-ийн машины загварыг тоглоомд бэлтгэх хэрэгсэл (raylib + meshoptimizer).
// Оролт нь tools/glb_extract.py-ийн гаргасан түүхий өгөгдөл: <in>.pos, <in>.uv, <in>.idx
//
//   vehicleprep views <in> <texture.png> <out.png>
//       Хажуу, дээрээс, урдаас харсан зураг (0.05 нэгж тутамд тор): дугуйн байрлал, урд талыг тодорхойлоход.
//   vehicleprep build <in> <out.vmesh> <triangles> <length> <frontSign> <radius> <sideX> <z1> [<z2> ...]
//       Загварыг triangles хүртэл багасгаж, урт нь length м болохоор масштаблана (урд тал = +Z).
//       Дугуйнууд: тэнхлэг бүрийн z (эх загварын координатаар), ойролцоо радиус ба хажуугийн |x| (нарийн утгыг геометрээс олно);
//       тэдгээрийн гурвалжнуудыг их биеэс салгаж тусдаа хэсэг болгоно.
//   vehicleprep prop <in> <out.vmesh> <triangles> <length>
//       Дугуйгүй нэг хэсэгтэй загвар (зэвсэг, гильз): triangles хүртэл багасгаж, Z тэнхлэгийн дагуух урт нь length м
//       болохоор масштаблана. Чиглэлийг өмнө нь tools/prop_align.py-аар тааруулна (эх нь хэвээр үлдэнэ).
//
// .vmesh формат (little-endian):
//   "VMSH" u32 version=1, u32 partCount
//   хэсэг бүр: f32 pivot[3] (дугуйн төв; их биеийнх 0), u32 kind (0 их бие, 1 урд дугуй, 2 хойд дугуй),
//              u32 vertexCount, u32 indexCount, f32 position[3*vc], f32 normal[3*vc], f32 uv[2*vc], u16 index[ic]
//   Байрлалууд нь хэсгийн pivot-оос хамааралтай (дугуй эргэхэд төвөө тойрно).
//   kind 3: нисдэг тэрэгний үндсэн сэнс (Y тэнхлэгийг тойрно), 4: сүүлний сэнс (X тэнхлэгийг тойрно).
//
// Нэмэлт хэсгүүд (сэнс): <in>.labels (гурвалжин бүрд u8: 0 бол их бие эсвэл дугуй, n > 0 бол <in>.pivots-ийн n дэх мөр)
// ба <in>.pivots (мөр бүр "kind x y z", эх загварын координатаар) байвал тэдгээр гурвалжнууд тусдаа хэсэг болно.
// Нисдэг тэрэг (сэнсийг tools/heli_rotors.py шошголно, урд = +Z болгож эргүүлнэ):
//   python tools/glb_extract.py assets/cars/unadag_helicopter.glb tmp/heli
//   python tools/heli_rotors.py tmp/heli 0 label 0.355 0.05 -0.29 -0.40 0.28 -1 0
//   vehicleprep build tmp/heli_r assets/cars/heli.vmesh 30000 18.6 1 0 0
//   python tools/glb_extract.py assets/cars/drop_buulgadag_helicopter.glb tmp/drop
//   python tools/heli_rotors.py tmp/drop 90 label 0.29 0.05 -0.30 -0.38 0.16 0.034 1
//   vehicleprep build tmp/drop_r assets/cars/dropheli.vmesh 24000 18.6 1 0 0
// Мод, торх (assets/trees, assets/pooshig): өндөр нь 1 м болохоор (VPREP_HEIGHT=1), зурахдаа өндрөөр нь масштаблана
// (холын хувилбар: VPREP_MODE=1 ... 60 1, дараа нь tools/far_trees.py -> <name>_far.bin):
//   python tools/glb_extract.py "assets/trees/pine tree 3d model.glb" tmp/larch   (+ текстур 1024 px: trees/larch_color.png)
//   VPREP_HEIGHT=1 vehicleprep prop tmp/larch assets/trees/larch.vmesh 1500 1
//   birch, elm, saxaul (desert tree) ижил; торх: barrel_fuel (red steel drum), barrel_old (rusty drum) 500 1
//
// Хөрвүүлэх (meshoptimizer v0.22: git clone --branch v0.22 https://github.com/zeux/meshoptimizer.git <dir>):
//   g++ -O2 -std=c++17 tools/vehicleprep.cpp <dir>/src/*.cpp -I build/_deps/raylib-src/src -I <dir>/src
//       build/_deps/raylib-build/raylib/libraylib.a -lopengl32 -lgdi32 -lwinmm -o vehicleprep.exe
// G-Class-ийг ингэж бэлтгэсэн (урт 4.3 м, урд = +Z, дугуйн радиус 0.097, тэнхлэг 0.302 / -0.27, |x| 0.184):
//   python tools/glb_extract.py assets/cars/G-class.glb tmp/gclass   (+ текстурыг 1024 px PNG болгох)
//   vehicleprep build tmp/gclass assets/cars/gclass.vmesh 20000 4.3 1 0.097 0.184 0.302 -0.27
//   vehicleprep build tmp/truck assets/cars/truck.vmesh 20000 7.4 1 0.086 0.161 0.316 -0.141 -0.334
// Зэвсэг (assets/buu): AK47.glb -> ak47.vmesh (урт 1.15 м = DrawGun-ийн хэмжээ), sum.glb -> sum.vmesh (57 мм)
//   python tools/glb_extract.py assets/buu/AK47.glb tmp/ak47_raw
//   python tools/prop_align.py tmp/ak47_raw tmp/ak47          (+ текстурыг 1024 px PNG: assets/buu/ak47_color.png)
//   vehicleprep prop tmp/ak47 assets/buu/ak47.vmesh 4000 1.15     (гаралт ~12000 гурвалжин)
//   python tools/glb_extract.py assets/buu/sum.glb tmp/sum_raw
//   python tools/prop_align.py tmp/sum_raw tmp/sum --upright  (+ текстурыг 256 px PNG: assets/buu/sum_color.png)
//   vehicleprep prop tmp/sum assets/buu/sum.vmesh 150 0.057       (гаралт ~450 гурвалжин)
//   Sniper.glb -> sniper.vmesh (prop_align, 4000 1.49; 1024 px), pistol.glb -> pistol.vmesh (prop_align ... 0.04, 2000 0.23; 512 px)
//   shotgun.glb -> shotgun.vmesh (prop_align, 4000 1.255; 1024 px), knife.glb -> knife.vmesh (prop_align, 1500 0.44; 512 px)
#include "raylib.h"
#include "meshoptimizer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    template <typename T>
    std::vector<T> ReadAll(const std::string& path)
    {
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) { std::printf("cannot open %s\n", path.c_str()); std::exit(1); }
        std::fseek(f, 0, SEEK_END);
        const long size = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        std::vector<T> data((size_t)size / sizeof(T));
        if (std::fread(data.data(), sizeof(T), data.size(), f) != data.size()) { std::printf("read error\n"); std::exit(1); }
        std::fclose(f);
        return data;
    }

    struct Source
    {
        std::vector<float> pos, uv;
        std::vector<unsigned int> idx;
        size_t VertexCount() const { return pos.size() / 3; }
    };

    Source Load(const std::string& prefix)
    {
        Source s;
        s.pos = ReadAll<float>(prefix + ".pos");
        s.uv = ReadAll<float>(prefix + ".uv");
        s.idx = ReadAll<unsigned int>(prefix + ".idx");
        std::printf("loaded %zu vertices, %zu triangles\n", s.VertexCount(), s.idx.size() / 3);
        return s;
    }

    void Bounds(const Source& s, float mn[3], float mx[3])
    {
        for (int k = 0; k < 3; ++k) { mn[k] = 1e9f; mx[k] = -1e9f; }
        for (size_t i = 0; i < s.VertexCount(); ++i)
        {
            for (int k = 0; k < 3; ++k)
            {
                mn[k] = std::min(mn[k], s.pos[i * 3 + k]);
                mx[k] = std::max(mx[k], s.pos[i * 3 + k]);
            }
        }
    }

    // --- views: гурван төсөөлөл, z-buffer-тэй цэгэн зураг ---
    int Views(const std::string& prefix, const char* texturePath, const char* outPath)
    {
        Source s = Load(prefix);
        Image texture = LoadImage(texturePath);
        ImageFormat(&texture, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        const Color* texels = (const Color*)texture.data;
        float mn[3], mx[3];
        Bounds(s, mn, mx);
        std::printf("bounds x %.3f..%.3f y %.3f..%.3f z %.3f..%.3f\n", mn[0], mx[0], mn[1], mx[1], mn[2], mx[2]);

        const float scale = 700.0f; // пиксел / нэгж
        // Гурван цонх: хажуу (z -> баруун, y -> дээш, +x талаас), дээрээс (z, x), урдаас (x, y; +z талаас)
        struct View { int axisU, axisV, axisDepth; float depthSign; const char* name; };
        const View views[3] = { { 2, 1, 0, 1.0f, "side (+x looking)" }, { 2, 0, 1, 1.0f, "top" }, { 0, 1, 2, 1.0f, "front (+z looking)" } };
        const int pad = 30;
        int widths[3], heights[3];
        for (int v = 0; v < 3; ++v)
        {
            widths[v] = (int)((mx[views[v].axisU] - mn[views[v].axisU]) * scale) + 2 * pad;
            heights[v] = (int)((mx[views[v].axisV] - mn[views[v].axisV]) * scale) + 2 * pad;
        }
        const int W = widths[0] + widths[2], H = heights[0] + heights[1];
        Image out = GenImageColor(W, H, { 40, 60, 80, 255 });
        std::vector<float> depth((size_t)W * H, -1e9f);
        int ox[3] = { 0, 0, widths[0] }, oy[3] = { 0, heights[0], 0 };

        for (int v = 0; v < 3; ++v)
        {
            const View& view = views[v];
            // Тор: 0.05 нэгж тутамд
            for (float g = std::floor(mn[view.axisU] / 0.05f) * 0.05f; g <= mx[view.axisU]; g += 0.05f)
            {
                const int px = ox[v] + pad + (int)((g - mn[view.axisU]) * scale);
                const bool zero = std::fabs(g) < 0.001f;
                for (int py = oy[v]; py < oy[v] + heights[v]; ++py)
                    ImageDrawPixel(&out, px, py, zero ? Color{ 255, 80, 80, 255 } : Color{ 70, 90, 110, 255 });
            }
            for (float g = std::floor(mn[view.axisV] / 0.05f) * 0.05f; g <= mx[view.axisV]; g += 0.05f)
            {
                const int py = oy[v] + heights[v] - pad - (int)((g - mn[view.axisV]) * scale);
                const bool zero = std::fabs(g) < 0.001f;
                for (int px = ox[v]; px < ox[v] + widths[v]; ++px)
                    ImageDrawPixel(&out, px, py, zero ? Color{ 255, 80, 80, 255 } : Color{ 70, 90, 110, 255 });
            }
        }
        for (size_t i = 0; i < s.VertexCount(); ++i)
        {
            const float* p = &s.pos[i * 3];
            const float u = s.uv[i * 2], w = s.uv[i * 2 + 1];
            const int tx = std::clamp((int)(u * texture.width), 0, texture.width - 1);
            const int ty = std::clamp((int)(w * texture.height), 0, texture.height - 1);
            Color c = texels[ty * texture.width + tx];
            for (int v = 0; v < 3; ++v)
            {
                const View& view = views[v];
                const int px = ox[v] + pad + (int)((p[view.axisU] - mn[view.axisU]) * scale);
                const int py = oy[v] + heights[v] - pad - (int)((p[view.axisV] - mn[view.axisV]) * scale);
                const float d = p[view.axisDepth] * view.depthSign;
                if (px < 0 || py < 0 || px >= W || py >= H) continue;
                float& z = depth[(size_t)py * W + px];
                if (d < z) continue;
                z = d;
                // Гүнээр бага зэрэг сүүдэрлэнэ (хэлбэр харагдахаар)
                const float k = 0.55f + 0.45f * (d - mn[view.axisDepth]) / (mx[view.axisDepth] - mn[view.axisDepth]);
                const Color shaded = { (unsigned char)std::min(255.0f, 40 + c.r * k * 1.4f), (unsigned char)std::min(255.0f, 40 + c.g * k * 1.4f),
                                       (unsigned char)std::min(255.0f, 40 + c.b * k * 1.4f), 255 };
                ImageDrawPixel(&out, px, py, shaded);
            }
        }
        // (ImageDrawText нь цонхгүй үед үндсэн фонтгүй тул ашиглахгүй)
        ExportImage(out, outPath);
        std::printf("scale %.0f px/unit, grid 0.05, red = 0\n", scale);
        return 0;
    }
}

int Build(int argc, char** argv);

int main(int argc, char** argv)
{
    SetTraceLogLevel(LOG_WARNING);
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc >= 5 && std::strcmp(argv[1], "views") == 0) return Views(argv[2], argv[3], argv[4]);
    if (argc >= 5 && std::strcmp(argv[1], "texture") == 0)
    {
        // texture <in.jpg|png> <out.png> <size>: glb_extract-ийн гаргасан өнгөний зургийг size px PNG болгоно
        Image image = LoadImage(argv[2]);
        if (!image.data) { std::printf("cannot load %s\n", argv[2]); return 1; }
        const int size = std::atoi(argv[4]);
        if (image.width > size || image.height > size) ImageResize(&image, size, size);
        const bool ok = ExportImage(image, argv[3]);
        std::printf("%s %dx%d\n", ok ? "saved" : "FAILED", image.width, image.height);
        UnloadImage(image);
        return ok ? 0 : 1;
    }
    if (argc >= 9 && std::strcmp(argv[1], "build") == 0) return Build(argc, argv); // (тэнхлэггүй: зөвхөн сэнс гэх мэт хэсгүүд)
    if (argc >= 6 && std::strcmp(argv[1], "prop") == 0)
    {
        // Дугуйгүй build: урд = +Z, радиус ба хажуу 0, тэнхлэг байхгүй
        char frontSign[] = "1", zero[] = "0";
        char* buildArgs[] = { argv[0], argv[1], argv[2], argv[3], argv[4], argv[5], frontSign, zero, zero };
        return Build(9, buildArgs);
    }
    std::printf("usage: see source\n");
    return 1;
}

// --- build: багасгах, дугуй салгах, масштаблах ---
namespace
{
    struct Part
    {
        float pivot[3] = { 0, 0, 0 };
        unsigned int kind = 0;
        std::vector<unsigned int> triangles; // эх индексүүд (3-аар)
    };

    void WriteU32(FILE* f, unsigned int v) { std::fwrite(&v, 4, 1, f); }
}

int Build(int argc, char** argv)
{
    Source s = Load(argv[2]);
    const char* outPath = argv[3];
    const size_t targetTriangles = (size_t)std::atoi(argv[4]);
    const float length = (float)std::atof(argv[5]);
    const float frontSign = (float)std::atof(argv[6]); // +1: эх загварын +Z нь урд, -1: -Z нь урд
    const float roughRadius = (float)std::atof(argv[7]);
    const float roughSideX = (float)std::atof(argv[8]);
    std::vector<float> axles;
    for (int i = 9; i < argc; ++i) axles.push_back((float)std::atof(argv[i]));
    float mn[3], mx[3];
    Bounds(s, mn, mx);
    // 0: текстурын хилийг хадгалж хялбаршуулна; 2 (VPREP_MODE=2): зөвхөн байрлалаар (оройнууд нь хуваагдсан загварт)
    const int mode = std::getenv("VPREP_MODE") ? std::atoi(std::getenv("VPREP_MODE")) : 0;
    // VPREP_ERROR: хялбаршуулалтын зөвшөөрөгдөх алдаа (загварын хэмжээний хувиар; анхдагч 0.05)
    const float maxError = std::getenv("VPREP_ERROR") ? (float)std::atof(std::getenv("VPREP_ERROR")) : 0.05f;

    // 1. Ижил (байрлал + uv) оройнуудыг нэгтгэнэ
    const size_t vc = s.VertexCount();
    struct Vtx { float p[3]; float t[2]; };
    std::vector<Vtx> verts(vc);
    for (size_t i = 0; i < vc; ++i)
    {
        std::memcpy(verts[i].p, &s.pos[i * 3], 12);
        std::memcpy(verts[i].t, &s.uv[i * 2], 8);
    }
    std::vector<unsigned int> remap(vc);
    const size_t unique = meshopt_generateVertexRemap(remap.data(), s.idx.data(), s.idx.size(), verts.data(), vc, sizeof(Vtx));
    std::vector<Vtx> uv(unique);
    meshopt_remapVertexBuffer(uv.data(), verts.data(), vc, sizeof(Vtx), remap.data());
    std::vector<unsigned int> indices(s.idx.size());
    meshopt_remapIndexBuffer(indices.data(), s.idx.data(), s.idx.size(), remap.data());
    std::printf("welded: %zu unique vertices\n", unique);

    // 2. Дугуй бүрийн төв, радиус, өргөнийг геометрээс нарийвчилна (нүдээр хэмжсэн утгаас эхэлж).
    //    Газрын ойролцоо зөвхөн дугуй байдаг: хоёр өндөрт (h) дугуйн хөвчийн хагас урт a-г хэмжиж
    //    r = (a^2 + h^2) / (2h) томъёогоор радиусыг, хөвчийн дундаас z төвийг олно. Дугуйн хаалт, гишгүүр нь
    //    илүү өндөрт байдаг тул нөлөөлөхгүй.
    struct Wheel { float c[3]; float r; float x0, x1; unsigned int kind; };
    std::vector<Wheel> wheels;
    for (float z : axles)
    {
        for (float side : { 1.0f, -1.0f })
        {
            Wheel w = { { side * roughSideX, roughRadius, z }, roughRadius, 0, 0, (z * frontSign > 0.0f) ? 1u : 2u };
            // Дугуйн доод цэг (газар)
            float ground = 1e9f;
            for (size_t i = 0; i < unique; ++i)
            {
                const float* p = uv[i].p;
                if (p[0] * side <= 0.0f || std::fabs(p[0] - w.c[0]) > roughRadius * 0.4f || std::fabs(p[2] - z) > roughRadius * 0.5f) continue;
                ground = std::min(ground, p[1]);
            }
            float radii = 0.0f, centers = 0.0f;
            int samples = 0;
            for (float fraction : { 0.25f, 0.4f, 0.55f })
            {
                const float h = roughRadius * fraction, band = roughRadius * 0.03f;
                float zmin = 1e9f, zmax = -1e9f;
                for (size_t i = 0; i < unique; ++i)
                {
                    const float* p = uv[i].p;
                    if (p[0] * side <= 0.0f || std::fabs(p[0] - w.c[0]) > roughRadius * 0.4f) continue;
                    if (std::fabs(p[1] - ground - h) > band || std::fabs(p[2] - z) > roughRadius * 1.15f) continue;
                    zmin = std::min(zmin, p[2]);
                    zmax = std::max(zmax, p[2]);
                }
                if (zmax <= zmin) continue;
                const float a = (zmax - zmin) * 0.5f;
                const float r = (a * a + h * h) / (2.0f * h);
                std::printf("    h %.3f: chord %.4f..%.4f -> r %.4f\n", h, zmin, zmax, r);
                radii += r;
                centers += (zmin + zmax) * 0.5f;
                ++samples;
            }
            if (samples > 0)
            {
                w.r = radii / (float)samples;
                w.c[2] = centers / (float)samples;
            }
            w.c[1] = ground + w.r;
            // Өргөн: цилиндр доторх оройнуудын x-ийн хүрээ (их биеийн дотор тал руу хэт гүнзгий орохгүй)
            w.x0 = 1e9f; w.x1 = -1e9f;
            for (size_t i = 0; i < unique; ++i)
            {
                const float* p = uv[i].p;
                // Зөвхөн газарт хүрч буй доод хэсгээс: тэнд дугуйнаас өөр зүйл (тэнхлэг, түдгэлзүүр) байхгүй
                if (p[0] * side <= 0.0f || std::fabs(p[0] - w.c[0]) > w.r * 0.8f) continue;
                if (std::fabs(p[2] - w.c[2]) > w.r * 0.35f || p[1] > ground + w.r * 0.12f) continue;
                w.x0 = std::min(w.x0, p[0]);
                w.x1 = std::max(w.x1, p[0]);
            }
            w.c[0] = (w.x0 + w.x1) * 0.5f;
            std::printf("wheel z %.3f side %+.0f: center (%.4f %.4f %.4f) r %.4f width %.4f..%.4f\n", z, side, w.c[0], w.c[1], w.c[2],
                        w.r, w.x0, w.x1);
            wheels.push_back(w);
        }
    }

    // Нэмэлт хэсгүүд (сэнс): шошго ба эргэх төвүүд
    std::vector<unsigned char> labels;
    struct Extra { unsigned int kind; float c[3]; };
    std::vector<Extra> extras;
    if (FILE* pf = std::fopen((std::string(argv[2]) + ".pivots").c_str(), "r"))
    {
        Extra e;
        while (std::fscanf(pf, "%u %f %f %f", &e.kind, &e.c[0], &e.c[1], &e.c[2]) == 4) extras.push_back(e);
        std::fclose(pf);
        labels = ReadAll<unsigned char>(std::string(argv[2]) + ".labels");
        if (labels.size() != indices.size() / 3) { std::printf("labels: %zu, triangles %zu\n", labels.size(), indices.size() / 3); return 1; }
        std::printf("%zu extra parts from labels\n", extras.size());
    }

    std::vector<Part> parts(1 + wheels.size() + extras.size());
    for (size_t w = 0; w < wheels.size(); ++w)
    {
        for (int a = 0; a < 3; ++a) parts[1 + w].pivot[a] = wheels[w].c[a];
        parts[1 + w].kind = wheels[w].kind;
    }
    for (size_t e = 0; e < extras.size(); ++e)
    {
        Part& part = parts[1 + wheels.size() + e];
        for (int a = 0; a < 3; ++a) part.pivot[a] = extras[e].c[a];
        part.kind = extras[e].kind;
    }
    for (size_t t = 0; t < indices.size(); t += 3)
    {
        float c[3] = { 0, 0, 0 };
        for (int k = 0; k < 3; ++k)
            for (int a = 0; a < 3; ++a) c[a] += uv[indices[t + k]].p[a] / 3.0f;
        int target = 0;
        if (!labels.empty() && labels[t / 3] > 0 && labels[t / 3] <= extras.size()) target = (int)(wheels.size() + labels[t / 3]);
        for (size_t w = 0; w < wheels.size() && target == 0; ++w)
        {
            const Wheel& wheel = wheels[w];
            const float dy = c[1] - wheel.c[1], dz = c[2] - wheel.c[2];
            const float margin = wheel.r * 0.04f;
            const bool inside = dy * dy + dz * dz < (wheel.r + margin) * (wheel.r + margin) && c[0] > wheel.x0 - margin &&
                                c[0] < wheel.x1 + margin;
            if (inside) target = (int)(1 + w);
        }
        for (int k = 0; k < 3; ++k) parts[target].triangles.push_back(indices[t + k]);
    }

    // Шалгах зураг: хажуугаас, дугуйд орсон оройнууд улаан
    {
        const float scale = 700.0f;
        const int W = (int)((mx[2] - mn[2]) * scale) + 60, H = (int)((mx[1] - mn[1]) * scale) + 60;
        Image debug = GenImageColor(W, H, { 30, 40, 50, 255 });
        for (size_t pi = 0; pi < parts.size(); ++pi)
        {
            for (unsigned int index : parts[pi].triangles)
            {
                const float* p = uv[index].p;
                if (p[0] < 0.0f) continue; // зөвхөн нэг тал
                const int px = 30 + (int)((p[2] - mn[2]) * scale), py = H - 30 - (int)((p[1] - mn[1]) * scale);
                ImageDrawPixel(&debug, px, py, pi == 0 ? Color{ 150, 160, 170, 255 } : Color{ 255, 60, 60, 255 });
            }
        }
        ExportImage(debug, (std::string(outPath) + "_debug.png").c_str());
    }

    // 3. Хэсэг бүрийг багасгаж, масштаблаад бичнэ
    // VPREP_HEIGHT=1: урт биш өндрийг (Y) length болгоно (мод, торх: өндөр нь 1 м, зурахдаа масштаблана)
    const float sourceLength = std::getenv("VPREP_HEIGHT") ? mx[1] - mn[1] : mx[2] - mn[2];
    const float k = length / sourceLength;
    FILE* f = std::fopen(outPath, "wb");
    std::fwrite("VMSH", 1, 4, f);
    WriteU32(f, 1);
    WriteU32(f, (unsigned int)parts.size());
    size_t totalTriangles = 0;
    for (size_t pi = 0; pi < parts.size(); ++pi)
    {
        Part& part = parts[pi];
        const size_t share = std::max<size_t>(300, (size_t)((double)targetTriangles * (double)part.triangles.size() / (double)(indices.size() / 3)));
        std::vector<unsigned int> simplified(part.triangles.size());
        float error = 0.0f;
        size_t count = 0;
        if (mode == 0)
        {
            count = meshopt_simplify(simplified.data(), part.triangles.data(), part.triangles.size(), &uv[0].p[0], unique, sizeof(Vtx),
                                     std::min(part.triangles.size(), share * 3), maxError, 0, &error);
        }
        else if (mode == 1)
        {
            count = meshopt_simplifySloppy(simplified.data(), part.triangles.data(), part.triangles.size(), &uv[0].p[0], unique,
                                           sizeof(Vtx), share * 3, 1.0f, &error);
        }
        else
        {
            // Зөвхөн байрлалаар: текстурын хил (seam) хялбаршуулалтыг зогсоохгүй. Үлдсэн орой бүр өөрийн uv-тэй.
            std::vector<unsigned int> shadow(part.triangles.size());
            meshopt_generateShadowIndexBuffer(shadow.data(), part.triangles.data(), part.triangles.size(), &uv[0].p[0], unique,
                                              sizeof(float) * 3, sizeof(Vtx));
            count = meshopt_simplify(simplified.data(), shadow.data(), shadow.size(), &uv[0].p[0], unique, sizeof(Vtx),
                                     std::min(shadow.size(), share * 3), maxError, 0, &error);
        }
        simplified.resize(count);
        meshopt_optimizeVertexCache(simplified.data(), simplified.data(), count, unique);

        // Ашиглагдсан оройнуудыг шинэ дугаарлалтаар
        std::vector<int> map(unique, -1);
        std::vector<float> positions, normals, texcoords;
        std::vector<unsigned short> out;
        for (unsigned int index : simplified)
        {
            if (map[index] < 0)
            {
                map[index] = (int)(positions.size() / 3);
                const Vtx& v = uv[index];
                // Урд талыг +Z болгоно (frontSign = -1 бол X, Z хоёулаа эсрэг: 180° эргүүлнэ)
                const float px = (v.p[0] - part.pivot[0]) * frontSign * k;
                const float py = (v.p[1] - part.pivot[1]) * k;
                const float pz = (v.p[2] - part.pivot[2]) * frontSign * k;
                positions.insert(positions.end(), { px, py, pz });
                normals.insert(normals.end(), { 0.0f, 0.0f, 0.0f });
                texcoords.insert(texcoords.end(), { v.t[0], v.t[1] });
            }
            if (map[index] > 65535) { std::printf("too many vertices in part %zu\n", pi); return 1; }
            out.push_back((unsigned short)map[index]);
        }
        // Гөлгөр normal: гурвалжны normal-уудыг оройд нийлүүлнэ
        for (size_t t = 0; t + 2 < out.size(); t += 3)
        {
            const float* a = &positions[out[t] * 3];
            const float* b = &positions[out[t + 1] * 3];
            const float* c = &positions[out[t + 2] * 3];
            const float e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
            const float n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
            for (int j = 0; j < 3; ++j)
                for (int a2 = 0; a2 < 3; ++a2) normals[out[t + j] * 3 + a2] += n[a2];
        }
        for (size_t i = 0; i < normals.size(); i += 3)
        {
            const float len = std::sqrt(normals[i] * normals[i] + normals[i + 1] * normals[i + 1] + normals[i + 2] * normals[i + 2]);
            if (len > 0.0f) for (int a2 = 0; a2 < 3; ++a2) normals[i + a2] /= len;
        }
        const float pivot[3] = { part.pivot[0] * frontSign * k, part.pivot[1] * k, part.pivot[2] * frontSign * k };
        std::fwrite(pivot, 4, 3, f);
        WriteU32(f, part.kind);
        WriteU32(f, (unsigned int)(positions.size() / 3));
        WriteU32(f, (unsigned int)out.size());
        std::fwrite(positions.data(), 4, positions.size(), f);
        std::fwrite(normals.data(), 4, normals.size(), f);
        std::fwrite(texcoords.data(), 4, texcoords.size(), f);
        std::fwrite(out.data(), 2, out.size(), f);
        totalTriangles += out.size() / 3;
        std::printf("part %zu kind %u: %zu -> %zu triangles, %zu vertices, error %.4f, pivot (%.2f %.2f %.2f) m\n", pi, part.kind,
                    part.triangles.size() / 3, out.size() / 3, positions.size() / 3, error, pivot[0], pivot[1], pivot[2]);
    }
    std::fclose(f);
    std::printf("total %zu triangles, scale %.3f (length %.2f m)\n", totalTriangles, k, length);
    return 0;
}
