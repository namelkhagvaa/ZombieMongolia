"""Тоглолтын техникийн лог: тоглоомын илгээсэн мөрүүд ба серверийн алхмууд (алдаа оношлоход).

Тоглоом (src/GameLog.cpp) тулаан, хамтын тоглолт, сүлжээний мөрүүд, анхааруулга, алдааг багцаар илгээнэ (api/logs/).
Давталтын алхам бүр "STEP: <нэр> ..." мөр: host_start, guest_joined, battle_start, result, loser_left, winner_continues,
stage_complete. Сервер өөрийн алхмуудыг (өрөө нээсэн, зочин орсон, үр дүн, хаагдсан) энд бичнэ. Өрөө, тулааны дугаараар
хост, зочин, сервер гурвын мөрийг нэг цагийн дарааллаар уншина: `manage.py game_log --room 146`, админы "Тоглолтын лог".
"""
import re
from datetime import datetime, timedelta
from datetime import timezone as dt_timezone

from django.db.models import Q
from django.utils import timezone

from .models import GameLog, Player, Room

KEEP_DAYS = 14
MAX_ENTRIES = 200            # нэг хүсэлтэд
MAX_PER_MINUTE = 1500        # нэг тоглогчоос минутад (буруу ажилласан тоглоом серверийг дүүргэхгүй)
PLATFORMS = ("windows", "android", "web", "linux", "mac")
STEP_RE = re.compile(r"STEP: ([a-z_]{2,24})")


class LogError(Exception):
    def __init__(self, message, status=400):
        super().__init__(message)
        self.status = status


def server(room, step, text, player=None):
    """Серверийн алхам (өрөө нээсэн, зочин орсон, үр дүн г.м.)."""
    GameLog.objects.create(player=player, room_id=room.pk if room else None, battle=room.battle if room else 0,
                           role=GameLog.SERVER, source="server", level="I", step=step[:24], text=str(text)[:500],
                           at=timezone.now())


def _text(value, limit):
    return value[:limit] if isinstance(value, str) else ""


def _int(value, low, high, default):
    return value if isinstance(value, int) and not isinstance(value, bool) and low <= value <= high else default


def store(player, payload):
    """Тоглоомын багц: {"now": мс, "platform", "build", "session", "entries": [{"t", "room", "battle", "role", "level",
    "text"}]}. Тоглоомын цагийг серверийн цагт тааруулна (утасны цаг зөрүүтэй байж болно). Хадгалсан тоог буцаана."""
    entries = payload.get("entries")
    if not isinstance(entries, list) or len(entries) > MAX_ENTRIES:
        raise LogError("Буруу хүсэлт")
    now = timezone.now()
    recent = GameLog.objects.filter(player=player, created__gte=now - timedelta(minutes=1)).count()
    if recent + len(entries) > MAX_PER_MINUTE:
        raise LogError("Хэт олон лог", 429)
    now_ms = int(now.timestamp() * 1000)
    client_now = _int(payload.get("now"), 0, 10 ** 14, 0)
    offset = now_ms - client_now if client_now else 0
    platform = payload.get("platform") if payload.get("platform") in PLATFORMS else ""
    build = _text(payload.get("build"), 20)
    session = _text(payload.get("session"), 12)
    rows = []
    for item in entries:
        if not isinstance(item, dict):
            continue
        text = _text(item.get("text"), 500).strip()
        if not text:
            continue
        stamp = _int(item.get("t"), 0, 10 ** 14, client_now) + offset
        if not now_ms - 3 * 86400 * 1000 <= stamp <= now_ms + 60 * 1000:
            stamp = now_ms  # (цаг эвдэрсэн)
        match = STEP_RE.match(text)
        rows.append(GameLog(player=player, room_id=_int(item.get("room"), 1, 2 ** 31 - 1, None),
                            battle=_int(item.get("battle"), 0, 10 ** 6, 0),
                            role=item.get("role") if item.get("role") in (GameLog.HOST, GameLog.GUEST) else "",
                            source=platform, build=build, session=session,
                            level=item.get("level") if item.get("level") in ("I", "W", "E") else "I",
                            step=match.group(1) if match else "", text=text,
                            at=datetime.fromtimestamp(stamp / 1000.0, tz=dt_timezone.utc)))
    GameLog.objects.bulk_create(rows)
    return len(rows)


def purge():
    """KEEP_DAYS-ээс хуучин лог устгана."""
    GameLog.objects.filter(created__lt=timezone.now() - timedelta(days=KEEP_DAYS)).delete()


def find_player(text):
    """Нэр, утас (сүүлийн оронгууд) эсвэл id-аар тоглогч."""
    text = (text or "").strip()
    if not text:
        return None
    if text.isdigit() and len(text) < 7:
        return Player.objects.filter(pk=int(text)).first()
    return (Player.objects.filter(name=text).first() or Player.objects.filter(phone__endswith=text).first()
            or Player.objects.filter(name__icontains=text).first())


def query(room=None, player=None, hours=6, steps_only=False, limit=5000):
    """Өрөө (ба серверийн мөр), эсвэл тоглогчийн (ба түүний орсон өрөөнүүдийн серверийн мөр) лог, цагийн дарааллаар."""
    since = timezone.now() - timedelta(hours=hours)
    qs = GameLog.objects.select_related("player").filter(at__gte=since)
    if room:
        qs = qs.filter(room_id=room)
    elif player:
        rooms = set(GameLog.objects.filter(player=player, at__gte=since, room_id__isnull=False)
                    .values_list("room_id", flat=True))
        qs = qs.filter(Q(player=player) | Q(role=GameLog.SERVER, room_id__in=rooms))
    if steps_only:
        qs = qs.exclude(step="")
    rows = list(qs.order_by("-at", "-id")[:limit])
    rows.reverse()
    return rows


ROLE_NAMES = {GameLog.HOST: "ХОСТ", GameLog.GUEST: "ЗОЧИН", GameLog.SERVER: "СЕРВЕР", "": "ГАНЦААРАА"}


def describe(row):
    who = ROLE_NAMES.get(row.role, row.role)
    if row.role != GameLog.SERVER:
        device = " ".join(part for part in (row.source, row.build) if part)
        who += f" {row.player.name if row.player else '?'}" + (f" ({device})" if device else "")
    return who


def format_lines(rows):
    """Нэгтгэсэн цагийн хуваарь (текст): өрөө солигдох бүрд гарчиг (тулааны дугаар серверийн мөрөнд: тоглоом түүнийг
    хожуу мэддэг); алхам ◆, анхааруулга !, алдаа ✗."""
    lines = []
    current = None
    rooms = {room.pk: room for room in Room.objects.filter(pk__in={r.room_id for r in rows if r.room_id}).select_related("host", "guest")}
    for row in rows:
        key = row.room_id
        if key != current:
            current = key
            if row.room_id:
                room = rooms.get(row.room_id)
                title = f"Өрөө {row.room_id}"
                if room:
                    title += (f" · аймаг {room.province + 1} · одоогийн хост {room.host.name}"
                              + (f" · зочин {room.guest.name}" if room.guest_id else "")
                              + (f" · хаагдсан ({room.closed_reason})" if room.status == Room.CLOSED else ""))
                lines.append(f"== {title}")
            else:
                lines.append("== Өрөөгүй")
        mark = "◆" if row.step else "✗" if row.level == "E" else "!" if row.level == "W" else " "
        text = f"[тулаан {row.battle}] {row.text}" if row.role == GameLog.SERVER and row.battle else row.text
        lines.append(f"{timezone.localtime(row.at):%m-%d %H:%M:%S} {mark} {describe(row):<34} {text}")
    return "\n".join(lines)


def row_json(row):
    return {"id": row.id, "at": row.at.isoformat(), "room": row.room_id, "battle": row.battle, "role": row.role,
            "who": describe(row), "player": row.player_id, "level": row.level, "step": row.step, "text": row.text}
