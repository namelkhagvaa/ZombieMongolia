#pragma once

#include "raylib.h"

#include <string>
#include <vector>

// Mixamo-гийн хөдөлгөөнүүд (tools/mixamo_import.py -> assets/<name>_anims.bin).
// Кадр бүрд яс бүрийн эргэлт нь "анхны (T) байрлалаас хэр эргэсэн"-ийг эцгийнхтэй харьцангуйгаар (загварын
// орон зайд) хадгална: D(яс) = D(эцэг) * L. Ташааны байрлал нь загварын нэгжээр, байрнаасаа явах шилжилтгүй.
struct AnimClip
{
    std::string name;
    int frames = 0;
    float fps = 30.0f;
    bool loop = true;
    Vector3 velocity = { 0.0f, 0.0f, 0.0f }; // хасагдсан шилжилтийн хурд (загварын нэгж/с): алхааны урт
    float phase = 0.0f;                      // баруун хөл явах зүг рүүгээ хамгийн урд гарсан агшин (0..1)
    std::vector<Vector3> hips;               // [кадр]
    std::vector<Quaternion> rotations;       // [кадр * ясны тоо]

    float Duration() const { return (float)(frames - 1) / fps; }
};

struct AnimSet
{
    std::vector<std::string> bones; // Mixamo нэр, "mixamorig:" угтваргүй
    std::vector<Vector3> rest;      // эх дүрийн ясны анхны байрлал: өөр дүрд тааруулахад (CharacterModel.cpp SetupClips)
    std::vector<AnimClip> clips;

    int Find(const char* name) const;
};

bool LoadAnimSet(const char* path, AnimSet& out);

// Хэд хэдэн клипийг жинлэн хольж ясны эргэлт, ташааны байрлалыг гаргана (AnimSet-ийн ясны дарааллаар).
class AnimBlend
{
public:
    void Reset(int boneCount);
    // time: клипийн секунд (давтагдах клипт үлдэгдлээр), weight > 0
    void Add(const AnimClip& clip, float time, float weight);
    // Хуримтлагдсан жин > 0 бол хэвийн болгож буцаана
    bool Result(std::vector<Quaternion>& rotations, Vector3& hips) const;
    float Weight() const { return total; }

private:
    std::vector<Quaternion> sum;
    Vector3 hipSum = { 0.0f, 0.0f, 0.0f };
    float total = 0.0f;
};
