#pragma once

#include <string>

// Сервертэй холбогдох (backend/: Django API) платформ бүрийн хэсэг:
//   хөтөч  — fetch (JavaScript), токен ба төхөөрөмжийн ID localStorage-д
//   Windows — WinHTTP (тусдаа урсгалд), хадгалалт %LOCALAPPDATA%\ZombieMongolia
//   Android — java.net.HttpURLConnection (JNI, тусдаа урсгалд), хадгалалт апп-ын дотоод санд
// Хүсэлт асинхрон: Send дугаар буцаана, Poll-оор frame бүрд шалгана (тоглоом гацахгүй).
namespace Online
{
    // method: "GET", "POST". token хоосон биш бол "Authorization: Bearer <token>".
    int Send(const char* method, const std::string& url, const std::string& body, const std::string& token);
    // Дууссан бол true: status (0 = сүлжээний алдаа), body. Дуусаагүй бол false.
    bool Poll(int id, int& status, std::string& body);

    // Банкны апп, SMS бичих апп, вэб хуудас нээнэ (bank://..., sms:144773?body=...).
    void OpenUrl(const std::string& url);

    // Энэ төхөөрөмжийн тогтмол ID (нэг төхөөрөмж = нэг данс): "web-", "win-", "and-" + 8..64 тэмдэгт.
    std::string DeviceId();

    // Жижиг утга хадгалах (нэвтрэх токен): хөтөч localStorage, бусад нь файл.
    std::string Load(const char* key);
    void Store(const char* key, const std::string& value); // хоосон бол устгана

    // API-ийн анхдагч хаяг: https://zombie-api.apps.mn/api/ (хөтөч дээр өөрийн компьютерээс нээвэл
    // http://localhost:8010/api/, ?api=... параметрээр солино).
    std::string DefaultApiBase();
}
