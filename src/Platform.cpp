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

    EM_JS(int, IsCoarsePointer, (), {
        try { return window.matchMedia('(pointer: coarse)').matches ? 1 : 0; } catch (e) { return 0; }
    });

    // Хуудасны canvas дээрх идэвхтэй хүрэлтүүдийг хадгалж, canvas-ийн зургийн пиксел рүү хөрвүүлнэ.
    EM_JS(int, ReadCanvasTouches, (float* xy, int* ids, int capacity), {
        var canvas = Module.canvas;
        if (!canvas) return 0;
        if (!Module.zmTouches) {
            Module.zmTouches = [];
            var update = function(e) { Module.zmTouches = Array.prototype.slice.call(e.touches); };
            ['touchstart', 'touchmove', 'touchend', 'touchcancel'].forEach(function(type) {
                canvas.addEventListener(type, update, { passive: true });
            });
        }
        var list = Module.zmTouches;
        if (capacity <= 0) return list.length;
        var rect = canvas.getBoundingClientRect();
        var sx = rect.width > 0 ? canvas.width / rect.width : 1;
        var sy = rect.height > 0 ? canvas.height / rect.height : 1;
        var count = Math.min(list.length, capacity);
        for (var i = 0; i < count; i++) {
            HEAPF32[(xy >> 2) + 2 * i] = (list[i].clientX - rect.left) * sx;
            HEAPF32[(xy >> 2) + 2 * i + 1] = (list[i].clientY - rect.top) * sy;
            HEAP32[(ids >> 2) + i] = list[i].identifier;
        }
        return count;
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
bool WebIsTouchDevice() { return IsCoarsePointer() != 0; }
int WebReadTouches(float* xy, int* ids, int capacity) { return ReadCanvasTouches(xy, ids, capacity); }
void WebRestoreFile(const char* path) { RestoreFromStorage(path); }
void WebPersistFile(const char* path) { PersistToStorage(path); }

#else

bool AppShouldClose() { return WindowShouldClose(); }
void FrameYield() {}
bool WebIsTouchDevice() { return false; }
int WebReadTouches(float*, int*, int) { return 0; }
void WebRestoreFile(const char*) {}
void WebPersistFile(const char*) {}

#endif
