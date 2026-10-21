#include "Platform.h"

#include "raylib.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <emscripten/heap.h>

namespace
{
    // Хөтчийн дараагийн кадр хүртэл үндсэн урсгалыг түр зогсооно (ASYNCIFY). requestAnimationFrame ирэхгүй бол
    // (iPhone-ий Safari ачааллын төгсгөлд зарим үед, нуусан tab) 100 мс-ийн дараа таймераар үргэлжлүүлнэ:
    // эс бөгөөс тоглоом бүрмөсөн зогсдог байсан. Module.zmFrames: ?debug-ийн тоолуур (web/shell.html).
    EM_ASYNC_JS(void, WaitAnimationFrame, (), {
        var stats = Module.zmFrames || (Module.zmFrames = { raf: 0, timeout: 0 });
        await new Promise(function(resolve) {
            var done = false;
            requestAnimationFrame(function() { if (!done) { done = true; stats.raf++; resolve(); } });
            setTimeout(function() { if (!done) { done = true; stats.timeout++; resolve(); } }, 100);
        });
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

    EM_JS(int, PromptNumber, (const char* message), {
        var text = window.prompt(UTF8ToString(message), '');
        var value = parseInt((text || '').replace(/[^0-9]/g, ''), 10); // (EM_JS дотор "\" алга болдог)
        return isNaN(value) ? 0 : value;
    });

    // Түгжээ гаднаас (tab солих, ESC) алдагдсаныг тэмдэглэнэ; тоглоом өөрөө суллавал (EnableCursor ->
    // document.exitPointerLock) тооцохгүй. Анх удаа дуудахад сонсогчийг суулгана.
    EM_JS(int, TakePointerLockLost, (), {
        if (!Module.zmLockWatch) {
            Module.zmLockWatch = true;
            Module.zmLockLost = false;
            // iPhone-ий Safari зэрэгт pointer lock огт байхгүй: түгжээ алдагдах ч үгүй.
            if (typeof document.exitPointerLock !== 'function') return 0;
            var expected = false;
            var exit = document.exitPointerLock.bind(document);
            document.exitPointerLock = function() { if (document.pointerLockElement) expected = true; exit(); };
            document.addEventListener('pointerlockchange', function() {
                if (!document.pointerLockElement && !expected) Module.zmLockLost = true;
                expected = false;
            });
        }
        var lost = Module.zmLockLost;
        Module.zmLockLost = false;
        return lost ? 1 : 0;
    });

    EM_JS(void, HideLoader, (), {
        if (typeof Module.zmHideLoader === 'function') Module.zmHideLoader();
    });

    // Урьдчилан ачаалсан файлууд нь нэг том татсан buffer-ийн хэсгүүд: бүгдийг нь устгавал buffer бүхэлдээ
    // чөлөөлөгдөнө. Үлдэх файлуудыг тусдаа хуулбар болгож бичнэ.
    EM_JS(void, ReleaseLoadedAssets, (), {
        // (EM_JS дотор "\" тэмдэгт алга болдог тул regex биш, мөр харьцуулалт)
        function keep(path) {
            return path.indexOf('/assets/huds/') === 0 || path.indexOf('/assets/icons/') === 0 || path === '/assets/home_back.png' ||
                   path.indexOf('/assets/city/') === 0;
        }
        var removed = 0, kept = 0;
        function walk(dir) {
            FS.readdir(dir).forEach(function(name) {
                if (name === '.' || name === '..') return;
                var path = dir + '/' + name;
                if (FS.isDir(FS.stat(path).mode)) { walk(path); return; }
                if (keep(path)) {
                    var data = FS.readFile(path); // хуулбар
                    FS.unlink(path);
                    FS.writeFile(path, data);
                    kept++;
                } else {
                    FS.unlink(path);
                    removed++;
                }
            });
        }
        try {
            walk('/assets');
            try { FS.unlink('/ui_font.ttf'); removed++; } catch (e) {}
            out('INFO: WEB: released ' + removed + ' loaded asset files, kept ' + kept);
        } catch (e) {
            err('WARNING: WEB: could not release asset files: ' + e);
        }
    });

    EM_JS(void, LoadingFinished, (), {
        if (typeof Module.zmLoadingFinished === 'function') Module.zmLoadingFinished();
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
int WebPromptNumber(const char* message) { return PromptNumber(message); }
void WebHideLoader() { HideLoader(); }
void WebLoadingFinished() { LoadingFinished(); }
void WebReleaseLoadedAssets() { ReleaseLoadedAssets(); }
int WebHeapMegabytes() { return (int)(emscripten_get_heap_size() / (1024 * 1024)); }
bool WebPointerLockLost() { return TakePointerLockLost() != 0; }
void WebRestoreFile(const char* path) { RestoreFromStorage(path); }
void WebPersistFile(const char* path) { PersistToStorage(path); }

#else

bool AppShouldClose() { return WindowShouldClose(); }
void FrameYield() {}
bool WebIsTouchDevice() { return false; }
int WebReadTouches(float*, int*, int) { return 0; }
int WebPromptNumber(const char*) { return 0; }
void WebHideLoader() {}
void WebLoadingFinished() {}
void WebReleaseLoadedAssets() {}
int WebHeapMegabytes() { return 0; }
bool WebPointerLockLost() { return false; }
void WebRestoreFile(const char*) {}
void WebPersistFile(const char*) {}

#endif
