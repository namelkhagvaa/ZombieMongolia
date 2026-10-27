#include "Platform.h"

#include "raylib.h"

#include <string>
#include <thread>

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

    // Утасны гар: тоглоомын талбар бүрийн дээр үл үзэгдэх <input> (src/Account.cpp FieldBox байрлуулна).
    // Товчны үйлдлийг raylib руу дамжуулахгүй (stopPropagation).
    EM_JS(void, FieldPlace, (int slot, float fx, float fy, float fw, float fh, int kind, int next, const char* value), {
        var F = Module.zmFields || (Module.zmFields = { list: [], placed: [], enter: false });
        var input = F.list[slot];
        if (!input) {
            input = F.list[slot] = document.createElement('input');
            input.autocomplete = 'off';
            input.setAttribute('autocorrect', 'off');
            input.setAttribute('autocapitalize', 'off');
            input.spellcheck = false;
            // 16px: iPhone талбарт хүрэхэд хуудсыг томруулдаггүй
            input.style.cssText = 'position:fixed;margin:0;padding:0;border:0;outline:none;background:transparent;' +
                'color:transparent;caret-color:transparent;opacity:0.01;font-size:16px;z-index:4;display:none;' +
                '-webkit-tap-highlight-color:transparent;-webkit-user-select:text;user-select:text;touch-action:none;';
            input.zmKind = -1;
            input.addEventListener('keydown', function(e) {
                e.stopPropagation();
                if (e.key === 'Enter') {
                    e.preventDefault();
                    var target = input.zmNext >= 0 ? F.list[input.zmNext] : null;
                    if (target && target.style.display !== 'none') target.focus({ preventScroll: true });
                    else { F.enter = true; input.blur(); }
                }
                // raylib-ийн (GLFW) window дээрх capture сонсогч Backspace-ийн үндсэн үйлдлийг хориглодог. Зарим
                // хөтөч (Android) утасны гарын устгалыг ч гэсэн хийдэг, зарим нь (компьютер) хийдэггүй: утга
                // өөрчлөгдөөгүй бол л өөрсдөө устгана (эс бөгөөс нэг дарахад хоёр тэмдэгт устдаг байсан).
                if ((e.key === 'Backspace' || e.keyCode === 8) && e.defaultPrevented) {
                    var before = input.value, from = input.selectionStart, to = input.selectionEnd;
                    setTimeout(function() {
                        if (input.value !== before) return;
                        var v = input.value;
                        if (from === null || to === null) { from = to = v.length; }
                        if (from === to && from > 0) {
                            from--;
                            var c = v.charCodeAt(from);
                            if (from > 0 && c >= 0xDC00 && c <= 0xDFFF) from--;
                        }
                        input.value = v.slice(0, from) + v.slice(to);
                        try { input.setSelectionRange(from, from); } catch (err) {}
                    }, 40);
                }
            });
            var stop = function(e) { e.stopPropagation(); };
            input.addEventListener('keyup', stop);
            input.addEventListener('keypress', stop);
            (document.getElementById('stage') || document.body).appendChild(input); // бүтэн дэлгэцэд stage л харагдана
        }
        if (input.zmKind !== kind) {
            input.zmKind = kind;
            // Бүгд энгийн текст талбар: password / tel төрөл Chrome-ийн автоматаар бөглөх (нууц үг, карт, хаяг)
            // цагаан мөрийг гарын дээр гаргадаг. Талбар үл үзэгдэх тул нууц үгийг тоглоом өөрөө "•"-ээр харуулна.
            input.type = 'text';
            input.inputMode = kind === 1 || kind === 3 ? 'numeric' : 'text';
            input.name = 'zm-field-' + slot;
            input.maxLength = kind === 1 ? 8 : kind === 3 ? 20 : kind === 2 ? 32 : kind === 4 ? 120 : 30; // 3: дансны дугаар, дүн; 4: чат
        }
        input.zmNext = next;
        input.enterKeyHint = next >= 0 ? 'next' : 'done';
        var r = Module.canvas.getBoundingClientRect();
        input.style.left = (r.left + fx * r.width) + 'px';
        input.style.top = (r.top + fy * r.height) + 'px';
        input.style.width = (fw * r.width) + 'px';
        input.style.height = (fh * r.height) + 'px';
        if (document.activeElement !== input) {
            var text = UTF8ToString(value);
            if (input.value !== text) input.value = text;
        }
        input.style.display = 'block';
        F.placed[slot] = true;
    });

    EM_JS(void, FieldsEnd, (), {
        var F = Module.zmFields;
        if (!F) return;
        for (var i = 0; i < F.list.length; i++) {
            var input = F.list[i];
            if (!input || F.placed[i]) continue;
            if (document.activeElement === input) input.blur();
            input.style.display = 'none';
        }
        F.placed = [];
    });

    EM_JS(int, FieldPoll, (char* buffer, int capacity, int* enter), {
        var F = Module.zmFields;
        HEAP32[enter >> 2] = F && F.enter ? 1 : 0;
        if (!F) return -1;
        F.enter = false;
        for (var i = 0; i < F.list.length; i++) {
            if (F.list[i] && document.activeElement === F.list[i]) {
                stringToUTF8(F.list[i].value, buffer, capacity);
                return i;
            }
        }
        return -1;
    });

    EM_JS(void, FieldBlur, (), {
        var F = Module.zmFields;
        if (F && F.list.indexOf(document.activeElement) >= 0) document.activeElement.blur();
    });

    EM_JS(float, VisibleFraction, (), {
        var v = window.visualViewport;
        if (!v || !window.innerHeight) return 1;
        return Math.max(0.2, Math.min(1, (v.height + v.offsetTop) / window.innerHeight));
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
    EM_JS(void, ReleaseLoadedAssets, (int world), {
        // (EM_JS дотор "\" тэмдэгт алга болдог тул regex биш, мөр харьцуулалт)
        function keep(path) {
            return path.indexOf('/assets/huds/') === 0 || path.indexOf('/assets/icons/') === 0 || path === '/assets/home_back.png' || path === '/assets/home_back.jpg' ||
                   path === '/assets/splash.jpg' || path.slice(-8) === '_far.bin' || // (холын мод: газар нутаг бүрд уншина)
                   path.indexOf('/assets/city/') === 0 ||
                   (!world && (path.indexOf('/assets/cars/') === 0 || path.indexOf('/assets/trees/') === 0 ||
                               path.indexOf('/assets/pooshig/') === 0));
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

    // web/shell.html-ийн багц татагч (zmPacks): эхлүүлээд явцыг асууна
    EM_JS(int, PackStatus, (const char* name), {
        return typeof Module.zmPackStatus === 'function' ? Module.zmPackStatus(UTF8ToString(name)) : 1000;
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

    EM_JS(void, SessionStore, (const char* key, const char* value), {
        try {
            var k = 'ZombieMongolia:' + UTF8ToString(key), v = UTF8ToString(value);
            if (v) window.sessionStorage.setItem(k, v);
            else window.sessionStorage.removeItem(k);
        } catch (e) {}
    });

    EM_JS(void, SessionLoad, (const char* key, char* out, int capacity), {
        var v = '';
        try { v = window.sessionStorage.getItem('ZombieMongolia:' + UTF8ToString(key)) || ''; } catch (e) {}
        stringToUTF8(v, out, capacity);
    });

    EM_JS(double, NowMs, (), { return Date.now(); });

    EM_JS(void, PersistToStorage, (const char* path), {
        try {
            var name = UTF8ToString(path);
            window.localStorage.setItem('ZombieMongolia:' + name, FS.readFile(name, { encoding: 'utf8' }));
        } catch (e) {}
    });
}

namespace
{
    // Хөтчийн бодит видео карт (WEBGL_debug_renderer_info): тусдаа canvas дээр асууна. Зарим хөтөч нуудаг.
    EM_JS(void, ReadGpuName, (char* out, int capacity), {
        var name = '';
        try {
            var gl = document.createElement('canvas').getContext('webgl2') || document.createElement('canvas').getContext('webgl');
            var info = gl && gl.getExtension('WEBGL_debug_renderer_info');
            name = info ? gl.getParameter(info.UNMASKED_RENDERER_WEBGL) : (gl ? gl.getParameter(gl.RENDERER) : '');
        } catch (e) {}
        stringToUTF8(String(name || ''), out, capacity);
    });

    EM_JS(int, ReadCpuCores, (), { return navigator.hardwareConcurrency || 2; });
}

const char* GpuName()
{
    static char name[256] = {};
    if (!name[0]) ReadGpuName(name, (int)sizeof(name));
    return name;
}
int CpuCores() { return ReadCpuCores(); }

bool AppShouldClose() { return false; }
void FrameYield() { WaitAnimationFrame(); }
bool WebIsTouchDevice() { return IsCoarsePointer() != 0; }
int WebReadTouches(float* xy, int* ids, int capacity) { return ReadCanvasTouches(xy, ids, capacity); }
int WebPromptNumber(const char* message) { return PromptNumber(message); }
void WebFieldPlace(int slot, float x, float y, float width, float height, int kind, int next, const char* value)
{
    FieldPlace(slot, x, y, width, height, kind, next, value);
}
void WebFieldsEnd() { FieldsEnd(); }
int WebFieldPoll(char* buffer, int capacity, bool& enter)
{
    int pressed = 0;
    const int slot = FieldPoll(buffer, capacity, &pressed);
    enter = pressed != 0;
    return slot;
}
void WebFieldBlur() { FieldBlur(); }
float WebVisibleFraction() { return VisibleFraction(); }
void WebHideLoader() { HideLoader(); }
void WebLoadingFinished() { LoadingFinished(); }
void WebReleaseLoadedAssets(bool world) { ReleaseLoadedAssets(world ? 1 : 0); }
int WebPackStatus(const char* name) { return PackStatus(name); }
int WebHeapMegabytes() { return (int)(emscripten_get_heap_size() / (1024 * 1024)); }
bool WebPointerLockLost() { return TakePointerLockLost() != 0; }
void WebRestoreFile(const char* path) { RestoreFromStorage(path); }
void WebPersistFile(const char* path) { PersistToStorage(path); }
void WebSessionStore(const char* key, const char* value) { SessionStore(key, value); }
std::string WebSessionLoad(const char* key)
{
    char buffer[1024] = {};
    SessionLoad(key, buffer, (int)sizeof(buffer));
    return buffer;
}
double WebNowMs() { return NowMs(); }

#else

// OpenGL 1.1-ийн функц (Windows: opengl32.dll, Android: GLES) — raylib-ийн glad-ийг шаардахгүй
#if defined(_WIN32)
extern "C" __declspec(dllimport) const unsigned char* __stdcall glGetString(unsigned int name);
#else
extern "C" const unsigned char* glGetString(unsigned int name);
#endif

const char* GpuName()
{
    static std::string name;
    if (name.empty())
    {
        const unsigned char* renderer = glGetString(0x1F01); // GL_RENDERER
        name = renderer ? (const char*)renderer : "";
    }
    return name.c_str();
}
int CpuCores() { return (int)std::thread::hardware_concurrency(); }

bool AppShouldClose() { return WindowShouldClose(); }
void FrameYield() {}
bool WebIsTouchDevice() { return false; }
int WebReadTouches(float*, int*, int) { return 0; }
int WebPromptNumber(const char*) { return 0; }
void WebFieldPlace(int, float, float, float, float, int, int, const char*) {}
void WebFieldsEnd() {}
int WebFieldPoll(char* buffer, int capacity, bool& enter)
{
    if (capacity > 0) buffer[0] = 0;
    enter = false;
    return -1;
}
void WebFieldBlur() {}
float WebVisibleFraction() { return 1.0f; }
void WebHideLoader() {}
void WebLoadingFinished() {}
void WebReleaseLoadedAssets(bool) {}
int WebPackStatus(const char*) { return 1000; }
int WebHeapMegabytes() { return 0; }
bool WebPointerLockLost() { return false; }
void WebRestoreFile(const char*) {}
void WebPersistFile(const char*) {}
void WebSessionStore(const char*, const char*) {}
std::string WebSessionLoad(const char*) { return std::string(); }
double WebNowMs() { return 0.0; }

#endif

std::string ImageFile(const std::string& pngPath)
{
#if defined(__ANDROID__)
    return pngPath; // APK-д PNG хэвээр (FileExists нь APK-ийн assets-ийг хардаггүй)
#else
    const size_t n = pngPath.size();
    if (n < 4 || pngPath.compare(n - 4, 4, ".png") != 0 || FileExists(pngPath.c_str())) return pngPath;
    const std::string jpg = pngPath.substr(0, n - 4) + ".jpg";
    return FileExists(jpg.c_str()) ? jpg : pngPath;
#endif
}
