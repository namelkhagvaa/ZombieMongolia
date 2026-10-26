#pragma once

#include <string>

// Тоглолтын техникийн лог (алдаа оношлох): TraceLog-ийн чухал мөрүүдийг (тулаан, хамтын тоглолт, сүлжээ,
// анхааруулга, алдаа) серверт (backend/game/gamelog.py, api/logs/) багцаар илгээнэ. Давталтын алхам бүр
// "STEP: <нэр> ..." мөр (host_start, guest_joined, guest_arrived, battle_start, result, server_result, left_to_map,
// winner_continues, stage_complete): серверт өрөө, тулаанаар хост, зочин хоёрын мөрийг нэгтгэж уншина.
// Илгээгээгүй мөрүүд төхөөрөмж дээр хадгалагдана (тоглоом унасан ч дараагийн ажиллуулалтад илгээнэ).
namespace GameLog
{
    // Тоглоом эхлэхэд (цонх нээгдсэний дараа): өмнөх ажиллуулалтын илгээгээгүй мөрүүдийг ачаална (өмнөх удаа хэвийн
    // бус хаагдсан бол тэмдэглэнэ). enabled = false (туршилтын горим, данс идэвхгүй): юу ч барихгүй, хадгалахгүй.
    void Init(bool enabled);
    // Хэвийн хаагдахад (main-ийн төгсгөл)
    void Shutdown();
    // TraceLog бүрээс (main.cpp-ийн callback): шүүж, өрөөний мэдээлэлтэй хадгална. Аль ч thread-ээс.
    void Capture(int level, const char* text);
    // Account: одоогийн өрөө, тулаан, үүрэг ('h' хост, 'g' зочин; room < 0 бол өрөөгүй)
    void SetRoom(int room, int battle, char role);

    // Account::Tick: илгээх цаг болсон (30 секунд тутам, эсвэл алхам, алдаа гарсан даруйд)
    bool Ready(double now);
    // Илгээх багц (JSON): хүлээгдэж буй эхний count мөр
    std::string TakeBatch(int& count);
    void Sent(int count);   // хүлээн авсан (эсвэл сервер татгалзсан): эдгээр мөрийг устгана
    void Failed(double now); // сүлжээ: дараа дахин
    // Account::Tick: өөрчлөгдсөн бол төхөөрөмж дээр хадгална (2 секундэд нэгээс олонгүй)
    void Persist(double now);
}
