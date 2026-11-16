# Кодын монгол текстүүд (дэлгэцэнд гарах) ба англи орчуулга (src/lang/*.inc) байгаа эсэх.
#   python tools/lang_strings.py               -> файл бүрээр орчуулгагүй текстийн тоо
#   python tools/lang_strings.py src/Hud.cpp   -> тэр файлын орчуулгагүй текстүүд (C++-ийн бичлэгээр, .inc-д хуулахад)
# Текст: тайлбаргүй, зэргэлдээ "..." "..." нэгтгэсэн, TraceLog/printf-ийн лог биш. Түлхүүр нь C++-ийн бичлэгээрээ
# (\n, \xE2... хэвээр): .inc-д ижилээр бичвэл хөрвүүлэгч ижил байт гаргана.
import glob
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
CYRILLIC = re.compile('[Ѐ-ӿ]')
LOG_CALL = re.compile(r'\b(TraceLog|printf|fprintf|snprintf|TRACELOG|LogLine)\s*\($')


def literals(text):
    """(мөр, C++ бичлэг) — тайлбар алгасна, зэргэлдээ текстийг нэгтгэнэ, лог дуудлагынхыг хасна."""
    out = []
    i, n, line = 0, len(text), 1
    pending, pending_line, pending_log = None, 0, False
    call_stack = []  # нээлттэй хаалт бүрийн өмнөх үг (лог дуудлага эсэх)
    while i < n:
        c = text[i]
        if c == '\n':
            line += 1
            i += 1
            continue
        if text.startswith('//', i):
            j = text.find('\n', i)
            i = n if j < 0 else j
            continue
        if text.startswith('/*', i):
            j = text.find('*/', i + 2)
            line += text.count('\n', i, n if j < 0 else j)
            i = n if j < 0 else j + 2
            continue
        if c == "'" and not (i > 0 and (text[i - 1].isalnum() or text[i - 1] == '_')):  # (1'000 гэх мэт тоо биш)
            j = i + 1
            while j < n and text[j] != "'":
                j += 2 if text[j] == '\\' else 1
            i = j + 1
            continue
        if c == '"' and i > 0 and text[i - 1] == 'R':
            # Raw текст R"delim( ... )delim" (shader): алгасна
            open_paren = text.find('(', i)
            delim = text[i + 1:open_paren]
            end = text.find(')' + delim + '"', open_paren)
            end = n if end < 0 else end + len(delim) + 2
            line += text.count('\n', i, end)
            i = end
            continue
        if c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == '\\' else 1
            body = text[i + 1:j]
            in_log = any(call_stack)
            if pending is not None:
                pending += body
            else:
                pending, pending_line, pending_log = body, line, in_log
            i = j + 1
            # Дараагийн тэмдэгт (хоосон зайн дараа) бас текст бол нэгтгэнэ
            k = i
            while k < n and text[k] in ' \t\r\n':
                k += 1
            if k < n and text[k] == '"':
                line += text.count('\n', i, k)
                i = k
                continue
            if CYRILLIC.search(pending) and not pending_log:
                out.append((pending_line, pending))
            pending = None
            continue
        if c == '(':
            before = text[max(0, i - 40):i + 1]
            call_stack.append(bool(LOG_CALL.search(before)))
        elif c == ')' and call_stack:
            call_stack.pop()
        i += 1
    return out


def translated():
    keys = set()
    for path in glob.glob(os.path.join(ROOT, 'src', 'lang', '*.inc')):
        for _, body in literals(open(path, encoding='utf-8').read()):
            keys.add(body)
    return keys


def sources():
    files = glob.glob(os.path.join(ROOT, 'src', '*.cpp')) + glob.glob(os.path.join(ROOT, 'src', '*.h'))
    return sorted(f for f in files if not os.path.basename(f).startswith('Lang'))


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    done = translated()
    if len(sys.argv) > 1:
        seen = set()
        for path in sys.argv[1:]:
            for line, body in literals(open(path, encoding='utf-8').read()):
                if body in done or body in seen:
                    continue
                seen.add(body)
                print(f'{os.path.basename(path)}:{line}\t"{body}"')
        return
    total = 0
    for path in sources():
        missing = {b for _, b in literals(open(path, encoding='utf-8').read()) if b not in done}
        if missing:
            print(f'{len(missing):5d}  {os.path.basename(path)}')
            total += len(missing)
    print(f'{total:5d}  total untranslated')


if __name__ == '__main__':
    main()
