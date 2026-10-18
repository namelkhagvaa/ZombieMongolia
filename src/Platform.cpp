#include "Platform.h"

#include "raylib.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

namespace
{
    // Хөтчийн дараагийн кадр хүртэл үндсэн урсгалыг түр зогсооно (ASYNCIFY).
    EM_ASYNC_JS(void, WaitAnimationFrame, (), {
        await new Promise(function(resolve) { requestAnimationFrame(resolve); });
    });

    EM_JS(void, RestoreFromStorage, (const char* path), {
        try {
            var name = UTF8ToString(path);
            var text = window.localStorage.getItem('ZombieMongolia:' + name);
            if (text !== null) FS.writeFile(name, text);
        } catch (e) {}
    });

    EM_JS(void, PersistToStorage, (const char* path), {
        try {
            var name = UTF8ToString(path);
            window.localStorage.setItem('ZombieMongolia:' + name, FS.readFile(name, { encoding: 'utf8' }));
        } catch (e) {}
    });
}

bool AppShouldClose() { return false; }
void FrameYield() { WaitAnimationFrame(); }
void WebRestoreFile(const char* path) { RestoreFromStorage(path); }
void WebPersistFile(const char* path) { PersistToStorage(path); }

#else

bool AppShouldClose() { return WindowShouldClose(); }
void FrameYield() {}
void WebRestoreFile(const char*) {}
void WebPersistFile(const char*) {}

#endif
