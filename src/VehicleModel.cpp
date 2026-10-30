#include "VehicleModel.h"

#include "Platform.h"
#include "UiStyle.h"

#include "raymath.h"
#include "rlgl.h"

#include <cstring>
#include <string>
#include <vector>

namespace
{
    struct Part
    {
        Mesh mesh = {};
        Vector3 pivot = {};
        unsigned int kind = 0; // 0 их бие, 1 урд дугуй, 2 хойд дугуй, 3 үндсэн сэнс, 4 сүүлний сэнс
    };

    struct Model3
    {
        std::vector<Part> parts;
        Texture2D texture = {};
        Material material = {};
        float wheelRadius = 0.42f;
        bool loaded = false;
    };

    Model3 models[(int)VehicleModelId::Count];

    // Улсын дугаарын хавтан: Монгол дугаар (520 x 112 мм). Машин бүрийн текстурыг нэг удаа UI фонтоор (кирилл) зурж,
    // машины shader-ээр (гэрэл, манан) урд, хойд бампер дээр зурна. Загварын өөрийн (гадаад) дугаарыг хар хүрээгээр хучина.
    struct PlateSpot
    {
        Vector3 center; // загварын орон зай, м (урд нь +Z руу, хойд нь -Z руу харна)
        float tilt;     // градус: эерэг бол дээд ирмэг нь гадагш (урд талд урагш, хойд талд хойш) хазайна
        float scale;    // хавтангийн хэмжээ (1 = 52 см)
        Vector2 holder; // хуучин дугаарыг хучих хар хүрээ (өргөн, өндөр, м; 0 = байхгүй)
    };
    struct PlateCar
    {
        VehicleModelId model;
        const char* number;  // "03 10"
        const char* letters; // "УБО"
        PlateSpot front, rear;
    };
    const PlateCar kPlateCars[] = {
        { VehicleModelId::Sedan, "03 10", "УБО", { { 0.0f, 0.45f, 2.442f }, -5.0f, 1.0f, {} },
          { { 0.0f, 0.885f, -2.362f }, -10.0f, 1.0f, {} } },
        { VehicleModelId::Suv, "72 30", "УНҮ", { { 0.0f, 0.60f, 2.352f }, 0.0f, 1.0f, { 0.57f, 0.18f } },
          { { 0.0f, 1.083f, -2.245f }, -11.0f, 1.0f, { 0.57f, 0.18f } } },
        { VehicleModelId::Hatchback, "00 00", "УНА", { { 0.0f, 0.553f, 1.977f }, 0.0f, 1.0f, { 0.59f, 0.155f } },
          { { 0.0f, 0.97f, -1.876f }, -20.0f, 0.85f, { 0.46f, 0.155f } } },
        { VehicleModelId::Compact, "58 64", "УКМ", { { 0.0f, 0.48f, 2.024f }, -3.0f, 1.0f, {} },
          { { 0.0f, 0.87f, -1.952f }, 0.0f, 1.0f, {} } },
    };
    constexpr int kPlateCarCount = (int)(sizeof(kPlateCars) / sizeof(kPlateCars[0]));

    struct Plates
    {
        RenderTexture2D target[kPlateCarCount] = {};
        Material material[kPlateCarCount] = {};
        Material holder = {};
        Mesh mesh = {};
        bool loaded = false;
    };
    Plates plates;

    // Монгол дугаар: цайвар, нимгэн хар хүрээ, "03 10 (MGL) УБО" бүдүүн хар тэмдэгт. Тэмдэгтийн хэмжээг хавтангийн
    // өндрөөр сонгож, текстурын өргөнийг тэдгээрт тааруулна: хавтан дээр (52 см) шахагдаж жинхэнэ дугаар шиг өндөр,
    // нарийн харагдана. Бүдүүн зураас: тэмдэгтийг хажуу тийш шилжүүлж хэд давхарлана.
    RenderTexture2D DrawPlateTexture(const char* number, const char* letters)
    {
        const int height = 112, size = 168, margin = 16, gap = 14, ovalWidth = 104;
        const int numberWidth = Ui::MeasureBold(number, size);
        const int lettersWidth = Ui::MeasureBold(letters, size);
        const int width = margin + numberWidth + gap + ovalWidth + gap + lettersWidth + margin;
        RenderTexture2D target = LoadRenderTexture(width, height);
        if (target.id == 0) return target;
        const Color ink = { 14, 14, 16, 255 }, paper = { 238, 239, 236, 255 };
        BeginTextureMode(target);
        ClearBackground(ink);
        DrawRectangle(5, 4, width - 10, height - 8, paper);
        const int top = (int)((float)height * 0.5f - (float)size * 0.585f); // цифрийн дунд хавтангийн дунд
        for (int dx : { -4, -2, 0, 2, 4 })
        {
            Ui::TextBold(number, margin + dx, top, size, ink);
            Ui::TextBold(letters, width - margin - lettersWidth + dx, top, size, ink);
        }
        // MGL зууван: дугаар, үсгийн хооронд
        const float ovalX = (float)(margin + numberWidth + gap) + (float)ovalWidth * 0.5f, ovalY = (float)height * 0.5f;
        DrawEllipse((int)ovalX, (int)ovalY, (float)ovalWidth * 0.5f, 22.0f, ink);
        DrawEllipse((int)ovalX, (int)ovalY, (float)ovalWidth * 0.5f - 5.0f, 17.0f, paper);
        for (int dx : { -1, 0, 1 }) Ui::TextBold("MGL", (int)ovalX - Ui::MeasureBold("MGL", 30) / 2 + dx, (int)ovalY - 19, 30, ink);
        EndTextureMode();
        GenTextureMipmaps(&target.texture);
        SetTextureFilter(target.texture, TEXTURE_FILTER_TRILINEAR);
        return target;
    }

    void LoadPlates(Shader shader)
    {
        if (plates.loaded) return;
        for (int i = 0; i < kPlateCarCount; ++i)
        {
            plates.target[i] = DrawPlateTexture(kPlateCars[i].number, kPlateCars[i].letters);
            plates.material[i] = LoadMaterialDefault();
            plates.material[i].shader = shader;
            plates.material[i].maps[MATERIAL_MAP_DIFFUSE].texture = plates.target[i].texture;
        }
        plates.holder = LoadMaterialDefault(); // (цагаан текстур: өнгийг нь зурахдаа өгнө)
        plates.holder.shader = shader;

        // Хавтан: XY хавтгайд 1 x 1 м, нүүр +Z (RenderTexture-ийн дээд мөр нь v = 1); зурахдаа хэмжээгээр масштаблана
        Mesh& mesh = plates.mesh;
        mesh.vertexCount = 4;
        mesh.triangleCount = 2;
        mesh.vertices = (float*)MemAlloc(4 * 3 * sizeof(float));
        mesh.normals = (float*)MemAlloc(4 * 3 * sizeof(float));
        mesh.texcoords = (float*)MemAlloc(4 * 2 * sizeof(float));
        mesh.indices = (unsigned short*)MemAlloc(6 * sizeof(unsigned short));
        const float corners[4][4] = { { -0.5f, -0.5f, 0.0f, 0.0f }, { 0.5f, -0.5f, 1.0f, 0.0f }, { 0.5f, 0.5f, 1.0f, 1.0f }, { -0.5f, 0.5f, 0.0f, 1.0f } };
        for (int i = 0; i < 4; ++i)
        {
            mesh.vertices[i * 3 + 0] = corners[i][0];
            mesh.vertices[i * 3 + 1] = corners[i][1];
            mesh.vertices[i * 3 + 2] = 0.0f;
            mesh.normals[i * 3 + 0] = 0.0f;
            mesh.normals[i * 3 + 1] = 0.0f;
            mesh.normals[i * 3 + 2] = 1.0f;
            mesh.texcoords[i * 2 + 0] = corners[i][2];
            mesh.texcoords[i * 2 + 1] = corners[i][3];
        }
        const unsigned short order[6] = { 0, 1, 2, 0, 2, 3 };
        std::memcpy(mesh.indices, order, sizeof(order));
        UploadMesh(&mesh, false);
        plates.loaded = true;
    }

    void UnloadPlates()
    {
        if (!plates.loaded) return;
        UnloadMesh(plates.mesh);
        for (int i = 0; i < kPlateCarCount; ++i)
        {
            UnloadRenderTexture(plates.target[i]);
            MemFree(plates.material[i].maps); // (shader нь унааны загварынх: энд устгахгүй)
        }
        MemFree(plates.holder.maps);
        plates = Plates();
    }

    void DrawPlates(VehicleModelId id, Color tint)
    {
        if (!plates.loaded) return;
        for (int i = 0; i < kPlateCarCount; ++i)
        {
            if (kPlateCars[i].model != id) continue;
            plates.material[i].maps[MATERIAL_MAP_DIFFUSE].color = tint;
            plates.holder.maps[MATERIAL_MAP_DIFFUSE].color = { (unsigned char)(tint.r / 10), (unsigned char)(tint.g / 10),
                                                               (unsigned char)(tint.b / 10), 255 };
            for (int back = 0; back < 2; ++back)
            {
                const PlateSpot& spot = back ? kPlateCars[i].rear : kPlateCars[i].front;
                Matrix place = MatrixRotateX(spot.tilt * DEG2RAD);
                if (back) place = MatrixMultiply(place, MatrixRotateY(PI));
                place = MatrixMultiply(place, MatrixTranslate(spot.center.x, spot.center.y, spot.center.z));
                if (spot.holder.x > 0.0f)
                {
                    // Хар хүрээ хавтангаас 3 мм ард (хавтантай давхцаж анивчихгүй)
                    const Matrix holder = MatrixMultiply(MatrixMultiply(MatrixScale(spot.holder.x, spot.holder.y, 1.0f),
                                                                        MatrixTranslate(0.0f, 0.0f, -0.003f)), place);
                    DrawMesh(plates.mesh, plates.holder, holder);
                }
                DrawMesh(plates.mesh, plates.material[i], MatrixMultiply(MatrixScale(0.52f * spot.scale, 0.112f * spot.scale, 1.0f), place));
            }
            return;
        }
    }
    const char* kNames[(int)VehicleModelId::Count] = { "gclass", "truck", "xtrail", "yaris", "moto", "trike", "heli", "dropheli", "armored", "crown", "aqua" };

    Model3 props[(int)PropModelId::Count];
    const char* kPropNames[(int)PropModelId::Count] = { "ak47", "sum", "sniper", "pistol", "shotgun", "knife" };
    const Material* propDepth = nullptr;

    Model3 scenery[(int)SceneryModelId::Count];
    const char* kSceneryNames[(int)SceneryModelId::Count][2] = {
        { "trees", "larch" }, { "trees", "birch" }, { "trees", "elm" }, { "trees", "saxaul" },
        { "pooshig", "barrel_old" }, { "pooshig", "barrel_fuel" },
    };

    // Файлын байрлал: компьютер дээр exe-ийн хажуу эсвэл төслийн assets/<sub>, утсан дээр APK-ийн assets/<sub>
    std::string AssetFolder(const std::string& sub)
    {
#if defined(__ANDROID__)
        return sub + "/";
#else
        const std::string application = GetApplicationDirectory();
        for (const std::string& folder : { "assets/" + sub + "/", application + "assets/" + sub + "/", application + "../assets/" + sub + "/" })
        {
            if (DirectoryExists(folder.c_str())) return folder;
        }
        return "assets/" + sub + "/";
#endif
    }

    void UnloadOne(Model3& model)
    {
        if (!model.loaded) return;
        for (Part& part : model.parts) UnloadMesh(part.mesh);
        model.parts.clear();
        if (model.texture.id != 0) UnloadTexture(model.texture);
        RL_FREE(model.material.maps);
        model = {};
    }

    template <typename T>
    bool Take(const unsigned char*& cursor, const unsigned char* end, T* out, size_t count)
    {
        const size_t bytes = sizeof(T) * count;
        if ((size_t)(end - cursor) < bytes) return false;
        std::memcpy(out, cursor, bytes);
        cursor += bytes;
        return true;
    }

    bool LoadOne(Model3& model, const std::string& meshPath, const std::string& texturePath, Shader shader)
    {
        int size = 0;
        unsigned char* data = LoadFileData(meshPath.c_str(), &size);
        if (!data) return false;
        const unsigned char* cursor = data;
        const unsigned char* end = data + size;
        char magic[4];
        unsigned int version = 0, partCount = 0;
        bool ok = Take(cursor, end, magic, 4) && std::memcmp(magic, "VMSH", 4) == 0 && Take(cursor, end, &version, 1) &&
                  version == 1 && Take(cursor, end, &partCount, 1);
        for (unsigned int p = 0; ok && p < partCount; ++p)
        {
            Part part;
            unsigned int vertexCount = 0, indexCount = 0;
            ok = Take(cursor, end, &part.pivot.x, 3) && Take(cursor, end, &part.kind, 1) && Take(cursor, end, &vertexCount, 1) &&
                 Take(cursor, end, &indexCount, 1);
            if (!ok) break;
            Mesh& mesh = part.mesh;
            mesh.vertexCount = (int)vertexCount;
            mesh.triangleCount = (int)(indexCount / 3);
            mesh.vertices = (float*)MemAlloc(vertexCount * 3 * sizeof(float));
            mesh.normals = (float*)MemAlloc(vertexCount * 3 * sizeof(float));
            mesh.texcoords = (float*)MemAlloc(vertexCount * 2 * sizeof(float));
            mesh.indices = (unsigned short*)MemAlloc(indexCount * sizeof(unsigned short));
            ok = Take(cursor, end, mesh.vertices, vertexCount * 3) && Take(cursor, end, mesh.normals, vertexCount * 3) &&
                 Take(cursor, end, mesh.texcoords, vertexCount * 2) && Take(cursor, end, mesh.indices, indexCount);
            if (!ok)
            {
                UnloadMesh(mesh);
                break;
            }
            UploadMesh(&mesh, false);
            if (part.kind == 1 || part.kind == 2) model.wheelRadius = part.pivot.y;
            model.parts.push_back(part);
        }
        UnloadFileData(data);
        if (!ok || model.parts.empty())
        {
            for (Part& part : model.parts) UnloadMesh(part.mesh);
            model.parts.clear();
            TraceLog(LOG_WARNING, "VEHICLE: %s is not a valid mesh", meshPath.c_str());
            return false;
        }

        model.texture = LoadTexture(texturePath.c_str());
        if (model.texture.id != 0)
        {
            GenTextureMipmaps(&model.texture);
            SetTextureFilter(model.texture, TEXTURE_FILTER_TRILINEAR);
        }
        model.material = LoadMaterialDefault();
        model.material.shader = shader;
        if (model.texture.id != 0) model.material.maps[MATERIAL_MAP_DIFFUSE].texture = model.texture;
        model.loaded = true;
        int triangles = 0;
        for (const Part& part : model.parts) triangles += part.mesh.triangleCount;
        TraceLog(LOG_INFO, "VEHICLE: %s loaded, %d parts, %d triangles", meshPath.c_str(), (int)model.parts.size(), triangles);
        return true;
    }
}

void LoadVehicleModels(Shader shader)
{
    const std::string folder = AssetFolder("cars");
    for (int i = 0; i < (int)VehicleModelId::Count; ++i)
    {
        const std::string mesh = folder + kNames[i] + ".vmesh";
        const std::string texture = ImageFile(folder + kNames[i] + "_color.png"); // (хөтөч: .jpg)
#if !defined(__ANDROID__)
        if (!FileExists(mesh.c_str())) continue;
#endif
        LoadOne(models[i], mesh, texture, shader);
    }
    LoadPlates(shader);
}

void UnloadVehicleModels()
{
    for (Model3& model : models) UnloadOne(model);
    UnloadPlates();
}

bool HasVehicleModel(VehicleModelId id)
{
    return models[(int)id].loaded;
}

float VehicleModelWheelRadius(VehicleModelId id)
{
    return models[(int)id].wheelRadius;
}

Vector3 VehicleModelPivot(VehicleModelId id, unsigned int kind)
{
    for (const Part& part : models[(int)id].parts)
    {
        if (part.kind == kind) return part.pivot;
    }
    return { 0.0f, 0.0f, 0.0f };
}

void DrawVehicleModel(VehicleModelId id, float distance, float steer, const Material* depth, Color tint, float rotor, float pitch)
{
    Model3& model = models[(int)id];
    if (!model.loaded) return;
    const Material& material = depth ? *depth : model.material;
    if (!depth) model.material.maps[MATERIAL_MAP_DIFFUSE].color = tint;
    const float spin = distance / model.wheelRadius;
    for (const Part& part : model.parts)
    {
        if (part.kind == 0)
        {
            DrawMesh(part.mesh, material, MatrixIdentity());
            if (!depth) DrawPlates(id, tint);
            continue;
        }
        rlPushMatrix();
        rlTranslatef(part.pivot.x, part.pivot.y, part.pivot.z);
        if (part.kind == 3)
        {
            rlRotatef(rotor * RAD2DEG, 0.0f, 1.0f, 0.0f);   // үндсэн сэнс, пулемёт: босоо тэнхлэг
            if (pitch != 0.0f) rlRotatef(-pitch * RAD2DEG, 1.0f, 0.0f, 0.0f); // пулемётын хошуу дээш / доош
        }
        else if (part.kind == 4) rlRotatef(rotor * 5.0f * RAD2DEG, 1.0f, 0.0f, 0.0f); // сүүлний сэнс: хажуугийн тэнхлэг
        else
        {
            if (part.kind == 1) rlRotatef(-steer * 30.0f, 0.0f, 1.0f, 0.0f); // урд дугуйнууд жолоогоор эргэнэ
            rlRotatef(spin * RAD2DEG, 1.0f, 0.0f, 0.0f);
        }
        DrawMesh(part.mesh, material, MatrixIdentity());
        rlPopMatrix();
    }
}

void LoadSceneryModels(Shader shader)
{
    for (int i = 0; i < (int)SceneryModelId::Count; ++i)
    {
        const std::string folder = AssetFolder(kSceneryNames[i][0]);
        const std::string mesh = folder + kSceneryNames[i][1] + ".vmesh";
        const std::string texture = ImageFile(folder + kSceneryNames[i][1] + "_color.png");
#if !defined(__ANDROID__)
        if (!FileExists(mesh.c_str())) continue;
#endif
        LoadOne(scenery[i], mesh, texture, shader);
    }
}

void UnloadSceneryModels()
{
    for (Model3& model : scenery) UnloadOne(model);
}

bool HasSceneryModel(SceneryModelId id)
{
    return scenery[(int)id].loaded;
}

void DrawSceneryModel(SceneryModelId id, const Material* depth)
{
    Model3& model = scenery[(int)id];
    if (!model.loaded) return;
    const Material& material = depth ? *depth : model.material;
    for (const Part& part : model.parts) DrawMesh(part.mesh, material, MatrixIdentity());
}

void LoadPropModels(Shader shader)
{
    const std::string folder = AssetFolder("buu");
    for (int i = 0; i < (int)PropModelId::Count; ++i)
    {
        const std::string mesh = folder + kPropNames[i] + ".vmesh";
        const std::string texture = ImageFile(folder + kPropNames[i] + "_color.png");
#if !defined(__ANDROID__)
        if (!FileExists(mesh.c_str())) continue;
#endif
        LoadOne(props[i], mesh, texture, shader);
    }
}

void UnloadPropModels()
{
    for (Model3& model : props) UnloadOne(model);
}

bool HasPropModel(PropModelId id)
{
    return props[(int)id].loaded;
}

void SetPropDepthMaterial(const Material* depth)
{
    propDepth = depth;
}

void DrawPropModel(PropModelId id, Color tint)
{
    Model3& model = props[(int)id];
    if (!model.loaded) return;
    if (!propDepth) model.material.maps[MATERIAL_MAP_DIFFUSE].color = tint;
    const Material& material = propDepth ? *propDepth : model.material;
    for (const Part& part : model.parts) DrawMesh(part.mesh, material, MatrixIdentity());
}
