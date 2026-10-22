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

    // Утасны гар: хуудасны дээд талд нэг <input>. Товчны үйлдлийг raylib (window дээр сонсдог, Backspace-г
    // хориглодог) руу дамжуулахгүй. Хөтөч гарыг зөвхөн хэрэглэгчийн үйлдлээр нээдэг тул шууд focus хийхээс
    // гадна дараагийн touchend дээр дахин оролдоно.
    EM_JS(void, EditBegin, (const char* text, int kind, const char* placeholder, int last), {
        var input = Module.zmEdit;
        if (!input) {
            input = Module.zmEdit = document.createElement('input');
            input.autocomplete = 'off';
            input.setAttribute('autocorrect', 'off');
            input.setAttribute('autocapitalize', 'off');
            input.spellcheck = false;
            input.style.cssText = 'position:fixed;left:50%;top:10px;transform:translateX(-50%);width:min(92vw,560px);' +
                'z-index:20;box-sizing:border-box;padding:12px 16px;font-size:20px;border-radius:12px;' +
                'border:2px solid rgb(255,192,64);background:rgba(8,10,14,0.96);color:#fff;outline:none;display:none;' +
                'font-family:ZmUi,Segoe UI,Arial,sans-serif;';
            Module.zmEditState = -1;
            var stop = function(e) { e.stopPropagation(); };
            input.addEventListener('keydown', function(e) {
                e.stopPropagation();
                if (e.key === 'Enter') { e.preventDefault(); Module.zmEditState = 1; }
            });
            input.addEventListener('keyup', stop);
            input.addEventListener('keypress', stop);
            input.addEventListener('blur', function() {
                if (Module.zmEditWant) Module.zmEditState = -1;
                Module.zmEditWant = false;
                input.style.display = 'none';
            });
            document.addEventListener('touchend', function() {
                if (Module.zmEditWant && document.activeElement !== input) input.focus();
            }, true);
            (document.getElementById('stage') || document.body).appendChild(input); // бүтэн дэлгэцэд stage л харагдана
        }
        input.type = kind === 2 ? 'password' : kind === 1 ? 'tel' : 'text';
        input.inputMode = kind === 1 ? 'numeric' : 'text';
        input.maxLength = kind === 1 ? 8 : kind === 2 ? 32 : 30;
        input.placeholder = UTF8ToString(placeholder);
        input.enterKeyHint = last ? 'done' : 'next';
        input.value = UTF8ToString(text);
        input.style.display = 'block';
        Module.zmEditState = 0;
        Module.zmEditWant = true;
        try { input.focus(); input.setSelectionRange(input.value.length, input.value.length); } catch (e) {}
    });

    EM_JS(int, EditRead, (char* buffer, int capacity), {
        var input = Module.zmEdit;
        if (!input) return -1;
        stringToUTF8(input.value, buffer, capacity);
        var state = Module.zmEditState;
        if (state === 1) Module.zmEditState = 0;
        return state;
    });

    EM_JS(void, EditEnd, (), {
        var input = Module.zmEdit;
        if (!input) return;
        Module.zmEditWant = false;
        Module.zmEditState = -1;
        input.style.display = 'none';
        input.blur();
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
void WebEditBegin(const char* text, int kind, const char* placeholder, bool last) { EditBegin(text, kind, placeholder, last ? 1 : 0); }
int WebEditRead(char* buffer, int capacity) { return EditRead(buffer, capacity); }
void WebEditEnd() { EditEnd(); }
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
void WebEditBegin(const char*, int, const char*, bool) {}
int WebEditRead(char* buffer, int capacity)
{
    if (capacity > 0) buffer[0] = 0;
    return -1;
}
void WebEditEnd() {}
void WebHideLoader() {}
void WebLoadingFinished() {}
void WebReleaseLoadedAssets() {}
int WebHeapMegabytes() { return 0; }
bool WebPointerLockLost() { return false; }
void WebRestoreFile(const char*) {}
void WebPersistFile(const char*) {}

#endif
