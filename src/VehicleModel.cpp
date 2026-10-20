#include "VehicleModel.h"

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
        unsigned int kind = 0; // 0 их бие, 1 урд дугуй, 2 хойд дугуй
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
    const char* kNames[(int)VehicleModelId::Count] = { "gclass", "truck", "xtrail", "yaris" };

    Model3 props[(int)PropModelId::Count];
    const char* kPropNames[(int)PropModelId::Count] = { "ak47", "sum", "sniper", "pistol", "shotgun", "knife" };
    const Material* propDepth = nullptr;

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
            if (part.kind != 0) model.wheelRadius = part.pivot.y;
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
        const std::string texture = folder + kNames[i] + "_color.png";
#if !defined(__ANDROID__)
        if (!FileExists(mesh.c_str())) continue;
#endif
        LoadOne(models[i], mesh, texture, shader);
    }
}

void UnloadVehicleModels()
{
    for (Model3& model : models) UnloadOne(model);
}

bool HasVehicleModel(VehicleModelId id)
{
    return models[(int)id].loaded;
}

float VehicleModelWheelRadius(VehicleModelId id)
{
    return models[(int)id].wheelRadius;
}

void DrawVehicleModel(VehicleModelId id, float distance, float steer, const Material* depth, Color tint)
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
            continue;
        }
        rlPushMatrix();
        rlTranslatef(part.pivot.x, part.pivot.y, part.pivot.z);
        if (part.kind == 1) rlRotatef(-steer * 30.0f, 0.0f, 1.0f, 0.0f); // урд дугуйнууд жолоогоор эргэнэ
        rlRotatef(spin * RAD2DEG, 1.0f, 0.0f, 0.0f);
        DrawMesh(part.mesh, material, MatrixIdentity());
        rlPopMatrix();
    }
}

void LoadPropModels(Shader shader)
{
    const std::string folder = AssetFolder("buu");
    for (int i = 0; i < (int)PropModelId::Count; ++i)
    {
        const std::string mesh = folder + kPropNames[i] + ".vmesh";
        const std::string texture = folder + kPropNames[i] + "_color.png";
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
