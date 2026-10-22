#include "Online.h"

#include <cstdint>
#include <cstdio>
#include <random>

namespace
{
    // Төхөөрөмжийн танигчийг шууд илгээхгүй: FNV-1a 64-ээр хоёр удаа (өөр давс) хэш хийж 32 hex тэмдэгт болгоно.
    std::string HashId(const std::string& raw)
    {
        std::string out;
        for (uint64_t seed : { 0xcbf29ce484222325ull, 0x84222325cbf29ce4ull })
        {
            uint64_t h = seed;
            for (const unsigned char c : "zombie-mongolia:" + raw)
            {
                h ^= c;
                h *= 0x100000001b3ull;
            }
            char buffer[17];
            std::snprintf(buffer, sizeof(buffer), "%016llx", (unsigned long long)h);
            out += buffer;
        }
        return out;
    }

    std::string RandomId()
    {
        std::random_device device;
        std::string raw;
        for (int i = 0; i < 4; ++i) raw += std::to_string(device());
        return HashId(raw);
    }
}

#if defined(__EMSCRIPTEN__)
// ================================================================ Хөтөч
#include <emscripten.h>

#include <vector>

namespace
{
    // (EM_JS дотор урвуу налуу зураас "\" алга болдог тул regex-д хэрэглэхгүй)
    EM_JS(void, JsSend, (int id, const char* method, const char* url, const char* body, const char* token), {
        var store = Module.zmHttp || (Module.zmHttp = {});
        var headers = { 'Content-Type': 'application/json' };
        var t = UTF8ToString(token);
        if (t) headers['Authorization'] = 'Bearer ' + t;
        var m = UTF8ToString(method);
        fetch(UTF8ToString(url), { method: m, headers: headers, body: m === 'GET' ? undefined : UTF8ToString(body) })
            .then(function(r) { return r.text().then(function(text) { store[id] = { status: r.status, body: text }; }); })
            .catch(function() { store[id] = { status: 0, body: "" }; });
    });
    EM_JS(int, JsState, (int id), {
        var s = Module.zmHttp && Module.zmHttp[id];
        return s ? s.status : -1;
    });
    EM_JS(int, JsBodySize, (int id), { return lengthBytesUTF8(Module.zmHttp[id].body) + 1; });
    EM_JS(void, JsTake, (int id, char* out, int size), {
        stringToUTF8(Module.zmHttp[id].body, out, size);
        delete Module.zmHttp[id];
    });
    EM_JS(void, JsOpen, (const char* url), {
        var u = UTF8ToString(url);
        if (u.indexOf('http') === 0) window.open(u, '_blank');
        else window.location.href = u;
    });
    EM_JS(void, JsLoad, (const char* key, char* out, int size), {
        var v = "";
        try { v = localStorage.getItem(UTF8ToString(key)) || ""; } catch (e) {}
        stringToUTF8(v, out, size);
    });
    EM_JS(void, JsStore, (const char* key, const char* value), {
        try {
            var v = UTF8ToString(value);
            if (v) localStorage.setItem(UTF8ToString(key), v);
            else localStorage.removeItem(UTF8ToString(key));
        } catch (e) {}
    });
    EM_JS(void, JsApiBase, (char* out, int size), {
        var param = new URLSearchParams(location.search).get('api');
        var local = location.hostname === 'localhost' || location.hostname === '127.0.0.1';
        stringToUTF8(param || (local ? 'http://localhost:8010/api/' : "https://zombie-api.apps.mn/api/"), out, size);
    });

    int nextId = 1;
}

int Online::Send(const char* method, const std::string& url, const std::string& body, const std::string& token)
{
    const int id = nextId++;
    JsSend(id, method, url.c_str(), body.c_str(), token.c_str());
    return id;
}

bool Online::Poll(int id, int& status, std::string& body)
{
    status = JsState(id);
    if (status < 0) return false;
    std::vector<char> buffer((size_t)JsBodySize(id));
    JsTake(id, buffer.data(), (int)buffer.size());
    body = buffer.data();
    return true;
}

void Online::OpenUrl(const std::string& url) { JsOpen(url.c_str()); }

std::string Online::Load(const char* key)
{
    char buffer[512] = {};
    JsLoad(key, buffer, (int)sizeof(buffer));
    return buffer;
}

void Online::Store(const char* key, const std::string& value) { JsStore(key, value.c_str()); }

std::string Online::DeviceId()
{
    std::string id = Load("zm_device"); // хуучин вэб хувилбарын хадгалсан ID-тай нийцнэ
    if (id.size() < 8)
    {
        id = "web-" + RandomId();
        Store("zm_device", id);
    }
    return id;
}

std::string Online::DefaultApiBase()
{
    char buffer[256] = {};
    JsApiBase(buffer, (int)sizeof(buffer));
    return buffer;
}

#else
// ================================================================ Windows, Android: тусдаа урсгалд хүсэлт

#include <atomic>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace
{
    struct Request
    {
        std::atomic<bool> done{ false };
        int status = 0;
        std::string body;
    };

    std::mutex requestsLock;
    std::map<int, std::shared_ptr<Request>> requests;
    int nextId = 1;

    void Run(std::shared_ptr<Request> request, std::string method, std::string url, std::string body, std::string token);

    std::string StoragePath(const char* key);
}

int Online::Send(const char* method, const std::string& url, const std::string& body, const std::string& token)
{
    auto request = std::make_shared<Request>();
    int id;
    {
        std::lock_guard<std::mutex> guard(requestsLock);
        id = nextId++;
        requests[id] = request;
    }
    std::thread(Run, request, std::string(method), url, body, token).detach();
    return id;
}

bool Online::Poll(int id, int& status, std::string& body)
{
    std::shared_ptr<Request> request;
    {
        std::lock_guard<std::mutex> guard(requestsLock);
        auto it = requests.find(id);
        if (it == requests.end())
        {
            status = 0;
            return true;
        }
        request = it->second;
        if (!request->done) return false;
        requests.erase(it);
    }
    status = request->status;
    body = std::move(request->body);
    return true;
}

std::string Online::Load(const char* key)
{
    std::ifstream file(StoragePath(key));
    std::string value;
    std::getline(file, value);
    return value;
}

void Online::Store(const char* key, const std::string& value)
{
    const std::string path = StoragePath(key);
    if (value.empty())
    {
        std::remove(path.c_str());
        return;
    }
    std::ofstream file(path, std::ios::trunc);
    file << value << '\n';
}

std::string Online::DefaultApiBase() { return "https://zombie-api.apps.mn/api/"; }

#if defined(_WIN32)
// ---------------------------------------------------------------- Windows (WinHTTP)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>

#include <cstdlib>

namespace
{
    std::wstring Wide(const std::string& text)
    {
        const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
        std::wstring wide((size_t)(size > 0 ? size : 1), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &wide[0], size);
        wide.resize(wide.size() - 1);
        return wide;
    }

    void Run(std::shared_ptr<Request> request, std::string method, std::string url, std::string body, std::string token)
    {
        const std::wstring wideUrl = Wide(url);
        wchar_t host[256] = {}, path[2048] = {}, extra[1024] = {};
        URL_COMPONENTS parts = {};
        parts.dwStructSize = sizeof(parts);
        parts.lpszHostName = host;
        parts.dwHostNameLength = 256;
        parts.lpszUrlPath = path;
        parts.dwUrlPathLength = 2048;
        parts.lpszExtraInfo = extra;
        parts.dwExtraInfoLength = 1024;
        if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &parts))
        {
            request->done = true;
            return;
        }
        const std::wstring target = std::wstring(path) + extra;

        HINTERNET session = WinHttpOpen(L"ZombieMongolia/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                        WINHTTP_NO_PROXY_BYPASS, 0);
        HINTERNET connection = session ? WinHttpConnect(session, host, parts.nPort, 0) : nullptr;
        HINTERNET handle = connection ? WinHttpOpenRequest(connection, Wide(method).c_str(), target.c_str(), nullptr,
                                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                           parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                                      : nullptr;
        if (session) WinHttpSetTimeouts(session, 10000, 10000, 20000, 20000);
        std::wstring headers = L"Content-Type: application/json\r\n";
        if (!token.empty()) headers += L"Authorization: Bearer " + Wide(token) + L"\r\n";
        const DWORD length = (DWORD)body.size();
        if (handle &&
            WinHttpSendRequest(handle, headers.c_str(), (DWORD)-1L, length ? (LPVOID)body.data() : WINHTTP_NO_REQUEST_DATA,
                               length, length, 0) &&
            WinHttpReceiveResponse(handle, nullptr))
        {
            DWORD code = 0, size = sizeof(code);
            WinHttpQueryHeaders(handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                                &code, &size, WINHTTP_NO_HEADER_INDEX);
            std::string received;
            DWORD available = 0;
            while (WinHttpQueryDataAvailable(handle, &available) && available > 0)
            {
                std::string chunk(available, '\0');
                DWORD read = 0;
                if (!WinHttpReadData(handle, &chunk[0], available, &read) || read == 0) break;
                received.append(chunk.data(), read);
            }
            request->body = std::move(received);
            request->status = (int)code;
        }
        if (handle) WinHttpCloseHandle(handle);
        if (connection) WinHttpCloseHandle(connection);
        if (session) WinHttpCloseHandle(session);
        request->done = true;
    }

    std::string StoragePath(const char* key)
    {
        const char* local = std::getenv("LOCALAPPDATA");
        std::string folder = local && *local ? std::string(local) + "\\ZombieMongolia" : std::string(".");
        CreateDirectoryW(Wide(folder).c_str(), nullptr);
        return folder + "\\" + key + ".txt";
    }
}

void Online::OpenUrl(const std::string& url)
{
    ShellExecuteW(nullptr, L"open", Wide(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::string Online::DeviceId()
{
    // Windows-ийн суулгацын тогтмол дугаар (дахин суулгахад л солигдоно)
    wchar_t guid[128] = {};
    DWORD size = sizeof(guid);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", L"MachineGuid",
                     RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, guid, &size) == ERROR_SUCCESS && guid[0])
    {
        std::string raw;
        for (const wchar_t* p = guid; *p; ++p) raw += (char)*p;
        return "win-" + HashId(raw);
    }
    std::string id = Load("device");
    if (id.size() < 8)
    {
        id = "win-" + RandomId();
        Store("device", id);
    }
    return id;
}

#elif defined(__ANDROID__)
// ---------------------------------------------------------------- Android (HttpURLConnection, JNI)
#include <android_native_app_glue.h>
#include <jni.h>

extern "C" struct android_app* GetAndroidApp(void); // raylib-ийн Android давхаргаас

namespace
{
    // Java-ийн алдаа (сүлжээгүй, хаяг буруу) гарсан эсэх: гарсан бол цэвэрлэнэ.
    bool Failed(JNIEnv* env)
    {
        if (!env->ExceptionCheck()) return false;
        env->ExceptionClear();
        return true;
    }

    void Run(std::shared_ptr<Request> request, std::string method, std::string url, std::string body, std::string token)
    {
        JavaVM* vm = GetAndroidApp()->activity->vm;
        JNIEnv* env = nullptr;
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK)
        {
            request->done = true;
            return;
        }
        env->PushLocalFrame(64);
        do
        {
            jclass urlClass = env->FindClass("java/net/URL");
            jobject address = env->NewObject(urlClass, env->GetMethodID(urlClass, "<init>", "(Ljava/lang/String;)V"),
                                             env->NewStringUTF(url.c_str()));
            if (Failed(env)) break;
            jobject connection = env->CallObjectMethod(address, env->GetMethodID(urlClass, "openConnection", "()Ljava/net/URLConnection;"));
            if (Failed(env)) break;
            jclass http = env->FindClass("java/net/HttpURLConnection");
            env->CallVoidMethod(connection, env->GetMethodID(http, "setRequestMethod", "(Ljava/lang/String;)V"),
                                env->NewStringUTF(method.c_str()));
            jmethodID property = env->GetMethodID(http, "setRequestProperty", "(Ljava/lang/String;Ljava/lang/String;)V");
            env->CallVoidMethod(connection, property, env->NewStringUTF("Content-Type"), env->NewStringUTF("application/json"));
            if (!token.empty())
            {
                env->CallVoidMethod(connection, property, env->NewStringUTF("Authorization"),
                                    env->NewStringUTF(("Bearer " + token).c_str()));
            }
            env->CallVoidMethod(connection, env->GetMethodID(http, "setConnectTimeout", "(I)V"), 10000);
            env->CallVoidMethod(connection, env->GetMethodID(http, "setReadTimeout", "(I)V"), 20000);
            if (Failed(env)) break;
            if (method != "GET")
            {
                env->CallVoidMethod(connection, env->GetMethodID(http, "setDoOutput", "(Z)V"), JNI_TRUE);
                jobject output = env->CallObjectMethod(connection, env->GetMethodID(http, "getOutputStream", "()Ljava/io/OutputStream;"));
                if (Failed(env)) break;
                jbyteArray bytes = env->NewByteArray((jsize)body.size());
                env->SetByteArrayRegion(bytes, 0, (jsize)body.size(), (const jbyte*)body.data());
                jclass stream = env->FindClass("java/io/OutputStream");
                env->CallVoidMethod(output, env->GetMethodID(stream, "write", "([B)V"), bytes);
                env->CallVoidMethod(output, env->GetMethodID(stream, "close", "()V"));
                if (Failed(env)) break;
            }
            const jint code = env->CallIntMethod(connection, env->GetMethodID(http, "getResponseCode", "()I"));
            if (Failed(env)) break;
            jobject input = env->CallObjectMethod(connection, env->GetMethodID(http, code >= 400 ? "getErrorStream" : "getInputStream",
                                                                                "()Ljava/io/InputStream;"));
            if (Failed(env)) input = nullptr;
            std::string received;
            if (input)
            {
                jclass stream = env->FindClass("java/io/InputStream");
                jmethodID read = env->GetMethodID(stream, "read", "([B)I");
                jbyteArray chunk = env->NewByteArray(8192);
                while (true)
                {
                    const jint count = env->CallIntMethod(input, read, chunk);
                    if (Failed(env) || count <= 0) break;
                    const size_t start = received.size();
                    received.resize(start + (size_t)count);
                    env->GetByteArrayRegion(chunk, 0, count, (jbyte*)&received[start]);
                }
                env->CallVoidMethod(input, env->GetMethodID(stream, "close", "()V"));
                Failed(env);
            }
            env->CallVoidMethod(connection, env->GetMethodID(http, "disconnect", "()V"));
            Failed(env);
            request->body = std::move(received);
            request->status = (int)code;
        } while (false);
        env->PopLocalFrame(nullptr);
        vm->DetachCurrentThread();
        request->done = true;
    }

    // Тоглоомын үндсэн урсгалын JNIEnv (нэг удаа холбоод хадгална)
    JNIEnv* MainEnv()
    {
        static JNIEnv* env = nullptr;
        if (!env) GetAndroidApp()->activity->vm->AttachCurrentThread(&env, nullptr);
        return env;
    }

    std::string StoragePath(const char* key)
    {
        return std::string(GetAndroidApp()->activity->internalDataPath) + "/" + key + ".txt";
    }
}

void Online::OpenUrl(const std::string& url)
{
    JNIEnv* env = MainEnv();
    env->PushLocalFrame(16);
    jobject activity = GetAndroidApp()->activity->clazz;
    jclass uriClass = env->FindClass("android/net/Uri");
    jobject uri = env->CallStaticObjectMethod(uriClass, env->GetStaticMethodID(uriClass, "parse", "(Ljava/lang/String;)Landroid/net/Uri;"),
                                              env->NewStringUTF(url.c_str()));
    jclass intentClass = env->FindClass("android/content/Intent");
    jobject intent = env->NewObject(intentClass, env->GetMethodID(intentClass, "<init>", "(Ljava/lang/String;Landroid/net/Uri;)V"),
                                    env->NewStringUTF("android.intent.action.VIEW"), uri);
    env->CallVoidMethod(activity, env->GetMethodID(env->GetObjectClass(activity), "startActivity", "(Landroid/content/Intent;)V"), intent);
    Failed(env); // банкны апп суугаагүй
    env->PopLocalFrame(nullptr);
}

std::string Online::DeviceId()
{
    JNIEnv* env = MainEnv();
    env->PushLocalFrame(16);
    std::string raw;
    jobject activity = GetAndroidApp()->activity->clazz;
    jobject resolver = env->CallObjectMethod(activity, env->GetMethodID(env->GetObjectClass(activity), "getContentResolver",
                                                                        "()Landroid/content/ContentResolver;"));
    if (!Failed(env))
    {
        jclass secure = env->FindClass("android/provider/Settings$Secure");
        jstring value = (jstring)env->CallStaticObjectMethod(
            secure, env->GetStaticMethodID(secure, "getString", "(Landroid/content/ContentResolver;Ljava/lang/String;)Ljava/lang/String;"),
            resolver, env->NewStringUTF("android_id"));
        if (!Failed(env) && value)
        {
            const char* chars = env->GetStringUTFChars(value, nullptr);
            raw = chars;
            env->ReleaseStringUTFChars(value, chars);
        }
    }
    env->PopLocalFrame(nullptr);
    if (!raw.empty()) return "and-" + HashId(raw);
    std::string id = Load("device");
    if (id.size() < 8)
    {
        id = "and-" + RandomId();
        Store("device", id);
    }
    return id;
}

#else
// ---------------------------------------------------------------- Бусад (Linux г.м.): сүлжээгүй
namespace
{
    void Run(std::shared_ptr<Request> request, std::string, std::string, std::string, std::string) { request->done = true; }
    std::string StoragePath(const char* key) { return std::string(key) + ".txt"; }
}

void Online::OpenUrl(const std::string&) {}

std::string Online::DeviceId()
{
    std::string id = Load("device");
    if (id.size() < 8)
    {
        id = "dev-" + RandomId();
        Store("device", id);
    }
    return id;
}
#endif

#endif
